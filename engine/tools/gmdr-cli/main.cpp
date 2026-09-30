// gmdr-cli: headless client of the engine (ADR-003: the same command bus as the app).
//   call <cmd> [json]            run one engine command
//   run [--no-wait] <demo> ...   open a demo through the engine and run commands on it; info <demo> =
//   demo.info
// Developer tools that bypass the engine:
//   parse <demo>                 run the demo parser and print statistics as JSON
//   import <demo> <out.gmstate>  full import in-process (same pipeline as gmdr-import)
//   spawn-import <demo> <out>    import through the gmdr-import child process (inherited handles, Job Object)
//   verify <demo>                import to a temp file and compare the state file with the parser's own state
//                                at sampled ticks; also measures seek and step times

#include "api/engine.h"
#include "assets/archives.h"
#include "assets/content.h"
#include "assets/locator.h"
#include "assets/vfs.h"
#include "core/file.h"
#include "core/hash.h"
#include "core/json.h"
#include "core/limits.h"
#include "core/process.h"
#include "core/text.h"
#include "demo/import.h"
#include "demo/parser.h"
#include "demo/statedb/codec.h"
#include "demo/statedb/reader.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;
double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::uint64_t fileSize(const gmdr::File& f) {
    auto s = f.size();
    return s ? *s : 0;
}

void printError(const gmdr::Error& e) {
    std::cerr << e.code << ": " << e.message << (e.details.empty() ? "" : " (" + e.details + ")") << "\n";
}

struct StatsSink : gmdr::demo::DemoSink {
    std::map<std::string, std::uint64_t> eventsByKind;
    std::uint64_t newLives = 0;
    std::uint64_t decodeErrors = 0;
    std::vector<std::string> errorSamples;
    gmdr::demo::ServerInfo server;
    std::string map;
    void onHeader(const gmdr::demo::DemoHeader& h) override { map = h.mapName; }
    void onServerInfo(const gmdr::demo::ServerInfo& s) override { server = s; }
    void onEvent(const gmdr::demo::DemoEvent& e) override {
        ++eventsByKind[gmdr::demo::eventKindName(e.kind)];
    }
    void onEntityEnter(gmdr::Tick, const gmdr::demo::EntityRef&, bool newLife,
                       std::span<const gmdr::demo::PropValue>) override {
        newLives += newLife ? 1 : 0;
    }
    void onDecodeError(gmdr::Tick tick, std::uint64_t offset, const gmdr::Error& e) override {
        ++decodeErrors;
        if (errorSamples.size() < 10)
            errorSamples.push_back("tick " + std::to_string(tick) + " @" + std::to_string(offset) + ": " +
                                   e.code + " " + e.message + " " + e.details);
    }
};

struct Mapped {
    gmdr::File file;
    gmdr::MappedFile map;
};

std::optional<Mapped> mapDemo(const std::string& path) {
    auto file = gmdr::File::open(gmdr::pathFromUtf8(path), gmdr::File::Mode::Read);
    if (!file) {
        printError(file.error());
        return std::nullopt;
    }
    auto map = gmdr::MappedFile::map(*file);
    if (!map) {
        printError(map.error());
        return std::nullopt;
    }
    return Mapped{std::move(*file), std::move(*map)};
}

int cmdParse(const std::string& path) {
    auto demo = mapDemo(path);
    if (!demo)
        return 2;
    StatsSink sink;
    gmdr::demo::DemoParser parser(demo->map.data(), sink);
    const auto t0 = Clock::now();
    auto r = parser.run();
    const double secs = secondsSince(t0);
    const auto& st = parser.stats();
    gmdr::Json out = {
        {"ok", r.ok()},
        {"error", r.ok() ? "" : r.error().code + " " + r.error().message},
        {"seconds", secs},
        {"map", sink.map},
        {"tickInterval", sink.server.tickInterval},
        {"gamemode", sink.server.gamemode},
        {"packets", st.packets},
        {"entityEnters", st.entityEnters},
        {"entityUpdates", st.entityUpdates},
        {"entityLeaves", st.entityLeaves},
        {"newLives", sink.newLives},
        {"events", st.events},
        {"eventsByKind", sink.eventsByKind},
        {"decodeErrors", st.decodeErrors},
        {"errorSamples", sink.errorSamples},
    };
    std::cout << out.dump(2) << "\n";
    return r.ok() ? 0 : 1;
}

