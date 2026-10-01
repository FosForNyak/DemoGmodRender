// Corpus regression (spec §12.2): every .dem in $GMDR_CORPUS is parsed and summarised — counts, events by
// kind, decode error codes, lives and entity-state hashes at 10 ticks — and compared with
// tests/corpus/<name>.summary.json. The demos never enter the repository (they hold other players' data); the
// summaries hold no names or SteamIDs. A missing summary is written on the first run; GMDR_CORPUS_UPDATE=1
// rewrites them after an intended decoder change.
#include "core/file.h"
#include "core/hash.h"
#include "core/json.h"
#include "core/text.h"
#include "demo/commands.h"
#include "demo/parser.h"
#include "demo/statedb/codec.h"

#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <filesystem>
#include <map>
#include <optional>

using namespace gmdr;
using namespace gmdr::demo;
namespace fs = std::filesystem;

namespace {

struct CorpusSink : DemoSink {
    struct Ent {
        int classId = 0, serial = 0;
        std::uint32_t life = 0;
        bool inPvs = true;
        std::vector<PropValue> props;
    };
    std::vector<std::optional<Ent>> ents = std::vector<std::optional<Ent>>(1u << 13);
    std::vector<Tick> sampleTicks;
    std::map<Tick, std::string> hashes;
    std::map<std::string, std::uint64_t> eventsByKind;
    std::map<std::string, std::uint64_t> errorCodes;
    std::uint64_t newLives = 0, classes = 0;

    void onDataTables(const DataTables& dt) override { classes = dt.classes.size(); }
    void onEvent(const DemoEvent& e) override { ++eventsByKind[eventKindName(e.kind)]; }
    void onDecodeError(Tick, std::uint64_t, const Error& e) override { ++errorCodes[e.code]; }
    void onEntityEnter(Tick, const EntityRef& ref, bool newLife, std::span<const PropValue> s) override {
        newLives += newLife ? 1 : 0;
        ents[static_cast<std::size_t>(ref.index)] =
            Ent{ref.classId, ref.serial, ref.life, true, {s.begin(), s.end()}};
    }
    void onEntityUpdate(Tick, const EntityRef& ref, std::span<const int> changed,
                        std::span<const PropValue> s) override {
        auto& e = ents[static_cast<std::size_t>(ref.index)];
        if (!e)
            return;
        for (int i : changed)
            e->props[static_cast<std::size_t>(i)] = s[static_cast<std::size_t>(i)];
    }
    void onEntityLeave(Tick, const EntityRef& ref, bool deleted) override {
        auto& e = ents[static_cast<std::size_t>(ref.index)];
        if (deleted)
            e.reset();
        else if (e)
            e->inPvs = false;
    }
    std::string hashState() const {
        statedb::Encoder enc;
        for (std::size_t i = 0; i < ents.size(); ++i) {
            if (!ents[i])
                continue;
            enc.varint(i);
            enc.varint(static_cast<std::uint64_t>(ents[i]->classId));
            enc.varint(static_cast<std::uint64_t>(ents[i]->serial));
            enc.varint(ents[i]->life);
            enc.u8(ents[i]->inPvs ? 1 : 0);
            for (const auto& p : ents[i]->props)
                enc.value(p);
        }
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(xxh3_64(enc.data())));
        return hex;
    }
    // The state at sample tick t = after every packet with tick <= t: hash it just before the first packet
    // of a later tick is applied.
    std::size_t nextSample = 0;
    void onPacket(const CommandRecord& rec) override {
        while (nextSample < sampleTicks.size() && rec.tick > sampleTicks[nextSample])
            hashes[sampleTicks[nextSample++]] = hashState();
    }
    void finish() {
        while (nextSample < sampleTicks.size())
            hashes[sampleTicks[nextSample++]] = hashState();
    }
};

Json summarise(const fs::path& path) {
    auto file = File::open(path, File::Mode::Read);
    REQUIRE(file);
    auto map = MappedFile::map(*file);
    REQUIRE(map);
    Tick last = 0;
    {
        CommandReader reader(map->data());
        TimelineClock clock;
        CommandRecord rec;
        while (true) {
            auto more = reader.next(rec);
            if (!more || !*more)
                break;
            last = std::max<Tick>(last, clock.next(rec));
        }
    }
    CorpusSink sink;
    for (int i = 1; i <= 10; ++i)
        sink.sampleTicks.push_back(last * i / 10);
    DemoParser parser(map->data(), sink);
    auto r = parser.run();
    sink.finish();
    const auto& st = parser.stats();
    Json hashes = Json::object();
    for (const auto& [t, h] : sink.hashes)
        hashes[std::to_string(t)] = h;
    return {
        {"demoBlake3", blake3(map->data()).hex()},
        {"bytes", map->data().size()},
        {"parsed", r.ok()},
        {"lastTick", last},
        {"packets", st.packets},
        {"entityEnters", st.entityEnters},
        {"entityUpdates", st.entityUpdates},
        {"entityLeaves", st.entityLeaves},
        {"newLives", sink.newLives},
        {"classes", sink.classes},
        {"events", st.events},
        {"eventsByKind", sink.eventsByKind},
        {"decodeErrors", sink.errorCodes},
        {"stateHashes", hashes},
    };
}

} // namespace

TEST_CASE("corpus: demos in $GMDR_CORPUS match their stored summaries") {
    const char* dir = std::getenv("GMDR_CORPUS");
    if (!dir || !*dir) {
        MESSAGE("GMDR_CORPUS is not set: corpus regression skipped");
        return;
    }
    const bool update =
        std::getenv("GMDR_CORPUS_UPDATE") && std::string(std::getenv("GMDR_CORPUS_UPDATE")) == "1";
    const fs::path summaries = fs::path(GMDR_TEST_DATA_DIR).parent_path() / "corpus";
    std::error_code ec;
    fs::create_directories(summaries, ec);
    int checked = 0;
    for (const auto& entry : fs::directory_iterator(pathFromUtf8(dir))) {
        if (!entry.is_regular_file() || toLowerAscii(pathToUtf8(entry.path().extension())) != ".dem")
            continue;
        const auto name = pathToUtf8(entry.path().stem());
        CAPTURE(name);
        const Json got = summarise(entry.path());
        const fs::path file = summaries / (name + ".summary.json");
        if (update || !fs::exists(file)) {
            const std::string text = got.dump(2) + "\n";
            auto f = File::open(file, File::Mode::CreateTruncate);
            REQUIRE(f);
            REQUIRE(
                f->writeAt(0, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())));
            MESSAGE("wrote " << pathToUtf8(file));
            continue;
        }
        auto bytes = readWholeFile(file, 1u << 20);
        REQUIRE(bytes);
        const Json want = Json::parse(bytes->begin(), bytes->end());
        if (want.value("demoBlake3", std::string()) != got.value("demoBlake3", std::string())) {
            MESSAGE("a different file has this name; summary not compared");
            continue;
        }
        for (auto it = want.begin(); it != want.end(); ++it) {
            const std::string key = it.key();
            CAPTURE(key);
            REQUIRE(got.contains(key));
            const bool same = got.at(key) == it.value();
            if (!same)
                MESSAGE("expected " << it.value().dump() << "\ngot      " << got.at(key).dump());
            CHECK(same);
        }
        ++checked;
    }
    MESSAGE(checked << " corpus demo(s) checked");
}
