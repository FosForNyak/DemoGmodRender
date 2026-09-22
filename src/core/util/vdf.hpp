// =============================================================================
//  vdf.hpp — розбір текстового формату Valve KeyValues (.vdf / .acf).
//  Потрібен, щоб знайти, у якій бібліотеці Steam встановлено Garry's Mod
//  (steamapps/libraryfolders.vdf та appmanifest_4000.acf).
// =============================================================================
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gmdr::vdf {

struct Node {
    std::string                                       value;      // якщо це лист
    std::vector<std::pair<std::string, std::unique_ptr<Node>>> children;
    bool                                              is_block = false;

    const Node*  child(std::string_view key) const;          // без урахування регістру
    std::string  get(std::string_view key, std::string def = {}) const;
};

std::unique_ptr<Node> parse(std::string_view text);

} // namespace gmdr::vdf
