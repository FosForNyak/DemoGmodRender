// =============================================================================
//  netmessages.hpp — розбір мережевих повідомлень Garry's Mod з демо-пакетів.
//
//  Кожен dem_packet містить послідовність повідомлень:
//      [6 біт — тип] [дані повідомлення, формат залежить від типу] ...
//  Довжини у більшості повідомлень немає, тому щоб дістатися до потрібного
//  (наприклад svc_VoiceData — голос гравців), треба КОРЕКТНО розібрати
//  (або пропустити) всі попередні повідомлення.
//
//  Формат узято з відкритих реалізацій протоколу GMod (gm_sourcenet,
//  leysourceengineclient) і перевірено автоматично: для кожного демо ми
//  пробуємо кілька варіантів протоколу і вибираємо той, з яким усі пакети
//  розбираються рівно до кінця (див. detect_protocol_variant).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gmdr::demo {

enum NetMsgType : int {
    net_NOP = 0,
    net_Disconnect = 1,
    net_File = 2,
    net_Tick = 3,
    net_StringCmd = 4,
    net_SetConVar = 5,
    net_SignonState = 6,
    svc_Print = 7,
    svc_ServerInfo = 8,
    svc_SendTable = 9,
    svc_ClassInfo = 10,
    svc_SetPause = 11,
    svc_CreateStringTable = 12,
    svc_UpdateStringTable = 13,
    svc_VoiceInit = 14,
    svc_VoiceData = 15,
    // 16 не використовується
    svc_Sounds = 17,
    svc_SetView = 18,
    svc_FixAngle = 19,
    svc_CrosshairAngle = 20,
    svc_BSPDecal = 21,
    // 22 не використовується (TerrainMod)
    svc_UserMessage = 23,
    svc_EntityMessage = 24,
    svc_GameEvent = 25,
    svc_PacketEntities = 26,
    svc_TempEntities = 27,
    svc_Prefetch = 28,
    svc_Menu = 29,
    svc_GameEventList = 30,
    svc_GetCvarValue = 31,
    svc_CmdKeyValues = 32,
    svc_GMod_ServerToClient = 33,   // net.* повідомлення Lua у GMod
};

const char* net_message_name(int type);

// Параметри протоколу, які відрізняються між версіями гри/джерелами.
struct ProtocolVariant {
    int  edict_bits = 13;                // GMod: 8192 сутностей
    int  packet_entities_len_bits = 24;  // довжина svc_PacketEntities (24 у GMod, 20 у ваніллі)
    bool voice_init_sample_rate = false; // svc_VoiceInit: +16 біт, якщо quality == 255
    bool serverinfo_gmod_strings = true; // svc_ServerInfo: + loading URL + gamemode
    // Сервери GMod 2026 року (білд 10000+): svc_ServerInfo має ще 16 біт у кінці,
    // а svc_CreateStringTable передає максимальний розмір таблиці як log2 у 5 бітах
    // замість 16-бітного числа. Мережевий протокол у заголовку демо лишився 24.
    bool gmod_2026 = false;

    std::string describe() const;
    bool operator==(const ProtocolVariant&) const = default;
};

std::vector<ProtocolVariant> candidate_variants();

// ---- Розібрані повідомлення, які нам цікаві --------------------------------
struct ServerInfoMsg {
    int         protocol = 0;
    int         server_count = 0;
    bool        is_hltv = false;
    bool        is_dedicated = false;
    int         max_classes = 0;
    int         player_slot = -1;      // слот гравця, що записував демо
    int         max_clients = 0;
    float       tick_interval = 0.0f;  // секунд на тік (1/66 ≈ 0.01515)
    char        os = '?';
    std::string game_dir, map_name, sky_name, host_name, loading_url, gamemode;
};

struct CreateStringTableMsg {
    std::string          name;
    int                  max_entries = 0;
    int                  num_entries = 0;
    bool                 user_data_fixed_size = false;
    int                  user_data_size = 0;
    int                  user_data_size_bits = 0;
    bool                 compressed = false;
    std::vector<uint8_t> data;
    size_t               data_bits = 0;
};

struct UpdateStringTableMsg {
    int                  table_id = 0;
    int                  changed_entries = 1;
    std::vector<uint8_t> data;
    size_t               data_bits = 0;
};

struct VoiceInitMsg {
    std::string codec;
    int         quality = 0;
    int         sample_rate = 0;
};

struct VoiceDataMsg {
    int                  client = -1;     // слот гравця-мовця
    bool                 proximity = false;
    std::vector<uint8_t> data;            // упакований голос (формат Steam Voice)
    size_t               data_bits = 0;
};

// Обробник: перевизначте лише потрібні методи.
class NetHandler {
public:
    virtual ~NetHandler() = default;
    virtual bool wants_string_tables() const { return false; }
    virtual bool wants_voice() const { return false; }
    virtual void on_tick(int32_t /*tick*/) {}
    virtual void on_server_info(const ServerInfoMsg&) {}
    virtual void on_create_string_table(const CreateStringTableMsg&) {}
    virtual void on_update_string_table(const UpdateStringTableMsg&) {}
    virtual void on_voice_init(const VoiceInitMsg&) {}
    virtual void on_voice_data(const VoiceDataMsg&) {}
    virtual void on_print(const std::string&) {}
    virtual void on_set_convar(const std::string& /*name*/, const std::string& /*value*/) {}
};

struct PacketParseResult {
    bool        ok = true;
    int         messages = 0;
    int         fail_type = -1;     // тип повідомлення, на якому сталася помилка
    size_t      fail_bit = 0;
    std::string error;
    uint32_t    type_mask = 0;      // які типи зустрілися (біт = тип, до 33)
    uint64_t    type_mask_hi = 0;
};

// Розібрати всі повідомлення з пакета. handler може бути nullptr (лише перевірка).
PacketParseResult parse_packet(const uint8_t* data, size_t size, const ProtocolVariant& variant,
                               NetHandler* handler);

// floor(log2(v)) як у Source (Q_log2)
int q_log2(int v);

} // namespace gmdr::demo