int cmdImport(const std::string& demoPath, const std::string& outPath) {
    auto demo = mapDemo(demoPath);
    if (!demo)
        return 2;
    auto out = gmdr::File::open(gmdr::pathFromUtf8(outPath), gmdr::File::Mode::CreateTruncate);
    if (!out) {
        printError(out.error());
        return 2;
    }
    const auto t0 = Clock::now();
    const auto hash = gmdr::blake3(demo->map.data());
    const double hashSecs = secondsSince(t0);
    double indexedAt = 0;
    std::uint64_t chunks = 0;
    gmdr::demo::ImportCallbacks cb;
    cb.onIndexed = [&](const gmdr::demo::IndexSummary&) { indexedAt = secondsSince(t0); };
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> byKind; // raw, stored
    cb.onChunk = [&](const gmdr::demo::statedb::ChunkRef& c) {
        ++chunks;
        auto& k = byKind[gmdr::demo::statedb::chunkKindName(c.kind)];
        k.first += c.rawSize;
        k.second += c.storedSize;
    };
    auto stats = gmdr::demo::importDemo(demo->map.data(), *out, hash, cb);
    const double secs = secondsSince(t0);
    if (!stats) {
        printError(stats.error());
        return 1;
    }
    const auto demoSize = demo->map.data().size();
    const auto stateSize = fileSize(*out);
    gmdr::Json j = {
        {"seconds", secs},
        {"hashSeconds", hashSecs},
        {"indexedSeconds", indexedAt},
        {"chunks", chunks},
        {"demoBytes", demoSize},
        {"stateBytes", stateSize},
        {"ratio", static_cast<double>(stateSize) / static_cast<double>(demoSize)},
        {"decodeErrors", stats->decodeErrors},
    };
    for (const auto& [kind, sizes] : byKind)
        j["chunkBytes"][kind] = {{"raw", sizes.first}, {"stored", sizes.second}};
    std::cout << j.dump(2) << "\n";
    return 0;
}

int cmdSpawnImport(const std::string& demoPath, const std::string& outPath) {
    auto in = gmdr::File::open(gmdr::pathFromUtf8(demoPath), gmdr::File::Mode::Read);
    auto out = gmdr::File::open(gmdr::pathFromUtf8(outPath), gmdr::File::Mode::CreateTruncate);
    if (!in || !out) {
        printError(!in ? in.error() : out.error());
        return 2;
    }
    if (auto r = in->setInheritable(true); !r) {
        printError(r.error());
        return 2;
    }
    if (auto r = out->setInheritable(true); !r) {
        printError(r.error());
        return 2;
    }
    gmdr::ProcessOptions opts;
    opts.executable = gmdr::currentExecutablePath().parent_path().parent_path() / "gmdr-import" /
#ifdef _WIN32
                      "gmdr-import.exe";
#else
                      "gmdr-import";
#endif
    opts.args = {"--in-handle", std::to_string(in->nativeHandle()), "--out-handle",
                 std::to_string(out->nativeHandle())};
    opts.inheritHandles = {in->nativeHandle(), out->nativeHandle()};
    opts.memoryLimitBytes = 4ull << 30;
    const auto t0 = Clock::now();
    auto child = gmdr::ChildProcess::spawn(opts);
    if (!child) {
        printError(child.error());
        return 2;
    }
    std::string line;
    std::uint64_t lines = 0;
    std::string last;
    while (child->readLine(line)) {
        ++lines;
        if (line.rfind("{\"chunk\"", 0) != 0 && line.rfind("{\"progress\"", 0) != 0)
            std::cout << line << "\n";
        last = line;
    }
    const int code = child->wait();
    std::cout << "exit " << code << ", " << lines << " lines, " << secondsSince(t0) << " s\n";
    return code;
}

