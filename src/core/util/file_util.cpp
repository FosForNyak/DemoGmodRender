#include "file_util.hpp"

#include "strings.hpp"
#include "i18n.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <random>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace gmdr {

std::optional<std::vector<uint8_t>> read_file_bytes(const fs::path& p, std::string* error) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (error) *error = tr("не вдалося відкрити файл");
        return std::nullopt;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) {
        if (error) *error = tr("не вдалося визначити розмір файлу");
        return std::nullopt;
    }
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), size)) {
        if (error) *error = tr("помилка читання файлу");
        return std::nullopt;
    }
    return data;
}

std::optional<std::string> read_file_text(const fs::path& p, std::string* error) {
    auto bytes = read_file_bytes(p, error);
    if (!bytes) return std::nullopt;
    std::string s(bytes->begin(), bytes->end());
    // прибираємо UTF-8 BOM
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    return s;
}

bool write_file_text(const fs::path& p, const std::string& text, std::string* error) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = tr("не вдалося створити файл");
        return false;
    }
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) {
        if (error) *error = tr("помилка запису файлу");
        return false;
    }
    return true;
}

bool write_file_atomic(const fs::path& p, const std::string& text, std::string* error) {
    fs::path tmp = p;
    tmp += ".tmp";
    if (!write_file_text(tmp, text, error)) return false;
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        // На Windows rename поверх відкритого файлу може не вдатися — пробуємо ще раз
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
        if (ec) {
            if (error) *error = ec.message();
            return false;
        }
    }
    return true;
}

bool copy_file_overwrite(const fs::path& from, const fs::path& to, std::string* error) {
    std::ifstream in(from, std::ios::binary);
    if (!in) {
        if (error) *error = tr("не вдалося відкрити ") + path_to_utf8(from.filename());
        return false;
    }
    std::error_code ec;
    if (to.has_parent_path()) fs::create_directories(to.parent_path(), ec);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = tr("не вдалося створити ") + path_to_utf8(to.filename());
        return false;
    }
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) out.write(buf.data(), n);
        if (!out) {
            if (error) *error = tr("помилка запису (диск заповнено?)");
            return false;
        }
    }
    return true;
}

bool path_is_inside(const fs::path& p, const fs::path& dir) {
    std::string a = path_to_utf8(p.lexically_normal()), b = path_to_utf8(dir.lexically_normal());
#ifdef _WIN32
    a = to_lower(a);
    b = to_lower(b);
#endif
    while (!b.empty() && (b.back() == '/' || b.back() == '\\')) b.pop_back();
    return !b.empty() && a.size() > b.size() + 1 && a.compare(0, b.size(), b) == 0 &&
           (a[b.size()] == '/' || a[b.size()] == '\\');
}

uint64_t file_size_or_zero(const fs::path& p) {
    std::error_code ec;
    const auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(s);
}

bool remove_file_quiet(const fs::path& p) {
    std::error_code ec;
    return fs::remove(p, ec);
}

uint64_t free_disk_space(const fs::path& p) {
    std::error_code ec;
    const auto info = fs::space(p, ec);
    return ec ? 0 : static_cast<uint64_t>(info.available);
}

fs::path executable_dir() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return fs::path(buf).parent_path();
#else
    char buf[PATH_MAX + 1] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, PATH_MAX);
    if (n > 0) return fs::path(std::string(buf, static_cast<size_t>(n))).parent_path();
    return fs::current_path();
#endif
}

namespace {
bool dir_is_writable(const fs::path& dir) {
    const fs::path probe = dir / std::format(".gmdr_write_test_{}", make_unique_id());
    {
        std::ofstream f(probe, std::ios::binary);
        if (!f) return false;
        f << "ok";
        if (!f) return false;
    }
    std::error_code ec;
    fs::remove(probe, ec);
    return true;
}
} // namespace

fs::path app_data_dir() {
    static const fs::path dir = [] {
        const fs::path exe = executable_dir();
        if (dir_is_writable(exe)) return exe;
        fs::path base;
#ifdef _WIN32
        if (const wchar_t* la = _wgetenv(L"LOCALAPPDATA"); la && *la) base = fs::path(la) / "GModDemoRender";
#else
        if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) base = fs::path(x) / "gmod-demo-render";
        else if (const char* h = std::getenv("HOME"); h && *h) base = fs::path(h) / ".config" / "gmod-demo-render";
#endif
        if (base.empty()) return exe;
        std::error_code ec;
        fs::create_directories(base, ec);
        return ec ? exe : base;
    }();
    return dir;
}

std::string make_unique_id() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::random_device rd;
    std::mt19937 gen(rd());
    return std::format("{:x}{:04x}", ms, gen() & 0xFFFF);
}

} // namespace gmdr
