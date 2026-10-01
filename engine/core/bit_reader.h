#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace gmdr {

// Little-endian, LSB-first bit reader over an immutable byte buffer (Source engine bf_read semantics).
//
// Reading past the end never touches memory outside the buffer: it returns zeros, moves the cursor to the
// end and sets a sticky overflow flag. Callers check `overflowed()` at message boundaries, which keeps the
// hot decoding loops free of per-read error branches.
class BitReader {
public:
    BitReader() = default;
    explicit BitReader(std::span<const std::uint8_t> bytes);
    // A window [bitBegin, bitEnd) over `bytes`; bitEnd is clamped to the buffer size.
    BitReader(std::span<const std::uint8_t> bytes, std::size_t bitBegin, std::size_t bitEnd);

    std::uint32_t ubit(int n); // n in [0, 32]
    std::int32_t sbit(int n);  // n in [1, 32], two's complement sign extension
    bool bit() { return ubit(1) != 0; }
    std::uint64_t ubit64(int n); // n in [0, 64]

    std::uint32_t varint32();  // protobuf-style, at most 5 bytes
    std::uint64_t varint64();  // at most 10 bytes
    static std::int32_t zigzag32(std::uint32_t v) { return static_cast<std::int32_t>((v >> 1) ^ (~(v & 1) + 1)); }
    static std::int64_t zigzag64(std::uint64_t v) { return static_cast<std::int64_t>((v >> 1) ^ (~(v & 1) + 1)); }

    // Source SDK 2013 bf_read::ReadUBitVar: the low 2 bits select the width (4, 8, 12 or 32 bits).
    std::uint32_t ubitVar();

    float float32();
    double float64();

    // Null-terminated string; stops at maxLen bytes and sets `truncated` (not an overflow).
    std::string string(std::size_t maxLen, bool* truncated = nullptr);
    // Exactly n bytes (not bit-aligned in general).
    void bytes(std::uint8_t* out, std::size_t n);

    void skip(std::size_t nbits);
    void seek(std::size_t bitPos);
    // Splits off a sub-reader for the next nbits and advances past them; overflows if not enough bits.
    BitReader take(std::size_t nbits);

    std::size_t position() const { return pos_; }
    std::size_t begin() const { return begin_; }
    std::size_t end() const { return end_; }
    std::size_t remaining() const { return end_ - pos_; }
    std::size_t consumed() const { return pos_ - begin_; }
    bool overflowed() const { return overflow_; }
    std::span<const std::uint8_t> buffer() const { return data_; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t begin_ = 0;
    std::size_t pos_ = 0;
    std::size_t end_ = 0;
    bool overflow_ = false;
};

} // namespace gmdr
