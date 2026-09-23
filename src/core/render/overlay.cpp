#include "overlay.hpp"

#include "../util/file_util.hpp"
#include "../util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

// stb_truetype — той самий, що в Dear ImGui (third_party/imgui); статичний, щоб не
// конфліктувати з копією всередині ImGui
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace gmdr::render {

namespace fs = std::filesystem;

namespace {
constexpr double kMergeGap = 0.6;    // як у субтитрах: коротші паузи — та сама репліка
constexpr double kMinLength = 0.25;  // коротші уривки не показуємо
constexpr double kFadeIn = 0.12, kFadeOut = 0.35;
constexpr int    kMaxVisible = 6;

std::vector<uint32_t> utf8_codepoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        int n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

// RGB (0..255) -> Y'CbCr кадру (матриця і діапазон — як у самого кадру)
void rgb_to_yuv(const frames::Image& img, double r, double g, double b, double& y, double& u, double& v) {
    const double kr = img.bt709 ? 0.2126 : 0.299, kb = img.bt709 ? 0.0722 : 0.114, kg = 1.0 - kr - kb;
    const double Y = kr * r + kg * g + kb * b;
    const double Cb = (b - Y) / (2.0 * (1.0 - kb)), Cr = (r - Y) / (2.0 * (1.0 - kr));
    if (img.full_range) {
        y = Y;
        u = 128.0 + Cb;
        v = 128.0 + Cr;
    } else {
        y = 16.0 + Y * 219.0 / 255.0;
        u = 128.0 + Cb * 224.0 / 255.0;
        v = 128.0 + Cr * 224.0 / 255.0;
    }
}
} // namespace

std::vector<SpeakerOverlay::Speaker> SpeakerOverlay::speakers_for(const std::vector<SpeakerSubtitleSource>& src,
                                                                   int64_t origin_sample, double duration, double delay,
                                                                   double speed) {
    std::vector<Speaker> out;
    for (const auto& s : src) {
        if (!s.track) continue;
        Speaker sp;
        sp.name = s.name;
        double ca = -1, cb = -1;
        auto flush = [&] {
            if (ca < 0) return;
            const double a = std::max(0.0, ca), b = std::min(duration, cb);
            if (b - a >= kMinLength) sp.spans.push_back({a, b});
            ca = cb = -1;
        };
        for (const auto& seg : s.track->segments) {
            const double a = ((seg.start - origin_sample) / static_cast<double>(voice::kVoiceRate) + delay) / speed;
            const double b = ((seg.end() - origin_sample) / static_cast<double>(voice::kVoiceRate) + delay) / speed;
            if (b <= 0 || a >= duration) continue;
            if (ca >= 0 && a - cb < kMergeGap) {
                cb = std::max(cb, b);
            } else {
                flush();
                ca = a;
                cb = b;
            }
        }
        flush();
        if (!sp.spans.empty()) out.push_back(std::move(sp));
    }
    return out;
}

std::string SpeakerOverlay::find_font() {
    std::vector<fs::path> candidates;
#ifdef _WIN32
    fs::path dir = "C:\\Windows\\Fonts";
    if (const char* w = std::getenv("WINDIR"); w && *w) dir = fs::path(w) / "Fonts";
    for (const char* f : {"segoeuib.ttf", "arialbd.ttf", "segoeui.ttf", "arial.ttf", "tahoma.ttf"}) candidates.push_back(dir / f);
#else
    for (const char* f : {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                          "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                          "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
                          "/usr/share/fonts/noto/NotoSans-Bold.ttf"})
        candidates.push_back(f);
#endif
    std::error_code ec;
    for (const auto& c : candidates)
        if (fs::exists(c, ec)) return path_to_utf8(c);
    return {};
}

bool SpeakerOverlay::init(std::vector<Speaker> speakers, int frame_w, int frame_h, const std::string& font_path,
                          std::string* error) {
    speakers_ = std::move(speakers);
    labels_.clear();
    frame_w_ = frame_w;
    frame_h_ = frame_h;
    if (speakers_.empty()) {
        if (error) *error = "у фрагменті ніхто не говорить";
        return false;
    }
    auto font_data = read_file_bytes(path_from_utf8(font_path));
    stbtt_fontinfo font{};
    if (!font_data || !stbtt_InitFont(&font, font_data->data(), stbtt_GetFontOffsetForIndex(font_data->data(), 0))) {
        if (error) *error = "не вдалося прочитати шрифт " + font_path;
        return false;
    }
    const int px = std::max(12, static_cast<int>(std::lround(frame_h * 0.028)));   // 1080p -> 30 px
    const float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(px));
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    const int baseline = static_cast<int>(std::lround(ascent * scale));
    const int text_h = static_cast<int>(std::lround((ascent - descent) * scale));
    const int pad_x = std::max(4, px * 11 / 20), pad_y = std::max(2, px * 3 / 10), accent = std::max(3, px / 4);
    margin_ = std::max(8, static_cast<int>(std::lround(frame_h * 0.03)));
    gap_ = std::max(2, px / 4);

    for (const auto& sp : speakers_) {
        // Надто довгі ніки обрізаємо
        auto cps = utf8_codepoints(sp.name);
        if (cps.size() > 28) {
            cps.resize(27);
            cps.push_back(0x2026);   // …
        }
        // Ширина тексту
        int text_w = 0;
        for (size_t i = 0; i < cps.size(); ++i) {
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&font, static_cast<int>(cps[i]), &adv, &lsb);
            text_w += static_cast<int>(std::lround(adv * scale));
            if (i + 1 < cps.size())
                text_w += static_cast<int>(std::lround(
                    stbtt_GetCodepointKernAdvance(&font, static_cast<int>(cps[i]), static_cast<int>(cps[i + 1])) * scale));
        }
        Label l;
        l.w = accent + pad_x + text_w + pad_x;
        l.h = text_h + pad_y * 2;
        // Маска тексту (гліфи можуть перекриватися — беремо максимум)
        std::vector<uint8_t> mask(static_cast<size_t>(l.w) * l.h, 0), glyph;
        int pen = accent + pad_x;
        for (size_t i = 0; i < cps.size(); ++i) {
            const int cp = static_cast<int>(cps[i]);
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&font, cp, scale, scale, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                glyph.assign(static_cast<size_t>(gw) * gh, 0);
                stbtt_MakeCodepointBitmap(&font, glyph.data(), gw, gh, gw, scale, scale, cp);
                for (int yy = 0; yy < gh; ++yy)
                    for (int xx = 0; xx < gw; ++xx) {
                        const int X = pen + x0 + xx, Y = pad_y + baseline + y0 + yy;
                        if (X < 0 || Y < 0 || X >= l.w || Y >= l.h) continue;
                        uint8_t& m = mask[static_cast<size_t>(Y) * l.w + X];
                        m = std::max(m, glyph[static_cast<size_t>(yy) * gw + xx]);
                    }
            }
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
            pen += static_cast<int>(std::lround(adv * scale));
            if (i + 1 < cps.size())
                pen += static_cast<int>(std::lround(stbtt_GetCodepointKernAdvance(&font, cp, static_cast<int>(cps[i + 1])) * scale));
        }
        // Плашка: темний фон із заокругленими кутами, зелена позначка ліворуч, білий текст
        l.rgba.assign(static_cast<size_t>(l.w) * l.h * 4, 0);
        const double radius = std::max(2.0, px * 0.22);
        for (int y = 0; y < l.h; ++y)
            for (int x = 0; x < l.w; ++x) {
                // Кути: відстань до центру кола заокруглення
                const double cx = x + 0.5 < radius ? radius : (x + 0.5 > l.w - radius ? l.w - radius : x + 0.5);
                const double cy = y + 0.5 < radius ? radius : (y + 0.5 > l.h - radius ? l.h - radius : y + 0.5);
                const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
                const double shape = std::clamp(radius + 0.5 - d, 0.0, 1.0);
                const bool is_accent = x < accent;
                double r = is_accent ? 70 : 16, g = is_accent ? 205 : 18, b = is_accent ? 105 : 22;
                double a = (is_accent ? 1.0 : 0.66) * shape;
                const double m = mask[static_cast<size_t>(y) * l.w + x] / 255.0;
                if (m > 0) {   // білий текст поверх фону
                    const double oa = m + a * (1 - m);
                    r = (255 * m + r * a * (1 - m)) / oa;
                    g = (255 * m + g * a * (1 - m)) / oa;
                    b = (255 * m + b * a * (1 - m)) / oa;
                    a = oa;
                }
                uint8_t* o = &l.rgba[(static_cast<size_t>(y) * l.w + x) * 4];
                o[0] = static_cast<uint8_t>(std::lround(r));
                o[1] = static_cast<uint8_t>(std::lround(g));
                o[2] = static_cast<uint8_t>(std::lround(b));
                o[3] = static_cast<uint8_t>(std::lround(a * 255));
            }
        labels_.push_back(std::move(l));
    }
    return true;
}

