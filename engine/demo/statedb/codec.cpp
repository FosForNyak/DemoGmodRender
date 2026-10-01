#include "demo/statedb/codec.h"

#include "core/limits.h"

namespace gmdr::demo::statedb {

namespace {
enum Tag : std::uint8_t {
    Unset = 0,
    Int = 1,
    Float = 2,
    Double = 3,
    Vector = 4,
    String = 5,
    Array = 6,
    Nw2 = 7
};
}

void Encoder::value(const PropValue& v) {
    switch (v.v.index()) {
    case 0:
        u8(Unset);
        break;
    case 1:
        u8(Int);
        svarint(std::get<std::int64_t>(v.v));
        break;
    case 2:
        u8(Float);
        f32(std::get<float>(v.v));
        break;
    case 3:
        u8(Double);
        f64(std::get<double>(v.v));
        break;
    case 4: {
        const auto& x = std::get<Vec3>(v.v);
        u8(Vector);
        f32(x.x);
        f32(x.y);
        f32(x.z);
        break;
    }
    case 5:
        u8(String);
        string(std::get<std::string>(v.v));
        break;
    case 6: {
        const auto& a = std::get<PropValue::Array>(v.v);
        u8(Array);
        varint(a.size());
        for (const auto& e : a)
            value(e);
        break;
    }
    case 7: {
        const auto& t = std::get<Nw2Table>(v.v);
        u8(Nw2);
        u8(t.flag ? 1 : 0);
        varint(t.entries.size());
        for (const auto& e : t.entries) {
            varint(e.key);
            u8(static_cast<std::uint8_t>(e.type));
            switch (e.value.index()) {
            case 1:
                f32(std::get<float>(e.value));
                break;
            case 2:
                svarint(std::get<std::int32_t>(e.value));
                break;
            case 3:
                u8(std::get<bool>(e.value) ? 1 : 0);
                break;
            case 4: {
                const auto& x = std::get<Vec3>(e.value);
                f32(x.x);
                f32(x.y);
                f32(x.z);
                break;
            }
            case 5:
                varint(std::get<std::uint32_t>(e.value));
                break;
            case 6:
                string(std::get<std::string>(e.value));
                break;
            default:
                break;
            }
        }
        break;
    }
    }
}

bool Decoder::value(PropValue& out, int depth) {
    if (depth > 2)
        return ok_ = false;
    const std::uint8_t tag = u8();
    switch (tag) {
    case Unset:
        out.v = std::monostate{};
        break;
    case Int:
        out.v = svarint();
        break;
    case Float:
        out.v = f32();
        break;
    case Double:
        out.v = f64();
        break;
    case Vector: {
        Vec3 x;
        x.x = f32();
        x.y = f32();
        x.z = f32();
        out.v = x;
        break;
    }
    case String:
        out.v = string(4096);
        break;
    case Array: {
        const std::uint64_t n = varint();
        if (n > limits::kMaxArrayElements)
            return ok_ = false;
        PropValue::Array a(static_cast<std::size_t>(n));
        for (auto& e : a)
            if (!value(e, depth + 1))
                return false;
        out.v = std::move(a);
        break;
    }
    case Nw2: {
        Nw2Table t;
        t.flag = u8() != 0;
        const std::uint64_t n = varint();
        if (n > limits::kMaxNw2Entries)
            return ok_ = false;
        t.entries.resize(static_cast<std::size_t>(n));
        for (auto& e : t.entries) {
            e.key = static_cast<std::uint16_t>(varint());
            e.type = static_cast<Nw2Type>(u8());
            switch (e.type) {
            case Nw2Type::Nil:
                break;
            case Nw2Type::Float:
                e.value = f32();
                break;
            case Nw2Type::Int:
                e.value = static_cast<std::int32_t>(svarint());
                break;
            case Nw2Type::Bool:
                e.value = u8() != 0;
                break;
            case Nw2Type::Vector:
            case Nw2Type::Angle: {
                Vec3 x;
                x.x = f32();
                x.y = f32();
                x.z = f32();
                e.value = x;
                break;
            }
            case Nw2Type::Entity:
                e.value = static_cast<std::uint32_t>(varint());
                break;
            case Nw2Type::String:
                e.value = string(4096);
                break;
            default:
                return ok_ = false;
            }
        }
        out.v = std::move(t);
        break;
    }
    default:
        return ok_ = false;
    }
    return ok_;
}

} // namespace gmdr::demo::statedb
