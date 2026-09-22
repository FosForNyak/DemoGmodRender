// =============================================================================
//  image_decode.hpp — читання і декодування файлу кадру (TGA, JPEG, PNG, BMP).
//  JPEG/PNG/BMP декодуються вбудованими декодерами FFmpeg (з SIMD).
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "image.hpp"

namespace gmdr::frames {

enum class ImageFileType { Unknown, Tga, Jpeg, Png, Bmp };

ImageFileType detect_image_type(const uint8_t* data, size_t size, const std::string& ext_hint);

// Скільки нульових байтів тримати в кінці буфера файлу (цього вимагають
// декодери FFmpeg; дорівнює AV_INPUT_BUFFER_PADDING_SIZE).
constexpr size_t kDecodePadding = 64;

// Декодувати файл, що вже прочитаний у bytes (size — розмір файлу; після нього
// в буфері має бути kDecodePadding нулів). Буфер переходить у власність кадру:
// TGA лишається в ньому без копіювання.
// keep_yuv: JPEG віддавати у рідному YUV (швидше), інакше — перетворити в BGR24.
// Потокобезпечно (кожен потік має власний декодер).
bool decode_image_buffer(PixelBytes&& bytes, size_t size, const std::string& ext_hint, Image& out,
                         std::string* error = nullptr, bool keep_yuv = true);

// Те саме для звичайного вектора (копіює дані; для тестів і утиліт).
bool decode_image_file(const std::vector<uint8_t>& bytes, const std::string& ext_hint, Image& out,
                       std::string* error = nullptr, bool keep_yuv = true);

// Перевірка, що файл кадру записано повністю (для файлів, які ще пише гра).
bool image_file_complete(const uint8_t* data, size_t size, const std::string& ext_hint);

// ---- Читання файлу кадру ------------------------------------------------------
enum class ReadStatus {
    Ok,        // прочитано
    Missing,   // файлу немає
    Locked,    // файл зайнятий (гра ще пише або антивірус) — спробувати пізніше
    Error,
};

struct FrameFileRead {
    PixelBytes  bytes;            // вміст + kDecodePadding нулів
    size_t      size = 0;         // розмір файлу
    bool        complete = false; // image_file_complete()
    bool        deleted = false;  // файл уже видалено (якщо просили)
    std::string error;
};

// Прочитати файл кадру в буфер з пулу. Якщо delete_when_complete і файл
// дописаний повністю — видалити його через той самий дескриптор (без
// повторного відкриття). Послідовне читання (FILE_FLAG_SEQUENTIAL_SCAN).
ReadStatus read_frame_file(const std::filesystem::path& path, const std::string& ext_hint, bool delete_when_complete,
                           FrameFileRead& out);

} // namespace gmdr::frames
