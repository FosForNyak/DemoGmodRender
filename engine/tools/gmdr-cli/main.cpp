// gmdr-cli: headless client of the engine. For now: `parse <demo>` runs the demo parser and prints
// statistics as JSON (used to compare with the reference decoder on the local corpus).

#include "core/file.h"
#include "core/json.h"
#include "core/text.h"
#include "demo/parser.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>

namespace {

struct StatsSink : gmdr::demo::DemoSink {
    std::map<std::string, std::uint64_t> eventsByKind;
    std::uint64_t newLives = 0;
    std::uint64_t decodeErrors = 0;
    std::vector<std::string> errorSamples;
    gmdr::demo::ServerInfo server;
    std::string map;
    void onHeader(const gmdr::demo::DemoHeader& h) override { map = h.mapName; }
    void onServerInfo(const gmdr::demo::ServerInfo& s) override { server = s; }
    void onEvent(const gmdr::demo::DemoEvent& e) override { ++eventsByKind[gmdr::demo::eventKindName(e.kind)]; }
    void onEntityEnter(gmdr::Tick, const gmdr::demo::EntityRef&, bool newLife,
                       std::span<const gmdr::demo::PropValue>) override {
        newLives += newLife ? 1 : 0;
    }
    void onDecodeError(gmdr::Tick tick, std::uint64_t offset, const gmdr::Error& e) override {
        ++decodeErrors;
        if (errorSamples.size() < 10)
            errorSamples.push_back("tick " + std::to_string(tick) + " @" + std::to_string(offset) + ": " + e.code +
                                   " " + e.message + " " + e.details);
    }
};

int cmdParse(const std::string& path) {
    auto file = gmdr::File::open(gmdr::pathFromUtf8(path), gmdr::File::Mode::Read);
    if (!file) {
        std::cerr << file.error().code << ": " << file.error().message << " " << file.error().details << "\n";
        return 2;
    }
    auto map = gmdr::MappedFile::map(*file);
    if (!map) {
        std::cerr << map.error().message << "\n";
        return 2;
    }
    StatsSink sink;
    gmdr::demo::DemoParser parser(map->data(), sink);
    const auto t0 = std::chrono::steady_clock::now();
    auto r = parser.run();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const auto& st = parser.stats();
    gmdr::Json out = {
        {"ok", r.ok()},
        {"error", r.ok() ? "" : r.error().code + " " + r.error().message},
        {"seconds", secs},
        {"map", sink.map},
        {"tickInterval", sink.server.tickInterval},
        {"gamemode", sink.server.gamemode},
        {"packets", st.packets},
        {"entityEnters", st.entityEnters},
        {"entityUpdates", st.entityUpdates},
        {"entityLeaves", st.entityLeaves},
        {"newLives", sink.newLives},
        {"events", st.events},
        {"eventsByKind", sink.eventsByKind},
        {"decodeErrors", st.decodeErrors},
        {"errorSamples", sink.errorSamples},
    };
    std::cout << out.dump(2) << "\n";
    return r.ok() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "parse")
        return cmdParse(argv[2]);
    std::cerr << "usage: gmdr-cli parse <demo.dem>\n";
    return 64;
}
