// "Bomb" inputs: every limit in core/limits.h rejects oversized or self-referencing data with a clean error,
// before memory is reserved for the claimed size.
#include "bit_writer.h"
#include "core/compress.h"
#include "core/file.h"
#include "core/limits.h"
#include "demo/commands.h"
#include "demo/game_events.h"
#include "demo/lzss.h"
#include "demo/protocol.h"
#include "demo/send_tables.h"
#include "demo/statedb/format.h"
#include "demo/statedb/reader.h"
#include "demo/string_tables.h"

#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <random>

using namespace gmdr;
using namespace gmdr::demo;
using gmdr::test::BitWriter;

namespace {

void table(BitWriter& w, const std::string& name, const std::vector<std::string>& childTables) {
    w.bit(true);
    w.bit(false);
    w.string(name);
    w.ubit(childTables.size(), 10);
    for (const auto& child : childTables) {
        w.ubit(6, 5); // DataTable
        w.string("sub");
        w.ubit(0, 16);
        w.string(child);
    }
}

std::vector<std::uint8_t> tablesWithClass(BitWriter& w, const std::string& rootTable) {
    w.bit(false);
    w.ubit(1, 16);
    w.ubit(0, 16);
    w.string("CRoot");
    w.string(rootTable);
    return w.data();
}

} // namespace

TEST_CASE("LZSS: declared sizes beyond the limit or beyond what the input can hold are rejected") {
    std::vector<std::uint8_t> in = {'L', 'Z', 'S', 'S', 0, 0, 0, 0, 0x00, 'a'};
    const std::uint32_t huge = static_cast<std::uint32_t>(limits::kMaxDecompressedBytes) + 1;
    std::memcpy(in.data() + 4, &huge, 4);
    auto r = lzssDecompress(in);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == "demo.lzss_too_large");

    const std::uint32_t million = 1u << 20; // 1 MB from a 2-byte stream
    std::memcpy(in.data() + 4, &million, 4);
    r = lzssDecompress(in);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == "demo.lzss_corrupt");
}

TEST_CASE("zstd: the frame's own size must match before anything is allocated") {
    std::vector<std::uint8_t> raw(1000, 7);
    auto packed = zstdCompress(raw);
    REQUIRE(packed);
    CHECK(zstdDecompress(*packed, 1000));
    auto r = zstdDecompress(*packed, 200u << 20); // claims 200 MB
    REQUIRE_FALSE(r);
    CHECK(r.error().code == "core.zstd_size_mismatch");
    r = zstdDecompress(*packed, limits::kMaxChunkBytes + 1);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == "core.zstd_too_large");
    CHECK_FALSE(zstdDecompress(std::vector<std::uint8_t>{1, 2, 3}, 3));
}

TEST_CASE("SendTables: too many tables and classes are rejected") {
    BitWriter w;
    for (std::size_t i = 0; i <= limits::kMaxSendTables; ++i)
        table(w, "T" + std::to_string(i), {});
    auto dt = parseDataTables(tablesWithClass(w, "T0"), knownProtocolVariants()[0]);
    REQUIRE_FALSE(dt);
    CHECK(dt.error().code == "demo.datatables_limit");

    BitWriter c;
    table(c, "T", {});
    c.bit(false);
    c.ubit(limits::kMaxServerClasses + 1, 16);
    auto dc = parseDataTables(c.data(), knownProtocolVariants()[0]);
    REQUIRE_FALSE(dc);
    CHECK(dc.error().code == "demo.datatables_limit");
}

TEST_CASE("SendTables: deep nesting and reference cycles fail cleanly when flattened") {
    SUBCASE("a chain deeper than the limit") {
        BitWriter w;
        const int depth = static_cast<int>(limits::kMaxTableDepth) + 8;
        for (int i = 0; i < depth; ++i)
            table(w, "T" + std::to_string(i),
                  i + 1 < depth ? std::vector<std::string>{"T" + std::to_string(i + 1)}
                                : std::vector<std::string>{});
        auto dt = parseDataTables(tablesWithClass(w, "T0"), knownProtocolVariants()[0]);
        REQUIRE(dt);
        CHECK_FALSE(flattenClass(**dt, "T0"));
    }
    SUBCASE("a cycle") {
        BitWriter w;
        table(w, "A", {"B"});
        table(w, "B", {"A"});
        auto dt = parseDataTables(tablesWithClass(w, "A"), knownProtocolVariants()[0]);
        REQUIRE(dt);
        CHECK_FALSE(flattenClass(**dt, "A"));
    }
    SUBCASE("a fan-out that would explode combinatorially") {
        // Each table references the next one many times: 10 levels x 30 references = 30^10 paths.
        BitWriter w;
        for (int i = 0; i < 10; ++i)
            table(w, "F" + std::to_string(i),
                  i < 9 ? std::vector<std::string>(30, "F" + std::to_string(i + 1))
                        : std::vector<std::string>{});
        auto dt = parseDataTables(tablesWithClass(w, "F0"), knownProtocolVariants()[0]);
        REQUIRE(dt);
        CHECK_FALSE(flattenClass(**dt, "F0"));
    }
}

