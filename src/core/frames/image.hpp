// =============================================================================
//  image.hpp — кадр у пам'яті (сирі пікселі).
//
//  Кадр 4K займає ~25 МБ, а гра видає їх десятками за секунду, тому тут
//  кілька прийомів, щоб не гаяти час на зайві копії:
//   * буфер береться з пулу і не обнуляється (його одразу перезаписує файл
//     або декодер) — див. acquire_buffer();
//   * TGA лишається в буфері, прочитаному з файлу: рядки знизу вгору
//     описуються від'ємним кроком (stride), заголовок — зсувом (offset);
//   * JPEG зберігається у своєму рідному YUV (площини Y, U, V), без
//     перетворення в RGB і назад.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace gmdr::frames {

// Алокатор, що не обнуляє нові елементи під час resize() (звичайний
// std::vector<uint8_t> заповнює нулями кожен новий байт).
template <class T>
struct DefaultInitAllocator : std::allocator<T> {
    using value_type = T;
    template <class U>
    struct rebind { using other = DefaultInitAllocator<U>; };
    DefaultInitAllocator() noexcept = default;
    template <class U>
    DefaultInitAllocator(const DefaultInitAllocator<U>&) noexcept {}
    template <class U>
    void construct(U* p) noexcept(std::is_nothrow_default_constructible_v<U>) {
        ::new (static_cast<void*>(p)) U;
    }
    template <class U, class... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
};
template <class T, class U>
bool operator==(const DefaultInitAllocator<T>&, const DefaultInitAllocator<U>&) { return true; }

using PixelBytes = std::vector<uint8_t, DefaultInitAllocator<uint8_t>>;

// Буфер потрібного розміру з пулу (вміст невизначений) і повернення в пул.
// Великі буфери кадрів повторно використовуються: виділення нової пам'яті
// у Windows коштує обнулення сторінок ядром на кожен кадр.
PixelBytes acquire_buffer(size_t size);
void       recycle_buffer(PixelBytes&& buffer);

enum class PixelLayout {
    BGR24,       // 8 біт на канал, порядок B,G,R (так пише TGA рушій Source)
    BGRA32,      // 8 біт на канал + альфа
    BGR48,       // 16 біт на канал (результат motion blur — більша точність)
    YUV420P,     // 8 біт, площини Y/U/V (так декодується JPEG)
    YUV422P,
    YUV444P,
    YUV420P16,   // 16 біт, площини (motion blur над кадрами JPEG)
    YUV422P16,
    YUV444P16,
};

struct LayoutInfo {
    int  planes = 1;
    int  bytes_per_pixel = 3;   // для упакованих (BGR) — байтів на піксель, для площин — на семпл
    int  log2_chroma_w = 0;     // субдискретизація кольору (лише YUV)
    int  log2_chroma_h = 0;
    bool yuv = false;
    bool high_depth = false;    // 16 біт на семпл
};
LayoutInfo layout_info(PixelLayout l);
inline bool is_yuv(PixelLayout l) { return layout_info(l).yuv; }
// Байтів на піксель площини 0 (для BGR — на весь піксель).
inline int bytes_per_pixel(PixelLayout l) { return layout_info(l).bytes_per_pixel; }
// 16-бітний варіант того самого розміщення (для результату motion blur).
PixelLayout high_depth_layout(PixelLayout l);
PixelLayout low_depth_layout(PixelLayout l);

class Image {
public:
    int         width = 0;
    int         height = 0;
    PixelLayout layout = PixelLayout::BGR24;
    bool        full_range = true;   // лише для YUV: повний діапазон 0..255 (як у JPEG)
    bool        bt709 = false;       // лише для YUV: матриця BT.709 (інакше BT.601, як у JPEG)
    int64_t     index = -1;          // номер кадру у послідовності
    PixelBytes  data;                // сховище (може містити і заголовок файлу)
    size_t      offset[3] = {0, 0, 0};   // де в data починається верхній рядок площини
    int         stride[3] = {0, 0, 0};   // байтів на рядок; від'ємний — рядки лежать знизу вгору

    Image() = default;
    ~Image();
    Image(Image&& o) noexcept { *this = std::move(o); }
    Image& operator=(Image&& o) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    bool empty() const { return data.empty(); }
    int  planes() const { return layout_info(layout).planes; }
    int  plane_width(int p) const;    // у семплах
    int  plane_height(int p) const;
    // Байтів корисних даних у рядку площини (без вирівнювання).
    size_t row_bytes(int p) const;
    uint8_t* row(int p, int y) { return data.data() + offset[p] + static_cast<ptrdiff_t>(y) * stride[p]; }
    const uint8_t* row(int p, int y) const {
        return data.data() + offset[p] + static_cast<ptrdiff_t>(y) * stride[p];
    }
    // Виділити щільний буфер (рядки зверху вниз) з пулу; вміст невизначений.
    void allocate(int w, int h, PixelLayout l);
    // Звільнити буфер (повернути в пул).
    void release();
};

} // namespace gmdr::frames
