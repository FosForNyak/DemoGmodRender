// =============================================================================
//  voice_library.hpp — бібліотека голосів гравців для клонування (озвучення
//  перекладу голосом самого гравця).
//
//  Для кожного гравця (за SteamID) накопичуються зразки: чисті фрази з демо разом із
//  розпізнаним текстом — з кожного нового демо додаються найдовші й найчистіші.
//  Зберігаються в <дані програми>/voices/<steam_…>/: profile.json і clips/*.wav
//  (48 кГц, моно, 16 біт). З найкращих зразків складається еталон для клонування —
//  один WAV до ~12 с і його текст. Гравці без SteamID (боти, «slot:N») не накопичуються.
//
//  Голос — особисті дані: бібліотеку можна переглянути й очистити (сторінка «Переклад
//  і озвучення»), а клонування вмикається лише після підтвердження згоди гравців.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "../speech/transcribe.hpp"
#include "../voice/voice_decoder.hpp"

namespace gmdr::dub {

struct VoiceSample {
    std::string file;          // ім'я файлу в clips/
    std::string text;          // що сказано
    std::string language;      // мова розпізнавання ("uk", "auto" …)
    double      seconds = 0;
    std::string demo;          // з якого демо
    double      demo_time = 0; // де в демо (с)
    int64_t     added = 0;     // unix-час
};

struct VoiceProfile {
    std::string              key;      // "steam:7656…"
    std::vector<std::string> names;    // імена, під якими гравець траплявся (останнє — першим)
    std::vector<VoiceSample> samples;
    std::string              elevenlabs_voice_id;   // клон у ElevenLabs (створюється при першому озвученні)
    double total_seconds() const;
    const std::string& name() const;
};

std::filesystem::path voices_dir();
// Чи накопичується голос цього гравця (лише зі SteamID)
bool persistent_key(const std::string& key);
std::filesystem::path profile_dir(const std::string& key);

std::optional<VoiceProfile> load_profile(const std::string& key);
bool                        save_profile(const VoiceProfile& p, std::string* error);
std::vector<VoiceProfile>   list_profiles();
bool                        delete_profile(const std::string& key);

// Кандидати в зразки з демо: фрази мовця з розпізнаним текстом тривалістю 1.5–12 с (довші
// спершу). lines — розшифровка (беруться репліки саме цього мовця).
struct Candidate {
    double      start = 0, end = 0;   // с демо
    std::string text;
};
std::vector<Candidate> sample_candidates(const std::string& key, const std::vector<speech::Line>& lines);

// Додати зразки з демо: до max_new нових фраз (не повторюючи вже взяті з цього демо), тихі
// й перевантажені відкидаються; усього в профілі — не більше max_total секунд (лишаються
// найдовші). dir — куди писати WAV (тека профілю або тимчасова). Повертає скільки додано.
int collect_samples(VoiceProfile& p, const std::filesystem::path& dir, const voice::SpeakerTrack& track,
                    const std::vector<speech::Line>& lines, const std::string& demo_name, const std::string& language,
                    int max_new = 8, double max_total = 90.0);

// Еталон для клонування: кілька найкращих зразків поспіль (паузи 0.25 с) до max_seconds —
// один WAV у work_dir і його текст.
struct Reference {
    std::filesystem::path wav;
    std::string           text;
    double                seconds = 0;
    std::vector<std::filesystem::path> files;   // окремі зразки (для хмарного клонування)
};
std::optional<Reference> make_reference(const VoiceProfile& p, const std::filesystem::path& samples_dir,
                                        const std::filesystem::path& work_dir, double max_seconds = 12.0);

std::string   profile_to_json(const VoiceProfile& p);
std::optional<VoiceProfile> profile_from_json(const std::string& text);

} // namespace gmdr::dub
