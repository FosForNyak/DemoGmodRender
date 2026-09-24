// =============================================================================
//  settings.hpp — усі налаштування рендеру в одній структурі.
//  Зберігаються у JSON (gmdr_settings.json поруч із програмою).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "../util/json.hpp"

namespace gmdr::render {

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
    bool        rtx = false;              // режим GMod RTX: копія від RTXLauncher (свої параметри запуску, довший розгін)
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

// Поле з API-ключем (зберігається зашифрованим, у звіт не потрапляє)
bool is_secret_field(const std::string& name);
// JSON налаштувань чи черги без API-ключів (для звіту про проблему); не JSON — як є
std::string redact_secrets_json(const std::string& text);

bool save_settings(const RenderSettings& s, const std::string& path_utf8, std::string* error = nullptr);
bool load_settings(RenderSettings& s, const std::string& path_utf8, std::string* error = nullptr);

} // namespace gmdr::render