// ---- verify ---------------------------------------------------------------------------------------------

struct ModelEntity {
    int classId = 0, serial = 0;
    std::uint32_t life = 0;
    bool inPvs = true;
    std::vector<gmdr::demo::PropValue> props;
};

std::uint64_t hashState(
    const std::vector<std::optional<ModelEntity>>& ents,
    const std::map<int,
                   std::pair<std::string, std::map<int, std::pair<std::string, std::vector<std::uint8_t>>>>>&
        tables) {
    gmdr::demo::statedb::Encoder e;
    for (std::size_t i = 0; i < ents.size(); ++i) {
        if (!ents[i])
            continue;
        const auto& x = *ents[i];
        e.varint(i);
        e.varint(static_cast<std::uint64_t>(x.classId));
        e.varint(static_cast<std::uint64_t>(x.serial));
        e.varint(x.life);
        e.u8(x.inPvs ? 1 : 0);
        for (const auto& p : x.props)
            e.value(p);
    }
    for (const auto& [id, t] : tables) {
        e.varint(static_cast<std::uint64_t>(id));
        e.string(t.first);
        for (const auto& [idx, entry] : t.second) {
            e.varint(static_cast<std::uint64_t>(idx));
            e.string(entry.first);
            e.bytes(entry.second);
        }
    }
    return gmdr::xxh3_64(e.data());
}

std::uint64_t hashWorld(const gmdr::demo::statedb::WorldState& s) {
    std::vector<std::optional<ModelEntity>> ents(s.entities.size());
    for (std::size_t i = 0; i < s.entities.size(); ++i)
        if (s.entities[i])
            ents[i] = ModelEntity{s.entities[i]->classId, s.entities[i]->serial, s.entities[i]->life,
                                  s.entities[i]->inPvs, s.entities[i]->props};
    std::map<int, std::pair<std::string, std::map<int, std::pair<std::string, std::vector<std::uint8_t>>>>>
        tables;
    for (const auto& [id, t] : s.tables) {
        auto& dst = tables[id];
        dst.first = t.name;
        for (const auto& [idx, entry] : t.entries)
            dst.second[idx] = entry;
    }
    return hashState(ents, tables);
}

struct VerifySink : gmdr::demo::DemoSink {
    std::vector<std::optional<ModelEntity>> ents =
        std::vector<std::optional<ModelEntity>>(gmdr::limits::kMaxEntities);
    std::map<int, std::pair<std::string, std::map<int, std::pair<std::string, std::vector<std::uint8_t>>>>>
        tables;
    std::vector<gmdr::Tick> sampleTicks; // sorted
    std::map<gmdr::Tick, std::uint64_t> expected;
    std::uint64_t updates = 0, fullChecks = 0, changedListMismatches = 0;
    gmdr::Tick lastSeenTick = -1;
    std::uint64_t nonMonotonicTicks = 0;

