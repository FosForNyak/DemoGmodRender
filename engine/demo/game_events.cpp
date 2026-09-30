#include "demo/game_events.h"

#include "core/limits.h"

#include <cstdio>

namespace gmdr::demo {

Result<void> GameEventSchema::parseList(BitReader& r, int count) {
    if (count < 0 || static_cast<std::size_t>(count) > limits::kMaxGameEventDescriptors)
        return makeError("demo.gameevents_limit", "too many game event descriptors");
    byId_.clear();
    for (int i = 0; i < count; ++i) {
        GameEventDescriptor d;
        d.id = static_cast<int>(r.ubit(9));
        d.name = r.string(limits::kMaxStringBytes);
        while (true) {
            const int type = static_cast<int>(r.ubit(3));
            if (type == 0 || r.overflowed())
                break;
            if (d.keys.size() >= limits::kMaxGameEventKeys)
                return makeError("demo.gameevents_limit", "too many keys in a game event", d.name);
            GameEventKey k;
            k.type = type;
            k.name = r.string(limits::kMaxStringBytes);
            d.keys.push_back(std::move(k));
        }
        if (r.overflowed())
            return makeError("demo.gameevents_truncated", "game event list ends early");
        byId_[d.id] = std::move(d);
    }
    return {};
}

Result<void> GameEventSchema::decode(BitReader& r, std::string& name, std::string& summary) const {
    const int id = static_cast<int>(r.ubit(9));
    auto it = byId_.find(id);
    if (it == byId_.end())
        return makeError("demo.gameevent_unknown", "game event id is not in the list", std::to_string(id));
    const auto& d = it->second;
    name = d.name;
    summary.clear();
    char buf[64];
    for (const auto& k : d.keys) {
        if (!summary.empty())
            summary += ' ';
        summary += k.name;
        summary += '=';
        switch (k.type) {
        case 1:
            summary += r.string(256);
            break;
        case 2:
            std::snprintf(buf, sizeof buf, "%g", static_cast<double>(r.float32()));
            summary += buf;
            break;
        case 3:
            summary += std::to_string(r.sbit(32));
            break;
        case 4:
            summary += std::to_string(r.sbit(16));
            break;
        case 5:
            summary += std::to_string(r.ubit(8));
            break;
        case 6:
            summary += r.bit() ? "1" : "0";
            break;
        default:
            return makeError("demo.gameevent_key_type", "unknown game event key type", d.name);
        }
        if (summary.size() > 512) {
            summary.resize(512);
            break;
        }
    }
    if (r.overflowed())
        return makeError("demo.gameevent_truncated", "game event ends early", d.name);
    return {};
}

} // namespace gmdr::demo
