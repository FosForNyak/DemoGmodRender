// gmdr-import: parses one untrusted .dem into a state.gmstate file. It never opens a path: the parent passes
// an inherited read handle for the demo and a write handle for the state file, and reads JSON lines on
// stdout:
//
//   {"hello":{"format":1,"parser":1}}
//   {"indexed":{"firstTick":..,"lastTick":..,"packets":..,"commands":..}}
//   {"chunk":{"kind":"deltas","offset":..,"from":..,"to":..,"raw":..,"stored":..}}   (after every chunk)
//   {"progress":{"tick":..}}                                                         (at most ~10 per second)
//   {"done":{"seconds":..,"packets":..,"entityEnters":..,...}}
//   {"error":{"code":"..","message":"..","details":".."}}                            (last line on failure)
//
// Usage: gmdr-import --in-handle N --out-handle M [--hash <64 hex chars>]
//        gmdr-import --list-pakfile --in-handle N --offset O --size S  ->  {"pakfile":{"files":[...]}}

#include "assets/archives.h"
#include "core/file.h"
#include "core/hash.h"
#include "core/json.h"
#include "core/text.h"
#include "demo/import.h"
#include "demo/statedb/format.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

namespace {

void emit(const gmdr::Json& j) {
    const std::string s = j.dump() + "\n";
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

int fail(const gmdr::Error& e) {
    emit({{"error", {{"code", e.code}, {"message", e.message}, {"details", e.details}}}});
    return 1;
}

void harden() {
#ifdef _WIN32
    // Defence in depth on top of the Job Object: no dynamic code, no DLLs from remote or low-integrity
    // locations, no legacy extension points, strict handle checks. Failures are ignored (older Windows).
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dyn{};
    dyn.ProhibitDynamicCode = 1;
    SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &dyn, sizeof dyn);
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY img{};
    img.NoRemoteImages = 1;
    img.NoLowMandatoryLabelImages = 1;
    img.PreferSystem32Images = 1;
    SetProcessMitigationPolicy(ProcessImageLoadPolicy, &img, sizeof img);
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY ext{};
    ext.DisableExtensionPoints = 1;
    SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &ext, sizeof ext);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

// Checks that the inherited handles are real before strict handle checking turns any bad reference into
// a crash.
bool handlesValid(std::intptr_t a, std::intptr_t b) {
#ifdef _WIN32
    DWORD flags = 0;
    if (!GetHandleInformation(reinterpret_cast<HANDLE>(a), &flags) ||
        !GetHandleInformation(reinterpret_cast<HANDLE>(b), &flags))
        return false;
    PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY strict{};
    strict.RaiseExceptionOnInvalidHandleReference = 1;
    strict.HandleExceptionsPermanentlyEnabled = 1;
    SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &strict, sizeof strict);
    return true;
#else
    return fcntl(static_cast<int>(a), F_GETFD) != -1 && fcntl(static_cast<int>(b), F_GETFD) != -1;
#endif
}

bool parseHandle(const char* s, std::intptr_t& out) {
    char* end = nullptr;
    const long long v = std::strtoll(s, &end, 10);
    if (!end || *end != '\0' || v <= 0)
        return false;
    out = static_cast<std::intptr_t>(v);
    return true;
}

bool parseHash(const std::string& hex, gmdr::Blake3Digest& out) {
    if (hex.size() != 64)
        return false;
    for (std::size_t i = 0; i < 32; ++i) {
        unsigned v = 0;
        for (int k = 0; k < 2; ++k) {
            const char c = hex[i * 2 + static_cast<std::size_t>(k)];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= static_cast<unsigned>(c - 'a' + 10);
            else
                return false;
        }
        out.bytes[i] = static_cast<std::uint8_t>(v);
    }
    return true;
}

// --list-pakfile: lists the pakfile of a map given as a byte range of the inherited file.
int listPakfile(std::intptr_t inHandle, std::uint64_t offset, std::uint64_t size) {
    if (!handlesValid(inHandle, inHandle))
        return fail(gmdr::makeError("import.bad_handle", "inherited handles are not valid"));
    gmdr::File in = gmdr::File::adopt(inHandle);
    auto names = gmdr::assets::listBspPakfile(in, offset, size);
    if (!names)
        return fail(names.error());
    gmdr::Json files = gmdr::Json::array();
    for (const auto& n : *names)
        files.push_back(gmdr::sanitizeUtf8(n));
    emit({{"pakfile", {{"files", std::move(files)}}}});
    return 0;
}

