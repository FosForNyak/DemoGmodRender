// =============================================================================
//  tga.hpp — читання/запис TGA (Targa). startmovie у рушії Source пише кадри
//  саме в TGA: 24 біти, без стиснення, зазвичай рядки знизу вгору.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "image.hpp"

namespace gmdr::frames {

// Прочитати лише заголовок. expected_size — очікуваний розмір файлу для
// нестиснених TGA (0 для RLE, де розмір наперед невідомий).
bool tga_header_info(const uint8_t* data, size_t size, int& width, int& height, int& bpp, size_t& expected_size);

// Декодувати у BGR24 (або BGRA32 для 32-бітних), рядки зверху вниз (копія).
bool decode_tga(const uint8_t* data, size_t size, Image& out, std::string* error = nullptr);

// Декодувати файл, що вже лежить в img.data (size — розмір файлу без запасу в
// кінці буфера). Звичайний випадок (без стиснення, 24/32 біти) — без жодної
// копії: кадр описується зсувом і кроком рядка прямо в буфері файлу.
bool decode_tga_inplace(Image& img, size_t size, std::string* error = nullptr);

// Закодувати (для тестів та симулятора гри).
std::vector<uint8_t> encode_tga(const Image& img, bool bottom_up = true, bool rle = false);

} // namespace gmdr::frames
