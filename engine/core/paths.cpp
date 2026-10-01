#include "core/paths.h"

#include "core/text.h"

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gmdr {

namespace {

std::filesystem::path envPath(const char* name) {
#ifdef _WIN32
    const std::wstring wname(name, name + std::char_traits<char>::length(name));
    DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (n == 0)
        return {};
    std::wstring value(n, L'\0');
    n = GetEnvironmentVariableW(wname.c_str(), value.data(), n);
    value.resize(n);
    return std::filesystem::path(value);
#else
    const char* v = std::getenv(name);
    return v ? std::filesystem::path(v) : std::filesystem::path{};
#endif
}

} // namespace

std::filesystem::path userCacheDir() {
#ifdef _WIN32
    auto base = envPath("LOCALAPPDATA");
    if (base.empty())
        base = envPath("TEMP");
    return base / "DemoGmodRender";
#else
    auto base = envPath("XDG_CACHE_HOME");
    if (base.empty())
        base = envPath("HOME") / ".cache";
    return base / "demogmodrender";
#endif
}

std::filesystem::path userConfigDir() {
#ifdef _WIN32
    auto base = envPath("APPDATA");
    if (base.empty())
        base = envPath("LOCALAPPDATA");
    return base / "DemoGmodRender";
#else
    auto base = envPath("XDG_CONFIG_HOME");
    if (base.empty())
        base = envPath("HOME") / ".config";
    return base / "demogmodrender";
#endif
}

} // namespace gmdr
