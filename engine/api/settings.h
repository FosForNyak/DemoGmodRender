#pragma once

#include "core/error.h"
#include "core/json.h"

#include <filesystem>
#include <mutex>
#include <string>

namespace gmdr::api {

// settings.json in the user config folder. Known keys are validated; "ui.*" keys belong to the UI and hold
// any JSON. Writes are atomic (temp file + rename).
class Settings {
public:
    explicit Settings(std::filesystem::path file);

    Json get(const std::string& key) const; // null if unset
    Json all() const;
    Result<void> set(const std::string& key, const Json& value);

    // Most recent first, at most 10 entries of {path, name, openedAt}.
    void addRecent(const std::string& path, const std::string& name);

    std::string gmodPath() const;          // "" = automatic
    std::uint64_t cacheLimitBytes() const; // default 20 GB

private:
    Result<void> save() const;

    std::filesystem::path file_;
    mutable std::mutex mutex_;
    Json data_ = Json::object();
};

} // namespace gmdr::api
