#pragma once

#include <cstddef>
#include <cstdint>

// Hard limits applied before any allocation driven by untrusted input ("Слабкі місця", S7).
namespace gmdr::limits {

inline constexpr std::size_t kMaxPacketBytes = 16u << 20;          // one demo command payload
inline constexpr std::size_t kMaxStringBytes = 4096;               // null-terminated strings in messages
inline constexpr std::size_t kMaxStringTables = 64;
inline constexpr std::size_t kMaxStringTableEntries = 65536;
inline constexpr std::size_t kMaxUserDataBytes = 1u << 19;         // 19-bit length field in GMod
inline constexpr std::size_t kMaxDecompressedBytes = 64u << 20;    // LZSS / zstd output
inline constexpr std::size_t kMaxSendTables = 4096;
inline constexpr std::size_t kMaxPropsPerTable = 1024;
inline constexpr std::size_t kMaxFlatProps = 8192;
inline constexpr std::size_t kMaxTableDepth = 32;
inline constexpr std::size_t kMaxServerClasses = 8192;
inline constexpr std::size_t kMaxArrayElements = 1024;
inline constexpr std::size_t kMaxEntities = 1u << 13;              // GMod MAX_EDICT_BITS = 13
inline constexpr std::size_t kMaxNw2Entries = 4096;
inline constexpr std::size_t kMaxGameEventDescriptors = 1024;
inline constexpr std::size_t kMaxGameEventKeys = 64;
inline constexpr std::size_t kMaxChunkBytes = 256u << 20;          // one state-file chunk, decompressed

} // namespace gmdr::limits
