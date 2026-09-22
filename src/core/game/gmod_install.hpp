// =============================================================================
//  gmod_install.hpp — пошук встановленого Garry's Mod (через бібліотеки Steam).
// =============================================================================
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gmdr::game {

namespace fs = std::filesystem;

struct GModInstall {
    fs::path              root;          // ...\steamapps\common\GarrysMod
    fs::path              garrysmod;     // ...\GarrysMod\garrysmod
    std::vector<fs::path> executables;   // знайдені .exe (64-біт першим)

    bool     valid() const { return !garrysmod.empty() && !executables.empty(); }
    fs::path default_exe() const { return executables.empty() ? fs::path() : executables.front(); }
    static std::string exe_label(const fs::path& exe);   // "64-біт (gmod.exe)" тощо
};

// Автоматичний пошук (реєстр Windows / стандартні шляхи Steam на Linux).
std::optional<GModInstall> detect_gmod(std::vector<std::string>* log = nullptr);
// Перевірити конкретну папку (можна вказати як GarrysMod, так і GarrysMod\garrysmod).
std::optional<GModInstall> gmod_from_dir(const fs::path& dir);

// Усі бібліотеки Steam (для діагностики).
std::vector<fs::path> steam_library_folders(std::vector<std::string>* log = nullptr);

} // namespace gmdr::game
