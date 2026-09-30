#pragma once

#include <span>
#include <string>

namespace gmdr::demo {

// Network message ids of protocol 24 (Source SDK 2013) plus the GMod additions.
enum class NetMsg : int {
    Nop = 0,
    Disconnect = 1,
    File = 2,
    Tick = 3,
    StringCmd = 4,
    SetConVar = 5,
    SignonState = 6,
    Print = 7,
    ServerInfo = 8,
    SendTable = 9,
    ClassInfo = 10,
    SetPause = 11,
    CreateStringTable = 12,
    UpdateStringTable = 13,
    VoiceInit = 14,
    VoiceData = 15,
    Sounds = 17,
    SetView = 18,
    FixAngle = 19,
    CrosshairAngle = 20,
    BSPDecal = 21,
    UserMessage = 23,
    EntityMessage = 24,
    GameEvent = 25,
    PacketEntities = 26,
    TempEntities = 27,
    Prefetch = 28,
    Menu = 29,
    GameEventList = 30,
    GetCvarValue = 31,
    CmdKeyValues = 32,
    GModServerToClient = 33,
};

const char* netMsgName(int type);

// Everything that differs between engine builds lives here, so a new GMod build is a new entry rather
// than code changes spread through the parser. Values below are confirmed on the local corpus (spec §4).
struct ProtocolVariant {
    std::string name;
    int netMsgTypeBits = 6;
    int edictBits = 13;
    int serialBits = 10;
    int deltaSizeBits = 24;             // svc_PacketEntities payload length
    int serverInfoExtraStrings = 2;     // sv_loadingurl, gamemode
    int serverInfoExtraBits = 16;
    int createTableMaxEntriesBits = 5;  // width of log2(maxEntries)
    bool createTableVarintLength = true;
    int userDataLengthBits = 19;
    int updateTableLengthBits = 20;
    int userMessageLengthBits = 11;
    int entityMessageLengthBits = 11;
    int gameEventLengthBits = 11;
    bool tempEntitiesVarintLength = true;
    int gmodNetLengthBits = 20;
    int sendPropFlagBits = 16;
    int maxDecalIndexBits = 9;
    int modelIndexBits = 13;
    int soundIndexBits = 14;
};

std::span<const ProtocolVariant> knownProtocolVariants();

} // namespace gmdr::demo
