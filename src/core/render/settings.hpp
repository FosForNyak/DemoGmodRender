// =============================================================================
//  settings.hpp — усі налаштування рендеру в одній структурі.
//  Зберігаються у JSON (gmdr_settings.json поруч із програмою).
//
//  Це дані, а не стан інтерфейсу: вікно, CLI, черга й сам рендер працюють з тією
//  самою структурою, а що з чим сумісне, вирішує config::evaluate()
//  (config/constraints.hpp). Опис кожного поля — config::settings_catalog().
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "../util/json.hpp"

namespace gmdr::game {
class GameRenderer;
}

namespace gmdr::render {

// Версія формату налаштувань (поле configuration_version у JSON). Старіші файли мігрують у
// from_json(): 0 — до версій (RTX був bool "rtx").
inline constexpr int kConfigurationVersion = 1;

struct RenderSettings {
    // ---- Вхід ----
    std::string demo_path;

    // ---- Гра ----
    std::string game_dir;                 // папка GarrysMod (порожньо — автопошук)
    std::string game_exe;                 // порожньо — автоматично (64-біт, якщо є)
    int         render_width = 0;         // розмір вікна гри (0 — як вихідне відео)
    int         render_height = 0;
    std::string capture_format = "tga";   // tga (без втрат) / jpg
    // Як кадри йдуть з гри: auto — каналом, без файлів на диску (якщо гра в канал не пише —
    // файлами), pipe — лише каналом, files — файлами в тимчасовій папці (frame_transport.hpp)
    std::string frame_transport = "auto";
    int         jpeg_quality = 95;
    bool        hide_hud = false;
    bool        hide_viewmodel = false;
    std::string extra_commands;           // додаткові консольні команди (кожна з нового рядка)
    std::string extra_launch_args;        // додаткові параметри запуску
    int         max_pending_frames = 90;  // понад цю кількість кадрів на диску гра ставиться на паузу
    bool        quit_game_when_done = true;
    bool        manual_mode = false;      // користувач сам вводить startmovie/endmovie
    bool        mute_engine_voice = true; // вимкнути голос у грі (його додамо самі, чистіше)
    double      menu_delay = 3.0;
    bool        high_priority = true;
    int         parallel_games = 1;       // скільки копій гри рендерять фрагмент частинами одночасно (-multirun)
    std::string game_window = "offscreen";  // де вікно гри: offscreen (за межами екрана) / behind (позаду інших) / normal
    bool        mute_game_sound = true;   // вимкнути звук GMod у мікшері Windows на час рендеру (на відео не впливає)
    std::string game_renderer = "standard";   // чим рендерити: standard / rtx (game/game_renderer.hpp)
    std::string rtx_game_dir;             // папка копії GMod RTX (порожньо — з налаштувань RTXLauncher)

    // ---- Діапазон ----
    int32_t start_tick = 0;
    int32_t end_tick = -1;                // -1 — до кінця

    // ---- Відео ----
    int         width = 1920;
    int         height = 1080;
    std::string fps = "60";
    int         motion_blur = 1;          // під-кадрів на кадр (1 — вимкнено)
    double      speed = 1.0;              // 0.5 — уповільнення вдвічі, 4 — прискорення вчетверо
    std::string speed_audio = "stretch";  // звук при зміні швидкості: stretch (atempo) / mute
    double      shutter = 180.0;
    std::string video_codec = "libx264";
    std::string pix_fmt = "auto";
    int         bit_depth = 8;
    int         chroma = 420;
    int         quality = -1;             // -1 — типове
    std::string video_bitrate;            // "20M" (порожньо — за якістю)
    std::string preset;
    std::string video_options;            // "key=value; key=value"
    std::string scaler = "lanczos";
    bool        accurate_color = false;   // максимальна точність кольору (повільніше)
    bool        full_range = false;
    int         gop_seconds = 2;

    // ---- Звук ----
    bool        audio = true;
    std::string audio_codec = "aac";
    std::string audio_bitrate = "320k";
    int         sample_rate = 48000;
    bool        game_audio = true;
    double      game_volume = 1.0;
    double      game_audio_offset = 0.0;  // с
    std::string voice_mode = "all";       // all / local / others / none / selected
    std::string voice_selected;           // ключі мовців через кому (для selected)
    double      voice_volume = 1.0;
    double      voice_delay = 0.0;        // с
    std::string voice_volumes;            // гучність окремих гравців: "steam:7656...=1.5; slot:3=0" (0 — вимкнено)
    bool        separate_tracks = false;
    std::string mic_file;
    double      mic_offset = 0.0;         // с
    double      mic_volume = 1.0;
    bool        voice_level = false;      // вирівняти гучність гравців (кожного — до однакового рівня за EBU R128)
    bool        voice_denoise = false;    // шумодав і тиша між фразами для всіх гравців
    std::string voice_denoise_players;    // ... або лише для цих (ключі через кому)
    bool        duck_game = false;        // гра стихає, коли хтось говорить
    double      loudness_target = 0;      // гучність загального міксу, LUFS (-14 для YouTube); 0 — не змінювати

