// =============================================================================
//  tts.hpp — синтез мовлення для озвучення перекладу.
//
//    omnivoice   — локально: OmniVoice (k2-fsa, Apache-2.0), 600+ мов, клонування голосу
//                  з 3–10 с зразка. Працює в окремому середовищі Python, яке програма
//                  ставить сама (<дані програми>/voice_engine, ~5 ГБ, з GPU NVIDIA —
//                  значно швидше), і вбудованому помічнику gmdr_voice.py.
//    elevenlabs  — хмара ElevenLabs (ключ): Multilingual v2 чи інша модель; клон голосу
//                  гравця (Instant Voice Cloning) створюється один раз і запам'ятовується.
//    fake        — тон замість мови, для тестів.
//
//  Результат — WAV 24 кГц моно для кожної фрази.
// =============================================================================
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gmdr::dub {

struct EngineInfo {
    const char* id;
    const char* label;
    bool        cloud;   // текст і зразки голосу йдуть на сервер
};
const std::vector<EngineInfo>& engines();
const EngineInfo*              find_engine(const std::string& id);
// Чи вміє рушій мову (коди translate::languages()).
bool engine_supports(const std::string& engine, const std::string& elevenlabs_model, const std::string& lang);

// Чим озвучувати одного гравця
struct SpeakerVoice {
    std::string                        key;
    std::filesystem::path              ref_wav;    // клон (локально): еталон 3–10 с …
    std::string                        ref_text;   // … і що в ньому сказано
    std::string                        elevenlabs_voice_id;   // голос ElevenLabs (клон чи готовий)
    std::string                        instruct;   // без клону (локально): опис голосу, "male, low pitch"
};
// Готові голоси, щоб різні гравці без клону звучали по-різному (за номером гравця)
std::string generic_instruct(size_t index);
std::string generic_elevenlabs_voice(size_t index);

struct TtsItem {
    std::string           text;
    std::string           language;   // код мови ("de", "pt-BR")
    size_t                voice = 0;  // номер у списку SpeakerVoice
    std::filesystem::path out;        // WAV
};

struct TtsConfig {
    std::string engine = "omnivoice";
    std::string device = "auto";      // omnivoice: auto / cuda / cpu
    int         num_step = 32;        // omnivoice: кроки дифузії (менше — швидше, гірше)
    std::string python;               // omnivoice: свій Python з omnivoice (порожньо — встановлений програмою)
    std::string elevenlabs_key;
    std::string elevenlabs_model = "eleven_multilingual_v2";
};

using TtsProgress = std::function<void(double fraction, const std::string& what)>;

// Озвучити всі фрази. Фраза, яку рушій не зміг озвучити, лишається без файлу (warnings);
// false — рушій не працює взагалі (error).
bool synthesize(const TtsConfig& c, const std::vector<SpeakerVoice>& voices, const std::vector<TtsItem>& items,
                const std::filesystem::path& work_dir, const TtsProgress& progress, const std::atomic<bool>* cancel,
                std::vector<std::string>* warnings, std::string* error);

// ---- ElevenLabs ----------------------------------------------------------------------
// Створити клон голосу зі зразків (до 25 файлів) — повертає voice_id.
std::optional<std::string> elevenlabs_clone(const std::string& key, const std::string& name,
                                            const std::vector<std::filesystem::path>& samples,
                                            const std::atomic<bool>* cancel, std::string* error);
bool elevenlabs_delete_voice(const std::string& key, const std::string& voice_id, std::string* error);
// Перевірити ключ: ім'я тарифу й залишок символів ("creator, 81234 of 100000 left")
std::optional<std::string> elevenlabs_check(const std::string& key, std::string* error);

// ---- Локальний рушій (OmniVoice) -------------------------------------------------------
std::filesystem::path engine_dir();   // <дані програми>/voice_engine
// Python середовища рушія, якщо його встановлено
std::optional<std::filesystem::path> engine_python(const TtsConfig& c);
// Скрипт-помічник (записується поруч із середовищем з тексту, вбудованого в програму)
std::filesystem::path write_helper_script(std::string* error);
// Чи є GPU NVIDIA (nvidia-smi відповідає)
bool has_nvidia_gpu();

// Встановити рушій: uv → Python 3.12 → PyTorch (CUDA 12.8 або CPU) → omnivoice → модель.
// Усе — у engine_dir(), нічого в системі не змінюється. Windows.
bool install_engine(bool cuda, const TtsProgress& progress, const std::atomic<bool>* cancel, std::string* error);
// Перевірити встановлений рушій: завантажити модель ("cuda:0" / "cpu")
std::optional<std::string> check_engine(const TtsConfig& c, const std::atomic<bool>* cancel, std::string* error);
bool remove_engine(std::string* error);

// ---- Складники (тестуються окремо) --------------------------------------------------
// Завдання для помічника: {"device", "num_step", "items": [...]}
std::string make_job_json(const TtsConfig& c, const std::vector<SpeakerVoice>& voices, const std::vector<TtsItem>& items);
// Рядок виводу помічника: "GMDR_DONE 3" → {done, 3}; "GMDR_FAIL 3 msg" → {fail, 3, msg} …
struct HelperEvent {
    enum Kind { None, Loading, Ready, Done, Fail, Error } kind = None;
    int         index = -1;
    std::string text;
};
HelperEvent parse_helper_line(const std::string& line);
// Код мови для рушія: OmniVoice і ElevenLabs — ISO 639-1 ("pt-BR" → "pt")
std::string engine_language(const std::string& lang);

} // namespace gmdr::dub
