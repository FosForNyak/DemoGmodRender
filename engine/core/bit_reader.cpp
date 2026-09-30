#include "core/bit_reader.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace gmdr {

static_assert(std::endian::native == std::endian::little, "BitReader assumes a little-endian host");

BitReader::BitReader(std::span<const std::uint8_t> bytes) : data_(bytes), begin_(0), pos_(0), end_(bytes.size() * 8) {}

BitReader::BitReader(std::span<const std::uint8_t> bytes, std::size_t bitBegin, std::size_t bitEnd) : data_(bytes) {
    const std::size_t total = bytes.size() * 8;
    end_ = std::min(bitEnd, total);
    begin_ = std::min(bitBegin, end_);
    pos_ = begin_;
    if (bitEnd > total || bitBegin > bitEnd)
        overflow_ = true;
}

std::uint32_t BitReader::ubit(int n) {
    if (n <= 0)
        return 0;
    if (n > 32 || static_cast<std::size_t>(n) > end_ - pos_) {
        overflow_ = true;
        pos_ = end_;
        return 0;
    }
    const std::size_t byte = pos_ >> 3;
    const int shift = static_cast<int>(pos_ & 7);
    std::uint64_t window = 0;
    const std::size_t take = std::min<std::size_t>(8, data_.size() - byte);
    std::memcpy(&window, data_.data() + byte, take);
    window >>= shift;
    pos_ += static_cast<std::size_t>(n);
    const std::uint64_t mask = (n == 32) ? 0xFFFFFFFFull : ((1ull << n) - 1);
    return static_cast<std::uint32_t>(window & mask);
}

std::int32_t BitReader::sbit(int n) {
    if (n <= 0)
        return 0;
    const std::uint32_t v = ubit(n);
    if (n >= 32)
        return static_cast<std::int32_t>(v);
    const std::uint32_t signBit = 1u << (n - 1);
    return static_cast<std::int32_t>((v ^ signBit) - signBit);
}

std::uint64_t BitReader::ubit64(int n) {
    if (n <= 32)
        return ubit(n);
    const std::uint64_t lo = ubit(32);
    const std::uint64_t hi = ubit(n - 32);
    return lo | (hi << 32);
}

std::uint32_t BitReader::varint32() {
    std::uint32_t result = 0;
    for (int i = 0; i < 5; ++i) {
        const std::uint32_t b = ubit(8);
        result |= (b & 0x7F) << (7 * i);
        if (!(b & 0x80) || overflow_)
            break;
    }
    return result;
}

std::uint64_t BitReader::varint64() {
    std::uint64_t result = 0;
    for (int i = 0; i < 10; ++i) {
        const std::uint64_t b = ubit(8);
        result |= (b & 0x7F) << (7 * i);
        if (!(b & 0x80) || overflow_)
            break;
    }
    return result;
}

std::uint32_t BitReader::ubitVar() {
    const std::uint32_t six = ubit(6);
    if (overflow_)
        return 0;
    const std::uint32_t encoding = six & 3;
    if (encoding == 0)
        return six >> 2;
    // Re-read: the 2 encoding bits are followed by the whole value.
    pos_ -= 4;
    static constexpr int kWidths[4] = {0, 8, 12, 32};
    return ubit(kWidths[encoding]);
}

float BitReader::float32() {
    return std::bit_cast<float>(ubit(32));
}

double BitReader::float64() {
    return std::bit_cast<double>(ubit64(64));
}

std::string BitReader::string(std::size_t maxLen, bool* truncated) {
    std::string out;
    if (truncated)
        *truncated = false;
    while (true) {
        if (pos_ + 8 > end_) {
            overflow_ = true;
            pos_ = end_;
            break;
        }
        const char c = static_cast<char>(ubit(8));
        if (c == '\0')
            break;
        if (out.size() >= maxLen) {
            if (truncated)
                *truncated = true;
            continue; // keep consuming until the terminator so the stream stays aligned
        }
        out.push_back(c);
    }
    return out;
}

void BitReader::bytes(std::uint8_t* out, std::size_t n) {
    if (n * 8 > end_ - pos_) {
        overflow_ = true;
        pos_ = end_;
        std::memset(out, 0, n);
        return;
    }
    if ((pos_ & 7) == 0) {
        std::memcpy(out, data_.data() + (pos_ >> 3), n);
        pos_ += n * 8;
        return;
    }
    for (std::size_t i = 0; i < n; ++i)
        out[i] = static_cast<std::uint8_t>(ubit(8));
}

void BitReader::skip(std::size_t nbits) {
    if (nbits > end_ - pos_) {
        overflow_ = true;
        pos_ = end_;
        return;
    }
    pos_ += nbits;
}

void BitReader::seek(std::size_t bitPos) {
    if (bitPos < begin_ || bitPos > end_) {
        overflow_ = true;
        pos_ = end_;
        return;
    }
    pos_ = bitPos;
}

BitReader BitReader::take(std::size_t nbits) {
    BitReader sub;
    sub.data_ = data_;
    sub.begin_ = pos_;
    sub.pos_ = pos_;
    if (nbits > end_ - pos_) {
        sub.end_ = end_;
        sub.overflow_ = true;
        overflow_ = true;
        pos_ = end_;
        return sub;
    }
    sub.end_ = pos_ + nbits;
    pos_ += nbits;
    return sub;
}

} // namespace gmdr
