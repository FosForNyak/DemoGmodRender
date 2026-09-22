#include "tga.hpp"

#include <cstring>
#include <format>

namespace gmdr::frames {

namespace {
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

struct TgaHeader {
    int    id_len = 0, cmap_type = 0, type = 0;
    int    cmap_len = 0, cmap_bits = 0;
    int    width = 0, height = 0, bpp = 0, descriptor = 0;
    size_t data_offset = 0;
};

bool parse(const uint8_t* d, size_t size, TgaHeader& h, std::string* error) {
    if (size < 18) {
        if (error) *error = "TGA: файл менший за заголовок";
        return false;
    }
    h.id_len = d[0];
    h.cmap_type = d[1];
    h.type = d[2];
    h.cmap_len = rd16(d + 5);
    h.cmap_bits = d[7];
    h.width = rd16(d + 12);
    h.height = rd16(d + 14);
    h.bpp = d[16];
    h.descriptor = d[17];
    h.data_offset = 18 + static_cast<size_t>(h.id_len) +
                    (h.cmap_type ? static_cast<size_t>(h.cmap_len) * ((h.cmap_bits + 7) / 8) : 0);
    if (h.type != 2 && h.type != 10 && h.type != 3 && h.type != 11) {
        if (error) *error = std::format("TGA: непідтримуваний тип зображення {}", h.type);
        return false;
    }
    const bool gray = h.type == 3 || h.type == 11;
    if ((!gray && h.bpp != 24 && h.bpp != 32) || (gray && h.bpp != 8)) {
        if (error) *error = std::format("TGA: непідтримувана глибина {} біт", h.bpp);
        return false;
    }
    if (h.width <= 0 || h.height <= 0) {
        if (error) *error = "TGA: нульовий розмір";
        return false;
    }
    return true;
}
} // namespace

bool tga_header_info(const uint8_t* data, size_t size, int& width, int& height, int& bpp, size_t& expected_size) {
    TgaHeader h;
    if (!parse(data, size, h, nullptr)) return false;
    width = h.width;
    height = h.height;
    bpp = h.bpp;
    expected_size = (h.type == 2 || h.type == 3)
                        ? h.data_offset + static_cast<size_t>(h.width) * h.height * (h.bpp / 8)
                        : 0;
    return true;
}

bool decode_tga(const uint8_t* d, size_t size, Image& out, std::string* error) {
    TgaHeader h;
    if (!parse(d, size, h, error)) return false;
    const int in_bpp = h.bpp / 8;
    const bool gray = in_bpp == 1;
    const PixelLayout layout = in_bpp == 4 ? PixelLayout::BGRA32 : PixelLayout::BGR24;
    const int out_bpp = bytes_per_pixel(layout);
    out.allocate(h.width, h.height, layout);

    const bool top_down = (h.descriptor & 0x20) != 0;
    const bool right_to_left = (h.descriptor & 0x10) != 0;
    const size_t pixels = static_cast<size_t>(h.width) * static_cast<size_t>(h.height);
    const uint8_t* src = d + h.data_offset;
    const uint8_t* end = d + size;

    auto dst_row = [&](int y) -> uint8_t* { return out.row(0, top_down ? y : (h.height - 1 - y)); };

    if (h.type == 2 || h.type == 3) {
        const size_t need = pixels * static_cast<size_t>(in_bpp);
        if (h.data_offset > size || static_cast<size_t>(end - src) < need) {
            if (error) *error = "TGA: дані обрізані (файл ще записується?)";
            return false;
        }
        const size_t row_bytes = static_cast<size_t>(h.width) * static_cast<size_t>(in_bpp);
        for (int y = 0; y < h.height; ++y) {
            const uint8_t* s = src + static_cast<size_t>(y) * row_bytes;
            uint8_t* o = dst_row(y);
            if (!gray && !right_to_left) {
                std::memcpy(o, s, row_bytes);
            } else {
                for (int x = 0; x < h.width; ++x) {
                    const int dx = right_to_left ? (h.width - 1 - x) : x;
                    uint8_t* p = o + dx * out_bpp;
                    if (gray) { p[0] = p[1] = p[2] = s[x]; }
                    else std::memcpy(p, s + x * in_bpp, static_cast<size_t>(in_bpp));
                }
            }
        }
        return true;
    }

    // RLE (типи 10 і 11)
    size_t written = 0;
    uint8_t px[4] = {0, 0, 0, 0};
    auto put = [&](const uint8_t* p) {
        const int y = static_cast<int>(written / static_cast<size_t>(h.width));
        int x = static_cast<int>(written % static_cast<size_t>(h.width));
        if (right_to_left) x = h.width - 1 - x;
        uint8_t* o = dst_row(y) + x * out_bpp;
        if (gray) { o[0] = o[1] = o[2] = p[0]; }
        else std::memcpy(o, p, static_cast<size_t>(in_bpp));
        ++written;
    };
    if (h.data_offset > size) {
        if (error) *error = "TGA: RLE-дані обрізані";
        return false;
    }
    while (written < pixels) {
        if (src >= end) {
            if (error) *error = "TGA: RLE-дані обрізані";
            return false;
        }
        const uint8_t hdr = *src++;
        const int count = (hdr & 0x7F) + 1;
        if (hdr & 0x80) {
            if (end - src < in_bpp) { if (error) *error = "TGA: RLE-дані обрізані"; return false; }
            std::memcpy(px, src, static_cast<size_t>(in_bpp));
            src += in_bpp;
            for (int i = 0; i < count && written < pixels; ++i) put(px);
        } else {
            if (end - src < static_cast<ptrdiff_t>(count) * in_bpp) { if (error) *error = "TGA: RLE-дані обрізані"; return false; }
            for (int i = 0; i < count && written < pixels; ++i) {
                put(src);
                src += in_bpp;
            }
        }
    }
    return true;
}

bool decode_tga_inplace(Image& img, size_t size, std::string* error) {
    TgaHeader h;
    if (size > img.data.size() || !parse(img.data.data(), size, h, error)) return false;
    const int in_bpp = h.bpp / 8;
    const bool simple = (h.type == 2) && (in_bpp == 3 || in_bpp == 4) && !(h.descriptor & 0x10);
    if (!simple) {
        // Рідкісні варіанти (RLE, сірий, справа наліво) — звичайне декодування з копією
        Image tmp;
        if (!decode_tga(img.data.data(), size, tmp, error)) return false;
        tmp.index = img.index;
        img = std::move(tmp);
        return true;
    }
    const size_t row_bytes = static_cast<size_t>(h.width) * static_cast<size_t>(in_bpp);
    const size_t need = row_bytes * static_cast<size_t>(h.height);
    if (h.data_offset > size || size - h.data_offset < need) {
        if (error) *error = "TGA: дані обрізані (файл ще записується?)";
        return false;
    }
    img.width = h.width;
    img.height = h.height;
    img.layout = in_bpp == 4 ? PixelLayout::BGRA32 : PixelLayout::BGR24;
    const bool top_down = (h.descriptor & 0x20) != 0;
    img.offset[0] = h.data_offset + (top_down ? 0 : row_bytes * static_cast<size_t>(h.height - 1));
    img.stride[0] = top_down ? static_cast<int>(row_bytes) : -static_cast<int>(row_bytes);
    img.offset[1] = img.offset[2] = 0;
    img.stride[1] = img.stride[2] = 0;
    return true;
}

std::vector<uint8_t> encode_tga(const Image& img, bool bottom_up, bool rle) {
    const int bpp = img.layout == PixelLayout::BGRA32 ? 4 : 3;
    std::vector<uint8_t> out(18, 0);
    out[2] = rle ? 10 : 2;
    out[12] = static_cast<uint8_t>(img.width & 0xFF);
    out[13] = static_cast<uint8_t>(img.width >> 8);
    out[14] = static_cast<uint8_t>(img.height & 0xFF);
    out[15] = static_cast<uint8_t>(img.height >> 8);
    out[16] = static_cast<uint8_t>(bpp * 8);
    out[17] = static_cast<uint8_t>((bottom_up ? 0 : 0x20) | (bpp == 4 ? 8 : 0));
    for (int yy = 0; yy < img.height; ++yy) {
        const int y = bottom_up ? img.height - 1 - yy : yy;
        const uint8_t* row = img.row(0, y);
        if (!rle) {
            out.insert(out.end(), row, row + static_cast<size_t>(img.width) * bpp);
            continue;
        }
        // Просте RLE: пакети повторів або сирі пакети в межах рядка
        int x = 0;
        while (x < img.width) {
            int run = 1;
            while (x + run < img.width && run < 128 &&
                   std::memcmp(row + x * bpp, row + (x + run) * bpp, static_cast<size_t>(bpp)) == 0)
                ++run;
            if (run > 1) {
                out.push_back(static_cast<uint8_t>(0x80 | (run - 1)));
                out.insert(out.end(), row + x * bpp, row + x * bpp + bpp);
                x += run;
            } else {
                int raw = 1;
                while (x + raw < img.width && raw < 128 &&
                       !(x + raw + 1 < img.width &&
                         std::memcmp(row + (x + raw) * bpp, row + (x + raw + 1) * bpp, static_cast<size_t>(bpp)) == 0))
                    ++raw;
                out.push_back(static_cast<uint8_t>(raw - 1));
                out.insert(out.end(), row + x * bpp, row + (x + raw) * bpp);
                x += raw;
            }
        }
    }
    return out;
}

} // namespace gmdr::frames
