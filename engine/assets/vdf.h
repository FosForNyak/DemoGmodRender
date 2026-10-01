#pragma once

#include "core/error.h"

#include <string>
#include <string_view>
#include <vector>

namespace gmdr::assets {

// Valve KeyValues text ("VDF"): libraryfolders.vdf, appmanifest_*.acf, mount.cfg, mountdepots.txt.
struct VdfNode {
    std::string key;
    std::string value;             // empty for objects
    std::vector<VdfNode> children; // non-empty or `object` for objects
    bool object = false;

    // Case-insensitive lookups (Valve files are not consistent about case).
    const VdfNode* child(std::string_view key) const;
    std::string_view get(std::string_view key, std::string_view fallback = {}) const;
};

// Parses a whole file: returns a root object whose children are the top-level pairs. Handles quoted and
// bare tokens, // comments and [$CONDITION] suffixes (ignored). Bounded depth and size.
// `escapes`: Steam-written files (libraryfolders.vdf, *.acf) escape backslashes; game config files such as
// mount.cfg do not, and a path like "C:\tf" must stay as written.
Result<VdfNode> parseVdf(std::string_view text, bool escapes);

} // namespace gmdr::assets
