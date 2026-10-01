#include "assets/archives.h"

#include "core/byte_reader.h"
#include "core/text.h"

#include <algorithm>

namespace gmdr::assets {

namespace {

constexpr std::size_t kMaxNameBytes = 1024;
constexpr std::size_t kMaxArchiveFiles = 4u << 20;

Error bad(const char* code, const char* what) {
    return makeError(code, what);
}

} // namespace

// ---- VPK ----

Result<VpkIndex> parseVpkDirectory(std::span<const std::uint8_t> dirFile) {
    ByteReader r(dirFile);
    std::uint32_t signature = 0, version = 0, treeSize = 0;
    r.u32(signature);
    r.u32(version);
    r.u32(treeSize);
    if (!r.ok() || signature != 0x55AA1234)
        return bad("vpk.bad_magic", "not a VPK directory file");
    if (version != 1 && version != 2)
        return makeError("vpk.version", "unsupported VPK version", std::to_string(version));
    if (version == 2)
        r.skip(16); // file data, archive MD5, other MD5, signature section sizes
    if (!r.ok() || treeSize > r.remaining())
        return bad("vpk.truncated", "VPK tree is larger than the file");

    VpkIndex index;
    index.version = version;
    index.treeEnd = static_cast<std::uint32_t>(r.position() + treeSize);
    std::span<const std::uint8_t> treeBytes;
    r.bytes(treeSize, treeBytes);
    const std::size_t treeStart = index.treeEnd - treeSize;
    ByteReader t(treeBytes);
    std::string ext, path, name;
    while (true) {
        if (!t.cstring(kMaxNameBytes, ext))
            return bad("vpk.truncated", "VPK tree ends inside an extension");
        if (ext.empty())
            break;
        while (true) {
            if (!t.cstring(kMaxNameBytes, path))
                return bad("vpk.truncated", "VPK tree ends inside a path");
            if (path.empty())
                break;
            while (true) {
                if (!t.cstring(kMaxNameBytes, name))
                    return bad("vpk.truncated", "VPK tree ends inside a name");
                if (name.empty())
                    break;
                VpkEntry e;
                std::uint16_t terminator = 0;
                t.u32(e.crc);
                t.u16(e.preloadSize);
                t.u16(e.archiveIndex);
                t.u32(e.offset);
                t.u32(e.length);
                t.u16(terminator);
                if (!t.ok() || terminator != 0xFFFF)
                    return bad("vpk.bad_entry", "VPK entry is damaged");
                e.preloadOffset = static_cast<std::uint32_t>(treeStart + t.position());
                if (!t.skip(e.preloadSize))
                    return bad("vpk.truncated", "VPK preload data is outside the tree");
                if (index.files.size() >= kMaxArchiveFiles)
                    return bad("vpk.too_many_files", "VPK has too many files");
                std::string full;
                if (path != " ")
                    full = path + "/";
                full += name;
                if (ext != " ")
                    full += "." + ext;
                index.files[normalizeGamePath(full)] = e;
            }
        }
    }
    return index;
}

// ---- GMA ----

Result<GmaIndex> parseGmaHeader(std::span<const std::uint8_t> head, std::uint64_t fileSize) {
    ByteReader r(head);
    std::span<const std::uint8_t> magic;
    GmaIndex g;
    if (!r.bytes(4, magic) || std::memcmp(magic.data(), "GMAD", 4) != 0)
        return bad("gma.bad_magic", "not a GMA file");
    r.u8(g.version);
    if (!r.ok() || g.version < 1 || g.version > 3)
        return makeError("gma.version", "unsupported GMA version", std::to_string(g.version));
    std::uint64_t timestamp = 0;
    r.u64(g.steamId);
    r.u64(timestamp);
    auto needMore = [&]() -> Error {
        return head.size() < fileSize ? makeError("gma.need_more", "GMA table extends past the bytes read")
                                      : makeError("gma.truncated", "GMA file ends inside its table");
    };
    if (g.version > 1) {
        std::string required;
        for (int i = 0;; ++i) {
            if (!r.cstring(kMaxNameBytes, required))
                return needMore();
            if (required.empty())
                break;
            if (i > 1024)
                return bad("gma.bad_header", "GMA has too many required-content entries");
        }
    }
    std::int32_t addonVersion = 0;
    if (!r.cstring(kMaxNameBytes, g.name) || !r.cstring(1u << 20, g.description) ||
        !r.cstring(kMaxNameBytes, g.author) || !r.i32(addonVersion))
        return needMore();
    struct Pending {
        std::string name;
        std::uint64_t size;
        std::uint32_t crc;
    };
    std::vector<Pending> pending;
    while (true) {
        std::uint32_t number = 0;
        if (!r.u32(number))
            return needMore();
        if (number == 0)
            break;
        Pending p;
        std::uint64_t size = 0;
        if (!r.cstring(kMaxNameBytes, p.name) || !r.u64(size) || !r.u32(p.crc))
            return needMore();
        if (size > fileSize)
            return bad("gma.bad_entry", "GMA entry is larger than the file");
        p.size = size;
        if (pending.size() >= kMaxArchiveFiles)
            return bad("gma.too_many_files", "GMA has too many files");
        pending.push_back(std::move(p));
    }
    std::uint64_t offset = r.position();
    if (offset > fileSize)
        return bad("gma.bad_header", "GMA table extends past the end of the file");
    for (auto& p : pending) {
        if (p.size > fileSize - offset)
            return bad("gma.bad_entry", "GMA entry data is outside the file");
        g.files[normalizeGamePath(p.name)] = GmaEntry{offset, p.size, p.crc};
        offset += p.size;
    }
    return g;
}

Result<GmaIndex> readGma(const std::filesystem::path& path) {
    auto file = File::open(path, File::Mode::Read);
    if (!file)
        return file.error();
    auto size = file->size();
    if (!size)
        return size.error();
    std::size_t want = 1u << 20;
    constexpr std::size_t kMaxHead = 64u << 20;
    while (true) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(want, *size));
        std::vector<std::uint8_t> head(n);
        GMDR_TRY(file->readExactAt(0, head));
        auto g = parseGmaHeader(head, *size);
        if (g || g.error().code != "gma.need_more" || want >= kMaxHead)
            return g;
        want *= 4;
    }
}

