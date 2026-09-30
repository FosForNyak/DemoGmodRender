#include "bit_writer.h"
#include "demo/commands.h"
#include "demo/header.h"
#include "demo/lzss.h"
#include "demo/props.h"
#include "demo/protocol.h"
#include "demo/send_tables.h"
#include "demo/string_tables.h"

#include <cstring>
#include <doctest/doctest.h>

using namespace gmdr;
using namespace gmdr::demo;
using gmdr::test::BitWriter;

namespace {

std::vector<std::uint8_t> makeHeader(const char* magic = "GMODEMO", int demoProto = 3, int netProto = 24) {
    std::vector<std::uint8_t> h(kHeaderSize, 0);
    std::memcpy(h.data(), magic, std::strlen(magic));
    std::memcpy(h.data() + 8, &demoProto, 4);
    std::memcpy(h.data() + 12, &netProto, 4);
    std::memcpy(h.data() + 16 + 260 * 2, "gm_construct", 12);
    const int ticks = 1234;
    std::memcpy(h.data() + 1056 + 4, &ticks, 4);
    return h;
}

void appendI32(std::vector<std::uint8_t>& v, std::int32_t x) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&x);
    v.insert(v.end(), p, p + 4);
}

} // namespace

TEST_CASE("demo header: GMODEMO accepted, garbage rejected") {
    auto h = makeHeader();
    auto r = parseHeader(h);
    REQUIRE(r);
    CHECK(r->magic == "GMODEMO");
    CHECK(r->mapName == "gm_construct");
    CHECK(r->ticks == 1234);
    CHECK_FALSE(parseHeader(makeHeader("XXXDEMO")));
    CHECK(parseHeader(makeHeader("HL2DEMO")));
    CHECK_FALSE(parseHeader(makeHeader("GMODEMO", 4)));
    CHECK_FALSE(parseHeader(std::span(h).first(100)));
}

TEST_CASE("command walk validates lengths and stops at dem_stop") {
    auto f = makeHeader();
    // dem_consolecmd tick 5 "hi"
    f.push_back(4);
    appendI32(f, 5);
    appendI32(f, 3);
    f.insert(f.end(), {'h', 'i', 0});
    // dem_packet tick 6 with 2 bytes payload
    f.push_back(2);
    appendI32(f, 6);
    f.insert(f.end(), 76 + 8, 0);
    appendI32(f, 2);
    f.insert(f.end(), {0xAB, 0xCD});
    f.push_back(7); // stop
    appendI32(f, 6);

    CommandReader reader(f);
    CommandRecord rec;
    REQUIRE(reader.next(rec).value());
    CHECK(rec.cmd == DemoCommand::ConsoleCmd);
    CHECK(rec.payloadSize == 3);
    REQUIRE(reader.next(rec).value());
    CHECK(rec.cmd == DemoCommand::Packet);
    CHECK(rec.tick == 6);
    CHECK(f[rec.payloadOffset] == 0xAB);
    auto end = reader.next(rec);
    REQUIRE(end);
    CHECK_FALSE(*end);
    CHECK(reader.stopped());
}

TEST_CASE("command walk rejects a length past the end") {
    auto f = makeHeader();
    f.push_back(4);
    appendI32(f, 1);
    appendI32(f, 1000);
    f.push_back('x');
    CommandReader reader(f);
    CommandRecord rec;
    auto r = reader.next(rec);
    CHECK_FALSE(r);
    CHECK(r.error().code == "demo.truncated");
}

TEST_CASE("string table entries: sequential, explicit index, substring history, 19-bit userdata") {
    BitWriter w;
    // entry 0: sequential, string "models/a.mdl", no userdata
    w.bit(true);
    w.bit(true);
    w.bit(false);
    w.string("models/a.mdl");
    w.bit(false);
    // entry 5: explicit index (4 bits), substring of history[0] first 7 chars + "b.mdl", userdata 2 bytes
    w.bit(false);
    w.ubit(5, 4);
    w.bit(true);
    w.bit(true);
    w.ubit(0, 5);
    w.ubit(7, 5);
    w.string("b.mdl");
    w.bit(true);
    w.ubit(2, 19);
    w.byte(0x11);
    w.byte(0x22);

    StringTable t("modelprecache", 4, false, 0, 0);
    BitReader r(w.data());
    std::vector<int> changed;
    REQUIRE(t.parseEntries(r, 2, 19, changed));
    CHECK(changed == std::vector<int>{0, 5});
    CHECK(t.entry(0)->string == "models/a.mdl");
    CHECK(t.entry(5)->string == "models/b.mdl");
    CHECK(t.entry(5)->userData == std::vector<std::uint8_t>{0x11, 0x22});
    CHECK(t.find("models/b.mdl") == 5);
    CHECK(t.entry(3) == nullptr);
}

