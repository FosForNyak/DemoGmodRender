#include "image.hpp"

#include <algorithm>
#include <mutex>

namespace gmdr::frames {

namespace {
// Пул великих буферів. Менші за kMinPooled не зберігаємо — їх дешево виділити.
constexpr size_t kMinPooled = 1u << 20;
constexpr size_t kMaxPooledBytes = 640ull << 20;
constexpr size_t kMaxPooledCount = 32;

struct BufferPool {
    std::mutex              mutex;
    std::vector<PixelBytes> free;
    size_t                  bytes = 0;
};
BufferPool& pool() {
    static BufferPool* p = new BufferPool();   // навмисно "витікає": живе до кінця процесу
    return *p;
}
} // namespace

PixelBytes acquire_buffer(size_t size) {
    if (size >= kMinPooled) {
        BufferPool& p = pool();
        std::lock_guard lock(p.mutex);
        // Найменший буфер, що вміщує потрібний розмір (і не набагато більший)
        size_t best = p.free.size();
        for (size_t i = 0; i < p.free.size(); ++i) {
            const size_t cap = p.free[i].capacity();
            if (cap >= size && cap <= size * 2 && (best == p.free.size() || cap < p.free[best].capacity())) best = i;
        }
        if (best != p.free.size()) {
            PixelBytes b = std::move(p.free[best]);
            p.free.erase(p.free.begin() + static_cast<ptrdiff_t>(best));
            p.bytes -= b.capacity();
            b.resize(size);   // без обнулення (DefaultInitAllocator)
            return b;
        }
    }
    PixelBytes b;
    b.resize(size);
    return b;
}

void recycle_buffer(PixelBytes&& buffer) {
    const size_t cap = buffer.capacity();
    if (cap < kMinPooled || cap > kMaxPooledBytes / 4) return;
    BufferPool& p = pool();
    std::lock_guard lock(p.mutex);
    PixelBytes b = std::move(buffer);
    b.clear();
    // Якщо пул переповнений — викидаємо найстаріші буфери
    while (!p.free.empty() && (p.free.size() >= kMaxPooledCount || p.bytes + cap > kMaxPooledBytes)) {
        p.bytes -= p.free.front().capacity();
        p.free.erase(p.free.begin());
    }
    p.bytes += cap;
    p.free.push_back(std::move(b));
}

LayoutInfo layout_info(PixelLayout l) {
    switch (l) {
    case PixelLayout::BGR24: return {1, 3, 0, 0, false, false};
    case PixelLayout::BGRA32: return {1, 4, 0, 0, false, false};
    case PixelLayout::BGR48: return {1, 6, 0, 0, false, true};
    case PixelLayout::YUV420P: return {3, 1, 1, 1, true, false};
    case PixelLayout::YUV422P: return {3, 1, 1, 0, true, false};
    case PixelLayout::YUV444P: return {3, 1, 0, 0, true, false};
    case PixelLayout::YUV420P16: return {3, 2, 1, 1, true, true};
    case PixelLayout::YUV422P16: return {3, 2, 1, 0, true, true};
    case PixelLayout::YUV444P16: return {3, 2, 0, 0, true, true};
    }
    return {};
}

PixelLayout high_depth_layout(PixelLayout l) {
    switch (l) {
    case PixelLayout::YUV420P: return PixelLayout::YUV420P16;
    case PixelLayout::YUV422P: return PixelLayout::YUV422P16;
    case PixelLayout::YUV444P: return PixelLayout::YUV444P16;
    case PixelLayout::YUV420P16:
    case PixelLayout::YUV422P16:
    case PixelLayout::YUV444P16: return l;
    default: return PixelLayout::BGR48;
    }
}

PixelLayout low_depth_layout(PixelLayout l) {
    switch (l) {
    case PixelLayout::YUV420P:
    case PixelLayout::YUV420P16: return PixelLayout::YUV420P;
    case PixelLayout::YUV422P:
    case PixelLayout::YUV422P16: return PixelLayout::YUV422P;
    case PixelLayout::YUV444P:
    case PixelLayout::YUV444P16: return PixelLayout::YUV444P;
    default: return PixelLayout::BGR24;
    }
}

Image::~Image() { release(); }

Image& Image::operator=(Image&& o) noexcept {
    if (this == &o) return *this;
    release();
    width = o.width;
    height = o.height;
    layout = o.layout;
    full_range = o.full_range;
    bt709 = o.bt709;
    index = o.index;
    data = std::move(o.data);
    for (int i = 0; i < 3; ++i) {
        offset[i] = o.offset[i];
        stride[i] = o.stride[i];
    }
    o.data.clear();
    o.width = o.height = 0;
    return *this;
}

int Image::plane_width(int p) const {
    const LayoutInfo li = layout_info(layout);
    if (p == 0 || !li.yuv) return width;
    return (width + (1 << li.log2_chroma_w) - 1) >> li.log2_chroma_w;
}

int Image::plane_height(int p) const {
    const LayoutInfo li = layout_info(layout);
    if (p == 0 || !li.yuv) return height;
    return (height + (1 << li.log2_chroma_h) - 1) >> li.log2_chroma_h;
}

size_t Image::row_bytes(int p) const {
    return static_cast<size_t>(plane_width(p)) * static_cast<size_t>(layout_info(layout).bytes_per_pixel);
}

void Image::allocate(int w, int h, PixelLayout l) {
    release();
    width = w;
    height = h;
    layout = l;
    size_t total = 0;
    const int n = planes();
    for (int p = 0; p < 3; ++p) {
        offset[p] = 0;
        stride[p] = 0;
    }
    for (int p = 0; p < n; ++p) {
        offset[p] = total;
        stride[p] = static_cast<int>(row_bytes(p));
        total += static_cast<size_t>(stride[p]) * static_cast<size_t>(plane_height(p));
    }
    data = acquire_buffer(total);
}

void Image::release() {
    if (!data.empty() || data.capacity() > 0) recycle_buffer(std::move(data));
    data = PixelBytes();
}

} // namespace gmdr::frames
