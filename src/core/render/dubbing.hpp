// =============================================================================
//  dubbing.hpp — переклад і озвучення після рендеру.
//
//  Розпізнане мовлення (speech/transcribe) перекладається на вибрані мови
//  (translate), і для кожної мови робиться:
//    • перекладені субтитри <відео>.<мова>.srt;
//    • озвучення (dub/tts): фрази перекладу голосом гравця (клон, якщо користувач
//      підтвердив згоду гравців) або готовими голосами — поверх звуку гри, який
//      під час рендеру пишеться окремим WAV;
//    • куди озвучення: доріжки в основному відео (з мітками мови), окреме відео
//      на кожну мову, окремі аудіофайли (напр. для «Дубляжу» на YouTube).
//
//  Відео вже готове, тож збій перекладу чи озвучення рендер не валить — лише
//  попередження в журналі.
// =============================================================================
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../dub/tts.hpp"
#include "../media/audio_encoder.hpp"
#include "../speech/transcribe.hpp"
#include "../translate/translate.hpp"
#include "../voice/voice_decoder.hpp"
#include "encode_session.hpp"
#include "settings.hpp"

namespace gmdr::render {

// Мови перекладу з налаштувань (відомі, без повторів)
std::vector<std::string> dub_language_list(const RenderSettings& s);
// Чи просили переклад (субтитри чи озвучення) хоч на одну мову
bool translation_requested(const RenderSettings& s);
// Чи потрібні окремі WAV джерел звуку (озвучення кладеться на звук гри без голосів)
bool dub_needs_stems(const RenderSettings& s);
bool dub_output(const RenderSettings& s, const std::string& what);   // "tracks" / "videos" / "audio"
// Тимчасова тека озвучення поруч із відео (<назва>.dub_tmp): окремі WAV джерел, фрази, доріжки
std::filesystem::path dub_work_dir(const RenderSettings& s);

// Налаштування сервісів (ключі — розшифровані)
translate::Config translator_config(const RenderSettings& s);
dub::TtsConfig    tts_config(const RenderSettings& s);
// Поле з ключем сервісу перекладу ("deepl_key"…); порожньо — сервіс без ключа
std::string*       translator_key_field(RenderSettings& s, const std::string& provider);
const std::string* translator_key_field(const RenderSettings& s, const std::string& provider);
// Клонувати голоси: увімкнено й згоду підтверджено
bool clone_allowed(const RenderSettings& s);

// Шаблони публікації: набір виходів під конкретний сервіс
struct PublishTemplate {
    const char* id;
    const char* label;
    const char* hint;
    bool        subtitles;      // перекладені .srt
    const char* outputs;        // dub_outputs
    const char* audio_format;   // dub_audio_format ("" — не змінювати)
    double      loudness;       // LUFS (0 — не змінювати)
};
const std::vector<PublishTemplate>& publish_templates();
const PublishTemplate*              find_template(const std::string& id);
void                                apply_template(RenderSettings& s, const PublishTemplate& t);

struct DubSource {
    std::vector<speech::Line>               lines;      // розшифровка (час демо)
    std::vector<std::string>                keys;       // чиї репліки (увімкнені гравці; порожньо — усіх)
    std::vector<const voice::SpeakerTrack*> speakers;   // доріжки гравців (зразки для клонування)
    double                                  origin = 0;     // секунда демо першого кадру відео
    double                                  duration = 0;   // тривалість відео, с
    std::vector<EncodeSession::StemFile>    stems;          // окремі WAV джерел (від першого кадру відео)
    media::AudioEncoderSettings             main_audio;     // звук основного відео (для доріжок у ньому)
};
using StageFn = std::function<void(const std::string& what, double fraction)>;

// Переклад і озвучення; повертає створені файли. Проблеми — у журнал.
std::vector<std::string> make_translations(const RenderSettings& s, const DubSource& src, const StageFn& stage,
                                           const std::atomic<bool>* cancel);

// Бібліотека голосів: додати зразки гравців (лише зі SteamID) з розшифровки демо.
// Повертає скільки зразків додано.
int collect_voice_samples(const RenderSettings& s, const std::vector<const voice::SpeakerTrack*>& speakers,
                          const speech::Transcript& t);

// Переклад розшифровки демо без рендеру (кнопка «Перекласти субтитри»): <демо>.<мова>.srt
// поруч із відео чи демо. origin/duration — відрізок; повертає створені файли.
std::vector<std::string> translate_transcript_files(const RenderSettings& s, const std::vector<speech::Line>& lines,
                                                    const std::vector<std::string>& keys, double origin, double duration,
                                                    const std::string& base_path, const StageFn& stage,
                                                    const std::atomic<bool>* cancel, std::string* error);

} // namespace gmdr::render
