#pragma once

#include "core/error.h"
#include "core/file.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Readers for the archive formats GMod content lives in. All of them only build a file index (normalized
// lower-case paths with '/'); every offset and size is checked against the container.
namespace gmdr::assets {

// ---- VPK (Valve pak, versions 1 and 2) ----
struct VpkEntry {
    std::uint32_t crc = 0;
    std::uint16_t archiveIndex = 0; // 0x7FFF: data follows the tree in the _dir file
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::uint32_t preloadOffset = 0; // in the _dir file
    std::uint16_t preloadSize = 0;
};
struct VpkIndex {
    std::uint32_t version = 0;
    std::uint32_t treeEnd = 0; // file offset where the tree ends (base for archiveIndex 0x7FFF)
    std::unordered_map<std::string, VpkEntry> files;
};
Result<VpkIndex> parseVpkDirectory(std::span<const std::uint8_t> dirFile);

// ---- GMA (Garry's Mod addon) ----
struct GmaEntry {
    std::uint64_t offset = 0; // absolute file offset of the data
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
};
struct GmaIndex {
    std::uint8_t version = 0;
    std::uint64_t steamId = 0;
    std::string name, description, author;
    std::unordered_map<std::string, GmaEntry> files;
};
// Parses the header and file table from the start of the file; `fileSize` bounds the data offsets.
// Returns `gma.need_more` if `head` ends before the table does (the caller retries with more bytes).
Result<GmaIndex> parseGmaHeader(std::span<const std::uint8_t> head, std::uint64_t fileSize);
// Reads as much of the file as the table needs (up to 64 MB) and parses it.
Result<GmaIndex> readGma(const std::filesystem::path& path);

// ---- ZIP (BSP pakfiles) ----
struct ZipEntry {
    std::uint16_t method = 0; // 0 stored, 14 LZMA (Source)
    std::uint32_t compressedSize = 0;
    std::uint32_t size = 0;
    std::uint32_t localHeaderOffset = 0;
};
struct ZipIndex {
    std::unordered_map<std::string, ZipEntry> files;
};
Result<ZipIndex> parseZipDirectory(std::span<const std::uint8_t> zip);

// ---- BSP ----
struct BspInfo {
    std::int32_t version = 0;
    std::int32_t mapRevision = 0;
    std::uint64_t pakfileOffset = 0; // lump 40, relative to the start of the BSP
    std::uint64_t pakfileSize = 0;
};
Result<BspInfo> parseBspHeader(std::span<const std::uint8_t> bspHead, std::uint64_t bspSize);

// Lists the files in a map's pakfile. The map is the byte range [offset, offset + size) of `file` (a loose
// .bsp, or a GMA/VPK that contains it). Runs in gmdr-import: the map may come from a server.
Result<std::vector<std::string>> listBspPakfile(const File& file, std::uint64_t offset, std::uint64_t size);

} // namespace gmdr::assets
