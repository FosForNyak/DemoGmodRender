#include "settings_catalog.hpp"

#include "../util/i18n.hpp"

#include <map>
#include <type_traits>

namespace gmdr::config {

const char* group_id(SettingGroup g) {
    switch (g) {
    case SettingGroup::Source: return "source";
    case SettingGroup::Game: return "game";
    case SettingGroup::Output: return "output";
    case SettingGroup::Video: return "video";
    case SettingGroup::Motion: return "motion";
    case SettingGroup::Audio: return "audio";
    case SettingGroup::Subtitles: return "subtitles";
    case SettingGroup::Translation: return "translation";
    case SettingGroup::Dubbing: return "dubbing";
    case SettingGroup::Performance: return "performance";
    case SettingGroup::Outputs: return "outputs";
    default: return "application";
    }
}

namespace {

template <class T>
constexpr SettingType type_of() {
    if constexpr (std::is_same_v<T, bool>) return SettingType::Bool;
    else if constexpr (std::is_integral_v<T>) return SettingType::Int;
    else if constexpr (std::is_floating_point_v<T>) return SettingType::Double;
    else return SettingType::String;
}

json::Value to_value(const std::string& v) { return json::Value::string(v); }
json::Value to_value(bool v) { return json::Value::boolean(v); }
json::Value to_value(int v) { return json::Value::number(v); }
json::Value to_value(double v) { return json::Value::number(v); }

// Значення не того типу не записується (false) — налаштування лишається як було
bool from_value(const json::Value& j, std::string& v) {
    if (j.type() != json::Value::Type::String) return false;
    v = j.as_string();
    return true;
}
bool from_value(const json::Value& j, bool& v) {
    if (j.type() != json::Value::Type::Bool) return false;
    v = j.as_bool(v);
    return true;
}
bool from_value(const json::Value& j, int& v) {
    if (j.type() != json::Value::Type::Number) return false;
    v = static_cast<int>(j.as_int(v));
    return true;
}
bool from_value(const json::Value& j, double& v) {
    if (j.type() != json::Value::Type::Number) return false;
    v = j.as_number(v);
    return true;
}

using G = SettingGroup;
struct Meta {
    const char* key;
    G           group;
    const char* label;
    bool        advanced;
};
// Група, підпис (український ключ перекладу) і чи лише в розширеному режимі
const Meta kMeta[] = {
    // ---- Джерело ----
    {"demo_path", G::Source, N_("Демо"), false},
    {"start_tick", G::Source, N_("Початок фрагмента"), false},
    {"end_tick", G::Source, N_("Кінець фрагмента"), false},
    {"markers", G::Source, N_("Позначки"), false},
    // ---- Гра ----
    {"game_renderer", G::Game, N_("Рендерер гри"), false},
    {"game_dir", G::Game, N_("Папка Garry's Mod"), false},
    {"rtx_game_dir", G::Game, N_("Папка GMod RTX"), false},
    {"game_exe", G::Game, N_("Версія гри"), true},
    {"game_window", G::Game, N_("Вікно гри"), false},
    {"hide_hud", G::Game, N_("Сховати HUD"), false},
    {"hide_viewmodel", G::Game, N_("Сховати руки і зброю"), false},
    {"mute_game_sound", G::Game, N_("Вимкнути звук гри в мікшері Windows на час рендеру"), false},
    {"render_width", G::Game, N_("Розмір вікна гри"), true},
    {"render_height", G::Game, N_("Розмір вікна гри"), true},
    {"capture_format", G::Game, N_("Формат кадрів"), true},
    {"jpeg_quality", G::Game, N_("Якість JPEG"), true},
    {"frame_transport", G::Game, N_("Передача кадрів"), true},
    {"quit_game_when_done", G::Game, N_("Закрити гру після рендеру"), true},
    {"high_priority", G::Game, N_("Високий пріоритет гри"), true},
    {"manual_mode", G::Game, N_("Ручний режим (я сам керую записом у грі)"), true},
    {"mute_engine_voice", G::Game, N_("Вимкнути голос усередині гри (рекомендовано)"), true},
    {"extra_commands", G::Game, N_("Додаткові консольні команди"), true},
    {"extra_launch_args", G::Game, N_("Параметри запуску"), true},
    {"menu_delay", G::Game, N_("Затримка меню"), true},
    // ---- Вихід ----
    {"output_path", G::Output, N_("Файл результату"), false},
    {"container", G::Output, N_("Формат файлу"), false},
    {"faststart", G::Output, N_("Швидкий старт MP4 (faststart, для інтернету)"), true},
    {"crash_safe", G::Output, N_("Захист від збою: MP4/MOV пишеться фрагментами"), true},
    {"chapters", G::Output, N_("Розділи у відео з позначок"), false},
    // ---- Відео ----
    {"width", G::Video, N_("Роздільна здатність"), false},
    {"height", G::Video, N_("Роздільна здатність"), false},
    {"fps", G::Video, N_("Частота кадрів (FPS)"), false},
    {"video_codec", G::Video, N_("Відеокодек"), false},
    {"quality", G::Video, N_("Якість"), false},
    {"target_size_mb", G::Video, N_("Розмір файлу"), false},
    {"video_bitrate", G::Video, N_("Бітрейт замість якості"), true},
    {"preset", G::Video, N_("Швидкість кодування"), true},
    {"bit_depth", G::Video, N_("Бітність кольору"), true},
    {"chroma", G::Video, N_("Субдискретизація"), true},
    {"pix_fmt", G::Video, N_("Формат пікселів"), true},
    {"video_options", G::Video, N_("Параметри кодека"), true},
    {"scaler", G::Video, N_("Масштабування"), true},
    {"accurate_color", G::Video, N_("Максимальна точність кольору (повільніше)"), true},
    {"full_range", G::Video, N_("Повний діапазон (0–255 замість 16–235)"), true},
    {"gop_seconds", G::Video, N_("Ключовий кадр кожні"), true},
    // ---- Рух ----
    {"motion_blur", G::Motion, N_("Розмиття руху"), false},
    {"shutter", G::Motion, N_("Кут затвора"), true},
    {"speed", G::Motion, N_("Швидкість відео"), false},
    {"speed_audio", G::Motion, N_("Звук при зміні швидкості"), false},
    // ---- Звук ----
    {"audio", G::Audio, N_("Записувати звук"), false},
    {"audio_codec", G::Audio, N_("Аудіокодек"), true},
    {"audio_bitrate", G::Audio, N_("Бітрейт звуку"), true},
    {"sample_rate", G::Audio, N_("Частота"), true},
    {"game_audio", G::Audio, N_("Звук гри (постріли, кроки, музика...)"), false},
    {"game_volume", G::Audio, N_("Гучність гри"), false},
    {"game_audio_offset", G::Audio, N_("Зсув звуку гри"), true},
    {"voice_mode", G::Audio, N_("Чиї голоси"), false},
    {"voice_selected", G::Audio, N_("Вибрані гравці"), false},
    {"voice_volume", G::Audio, N_("Гучність голосу"), false},
    {"voice_delay", G::Audio, N_("Затримка голосу"), true},
    {"voice_volumes", G::Audio, N_("Гучність окремих гравців"), false},
    {"voice_level", G::Audio, N_("Вирівняти гучність гравців"), false},
    {"voice_denoise", G::Audio, N_("Шумодав для всіх гравців"), false},
    {"voice_denoise_players", G::Audio, N_("Шумодав для окремих гравців"), false},
    {"duck_game", G::Audio, N_("Приглушувати звук гри, коли хтось говорить"), false},
    {"loudness_target", G::Audio, N_("Гучність результату"), false},
    {"mic_file", G::Audio, N_("Файл мікрофона"), false},
    {"mic_offset", G::Audio, N_("Зсув мікрофона"), false},
    {"mic_volume", G::Audio, N_("Гучність мікрофона"), false},
    {"separate_tracks", G::Audio, N_("Окремі звукові доріжки у файлі"), true},
    {"edit_package", G::Audio, N_("Пакет для монтажу (WAV + проєкт XML)"), true},
    // ---- Субтитри і розпізнавання ----
    {"subtitles_srt", G::Subtitles, N_("Субтитри «хто говорить» (.srt поруч із відео)"), false},
    {"speech_subtitles", G::Subtitles, N_("з текстом розмов (розпізнати мовлення)"), false},
    {"speaker_overlay", G::Subtitles, N_("Підписи «хто говорить» прямо на відео"), false},
    {"chat_srt", G::Subtitles, N_("Субтитри з чатом у відео (.srt)"), false},
    {"whisper_language", G::Subtitles, N_("Мова розмов"), false},
    {"whisper_model", G::Subtitles, N_("Модель розпізнавання"), true},
    {"whisper_cli", G::Subtitles, N_("whisper-cli"), true},
    // ---- Переклад ----
    {"dub_languages", G::Translation, N_("Мови перекладу"), false},
    {"translate_subtitles", G::Translation, N_("Перекладені субтитри (.srt для кожної мови)"), false},
    {"translator", G::Translation, N_("Сервіс перекладу"), false},
    {"translator_url", G::Translation, N_("Адреса"), false},
    {"translator_model", G::Translation, N_("Модель"), false},
    {"deepl_key", G::Translation, N_("Ключ API"), false},
    {"google_key", G::Translation, N_("Ключ API"), false},
    {"libre_key", G::Translation, N_("Ключ API"), false},
    {"openai_key", G::Translation, N_("Ключ API"), false},
    // ---- Озвучення ----
    {"dub", G::Dubbing, N_("Озвучити переклад"), false},
    {"dub_outputs", G::Dubbing, N_("Куди озвучення"), false},
    {"dub_audio_format", G::Dubbing, N_("Формат аудіофайлів"), false},
    {"dub_original_volume", G::Dubbing, N_("Оригінальні голоси"), false},
    {"tts_engine", G::Dubbing, N_("Рушій озвучення"), false},
    {"tts_device", G::Dubbing, N_("Пристрій"), false},
    {"tts_python", G::Dubbing, N_("Свій Python"), true},
    {"tts_clone", G::Dubbing, N_("Озвучувати голосом самого гравця (клонування)"), false},
    {"tts_clone_ack", G::Dubbing, N_("Згоду гравців підтверджено"), false},
    {"elevenlabs_key", G::Dubbing, N_("Ключ API"), false},
    {"elevenlabs_model", G::Dubbing, N_("Модель"), false},
    {"elevenlabs_voice", G::Dubbing, N_("Голос для всіх"), true},
    {"voice_library_auto", G::Dubbing, N_("Накопичувати зразки з кожного розпізнаного демо"), false},
    // ---- Продуктивність ----
    {"parallel_games", G::Performance, N_("Копій гри одночасно"), false},
    {"threads", G::Performance, N_("Потоків CPU"), true},
    {"max_pending_frames", G::Performance, N_("Черга кадрів на диску"), true},
    {"keep_temp_files", G::Performance, N_("Залишати тимчасові файли (для діагностики)"), true},
    // ---- Додаткові версії ----
    {"extra_versions", G::Outputs, N_("Ще версії"), false},
};

// Уподобання вікна: на рендер не впливають
bool is_app_preference(const std::string& key) {
    return key.rfind("ui_", 0) == 0 || key == "notify_when_done" || key == "minimize_to_tray" || key == "library_dirs" ||
           key == "dub_template";
}

} // namespace

const std::vector<SettingInfo>& settings_catalog() {
    static const std::vector<SettingInfo> list = [] {
        std::vector<SettingInfo> v;
#define X(name)                                                                                                      \
    v.push_back({SettingId::name, #name, SettingGroup::Application, type_of<decltype(render::RenderSettings::name)>(), \
                 "", false, render::is_secret_field(#name), !is_app_preference(#name)});
        GMDR_SETTINGS_FIELDS(X)
#undef X
        std::map<std::string, size_t> index;
        for (size_t i = 0; i < v.size(); ++i) index[v[i].key] = i;
        for (const auto& m : kMeta) {
            auto it = index.find(m.key);
            if (it == index.end()) continue;
            auto& e = v[it->second];
            e.group = m.group;
            e.label = m.label;
            e.advanced = m.advanced;
        }
        return v;
    }();
    return list;
}

const SettingInfo& setting_info(SettingId id) { return settings_catalog()[static_cast<size_t>(id)]; }

const std::string& setting_key(SettingId id) { return setting_info(id).key; }

std::optional<SettingId> find_setting(std::string_view key) {
    for (const auto& e : settings_catalog())
        if (e.key == key) return e.id;
    return std::nullopt;
}

json::Value get_setting(const render::RenderSettings& s, SettingId id) {
    switch (id) {
#define X(name) \
    case SettingId::name: return to_value(s.name);
        GMDR_SETTINGS_FIELDS(X)
#undef X
    default: return json::Value();
    }
}

bool set_setting(render::RenderSettings& s, SettingId id, const json::Value& v) {
    switch (id) {
#define X(name)           \
    case SettingId::name: \
        return from_value(v, s.name);
        GMDR_SETTINGS_FIELDS(X)
#undef X
    default: return false;
    }
}

} // namespace gmdr::config
