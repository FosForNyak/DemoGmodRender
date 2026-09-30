#pragma once

#include "core/error.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gmdr::demo {

// Valve LZSS ("LZSS" magic, u32 decompressed size, then the stream). Output is capped by limits.
Result<std::vector<std::uint8_t>> lzssDecompress(std::span<const std::uint8_t> input);

} // namespace gmdr::demo