    // ---- Вихід ----
    std::string output_path;
    std::string container;                // порожньо — за розширенням
    bool        faststart = true;
    bool        crash_safe = true;        // MP4/MOV фрагментами під час рендеру (файл вціліє при збої)
    bool        subtitles_srt = false;    // субтитри "хто говорить" (.srt поруч із відео)
    bool        speaker_overlay = false;  // підписи "хто говорить" прямо на кадрі
    bool        edit_package = false;     // пакет для монтажу: окремі WAV + проєкт XML (Premiere, DaVinci)
    bool        speech_subtitles = false; // у субтитрах — текст розмов (розпізнане мовлення, whisper.cpp)
    std::string whisper_language = "auto";   // мова розмов: auto, uk, ru, en...
    std::string whisper_model;            // файл моделі ggml-*.bin (порожньо — найкраща знайдена)
    std::string whisper_cli;              // whisper-cli (порожньо — поруч із програмою)
    std::string ui_language;              // мова інтерфейсу: "uk", "en"; порожньо — за мовою або регіоном Windows
    bool        ui_advanced = false;      // режим інтерфейсу: розширений (усі параметри) чи стандартний
    std::string ui_theme = "dark";        // тема: dark / light / system
    std::string ui_accent = "violet";     // колір акценту (ui::accent_presets)
    double      ui_scale = 1.0;           // масштаб інтерфейсу поверх DPI монітора (0.8..2)
    bool        ui_compact = false;       // щільніше: менші відступи
    bool        ui_sidebar_collapsed = false;   // бічна навігація — лише значки
    std::string ui_page;                  // остання відкрита сторінка
    std::string ui_graphics_api = "auto"; // графічний API самого вікна (Qt): auto, d3d11, d3d12, vulkan, opengl, metal, software
    bool        chat_srt = false;         // субтитри з чатом (.srt, або .chat.srt разом із "хто говорить")
    bool        chapters = true;          // позначки у фрагменті -> розділи MP4/MOV/MKV
    std::string markers;                  // позначки поточного демо: рядок на позначку, "тік<TAB>назва"
    double      target_size_mb = 0;       // цільовий розмір файлу в МБ (напр. для Discord), 0 — за якістю
    std::string extra_versions;           // додаткові версії за один рендер: "discord,480p,vertical,master"
    int         threads = 0;
    bool        keep_temp_files = false;

    // ---- Переклад і озвучення (розпізнане мовлення → інші мови) ----
    std::string dub_languages;            // мови перекладу через кому: "en,de,pl"
    bool        translate_subtitles = false;   // перекладені субтитри <відео>.<мова>.srt
    bool        dub = false;              // озвучити переклад (голосом гравця, якщо клонування дозволено)
    std::string dub_outputs = "tracks";   // куди озвучення, через кому: tracks (доріжки в основному відео),
                                          // videos (окреме відео для кожної мови), audio (окремі аудіофайли)
    std::string dub_audio_format = "mp3"; // окремі аудіофайли: mp3 / flac / wav / m4a
    double      dub_original_volume = 0.12;   // оригінальні голоси під озвученням (0 — прибрати)
    std::string dub_template;             // останній вибраний шаблон публікації (для вікна)
    std::string translator = "deepl";     // deepl / google / libre / openai (translate::providers)
    std::string translator_url;           // libre / openai: адреса (порожньо — типова)
    std::string translator_model;         // openai: модель ("qwen2.5:7b", "gpt-4o-mini")
    // API-ключі — лише зашифровані (util/secret.hpp: "dpapi:…"), у звіт про проблему не потрапляють
    std::string deepl_key, google_key, libre_key, openai_key, elevenlabs_key;
    std::string tts_engine = "omnivoice"; // omnivoice (локально) / elevenlabs / fake
    std::string tts_device = "auto";      // omnivoice: auto / cuda / cpu
    std::string tts_python;               // omnivoice: свій Python з пакетом omnivoice (порожньо — встановлений програмою)
    bool        tts_clone = false;        // озвучувати голосом самого гравця (клонування)
    bool        tts_clone_ack = false;    // користувач підтвердив згоду гравців на клонування
    std::string elevenlabs_model = "eleven_multilingual_v2";
    std::string elevenlabs_voice;         // голос ElevenLabs для всіх без клону (порожньо — різні готові)
    bool        voice_library_auto = false;   // накопичувати зразки голосів з кожного розпізнаного демо

