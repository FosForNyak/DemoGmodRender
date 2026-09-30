#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace gmdr {

// Bounds-checked little-endian reader over bytes. Failures are sticky: after the first out-of-range read
// every later read fails too, so callers may check `ok()` once after a group of reads.
class ByteReader {
public:
    ByteReader() = default;
    explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

    template <class T> bool read(T& out) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!ok_ || sizeof(T) > data_.size() - pos_) {
            ok_ = false;
            out = T{};
            return false;
        }
        std::memcpy(&out, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    bool u8(std::uint8_t& v) { return read(v); }
    bool i32(std::int32_t& v) { return read(v); }
    bool u32(std::uint32_t& v) { return read(v); }
    bool u64(std::uint64_t& v) { return read(v); }
    bool f32(float& v) { return read(v); }

    // A fixed-size field holding a null-terminated string (e.g. char[260] in the demo header).
    bool fixedString(std::size_t size, std::string& out) {
        std::span<const std::uint8_t> field;
        if (!bytes(size, field))
            return false;
        std::size_t len = 0;
        while (len < field.size() && field[len] != 0)
            ++len;
        out.assign(reinterpret_cast<const char*>(field.data()), len);
        return true;
    }

    bool bytes(std::size_t n, std::span<const std::uint8_t>& out) {
        if (!ok_ || n > data_.size() - pos_) {
            ok_ = false;
            out = {};
            return false;
        }
        out = data_.subspan(pos_, n);
        pos_ += n;
        return true;
    }

    bool skip(std::size_t n) {
        if (!ok_ || n > data_.size() - pos_) {
            ok_ = false;
            return false;
        }
        pos_ += n;
        return true;
    }

    std::size_t position() const { return pos_; }
    std::size_t remaining() const { return data_.size() - pos_; }
    std::size_t size() const { return data_.size(); }
    bool ok() const { return ok_; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace gmdr
