#include "assets/locator.h"

#include "assets/vdf.h"
#include "core/file.h"
#include "core/text.h"

#include <cstdlib>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gmdr::assets {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxConfigBytes = 4u << 20;

bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

Result<VdfNode> readVdf(const fs::path& p, bool escapes) {
    auto bytes = readWholeFile(p, kMaxConfigBytes);
    if (!bytes)
        return bytes.error();
    return parseVdf(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), escapes);
}

#ifdef _WIN32
std::optional<fs::path> registryPath(HKEY root, const wchar_t* subKey, const wchar_t* value) {
    wchar_t buf[1024];
    DWORD size = sizeof buf;
    if (RegGetValueW(root, subKey, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return std::nullopt;
    fs::path p(buf);
    if (!isDir(p))
        return std::nullopt;
    return p.make_preferred();
}
#endif

bool looksLikeGarrysmod(const fs::path& garrysmod) {
    return isFile(garrysmod / "garrysmod_dir.vpk") || isFile(garrysmod / "gameinfo.txt");
}

std::vector<fs::path> readLibraries(const fs::path& steamRoot, std::vector<std::string>& warnings) {
    std::vector<fs::path> libs{steamRoot};
    const fs::path vdfPath = steamRoot / "steamapps" / "libraryfolders.vdf";
    if (!isFile(vdfPath))
        return libs;
    auto vdf = readVdf(vdfPath, true);
    if (!vdf) {
        warnings.push_back("libraryfolders.vdf: " + vdf.error().message);
        return libs;
    }
    const VdfNode* root = vdf->child("libraryfolders");
    if (!root)
        root = vdf->child("LibraryFolders");
    if (!root)
        return libs;
    for (const auto& c : root->children) {
        fs::path p;
        if (c.object)
            p = pathFromUtf8(std::string(c.get("path")));
        else if (!c.key.empty() && c.key.find_first_not_of("0123456789") == std::string::npos)
            p = pathFromUtf8(c.value); // old format: "1" "D:\\SteamLibrary"
        if (p.empty() || !isDir(p))
            continue;
        std::error_code ec;
        bool dup = false;
        for (const auto& l : libs)
            dup = dup || fs::equivalent(l, p, ec);
        if (!dup)
            libs.push_back(p);
    }
    return libs;
}

// App install folder in a library from appmanifest_<id>.acf ("installdir").
std::optional<fs::path> appInstallDir(const std::vector<fs::path>& libs, std::string_view appId) {
    for (const auto& lib : libs) {
        const fs::path acf = lib / "steamapps" / ("appmanifest_" + std::string(appId) + ".acf");
        if (!isFile(acf))
            continue;
        auto vdf = readVdf(acf, true);
        if (!vdf)
            continue;
        const VdfNode* state = vdf->child("AppState");
        if (!state)
            continue;
        const auto dir = state->get("installdir");
        if (dir.empty())
            continue;
        const fs::path p = lib / "steamapps" / "common" / pathFromUtf8(std::string(dir));
        if (isDir(p))
            return p;
    }
    return std::nullopt;
}

// Game folders GMod can mount through its depot system (cfg/mountdepots.txt): folder -> Steam app id.
struct Depot {
    const char* folder;
    const char* appId;
};
constexpr Depot kDepots[] = {
    {"hl2", "220"},      {"cstrike", "240"},      {"tf", "440"},     {"dod", "300"},
    {"hl2mp", "320"},    {"lostcoast", "340"},    {"hl1", "280"},    {"hl1mp", "360"},
    {"episodic", "220"}, {"ep2", "220"},          {"portal", "400"}, {"ageofchivalry", "17510"},
    {"diprip", "17530"}, {"zeno_clash", "22200"},
};

void readMounts(GmodInstall& g) {
    const fs::path mountCfg = g.garrysmod / "cfg" / "mount.cfg";
    if (isFile(mountCfg)) {
        auto vdf = readVdf(mountCfg, false);
        if (!vdf) {
            g.warnings.push_back("mount.cfg: " + vdf.error().message);
        } else if (const VdfNode* root = vdf->child("mountcfg")) {
            for (const auto& c : root->children) {
                if (c.object)
                    continue;
                MountedGame m;
                m.name = c.key;
                m.path = pathFromUtf8(c.value);
                m.origin = "mount.cfg";
                m.found = isDir(m.path);
                g.mounts.push_back(std::move(m));
            }
        }
    }
    const fs::path depots = g.garrysmod / "cfg" / "mountdepots.txt";
    if (!isFile(depots))
        return;
    auto vdf = readVdf(depots, false);
    if (!vdf) {
        g.warnings.push_back("mountdepots.txt: " + vdf.error().message);
        return;
    }
    const VdfNode* root = vdf->child("gamedepotsystem");
    if (!root)
        return;
    for (const auto& c : root->children) {
        if (c.object || c.value != "1")
            continue;
        bool mountedByCfg = false;
        for (const auto& m : g.mounts)
            mountedByCfg = mountedByCfg || toLowerAscii(m.name) == toLowerAscii(c.key);
        if (mountedByCfg)
            continue;
        MountedGame m;
        m.name = c.key;
        m.origin = "mountdepots.txt";
        for (const auto& d : kDepots) {
            if (toLowerAscii(c.key) != d.folder)
                continue;
            if (auto dir = appInstallDir(g.libraries, d.appId)) {
                m.path = *dir / d.folder;
                m.found = isDir(m.path);
            }
        }
        // The same folder may already be mounted through mount.cfg.
        bool duplicate = false;
        for (const auto& other : g.mounts) {
            std::error_code ec;
            duplicate = duplicate || (m.found && other.found && fs::equivalent(other.path, m.path, ec));
        }
        if (duplicate)
            continue;
        // HL2 content ships with GMod (sourceengine/), so a missing HL2 install is normal.
        if (m.path.empty() && toLowerAscii(c.key) != "hl2")
            g.warnings.push_back("depot '" + c.key + "' is enabled but its game is not installed");
        g.mounts.push_back(std::move(m));
    }
}

} // namespace

std::optional<fs::path> findSteamRoot() {
#ifdef _WIN32
    if (auto p = registryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"))
        return p;
    if (auto p = registryPath(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"))
        return p;
    for (const char* guess : {"C:\\Program Files (x86)\\Steam", "C:\\Program Files\\Steam"})
        if (isDir(guess))
            return fs::path(guess);
#else
    if (const char* home = std::getenv("HOME")) {
        for (const char* rel : {"/.steam/steam", "/.local/share/Steam",
                                "/.var/app/com.valvesoftware.Steam/.local/share/Steam"}) {
            const fs::path p = fs::path(home).concat(rel);
            if (isDir(p / "steamapps"))
                return p;
        }
    }
#endif
    return std::nullopt;
}

Result<GmodInstall> locateGmod(const std::optional<fs::path>& manualRoot) {
    GmodInstall g;
    if (manualRoot) {
        fs::path root = *manualRoot;
        if (looksLikeGarrysmod(root)) // given the inner "garrysmod" folder
            root = root.parent_path();
        if (!looksLikeGarrysmod(root / "garrysmod"))
            return makeError("gmod.not_found", "Garry's Mod was not found at the given path",
                             pathToUtf8(*manualRoot));
        g.gmodRoot = root;
        g.origin = "manual";
        // The Steam library is two levels above: <lib>/steamapps/common/GarrysMod.
        const fs::path lib = root.parent_path().parent_path().parent_path();
        if (isDir(lib / "steamapps"))
            g.libraries.push_back(lib);
        if (auto steam = findSteamRoot()) {
            g.steamRoot = *steam;
            for (auto& l : readLibraries(*steam, g.warnings)) {
                std::error_code ec;
                bool dup = false;
                for (const auto& x : g.libraries)
                    dup = dup || fs::equivalent(x, l, ec);
                if (!dup)
                    g.libraries.push_back(l);
            }
        }
    } else {
        auto steam = findSteamRoot();
        if (!steam)
            return makeError("gmod.not_found", "Steam was not found; set the Garry's Mod folder in Settings");
        g.steamRoot = *steam;
        g.libraries = readLibraries(*steam, g.warnings);
        g.origin = "auto";
        if (auto dir = appInstallDir(g.libraries, "4000"); dir && looksLikeGarrysmod(*dir / "garrysmod")) {
            g.gmodRoot = *dir;
        } else {
            for (const auto& lib : g.libraries) {
                const fs::path p = lib / "steamapps" / "common" / "GarrysMod";
                if (looksLikeGarrysmod(p / "garrysmod")) {
                    g.gmodRoot = p;
                    break;
                }
            }
        }
        if (g.gmodRoot.empty())
            return makeError("gmod.not_found", "Garry's Mod is not installed in any Steam library",
                             pathToUtf8(*steam));
    }
    g.garrysmod = g.gmodRoot / "garrysmod";
    // Workshop content lives in the library that holds the game.
    const fs::path lib = g.gmodRoot.parent_path().parent_path().parent_path();
    g.workshopContent = lib / "steamapps" / "workshop" / "content" / "4000";
    readMounts(g);
    return g;
}

} // namespace gmdr::assets
