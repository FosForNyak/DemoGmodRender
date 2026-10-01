#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gmdr {

struct Blake3Digest {
    std::array<std::uint8_t, 32> bytes{};
    std::string hex() const;
    std::uint64_t low64() const; // first 8 bytes, little-endian
    bool operator==(const Blake3Digest&) const = default;
};

class Blake3Hasher {
public:
    Blake3Hasher();
    ~Blake3Hasher();
    Blake3Hasher(const Blake3Hasher&) = delete;
    Blake3Hasher& operator=(const Blake3Hasher&) = delete;

    void update(std::span<const std::uint8_t> data);
    Blake3Digest finalize() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

Blake3Digest blake3(std::span<const std::uint8_t> data);
std::uint64_t xxh3_64(std::span<const std::uint8_t> data);

// A stable 64-bit id derived from the given fields (used for entity life ids).
std::uint64_t stableId64(std::span<const std::uint8_t> data);

} // namespace gmdr
