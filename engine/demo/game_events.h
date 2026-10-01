#pragma once

#include "core/bit_reader.h"
#include "core/error.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace gmdr::demo {

struct GameEventKey {
    std::string name;
    int type = 0; // 1 string, 2 float, 3 long, 4 short, 5 byte, 6 bool
};

struct GameEventDescriptor {
    int id = 0;
    std::string name;
    std::vector<GameEventKey> keys;
};

class GameEventSchema {
public:
    // svc_GameEventList payload.
    Result<void> parseList(BitReader& r, int count);
    // svc_GameEvent payload -> event name and "key=value" summary.
    Result<void> decode(BitReader& r, std::string& name, std::string& summary) const;
    bool empty() const { return byId_.empty(); }

private:
    std::unordered_map<int, GameEventDescriptor> byId_;
};

} // namespace gmdr::demo
