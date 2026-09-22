// =============================================================================
//  steam_voice.hpp — формат голосових пакетів Steam Voice (використовує GMod).
//
//  Пакет (усе little-endian):
//      u64  SteamID64 мовця
//      далі послідовність "операцій":
//          0x0B (11) u16 частота дискретизації (зазвичай 24000)
//          0x06 (6)  u16 довжина блоку, далі кадри Opus з PLC:
//                     i16 довжина кадру (-1 = скинути декодер), u16 номер кадру, байти Opus
//          0x00 (0)  u16 кількість семплів тиші
//          0x0A (10) 2 байти (невідомо)
//          0x04/0x05/0x01 інші кодеки: u16 довжина + дані (ми їх пропускаємо)
//      u32  CRC32 (IEEE) усіх попередніх байтів
//
//  Один кадр Opus = 20 мс (480 семплів при 24 кГц).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gmdr::voice {

uint32_t crc32_ieee(const uint8_t* data, size_t size);

struct OpusFrame {
    bool           reset = false;    // маркер кінця фрази (довжина = -1)
    uint16_t       seq = 0;
    const uint8_t* data = nullptr;
    size_t         size = 0;
};

struct SteamVoiceOp {
    enum class Kind { Opus, Silence, Unsupported } kind = Kind::Unsupported;
    std::vector<OpusFrame> frames;         // для Opus
    uint32_t               silence_samples = 0;
    int                    opcode = 0;
};

struct SteamVoicePacket {
    uint64_t                  steamid64 = 0;
    int                       sample_rate = 24000;
    bool                      crc_ok = false;
    std::vector<SteamVoiceOp> ops;
};

// Швидка перевірка "схоже на пакет Steam Voice" (SteamID + CRC).
bool looks_like_steam_voice(const uint8_t* data, size_t size);

// Повний розбір. Вказівники у кадрах посилаються на вхідний буфер!
bool parse_steam_voice(const uint8_t* data, size_t size, SteamVoicePacket& out, std::string* error = nullptr);

} // namespace gmdr::voice
