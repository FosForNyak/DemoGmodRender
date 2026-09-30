#pragma once

#include "core/time.h"

#include <array>
#include <cstdint>
#include <string>

namespace gmdr::demo {

enum class EventKind : std::uint8_t {
    GameEvent = 1,
    Sound = 2,
    TempEntities = 3,
    UserMessage = 4,
    EntityMessage = 5,
    GModNet = 6,
    Decal = 7,
    SetView = 8,
    FixAngle = 9,
    Voice = 10,
    ConsoleCmd = 11,
    Print = 12,
    StringCmd = 13,
    DecodeError = 14,
};

const char* eventKindName(EventKind kind);

// A timeline event. Payloads are not copied: `bitOffset`/`bitLength` point into the original .dem, which
// stays the single source of truth.
struct DemoEvent {
    Tick tick = 0;
    EventKind kind = EventKind::GameEvent;
    std::int32_t entity = -1;
    std::string name;    // game event name, net message name, console command
    std::string summary; // short details ("userid=5 health=40"), never used for logging
    std::uint64_t bitOffset = 0;
    std::uint32_t bitLength = 0;
};

struct ServerInfo {
    int protocol = 0;
    int serverCount = 0;
    bool hltv = false;
    bool dedicated = false;
    std::uint32_t clientCrc = 0;
    int maxClasses = 0;
    std::array<std::uint8_t, 16> mapMd5{};
    int playerSlot = 0;
    int maxClients = 0;
    float tickInterval = 0;
    char os = 0;
    std::string gameDir, mapName, skyName, hostName;
    std::string loadingUrl, gamemode; // GMod additions
};

} // namespace gmdr::demo
