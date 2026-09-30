#include "demo/props.h"

#include "core/limits.h"

#include <cmath>
#include <string>

namespace gmdr::demo {

namespace {

constexpr int kCoordIntegerBits = 14;
constexpr int kCoordIntegerBitsMp = 11;
constexpr int kCoordFractionalBits = 5;
constexpr int kCoordFractionalBitsMpLow = 3;
constexpr float kCoordResolution = 1.0f / 32.0f;
constexpr float kCoordResolutionLow = 1.0f / 8.0f;
constexpr int kNormalFractionalBits = 11;
constexpr float kNormalResolution = 1.0f / ((1 << kNormalFractionalBits) - 1);
constexpr int kStringLengthBits = 9;

float decodeFloat(BitReader& r, const SendProp& p) {
    using namespace prop_flags;
    if (p.flags & Coord)
        return readBitCoord(r);
    if (p.flags & (CoordMp | CoordMpLowPrecision | CoordMpIntegral))
        return readBitCoordMp(r, (p.flags & CoordMpIntegral) != 0, (p.flags & CoordMpLowPrecision) != 0);
    if (p.flags & NoScale)
        return r.float32();
    if (p.flags & Normal)
        return readBitNormal(r);
    if (p.bits <= 0 || p.bits > 32)
        return p.low;
    const std::uint32_t raw = r.ubit(p.bits);
    const double maxv = (p.bits == 32) ? 4294967295.0 : static_cast<double>((1ull << p.bits) - 1);
    return static_cast<float>(p.low + (static_cast<double>(p.high) - p.low) * (raw / maxv));
}

Result<void> decodeNw2(BitReader& r, Nw2Table& out) {
    const std::uint32_t header = r.ubit(13);
    out.flag = (header >> 12) != 0;
    const std::uint32_t count = header & 0xFFF;
    out.entries.clear();
    out.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Nw2Entry e;
        e.key = static_cast<std::uint16_t>(r.ubit(12));
        e.type = static_cast<Nw2Type>(r.ubit(3));
        switch (e.type) {
        case Nw2Type::Nil:
            break;
        case Nw2Type::Float:
            e.value = r.float32();
            break;
        case Nw2Type::Int:
            e.value = r.sbit(32);
            break;
        case Nw2Type::Bool:
            e.value = r.bit();
            break;
        case Nw2Type::Vector:
        case Nw2Type::Angle: {
            Vec3 v;
            v.x = r.float32();
            v.y = r.float32();
            v.z = r.float32();
            e.value = v;
            break;
        }
        case Nw2Type::Entity:
            e.value = r.ubit(23);
            break;
        case Nw2Type::String: {
            const std::size_t len = r.ubit(kStringLengthBits);
            std::string s(len, '\0');
            r.bytes(reinterpret_cast<std::uint8_t*>(s.data()), len);
            e.value = std::move(s);
            break;
        }
        }
        if (r.overflowed())
            return makeError("demo.nw2_truncated", "NW2 table ends early");
        out.entries.push_back(std::move(e));
    }
    return {};
}

Result<void> decodeScalar(BitReader& r, const SendProp& p, PropValue& out) {
    using namespace prop_flags;
    switch (p.type) {
    case PropType::Int: {
        if (p.flags & Normal) { // GMod: varint integer
            const std::uint32_t v = r.varint32();
            out.v = (p.flags & Unsigned) ? static_cast<std::int64_t>(v)
                                         : static_cast<std::int64_t>(BitReader::zigzag32(v));
        } else if (p.bits > 32) {
            const std::uint64_t v = r.ubit64(p.bits);
            out.v = static_cast<std::int64_t>(v);
        } else if (p.flags & Unsigned) {
            out.v = static_cast<std::int64_t>(r.ubit(p.bits));
        } else {
            out.v = static_cast<std::int64_t>(r.sbit(p.bits));
        }
        return {};
    }
    case PropType::Float:
        out.v = decodeFloat(r, p);
        return {};
    case PropType::Vector: {
        Vec3 v;
        v.x = decodeFloat(r, p);
        v.y = decodeFloat(r, p);
        if (p.flags & Normal) {
            const bool sign = r.bit();
            const float s = v.x * v.x + v.y * v.y;
            v.z = s < 1.0f ? std::sqrt(1.0f - s) : 0.0f;
            if (sign)
                v.z = -v.z;
        } else {
            v.z = decodeFloat(r, p);
        }
        out.v = v;
        return {};
    }
    case PropType::Time:
        out.v = r.float64();
        return {};
    case PropType::String: {
        const std::size_t len = r.ubit(kStringLengthBits);
        std::string s(len, '\0');
        r.bytes(reinterpret_cast<std::uint8_t*>(s.data()), len);
        out.v = std::move(s);
        return {};
    }
    case PropType::GModTable: {
        Nw2Table t;
        GMDR_TRY(decodeNw2(r, t));
        out.v = std::move(t);
        return {};
    }
    case PropType::Array:
    case PropType::DataTable:
        break;
    }
    return makeError("demo.prop_type", "unexpected property type in a value");
}

} // namespace

