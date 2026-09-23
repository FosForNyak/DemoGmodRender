// =============================================================================
//  zip_writer.hpp — найпростіший запис ZIP (файли без стиснення, "stored").
//
//  Для звіту про проблему: журнали, налаштування, відомості про систему. Стиснення
//  не потрібне (файли невеликі), а формат без нього — кілька заголовків і CRC32.
//  Імена — UTF-8 (прапорець 11), тож кирилиця в іменах відкривається правильно.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace gmdr {

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

class ZipWriter {
public:
    ~ZipWriter();
    bool open(const std::filesystem::path& path, std::string* error);
    bool add(const std::string& name_utf8, std::string_view data);
    bool add_file(const std::string& name_utf8, const std::filesystem::path& src);   // false — немає файлу
    bool close(std::string* error);   // центральний каталог; без нього архів не відкриється

private:
    struct Entry {
        std::string name;
        uint32_t    crc = 0, size = 0, offset = 0;
    };
    std::ofstream      f_;
    std::vector<Entry> entries_;
    uint16_t           dos_time_ = 0, dos_date_ = 0;
    bool               open_ = false;
};

} // namespace gmdr
