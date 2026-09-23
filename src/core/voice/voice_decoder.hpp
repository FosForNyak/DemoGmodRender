// =============================================================================
//  voice_decoder.hpp — голос гравців з демо: хто і коли говорив, декодування.
//
//  Робота у два кроки, щоб навіть багатогодинні демо відкривалися швидко:
//
//  1) decode_voice() лише РОЗБИРАЄ пакети Steam Voice: для кожного мовця
//     (SteamID) будує список подій (кадри Opus, тиша, скидання декодера) і
//     розставляє відрізки мовлення на часовій шкалі демо (час = тік пакета ×
//     інтервал тіку). Звук ще не декодується — це займає секунди й мало пам'яті.
//
//  2) VoiceStream декодує звук потоково, лише для потрібного відрізка часу
//     (наприклад, під час рендеру фрагмента). Частота — 48 кГц, моно.
//
//  Важливо про ВЛАСНИЙ голос: сервер не пересилає гравцю його ж голос, тому
//  у демо він є лише якщо під час запису було ввімкнено voice_loopback 1.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../demo/analysis.hpp"

namespace gmdr::voice {

constexpr int kVoiceRate = 48000;   // частота, у якій віддаємо декодований голос

// Одна подія голосового потоку мовця.
struct VoiceEvent {
    enum Kind : uint8_t { Frame = 0, Silence = 1, Reset = 2 };
    uint8_t  kind = Frame;
    uint32_t samples = 0;       // тривалість у семплах 48 кГц (для Reset — 0)
    uint32_t data_offset = 0;   // Frame: де лежить кадр Opus у SpeakerTrack::blob
    uint32_t data_size = 0;
};

// Суцільний відрізок мовлення на часовій шкалі демо.
struct VoiceSegment {
    int64_t  start = 0;         // перший семпл (48 кГц) від тіку 0 демо
    int64_t  length = 0;        // тривалість у семплах 48 кГц
    uint32_t first_event = 0;   // події [first_event, first_event + event_count)
    uint32_t event_count = 0;
    int64_t end() const { return start + length; }
};

struct SpeakerTrack {
    std::string               key;          // "steam:7656..." або "slot:3"
    int                       slot = -1;
    uint64_t                  steamid64 = 0;
    std::string               name;
    bool                      is_local = false;   // це той, хто записував демо
    std::vector<VoiceSegment> segments;
    std::vector<VoiceEvent>   events;
    std::vector<uint8_t>      blob;               // байти кадрів Opus
    int                       packets = 0;
    int                       frames = 0;
    int                       lost_frames = 0;
    int                       unsupported_ops = 0;
    double                    seconds = 0.0;      // сумарна тривалість мовлення

    int64_t end_sample() const { return segments.empty() ? 0 : segments.back().end(); }
    std::string display_name() const;
};

struct VoiceDecodeOptions {
    double jitter_tolerance = 0.08;   // с: менші паузи між пакетами вважаємо мережевим джитером
    double latency = 0.0;             // с: додаткова затримка всього голосу
};

struct VoiceDecodeResult {
    std::vector<SpeakerTrack> speakers;
    int                       total_packets = 0;
    int                       empty_packets = 0;    // порожні (так гра позначає власного гравця)
    int                       bad_packets = 0;      // не Steam Voice / пошкоджені
    std::vector<std::string>  warnings;
};

// Розібрати голос з демо (без декодування звуку — див. VoiceStream).
VoiceDecodeResult decode_voice(const demo::DemoAnalysis& analysis, const VoiceDecodeOptions& opt = {},
                               const std::function<void(double)>& progress = {},
                               const std::atomic<bool>* cancel = nullptr);

// Потокове декодування голосу одного мовця. Найшвидше, коли запити йдуть
// підряд уперед (як під час рендеру); стрибок назад чи в середину довгої
// фрази змушує декодувати цю фразу від початку.
class VoiceStream {
public:
    explicit VoiceStream(const SpeakerTrack& track);
    ~VoiceStream();
    VoiceStream(const VoiceStream&) = delete;
    VoiceStream& operator=(const VoiceStream&) = delete;

    // Додати до out семпли [from, from + count) часової шкали демо (48 кГц, моно),
    // помножені на gain. stride — крок у out (2 — писати в кожен другий float).
    void mix_into(int64_t from, size_t count, float* out, float gain = 1.0f, size_t stride = 1);

    int decode_errors() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Декодувати відрізок [from, to) у моно PCM 48 кГц.
std::vector<float> decode_range(const SpeakerTrack& track, int64_t from, int64_t to);

// Джерело звуку для експорту: додати в out семпли [from, from + count) часової шкали демо.
using VoiceFill = std::function<void(int64_t from, size_t count, float* out)>;

// Записати голос мовця за відрізок [from, to) у файл: .wav (16 біт, моно) або
// .flac (без втрат; тиша майже не займає місця). to < 0 — до кінця мовлення.
// fill — замість сирого голосу (напр. з обробкою).
bool export_speaker_audio(const SpeakerTrack& track, const std::filesystem::path& path, int64_t from, int64_t to,
                          std::string* error = nullptr, const std::atomic<bool>* cancel = nullptr,
                          const std::function<void(double)>& progress = {}, const VoiceFill& fill = {});

// Тривалість пакета Opus у семплах 48 кГц за його першим байтом (TOC); 0 — невідомо.
int opus_packet_samples_48k(const uint8_t* data, size_t size);

} // namespace gmdr::voice
