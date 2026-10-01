#include "core/hash.h"

#include <blake3.h>
#include <cstring>
#include <xxhash.h>

namespace gmdr {

std::string Blake3Digest::hex() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 15]);
    }
    return out;
}

std::uint64_t Blake3Digest::low64() const {
    std::uint64_t v = 0;
    std::memcpy(&v, bytes.data(), sizeof v);
    return v;
}

struct Blake3Hasher::State {
    blake3_hasher hasher;
};

Blake3Hasher::Blake3Hasher() : state_(std::make_unique<State>()) {
    blake3_hasher_init(&state_->hasher);
}

Blake3Hasher::~Blake3Hasher() = default;

void Blake3Hasher::update(std::span<const std::uint8_t> data) {
    blake3_hasher_update(&state_->hasher, data.data(), data.size());
}

Blake3Digest Blake3Hasher::finalize() const {
    Blake3Digest d;
    blake3_hasher_finalize(&state_->hasher, d.bytes.data(), d.bytes.size());
    return d;
}

Blake3Digest blake3(std::span<const std::uint8_t> data) {
    Blake3Hasher h;
    h.update(data);
    return h.finalize();
}

std::uint64_t xxh3_64(std::span<const std::uint8_t> data) {
    return XXH3_64bits(data.data(), data.size());
}

std::uint64_t stableId64(std::span<const std::uint8_t> data) {
    return blake3(data).low64();
}

} // namespace gmdr
