#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace gmdr {

// All paths cross module and FFI boundaries as UTF-8 strings.
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(std::string_view utf8);

// Replaces invalid UTF-8 sequences with U+FFFD so the text is safe to put into JSON.
std::string sanitizeUtf8(std::string_view bytes);

// ASCII-only lowercase, used for Source paths (case-insensitive, ASCII in practice).
std::string toLowerAscii(std::string_view s);
// Lowercase + forward slashes + no leading "./" or "/" : the VFS key for a game path.
std::string normalizeGamePath(std::string_view s);

bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);

} // namespace gmdr
