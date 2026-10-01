#pragma once

#include "core/error.h"
#include "core/json.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace gmdr::api {

struct EngineConfig {
    std::filesystem::path cacheDir;  // default: userCacheDir()
    std::filesystem::path configDir; // default: userConfigDir()
    std::filesystem::path importer;  // default: gmdr-import next to the running executable
    std::uint64_t importerMemoryBytes = 4ull << 30;
    std::chrono::seconds importerIdleTimeout{60};
    // Windows: AppContainer for gmdr-import (empty = none). Falls back to the Job Object alone, with a log
    // warning, when the importer's folder is not readable by AppContainer apps.
#ifdef _WIN32
    std::string importerAppContainer = "DemoGmodRender.Importer";
#else
    std::string importerAppContainer; // POSIX: fork/exec with RLIMIT_AS, no AppContainer equivalent
#endif
};

// Fills defaults and applies {"cacheDir", "configDir", "importer"} overrides.
Result<EngineConfig> makeConfig(const Json& overrides);

using EventSink = std::function<void(const std::string& json)>;

// The engine behind the command bus: settings, GMod install, open demos and their importers.
class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // {"cmd", "args"} -> {"ok", "result"|"error"}; never throws.
    std::string callJson(std::string_view request);
    // Result or error of one command; never throws.
    Result<Json> call(std::string_view cmd, const Json& args);

    void setEventSink(EventSink sink);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace gmdr::api
