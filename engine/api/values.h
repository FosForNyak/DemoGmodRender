#pragma once

#include "core/json.h"
#include "demo/statedb/reader.h"
#include "demo/types.h"

#include <optional>
#include <string>
#include <vector>

namespace gmdr::api {

const char* propTypeName(demo::PropType type);

// A property value as JSON: numbers (int64 beyond 2^53 and non-finite floats as strings), vectors as
// [x, y, z], arrays as lists, NW2 tables as {flag, entries: [{key, type, value}]} with keys resolved through
// the "networkvars" string table when given.
Json propToJson(const demo::PropValue& v, const demo::statedb::TableState* networkVars = nullptr);

// What the UI needs to know about a class, derived once from its flattened props.
struct ClassInfo {
    std::string name;
    std::string group;              // player, weapon, npc, prop, world, other
    int origin = -1;                // DT_BaseEntity.m_vecOrigin
    int originLocal = -1;           // players: DT_*LocalPlayerExclusive.m_vecOrigin (the recording player)
    int originNonLocal = -1;        // players: DT_*NonLocalPlayerExclusive.m_vecOrigin
    int originZ = -1;               // some SDKs split z: m_vecOrigin[2]
    int rotation = -1;              // DT_BaseEntity.m_angRotation
    int eyePitch = -1, eyeYaw = -1; // players: m_angEyeAngles[0], [1]
    int modelIndex = -1;            // DT_BaseEntity.m_nModelIndex
    int moveParent = -1;            // DT_BaseEntity.moveparent (m_hMoveParent)
    int health = -1;
    int team = -1;
};
ClassInfo describeClass(const demo::statedb::SchemaClass& c);

// GMod player_info_t in the "userinfo" string table (entry = entity index - 1): name[128] at 0,
// fakeplayer at 300 (324 bytes in total). Returns nothing for empty or foreign-sized entries.
struct PlayerInfo {
    std::string name;
    bool bot = false;
};
std::optional<PlayerInfo> playerInfo(const demo::statedb::WorldState& s, int entityIndex);

// EHANDLE as sent on the wire (13-bit index + 10-bit serial); 0x7FFFFF = none.
inline constexpr std::int64_t kInvalidEHandle = (1 << 23) - 1;

// Entity position for the top-down view: origin (player exclusive tables first) and yaw.
struct Placement {
    demo::Vec3 origin;
    float pitch = 0, yaw = 0;
};
std::optional<Placement> placement(const demo::statedb::EntityState& e, const ClassInfo& info,
                                   bool localPlayer);

} // namespace gmdr::api
