#include "demo/header.h"

#include "core/byte_reader.h"

namespace gmdr::demo {

Result<DemoHeader> parseHeader(std::span<const std::uint8_t> file) {
    if (file.size() < kHeaderSize)
        return makeError("demo.bad_header", "file is smaller than a demo header");
    ByteReader r(file.first(kHeaderSize));
    DemoHeader h;
    r.fixedString(8, h.magic);
    if (h.magic != "HL2DEMO" && h.magic != "GMODEMO")
        return makeError("demo.bad_header", "not a Source demo (bad magic)");
    r.i32(h.demoProtocol);
    r.i32(h.networkProtocol);
    r.fixedString(260, h.serverName);
    r.fixedString(260, h.clientName);
    r.fixedString(260, h.mapName);
    r.fixedString(260, h.gameDirectory);
    r.f32(h.playbackTime);
    r.i32(h.ticks);
    r.i32(h.frames);
    r.i32(h.signonLength);
    if (!r.ok())
        return makeError("demo.bad_header", "truncated header");
    if (h.demoProtocol != 3)
        return makeError("demo.unsupported_version", "unsupported demo protocol",
                         "demo protocol " + std::to_string(h.demoProtocol));
    if (h.networkProtocol != 24)
        return makeError("demo.unsupported_version", "unsupported network protocol",
                         "network protocol " + std::to_string(h.networkProtocol));
    return h;
}

} // namespace gmdr::demo
