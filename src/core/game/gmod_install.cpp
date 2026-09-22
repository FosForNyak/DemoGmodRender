#include "gmod_install.hpp"

#include "../util/file_util.hpp"
#include "../util/strings.hpp"
#include "../util/vdf.hpp"

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

namespace gmdr::game {

std::string GModInstall::exe_label(const fs::path& exe) {
    const std::string name = path_to_utf8(exe.filename());
    const std::string parent = path_to_utf8(exe.parent_path().filename());
    if (parent == "win64" || parent == "linux64") return std::format("64-біт ({})", name);
    return std::format("32-біт ({})", name);
}

std::optional<GModInstall> gmod_from_dir(const fs::path& dir_in) {
    std::error_code ec;
    fs::path dir = dir_in;
    if (dir.empty() || !fs::exists(dir, ec)) return std::nullopt;
    // Дозволяємо вказати і GarrysMod, і GarrysMod\garrysmod
    if (to_lower(path_to_utf8(dir.filename())) == "garrysmod" && fs::exists(dir / "gameinfo.txt", ec) &&
        !fs::exists(dir / "garrysmod", ec))
        dir = dir.parent_path();
    GModInstall g;
    g.root = dir;
    g.garrysmod = dir / "garrysmod";
    if (!fs::exists(g.garrysmod / "gameinfo.txt", ec) && !fs::exists(g.garrysmod / "lua", ec)) return std::nullopt;
#ifdef _WIN32
    const fs::path candidates[] = {dir / "bin" / "win64" / "gmod.exe", dir / "gmod.exe", dir / "hl2.exe"};
#else
    const fs::path candidates[] = {dir / "bin" / "linux64" / "gmod", dir / "hl2_linux", dir / "hl2.sh",
                                   dir / "bin" / "win64" / "gmod.exe", dir / "hl2.exe"};
#endif
    for (const auto& c : candidates)
        if (fs::exists(c, ec)) g.executables.push_back(c);
    return g;
}

#ifdef _WIN32
static std::optional<std::wstring> reg_string(HKEY root, const wchar_t* sub, const wchar_t* value) {
    wchar_t buf[2048];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, sub, value, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) return std::wstring(buf);
    return std::nullopt;
}
#endif

std::vector<fs::path> steam_library_folders(std::vector<std::string>* log) {
    std::vector<fs::path> steam_roots;
#ifdef _WIN32
    if (auto p = reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath")) steam_roots.emplace_back(*p);
    if (auto p = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath")) steam_roots.emplace_back(*p);
    if (auto p = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath")) steam_roots.emplace_back(*p);
    steam_roots.emplace_back(L"C:\\Program Files (x86)\\Steam");
    steam_roots.emplace_back(L"C:\\Program Files\\Steam");
#else
    if (const char* home = std::getenv("HOME")) {
        steam_roots.emplace_back(fs::path(home) / ".steam" / "steam");
        steam_roots.emplace_back(fs::path(home) / ".local" / "share" / "Steam");
        steam_roots.emplace_back(fs::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
    }
#endif
    std::vector<fs::path> libs;
    std::error_code ec;
    for (const auto& root : steam_roots) {
        if (!fs::exists(root, ec)) continue;
        const fs::path norm = fs::weakly_canonical(root, ec);
        auto add = [&](const fs::path& p) {
            const fs::path n = fs::weakly_canonical(p, ec);
            for (const auto& l : libs) {
#ifdef _WIN32
                // Steam пише "c:\games", а реєстр — "C:\Games": на Windows це той самий шлях
                if (iequals(path_to_utf8(l), path_to_utf8(n))) return;
#else
                if (l == n) return;
#endif
            }
            libs.push_back(n);
        };
        add(norm);
        const fs::path vdf_path = norm / "steamapps" / "libraryfolders.vdf";
        auto text = read_file_text(vdf_path);
        if (!text) continue;
        auto tree = vdf::parse(*text);
        if (!tree) {
            if (log) log->push_back("Не вдалося розібрати " + path_to_utf8(vdf_path));
            continue;
        }
        const vdf::Node* lf = tree->child("libraryfolders");
        if (!lf) lf = tree->child("LibraryFolders");
        if (!lf) continue;
        for (const auto& [key, node] : lf->children) {
            std::string p = node->is_block ? node->get("path") : node->value;
            if (p.empty() || !parse_int(key)) continue;
            add(path_from_utf8(p));
        }
    }
    if (log)
        for (const auto& l : libs) log->push_back("Бібліотека Steam: " + path_to_utf8(l));
    return libs;
}

std::optional<GModInstall> detect_gmod(std::vector<std::string>* log) {
    std::error_code ec;
    for (const auto& lib : steam_library_folders(log)) {
        const fs::path manifest = lib / "steamapps" / "appmanifest_4000.acf";
        std::string installdir = "GarrysMod";
        if (auto text = read_file_text(manifest)) {
            if (auto tree = vdf::parse(*text)) {
                if (const vdf::Node* st = tree->child("AppState")) installdir = st->get("installdir", installdir);
            }
        } else if (!fs::exists(lib / "steamapps" / "common" / "GarrysMod", ec)) {
            continue;
        }
        if (auto g = gmod_from_dir(lib / "steamapps" / "common" / path_from_utf8(installdir))) {
            if (log) log->push_back("Знайдено Garry's Mod: " + path_to_utf8(g->root));
            return g;
        }
    }
    if (log) log->push_back("Garry's Mod не знайдено автоматично — вкажіть папку вручну");
    return std::nullopt;
}

} // namespace gmdr::game
