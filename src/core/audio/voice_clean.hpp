// =============================================================================
//  voice_clean.hpp — обробка голосу гравців: рівна гучність і тиша між фразами.
//
//  Голос з демо відомий повністю ще до рендеру, тож параметри рахуються для
//  кожного гравця окремо з його власного мовлення (VoiceProfile):
//   - гучність за EBU R128 (ITU-R BS.1770) -> постійне підсилення до спільного
//     рівня: без "дихання", яке дають динамічні нормалізатори;
//   - рівень фону і мови -> поріг гейта. Один поріг на всіх не працює: у гравця
//     з відкритим мікрофоном фон лише на 15–20 дБ тихіший за мову, у решти — на 25+.
//
//  NoiseGate — гейт з RMS-детектором на вікні 20 мс уперед (lookahead), з
//  гістерезисом і утриманням: клацання клавіатури не відкривають його, а
//  початки слів не зрізаються.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gmdr::voice {
struct SpeakerTrack;
}

namespace gmdr::audio {

constexpr double kVoiceLevelTarget = -18.0;   // LUFS: рівень кожного гравця при вирівнюванні

// Інтегральна гучність моно-сигналу 48 кГц за BS.1770-4 (LUFS). Тиша — -99.
double integrated_loudness(const float* mono, size_t n);

struct VoiceProfile {
    double loudness = -99;    // LUFS
    double noise_db = -99;    // типовий рівень фону в паузах (RMS вікон 20 мс, дБ)
    double speech_db = -99;   // типовий рівень мови
    double seconds = 0;       // скільки мовлення проаналізовано
    bool valid() const { return seconds >= 1.0 && loudness > -70; }
};

// Проаналізувати мовлення гравця (не більше max_seconds, рівномірно по всьому демо).
VoiceProfile profile_voice(const voice::SpeakerTrack& track, double max_seconds = 600.0);
// Те саме для вже декодованого моно-сигналу (відрізки тиші пропускаються).
VoiceProfile profile_samples(const float* mono, size_t n);

// Підсилення, що доводить гравця до target LUFS (обмежене: -12…+15 дБ).
float level_gain(const VoiceProfile& p, double target_lufs = kVoiceLevelTarget);

// Обробка голосу одного гравця під час рендеру.
struct VoiceCleanup {
    float        level = 1.0f;     // підсилення вирівнювання гучності
    bool         denoise = false;  // шумодав і гейт між фразами
    VoiceProfile profile;
};

struct GateParams {
    double open_db = -45;     // відкривається вище цього рівня (RMS вікна 20 мс)
    double close_db = -49;    // закривається нижче (гістерезис)
    double floor_db = -30;    // наскільки глушити фон, коли закрито
};
GateParams gate_for(const VoiceProfile& p, float gain = 1.0f);

class NoiseGate {
public:
    explicit NoiseGate(const GateParams& p);
    // Скільки семплів "уперед" потрібно детектору.
    size_t lookahead() const { return window_; }
    // Обробити на місці x[0, n): для кожного семпла i читаються x[i .. i + lookahead()),
    // тож у буфері має бути щонайменше n + lookahead() семплів. Стан переходить між викликами
    // (наступний виклик починається з x[n] попереднього).
    void process(float* x, size_t n);
    bool open() const { return open_; }

private:
    size_t window_;
    double open_pow_, close_pow_;
    float  floor_;
    float  attack_step_, release_step_;
    size_t hold_;
    bool   open_ = false;
    size_t hold_left_ = 0;
    float  gain_;
    double sum_ = 0;          // сума квадратів вікна [i, i + window_)
    bool   primed_ = false;
    size_t since_recalc_ = 0;
};

} // namespace gmdr::audio
