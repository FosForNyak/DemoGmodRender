// =============================================================================
//  strings.hpp — дрібні допоміжні функції для рядків, шляхів і чисел.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gmdr {

namespace fs = std::filesystem;

// ---- UTF-8 <-> std::filesystem::path --------------------------------------
// Усередині програми всі рядки зберігаємо в UTF-8. На Windows шляхи
// "рідно" зберігаються у UTF-16, тому конвертуємо явно.
fs::path    path_from_utf8(std::string_view utf8);
std::string path_to_utf8(const fs::path& p);

#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view s);
std::string  wide_to_utf8(std::wstring_view s);
#endif

// ---- Рядки -------------------------------------------------------------------
std::string              trim(std::string_view s);
std::string              to_lower(std::string_view s);
bool                     iequals(std::string_view a, std::string_view b);
bool                     starts_with_i(std::string_view s, std::string_view prefix);
bool                     ends_with_i(std::string_view s, std::string_view suffix);
std::vector<std::string> split(std::string_view s, char sep, bool skip_empty = true);
std::string              replace_all(std::string s, std::string_view from, std::string_view to);
std::string              join(const std::vector<std::string>& parts, std::string_view sep);

// Розбирає "key=value; key2=value2" (роздільники ';' або перевід рядка).
std::vector<std::pair<std::string, std::string>> parse_key_values(std::string_view s);

// ---- Числа -------------------------------------------------------------------
std::optional<int64_t> parse_int(std::string_view s);
std::optional<double>  parse_double(std::string_view s);

// Раціональне число (наприклад частота кадрів): "60", "59.94", "60000/1001".
struct Rational {
    int num = 0;
    int den = 1;
    double value() const { return den ? static_cast<double>(num) / den : 0.0; }
    bool   valid() const { return num > 0 && den > 0; }
};
std::optional<Rational> parse_rational(std::string_view s);
std::string             rational_to_string(Rational r);

// "1920x1080" -> {1920, 1080}
std::optional<std::pair<int, int>> parse_size(std::string_view s);

// Розмір у байтах/бітах з суфіксом: "20M" -> 20'000'000, "320k" -> 320'000.
std::optional<int64_t> parse_bitrate(std::string_view s);

// ---- Форматування ----------------------------------------------------------------
std::string format_duration(double seconds);          // 01:02:03.4
std::string format_timecode(double seconds);          // 1:02:03.40 або 02:03.40 (для введення часу)
// Час у форматі "год:хв:сек", "хв:сек" або "сек" (секунди можуть бути дробові) -> секунди.
std::optional<double> parse_timecode(std::string_view s);
std::string format_bytes(uint64_t bytes);             // 1.5 GiB
std::string format_steamid(uint64_t steamid64);       // STEAM_0:1:12345
std::string hex_dump(const uint8_t* data, size_t n, size_t max_bytes = 32);

// Замінює символи, заборонені в іменах файлів Windows.
std::string sanitize_filename(std::string_view name);

} // namespace gmdr
