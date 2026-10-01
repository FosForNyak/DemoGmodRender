#pragma once

#include "core/error.h"
#include "demo/types.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gmdr::demo::statedb {

// Growable little-endian byte buffer with varints.
class Encoder {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
    void f32(float v) { raw(&v, 4); }
    void f64(double v) { raw(&v, 8); }
    void varint(std::uint64_t v) {
        while (v >= 0x80) {
            buf_.push_back(static_cast<std::uint8_t>(v | 0x80));
            v >>= 7;
        }
        buf_.push_back(static_cast<std::uint8_t>(v));
    }
    void svarint(std::int64_t v) {
        varint((static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63));
    }
    void string(std::string_view s) {
        varint(s.size());
        raw(s.data(), s.size());
    }
    void bytes(std::span<const std::uint8_t> b) {
        varint(b.size());
        raw(b.data(), b.size());
    }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    void value(const PropValue& v);

    std::vector<std::uint8_t>& data() { return buf_; }
    const std::vector<std::uint8_t>& data() const { return buf_; }
    std::size_t size() const { return buf_.size(); }
    void clear() { buf_.clear(); }

private:
    std::vector<std::uint8_t> buf_;
};

// Bounds-checked reader for the same encoding. Sticky failure; check ok() after reads.
class Decoder {
public:
    explicit Decoder(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint8_t u8() {
        if (!need(1))
            return 0;
        return data_[pos_++];
    }
    std::uint32_t u32() { return fixed<std::uint32_t>(); }
    std::uint64_t u64() { return fixed<std::uint64_t>(); }
    float f32() { return fixed<float>(); }
    double f64() { return fixed<double>(); }
    std::uint64_t varint() {
        std::uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (!need(1))
                return 0;
            const std::uint8_t b = data_[pos_++];
            v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80))
                return v;
        }
        ok_ = false;
        return 0;
    }
    std::int64_t svarint() {
        const std::uint64_t u = varint();
        return static_cast<std::int64_t>((u >> 1) ^ (~(u & 1) + 1));
    }
    std::string string(std::size_t maxLen = 1u << 20) {
        const std::uint64_t n = varint();
        if (n > maxLen || !need(static_cast<std::size_t>(n))) {
            ok_ = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_), static_cast<std::size_t>(n));
        pos_ += static_cast<std::size_t>(n);
        return s;
    }
    std::vector<std::uint8_t> bytes(std::size_t maxLen = 1u << 20) {
        const std::uint64_t n = varint();
        if (n > maxLen || !need(static_cast<std::size_t>(n))) {
            ok_ = false;
            return {};
        }
        std::vector<std::uint8_t> b(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                    data_.begin() + static_cast<std::ptrdiff_t>(pos_ + n));
        pos_ += static_cast<std::size_t>(n);
        return b;
    }
    bool value(PropValue& out, int depth = 0);

    bool ok() const { return ok_; }
    bool atEnd() const { return pos_ >= data_.size(); }
    std::size_t position() const { return pos_; }

private:
    bool need(std::size_t n) {
        if (!ok_ || n > data_.size() - pos_) {
            ok_ = false;
            return false;
        }
        return true;
    }
    template <class T> T fixed() {
        T v{};
        if (!need(sizeof(T)))
            return v;
        std::memcpy(&v, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return v;
    }

    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace gmdr::demo::statedb
