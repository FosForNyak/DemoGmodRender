#include "bit_writer.h"
#include "core/file.h"
#include "core/hash.h"
#include "demo/statedb/reader.h"
#include "demo/statedb/writer.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <map>
#include <random>
#include <set>

using namespace gmdr;
using namespace gmdr::demo;
using namespace gmdr::demo::statedb;

namespace {

struct TempFile {
    std::filesystem::path path;
    TempFile() {
        static int counter = 0;
        path =
            std::filesystem::temp_directory_path() /
            ("gmdr_statedb_test_" + std::to_string(std::random_device{}()) + "_" + std::to_string(counter++));
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

DataTables makeTables() {
    DataTables dt;
    SendTable t;
    t.name = "DT_Test";
    SendProp health;
    health.type = PropType::Int;
    health.name = "m_iHealth";
    health.bits = 8;
    health.flags = prop_flags::Unsigned;
    SendProp speed;
    speed.type = PropType::Float;
    speed.name = "m_flSpeed";
    speed.flags = prop_flags::NoScale;
    SendProp origin;
    origin.type = PropType::Vector;
    origin.name = "m_vecOrigin";
    origin.flags = prop_flags::Coord;
    SendProp model;
    model.type = PropType::String;
    model.name = "m_szModel";
    t.props = {health, speed, origin, model};
    dt.tables.push_back(t);
    dt.tableIndex["DT_Test"] = 0;
    dt.classes.push_back({0, "CTest", "DT_Test"});
    return dt;
}

struct Model {
    struct Ent {
        int classId = 0, serial = 0;
        std::uint32_t life = 0;
        bool inPvs = true;
        std::vector<PropValue> props;
    };
    std::map<int, Ent> ents;
    std::map<int, std::pair<std::string, std::map<int, std::string>>> tables;
    std::map<int, std::set<int>> changed; // at this tick
};

void checkState(const WorldState& s, const Model& m) {
    std::size_t present = 0;
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        if (!s.entities[i])
            continue;
        ++present;
        auto it = m.ents.find(static_cast<int>(i));
        REQUIRE(it != m.ents.end());
        const auto& e = *s.entities[i];
        CHECK(e.classId == it->second.classId);
        CHECK(e.serial == it->second.serial);
        CHECK(e.life == it->second.life);
        CHECK(e.inPvs == it->second.inPvs);
        CHECK(e.props == it->second.props);
    }
    CHECK(present == m.ents.size());
    std::map<int, std::vector<int>> wantChanged;
    for (const auto& [idx, set] : m.changed)
        wantChanged[idx] = std::vector<int>(set.begin(), set.end());
    CHECK(s.changed == wantChanged);
    for (const auto& [id, t] : m.tables) {
        auto it = s.tables.find(id);
        REQUIRE(it != s.tables.end());
        CHECK(it->second.name == t.first);
        for (const auto& [idx, str] : t.second) {
            const std::string* got = it->second.string(idx);
            REQUIRE(got);
            CHECK(*got == str);
        }
    }
}

// Drives a StateWriter with a deterministic random history and returns the expected state after each tick.
std::vector<Model> writeHistory(File& out, std::vector<ChunkRef>* announced, int ticks) {
    const DataTables dt = makeTables();
    const Blake3Digest hash = blake3(std::vector<std::uint8_t>{1, 2, 3});
    StateWriter w(out, hash,
                  {[&](const ChunkRef& r) {
                       if (announced)
                           announced->push_back(r);
                   },
                   {}});
    REQUIRE(w.begin());
    ServerInfo si;
    si.tickInterval = 1.0f; // one keyframe every 30 ticks
    si.mapName = "gm_test";
    w.onServerInfo(si);
    w.onDataTables(dt);

    // A string table with two sequential entries.
    test::BitWriter bw;
    for (const char* s : {"models/a.mdl", "models/b.mdl"}) {
        bw.bit(true);  // sequential index
        bw.bit(true);  // has string
        bw.bit(false); // not a history reference
        bw.string(s);
        bw.bit(false); // no userdata
    }
    StringTable table("modelprecache", 4, false, 0, 0);
    BitReader br(bw.data());
    std::vector<int> changed;
    REQUIRE(table.parseEntries(br, 2, 19, changed));
    w.onStringTableChanged(0, 3, table, changed, true);

    Model model;
    model.tables[3] = {"modelprecache", {{0, "models/a.mdl"}, {1, "models/b.mdl"}}};
    std::vector<Model> snapshots;
    std::mt19937 rng(1234);
    std::uint32_t nextLife = 1;
    std::map<int, int> serials;
    for (int tick = 0; tick < ticks; ++tick) {
        CommandRecord rec;
        rec.cmd = DemoCommand::Packet;
        rec.tick = tick;
        rec.info.viewOrigin = {static_cast<float>(tick), 0, 0};
        w.onPacket(rec);
        model.changed.clear();
        for (int op = 0; op < 4; ++op) {
            if (op == 2) {
                // Real demos often have several packets with the same tick; a keyframe may fall due in between.
                w.onTickEnd(tick);
                w.onPacket(rec);
            }
            const int index = static_cast<int>(rng() % 12);
            auto it = model.ents.find(index);
            const unsigned choice = rng() % 10;
            if (it == model.ents.end() || (!it->second.inPvs && choice < 3)) {
                // Enter: a new life, or a dormant entity coming back into the PVS.
                const bool newLife = it == model.ents.end();
                Model::Ent e;
                if (newLife) {
                    e.life = nextLife++;
                    e.serial = ++serials[index];
                    e.props.resize(4);
                } else {
                    e = it->second;
                }
                e.inPvs = true;
                e.props[0].v = static_cast<std::int64_t>(rng() % 100);
                e.props[3].v = std::string("models/e") + std::to_string(index) + ".mdl";
                w.onEntityEnter(tick, {index, 0, e.serial, e.life}, newLife, e.props);
                model.ents[index] = e;
                for (int p = 0; p < 4; ++p)
                    if (e.props[static_cast<std::size_t>(p)].isSet())
                        model.changed[index].insert(p);
            } else if (!it->second.inPvs) {
                // A dormant entity can still receive delta updates.
                auto& e = it->second;
                e.props[0].v = static_cast<std::int64_t>(rng() % 100);
                const int ch[] = {0};
                w.onEntityUpdate(tick, {index, 0, e.serial, e.life}, ch, e.props);
                model.changed[index].insert(0);
            } else if (choice < 6) {
                auto& e = it->second;
                std::vector<int> ch;
                if (rng() % 2) {
                    e.props[1].v = static_cast<float>(rng() % 1000) / 4.0f;
                    ch.push_back(1);
                }
                e.props[2].v = Vec3{static_cast<float>(tick), static_cast<float>(index), 1.5f};
                ch.push_back(2);
                w.onEntityUpdate(tick, {index, 0, e.serial, e.life}, ch, e.props);
                model.changed[index].insert(ch.begin(), ch.end());
            } else if (choice < 8) {
                w.onEntityLeave(tick, {index, 0, it->second.serial, it->second.life}, false);
                it->second.inPvs = false;
            } else {
                w.onEntityLeave(tick, {index, 0, it->second.serial, it->second.life}, true);
                model.ents.erase(it);
            }
        }
        w.onTickEnd(tick);
        snapshots.push_back(model);
    }
    ParseStats stats;
    stats.packets = static_cast<std::uint64_t>(ticks);
    REQUIRE(w.finish(stats));
    return snapshots;
}

} // namespace

TEST_CASE("state file round trip: random access and sequential playback match the written history") {
    TempFile tmp;
    std::vector<Model> snapshots;
    {
        auto f = File::open(tmp.path, File::Mode::CreateTruncate);
        REQUIRE(f);
        snapshots = writeHistory(*f, nullptr, 200);
    }
    auto reader = StateReader::open(tmp.path, true);
    REQUIRE(reader);
    auto& r = **reader;
    CHECK(r.complete());
    CHECK(r.readyTick() == 199);

    // Sequential playback (cursor path).
    for (int tick = 0; tick < 200; ++tick)
        REQUIRE(r.withStateAt(tick, [&](const WorldState& s) {
            CHECK(s.tick == tick);
            checkState(s, snapshots[static_cast<std::size_t>(tick)]);
        }));
    // Random access (keyframe + deltas), including going backwards.
    std::mt19937 rng(99);
    for (int i = 0; i < 100; ++i) {
        const int tick = static_cast<int>(rng() % 200);
        REQUIRE(r.withStateAt(
            tick, [&](const WorldState& s) { checkState(s, snapshots[static_cast<std::size_t>(tick)]); }));
    }

    auto lives = r.lives();
    REQUIRE(lives);
    CHECK(!(*lives)->empty());
    std::set<std::uint64_t> uids;
    for (const auto& l : **lives)
        uids.insert(l.uid);
    CHECK(uids.size() == (*lives)->size());

    auto cam = r.camera(10, 12);
    REQUIRE(cam);
    REQUIRE(cam->size() == 6); // two packets per tick
    CHECK((*cam)[0].tick == 10);
    CHECK((*cam)[5].tick == 12);
    CHECK((*cam)[5].origin.x == 12.0f);

    auto manifest = r.manifest();
    REQUIRE(manifest);
    CHECK((*manifest)["map"] == "gm_test");
    CHECK((*manifest)["models"].size() == 2);
    auto info = r.info();
    REQUIRE(info);
    CHECK((*info)["lastTick"] == 199);
    CHECK((*info)["stats"]["packets"] == 200);
}

TEST_CASE("state file is usable from chunk announcements while it is being written") {
    TempFile tmp;
    std::vector<ChunkRef> announced;
    std::vector<Model> snapshots;
    std::vector<std::uint8_t> incompleteHeader;
    {
        auto f = File::open(tmp.path, File::Mode::CreateTruncate);
        REQUIRE(f);
        snapshots = writeHistory(*f, &announced, 100);
    }
    // Make the header look like an import in progress: no complete flag, no directory.
    {
        auto f = File::open(tmp.path, File::Mode::ReadWrite);
        REQUIRE(f);
        FileHeader h{};
        REQUIRE(f->readExactAt(0, std::span(reinterpret_cast<std::uint8_t*>(&h), sizeof h)));
        h.flags = 0;
        h.directoryOffset = 0;
        REQUIRE(f->writeAt(0, std::span(reinterpret_cast<const std::uint8_t*>(&h), sizeof h)));
    }
    CHECK_FALSE(StateReader::open(tmp.path, true));
    auto reader = StateReader::open(tmp.path, false);
    REQUIRE(reader);
    auto& r = **reader;
    CHECK_FALSE(r.complete());
    CHECK(r.chunkCount() == 0);
    CHECK(r.readyTick() == -1);

    // Announce chunks one by one, as the importer does, and check every tick that became ready.
    Tick checkedUpTo = -1;
    for (const auto& ref : announced) {
        if (ref.kind == ChunkKind::Directory)
            break;
        REQUIRE(r.addChunk(ref));
        const Tick ready = r.readyTick();
        for (Tick t = checkedUpTo + 1; t <= ready && t < 100; ++t)
            REQUIRE(r.withStateAt(
                t, [&](const WorldState& s) { checkState(s, snapshots[static_cast<std::size_t>(t)]); }));
        checkedUpTo = std::max(checkedUpTo, ready);
    }
    CHECK(checkedUpTo == 99);
}

TEST_CASE("state reader rejects damaged files without crashing") {
    TempFile tmp;
    std::vector<ChunkRef> announced;
    {
        auto f = File::open(tmp.path, File::Mode::CreateTruncate);
        REQUIRE(f);
        writeHistory(*f, &announced, 120);
    }
    auto bytes = readWholeFile(tmp.path, 64u << 20);
    REQUIRE(bytes);
    const std::size_t size = bytes->size();

    SUBCASE("a flipped payload byte fails the checksum") {
        const auto first = std::find_if(announced.begin(), announced.end(), [](const ChunkRef& c) {
            return c.kind == ChunkKind::Deltas && c.tickFrom == -1;
        });
        REQUIRE(first != announced.end());
        {
            auto f = File::open(tmp.path, File::Mode::ReadWrite);
            REQUIRE(f);
            const std::size_t at = first->offset + kChunkHeaderSize + first->storedSize / 2;
            const std::uint8_t b = static_cast<std::uint8_t>((*bytes)[at] ^ 0x5A);
            REQUIRE(f->writeAt(at, std::span(&b, 1)));
        }
        auto reader = StateReader::open(tmp.path, true);
        REQUIRE(reader);
        auto res = (*reader)->withStateAt(first->tickTo - 1, [](const WorldState&) {});
        REQUIRE_FALSE(res);
        CHECK(res.error().code == "statedb.corrupt");
        // Later segments do not depend on the damaged chunk.
        CHECK((*reader)->withStateAt(first->tickTo + 1, [](const WorldState&) {}));
    }
    SUBCASE("a truncated file is rejected") {
        {
            auto f = File::open(tmp.path, File::Mode::CreateTruncate);
            REQUIRE(f);
            REQUIRE(f->writeAt(0, std::span(bytes->data(), size / 2)));
        }
        CHECK_FALSE(StateReader::open(tmp.path, true));
    }
    SUBCASE("a garbage header is rejected") {
        {
            auto f = File::open(tmp.path, File::Mode::ReadWrite);
            REQUIRE(f);
            const std::uint8_t junk[8] = {'N', 'O', 'P', 'E', 0, 0, 0, 0};
            REQUIRE(f->writeAt(0, junk));
        }
        auto reader = StateReader::open(tmp.path, true);
        REQUIRE_FALSE(reader);
        CHECK(reader.error().code == "statedb.bad_magic");
    }
    SUBCASE("every single-byte corruption of the chunk area is detected or harmless") {
        std::mt19937 rng(7);
        for (int i = 0; i < 60; ++i) {
            auto copy = *bytes;
            const std::size_t at = kFileHeaderSize + rng() % (size - kFileHeaderSize);
            copy[at] ^= static_cast<std::uint8_t>(1 + rng() % 255);
            {
                auto f = File::open(tmp.path, File::Mode::CreateTruncate);
                REQUIRE(f);
                REQUIRE(f->writeAt(0, copy));
            }
            auto reader = StateReader::open(tmp.path, true);
            if (!reader)
                continue;
            for (Tick t : {0, 45, 119})
                (void)(*reader)->withStateAt(t, [](const WorldState&) {});
            (void)(*reader)->lives();
            (void)(*reader)->events(0, 200, 1000);
            (void)(*reader)->camera(0, 200);
            (void)(*reader)->manifest();
        }
    }
}

TEST_CASE("index chunk round trip") {
    TempFile tmp;
    {
        auto f = File::open(tmp.path, File::Mode::CreateTruncate);
        REQUIRE(f);
        StateWriter w(*f, blake3(std::vector<std::uint8_t>{9}), {});
        REQUIRE(w.begin());
        const std::vector<IndexEntry> entries = {{0, 1072}, {0, 5000}, {3, 9000}, {10, 12000}};
        REQUIRE(w.writeIndex(entries, 10));
        REQUIRE(w.finish(ParseStats{}));
    }
    auto reader = StateReader::open(tmp.path, true);
    REQUIRE(reader);
    CHECK((*reader)->lastTick() == 10);
    auto index = (*reader)->index();
    REQUIRE(index);
    REQUIRE((*index)->size() == 4);
    CHECK((**index)[2].tick == 3);
    CHECK((**index)[2].offset == 9000);
    CHECK((**index)[3].offset == 12000);
}
