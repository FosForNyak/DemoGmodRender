#include "game_events.hpp"

#include "bitreader.hpp"

#include <cmath>

namespace gmdr::demo {

namespace {
constexpr int kMaxEventBits = 9;
enum : int { kTypeLocal = 0, kTypeString = 1, kTypeFloat = 2, kTypeLong = 3, kTypeShort = 4, kTypeByte = 5, kTypeBool = 6 };
} // namespace

const GameEventField* GameEvent::find(const std::string& key) const {
    for (const auto& f : fields)
        if (f.key == key) return &f;
    return nullptr;
}

std::string GameEvent::get_str(const std::string& key) const {
    const auto* f = find(key);
    return f ? f->str : std::string();
}

int GameEvent::get_int(const std::string& key, int def) const {
    const auto* f = find(key);
    return f && !f->is_string && std::isfinite(f->num) ? static_cast<int>(f->num) : def;
}

bool GameEventDecoder::load_list(const RawBitsMsg& m) {
    BitReader br(m.data.data(), m.data.size(), m.data_bits);
    std::map<int, GameEventDescriptor> list;
    for (int i = 0; i < m.type; ++i) {
        GameEventDescriptor d;
        d.id = static_cast<int>(br.read_ubits(kMaxEventBits));
        d.name = br.read_string(32);
        int type = static_cast<int>(br.read_ubits(3));
        while (type != kTypeLocal && !br.overflowed()) {
            if (type > kTypeBool) return false;
            d.keys.emplace_back(br.read_string(32), type);
            type = static_cast<int>(br.read_ubits(3));
        }
        if (br.overflowed()) return false;
        list[d.id] = std::move(d);
    }
    by_id_ = std::move(list);
    return true;
}

std::optional<GameEvent> GameEventDecoder::decode(const RawBitsMsg& m) const {
    BitReader br(m.data.data(), m.data.size(), m.data_bits);
    const int id = static_cast<int>(br.read_ubits(kMaxEventBits));
    auto it = by_id_.find(id);
    if (it == by_id_.end() || br.overflowed()) return std::nullopt;
    GameEvent ev;
    ev.name = it->second.name;
    ev.fields.reserve(it->second.keys.size());
    for (const auto& [key, type] : it->second.keys) {
        GameEventField f;
        f.key = key;
        switch (type) {
        case kTypeString: f.str = br.read_string(1024); f.is_string = true; break;
        case kTypeFloat: f.num = br.read_float(); break;
        case kTypeLong: f.num = br.read_long(); break;
        case kTypeShort: f.num = br.read_short(); break;
        case kTypeByte: f.num = br.read_byte(); break;
        case kTypeBool: f.num = br.read_bit() ? 1 : 0; break;
        default: break;
        }
        if (!f.is_string) f.str = std::to_string(static_cast<long long>(f.num));
        ev.fields.push_back(std::move(f));
    }
    if (br.overflowed()) return std::nullopt;
    return ev;
}

} // namespace gmdr::demo
