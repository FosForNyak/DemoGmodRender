#pragma once

// Test helper: LSB-first bit writer that mirrors the Source engine bf_write layout.

#include <bit>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gmdr::test {

class BitWriter {
public:
    void ubit(std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i)
            bit(((v >> i) & 1) != 0);
    }
    void sbit(std::int64_t v, int n) { ubit(static_cast<std::uint64_t>(v), n); }
    void bit(bool b) {
        if (bits_ % 8 == 0)
            bytes_.push_back(0);
        if (b)
            bytes_.back() |= static_cast<std::uint8_t>(1u << (bits_ % 8));
        ++bits_;
    }
    void byte(std::uint8_t b) { ubit(b, 8); }
    void word(std::uint16_t w) { ubit(w, 16); }
    void i32(std::int32_t v) { ubit(static_cast<std::uint32_t>(v), 32); }
    void f32(float f) { ubit(std::bit_cast<std::uint32_t>(f), 32); }
    void f64(double d) { ubit(std::bit_cast<std::uint64_t>(d), 64); }
    void varint32(std::uint32_t v) {
        do {
            std::uint8_t b = v & 0x7F;
            v >>= 7;
            if (v)
                b |= 0x80;
            byte(b);
        } while (v);
    }
    // SDK 2013 bf_write::WriteUBitVar
    void ubitVar(std::uint32_t v) {
        if (v < 16) {
            ubit(v << 2, 6);
        } else if (v < 256) {
            ubit((v << 2) | 1, 10);
        } else if (v < 4096) {
            ubit((v << 2) | 2, 14);
        } else {
            ubit(3, 2);
            ubit(v, 32);
        }
    }
    void string(std::string_view s) {
        for (char c : s)
            byte(static_cast<std::uint8_t>(c));
        byte(0);
    }
    void bytes(const std::vector<std::uint8_t>& b) {
        for (auto x : b)
            byte(x);
    }
    void append(const BitWriter& other) {
        for (std::size_t i = 0; i < other.bits_; ++i)
            bit((other.bytes_[i / 8] >> (i % 8)) & 1);
    }

    std::size_t bits() const { return bits_; }
    const std::vector<std::uint8_t>& data() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t bits_ = 0;
};

} // namespace gmdr::test
