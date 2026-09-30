#include "bit_writer.h"
#include "core/bit_reader.h"
#include "core/byte_reader.h"

#include <doctest/doctest.h>

using gmdr::BitReader;
using gmdr::test::BitWriter;

TEST_CASE("BitReader reads LSB-first fields of any width") {
    BitWriter w;
    w.ubit(5, 3);
    w.ubit(0x1FFFF, 17);
    w.sbit(-3, 5);
    w.ubit(0xDEADBEEF, 32);
    w.bit(true);
    BitReader r(w.data());
    CHECK(r.ubit(3) == 5);
    CHECK(r.ubit(17) == 0x1FFFF);
    CHECK(r.sbit(5) == -3);
    CHECK(r.ubit(32) == 0xDEADBEEFu);
    CHECK(r.bit());
    CHECK_FALSE(r.overflowed());
}

TEST_CASE("BitReader never reads past the end and flags overflow") {
    const std::uint8_t data[2] = {0xFF, 0x01};
    BitReader r(data);
    CHECK(r.ubit(9) == 0x1FF);
    CHECK(r.ubit(8) == 0); // only 7 bits left
    CHECK(r.overflowed());
    CHECK(r.remaining() == 0);
    CHECK(r.ubit(1) == 0);
}

TEST_CASE("BitReader window respects its bounds") {
    const std::uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    BitReader r(data, 8, 16);
    CHECK(r.ubit(8) == 0xBB);
    CHECK(r.remaining() == 0);
    r.ubit(1);
    CHECK(r.overflowed());
    BitReader bad(data, 0, 999);
    CHECK(bad.overflowed());
}

TEST_CASE("ubitVar matches the SDK 2013 encoding for every width") {
    for (std::uint32_t v : {0u, 1u, 15u, 16u, 200u, 255u, 256u, 4095u, 4096u, 0x7FFFFFFFu}) {
        BitWriter w;
        w.ubitVar(v);
        w.ubit(0x5, 3); // trailer to check the cursor
        BitReader r(w.data());
        CHECK(r.ubitVar() == v);
        CHECK(r.ubit(3) == 0x5);
    }
}

TEST_CASE("varint32 and zigzag") {
    BitWriter w;
    w.bit(true); // unaligned on purpose
    w.varint32(0);
    w.varint32(300);
    w.varint32(0xFFFFFFFFu);
    BitReader r(w.data());
    r.bit();
    CHECK(r.varint32() == 0);
    CHECK(r.varint32() == 300);
    CHECK(r.varint32() == 0xFFFFFFFFu);
    CHECK(BitReader::zigzag32(0) == 0);
    CHECK(BitReader::zigzag32(1) == -1);
    CHECK(BitReader::zigzag32(2) == 1);
    CHECK(BitReader::zigzag32(3) == -2);
}

TEST_CASE("strings stop at the terminator and are length-limited") {
    BitWriter w;
    w.bit(false);
    w.string("gm_construct");
    w.string("0123456789");
    w.ubit(0x3, 2);
    BitReader r(w.data());
    r.bit();
    CHECK(r.string(260) == "gm_construct");
    bool truncated = false;
    CHECK(r.string(4, &truncated) == "0123");
    CHECK(truncated);
    CHECK(r.ubit(2) == 0x3); // still aligned after the truncated string
}

TEST_CASE("floats and doubles") {
    BitWriter w;
    w.bit(true);
    w.f32(-1.5f);
    w.f64(1807.2728);
    BitReader r(w.data());
    r.bit();
    CHECK(r.float32() == -1.5f);
    CHECK(r.float64() == doctest::Approx(1807.2728));
}

TEST_CASE("take splits a sub-reader and advances") {
    BitWriter w;
    w.ubit(0xABC, 12);
    w.ubit(0x7, 3);
    BitReader r(w.data());
    BitReader sub = r.take(12);
    CHECK(sub.ubit(12) == 0xABC);
    CHECK(sub.remaining() == 0);
    CHECK(r.ubit(3) == 0x7);
    BitReader over = r.take(100);
    CHECK(over.overflowed());
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader is sticky on failure") {
    const std::uint8_t data[6] = {1, 0, 0, 0, 'h', 0};
    gmdr::ByteReader r(data);
    std::int32_t v = 0;
    CHECK(r.i32(v));
    CHECK(v == 1);
    std::string s;
    CHECK(r.fixedString(2, s));
    CHECK(s == "h");
    CHECK_FALSE(r.i32(v));
    CHECK_FALSE(r.ok());
    std::uint8_t b = 0;
    CHECK_FALSE(r.u8(b));
}
