// The state file reader on untrusted bytes (the main process never trusts state.gmstate): open, directory,
// JSON chunks, schema, lives, index, events, camera and state at a few ticks. Seeds are real small state
// files written by StateWriter.
#include "core/file.h"
#include "demo/import.h"
#include "demo/statedb/reader.h"
#include "fuzz.h"

#include <cstring>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <process.h>
#define GMDR_GETPID _getpid
#else
#include <unistd.h>
#define GMDR_GETPID getpid
#endif

using namespace gmdr;

namespace {

std::filesystem::path scratch() {
    static const auto p = std::filesystem::temp_directory_path() /
                          ("gmdr-fuzz-statedb-" + std::to_string(GMDR_GETPID()) + ".gmstate");
    return p;
}

std::vector<std::uint8_t> importToBytes(const std::vector<std::uint8_t>& demo) {
    {
        auto f = File::open(scratch(), File::Mode::CreateTruncate);
        if (!f)
            return {};
        (void)demo::importDemo(demo, *f, blake3(demo), {});
    }
    auto bytes = readWholeFile(scratch(), 64u << 20);
    return bytes ? *bytes : std::vector<std::uint8_t>{};
}

} // namespace

std::vector<std::vector<std::uint8_t>> fuzzSeeds() {
    std::vector<std::uint8_t> demo(1072, 0);
    std::memcpy(demo.data(), "HL2DEMO", 7);
    const std::int32_t demoProtocol = 3, netProtocol = 24;
    std::memcpy(demo.data() + 8, &demoProtocol, 4);
    std::memcpy(demo.data() + 12, &netProtocol, 4);
    demo.insert(demo.end(), {7, 0, 0, 0, 0});
    auto state = importToBytes(demo);
    std::vector<std::vector<std::uint8_t>> seeds;
    if (!state.empty())
        seeds.push_back(state);
    return seeds;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > (4u << 20))
        return 0;
    {
        auto f = File::open(scratch(), File::Mode::CreateTruncate);
        if (!f)
            return 0;
        if (size && !f->writeAt(0, std::span(data, size)))
            return 0;
    }
    for (bool complete : {true, false}) {
        auto reader = demo::statedb::StateReader::open(scratch(), complete);
        if (!reader)
            continue;
        auto& r = **reader;
        (void)r.info();
        (void)r.manifest();
        (void)r.schema();
        (void)r.lives();
        (void)r.index();
        (void)r.events(0, 1'000'000, 1000);
        (void)r.camera(0, 1'000'000);
        for (Tick t : {Tick{0}, Tick{1}, Tick{100}, r.readyTick(), r.lastTick()})
            (void)r.withStateAt(t, [](const demo::statedb::WorldState&) {});
    }
    return 0;
}