    void onStringTableChanged(gmdr::Tick, int id, const gmdr::demo::StringTable& t,
                              std::span<const int> changed, bool created) override {
        auto& dst = tables[id];
        if (created)
            dst.first = t.name();
        for (int idx : changed)
            if (const auto* entry = t.entry(static_cast<std::size_t>(idx)))
                dst.second[idx] = {entry->string, t.name() == "instancebaseline" ? std::vector<std::uint8_t>{}
                                                                                 : entry->userData};
    }
    void onEntityEnter(gmdr::Tick, const gmdr::demo::EntityRef& ref, bool,
                       std::span<const gmdr::demo::PropValue> state) override {
        ents[static_cast<std::size_t>(ref.index)] =
            ModelEntity{ref.classId, ref.serial, ref.life, true, {state.begin(), state.end()}};
    }
    void onEntityUpdate(gmdr::Tick, const gmdr::demo::EntityRef& ref, std::span<const int> changed,
                        std::span<const gmdr::demo::PropValue> state) override {
        auto& e = ents[static_cast<std::size_t>(ref.index)];
        if (!e)
            return;
        for (int idx : changed)
            e->props[static_cast<std::size_t>(idx)] = state[static_cast<std::size_t>(idx)];
        if (++updates % 97 == 0) {
            ++fullChecks;
            if (!std::equal(e->props.begin(), e->props.end(), state.begin(), state.end()))
                ++changedListMismatches;
        }
    }
    void onEntityLeave(gmdr::Tick, const gmdr::demo::EntityRef& ref, bool deleted) override {
        auto& e = ents[static_cast<std::size_t>(ref.index)];
        if (deleted)
            e.reset();
        else if (e)
            e->inPvs = false;
    }
    void onTickEnd(gmdr::Tick tick) override {
        if (tick < lastSeenTick)
            ++nonMonotonicTicks;
        lastSeenTick = tick;
        if (std::binary_search(sampleTicks.begin(), sampleTicks.end(), tick))
            expected[tick] = hashState(ents, tables); // the last packet of a tick wins
    }
};

int cmdVerify(const std::string& demoPath) {
    auto demo = mapDemo(demoPath);
    if (!demo)
        return 2;
    const auto tmp = std::filesystem::temp_directory_path() / "gmdr-verify.gmstate";
    {
        auto out = gmdr::File::open(tmp, gmdr::File::Mode::CreateTruncate);
        if (!out) {
            printError(out.error());
            return 2;
        }
        // Pass 1 alone, to choose the sample ticks.
        gmdr::Tick lastTick = 0;
        {
            gmdr::demo::CommandReader reader(demo->map.data());
            gmdr::demo::TimelineClock clock;
            gmdr::demo::CommandRecord rec;
            while (true) {
                auto more = reader.next(rec);
                if (!more || !*more)
                    break;
                lastTick = std::max<gmdr::Tick>(lastTick, clock.next(rec));
            }
        }
        VerifySink sink;
        const int samples = 400;
        for (int i = 0; i <= samples; ++i)
            sink.sampleTicks.push_back(lastTick * i / samples);
        for (gmdr::Tick t = 0; t < 70; ++t)
            sink.sampleTicks.push_back(t); // signon and the first keyframe
        std::sort(sink.sampleTicks.begin(), sink.sampleTicks.end());
        sink.sampleTicks.erase(std::unique(sink.sampleTicks.begin(), sink.sampleTicks.end()),
                               sink.sampleTicks.end());

        gmdr::demo::ImportCallbacks cb;
        cb.observer = &sink;
        const auto t0 = Clock::now();
        auto stats = gmdr::demo::importDemo(demo->map.data(), *out, gmdr::blake3(demo->map.data()), cb);
        if (!stats) {
            printError(stats.error());
            return 1;
        }
        std::cout << "import " << secondsSince(t0) << " s, state " << fileSize(*out) << " bytes ("
                  << static_cast<double>(fileSize(*out)) / static_cast<double>(demo->map.data().size())
                  << " of the demo)\n";
        std::cout << "updates " << sink.updates << ", full checks " << sink.fullChecks
                  << ", changed-list mismatches " << sink.changedListMismatches << ", non-monotonic ticks "
                  << sink.nonMonotonicTicks << "\n";
        out->close();

        auto reader = gmdr::demo::statedb::StateReader::open(tmp, true);
        if (!reader) {
            printError(reader.error());
            return 1;
        }
        auto& r = **reader;
        std::uint64_t mismatches = 0, checked = 0;
        // Forward order (cursor), then random order (keyframe + deltas) with timings.
        double maxStep = 0;
        for (const auto& [tick, want] : sink.expected) {
            const auto s0 = Clock::now();
            std::uint64_t got = 0;
            auto res = r.withStateAt(tick, [&](const auto& s) { got = hashWorld(s); });
            maxStep = std::max(maxStep, secondsSince(s0));
            if (!res) {
                printError(res.error());
                return 1;
            }
            ++checked;
            if (got != want) {
                if (mismatches < 10)
                    std::cout << "MISMATCH at tick " << tick << "\n";
                ++mismatches;
            }
        }
        std::vector<gmdr::Tick> order;
        for (const auto& [tick, want] : sink.expected)
            order.push_back(tick);
        std::mt19937 rng(42);
        std::shuffle(order.begin(), order.end(), rng);
        double maxSeek = 0, totalSeek = 0;
        for (gmdr::Tick tick : order) {
            const auto s0 = Clock::now();
            std::uint64_t got = 0;
            auto res = r.withStateAt(tick, [&](const auto& s) { got = hashWorld(s); });
            const double dt = secondsSince(s0);
            maxSeek = std::max(maxSeek, dt);
            totalSeek += dt;
            if (!res) {
                printError(res.error());
                return 1;
            }
            ++checked;
            if (got != sink.expected[tick]) {
                if (mismatches < 10)
                    std::cout << "MISMATCH (random) at tick " << tick << "\n";
                ++mismatches;
            }
        }
        // Frame stepping: 600 consecutive ticks from the middle, without hashing.
        const gmdr::Tick mid = lastTick / 2;
        double maxFrame = 0;
        for (gmdr::Tick t = mid; t < mid + 600; ++t) {
            const auto s0 = Clock::now();
            auto res = r.withStateAt(t, [](const auto&) {});
            if (t > mid)
                maxFrame = std::max(maxFrame, secondsSince(s0));
            if (!res) {
                printError(res.error());
                return 1;
            }
        }
        std::cout << "checked " << checked << " states, mismatches " << mismatches << "\n";
        std::cout << "seek (incl. hashing): avg " << 1000 * totalSeek / static_cast<double>(order.size())
                  << " ms, max " << 1000 * maxSeek << " ms; frame step max " << 1000 * maxFrame << " ms\n";
        return mismatches == 0 && sink.changedListMismatches == 0 ? 0 : 1;
    }
}

