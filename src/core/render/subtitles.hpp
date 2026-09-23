// =============================================================================
//  subtitles.hpp — субтитри "хто говорить" (.srt) з голосових доріжок демо.
//  Легше монтувати й шукати моменти: плеєр (VLC, mpv) або програма монтажу
//  показує ім'я того, хто зараз говорить.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../demo/chat.hpp"
#include "../speech/transcribe.hpp"
#include "../voice/voice_decoder.hpp"

namespace gmdr::render {

struct SpeakerSubtitleSource {
    const voice::SpeakerTrack* track = nullptr;
    std::string                name;   // як підписати
};

// Субтитри для відрізка відео: origin_sample — семпл демо (48 кГц), що відповідає
// початку відео; duration — тривалість відео (с); delay — зсув голосу (с).
// Близькі фрази одного гравця (паузи < 0.6 с) об'єднуються; кілька гравців
// одночасно — "Ім'я1, Ім'я2".
// speed — швидкість відео (0.5 — уповільнення: репліки й затримка голосу розтягуються разом зі звуком).
std::string make_speaker_srt(const std::vector<SpeakerSubtitleSource>& speakers, int64_t origin_sample,
                             double duration, double delay = 0.0, double speed = 1.0);

// Субтитри чату: повідомлення з фрагмента [start_tick, end_tick) — кожне видно кілька
// секунд, одночасно до 4 останніх рядків (як у чаті гри). Для відео з прихованим HUD.
std::string make_chat_srt(const std::vector<demo::DemoEvent>& events, int32_t start_tick, int32_t end_tick,
                          double tick_interval, double duration);

// Субтитри з текстом розмов (розпізнане мовлення): "Ім'я: текст", одночасно до 3 реплік.
// origin — секунда демо першого кадру відео; keys — чиї репліки (порожньо — усіх);
// delay — зсув голосу (с, у часі демо), speed — швидкість відео.
std::string make_transcript_srt(const std::vector<speech::Line>& lines, const std::vector<std::string>& keys,
                                double origin, double duration, double delay = 0.0, double speed = 1.0);

// Час у форматі SRT: 01:02:03,456
std::string srt_timestamp(double seconds);

} // namespace gmdr::render