TEST_CASE("string tables and game events: counts beyond the limits are rejected") {
    StringTable t("big", 16, false, 0, 0);
    BitWriter w;
    w.bit(true);
    BitReader r(w.data());
    std::vector<int> changed;
    auto res = t.parseEntries(r, static_cast<int>(limits::kMaxStringTableEntries) + 1, 19, changed);
    REQUIRE_FALSE(res);
    CHECK(res.error().code == "demo.stringtable_count");

    GameEventSchema schema;
    BitReader e(w.data());
    CHECK_FALSE(schema.parseList(e, static_cast<int>(limits::kMaxGameEventDescriptors) + 1));
}

TEST_CASE("command walk: a packet longer than the limit is rejected without reading it") {
    std::vector<std::uint8_t> d(1072, 0);
    std::memcpy(d.data(), "HL2DEMO", 7);
    const std::int32_t demoProtocol = 3, netProtocol = 24;
    std::memcpy(d.data() + 8, &demoProtocol, 4);
    std::memcpy(d.data() + 12, &netProtocol, 4);
    for (const std::int32_t length : {std::int32_t{0x7FFFFFFF}, std::int32_t{-5}}) {
        auto demo = d;
        demo.push_back(2); // dem_packet
        demo.insert(demo.end(), {0, 0, 0, 0});
        demo.insert(demo.end(), 76 + 8, 0);
        const auto* p = reinterpret_cast<const std::uint8_t*>(&length);
        demo.insert(demo.end(), p, p + 4);
        CommandReader reader(demo);
        CommandRecord rec;
        CHECK_FALSE(reader.next(rec));
    }
}

TEST_CASE("state file: chunk sizes beyond the limit or the file are rejected") {
    using namespace gmdr::demo::statedb;
    const auto path = std::filesystem::temp_directory_path() /
                      ("gmdr_limits_" + std::to_string(std::random_device{}()) + ".gmstate");
    auto write = [&](std::uint32_t rawSize, std::uint32_t storedSize) {
        FileHeader h{};
        std::memcpy(h.magic, kFileMagic, sizeof h.magic);
        h.formatVersion = kFormatVersion;
        h.parserVersion = kParserVersion;
        h.flags = kComplete;
        h.directoryOffset = kFileHeaderSize;
        ChunkHeader c{};
        c.magic = kChunkMagic;
        c.kind = static_cast<std::uint16_t>(ChunkKind::Directory);
        c.rawSize = rawSize;
        c.storedSize = storedSize;
        auto f = File::open(path, File::Mode::CreateTruncate);
        REQUIRE(f);
        REQUIRE(f->writeAt(0, std::span(reinterpret_cast<const std::uint8_t*>(&h), sizeof h)));
        REQUIRE(f->writeAt(sizeof h, std::span(reinterpret_cast<const std::uint8_t*>(&c), sizeof c)));
        const std::uint8_t tail[16] = {};
        REQUIRE(f->writeAt(sizeof h + sizeof c, tail));
    };
    write(static_cast<std::uint32_t>(limits::kMaxChunkBytes) + 1, 16); // decompresses to more than the limit
    auto r = StateReader::open(path, true);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == "statedb.corrupt");
    write(64, 200u << 20); // payload claims 200 MB in a 120-byte file
    r = StateReader::open(path, true);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == "statedb.corrupt");
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("fuzz regression: an excluded prop typed Array has no element (CI fuzz_tables, UBSan)") {
    // crash-801eafdd78f025bd5cd95e56a2950ec9f8ba4802 without the selector byte: it indexed props[-1].
    const std::vector<std::uint8_t> input = {0x01, 0x08, 0x00, 0xf4, 0x3b, 0x00, 0xfc, 0x00, 0x00, 0x0a,
                                             0x00, 0xfc, 0x2c, 0x00, 0x00, 0x00, 0x17, 0x30, 0xe0, 0x2c};
    auto dt = parseDataTables(input, knownProtocolVariants()[0]);
    if (dt)
        for (const auto& c : (*dt)->classes)
            (void)flattenClass(**dt, c.tableName);
    CHECK(true); // reaching here without UB is the test (run under UBSan in CI)

    // The same shape, built explicitly: an Array-typed prop with the Exclude flag as the first prop.
    BitWriter w;
    w.bit(true);
    w.bit(false);
    w.string("DT_X");
    w.ubit(1, 10);
    w.ubit(5, 5); // Array
    w.string("m_excluded");
    w.ubit(prop_flags::Exclude, 16);
    w.string("DT_Other");
    w.bit(false);
    w.ubit(1, 16);
    w.ubit(0, 16);
    w.string("CX");
    w.string("DT_X");
    auto parsed = parseDataTables(w.data(), knownProtocolVariants()[0]);
    REQUIRE(parsed);
    auto flat = flattenClass(**parsed, "DT_X");
    REQUIRE(flat);
    CHECK(flat->empty());
}