// ---- content ----------------------------------------------------------------------------------------------

std::optional<gmdr::assets::GmodInstall> locate(const char* manual) {
    std::optional<std::filesystem::path> root;
    if (manual)
        root = gmdr::pathFromUtf8(manual);
    auto g = gmdr::assets::locateGmod(root);
    if (!g) {
        printError(g.error());
        return std::nullopt;
    }
    return std::move(*g);
}

int cmdGmod(const char* manual) {
    auto g = locate(manual);
    if (!g)
        return 1;
    auto vfs = gmdr::assets::Vfs::build(*g);
    if (!vfs) {
        printError(vfs.error());
        return 1;
    }
    gmdr::Json mounts = gmdr::Json::array();
    for (const auto& m : g->mounts)
        mounts.push_back(
            {{"name", m.name}, {"path", gmdr::pathToUtf8(m.path)}, {"origin", m.origin}, {"found", m.found}});
    const auto& s = vfs->stats();
    gmdr::Json j = {
        {"origin", g->origin},
        {"steam", gmdr::pathToUtf8(g->steamRoot)},
        {"gmod", gmdr::pathToUtf8(g->gmodRoot)},
        {"libraries", g->libraries.size()},
        {"mounts", mounts},
        {"vfs",
         {{"seconds", s.seconds},
          {"vpks", s.vpks},
          {"vpkFiles", s.vpkFiles},
          {"gmas", s.gmas},
          {"gmaFiles", s.gmaFiles},
          {"badGmas", s.badGmas},
          {"addonFolders", s.addonFolders},
          {"workshopLegacy", s.workshopLegacy},
          {"workshopCaches", s.workshopCaches}}},
        {"warnings", g->warnings},
        {"vfsWarnings", vfs->warnings()},
    };
    std::cout << j.dump(2) << "\n";
    return 0;
}

