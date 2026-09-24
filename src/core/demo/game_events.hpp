// =============================================================================
//  game_events.hpp — ігрові події Source (svc_GameEventList / svc_GameEvent).
//
//  Опис подій сервер надсилає на початку (svc_GameEventList): для кожної події
//  номер (9 біт), назва і список полів із типами (3 біти: 1 рядок, 2 float,
//  3 long, 4 short, 5 byte, 6 bool; 0 — кінець списку). Далі кожна подія —
//  номер і значення полів у тому ж порядку. Тож розбір не залежить від гри:
//  назви і поля беруться з самого демо.
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "netmessages.hpp"

namespace gmdr::demo {

struct GameEventDescriptor {
    int                                      id = -1;
    std::string                              name;
    std::vector<std::pair<std::string, int>> keys;   // назва поля, тип
};

struct GameEventField {
    std::string key;
    std::string str;       // для рядків
    double      num = 0;   // для чисел і bool
    bool        is_string = false;
};

struct GameEvent {
    std::string                 name;
    std::vector<GameEventField> fields;

    const GameEventField* find(const std::string& key) const;
    std::string get_str(const std::string& key) const;
    int         get_int(const std::string& key, int def = -1) const;
};

class GameEventDecoder {
public:
    // svc_GameEventList (m.type — кількість подій). Повертає false, якщо список пошкоджений.
    bool load_list(const RawBitsMsg& m);
    std::optional<GameEvent> decode(const RawBitsMsg& m) const;
    bool empty() const { return by_id_.empty(); }

private:
    std::map<int, GameEventDescriptor> by_id_;
};

} // namespace gmdr::demo
