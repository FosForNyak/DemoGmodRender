// =============================================================================
//  transcribe.hpp — розпізнавання мовлення гравців (whisper.cpp).
//
//  Локально, без інтернету: зовнішня програма whisper-cli і модель ggml-*.bin.
//  Для кожного мовця збирається "стиснуте" аудіо — лише його фрази (паузи між
//  ними по 1 с), 16 кГц моно; whisper-cli розпізнає його одним запуском, а час
//  кожної репліки переводиться назад у час демо. Результат зберігається для
//  кожного демо окремо (як позначки) і стає рядками чату та субтитрами.
// =============================================================================
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../voice/voice_decoder.hpp"

namespace gmdr::speech {

struct WhisperTools {
    std::filesystem::path cli;     // whisper-cli(.exe)
    std::filesystem::path model;   // ggml-*.bin
};

// Моделі, які програма пропонує завантажити (Hugging Face, ggerganov/whisper.cpp)
struct ModelInfo {
    const char* file;
    const char* label;
    int         size_mb;
};
const std::vector<ModelInfo>& known_models();
std::string model_url(const std::string& file);

// Де шукати whisper-cli і моделі: <тека програми>/whisper, <дані програми>/whisper і
// (для розробки) third_party/whisper угорі від теки програми.
std::vector<std::filesystem::path> whisper_dirs();
// Тека для завантажених моделей (<дані програми>/whisper/models).
std::filesystem::path models_download_dir();
// Явні шляхи мають перевагу (налаштування, --whisper-cli/--whisper-model, змінні
// GMDR_WHISPER_CLI / GMDR_WHISPER_MODEL). Модель — найкраща з наявних.
// partial — що знайшлося, навіть коли чогось бракує (є whisper-cli, але немає моделі).
std::optional<WhisperTools> find_whisper(const std::string& cli_override, const std::string& model_override,
                                         std::string* why, WhisperTools* partial = nullptr);

struct Line {
    double      start = 0, end = 0;   // секунди від тіку 0 демо
    std::string speaker_key;          // "steam:..." / "slot:3"
    std::string speaker;              // ім'я для показу
    std::string text;
};

// Відрізок демо, де мовлення гравця вже розпізнано
struct Coverage {
    std::string key;
    double      from = 0, to = 0;   // с
};

struct Transcript {
    std::string           model;      // ім'я файлу моделі
    std::string           language;   // "auto", "uk"...
    std::vector<Line>     lines;      // за часом
    std::vector<Coverage> covered;
};
// Чи розпізнано мовлення гравця key на всьому відрізку [from, to].
bool covers(const Transcript& t, const std::string& key, double from, double to);
// Додати нові результати: репліки тих самих гравців на тих самих відрізках замінюються.
void merge_transcript(Transcript& dst, const Transcript& fresh);

std::string               transcript_to_json(const Transcript& t);
std::optional<Transcript> transcript_from_json(const std::string& text);
// Розшифровки зберігаються в <дані програми>/transcripts, окремо для кожного демо
// (ім'я файлу + розмір — як позначки).
std::filesystem::path     transcript_path(const std::string& demo_path);
std::optional<Transcript> load_transcript(const std::string& demo_path);
bool                      save_transcript(const std::string& demo_path, const Transcript& t, std::string* error);

// ---- Складники (тестуються окремо) --------------------------------------------------
struct Piece {
    double compact = 0;   // початок у стиснутому аудіо, с
    double demo = 0;      // початок у демо, с
    double length = 0;    // с
};
// Фрази мовця: паузи коротші merge_gap зливаються, фрази коротші min_len відкидаються;
// у стиснутому аудіо між фразами — gap секунд тиші.
// from/to — лише мовлення з цього відрізка демо (с; to < 0 — до кінця).
std::vector<Piece> plan_pieces(const voice::SpeakerTrack& t, double merge_gap = 0.8, double min_len = 0.3,
                               double gap = 1.0, double from = 0, double to = -1);
// Час у стиснутому аудіо → час демо (у паузі між фразами — кінець попередньої фрази).
double compact_to_demo(const std::vector<Piece>& pieces, double t);
// Номер фрази, до якої належить час стиснутого аудіо (-1 — жодної).
int piece_at(const std::vector<Piece>& pieces, double t);

struct RawSegment {
    double      from = 0, to = 0;   // с, у стиснутому аудіо
    std::string text;
};
// Вивід whisper-cli -oj: {"transcription":[{"offsets":{"from":мс,"to":мс},"text":"..."}]}
std::vector<RawSegment> parse_whisper_json(const std::string& json, std::string* error);
// Шум замість мовлення: порожнє, лише розділові знаки, [музика], типові "галюцинації"
// Whisper на тиші ("Субтитры сделал...", "Дякую за перегляд!").
bool is_noise_text(const std::string& text);
// Сегменти whisper → репліки в часі демо (сегмент, що зачепив кілька фраз, обрізається
// кінцем першої — щоб текст не висів на екрані через довгу паузу).
std::vector<Line> segments_to_lines(const std::vector<RawSegment>& segs, const std::vector<Piece>& pieces,
                                    const std::string& key, const std::string& name);

struct Options {
    std::string language = "auto";   // "auto", "uk", "ru", "en"...
    int         threads = 0;         // 0 — усі логічні ядра (до 16)
    double      from = 0, to = -1;   // відрізок демо, с (to < 0 — до кінця)
};
using Progress = std::function<void(double fraction, const std::string& what)>;

// Розпізнати мовлення гравців. speakers — (доріжка, ім'я для показу). work_dir — для
// тимчасових WAV і JSON (прибираються). Повертає розшифровку (порожню — якщо мовлення нема).
std::optional<Transcript> transcribe(const std::vector<std::pair<const voice::SpeakerTrack*, std::string>>& speakers,
                                     const WhisperTools& tools, const Options& opt,
                                     const std::filesystem::path& work_dir, const Progress& progress,
                                     const std::atomic<bool>* cancel, std::string* error);

} // namespace gmdr::speech