int cmdContent(const std::string& statePath, const char* manual) {
    auto reader = gmdr::demo::statedb::StateReader::open(gmdr::pathFromUtf8(statePath), true);
    if (!reader) {
        printError(reader.error());
        return 1;
    }
    auto manifest = (*reader)->manifest();
    if (!manifest) {
        printError(manifest.error());
        return 1;
    }
    auto g = locate(manual);
    if (!g)
        return 1;
    auto vfs = gmdr::assets::Vfs::build(*g);
    if (!vfs) {
        printError(vfs.error());
        return 1;
    }
    // The map's pakfile. The engine lists it in gmdr-import; the CLI does it in-process.
    const std::string map = manifest->value("map", "");
    gmdr::assets::ContentOverlay pakfile;
    pakfile.name = "maps/" + map + ".bsp";
    if (auto slice = vfs->locate("maps/" + map + ".bsp")) {
        auto file = gmdr::File::open(slice->path, gmdr::File::Mode::Read);
        if (file) {
            auto names = gmdr::assets::listBspPakfile(*file, slice->offset, slice->size);
            if (names) {
                for (const auto& n : *names)
                    pakfile.add(n);
                std::cerr << "pakfile of " << map << ": " << names->size() << " files\n";
            } else {
                printError(names.error());
            }
        }
    }
    const auto t0 = Clock::now();
    auto report = gmdr::assets::checkContent(*manifest, *vfs, &pakfile);
    auto j = report.toJson();
    j["seconds"] = secondsSince(t0);
    gmdr::Json missing = gmdr::Json::array();
    std::map<std::string, std::uint64_t> where;
    for (const auto& item : j["items"]) {
        const std::string status = item["status"];
        if (status == "missing" || status == "workshop-missing" || status == "workshop-legacy")
            missing.push_back(item);
        if (item.contains("where"))
            ++where[item["where"].get<std::string>()];
    }
    j["missing"] = missing;
    j["foundIn"] = where;
    j.erase("items");
    std::cout << j.dump(2) << "\n";
    return 0;
}

// ---- command bus ------------------------------------------------------------------------------------------

gmdr::api::EngineConfig cliConfig() {
    auto config = gmdr::api::makeConfig(gmdr::Json());
    // In the build tree gmdr-import lives in a sibling folder.
    const auto exeDir = gmdr::currentExecutablePath().parent_path();
    for (const auto& candidate :
         {exeDir / "gmdr-import.exe", exeDir.parent_path() / "gmdr-import" / "gmdr-import.exe",
          exeDir / "gmdr-import", exeDir.parent_path() / "gmdr-import" / "gmdr-import"}) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            config->importer = candidate;
            break;
        }
    }
    if (const char* cache = std::getenv("GMDR_CACHE_DIR"))
        config->cacheDir = gmdr::pathFromUtf8(cache);
    return *config;
}

gmdr::Json parseArgs(const char* text) {
    if (!text)
        return gmdr::Json::object();
    auto j = gmdr::Json::parse(text, nullptr, false);
    return j.is_discarded() ? gmdr::Json() : j;
}

int printResult(const gmdr::Result<gmdr::Json>& r) {
    if (r) {
        std::cout << r->dump(2, ' ', false, gmdr::Json::error_handler_t::replace) << "\n";
        return 0;
    }
    std::cout << gmdr::Json{{"error",
                             {{"code", r.error().code},
                              {"message", r.error().message},
                              {"details", r.error().details}}}}
                     .dump(2)
              << "\n";
    return 1;
}

// call <cmd> [json]
int cmdCall(const char* name, const char* args) {
    gmdr::api::Engine engine(cliConfig());
    const auto a = parseArgs(args);
    if (a.is_null()) {
        std::cerr << "arguments are not valid JSON\n";
        return 64;
    }
    return printResult(engine.call(name, a));
}

