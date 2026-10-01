#include "core/compress.h"

#include "core/limits.h"

#include <string>
#include <zstd.h>

namespace gmdr {

Result<std::vector<std::uint8_t>> zstdCompress(std::span<const std::uint8_t> input, int level) {
    std::vector<std::uint8_t> out(ZSTD_compressBound(input.size()));
    const std::size_t n = ZSTD_compress(out.data(), out.size(), input.data(), input.size(), level);
    if (ZSTD_isError(n))
        return makeError("core.zstd_compress", ZSTD_getErrorName(n));
    out.resize(n);
    return out;
}

Result<std::vector<std::uint8_t>> zstdDecompress(std::span<const std::uint8_t> input,
                                                 std::size_t expectedSize) {
    if (expectedSize > limits::kMaxChunkBytes)
        return makeError("core.zstd_too_large", "decompressed size exceeds limit",
                         std::to_string(expectedSize));
    // The frame header states its content size (our writer always stores it): check it before allocating.
    const unsigned long long frameSize = ZSTD_getFrameContentSize(input.data(), input.size());
    if (frameSize == ZSTD_CONTENTSIZE_ERROR || frameSize == ZSTD_CONTENTSIZE_UNKNOWN ||
        frameSize != expectedSize)
        return makeError("core.zstd_size_mismatch", "compressed frame does not match the stored size");
    std::vector<std::uint8_t> out(expectedSize);
    const std::size_t n = ZSTD_decompress(out.data(), out.size(), input.data(), input.size());
    if (ZSTD_isError(n))
        return makeError("core.zstd_decompress", ZSTD_getErrorName(n));
    if (n != expectedSize)
        return makeError("core.zstd_size_mismatch", "decompressed size differs from the stored size");
    return out;
}

} // namespace gmdr