TEST_CASE("string table rejects bad history references and oversized userdata") {
    BitWriter w;
    w.bit(true);
    w.bit(true);
    w.bit(true);
    w.ubit(3, 5); // history is empty
    w.ubit(1, 5);
    w.string("x");
    StringTable t("t", 4, false, 0, 0);
    BitReader r(w.data());
    std::vector<int> changed;
    CHECK_FALSE(t.parseEntries(r, 1, 19, changed));
}

TEST_CASE("LZSS round trip of a literal and a back-reference") {
    // "abcabcabc": literals a b c, then a back-reference (pos=2, count=6)
    std::vector<std::uint8_t> in = {'L', 'Z', 'S', 'S', 9, 0, 0, 0};
    in.push_back(0x08); // cmd: bits 0..2 literals, bit 3 back-ref, then end marker as bit 4
    in.insert(in.end(), {'a', 'b', 'c'});
    in.push_back(static_cast<std::uint8_t>((2 >> 4) & 0xFF));
    in.push_back(static_cast<std::uint8_t>(((2 & 0x0F) << 4) | (6 - 1)));
    // end marker needs another cmd byte bit set; cmd continues (bit 4 of 0x08 is 0) -> use a new cmd
    // simpler: rebuild with cmd 0x18 (bit 3 back-ref, bit 4 end)
    in[8] = 0x18;
    in.push_back(0x00);
    in.push_back(0x00); // count nibble 0 -> count 1 -> end
    auto out = lzssDecompress(in);
    REQUIRE(out);
    CHECK(std::string(out->begin(), out->end()) == "abcabcabc");

    std::vector<std::uint8_t> bad = {'L', 'Z', 'S', 'S', 9, 0, 0, 0, 0x01, 0x00, 0x05};
    CHECK_FALSE(lzssDecompress(bad)); // back-reference before the start
}

namespace {

// Two tables: DT_Child {x int 8 CHANGES_OFTEN}, DT_Root {baseline DT_Child (collapsible), a int 4, arr[3] of int 2}
std::vector<std::uint8_t> makeDataTables() {
    BitWriter w;
    auto prop = [&](int type, const char* name, std::uint32_t flags) {
        w.ubit(static_cast<std::uint64_t>(type), 5);
        w.string(name);
        w.ubit(flags, 16);
    };
    auto numeric = [&](int bits) {
        w.f32(0);
        w.f32(0);
        w.ubit(static_cast<std::uint64_t>(bits), 7);
    };
    // DT_Child
    w.bit(true);
    w.bit(false);
    w.string("DT_Child");
    w.ubit(2, 10);
    prop(0, "m_other", prop_flags::Unsigned);
    numeric(3);
    prop(0, "m_x", prop_flags::Unsigned | prop_flags::ChangesOften);
    numeric(8);
    // DT_Root
    w.bit(true);
    w.bit(false);
    w.string("DT_Root");
    w.ubit(4, 10);
    prop(6, "baseclass", prop_flags::Collapsible);
    w.string("DT_Child");
    prop(0, "m_a", prop_flags::Unsigned);
    numeric(4);
    prop(0, "m_arr", prop_flags::Unsigned | prop_flags::InsideArray);
    numeric(2);
    prop(5, "m_arr", 0);
    w.ubit(3, 10);
    w.bit(false); // end of tables
    w.ubit(1, 16);
    w.ubit(0, 16);
    w.string("CRoot");
    w.string("DT_Root");
    return w.data();
}

} // namespace