// run [--no-wait] <demo> <cmd> [json] [<cmd> [json] ...]: opens the demo, waits for the import (unless
// --no-wait: then the commands run while it imports, e.g. "wait 500" pauses 500 ms), runs the commands.
int cmdRun(int argc, char** argv) {
    bool wait = true;
    if (argc >= 3 && std::string(argv[2]) == "--no-wait") {
        wait = false;
        ++argv;
        --argc;
    }
    if (argc < 3) {
        std::cerr << "usage: gmdr-cli run [--no-wait] <demo> <cmd> [json] ...\n";
        return 64;
    }
    gmdr::api::Engine engine(cliConfig());
    std::mutex m;
    std::condition_variable cv;
    bool finished = false;
    engine.setEventSink([&](const std::string& json) {
        auto e = gmdr::Json::parse(json, nullptr, false);
        const std::string type = e.is_object() ? e.value("type", "") : "";
        if (type == "import.progress")
            return;
        std::cerr << "event " << json << "\n";
        if (type == "import.done" || type == "import.failed") {
            std::lock_guard lock(m);
            finished = true;
            cv.notify_all();
        }
    });
    const auto demoPath = std::filesystem::absolute(gmdr::pathFromUtf8(argv[2]));
    const auto t0 = Clock::now();
    auto open = engine.call("demo.open", {{"path", gmdr::pathToUtf8(demoPath)}});
    if (!open)
        return printResult(open);
    const std::string demo = (*open)["demo"];
    if (wait && (*open)["import"]["state"] != "ready") {
        std::unique_lock lock(m);
        cv.wait(lock, [&] { return finished; });
        std::cerr << "ready after " << secondsSince(t0) << " s\n";
    }
    int rc = 0;
    for (int i = 3; i < argc;) {
        const char* name = argv[i++];
        if (std::string(name) == "wait" && i < argc) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(argv[i++])));
            continue;
        }
        gmdr::Json a = gmdr::Json::object();
        if (i < argc && argv[i][0] == '{')
            a = parseArgs(argv[i++]);
        if (!a.is_object()) {
            std::cerr << "arguments are not valid JSON\n";
            return 64;
        }
        a["demo"] = demo;
        std::cout << "== " << name << " " << a.dump() << "\n";
        const auto c0 = Clock::now();
        rc |= printResult(engine.call(name, a));
        std::cerr << name << ": " << 1000 * secondsSince(c0) << " ms\n";
    }
    (void)engine.call("demo.close", {{"demo", demo}});
    return rc;
}

