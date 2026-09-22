#include "string_tables.hpp"

#include "bitreader.hpp"

#include <algorithm>
#include <cstring>
#include <format>

namespace gmdr::demo {

namespace {
// Скільки біт займає довжина user data у записі таблиці. У Source SDK 2013 — 14,
// а сучасний Garry's Mod збільшив до 19 (перевірено на реальних демо: з 14 бітами
// таблиці userinfo/lightstyles/client_lua_files розбираються зі зсувом).
constexpr int kUserDataLenBitsGMod = 19;
constexpr int kUserDataLenBitsSource = 14;
constexpr int kSubstringBits = 5;
constexpr uint64_t kSteamIdBase = 76561197960265728ULL;
} // namespace

uint64_t steamid64_from_guid(const std::string& guid) {
    // Формат: STEAM_X:Y:Z  ->  base + Z*2 + Y
    if (guid.rfind("STEAM_", 0) != 0) return 0;
    const size_t c1 = guid.find(':');
    const size_t c2 = guid.find(':', c1 == std::string::npos ? 0 : c1 + 1);
    if (c1 == std::string::npos || c2 == std::string::npos) return 0;
    try {
        const uint64_t y = std::stoull(guid.substr(c1 + 1, c2 - c1 - 1));
        const uint64_t z = std::stoull(guid.substr(c2 + 1));
        if (y > 1) return 0;
        return kSteamIdBase + z * 2 + y;
    } catch (...) {
        return 0;
    }
}

std::optional<PlayerInfo> parse_player_info(const std::vector<uint8_t>& d) {
    if (d.size() < 8) return std::nullopt;
    PlayerInfo pi;
    // Ім'я — перший рядок з нульовим завершенням. У GMod структура займає 324 байти
    // і під ім'я відведено 128 байт; у Source SDK 2013 — 132 байти і 32 байти на ім'я.
    const size_t name_max = d.size() >= 324 ? 128 : 32;
    size_t n = 0;
    while (n < d.size() && n < name_max && d[n] != 0) ++n;
    pi.name.assign(reinterpret_cast<const char*>(d.data()), n);
    // GUID шукаємо за шаблоном "STEAM_" або "BOT".
    for (size_t i = 0; i + 6 < d.size(); ++i) {
        if (std::memcmp(d.data() + i, "STEAM_", 6) == 0) {
            size_t e = i;
            while (e < d.size() && d[e] != 0 && e - i < 40) ++e;
            pi.guid.assign(reinterpret_cast<const char*>(d.data() + i), e - i);
            pi.steamid64 = steamid64_from_guid(pi.guid);
            break;
        }
        if (i >= 32 && std::memcmp(d.data() + i, "BOT", 3) == 0 && (i + 3 >= d.size() || d[i + 3] == 0)) {
            pi.guid = "BOT";
            pi.fake_player = true;
            break;
        }
    }
    return pi;
}

std::optional<std::vector<uint8_t>> lzss_decompress(const uint8_t* data, size_t size) {
    if (size < 8 || std::memcmp(data, "LZSS", 4) != 0) return std::nullopt;
    uint32_t actual = 0;
    std::memcpy(&actual, data + 4, 4);
    if (actual > (64u << 20)) return std::nullopt;   // захист від сміття
    std::vector<uint8_t> out;
    out.reserve(actual);
    size_t in = 8;
    uint32_t cmd_byte = 0;
    int get_cmd = 0;
    while (in < size) {
        if (!get_cmd) cmd_byte = data[in++];
        get_cmd = (get_cmd + 1) & 7;
        if (cmd_byte & 1) {
            if (in + 2 > size) return std::nullopt;
            uint32_t position = static_cast<uint32_t>(data[in]) << 4;
            position |= data[in + 1] >> 4;
            const uint32_t count = (data[in + 1] & 0x0F) + 1u;
            in += 2;
            if (count == 1) break;   // маркер кінця
            if (position + 1 > out.size()) return std::nullopt;
            size_t src = out.size() - position - 1;
            for (uint32_t i = 0; i < count; ++i) out.push_back(out[src + i]);
        } else {
            if (in >= size) break;
            out.push_back(data[in++]);
        }
        cmd_byte >>= 1;
    }
    if (out.size() != actual) return std::nullopt;
    return out;
}

bool StringTableSet::on_create(const CreateStringTableMsg& msg) {
    StringTable t;
    t.name = msg.name;
    t.max_entries = msg.max_entries;
    t.user_data_fixed_size = msg.user_data_fixed_size;
    t.user_data_size = msg.user_data_size;
    t.user_data_size_bits = msg.user_data_size_bits;
    t.entries.resize(static_cast<size_t>(std::max(0, msg.max_entries)));
    tables_.push_back(std::move(t));
    StringTable& table = tables_.back();

    if (msg.compressed) {
        // Стиснені дані: u32 розпакований розмір, u32 стиснений розмір, далі блок.
        BitReader br(msg.data.data(), msg.data.size(), msg.data_bits);
        const uint32_t uncompressed = br.read_ubits(32);
        const uint32_t compressed = br.read_ubits(32);
        if (br.overflowed() || compressed > br.bits_left() / 8) {
            error_ = "стиснена таблиця рядків пошкоджена";
            return false;
        }
        std::vector<uint8_t> block = br.read_bits_to_bytes(static_cast<size_t>(compressed) * 8);
        auto raw = lzss_decompress(block.data(), block.size());
        if (!raw || raw->size() != uncompressed) {
            error_ = std::format("не вдалося розпакувати таблицю '{}'", msg.name);
            return false;
        }
        return apply_update(table, raw->data(), raw->size() * 8, msg.num_entries);
    }
    return apply_update(table, msg.data.data(), msg.data_bits, msg.num_entries);
}

bool StringTableSet::on_update(const UpdateStringTableMsg& msg) {
    if (msg.table_id < 0 || static_cast<size_t>(msg.table_id) >= tables_.size()) {
        error_ = std::format("оновлення невідомої таблиці {}", msg.table_id);
        return false;
    }
    return apply_update(tables_[static_cast<size_t>(msg.table_id)], msg.data.data(), msg.data_bits,
                        msg.changed_entries);
}

bool StringTableSet::apply_update(StringTable& t, const uint8_t* data, size_t bits, int num_entries) {
    // Ширину поля довжини user data визначаємо за першим "чистим" розбором:
    // спершу пробуємо варіант GMod (19 біт), потім класичний Source (14 біт).
    // "Чистий" — без виходу за межі і з залишком менше байта.
    const int preferred = ud_len_bits_;
    const int other = preferred == kUserDataLenBitsGMod ? kUserDataLenBitsSource : kUserDataLenBitsGMod;
    StringTable backup = t;
    if (apply_update_bits(t, data, bits, num_entries, preferred)) return true;
    const std::string first_error = error_;
    t = backup;
    if (!t.user_data_fixed_size && apply_update_bits(t, data, bits, num_entries, other)) {
        ud_len_bits_ = other;   // далі у цьому демо використовуємо цей варіант
        return true;
    }
    t = std::move(backup);
    error_ = first_error;
    return false;
}

bool StringTableSet::apply_update_bits(StringTable& t, const uint8_t* data, size_t bits, int num_entries,
                                       int ud_len_bits) {
    BitReader br(data, (bits + 7) / 8, bits);
    const int entry_bits = q_log2(std::max(1, t.max_entries));
    int last_entry = -1;
    std::vector<std::string> history;
    for (int i = 0; i < num_entries; ++i) {
        int index = last_entry + 1;
        if (!br.read_bit()) index = static_cast<int>(br.read_ubits(entry_bits));
        last_entry = index;
        if (index < 0 || index >= t.max_entries) {
            error_ = std::format("таблиця '{}': індекс {} поза межами", t.name, index);
            return false;
        }
        std::string key;
        bool has_key = false;
        if (br.read_bit()) {
            has_key = true;
            if (br.read_bit()) {
                const int hist_index = static_cast<int>(br.read_ubits(5));
                const int bytes_to_copy = static_cast<int>(br.read_ubits(kSubstringBits));
                if (hist_index < static_cast<int>(history.size()))
                    key = history[static_cast<size_t>(hist_index)].substr(0, static_cast<size_t>(bytes_to_copy));
                key += br.read_string(1024);
            } else {
                key = br.read_string(1024);
            }
        }
        std::vector<uint8_t> user_data;
        bool has_data = false;
        if (br.read_bit()) {
            has_data = true;
            if (t.user_data_fixed_size) {
                user_data = br.read_bits_to_bytes(static_cast<size_t>(t.user_data_size_bits));
            } else {
                const uint32_t nbytes = br.read_ubits(ud_len_bits);
                user_data = br.read_bits_to_bytes(static_cast<size_t>(nbytes) * 8);
            }
        }
        if (br.overflowed()) {
            error_ = std::format("таблиця '{}': дані обрізані", t.name);
            return false;
        }
        StringTableEntry& e = t.entries[static_cast<size_t>(index)];
        if (has_key) e.key = key;
        if (has_data) e.user_data = std::move(user_data);
        e.present = true;
        if (history.size() > 31) history.erase(history.begin());
        history.push_back(e.key);
    }
    if (br.bits_left() >= 8) {
        error_ = std::format("таблиця '{}': зайві дані після записів ({} біт)", t.name, br.bits_left());
        return false;
    }
    return true;
}

bool StringTableSet::on_snapshot(const uint8_t* data, size_t size) {
    // Формат (CNetworkStringTableContainer::WriteStringTables):
    //   u8 кількість таблиць; для кожної: назва, u16 кількість рядків,
    //   рядки [рядок, біт "є дані", u16 довжина, дані], біт "є клієнтські рядки" ...
    BitReader br(data, size);
    const int num_tables = br.read_byte();
    for (int ti = 0; ti < num_tables && !br.overflowed(); ++ti) {
        const std::string name = br.read_string(256);
        const int num_strings = br.read_word();
        StringTable* table = nullptr;
        for (auto& t : tables_)
            if (t.name == name) table = &t;
        for (int i = 0; i < num_strings; ++i) {
            std::string key = br.read_string(4096);
            std::vector<uint8_t> ud;
            if (br.read_bit()) {
                const int len = br.read_word();
                ud = br.read_bits_to_bytes(static_cast<size_t>(len) * 8);
            }
            if (br.overflowed()) break;   // обрізаний запис не застосовуємо
            if (table && i < table->max_entries) {
                auto& e = table->entries[static_cast<size_t>(i)];
                e.key = std::move(key);
                e.user_data = std::move(ud);
                e.present = true;
            }
        }
        if (br.overflowed()) break;
        if (br.read_bit()) {   // клієнтські рядки — пропускаємо
            const int num_client = br.read_word();
            for (int i = 0; i < num_client && !br.overflowed(); ++i) {
                br.read_string(4096);
                if (br.read_bit()) {
                    const int len = br.read_word();
                    br.skip_bits(static_cast<size_t>(len) * 8);
                }
            }
        }
    }
    // Гра обмежує знімок 512 КБ, і в GMod він майже завжди обрізаний на таблиці
    // client_lua_files. Таблиці до місця обриву вже застосовані — це не помилка.
    snapshot_truncated_ = br.overflowed();
    return true;
}

const StringTable* StringTableSet::find(const std::string& name) const {
    for (const auto& t : tables_)
        if (t.name == name) return &t;
    return nullptr;
}

std::map<int, PlayerInfo> StringTableSet::players() const {
    std::map<int, PlayerInfo> out;
    const StringTable* t = find("userinfo");
    if (!t) return out;
    for (size_t i = 0; i < t->entries.size(); ++i) {
        const auto& e = t->entries[i];
        if (!e.present || e.user_data.empty()) continue;
        auto pi = parse_player_info(e.user_data);
        if (!pi) continue;
        pi->slot = static_cast<int>(i);
        out[static_cast<int>(i)] = *pi;
    }
    return out;
}

} // namespace gmdr::demo