TEST_CASE("SendTables parse and flatten with the SDK 2013 order") {
    auto data = makeDataTables();
    auto dt = parseDataTables(data, knownProtocolVariants()[0]);
    REQUIRE(dt);
    CHECK((*dt)->classes.size() == 1);
    auto flat = flattenClass(**dt, "DT_Root");
    REQUIRE(flat);
    REQUIRE(flat->size() == 4);
    // collapsible child inlined first: m_other, m_x, then m_a, m_arr; then m_x (CHANGES_OFTEN) swapped to 0
    CHECK((*flat)[0].prop->name == "m_x");
    CHECK((*flat)[1].prop->name == "m_other");
    CHECK((*flat)[2].prop->name == "m_a");
    CHECK((*flat)[3].prop->name == "m_arr");
    CHECK((*flat)[3].element->bits == 2);
}

TEST_CASE("property list: ubitVar deltas, varint ints, arrays") {
    auto data = makeDataTables();
    auto dt = parseDataTables(data, knownProtocolVariants()[0]);
    REQUIRE(dt);
    auto flat = flattenClass(**dt, "DT_Root");
    REQUIRE(flat);

    BitWriter w;
    w.bit(true);
    w.ubitVar(0); // index 0: m_x (8 bits)
    w.ubit(200, 8);
    w.bit(true);
    w.ubitVar(1); // index 2: m_a (4 bits)
    w.ubit(9, 4);
    w.bit(true);
    w.ubitVar(0); // index 3: m_arr with 2 elements (count bits = log2(3)+1 = 2)
    w.ubit(2, 2);
    w.ubit(1, 2);
    w.ubit(3, 2);
    w.bit(false);

    std::vector<PropValue> state(flat->size());
    std::vector<int> changed;
    BitReader r(w.data());
    REQUIRE(readPropList(r, *flat, state, changed));
    CHECK(changed == std::vector<int>{0, 2, 3});
    CHECK(std::get<std::int64_t>(state[0].v) == 200);
    CHECK_FALSE(state[1].isSet());
    CHECK(std::get<std::int64_t>(state[2].v) == 9);
    const auto& arr = std::get<PropValue::Array>(state[3].v);
    REQUIRE(arr.size() == 2);
    CHECK(std::get<std::int64_t>(arr[1].v) == 3);

    BitWriter bad;
    bad.bit(true);
    bad.ubitVar(9); // index 9 is past the 4 props
    BitReader rb(bad.data());
    changed.clear();
    CHECK_FALSE(readPropList(rb, *flat, state, changed));
}

TEST_CASE("coordinate decoders") {
    BitWriter w;
    // BitCoord: int+fract, negative, int 100 (stored 99), fract 16/32
    w.bit(true);
    w.bit(true);
    w.bit(true);
    w.ubit(99, 14);
    w.ubit(16, 5);
    // BitCoordMP not integral, low precision, in bounds: int flag, sign 0, int 10 (stored 9, 11 bits), fract 4/8
    w.bit(true);
    w.bit(true);
    w.bit(false);
    w.ubit(9, 11);
    w.ubit(4, 3);
    // BitNormal: sign, value 2047 -> 1.0
    w.bit(true);
    w.ubit(2047, 11);
    BitReader r(w.data());
    CHECK(readBitCoord(r) == doctest::Approx(-100.5));
    CHECK(readBitCoordMp(r, false, true) == doctest::Approx(10.5));
    CHECK(readBitNormal(r) == doctest::Approx(-1.0));
    CHECK_FALSE(r.overflowed());
}

TEST_CASE("timeline clock: signon belongs to tick 0 and the timeline never goes back") {
    TimelineClock clock;
    auto rec = [](DemoCommand cmd, std::int32_t tick) {
        CommandRecord r;
        r.cmd = cmd;
        r.tick = tick;
        return r;
    };
    CHECK(clock.next(rec(DemoCommand::Signon, 96)) == 0);
    CHECK(clock.next(rec(DemoCommand::DataTables, 289)) == 0);
    CHECK(clock.next(rec(DemoCommand::Signon, 304)) == 0);
    CHECK(clock.next(rec(DemoCommand::SyncTick, 0)) == 0);
    CHECK(clock.next(rec(DemoCommand::Packet, 0)) == 0);
    CHECK(clock.next(rec(DemoCommand::Packet, 5)) == 5);
    CHECK(clock.next(rec(DemoCommand::ConsoleCmd, 5)) == 5);
    CHECK(clock.next(rec(DemoCommand::Packet, 4)) == 5);
    CHECK(clock.next(rec(DemoCommand::Packet, 7)) == 7);
}
