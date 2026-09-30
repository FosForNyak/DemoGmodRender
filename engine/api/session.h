#pragma once

#include "api/engine.h"
#include "api/values.h"
#include "core/hash.h"
#include "core/json.h"
#include "core/process.h"
#include "demo/statedb/reader.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gmdr::api {

enum class ImportState { Indexing, Importing, Ready, Failed, Cancelled };
const char* importStateName(ImportState s);

using Emit = std::function<void(const std::string& type, Json payload)>;

// One open demo: its state file (cached or being imported) and the importer that fills it.
class Session {
public:
    Session(std::string id, std::filesystem::path demoPath, Blake3Digest hash, std::filesystem::path dir);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    const std::string id;
    const std::filesystem::path demoPath;
    const Blake3Digest hash;
    const std::filesystem::path dir;

    std::filesystem::path statePath() const { return dir / "state.gmstate"; }
    std::filesystem::path partPath() const { return dir / "state.gmstate.part"; }

    // Uses a complete state file of this format and parser version, if there is one.
    Result<void> openCached();
    // Starts gmdr-import on inherited handles; progress arrives as events.
    Result<void> startImport(const EngineConfig& config, Emit emit);
    void cancel();
    // Stops the importer and waits for it; deletes an unfinished state file.
    void close();
    // Kills an importer that has not reported anything for `timeout`.
    void checkIdle(std::chrono::seconds timeout);

    struct Status {
        ImportState state = ImportState::Indexing;
        bool cached = false;
        Tick firstTick = 0, lastTick = -1, readyTick = -1;
        std::optional<Error> error;
        Json stats;
    };
    Status status() const;
    std::shared_ptr<demo::statedb::StateReader> reader() const;

    // Per-class info for the UI, built from the schema on first use.
    Result<std::shared_ptr<const std::vector<ClassInfo>>> classes();
    // Command result caches (content check, timeline summaries), valid while the session lives.
    std::mutex cacheMutex;
    std::optional<Json> contentCache;
    std::map<int, Json> summaryCache;

private:
    void supervise(Emit emit);
    void handleLine(const std::string& line, const Emit& emit);
    void finish(int exitCode, const Emit& emit);

    mutable std::mutex mutex_;
    Status status_;
    std::shared_ptr<demo::statedb::StateReader> reader_;
    std::unique_ptr<ChildProcess> child_;
    bool childRunning_ = false;
    bool done_ = false;
    std::optional<Error> childError_;
    std::thread supervisor_;
    std::atomic<bool> closing_{false};
    std::atomic<bool> cancelled_{false};
    std::atomic<std::int64_t> lastActivity_{0};
    std::chrono::steady_clock::time_point started_;
    std::shared_ptr<const std::vector<ClassInfo>> classes_;
};

} // namespace gmdr::api
