#pragma once

#include "core/error.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gmdr {

Result<std::vector<std::uint8_t>> zstdCompress(std::span<const std::uint8_t> input, int level = 3);
// `expectedSize` must be the exact decompressed size (stored next to the payload) and within limits.
Result<std::vector<std::uint8_t>> zstdDecompress(std::span<const std::uint8_t> input, std::size_t expectedSize);

} // namespace gmdr
