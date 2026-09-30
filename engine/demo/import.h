#pragma once

#include "core/error.h"
#include "core/file.h"
#include "core/hash.h"
#include "demo/parser.h"
#include "demo/statedb/format.h"

#include <functional>
#include <span>

namespace gmdr::demo {

struct IndexSummary {
    Tick firstTick = 0;
    Tick lastTick = 0;
    std::uint64_t commands = 0;
    std::uint64_t packets = 0;
    float headerPlaybackTime = 0;
};

struct ImportCallbacks {
    std::function<void(const IndexSummary&)> onIndexed;
    std::function<void(const statedb::ChunkRef&)> onChunk;
    std::function<void(Tick)> onProgress; // after every packet; throttle on the receiving side
    std::function<bool()> cancelled;
    DemoSink* observer = nullptr; // receives every parser callback too (verification tools)
};

// The whole import of one demo held in memory: pass 1 (command index -> INDEX chunk), then the full decode
// into `out` as a state.gmstate file. Used by gmdr-import (on inherited handles) and by gmdr-cli.
Result<ParseStats> importDemo(std::span<const std::uint8_t> demo, File& out, const Blake3Digest& demoHash,
                              const ImportCallbacks& callbacks);

} // namespace gmdr::demo
