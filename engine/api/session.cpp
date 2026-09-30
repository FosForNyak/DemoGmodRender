#include "api/session.h"

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"
#include "demo/statedb/format.h"

namespace gmdr::api {

namespace fs = std::filesystem;
using demo::statedb::ChunkKind;
using demo::statedb::ChunkRef;
using demo::statedb::StateReader;

namespace {

std::int64_t nowTicks() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

std::optional<ChunkKind> chunkKindFromName(std::string_view name) {
    for (int k = 1; k <= 11; ++k)
        if (name == demo::statedb::chunkKindName(static_cast<ChunkKind>(k)))
            return static_cast<ChunkKind>(k);
    return std::nullopt;
}

Json errorJson(const Error& e) {
    Json j = {{"code", e.code}, {"message", e.message}};
    if (!e.details.empty())
        j["details"] = e.details;
    return j;
}

} // namespace

const char* importStateName(ImportState s) {
    switch (s) {
    case ImportState::Indexing:
        return "indexing";
    case ImportState::Importing:
        return "importing";
    case ImportState::Ready:
        return "ready";
    case ImportState::Failed:
        return "failed";
    case ImportState::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

Session::Session(std::string id_, fs::path demoPath_, Blake3Digest hash_, fs::path dir_)
    : id(std::move(id_)), demoPath(std::move(demoPath_)), hash(hash_), dir(std::move(dir_)) {}

Session::~Session() {
    close();
}

Result<void> Session::openCached() {
    std::error_code ec;
    if (!fs::is_regular_file(statePath(), ec))
        return makeError("statedb.missing", "no cached state file");
    auto r = StateReader::open(statePath(), true);
    if (!r)
        return r.error();
    if ((*r)->demoHash() != hash.bytes)
        return makeError("statedb.foreign", "cached state file belongs to another demo");
    std::shared_ptr<StateReader> reader(std::move(*r));
    auto info = reader->info();
    std::lock_guard lock(mutex_);
    reader_ = std::move(reader);
    status_.state = ImportState::Ready;
    status_.cached = true;
    status_.lastTick = reader_->lastTick();
    status_.readyTick = reader_->readyTick();
    if (info && info->contains("stats"))
        status_.stats = (*info)["stats"];
    fs::last_write_time(statePath(), fs::file_time_type::clock::now(), ec); // LRU for the cache limit
    return {};
}

Result<void> Session::startImport(const EngineConfig& config, Emit emit) {
    std::error_code ec;
    fs::remove(partPath(), ec);
    fs::remove(statePath(), ec);
    if (!fs::is_regular_file(config.importer, ec))
        return makeError("import.importer_missing", "the importer program is missing",
                         pathToUtf8(config.importer));

    auto in = File::open(demoPath, File::Mode::Read);
    if (!in)
        return in.error();
    auto out = File::open(partPath(), File::Mode::CreateTruncate);
    if (!out)
        return out.error();
    GMDR_TRY(in->setInheritable(true));
    GMDR_TRY(out->setInheritable(true));

    ProcessOptions opts;
    opts.executable = config.importer;
    opts.args = {"--in-handle",  std::to_string(in->nativeHandle()),
                 "--out-handle", std::to_string(out->nativeHandle()),
                 "--hash",       hash.hex()};
    opts.inheritHandles = {in->nativeHandle(), out->nativeHandle()};
    opts.memoryLimitBytes = config.importerMemoryBytes;
    auto child = ChildProcess::spawn(opts);
    if (!child)
        return child.error();
    // The child has its own copies of the handles; ours close when `in` and `out` go out of scope.
    {
        std::lock_guard lock(mutex_);
        child_ = std::make_unique<ChildProcess>(std::move(*child));
        childRunning_ = true;
        status_ = Status{};
        status_.state = ImportState::Indexing;
    }
    started_ = std::chrono::steady_clock::now();
    lastActivity_ = nowTicks();
    supervisor_ = std::thread([this, emit = std::move(emit)] { supervise(emit); });
    return {};
}

void Session::supervise(Emit emit) {
    std::string line;
    while (child_->readLine(line)) {
        lastActivity_ = nowTicks();
        handleLine(line, emit);
    }
    {
        std::lock_guard lock(mutex_);
        childRunning_ = false;
    }
    finish(child_->wait(), emit);
}

void Session::handleLine(const std::string& line, const Emit& emit) {
    auto j = Json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.size() != 1) {
        logWarn("import", "unreadable line from the importer");
        return;
    }
    const auto entry = j.begin();
    const std::string type = entry.key();
    const Json& body = *entry;
    if (!body.is_object())
        return;
    if (type == "indexed") {
        std::lock_guard lock(mutex_);
        status_.state = ImportState::Importing;
        status_.firstTick = body.value("firstTick", Tick{0});
        status_.lastTick = body.value("lastTick", Tick{-1});
        emit("import.indexed",
             {{"demo", id}, {"firstTick", status_.firstTick}, {"lastTick", status_.lastTick}});
    } else if (type == "chunk") {
        const auto kind = chunkKindFromName(body.value("kind", ""));
        if (!kind)
            return;
        ChunkRef ref;
        ref.kind = *kind;
        ref.offset = body.value("offset", std::uint64_t{0});
        ref.tickFrom = body.value("from", Tick{0});
        ref.tickTo = body.value("to", Tick{0});
        ref.rawSize = body.value("raw", std::uint32_t{0});
        ref.storedSize = body.value("stored", std::uint32_t{0});
        std::shared_ptr<StateReader> reader;
        {
            std::lock_guard lock(mutex_);
            reader = reader_;
        }
        if (!reader) {
            // The header is written before the first chunk, so the file can be opened now.
            auto r = StateReader::open(partPath(), false);
            if (!r) {
                logWarn("import", "cannot open the growing state file: " + r.error().message);
                return;
            }
            reader = std::move(*r);
            std::lock_guard lock(mutex_);
            reader_ = reader;
        }
        if (auto r = reader->addChunk(ref); !r) {
            logWarn("import", "bad chunk announcement: " + r.error().message);
            return;
        }
        if (ref.kind == ChunkKind::Deltas) {
            std::lock_guard lock(mutex_);
            status_.readyTick = std::max(status_.readyTick, ref.tickTo);
        }
    } else if (type == "progress") {
        Json payload;
        {
            std::lock_guard lock(mutex_);
            payload = {{"demo", id},
                       {"tick", body.value("tick", Tick{0})},
                       {"readyTick", status_.readyTick},
                       {"lastTick", status_.lastTick}};
        }
        emit("import.progress", std::move(payload));
    } else if (type == "done") {
        std::lock_guard lock(mutex_);
        done_ = true;
        status_.stats = body;
    } else if (type == "error") {
        std::lock_guard lock(mutex_);
        childError_ = makeError(body.value("code", "import.failed"), body.value("message", "import failed"),
                                body.value("details", ""));
    }
}

void Session::finish(int exitCode, const Emit& emit) {
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    bool ok = false;
    {
        std::lock_guard lock(mutex_);
        ok = done_ && exitCode == 0 && !closing_;
    }
    if (ok) {
        // Publish the finished file under its final name and reopen it through its directory.
        std::error_code ec;
        fs::rename(partPath(), statePath(), ec);
        auto r = ec ? Result<std::unique_ptr<StateReader>>(
                          makeError("import.rename", "cannot finish the state file", ec.message()))
                    : StateReader::open(statePath(), true);
        std::lock_guard lock(mutex_);
        if (r) {
            reader_ = std::shared_ptr<StateReader>(std::move(*r));
            status_.state = ImportState::Ready;
            status_.readyTick = reader_->readyTick();
            status_.lastTick = reader_->lastTick();
            Json stats = status_.stats;
            stats["seconds"] = seconds;
            emit("import.done", {{"demo", id}, {"stats", stats}});
            return;
        }
        status_.state = ImportState::Failed;
        status_.error = r.error();
        emit("import.failed", {{"demo", id}, {"error", errorJson(r.error())}});
        return;
    }
    std::lock_guard lock(mutex_);
    if (closing_ || cancelled_) {
        status_.state = ImportState::Cancelled;
        if (!closing_)
            emit("import.failed",
                 {{"demo", id}, {"error", errorJson(makeError("import.cancelled", "import was cancelled"))}});
        return;
    }
    status_.state = ImportState::Failed;
    status_.error = childError_ ? *childError_
                                : makeError("import.crashed", "the importer stopped unexpectedly",
                                            "exit code " + std::to_string(exitCode));
    emit("import.failed",
         {{"demo", id}, {"error", errorJson(*status_.error)}, {"readyTick", status_.readyTick}});
}

void Session::cancel() {
    cancelled_ = true;
    std::lock_guard lock(mutex_);
    if (child_ && childRunning_)
        child_->kill();
}

void Session::close() {
    closing_ = true;
    {
        std::lock_guard lock(mutex_);
        if (child_ && childRunning_)
            child_->kill();
    }
    if (supervisor_.joinable())
        supervisor_.join();
    std::lock_guard lock(mutex_);
    if (status_.state != ImportState::Ready && child_) {
        reader_.reset();
        std::error_code ec;
        fs::remove(partPath(), ec);
        fs::remove(dir, ec); // only if empty
    }
    child_.reset();
}

void Session::checkIdle(std::chrono::seconds timeout) {
    std::lock_guard lock(mutex_);
    if (!child_ || !childRunning_)
        return;
    const auto idle = std::chrono::steady_clock::duration(nowTicks() - lastActivity_.load());
    if (idle > timeout) {
        childError_ = makeError("import.stalled", "the importer stopped making progress",
                                std::to_string(timeout.count()) + " s without progress");
        child_->kill();
    }
}

Session::Status Session::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::shared_ptr<StateReader> Session::reader() const {
    std::lock_guard lock(mutex_);
    return reader_;
}

Result<std::shared_ptr<const std::vector<ClassInfo>>> Session::classes() {
    {
        std::lock_guard lock(cacheMutex);
        if (classes_)
            return classes_;
    }
    auto r = reader();
    if (!r)
        return makeError("import.not_ready", "the demo is still being indexed");
    auto schema = r->schema();
    if (!schema)
        return schema.error();
    auto out = std::make_shared<std::vector<ClassInfo>>();
    for (const auto& c : **schema)
        out->push_back(describeClass(c));
    std::lock_guard lock(cacheMutex);
    classes_ = out;
    return classes_;
}

} // namespace gmdr::api
