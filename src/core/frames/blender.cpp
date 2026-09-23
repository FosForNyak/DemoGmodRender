#include "blender.hpp"
#include "../util/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace gmdr::frames {

MotionBlender::MotionBlender(int samples, double shutter, bool high_depth, ThreadPool* pool)
    : samples_(std::clamp(samples, 1, 256)), high_depth_(high_depth), pool_(pool) {
    shutter = std::clamp(shutter, 1.0, 360.0);
    used_ = std::clamp(static_cast<int>(std::lround(samples_ * shutter / 360.0)), 1, samples_);
}

std::optional<Image> MotionBlender::push(Image&& sub) {
    if (samples_ == 1) return std::move(sub);   // без розмиття — кадр як є
    if (layout_info(sub.layout).high_depth)
        throw std::runtime_error(tr("Motion blur: 16-бітні під-кадри не підтримуються"));

    const bool yuv = is_yuv(sub.layout);
    if (in_group_ == 0) {
        width_ = sub.width;
        height_ = sub.height;
        in_layout_ = sub.layout;
        full_range_ = sub.full_range;
        bt709_ = sub.bt709;
        first_index_ = sub.index;
        accumulated_ = 0;
        const int planes = yuv ? 3 : 1;
        for (int p = 0; p < 3; ++p) {
            if (p >= planes) {
                acc_[p].clear();
                acc_width_[p] = acc_height_[p] = 0;
                continue;
            }
            acc_width_[p] = yuv ? sub.plane_width(p) : width_ * 3;
            acc_height_[p] = sub.plane_height(p);
            acc_[p].assign(static_cast<size_t>(acc_width_[p]) * static_cast<size_t>(acc_height_[p]), 0);
        }
    }
    if (sub.width != width_ || sub.height != height_ || is_yuv(in_layout_) != yuv ||
        (yuv && sub.layout != in_layout_))
        throw std::runtime_error(tr("Motion blur: розмір або формат кадрів змінився посеред запису"));

    if (in_group_ < used_) {
        const int bpp = bytes_per_pixel(sub.layout);
        const int planes = yuv ? 3 : 1;
        for (int p = 0; p < planes; ++p) {
            const int aw = acc_width_[p];
            auto work = [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    const uint8_t* s = sub.row(p, y);
                    uint16_t* a = acc_[p].data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
                    if (yuv || bpp == 3) {
                        for (int i = 0; i < aw; ++i) a[i] = static_cast<uint16_t>(a[i] + s[i]);
                    } else {
                        for (int x = 0; x < width_; ++x) {
                            a[x * 3 + 0] = static_cast<uint16_t>(a[x * 3 + 0] + s[x * bpp + 0]);
                            a[x * 3 + 1] = static_cast<uint16_t>(a[x * 3 + 1] + s[x * bpp + 1]);
                            a[x * 3 + 2] = static_cast<uint16_t>(a[x * 3 + 2] + s[x * bpp + 2]);
                        }
                    }
                }
            };
            if (pool_) pool_->parallel_for(0, acc_height_[p], work, 32);
            else work(0, acc_height_[p]);
        }
        ++accumulated_;
    }
    ++in_group_;
    if (in_group_ >= samples_) return finish_group();
    return std::nullopt;
}

std::optional<Image> MotionBlender::flush() {
    if (samples_ == 1 || in_group_ == 0 || accumulated_ == 0) {
        in_group_ = 0;
        return std::nullopt;
    }
    return finish_group();
}

Image MotionBlender::finish_group() {
    Image out;
    const bool yuv = is_yuv(in_layout_);
    const PixelLayout base = yuv ? in_layout_ : PixelLayout::BGR24;
    out.allocate(width_, height_, high_depth_ ? high_depth_layout(base) : low_depth_layout(base));
    out.index = first_index_ >= 0 ? first_index_ / samples_ : -1;
    out.full_range = full_range_;
    out.bt709 = bt709_;
    const int count = std::max(1, accumulated_);
    const int planes = yuv ? 3 : 1;
    for (int p = 0; p < planes; ++p) {
        const int aw = acc_width_[p];
        std::function<void(int, int)> work;
        if (high_depth_) {
            const float scale = 65535.0f / (255.0f * static_cast<float>(count));
            work = [&, p, aw, scale](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    const uint16_t* a = acc_[p].data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
                    uint16_t* o = reinterpret_cast<uint16_t*>(out.row(p, y));
                    for (int i = 0; i < aw; ++i) o[i] = static_cast<uint16_t>(static_cast<float>(a[i]) * scale + 0.5f);
                }
            };
        } else {
            // Ділення через множення на обернене число (з округленням)
            const uint32_t inv =
                static_cast<uint32_t>((65536u + static_cast<uint32_t>(count) / 2) / static_cast<uint32_t>(count));
            const bool exact = (count & (count - 1)) == 0;
            int shift = 0;
            while ((1 << shift) < count) ++shift;
            work = [&, p, aw, inv, exact, shift](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    const uint16_t* a = acc_[p].data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
                    uint8_t* o = out.row(p, y);
                    if (exact) {
                        const uint32_t half = shift ? (1u << (shift - 1)) : 0;
                        for (int i = 0; i < aw; ++i) o[i] = static_cast<uint8_t>(std::min<uint32_t>(255, (a[i] + half) >> shift));
                    } else {
                        for (int i = 0; i < aw; ++i)
                            o[i] = static_cast<uint8_t>(std::min<uint32_t>(255, (a[i] * inv + 32768u) >> 16));
                    }
                }
            };
        }
        if (pool_) pool_->parallel_for(0, acc_height_[p], work, 32);
        else work(0, acc_height_[p]);
    }
    in_group_ = 0;
    accumulated_ = 0;
    return out;
}

} // namespace gmdr::frames
