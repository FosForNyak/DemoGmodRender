#include "api/settings.h"

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"

#include <chrono>

namespace gmdr::api {

namespace {

constexpr std::uint64_t kDefaultCacheLimit = 20ull << 30;
constexpr std::size_t kMaxSettingsBytes = 4u << 20;
constexpr std::size_t kMaxRecent = 10;

Result<void> validate(const std::string& key, const Json& value) {
    if (startsWith(key, "ui.") && key.size() > 3)
        return {};
    if (key == "gmod.path") {
        if (!value.is_string() && !value.is_null())
            return makeError("settings.bad_value", "gmod.path must be a string", key);
        return {};
    }
    if (key == "cache.limitBytes") {
        if (!value.is_null() && !(value.is_number_integer() && value.get<std::int64_t>() >= 0))
            return makeError("settings.bad_value", "cache.limitBytes must be a non-negative integer", key);
        return {};
    }
    if (key == "recent") {
        if (!value.is_array() && !value.is_null())
            return makeError("settings.bad_value", "recent must be a list", key);
        return {};
    }
    return makeError("settings.unknown_key", "unknown setting", key);
}

} // namespace

Settings::Settings(std::filesystem::path file) : file_(std::move(file)) {
    auto bytes = readWholeFile(file_, kMaxSettingsBytes);
    if (!bytes)
        return; // first run
    auto j = Json::parse(bytes->begin(), bytes->end(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        logWarn("settings", "settings.json is damaged; starting with defaults");
        return;
    }
    // Keep only valid entries: a hand-edited file must not break the engine.
    for (auto it = j.begin(); it != j.end(); ++it)
        if (validate(it.key(), it.value()))
            data_[it.key()] = it.value();
}

Json Settings::get(const std::string& key) const {
    std::lock_guard lock(mutex_);
    auto it = data_.find(key);
    return it == data_.end() ? Json() : *it;
}

Json Settings::all() const {
    std::lock_guard lock(mutex_);
    return data_;
}

Result<void> Settings::set(const std::string& key, const Json& value) {
    GMDR_TRY(validate(key, value));
    std::lock_guard lock(mutex_);
    if (value.is_null())
        data_.erase(key);
    else
        data_[key] = value;
    return save();
}

void Settings::addRecent(const std::string& path, const std::string& name) {
    std::lock_guard lock(mutex_);
    Json list = Json::array();
    const auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    list.push_back({{"path", path}, {"name", name}, {"openedAt", now}});
    if (auto it = data_.find("recent"); it != data_.end() && it->is_array())
        for (const auto& r : *it)
            if (r.is_object() && r.value("path", "") != path && list.size() < kMaxRecent)
                list.push_back(r);
    data_["recent"] = std::move(list);
    if (auto r = save(); !r)
        logWarn("settings", "could not save settings: " + r.error().message);
}

std::string Settings::gmodPath() const {
    std::lock_guard lock(mutex_);
    auto it = data_.find("gmod.path");
    return it != data_.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::uint64_t Settings::cacheLimitBytes() const {
    std::lock_guard lock(mutex_);
    auto it = data_.find("cache.limitBytes");
    if (it == data_.end() || !it->is_number_integer() || it->get<std::int64_t>() < 0)
        return kDefaultCacheLimit;
    return it->get<std::uint64_t>();
}

Result<void> Settings::save() const {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    const std::string text = data_.dump(2);
    const auto tmp = std::filesystem::path(file_).concat(".tmp");
    {
        auto f = File::open(tmp, File::Mode::CreateTruncate);
        if (!f)
            return f.error();
        GMDR_TRY(f->writeAt(0, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())));
        GMDR_TRY(f->flush());
    }
    std::filesystem::rename(tmp, file_, ec);
    if (ec)
        return makeError("settings.save_failed", "could not save settings", ec.message());
    return {};
}

} // namespace gmdr::api
