// =============================================================================
//  string_tables.hpp — "таблиці рядків" рушія Source.
//
//  Нам потрібна лише таблиця "userinfo": у ній для кожного слота гравця
//  лежить структура player_info_t (ім'я, SteamID...). Завдяки цьому ми
//  можемо підписати голосові доріжки іменами гравців.
//
//  Таблиці створюються повідомленням svc_CreateStringTable, змінюються
//  svc_UpdateStringTable, а на початку демо є повний знімок (dem_stringtables).
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "netmessages.hpp"

namespace gmdr::demo {

struct PlayerInfo {
    int         slot = -1;
    std::string name;
    int         userid = -1;   // номер гравця на сервері (у подіях player_*)
    std::string guid;          // "STEAM_0:1:12345"
    uint64_t    steamid64 = 0;
    bool        fake_player = false;
};

struct StringTableEntry {
    std::string          key;
    std::vector<uint8_t> user_data;
    bool                 present = false;
};

struct StringTable {
    std::string                   name;
    int                           max_entries = 0;
    bool                          user_data_fixed_size = false;
    int                           user_data_size = 0;
    int                           user_data_size_bits = 0;
    std::vector<StringTableEntry> entries;   // розмір max_entries
};

class StringTableSet {
public:
    // Повертають false при помилці розбору (текст — у last_error()).
    bool on_create(const CreateStringTableMsg& msg);
    bool on_update(const UpdateStringTableMsg& msg);
    bool on_snapshot(const uint8_t* data, size_t size);   // dem_stringtables

    const StringTable* find(const std::string& name) const;
    const std::string& last_error() const { return error_; }

    // Список гравців з таблиці userinfo (слот -> інформація).
    std::map<int, PlayerInfo> players() const;

private:
    bool apply_update(StringTable& t, const uint8_t* data, size_t bits, int num_entries);
    bool apply_update_bits(StringTable& t, const uint8_t* data, size_t bits, int num_entries, int ud_len_bits);

    std::vector<StringTable> tables_;
    std::string              error_;
    int                      ud_len_bits_ = 19;
    bool                     snapshot_truncated_ = false;

public:
    // Знімок таблиць у демо обмежений 512 КБ, і в GMod він часто обрізаний на
    // величезній таблиці client_lua_files. Це не помилка: потрібні нам таблиці
    // (userinfo) ідуть раніше.
    bool snapshot_truncated() const { return snapshot_truncated_; }
};

// Розбір структури player_info_t (стійкий до різних розмірів полів).
std::optional<PlayerInfo> parse_player_info(const std::vector<uint8_t>& data);

// Розпакування формату Valve LZSS ("LZSS" + розмір + дані).
std::optional<std::vector<uint8_t>> lzss_decompress(const uint8_t* data, size_t size);

// SteamID64 з рядка "STEAM_X:Y:Z" (0 — якщо не вдалося).
uint64_t steamid64_from_guid(const std::string& guid);

} // namespace gmdr::demo
