#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gmdr::demo {

struct Vec2 {
    float x = 0, y = 0;
    bool operator==(const Vec2&) const = default;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};

// SendProp types as numbered in GMod's SendTables. Type 3 is a 64-bit double time in GMod (VectorXY in the
// Source SDK), type 7 is GMod's NW2 variable table carried inside entities.
enum class PropType : std::uint8_t {
    Int = 0,
    Float = 1,
    Vector = 2,
    Time = 3,
    String = 4,
    Array = 5,
    DataTable = 6,
    GModTable = 7,
};

namespace prop_flags {
inline constexpr std::uint32_t Unsigned = 1u << 0;
inline constexpr std::uint32_t Coord = 1u << 1;
inline constexpr std::uint32_t NoScale = 1u << 2;
inline constexpr std::uint32_t RoundDown = 1u << 3;
inline constexpr std::uint32_t RoundUp = 1u << 4;
inline constexpr std::uint32_t Normal = 1u << 5;   // also "varint" on integer props in GMod
inline constexpr std::uint32_t Exclude = 1u << 6;
inline constexpr std::uint32_t Xyze = 1u << 7;
inline constexpr std::uint32_t InsideArray = 1u << 8;
inline constexpr std::uint32_t ProxyAlwaysYes = 1u << 9;
inline constexpr std::uint32_t ChangesOften = 1u << 10;
inline constexpr std::uint32_t IsVectorElem = 1u << 11;
inline constexpr std::uint32_t Collapsible = 1u << 12;
inline constexpr std::uint32_t CoordMp = 1u << 13;
inline constexpr std::uint32_t CoordMpLowPrecision = 1u << 14;
inline constexpr std::uint32_t CoordMpIntegral = 1u << 15;
} // namespace prop_flags

// NW2 value types in GMod's m_GMOD_DataTable.
enum class Nw2Type : std::uint8_t {
    Nil = 0,
    Float = 1,
    Int = 2,
    Bool = 3,
    Vector = 4,
    Angle = 5, // assumed (not seen in the corpus yet)
    Entity = 6,
    String = 7,
};

struct Nw2Entry {
    std::uint16_t key = 0; // index into the "networkvars" string table
    Nw2Type type = Nw2Type::Nil;
    std::variant<std::monostate, float, std::int32_t, bool, Vec3, std::uint32_t, std::string> value;
    bool operator==(const Nw2Entry&) const = default;
};

struct Nw2Table {
    bool flag = false; // top bit of the 13-bit count field; observed with zero entries
    std::vector<Nw2Entry> entries;
    bool operator==(const Nw2Table&) const = default;
};

// A decoded property value. Integers are widened to int64 (EHANDLEs stay as raw 23-bit values).
struct PropValue {
    using Array = std::vector<PropValue>;
    std::variant<std::monostate, std::int64_t, float, double, Vec3, std::string, Array, Nw2Table> v;

    bool isSet() const { return v.index() != 0; }
    bool operator==(const PropValue&) const = default;
};

} // namespace gmdr::demo