    // ---- Програма (вікно) ----
    bool        notify_when_done = true;  // сповіщення Windows, коли рендер чи черга закінчились
    bool        minimize_to_tray = false; // згорнуте вікно — лише значком у треї
    std::string library_dirs;             // бібліотека демо: ваші теки (через ;), тека гри — завжди

    json::Value to_json() const;
    static RenderSettings from_json(const json::Value& j);
};

// Усі поля — один список: серіалізація (settings.cpp) і ідентифікатори налаштувань
// (config::SettingId, config/settings_catalog.hpp) беруться з нього, тож не розійдуться.
#define GMDR_SETTINGS_FIELDS(X)                                                                       \
    X(demo_path) X(game_dir) X(game_exe) X(render_width) X(render_height) X(capture_format) X(frame_transport) \
    X(jpeg_quality) X(hide_hud) X(hide_viewmodel) X(extra_commands) X(extra_launch_args)             \
    X(max_pending_frames) X(quit_game_when_done) X(manual_mode) X(mute_engine_voice) X(menu_delay)   \
    X(high_priority) X(game_window) X(mute_game_sound) X(game_renderer) X(rtx_game_dir) X(start_tick) X(end_tick)              \
    X(width) X(height) X(fps) X(motion_blur) X(shutter)                                              \
    X(video_codec) X(pix_fmt) X(bit_depth) X(chroma) X(quality) X(video_bitrate) X(preset)           \
    X(video_options) X(scaler) X(accurate_color) X(full_range) X(gop_seconds) X(audio) X(audio_codec) \
    X(audio_bitrate) X(sample_rate) X(game_audio) X(game_volume) X(game_audio_offset) X(voice_mode)   \
    X(voice_selected) X(voice_volume) X(voice_delay) X(voice_volumes) X(separate_tracks) X(mic_file) \
    X(mic_offset) X(mic_volume) X(voice_level) X(voice_denoise) X(voice_denoise_players) X(duck_game) \
    X(loudness_target) X(output_path) X(container) X(faststart) X(crash_safe)                         \
    X(subtitles_srt) X(chat_srt) X(chapters) X(markers) X(target_size_mb) X(threads) X(keep_temp_files) \
    X(extra_versions) X(notify_when_done) X(minimize_to_tray) X(speaker_overlay) X(edit_package) X(library_dirs) X(speed) X(speed_audio) \
    X(speech_subtitles) X(whisper_language) X(whisper_model) X(whisper_cli) X(ui_language) X(parallel_games)  \
    X(ui_advanced) X(ui_theme) X(ui_accent) X(ui_scale) X(ui_compact) X(ui_sidebar_collapsed) X(ui_page) X(ui_graphics_api)  \
    X(dub_languages) X(translate_subtitles) X(dub) X(dub_outputs) X(dub_audio_format) X(dub_original_volume) \
    X(dub_template) X(translator) X(translator_url) X(translator_model) X(deepl_key) X(google_key)      \
    X(libre_key) X(openai_key) X(elevenlabs_key) X(tts_engine) X(tts_device) X(tts_python) X(tts_clone) \
    X(tts_clone_ack) X(elevenlabs_model) X(elevenlabs_voice) X(voice_library_auto)

// Поле з API-ключем (зберігається зашифрованим, у звіт не потрапляє)
bool is_secret_field(const std::string& name);

// Рендерер з налаштувань (невідомий id — стандартний; про невідомий скаже config::evaluate)
const game::GameRenderer& renderer_of(const RenderSettings& s);
// Поле з папкою копії гри для вибраного рендерера (game_dir чи rtx_game_dir)
std::string&       renderer_game_dir(RenderSettings& s);
const std::string& renderer_game_dir(const RenderSettings& s);
// JSON налаштувань чи черги без API-ключів (для звіту про проблему); не JSON — як є
std::string redact_secrets_json(const std::string& text);

bool save_settings(const RenderSettings& s, const std::string& path_utf8, std::string* error = nullptr);
bool load_settings(RenderSettings& s, const std::string& path_utf8, std::string* error = nullptr);

} // namespace gmdr::render