// Prints the string tables and entity counts of a state file at its last tick.
int cmdInspect(const std::string& statePath) {
    auto reader = gmdr::demo::statedb::StateReader::open(gmdr::pathFromUtf8(statePath), true);
    if (!reader) {
        printError(reader.error());
        return 1;
    }
    gmdr::Json j;
    j["lastTick"] = (*reader)->lastTick();
    auto res = (*reader)->withStateAt((*reader)->lastTick(), [&](const gmdr::demo::statedb::WorldState& s) {
        std::size_t entities = 0, dormant = 0;
        for (const auto& e : s.entities)
            if (e) {
                ++entities;
                dormant += e->inPvs ? 0 : 1;
            }
        j["entities"] = entities;
        j["dormant"] = dormant;
        for (const auto& [id, t] : s.tables) {
            std::size_t strBytes = 0, udBytes = 0;
            for (const auto& [idx, entry] : t.entries) {
                strBytes += entry.first.size();
                udBytes += entry.second.size();
            }
            j["tables"][t.name] = {{"id", id},
                                   {"entries", t.entries.size()},
                                   {"stringBytes", strBytes},
                                   {"userDataBytes", udBytes}};
        }
    });
    if (!res) {
        printError(res.error());
        return 1;
    }
    std::cout << j.dump(2) << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc >= 2 ? argv[1] : "";
    if (cmd == "parse" && argc >= 3)
        return cmdParse(argv[2]);
    if (cmd == "import" && argc >= 4)
        return cmdImport(argv[2], argv[3]);
    if (cmd == "spawn-import" && argc >= 4)
        return cmdSpawnImport(argv[2], argv[3]);
    if (cmd == "verify" && argc >= 3)
        return cmdVerify(argv[2]);
    if (cmd == "inspect" && argc >= 3)
        return cmdInspect(argv[2]);
    if (cmd == "table" && argc >= 4) {
        // table <state> <name>: entries at the last tick, userdata as hex (first 160 bytes)
        auto reader = gmdr::demo::statedb::StateReader::open(gmdr::pathFromUtf8(argv[2]), true);
        if (!reader) {
            printError(reader.error());
            return 1;
        }
        auto res =
            (*reader)->withStateAt((*reader)->lastTick(), [&](const gmdr::demo::statedb::WorldState& s) {
                const auto* t = s.table(argv[3]);
                if (!t)
                    return;
                for (const auto& [idx, e] : t->entries) {
                    std::cout << idx << " '" << e.first << "' " << e.second.size() << "b:";
                    for (std::size_t i = 0; i < e.second.size() && i < 160; ++i) {
                        char hex[4];
                        std::snprintf(hex, sizeof hex, " %02x", e.second[i]);
                        std::cout << hex;
                    }
                    std::cout << "\n";
                }
            });
        return res ? 0 : 1;
    }
    if (cmd == "schema" && argc >= 4) {
        // schema <state> <class substring>: flattened props of matching classes
        auto reader = gmdr::demo::statedb::StateReader::open(gmdr::pathFromUtf8(argv[2]), true);
        auto sch = reader ? (*reader)->schema() : decltype((*reader)->schema())(reader.error());
        if (!sch) {
            printError(sch.error());
            return 1;
        }
        for (const auto& c : **sch) {
            if (c.name.find(argv[3]) == std::string::npos)
                continue;
            std::cout << c.id << " " << c.name << " (" << c.table << ") " << c.props.size() << " props\n";
            for (std::size_t i = 0; i < c.props.size(); ++i) {
                const auto& p = c.props[i];
                std::cout << "  " << i << " " << p.table << "." << p.name
                          << " type=" << static_cast<int>(p.type) << " bits=" << p.bits << " flags=0x"
                          << std::hex << p.flags << std::dec << "\n";
            }
        }
        return 0;
    }
    if (cmd == "call" && argc >= 3)
        return cmdCall(argv[2], argc >= 4 ? argv[3] : nullptr);
    if (cmd == "run" && argc >= 3)
        return cmdRun(argc, argv);
    if (cmd == "info" && argc >= 3) {
        char name[] = "demo.info";
        char* args[] = {argv[0], argv[1], argv[2], name};
        return cmdRun(4, args);
    }
    if (cmd == "gmod")
        return cmdGmod(argc >= 3 ? argv[2] : nullptr);
    if (cmd == "content" && argc >= 3)
        return cmdContent(argv[2], argc >= 4 ? argv[3] : nullptr);
    if (cmd == "manifest" && argc >= 3) {
        auto reader = gmdr::demo::statedb::StateReader::open(gmdr::pathFromUtf8(argv[2]), true);
        auto m = reader ? (*reader)->manifest() : gmdr::Result<gmdr::Json>(reader.error());
        if (!m) {
            printError(m.error());
            return 1;
        }
        std::cout << m->dump(2) << "\n";
        return 0;
    }
    std::cerr
        << "usage (command bus):\n"
           "  gmdr-cli call <cmd> [json]                      one engine command, e.g. call gmod.locate\n"
           "  gmdr-cli run [--no-wait] <demo> <cmd> [json]... open a demo and run commands on it\n"
           "  gmdr-cli info <demo>                            demo.info after the import\n"
           "developer tools:\n"
           "  gmdr-cli parse | verify <demo.dem>\n"
           "  gmdr-cli import | spawn-import <demo.dem> <out.gmstate>\n"
           "  gmdr-cli inspect | manifest <state.gmstate>, schema <state> <class>, table <state> <name>\n"
           "  gmdr-cli gmod [path], content <state.gmstate> [gmod path]\n"
           "GMDR_CACHE_DIR overrides the cache folder.\n";
    return 64;
}