void SpeakerOverlay::draw(frames::Image& img, double t) const {
    if (labels_.empty() || img.empty()) return;
    // Хто зараз говорить (з плавною появою і зникненням) і з якого моменту
    struct Active {
        size_t idx;
        double since, alpha;
    };
    std::vector<Active> active;
    for (size_t i = 0; i < speakers_.size(); ++i)
        for (const auto& s : speakers_[i].spans) {
            if (t < s.a || t > s.b + kFadeOut) continue;
            double a = std::min(1.0, (t - s.a) / kFadeIn);
            if (t > s.b) a *= 1.0 - (t - s.b) / kFadeOut;
            if (a > 0.004) active.push_back({i, s.a, a});
            break;
        }
    if (active.empty()) return;
    std::sort(active.begin(), active.end(), [](const Active& x, const Active& y) { return x.since < y.since; });
    if (active.size() > kMaxVisible) active.erase(active.begin(), active.end() - kMaxVisible);

    // Розмір кадру може відрізнятися від очікуваного (обмеження монітора) — масштаб позицій
    const double k = frame_h_ > 0 ? static_cast<double>(img.height) / frame_h_ : 1.0;
    const frames::LayoutInfo li = frames::layout_info(img.layout);
    int bottom = img.height - static_cast<int>(std::lround(img.height * 0.2));   // над лічильником набоїв
    for (const auto& act : active) {
        const Label& l = labels_[act.idx];
        const int x0 = img.width - static_cast<int>(std::lround(margin_ * k)) - l.w;
        const int y0 = bottom - l.h;
        bottom = y0 - gap_;
        for (int p = 0; p < img.planes(); ++p) {
            const int sw = p > 0 && li.yuv ? li.log2_chroma_w : 0, sh = p > 0 && li.yuv ? li.log2_chroma_h : 0;
            const int px0 = std::max(0, x0) >> sw, px1 = std::min(img.width, x0 + l.w) >> sw;
            const int py0 = std::max(0, y0) >> sh, py1 = std::min(img.height, y0 + l.h) >> sh;
            for (int py = py0; py < py1; ++py) {
                uint8_t* row = img.row(p, py);
                for (int pxx = px0; pxx < px1; ++pxx) {
                    const int lx = (pxx << sw) - x0, ly = (py << sh) - y0;
                    if (lx < 0 || ly < 0 || lx >= l.w || ly >= l.h) continue;
                    const uint8_t* c = &l.rgba[(static_cast<size_t>(ly) * l.w + lx) * 4];
                    const double a = c[3] / 255.0 * act.alpha;
                    if (a <= 0) continue;
                    if (!li.yuv) {
                        // BGR / BGRA / BGR48: порядок B, G, R
                        const double comp[3] = {static_cast<double>(c[2]), static_cast<double>(c[1]), static_cast<double>(c[0])};
                        for (int ch = 0; ch < 3; ++ch) {
                            if (li.high_depth) {
                                uint16_t* v = reinterpret_cast<uint16_t*>(row) + pxx * 3 + ch;
                                *v = static_cast<uint16_t>(std::lround(*v * (1 - a) + comp[ch] * 257.0 * a));
                            } else {
                                uint8_t* v = row + pxx * li.bytes_per_pixel + ch;
                                *v = static_cast<uint8_t>(std::lround(*v * (1 - a) + comp[ch] * a));
                            }
                        }
                    } else {
                        double yv, uv, vv;
                        rgb_to_yuv(img, c[0], c[1], c[2], yv, uv, vv);
                        const double target = p == 0 ? yv : p == 1 ? uv : vv;
                        if (li.high_depth) {
                            uint16_t* v = reinterpret_cast<uint16_t*>(row) + pxx;
                            *v = static_cast<uint16_t>(std::lround(*v * (1 - a) + target * 257.0 * a));
                        } else {
                            uint8_t* v = row + pxx;
                            *v = static_cast<uint8_t>(std::lround(*v * (1 - a) + target * a));
                        }
                    }
                }
            }
        }
    }
}

} // namespace gmdr::render
