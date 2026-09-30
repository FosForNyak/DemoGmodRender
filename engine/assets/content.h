#pragma once

#include "assets/vfs.h"
#include "core/json.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gmdr::assets {

enum class ContentStatus {
    Found,             // somewhere in the search path
    Missing,           // not found anywhere
    Builtin,           // not a file (brush models "*N", sentences) or part of the map itself
    NotChecked,        // listed only (particle effect names, client Lua)
    WorkshopInstalled, // Workshop addon present as a GMA
    WorkshopLegacy,    // only the old LZMA Workshop format (_legacy.bin), not extracted
    WorkshopMissing,
};
const char* contentStatusName(ContentStatus s);

struct ContentItem {
    std::string kind; // map, model, sound, material, decal, generic, download, workshop, particle, lua
    std::string name; // as listed by the demo
    std::string path; // resolved game path ("sound/x.wav"), empty when not a file
    ContentStatus status = ContentStatus::Missing;
    std::string where;  // source kind name ("vpk", "gma", ...)
    std::string source; // source display name
    std::uint64_t workshopId = 0;
    std::string title;
};

struct ContentReport {
    std::vector<ContentItem> items;
    std::size_t found = 0, missing = 0, workshopInstalled = 0, workshopMissing = 0, notChecked = 0;
    Json toJson() const;
};

// Files that exist only for one demo: the map's pakfile (listed by the importer from the untrusted BSP).
// Searched after the shared VFS.
struct ContentOverlay {
    std::string name; // "maps/gm_x.bsp"
    std::unordered_set<std::string> files;
    void add(std::string_view path);
};

// Checks every resource the demo's MANIFEST chunk lists against the search path.
ContentReport checkContent(const Json& manifest, const Vfs& vfs, const ContentOverlay* pakfile = nullptr);

} // namespace gmdr::assets