float readBitCoord(BitReader& r) {
    const bool hasInt = r.bit();
    const bool hasFract = r.bit();
    float value = 0;
    if (hasInt || hasFract) {
        const bool sign = r.bit();
        std::uint32_t intval = 0, fractval = 0;
        if (hasInt)
            intval = r.ubit(kCoordIntegerBits) + 1;
        if (hasFract)
            fractval = r.ubit(kCoordFractionalBits);
        value = static_cast<float>(intval) + static_cast<float>(fractval) * kCoordResolution;
        if (sign)
            value = -value;
    }
    return value;
}

float readBitCoordMp(BitReader& r, bool integral, bool lowPrecision) {
    const bool inBounds = r.bit();
    float value = 0;
    bool sign = false;
    if (integral) {
        if (r.bit()) {
            sign = r.bit();
            value = static_cast<float>(r.ubit(inBounds ? kCoordIntegerBitsMp : kCoordIntegerBits) + 1);
        }
    } else {
        const bool hasInt = r.bit();
        sign = r.bit();
        std::uint32_t intval = 0;
        if (hasInt)
            intval = r.ubit(inBounds ? kCoordIntegerBitsMp : kCoordIntegerBits) + 1;
        const std::uint32_t fractval = r.ubit(lowPrecision ? kCoordFractionalBitsMpLow : kCoordFractionalBits);
        value = static_cast<float>(intval) +
                static_cast<float>(fractval) * (lowPrecision ? kCoordResolutionLow : kCoordResolution);
    }
    return sign ? -value : value;
}

float readBitNormal(BitReader& r) {
    const bool sign = r.bit();
    const float v = static_cast<float>(r.ubit(kNormalFractionalBits)) * kNormalResolution;
    return sign ? -v : v;
}

Result<void> decodeProp(BitReader& r, const FlatProp& fp, PropValue& out) {
    const SendProp& p = *fp.prop;
    if (p.type != PropType::Array)
        return decodeScalar(r, p, out);
    int countBits = 0;
    for (int n = p.elements; n > 1; n >>= 1)
        ++countBits;
    const std::uint32_t count = r.ubit(countBits + 1);
    if (count > limits::kMaxArrayElements || static_cast<int>(count) > p.elements)
        return makeError("demo.prop_array", "array has more elements than declared", p.name);
    PropValue::Array arr(count);
    for (auto& el : arr)
        GMDR_TRY(decodeScalar(r, *fp.element, el));
    out.v = std::move(arr);
    return {};
}

Result<void> readPropList(BitReader& r, const std::vector<FlatProp>& flat, std::vector<PropValue>& state,
                          std::vector<int>& changed) {
    int index = -1;
    while (r.bit()) {
        index += 1 + static_cast<int>(r.ubitVar());
        if (r.overflowed())
            return makeError("demo.props_truncated", "property list ends early");
        if (index < 0 || static_cast<std::size_t>(index) >= flat.size())
            return makeError("demo.prop_index", "property index out of range",
                             std::to_string(index) + " of " + std::to_string(flat.size()));
        GMDR_TRY(decodeProp(r, flat[static_cast<std::size_t>(index)], state[static_cast<std::size_t>(index)]));
        changed.push_back(index);
    }
    if (r.overflowed())
        return makeError("demo.props_truncated", "property list ends early");
    return {};
}

} // namespace gmdr::demo
