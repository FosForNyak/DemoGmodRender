// =============================================================================
//  mapped_file.hpp — файл, відображений у пам'ять (лише читання).
//
//  Демо на кілька годин важить сотні мегабайтів. Замість того щоб читати
//  його в пам'ять цілком, система сама підвантажує потрібні сторінки файлу,
//  а зайві — викидає: пам'яті процес займає набагато менше.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace gmdr {

class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(MappedFile&& o) noexcept;
    MappedFile& operator=(MappedFile&& o) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // false — не вдалося (напр. порожній файл або система не дозволила);
    // тоді можна прочитати файл звичайним способом.
    bool open(const std::filesystem::path& path, std::string* error = nullptr);
    void close();

    const uint8_t* data() const { return static_cast<const uint8_t*>(view_); }
    size_t         size() const { return size_; }
    bool           is_open() const { return view_ != nullptr; }

private:
    void*  view_ = nullptr;
    size_t size_ = 0;
#ifdef _WIN32
    void*  file_ = nullptr;      // HANDLE
    void*  mapping_ = nullptr;   // HANDLE
#else
    int    fd_ = -1;
#endif
};

} // namespace gmdr
