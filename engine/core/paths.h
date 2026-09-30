#pragma once

#include <filesystem>

namespace gmdr {

// %LOCALAPPDATA%\DemoGmodRender (Windows) or $XDG_CACHE_HOME/demogmodrender (Linux): caches, logs.
std::filesystem::path userCacheDir();
// %APPDATA%\DemoGmodRender (Windows) or $XDG_CONFIG_HOME/demogmodrender (Linux): settings.
std::filesystem::path userConfigDir();

} // namespace gmdr