// ---- ZIP ----

Result<ZipIndex> parseZipDirectory(std::span<const std::uint8_t> zip) {
    constexpr std::uint32_t kEocd = 0x06054B50, kCentral = 0x02014B50;
    if (zip.size() < 22)
        return bad("zip.truncated", "ZIP is too small");
    // The end-of-central-directory record is within the last 22 + 65535 bytes.
    const std::size_t lowest = zip.size() > 22 + 65535 ? zip.size() - 22 - 65535 : 0;
    std::size_t eocd = SIZE_MAX;
    for (std::size_t i = zip.size() - 22 + 1; i-- > lowest;) {
        std::uint32_t sig;
        std::memcpy(&sig, zip.data() + i, 4);
        if (sig == kEocd) {
            eocd = i;
            break;
        }
    }
    if (eocd == SIZE_MAX)
        return bad("zip.no_directory", "ZIP end-of-directory record not found");
    ByteReader e(zip.subspan(eocd + 4));
    std::uint16_t disk = 0, cdDisk = 0, entriesOnDisk = 0, entries = 0;
    std::uint32_t cdSize = 0, cdOffset = 0;
    e.u16(disk);
    e.u16(cdDisk);
    e.u16(entriesOnDisk);
    e.u16(entries);
    e.u32(cdSize);
    e.u32(cdOffset);
    if (!e.ok() || cdOffset > eocd || cdSize > eocd - cdOffset)
        return bad("zip.bad_directory", "ZIP central directory is outside the file");
    ZipIndex index;
    ByteReader r(zip.subspan(cdOffset, cdSize));
    for (std::uint32_t i = 0; i < entries; ++i) {
        std::uint32_t sig = 0, crc = 0, attrs = 0;
        std::uint16_t made = 0, needed = 0, flags = 0, time = 0, date = 0, nameLen = 0, extraLen = 0,
                      commentLen = 0, diskStart = 0, internal = 0;
        ZipEntry z;
        r.u32(sig);
        if (!r.ok() || sig != kCentral)
            return bad("zip.bad_entry", "ZIP central directory entry is damaged");
        r.u16(made);
        r.u16(needed);
        r.u16(flags);
        r.u16(z.method);
        r.u16(time);
        r.u16(date);
        r.u32(crc);
        r.u32(z.compressedSize);
        r.u32(z.size);
        r.u16(nameLen);
        r.u16(extraLen);
        r.u16(commentLen);
        r.u16(diskStart);
        r.u16(internal);
        r.u32(attrs);
        r.u32(z.localHeaderOffset);
        std::span<const std::uint8_t> name;
        r.bytes(nameLen, name);
        r.skip(static_cast<std::size_t>(extraLen) + commentLen);
        if (!r.ok())
            return bad("zip.bad_entry", "ZIP central directory entry is damaged");
        if (z.localHeaderOffset > cdOffset || z.compressedSize > cdOffset - z.localHeaderOffset)
            return bad("zip.bad_entry", "ZIP entry data is outside the file");
        if (index.files.size() >= kMaxArchiveFiles)
            return bad("zip.too_many_files", "ZIP has too many files");
        const std::string n =
            sanitizeUtf8(std::string_view(reinterpret_cast<const char*>(name.data()), name.size()));
        if (!n.empty() && n.back() != '/')
            index.files[normalizeGamePath(n)] = z;
    }
    return index;
}

