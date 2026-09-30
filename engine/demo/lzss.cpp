#include "demo/lzss.h"

#include "core/limits.h"

#include <cstring>

namespace gmdr::demo {

Result<std::vector<std::uint8_t>> lzssDecompress(std::span<const std::uint8_t> in) {
    if (in.size() < 8 || std::memcmp(in.data(), "LZSS", 4) != 0)
        return makeError("demo.lzss_header", "missing LZSS header");
    std::uint32_t size = 0;
    std::memcpy(&size, in.data() + 4, 4);
    if (size > limits::kMaxDecompressedBytes)
        return makeError("demo.lzss_too_large", "LZSS output exceeds limit");

    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::size_t src = 8;
    unsigned cmd = 0;
    unsigned getCmd = 0;
    while (true) {
        if (getCmd == 0) {
            if (src >= in.size())
                return makeError("demo.lzss_truncated", "LZSS stream ends early");
            cmd = in[src++];
        }
        getCmd = (getCmd + 1) & 7;
        if (cmd & 1) {
            if (src + 2 > in.size())
                return makeError("demo.lzss_truncated", "LZSS stream ends early");
            const std::size_t position = (static_cast<std::size_t>(in[src]) << 4) | (in[src + 1] >> 4);
            const std::size_t count = (in[src + 1] & 0x0F) + 1u;
            src += 2;
            if (count == 1)
                break;
            if (position + 1 > out.size())
                return makeError("demo.lzss_corrupt", "LZSS back-reference before the start");
            if (out.size() + count > size)
                return makeError("demo.lzss_corrupt", "LZSS output larger than declared");
            const std::size_t from = out.size() - position - 1;
            for (std::size_t i = 0; i < count; ++i)
                out.push_back(out[from + i]);
        } else {
            if (src >= in.size())
                return makeError("demo.lzss_truncated", "LZSS stream ends early");
            if (out.size() + 1 > size)
                return makeError("demo.lzss_corrupt", "LZSS output larger than declared");
            out.push_back(in[src++]);
        }
        cmd >>= 1;
    }
    if (out.size() != size)
        return makeError("demo.lzss_corrupt", "LZSS output size differs from the header");
    return out;
}

} // namespace gmdr::demo
