#pragma once

#include "core/time.h"

#include <array>
#include <cstdint>

// state.gmstate: 64-byte header, then append-only chunks (40-byte header + zstd payload), DIRECTORY last.
// See ADR-002 and ADR-005.
namespace gmdr::demo::statedb {

inline constexpr char kFileMagic[8] = {'G', 'M', 'S', 'T', 'A', 'T', 'E', '\0'};
inline constexpr std::uint32_t kFormatVersion = 1;
inline constexpr std::uint32_t kParserVersion = 1;
inline constexpr std::size_t kFileHeaderSize = 64;
inline constexpr std::uint32_t kChunkMagic = 0x4B4E4843; // "CHNK"
inline constexpr std::size_t kChunkHeaderSize = 40;
inline constexpr double kKeyframeIntervalSeconds = 30.0;

enum FileFlags : std::uint32_t { kComplete = 1 };

enum class ChunkKind : std::uint16_t {
    Info = 1,         // JSON: header, server info, stats
    Schema = 2,       // classes and flattened props
    Keyframe = 3,     // full entity state at tickFrom
    StringTables = 4, // string tables changed since the previous snapshot, in full, at tickFrom
    Deltas = 5,       // records in (tickFrom, tickTo]
    Events = 6,
    Camera = 7,
    Lives = 8,
    Manifest = 9, // JSON: content the demo needs
    Directory = 10,
    Index = 11, // pass 1: tick -> command offset in the .dem
};

enum class DeltaOp : std::uint8_t {
    Enter = 1,
    Update = 2,
    Leave = 3,
    StringEntry = 4,
    TableCreate = 5,
};

struct FileHeader {
    char magic[8];
    std::uint32_t formatVersion;
    std::uint32_t flags;
    std::array<std::uint8_t, 32> demoHash;
    std::uint32_t parserVersion;
    std::uint32_t reserved;
    std::uint64_t directoryOffset;
};
static_assert(sizeof(FileHeader) == kFileHeaderSize);

struct ChunkHeader {
    std::uint32_t magic;
    std::uint16_t kind;
    std::uint16_t flags;
    std::int64_t tickFrom;
    std::int64_t tickTo;
    std::uint32_t rawSize;
    std::uint32_t storedSize;
    std::uint64_t checksum; // XXH3-64 of the stored payload
};
static_assert(sizeof(ChunkHeader) == kChunkHeaderSize);

struct IndexEntry {
    Tick tick = 0;
    std::uint64_t offset = 0; // file offset of the command in the .dem
};

struct ChunkRef {
    ChunkKind kind = ChunkKind::Info;
    std::uint64_t offset = 0; // of the chunk header
    Tick tickFrom = 0;
    Tick tickTo = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t storedSize = 0;
};

const char* chunkKindName(ChunkKind kind);

} // namespace gmdr::demo::statedb
