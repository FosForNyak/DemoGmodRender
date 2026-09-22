#include "netmessages.hpp"

#include "bitreader.hpp"

#include <format>

namespace gmdr::demo {

const char* net_message_name(int type) {
    switch (type) {
    case net_NOP: return "net_NOP";
    case net_Disconnect: return "net_Disconnect";
    case net_File: return "net_File";
    case net_Tick: return "net_Tick";
    case net_StringCmd: return "net_StringCmd";
    case net_SetConVar: return "net_SetConVar";
    case net_SignonState: return "net_SignonState";
    case svc_Print: return "svc_Print";
    case svc_ServerInfo: return "svc_ServerInfo";
    case svc_SendTable: return "svc_SendTable";
    case svc_ClassInfo: return "svc_ClassInfo";
    case svc_SetPause: return "svc_SetPause";
    case svc_CreateStringTable: return "svc_CreateStringTable";
    case svc_UpdateStringTable: return "svc_UpdateStringTable";
    case svc_VoiceInit: return "svc_VoiceInit";
    case svc_VoiceData: return "svc_VoiceData";
    case svc_Sounds: return "svc_Sounds";
    case svc_SetView: return "svc_SetView";
    case svc_FixAngle: return "svc_FixAngle";
    case svc_CrosshairAngle: return "svc_CrosshairAngle";
    case svc_BSPDecal: return "svc_BSPDecal";
    case svc_UserMessage: return "svc_UserMessage";
    case svc_EntityMessage: return "svc_EntityMessage";
    case svc_GameEvent: return "svc_GameEvent";
    case svc_PacketEntities: return "svc_PacketEntities";
    case svc_TempEntities: return "svc_TempEntities";
    case svc_Prefetch: return "svc_Prefetch";
    case svc_Menu: return "svc_Menu";
    case svc_GameEventList: return "svc_GameEventList";
    case svc_GetCvarValue: return "svc_GetCvarValue";
    case svc_CmdKeyValues: return "svc_CmdKeyValues";
    case svc_GMod_ServerToClient: return "svc_GMod_ServerToClient";
    default: return "невідоме";
    }
}

std::string ProtocolVariant::describe() const {
    return std::format("edict={} PE_len={} voiceinit_rate={} gmod_serverinfo={} gmod2026={}", edict_bits,
                       packet_entities_len_bits, voice_init_sample_rate ? 1 : 0, serverinfo_gmod_strings ? 1 : 0,
                       gmod_2026 ? 1 : 0);
}

std::vector<ProtocolVariant> candidate_variants() {
    std::vector<ProtocolVariant> out;
    for (bool g26 : {false, true}) {
        for (int pe : {24, 20}) {
            for (bool vr : {false, true}) {
                ProtocolVariant v;
                v.packet_entities_len_bits = pe;
                v.voice_init_sample_rate = vr;
                v.gmod_2026 = g26;
                out.push_back(v);
            }
        }
    }
    return out;
}

int q_log2(int v) {
    int answer = 0;
    while (v >>= 1) ++answer;
    return answer;
}

namespace {
constexpr int kNetMsgTypeBits = 6;
constexpr int kMaxTablesBits = 5;     // log2(32)
constexpr int kMaxUserMessageBits = 11;
constexpr int kMaxEntityMessageBits = 11;
constexpr int kMaxServerClassBits = 9;
constexpr int kMaxDecalIndexBits = 9;
constexpr int kModelIndexBits = 12;   // GMod (у ваніллі 11)
constexpr int kMaxSoundIndexBits = 14;

bool skip_block(BitReader& br, size_t bits) { return br.skip_bits(bits); }
} // namespace

PacketParseResult parse_packet(const uint8_t* data, size_t size, const ProtocolVariant& v, NetHandler* h) {
    PacketParseResult res;
    BitReader br(data, size);
    const bool want_voice = h && h->wants_voice();
    const bool want_tables = h && h->wants_string_tables();

    auto fail = [&](int type, const std::string& why) {
        res.ok = false;
        res.fail_type = type;
        res.fail_bit = br.position();
        res.error = why;
        return res;
    };

    while (br.bits_left() >= kNetMsgTypeBits) {
        const size_t msg_start = br.position();
        const int type = static_cast<int>(br.read_ubits(kNetMsgTypeBits));
        if (type < 64) {
            if (type < 32) res.type_mask |= (1u << type);
            else res.type_mask_hi |= (1ull << (type - 32));
        }
        switch (type) {
        case net_NOP:
            // Нулі в кінці пакета — це вирівнювання. Якщо далі лише нулі, завершуємо.
            if (br.rest_is_zero()) return res;
            break;
        case net_Disconnect:
            br.read_string();
            break;
        case net_File: {
            br.read_ubits(32);                 // transfer id
            const bool requested = br.read_bit();
            if (requested) {
                br.read_ubits(1);              // request type
                br.read_ubits(32);             // file id
            }
            break;
        }
        case net_Tick: {
            const int32_t tick = br.read_long();
            br.read_ubits(16);   // host frametime
            br.read_ubits(16);   // host frametime std deviation
            if (h && !br.overflowed()) h->on_tick(tick);
            break;
        }
        case net_StringCmd:
            br.read_string();
            break;
        case net_SetConVar: {
            const int count = br.read_byte();
            for (int i = 0; i < count && !br.overflowed(); ++i) {
                std::string name = br.read_string(1024);
                std::string value = br.read_string(4096);
                if (h && !br.overflowed()) h->on_set_convar(name, value);
            }
            break;
        }
        case net_SignonState:
            br.read_byte();
            br.read_long();
            break;
        case svc_Print: {
            std::string s = br.read_string();
            if (h && !br.overflowed()) h->on_print(s);
            break;
        }
        case svc_ServerInfo: {
            ServerInfoMsg si;
            si.protocol = br.read_short();
            si.server_count = br.read_long();
            si.is_hltv = br.read_bit();
            si.is_dedicated = br.read_bit();
            br.read_long();                    // client.dll CRC
            si.max_classes = br.read_word();
            br.skip_bits(16 * 8);              // MD5 карти
            si.player_slot = br.read_byte();
            si.max_clients = br.read_byte();
            si.tick_interval = br.read_float();
            si.os = static_cast<char>(br.read_byte());
            si.game_dir = br.read_string(260);
            si.map_name = br.read_string(260);
            si.sky_name = br.read_string(260);
            si.host_name = br.read_string(260);
            if (v.serverinfo_gmod_strings) {
                si.loading_url = br.read_string(1024);
                si.gamemode = br.read_string(260);
            }
            if (v.gmod_2026) br.read_ubits(16);   // нове поле (у відомих демо завжди 0xFFFF)
            if (br.overflowed()) return fail(type, "svc_ServerInfo обрізано");
            if (!(si.tick_interval > 0.0001f && si.tick_interval < 1.0f))
                return fail(type, "svc_ServerInfo: неправдоподібний tick interval");
            if (h) h->on_server_info(si);
            break;
        }
        case svc_SendTable: {
            br.read_bit();   // needs decoder
            const uint32_t bits = br.read_ubits(16);
            skip_block(br, bits);
            break;
        }
        case svc_ClassInfo: {
            const int num = br.read_word();
            const bool create_on_client = br.read_bit();
            if (!create_on_client) {
                const int bits = q_log2(num) + 1;
                for (int i = 0; i < num && !br.overflowed(); ++i) {
                    br.read_ubits(bits);
                    br.read_string(256);
                    br.read_string(256);
                }
            }
            break;
        }
        case svc_SetPause:
            br.read_bit();
            break;
        case svc_CreateStringTable: {
            CreateStringTableMsg m;
            m.name = br.read_string(256);
            if (v.gmod_2026) {
                const uint32_t log2_max = br.read_ubits(5);
                if (log2_max > 16) return fail(type, "svc_CreateStringTable: неправдоподібний розмір таблиці");
                m.max_entries = 1 << log2_max;
            } else {
                m.max_entries = br.read_word();
            }
            if (m.max_entries <= 0) return fail(type, "svc_CreateStringTable: max_entries = 0");
            m.num_entries = static_cast<int>(br.read_ubits(q_log2(m.max_entries) + 1));
            const uint32_t bits = br.read_varint32();
            m.user_data_fixed_size = br.read_bit();
            if (m.user_data_fixed_size) {
                m.user_data_size = static_cast<int>(br.read_ubits(12));
                m.user_data_size_bits = static_cast<int>(br.read_ubits(4));
            }
            m.compressed = br.read_bit();
            if (br.overflowed() || bits > br.bits_left()) return fail(type, "svc_CreateStringTable обрізано");
            if (want_tables) {
                m.data_bits = bits;
                m.data = br.read_bits_to_bytes(bits);
                h->on_create_string_table(m);
            } else {
                skip_block(br, bits);
            }
            break;
        }
        case svc_UpdateStringTable: {
            UpdateStringTableMsg m;
            m.table_id = static_cast<int>(br.read_ubits(kMaxTablesBits));
            m.changed_entries = br.read_bit() ? br.read_word() : 1;
            const uint32_t bits = br.read_ubits(20);
            if (br.overflowed() || bits > br.bits_left()) return fail(type, "svc_UpdateStringTable обрізано");
            if (want_tables) {
                m.data_bits = bits;
                m.data = br.read_bits_to_bytes(bits);
                h->on_update_string_table(m);
            } else {
                skip_block(br, bits);
            }
            break;
        }
        case svc_VoiceInit: {
            VoiceInitMsg m;
            m.codec = br.read_string(256);
            m.quality = br.read_byte();
            if (v.voice_init_sample_rate && m.quality == 255) m.sample_rate = br.read_word();
            if (h && !br.overflowed()) h->on_voice_init(m);
            break;
        }
        case svc_VoiceData: {
            VoiceDataMsg m;
            m.client = br.read_byte();
            m.proximity = br.read_byte() != 0;
            const uint32_t bits = br.read_word();
            if (br.overflowed() || bits > br.bits_left()) return fail(type, "svc_VoiceData обрізано");
            if (want_voice) {
                m.data_bits = bits;
                m.data = br.read_bits_to_bytes(bits);
                h->on_voice_data(m);
            } else {
                skip_block(br, bits);
            }
            break;
        }
        case svc_Sounds: {
            const bool reliable = br.read_bit();
            uint32_t bits;
            if (reliable) {
                bits = br.read_ubits(8);
            } else {
                br.read_ubits(8);   // кількість звуків
                bits = br.read_ubits(16);
            }
            skip_block(br, bits);
            break;
        }
        case svc_SetView:
            br.read_ubits(v.edict_bits);
            break;
        case svc_FixAngle:
            br.read_bit();
            br.read_ubits(16);
            br.read_ubits(16);
            br.read_ubits(16);
            break;
        case svc_CrosshairAngle:
            br.read_ubits(16);
            br.read_ubits(16);
            br.read_ubits(16);
            break;
        case svc_BSPDecal: {
            float pos[3];
            br.read_bit_vec3_coord(pos);
            br.read_ubits(kMaxDecalIndexBits);
            if (br.read_bit()) {
                br.read_ubits(v.edict_bits);
                br.read_ubits(kModelIndexBits);
            }
            br.read_bit();   // low priority
            break;
        }
        case svc_UserMessage: {
            br.read_byte();   // тип user message
            const uint32_t bits = br.read_ubits(kMaxUserMessageBits);
            skip_block(br, bits);
            break;
        }
        case svc_EntityMessage: {
            br.read_ubits(v.edict_bits);
            br.read_ubits(kMaxServerClassBits);
            const uint32_t bits = br.read_ubits(kMaxEntityMessageBits);
            skip_block(br, bits);
            break;
        }
        case svc_GameEvent: {
            const uint32_t bits = br.read_ubits(11);
            skip_block(br, bits);
            break;
        }
        case svc_PacketEntities: {
            br.read_ubits(v.edict_bits);          // max entries
            if (br.read_bit()) br.read_long();    // delta from
            br.read_ubits(1);                     // baseline
            br.read_ubits(v.edict_bits);          // updated entries
            const uint32_t bits = br.read_ubits(v.packet_entities_len_bits);
            br.read_bit();                        // update baseline
            if (br.overflowed() || bits > br.bits_left()) return fail(type, "svc_PacketEntities: довжина за межами пакета");
            skip_block(br, bits);
            break;
        }
        case svc_TempEntities: {
            br.read_ubits(8);   // кількість
            const uint32_t bits = br.read_varint32();
            if (br.overflowed() || bits > br.bits_left()) return fail(type, "svc_TempEntities: довжина за межами пакета");
            skip_block(br, bits);
            break;
        }
        case svc_Prefetch:
            br.read_ubits(kMaxSoundIndexBits);
            break;
        case svc_Menu: {
            br.read_short();                          // тип меню
            const uint32_t bytes = br.read_word();
            skip_block(br, static_cast<size_t>(bytes) * 8);
            break;
        }
        case svc_GameEventList: {
            br.read_ubits(9);
            const uint32_t bits = br.read_ubits(20);
            skip_block(br, bits);
            break;
        }
        case svc_GetCvarValue:
            br.read_long();
            br.read_string(256);
            break;
        case svc_CmdKeyValues: {
            const uint32_t bytes = br.read_ubits(32);
            if (bytes > br.bits_left() / 8) return fail(type, "svc_CmdKeyValues: довжина за межами пакета");
            skip_block(br, static_cast<size_t>(bytes) * 8);
            break;
        }
        case svc_GMod_ServerToClient: {
            const uint32_t bits = br.read_ubits(20);
            if (bits > br.bits_left()) return fail(type, "svc_GMod_ServerToClient: довжина за межами пакета");
            skip_block(br, bits);
            break;
        }
        default:
            return fail(type, std::format("невідомий тип повідомлення {} (біт {})", type, msg_start));
        }
        if (br.overflowed()) return fail(type, std::format("{} виходить за межі пакета", net_message_name(type)));
        ++res.messages;
    }
    return res;
}

} // namespace gmdr::demo
