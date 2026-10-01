#pragma once

#include "core/error.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gmdr::assets {

struct MountedGame {
    std::string name;           // mount.cfg key or depot folder ("cstrike")
    std::filesystem::path path; // game content folder (contains *_dir.vpk and/or models/, materials/ ...)
    std::string origin;         // "mount.cfg" or "mountdepots.txt"
    bool found = false;
};

struct GmodInstall {
    std::filesystem::path steamRoot;              // empty when only a manual path is known
    std::vector<std::filesystem::path> libraries; // Steam library roots
    std::filesystem::path gmodRoot;               // .../steamapps/common/GarrysMod
    std::filesystem::path garrysmod;              // gmodRoot / "garrysmod"
    std::filesystem::path workshopContent;        // .../steamapps/workshop/content/4000 (may not exist)
    std::string origin;                           // "auto" or "manual"
    std::vector<MountedGame> mounts;
    std::vector<std::string> warnings;
};

// Finds Steam (registry / well-known paths), its libraries (libraryfolders.vdf) and Garry's Mod (app 4000).
// A manual path (GarrysMod or GarrysMod/garrysmod) overrides the search. Never writes anything.
Result<GmodInstall> locateGmod(const std::optional<std::filesystem::path>& manualRoot);

// Steam root from the registry or the usual install locations; nullopt if none is found.
std::optional<std::filesystem::path> findSteamRoot();

} // namespace gmdr::assets
