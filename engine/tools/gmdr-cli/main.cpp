// gmdr-cli: headless client of the engine.
//   parse <demo>                 run the demo parser and print statistics as JSON
//   import <demo> <out.gmstate>  full import in-process (same pipeline as gmdr-import)
//   spawn-import <demo> <out>    import through the gmdr-import child process (inherited handles, Job Object)
//   verify <demo>                import to a temp file and compare the state file with the parser's own state
//                                at sampled ticks; also measures seek and step times

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
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>

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
    std::cerr << "usage:\n  gmdr-cli parse <demo.dem>\n  gmdr-cli import <demo.dem> <out.gmstate>\n"
                 "  gmdr-cli spawn-import <demo.dem> <out.gmstate>\n  gmdr-cli verify <demo.dem>\n";
    return 64;
}