// ---- BSP ----

Result<BspInfo> parseBspHeader(std::span<const std::uint8_t> head, std::uint64_t bspSize) {
    constexpr int kLumps = 64, kPakfile = 40;
    ByteReader r(head);
    std::span<const std::uint8_t> ident;
    BspInfo info;
    if (!r.bytes(4, ident) || std::memcmp(ident.data(), "VBSP", 4) != 0)
        return bad("bsp.bad_magic", "not a Source BSP map");
    r.i32(info.version);
    if (!r.ok() || info.version < 19 || info.version > 21)
        return makeError("bsp.version", "unsupported BSP version", std::to_string(info.version));
    for (int i = 0; i < kLumps; ++i) {
        std::int32_t ofs = 0, len = 0, ver = 0, fourcc = 0;
        r.i32(ofs);
        r.i32(len);
        r.i32(ver);
        r.i32(fourcc);
        if (i == kPakfile) {
            if (ofs < 0 || len < 0 || static_cast<std::uint64_t>(ofs) > bspSize ||
                static_cast<std::uint64_t>(len) > bspSize - static_cast<std::uint64_t>(ofs))
                return bad("bsp.bad_lump", "BSP pakfile lump is outside the map");
            info.pakfileOffset = static_cast<std::uint64_t>(ofs);
            info.pakfileSize = static_cast<std::uint64_t>(len);
        }
    }
    r.i32(info.mapRevision);
    if (!r.ok())
        return bad("bsp.truncated", "BSP header is truncated");
    return info;
}

Result<std::vector<std::string>> listBspPakfile(const File& file, std::uint64_t offset, std::uint64_t size) {
    constexpr std::size_t kHeaderBytes = 8 + 64 * 16 + 4;
    constexpr std::uint64_t kMaxPakfile = 512u << 20;
    auto total = file.size();
    if (!total)
        return total.error();
    if (offset > *total || size > *total - offset || size < kHeaderBytes)
        return bad("bsp.bad_range", "map range is outside the file");
    std::vector<std::uint8_t> head(kHeaderBytes);
    GMDR_TRY(file.readExactAt(offset, head));
    auto info = parseBspHeader(head, size);
    if (!info)
        return info.error();
    std::vector<std::string> names;
    if (info->pakfileSize == 0)
        return names;
    if (info->pakfileSize > kMaxPakfile)
        return bad("bsp.pakfile_too_large", "map pakfile is too large");
    std::vector<std::uint8_t> pak(static_cast<std::size_t>(info->pakfileSize));
    GMDR_TRY(file.readExactAt(offset + info->pakfileOffset, pak));
    auto zip = parseZipDirectory(pak);
    if (!zip)
        return zip.error();
    names.reserve(zip->files.size());
    for (const auto& [name, entry] : zip->files)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace gmdr::assets