bool parseU64(const char* s, std::uint64_t& out) {
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s, &end, 10);
    if (!end || *end != '\0' || s[0] == '-')
        return false;
    out = v;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    harden();
    std::intptr_t inHandle = 0, outHandle = 0;
    std::string hashHex;
    bool pakfileMode = false;
    std::uint64_t offset = 0, size = 0;
    for (int i = 1; i < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--list-pakfile") {
            pakfileMode = true;
            --i;
            continue;
        }
        if (i + 1 >= argc)
            return fail(gmdr::makeError("import.usage", "missing value", key));
        if (key == "--in-handle" && parseHandle(argv[i + 1], inHandle))
            continue;
        if (key == "--out-handle" && parseHandle(argv[i + 1], outHandle))
            continue;
        if (key == "--offset" && parseU64(argv[i + 1], offset))
            continue;
        if (key == "--size" && parseU64(argv[i + 1], size))
            continue;
        if (key == "--hash") {
            hashHex = argv[i + 1];
            continue;
        }
        return fail(gmdr::makeError("import.usage", "bad arguments", key));
    }
    if (pakfileMode) {
        if (!inHandle || !size)
            return fail(gmdr::makeError(
                "import.usage", "usage: gmdr-import --list-pakfile --in-handle N --offset O --size S"));
        return listPakfile(inHandle, offset, size);
    }
    if (!inHandle || !outHandle)
        return fail(
            gmdr::makeError("import.usage", "usage: gmdr-import --in-handle N --out-handle M [--hash HEX]"));

    if (!handlesValid(inHandle, outHandle))
        return fail(gmdr::makeError("import.bad_handle", "inherited handles are not valid"));
    emit({{"hello",
           {{"format", gmdr::demo::statedb::kFormatVersion},
            {"parser", gmdr::demo::statedb::kParserVersion}}}});
    const auto t0 = std::chrono::steady_clock::now();

    gmdr::File in = gmdr::File::adopt(inHandle);
    gmdr::File out = gmdr::File::adopt(outHandle);
    auto map = gmdr::MappedFile::map(in);
    if (!map)
        return fail(map.error());

    gmdr::Blake3Digest hash;
    if (hashHex.empty())
        hash = gmdr::blake3(map->data());
    else if (!parseHash(hashHex, hash))
        return fail(gmdr::makeError("import.usage", "bad --hash"));

    auto lastProgress = std::chrono::steady_clock::now();
    gmdr::demo::ImportCallbacks cb;
    cb.onIndexed = [](const gmdr::demo::IndexSummary& s) {
        emit({{"indexed",
               {{"firstTick", s.firstTick},
                {"lastTick", s.lastTick},
                {"packets", s.packets},
                {"commands", s.commands}}}});
    };
    cb.onChunk = [](const gmdr::demo::statedb::ChunkRef& c) {
        emit({{"chunk",
               {{"kind", gmdr::demo::statedb::chunkKindName(c.kind)},
                {"offset", c.offset},
                {"from", c.tickFrom},
                {"to", c.tickTo},
                {"raw", c.rawSize},
                {"stored", c.storedSize}}}});
    };
    cb.onProgress = [&](gmdr::Tick tick) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastProgress < std::chrono::milliseconds(100))
            return;
        lastProgress = now;
        emit({{"progress", {{"tick", tick}}}});
    };

    auto stats = gmdr::demo::importDemo(map->data(), out, hash, cb);
    if (!stats)
        return fail(stats.error());
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    emit({{"done",
           {{"seconds", secs},
            {"packets", stats->packets},
            {"entityEnters", stats->entityEnters},
            {"entityUpdates", stats->entityUpdates},
            {"entityLeaves", stats->entityLeaves},
            {"events", stats->events},
            {"decodeErrors", stats->decodeErrors}}}});
    return 0;
}
