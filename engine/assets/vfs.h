#pragma once

#include "assets/archives.h"
#include "assets/locator.h"
#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gmdr::assets {

enum class SourceKind { Folder, AddonFolder, Gma, Vpk, MountFolder, MountVpk, Download, Pakfile };
const char* sourceKindName(SourceKind kind);

struct VfsHit {
    SourceKind kind = SourceKind::Folder;
    std::string source;           // display name: "garrysmod", "garrysmod_dir.vpk", "3001397905.gma" ...
    std::uint64_t workshopId = 0; // GMA from the Workshop
    std::string addonTitle;       // GMA title
};

// A byte range of a real file that holds one virtual file (used to hand a map to the importer).
struct FileSlice {
    std::filesystem::path path;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

enum class WorkshopState { Installed, Legacy, Missing };
struct WorkshopItem {
    WorkshopState state = WorkshopState::Missing;
    std::filesystem::path gma; // Installed
    std::string title;
};

// GMod's search path over the local install, in GMod's order: garrysmod/ -> addons/*/ -> Workshop GMAs ->
// garrysmod + fallbacks VPKs -> sourceengine + platform VPKs -> mounted games -> download/ -> map pakfile.
// Paths are case-insensitive with '/' separators (see normalizeGamePath).
class Vfs {
public:
    struct Stats {
        std::size_t vpks = 0, vpkFiles = 0;
        std::size_t gmas = 0, gmaFiles = 0, badGmas = 0;
        std::size_t addonFolders = 0;
        std::size_t workshopLegacy = 0; // *_legacy.bin (old LZMA Workshop format, not extracted)
        std::size_t workshopCaches = 0; // garrysmod/cache/workshop/*.cache
        std::size_t mounts = 0;
        double seconds = 0;
    };

    static Result<Vfs> build(const GmodInstall& install);

    std::optional<VfsHit> find(std::string_view path) const;
    std::optional<FileSlice> locate(std::string_view path) const;
    WorkshopItem workshopItem(std::uint64_t id) const;

    const Stats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    struct Source {
        SourceKind kind;
        std::string name;
        std::filesystem::path root; // folder sources: the folder; archives: the file
        std::uint64_t workshopId = 0;
        std::string title;
        VpkIndex vpk;
        GmaIndex gma;
    };
    bool contains(const Source& s, const std::string& path) const;

    std::vector<Source> sources_;
    std::map<std::uint64_t, WorkshopItem> workshop_;
    Stats stats_;
    std::vector<std::string> warnings_;
};

} // namespace gmdr::assets
