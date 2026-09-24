// =============================================================================
//  dub_mix.hpp — озвучена доріжка: синтезовані фрази на своїх місцях поверх звуку
//  гри (і тихих оригінальних голосів), закодована в аудіофайли.
//
//  Фраза перекладу буває довшою за оригінал. Тоді її трохи пришвидшуємо (atempo, до
//  ~1.35×), а якщо й так не влазить до наступної фрази того самого гравця — наступна
//  зсувається пізніше. Різні гравці можуть говорити одночасно, як і в оригіналі.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../audio/audio_inputs.hpp"
#include "../media/audio_encoder.hpp"

namespace gmdr::dub {

// ---- Розстановка -----------------------------------------------------------------------
struct Slot {
    double      start = 0, end = 0;   // оригінальна фраза, с відео
    std::string key;                  // гравець
    double      length = 0;           // тривалість озвученої фрази, с (0 — не озвучено)
};
struct Placement {
    double at = 0;      // з якої секунди відео звучить
    double tempo = 1;   // пришвидшення (1 — як є)
};
// max_tempo — найбільше пришвидшення; gap — пауза між фразами одного гравця.
std::vector<Placement> place_clips(const std::vector<Slot>& slots, double max_tempo = 1.35, double gap = 0.12);

// ---- Фрази -----------------------------------------------------------------------------
struct Clip {
    int64_t            at = 0;   // позиція, кадри 48 кГц від початку відео
    std::vector<float> stereo;   // 48 кГц стерео
};
// WAV від синтезу → 48 кГц стерео без тиші на краях, рівень ~-20 дБ RMS, м'які краї.
std::optional<std::vector<float>> load_clip(const std::filesystem::path& wav, std::string* error);
// Пришвидшити фразу (atempo, висота голосу та сама); не вдалося — як є.
std::vector<float> change_tempo(std::vector<float> clip, double tempo);

// Джерело для змішувача: фрази на своїх позиціях
class ClipsInput final : public audio::AudioInput {
public:
    explicit ClipsInput(std::vector<Clip> clips);
    std::string name() const override;
    int64_t available() override { return INT64_MAX; }
    void mix(int64_t pos, float* out, size_t frames, float gain) override;
    void discard_before(int64_t pos) override;

private:
    std::vector<Clip> clips_;   // за at
    size_t            first_ = 0;
};

// ---- Мікс і кодування -------------------------------------------------------------------
struct MixSpec {
    std::filesystem::path              game;        // WAV звуку гри від першого кадру відео (порожньо — без)
    std::vector<std::filesystem::path> originals;   // оригінальні голоси й мікрофон (стільки ж, від першого кадру)
    float                              original_gain = 0.12f;
    float                              dub_gain = 1.0f;
    bool                               duck = false;        // гра стихає під озвучення
    double                             loudness = 0;        // LUFS міксу; 0 — не змінювати
    double                             seconds = 0;         // тривалість (як у відео)
};
struct MixOutput {
    std::string                 path;        // UTF-8
    std::string                 container;   // порожньо — за розширенням
    media::AudioEncoderSettings audio;
    std::string                 title;       // назва доріжки
    std::string                 language;    // ISO 639-2 ("deu")
};
bool mix_dub(const MixSpec& spec, std::vector<Clip> clips, const std::vector<MixOutput>& outputs,
             const std::function<void(double)>& progress, const std::atomic<bool>* cancel, std::string* error);

// Кодек і розширення окремого аудіофайлу: mp3 / flac / wav / m4a
struct AudioFormat {
    const char* id;
    const char* ext;
    const char* codec;
    int64_t     bitrate;
};
const std::vector<AudioFormat>& audio_formats();
const AudioFormat&              find_audio_format(const std::string& id);

} // namespace gmdr::dub
