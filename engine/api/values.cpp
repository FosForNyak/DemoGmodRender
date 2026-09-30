#include "api/values.h"

#include "core/text.h"

#include <cmath>
#include <cstring>

namespace gmdr::api {

using demo::PropType;
using demo::PropValue;

namespace {

Json number(double v) {
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v > 0 ? "inf" : "-inf";
    return v;
}

Json vec(const demo::Vec3& v) {
    return Json::array({number(v.x), number(v.y), number(v.z)});
}

const char* nw2TypeName(demo::Nw2Type t) {
    switch (t) {
    case demo::Nw2Type::Nil:
        return "nil";
    case demo::Nw2Type::Float:
        return "float";
    case demo::Nw2Type::Int:
        return "int";
    case demo::Nw2Type::Bool:
        return "bool";
    case demo::Nw2Type::Vector:
        return "vector";
    case demo::Nw2Type::Angle:
        return "angle";
    case demo::Nw2Type::Entity:
        return "entity";
    case demo::Nw2Type::String:
        return "string";
    }
    return "unknown";
}

bool endsWithName(const std::string& s, std::string_view name) {
    return s == name;
}

} // namespace

const char* propTypeName(PropType type) {
    switch (type) {
    case PropType::Int:
        return "int";
    case PropType::Float:
        return "float";
    case PropType::Vector:
        return "vector";
    case PropType::Time:
        return "time";
    case PropType::String:
        return "string";
    case PropType::Array:
        return "array";
    case PropType::DataTable:
        return "table";
    case PropType::GModTable:
        return "nw2";
    }
    return "unknown";
}

Json propToJson(const PropValue& v, const demo::statedb::TableState* networkVars) {
    struct Visitor {
        const demo::statedb::TableState* vars;
        Json operator()(std::monostate) const { return nullptr; }
        Json operator()(std::int64_t x) const {
            constexpr std::int64_t kSafe = (1ll << 53);
            if (x > kSafe || x < -kSafe)
                return std::to_string(x);
            return x;
        }
        Json operator()(float x) const { return number(x); }
        Json operator()(double x) const { return number(x); }
        Json operator()(const demo::Vec3& x) const { return vec(x); }
        Json operator()(const std::string& x) const { return sanitizeUtf8(x); }
        Json operator()(const PropValue::Array& a) const {
            Json out = Json::array();
            for (const auto& e : a)
                out.push_back(propToJson(e, vars));
            return out;
        }
        Json operator()(const demo::Nw2Table& t) const {
            Json entries = Json::array();
            for (const auto& e : t.entries) {
                Json key = e.key;
                if (vars)
                    if (const std::string* name = vars->string(e.key))
                        key = sanitizeUtf8(*name);
                Json value;
                if (auto* f = std::get_if<float>(&e.value))
                    value = number(*f);
                else if (auto* i = std::get_if<std::int32_t>(&e.value))
                    value = *i;
                else if (auto* b = std::get_if<bool>(&e.value))
                    value = *b;
                else if (auto* v3 = std::get_if<demo::Vec3>(&e.value))
                    value = vec(*v3);
                else if (auto* h = std::get_if<std::uint32_t>(&e.value))
                    value = *h;
                else if (auto* s = std::get_if<std::string>(&e.value))
                    value = sanitizeUtf8(*s);
                entries.push_back({{"key", key}, {"type", nw2TypeName(e.type)}, {"value", value}});
            }
            return {{"flag", t.flag}, {"entries", entries}};
        }
    };
    return std::visit(Visitor{networkVars}, v.v);
}

ClassInfo describeClass(const demo::statedb::SchemaClass& c) {
    ClassInfo info;
    info.name = c.name;
    bool player = false, weapon = false, npc = false, physics = false;
    for (std::size_t i = 0; i < c.props.size(); ++i) {
        const auto& p = c.props[i];
        const int idx = static_cast<int>(i);
        const std::string& t = p.table;
        player = player || t == "DT_BasePlayer";
        weapon = weapon || t == "DT_BaseCombatWeapon";
        npc = npc || t == "DT_AI_BaseNPC";
        physics = physics || t == "DT_PhysicsProp" || t == "DT_BreakableProp";
        if (t == "DT_BaseEntity") {
            if (endsWithName(p.name, "m_vecOrigin"))
                info.origin = idx;
            else if (p.name == "m_vecOrigin[2]")
                info.originZ = idx;
            else if (p.name == "m_angRotation")
                info.rotation = idx;
            else if (p.name == "m_nModelIndex")
                info.modelIndex = idx;
            else if (p.name == "m_hMoveParent")
                info.moveParent = idx;
            else if (p.name == "m_iHealth")
                info.health = idx;
            else if (p.name == "m_iTeamNum")
                info.team = idx;
        } else if (p.name == "m_vecOrigin" && t.find("NonLocalPlayerExclusive") != std::string::npos) {
            info.originNonLocal = idx;
        } else if (p.name == "m_vecOrigin" && t.find("LocalPlayerExclusive") != std::string::npos) {
            info.originLocal = idx;
        } else if (p.name == "m_angEyeAngles[0]" && info.eyePitch < 0) {
            info.eyePitch = idx;
        } else if (p.name == "m_angEyeAngles[1]" && info.eyeYaw < 0) {
            info.eyeYaw = idx;
        }
    }
    if (player)
        info.group = "player";
    else if (weapon)
        info.group = "weapon";
    else if (npc)
        info.group = "npc";
    else if (physics || c.name.find("Prop") != std::string::npos)
        info.group = "prop";
    else if (c.name == "CWorld")
        info.group = "world";
    else
        info.group = "other";
    return info;
}

std::optional<PlayerInfo> playerInfo(const demo::statedb::WorldState& s, int entityIndex) {
    constexpr std::size_t kSize = 324, kNameBytes = 128, kFakePlayer = 300;
    const auto* table = s.table("userinfo");
    if (!table || entityIndex < 1)
        return std::nullopt;
    auto it = table->entries.find(entityIndex - 1);
    if (it == table->entries.end() || it->second.second.size() != kSize)
        return std::nullopt;
    const auto& ud = it->second.second;
    std::size_t len = 0;
    while (len < kNameBytes && ud[len] != 0)
        ++len;
    PlayerInfo p;
    p.name = sanitizeUtf8(std::string_view(reinterpret_cast<const char*>(ud.data()), len));
    p.bot = ud[kFakePlayer] != 0;
    return p;
}

std::optional<Placement> placement(const demo::statedb::EntityState& e, const ClassInfo& info,
                                   bool localPlayer) {
    auto vecAt = [&](int idx) -> const demo::Vec3* {
        if (idx < 0 || static_cast<std::size_t>(idx) >= e.props.size())
            return nullptr;
        return std::get_if<demo::Vec3>(&e.props[static_cast<std::size_t>(idx)].v);
    };
    auto floatAt = [&](int idx) -> std::optional<float> {
        if (idx < 0 || static_cast<std::size_t>(idx) >= e.props.size())
            return std::nullopt;
        if (auto* f = std::get_if<float>(&e.props[static_cast<std::size_t>(idx)].v))
            return *f;
        return std::nullopt;
    };
    const demo::Vec3* origin = nullptr;
    if (localPlayer)
        origin = vecAt(info.originLocal);
    if (!origin)
        origin = vecAt(info.originNonLocal);
    if (!origin)
        origin = vecAt(info.originLocal);
    if (!origin)
        origin = vecAt(info.origin);
    if (!origin)
        return std::nullopt;
    Placement p;
    p.origin = *origin;
    if (auto z = floatAt(info.originZ))
        p.origin.z = *z;
    if (auto yaw = floatAt(info.eyeYaw)) {
        p.yaw = *yaw;
        p.pitch = floatAt(info.eyePitch).value_or(0);
    } else if (const demo::Vec3* rot = vecAt(info.rotation)) {
        p.pitch = rot->x;
        p.yaw = rot->y;
    }
    return p;
}

} // namespace gmdr::api
