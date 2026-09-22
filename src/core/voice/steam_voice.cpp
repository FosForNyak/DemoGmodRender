#include "steam_voice.hpp"

#include <array>
#include <cstring>
#include <format>

namespace gmdr::voice {

namespace {
std::array<uint32_t, 256> make_crc_table() {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t rd64(const uint8_t* p) { return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32); }

bool plausible_steamid(uint64_t id) {
    // Універсум 1 (публічний), тип 1 (звичайний акаунт), instance 1.
    return (id >> 32) == 0x01100001u;
}
} // namespace

uint32_t crc32_ieee(const uint8_t* data, size_t size) {
    static const auto table = make_crc_table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool looks_like_steam_voice(const uint8_t* data, size_t size) {
    if (size < 8 + 1 + 4) return false;
    if (!plausible_steamid(rd64(data))) return false;
    return crc32_ieee(data, size - 4) == rd32(data + size - 4);
}

bool parse_steam_voice(const uint8_t* data, size_t size, SteamVoicePacket& out, std::string* error) {
    out = SteamVoicePacket{};
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (size < 8 + 4) return fail("пакет занадто короткий");
    out.steamid64 = rd64(data);
    out.crc_ok = crc32_ieee(data, size - 4) == rd32(data + size - 4);
    size_t pos = 8;
    const size_t end = size - 4;
    while (pos < end) {
        const uint8_t op = data[pos++];
        switch (op) {
        case 11: {   // частота
            if (pos + 2 > end) return fail("обрізана операція частоти");
            out.sample_rate = rd16(data + pos);
            pos += 2;
            break;
        }
        case 10: {   // невідомо, 2 байти
            if (pos + 2 > end) return fail("обрізана операція 10");
            pos += 2;
            break;
        }
        case 0: {    // тиша
            if (pos + 2 > end) return fail("обрізана операція тиші");
            SteamVoiceOp o;
            o.kind = SteamVoiceOp::Kind::Silence;
            o.opcode = op;
            o.silence_samples = rd16(data + pos);
            pos += 2;
            out.ops.push_back(std::move(o));
            break;
        }
        case 6: {    // Opus з PLC
            if (pos + 2 > end) return fail("обрізаний блок Opus");
            const size_t len = rd16(data + pos);
            pos += 2;
            if (pos + len > end) return fail("блок Opus виходить за межі пакета");
            SteamVoiceOp o;
            o.kind = SteamVoiceOp::Kind::Opus;
            o.opcode = op;
            size_t p = pos;
            const size_t block_end = pos + len;
            while (p + 2 <= block_end) {
                const int16_t flen = static_cast<int16_t>(rd16(data + p));
                p += 2;
                OpusFrame f;
                if (flen == -1) {
                    f.reset = true;
                    o.frames.push_back(f);
                    continue;
                }
                if (flen < 0 || p + 2 > block_end) return fail("пошкоджений кадр Opus");
                f.seq = rd16(data + p);
                p += 2;
                if (p + static_cast<size_t>(flen) > block_end) return fail("кадр Opus виходить за межі блоку");
                f.data = data + p;
                f.size = static_cast<size_t>(flen);
                p += static_cast<size_t>(flen);
                o.frames.push_back(f);
            }
            pos = block_end;
            out.ops.push_back(std::move(o));
            break;
        }
        case 1:
        case 2:
        case 4:
        case 5: {    // інші кодеки (legacy, SILK, Opus без PLC) — пропускаємо
            if (pos + 2 > end) return fail("обрізаний блок кодека");
            const size_t len = rd16(data + pos);
            pos += 2 + len;
            if (pos > end) return fail("блок кодека виходить за межі пакета");
            SteamVoiceOp o;
            o.kind = SteamVoiceOp::Kind::Unsupported;
            o.opcode = op;
            out.ops.push_back(std::move(o));
            break;
        }
        case 3: {    // сирі семпли до кінця
            SteamVoiceOp o;
            o.kind = SteamVoiceOp::Kind::Unsupported;
            o.opcode = op;
            out.ops.push_back(std::move(o));
            pos = end;
            break;
        }
        default:
            return fail(std::format("невідома операція Steam Voice 0x{:02x}", op));
        }
    }
    return true;
}

} // namespace gmdr::voice
