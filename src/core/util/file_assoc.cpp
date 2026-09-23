#include "file_assoc.hpp"

#include "strings.hpp"
#include "i18n.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace gmdr {

namespace fs = std::filesystem;

std::vector<RegValue> dem_association_values(const fs::path& exe) {
    const std::string e = path_to_utf8(exe);
    const std::string open = "\"" + e + "\" \"%1\"";
    const std::string prog = kDemProgId;
    const std::string app = "Applications\\" + path_to_utf8(exe.filename());
    return {
        {prog, "", tr("Демо Garry's Mod")},
        {prog + "\\DefaultIcon", "", "\"" + e + "\",0"},
        {prog + "\\shell\\open", "FriendlyAppName", "GMod Demo Render"},
        {prog + "\\shell\\open\\command", "", open},
        {".dem\\OpenWithProgids", prog, ""},
        {app, "FriendlyAppName", "GMod Demo Render"},
        {app + "\\SupportedTypes", ".dem", ""},
        {app + "\\shell\\open\\command", "", open},
    };
}

#ifdef _WIN32
namespace {
std::wstring w(const std::string& s) { return utf8_to_wide(s); }

bool set_value(const std::string& key, const std::string& name, const std::string& value, std::string* error) {
    HKEY h = nullptr;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, w(key).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr);
    if (r == ERROR_SUCCESS) {
        const std::wstring v = w(value);
        r = RegSetValueExW(h, name.empty() ? nullptr : w(name).c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                           static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(h);
    }
    if (r != ERROR_SUCCESS && error) *error = tr("не вдалося записати HKCU\\") + key + tr(" (код ") + std::to_string(r) + ")";
    return r == ERROR_SUCCESS;
}

std::string get_value(const std::string& key, const std::string& name) {
    wchar_t buf[2048] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, w(key).c_str(), name.empty() ? nullptr : w(name).c_str(), RRF_RT_REG_SZ, nullptr,
                     buf, &size) != ERROR_SUCCESS)
        return {};
    return wide_to_utf8(buf);
}

bool value_exists(const std::string& key, const std::string& name) {
    return RegGetValueW(HKEY_CURRENT_USER, w(key).c_str(), name.empty() ? nullptr : w(name).c_str(), RRF_RT_ANY, nullptr,
                        nullptr, nullptr) == ERROR_SUCCESS;
}

void notify_shell() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }
} // namespace

bool register_dem_association(const fs::path& exe, std::string* error, const std::string& root) {
    for (const auto& v : dem_association_values(exe))
        if (!set_value(root + "\\" + v.key, v.name, v.value, error)) return false;
    // Типова програма для .dem — лише якщо ще не призначена інша
    const std::string cur = get_value(root + "\\.dem", "");
    if (cur.empty() || cur == kDemProgId) {
        if (!set_value(root + "\\.dem", "", kDemProgId, error)) return false;
        set_value(root + "\\" + kDemProgId, "GmdrSetDefault", "1", nullptr);   // щоб при вимкненні прибрати
    }
    notify_shell();
    return true;
}

bool unregister_dem_association(std::string* error, const std::string& root) {
    const std::string prog = root + "\\" + kDemProgId;
    const bool was_default = get_value(prog, "GmdrSetDefault") == "1" || get_value(root + "\\.dem", "") == kDemProgId;
    LONG r = RegDeleteTreeW(HKEY_CURRENT_USER, w(prog).c_str());
    if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
        if (error) *error = tr("не вдалося видалити HKCU\\") + prog + tr(" (код ") + std::to_string(r) + ")";
        return false;
    }
    RegDeleteKeyValueW(HKEY_CURRENT_USER, w(root + "\\.dem\\OpenWithProgids").c_str(), w(kDemProgId).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, w(root + "\\Applications\\gmdr.exe").c_str());
    if (was_default && get_value(root + "\\.dem", "") == kDemProgId)
        RegDeleteKeyValueW(HKEY_CURRENT_USER, w(root + "\\.dem").c_str(), nullptr);
    notify_shell();
    return true;
}

bool dem_association_registered(const fs::path& exe, const std::string& root) {
    const auto vals = dem_association_values(exe);
    const auto& cmd = vals[3];   // shell\open\command — має вказувати саме на цей exe
    return value_exists(root + "\\.dem\\OpenWithProgids", kDemProgId) &&
           get_value(root + "\\" + cmd.key, cmd.name) == cmd.value;
}
#else
bool register_dem_association(const fs::path&, std::string* error, const std::string&) {
    if (error) *error = tr("асоціація файлів — лише у Windows");
    return false;
}
bool unregister_dem_association(std::string*, const std::string&) { return true; }
bool dem_association_registered(const fs::path&, const std::string&) { return false; }
#endif

} // namespace gmdr
