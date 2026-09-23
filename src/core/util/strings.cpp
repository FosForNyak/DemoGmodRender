#include "strings.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gmdr {

// ---------------------------------------------------------------------------
#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8(std::wstring_view s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

fs::path path_from_utf8(std::string_view utf8) { return fs::path(utf8_to_wide(utf8)); }
std::string path_to_utf8(const fs::path& p) { return wide_to_utf8(p.native()); }
#else
fs::path path_from_utf8(std::string_view utf8) { return fs::path(std::string(utf8)); }
std::string path_to_utf8(const fs::path& p) { return p.string(); }
#endif

// ---------------------------------------------------------------------------
std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string to_lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool starts_with_i(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool ends_with_i(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
}

std::vector<std::string> split(std::string_view s, char sep, bool skip_empty) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) pos = s.size();
        std::string part(s.substr(start, pos - start));
        if (!(skip_empty && trim(part).empty())) out.push_back(part);
        start = pos + 1;
    }
    return out;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::vector<std::pair<std::string, std::string>> parse_key_values(std::string_view s) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string normalized(s);
    std::replace(normalized.begin(), normalized.end(), '\n', ';');
    std::replace(normalized.begin(), normalized.end(), '\r', ';');
    for (auto& item : split(normalized, ';')) {
        const std::string t = trim(item);
        if (t.empty()) continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) {
            out.emplace_back(t, "1");
        } else {
            out.emplace_back(trim(t.substr(0, eq)), trim(t.substr(eq + 1)));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
std::optional<int64_t> parse_int(std::string_view s) {
    const std::string t = trim(s);
    if (t.empty()) return std::nullopt;
    int64_t v = 0;
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (ec != std::errc() || p != t.data() + t.size()) return std::nullopt;
    return v;
}

std::optional<double> parse_double(std::string_view s) {
    std::string t = trim(s);
    if (t.empty()) return std::nullopt;
    std::replace(t.begin(), t.end(), ',', '.');   // дозволяємо "59,94"
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<Rational> parse_rational(std::string_view s) {
    const std::string t = trim(s);
    if (t.empty()) return std::nullopt;
    const size_t slash = t.find('/');
    if (slash != std::string::npos) {
        auto n = parse_int(t.substr(0, slash));
        auto d = parse_int(t.substr(slash + 1));
        if (!n || !d || *n <= 0 || *d <= 0) return std::nullopt;
        return Rational{static_cast<int>(*n), static_cast<int>(*d)};
    }
    auto v = parse_double(t);
    if (!v || *v <= 0) return std::nullopt;
    // Відомі NTSC-частоти перетворюємо на точні дроби.
    struct Known { double v; int n, d; };
    static const Known known[] = {{23.976, 24000, 1001}, {29.97, 30000, 1001},
                                  {47.952, 48000, 1001}, {59.94, 60000, 1001},
                                  {119.88, 120000, 1001}};
    for (const auto& k : known)
        if (std::abs(*v - k.v) < 0.005) return Rational{k.n, k.d};
    if (std::abs(*v - std::round(*v)) < 1e-9) return Rational{static_cast<int>(std::lround(*v)), 1};
    return Rational{static_cast<int>(std::lround(*v * 1000.0)), 1000};
}

std::string rational_to_string(Rational r) {
    if (r.den == 1) return std::to_string(r.num);
    return std::format("{}/{}", r.num, r.den);
}

std::optional<std::pair<int, int>> parse_size(std::string_view s) {
    const std::string t = to_lower(trim(s));
    const size_t x = t.find('x');
    if (x == std::string::npos) return std::nullopt;
    auto w = parse_int(t.substr(0, x));
    auto h = parse_int(t.substr(x + 1));
    if (!w || !h || *w <= 0 || *h <= 0 || *w > 16384 || *h > 16384) return std::nullopt;
    return std::pair<int, int>{static_cast<int>(*w), static_cast<int>(*h)};
}

std::optional<int64_t> parse_bitrate(std::string_view s) {
    std::string t = to_lower(trim(s));
    if (t.empty()) return std::nullopt;
    double mul = 1.0;
    const char last = t.back();
    if (last == 'k') { mul = 1e3; t.pop_back(); }
    else if (last == 'm') { mul = 1e6; t.pop_back(); }
    else if (last == 'g') { mul = 1e9; t.pop_back(); }
    auto v = parse_double(t);
    if (!v || *v < 0) return std::nullopt;
    return static_cast<int64_t>(std::llround(*v * mul));
}

// ---------------------------------------------------------------------------
std::string format_duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const int64_t total_ms = static_cast<int64_t>(std::llround(seconds * 1000.0));
    const int64_t h = total_ms / 3600000;
    const int64_t m = (total_ms / 60000) % 60;
    const int64_t sec = (total_ms / 1000) % 60;
    const int64_t tenth = (total_ms / 100) % 10;
    if (h > 0) return std::format("{}:{:02}:{:02}", h, m, sec);
    return std::format("{:02}:{:02}.{}", m, sec, tenth);
}

std::string format_timecode(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const int64_t total_cs = static_cast<int64_t>(std::llround(seconds * 100.0));
    const int64_t h = total_cs / 360000;
    const int64_t m = (total_cs / 6000) % 60;
    const int64_t sec = (total_cs / 100) % 60;
    const int64_t cs = total_cs % 100;
    if (h > 0) return std::format("{}:{:02}:{:02}.{:02}", h, m, sec, cs);
    return std::format("{:02}:{:02}.{:02}", m, sec, cs);
}

std::optional<double> parse_timecode(std::string_view s) {
    std::string t = trim(std::string(s));
    for (char& c : t)
        if (c == ',') c = '.';
    if (t.empty()) return std::nullopt;
    const auto parts = split(t, ':', false);
    if (parts.empty() || parts.size() > 3) return std::nullopt;
    double total = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string p = trim(parts[i]);
        auto v = parse_double(p);
        if (!v || *v < 0) return std::nullopt;
        // Дробова частина дозволена лише в секундах; хвилини/секунди перед ":" — до 59
        if (i + 1 < parts.size() && (p.find('.') != std::string::npos)) return std::nullopt;
        if (i > 0 && *v >= 60.0) return std::nullopt;
        total = total * 60.0 + *v;
    }
    return total;
}

std::string format_bytes(uint64_t bytes) {
    const char* units[] = {tr("Б"), tr("КіБ"), tr("МіБ"), tr("ГіБ"), tr("ТіБ")};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    return u == 0 ? std::format("{} {}", bytes, units[0]) : std::format("{:.1f} {}", v, units[u]);
}

std::string format_steamid(uint64_t steamid64) {
    constexpr uint64_t base = 76561197960265728ULL;
    if (steamid64 < base) return std::to_string(steamid64);
    const uint64_t account = steamid64 - base;
    return std::format("STEAM_0:{}:{}", account & 1, account >> 1);
}

std::string hex_dump(const uint8_t* data, size_t n, size_t max_bytes) {
    std::string out;
    for (size_t i = 0; i < n && i < max_bytes; ++i) out += std::format("{:02x} ", data[i]);
    if (n > max_bytes) out += "...";
    return out;
}

std::string sanitize_filename(std::string_view name) {
    std::string out;
    for (unsigned char c : name) {
        if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
            c == '|' || c == '?' || c == '*')
            out += '_';
        else
            out += static_cast<char>(c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty()) out = "_";
    return out;
}

} // namespace gmdr
