// =============================================================================
//  app.hpp — головне вікно програми (Dear ImGui).
//
//  Вікно: угорі — логотип, меню, поточне демо, режим (стандартний / розширений) і
//  кнопки дій; ліворуч — бічна навігація по сторінках; посередині — сторінка, праворуч
//  від неї монітор (прев'ю), знизу таймлайн (обидва можна сховати); унизу — рядок стану.
//
//  Реалізація розкладена по файлах за частинами вікна:
//    app.cpp               — запуск, дії (відкрити демо, рендер...), меню, попапи
//    app_shell.cpp         — каркас: верхня панель, бічна навігація, розкладка, рядок стану
//    app_page_home.cpp     — «Огляд»: усе про демо і майбутній рендер
//    app_page_video.cpp    — «Відео» (кодек, якість, пресети, вихідний файл)
//    app_page_audio.cpp    — «Звук і голоси»
//    app_page_game.cpp     — «Гра» (стандарт / RTX, рендер у грі)
//    app_page_fragment.cpp — «Фрагмент і позначки»
//    app_page_chat.cpp     — «Чат і мовлення», позначки, перегляд демо в грі
//    app_page_translate.cpp — «Переклад і озвучення» (мови, сервіси, голоси гравців)
//    app_page_library.cpp  — «Бібліотека» демо
//    app_page_queue.cpp    — «Черга» рендерів
//    app_page_settings.cpp — «Налаштування» (вигляд, мова, поведінка)
//    app_timeline.cpp      — таймлайн
//    app_panels.cpp        — монітор, голоси, перевірки, журнал
//    ui_theme.cpp, ui_widgets.cpp — тема й віджети
// =============================================================================
#pragma once

#include <atomic>
#include <deque>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/voice_preview.hpp"
#include "core/demo/library.hpp"
#include "core/dub/voice_library.hpp"
#include "core/game/gmod_install.hpp"
#include "core/game/lua_driver.hpp"
#include "core/render/jobs.hpp"
#include "core/render/markers.hpp"
#include "core/render/settings.hpp"
#include "core/util/log.hpp"
#include "core/util/power.hpp"
#include "core/util/update_check.hpp"
#include "app_ui.hpp"
#include "imgui.h"
#include "voice_player.hpp"

namespace gmdr::gui {

class App {
public:
    App();
    ~App();

    void init(const std::vector<std::string>& args);
    void before_frame();   // перед NewFrame: застосувати змінену тему
    void frame();          // малювання інтерфейсу (між NewFrame і Render)
    void on_files_dropped(const std::vector<std::string>& files);
    bool on_close_request();
    bool wants_quit() const { return quit_; }

private:
    struct LogLine {
        LogLevel    level;
        std::string time;
        std::string text;
    };
    struct CodecEntry {
        std::string name;    // назва енкодера FFmpeg
        std::string label;   // людська назва
        bool        gpu = false;
        std::string group;
    };
    // Часова шкала: відрізки мовлення кожного гравця в секундах (готуються раз на демо)
    struct TimelineLane {
        std::string                         name;
        std::string                         key;
        std::vector<std::pair<float, float>> spans;
    };

    // ---- сторінки (бічна навігація) ----
    enum class Page { Home, Video, Audio, Game, Fragment, Chat, Translate, Library, Queue, Log, Settings, Count };
    struct PageInfo {
        Page        page;
        const char* id;          // для налаштувань і GMDR_TEST_PAGE
        const char* label;       // український ключ перекладу
        ui::Icon    icon;
        bool        advanced;    // лише в розширеному режимі
        bool        media;       // праворуч — монітор, знизу — таймлайн
        bool        fill;        // сторінка сама заповнює висоту (таблиця), без прокрутки
    };
    static const std::vector<PageInfo>& pages();
    const PageInfo& page_info(Page p) const;
    bool page_visible(Page p) const;
    void go_to(Page p);

    // ---- секції інтерфейсу ----
    void draw_top_bar();                             // логотип, меню, демо, режим, кнопки дій
    void draw_menus();
    void draw_sidebar(ImVec2 pos, ImVec2 size);
    void draw_content(ImVec2 pos, ImVec2 size);      // сторінка + монітор + таймлайн
    void draw_page(ImVec2 pos, ImVec2 size);
    void draw_output_card();                         // вихідний файл і підсумок налаштувань
    void draw_monitor_panel(ImVec2 pos, ImVec2 size);
    void draw_monitor_frame(ImVec2 p0, ImVec2 p1);
    bool encoding_preview_pending() const;           // рендер іде, а кадрів для прев'ю ще немає
    void draw_transport();
    void draw_project_panel(ImVec2 pos, ImVec2 size);
    void draw_timeline_panel(ImVec2 pos, ImVec2 size);
    void draw_status_bar(ImVec2 pos, ImVec2 size);
    void handle_shortcuts();                         // I / O / M, Home / End, Ctrl+O
    void open_demo_dialog();
    void set_playhead(double seconds);
    void draw_page_home();
    void draw_page_video();
    void draw_page_audio();
    void draw_page_game();
    void draw_page_fragment();
    void draw_page_chat();
    void draw_page_settings();
    void draw_page_log();
    // Сторінка «Переклад і озвучення»
    void draw_page_translate();
    void draw_translate_languages();
    void draw_translate_outputs();
    void draw_translator_card();
    void draw_voice_engine_card();
    void draw_voice_library_card();
    bool draw_key_field(const char* id, std::string* stored);   // API-ключ: зберігається лише зашифрованим
    void draw_translate_popups();
    void start_translate_only();
    void start_voice_engine(bool install, bool cuda);
    void start_service_check(bool elevenlabs);
    void refresh_voice_library();
    void apply_ui_theme();                           // тема, акцент, масштаб і щільність із налаштувань
    // Розпізнавання мовлення (сторінка «Чат і мовлення»): стан whisper, запуск, завантаження моделі
    void draw_speech_controls();
    void draw_model_popup();
    void refresh_whisper_status(bool force = false);
    void start_transcribe(bool again);
    bool job_has_fraction_only() const;   // збереження голосів, розпізнавання, завантаження
    void draw_markers_list();
    void draw_timeline();
    void draw_voice_table(float max_height = 0);
    void draw_checks(const std::vector<render::CheckItem>& checks);
    void draw_log();
    void draw_popups();

    // ---- дії ----
    void load_demo(const std::string& path);
    void start_render(bool test_run = false);
    void start_encode_frames(const std::string& dir);
    void export_voices(const std::string& dir);
    void detect_gmod(bool force);
    void detect_rtx_launcher();
    void start_gpu_probe();
    void poll();
    void mark_dirty() { dirty_ = true; }
    void save_settings_now();
    void set_language_after_restart(const std::string& code);
    void set_container(const std::string& ext);
    std::string current_container() const;
    void refresh_codec_lists();
    bool job_running() const;
    void apply_preset(int index);
    std::string best_gpu_codec(const char* family) const;   // "hevc" -> hevc_nvenc/amf/qsv, якщо працює
    double fragment_seconds() const;
    void rebuild_timeline();
    // Позначки поточного демо і фрагмент
    void load_markers_for_demo();
    void set_markers(std::vector<render::Marker> m);
    void add_marker_at(int32_t tick, const std::string& title);
    void set_fragment_start(int32_t tick);
    void set_fragment_end(int32_t tick);
    void focus_timeline(double seconds);
    // Перегляд демо в грі (клавіші F9/F11/F6 у грі -> фрагмент і позначки тут)
    void start_watch(int32_t tick);
    void apply_watch_marks();
    void update_taskbar();
    // Прослуховування голосу гравця (уривок з початку фрагмента, з обробкою як у відео)
    void listen_voice(const voice::SpeakerTrack& sp);
    void poll_voice_clip();
    // Черга рендерів (сторінка «Черга», gmdr_queue.json)
    struct QueueEntry {
        render::RenderSettings s;
        double                 tick_interval = 0;   // для показу часу фрагмента
    };
    void draw_page_queue();
    void add_to_queue();
    void add_demo_to_queue(const std::string& demo_path);   // ціле демо з поточними налаштуваннями
    void push_queue_entry(QueueEntry q);
    // Бібліотека демо
    void draw_page_library();
    std::vector<std::string> library_dirs() const;
    void rescan_library();
    void poll_library();
    void start_queue();
    void load_queue();
    void save_queue();
    void finish_queue(const render::QueueJob& q);
    // Звіт про проблему (Довідка → «Зібрати звіт про проблему»)
    void make_report();
    // Кінець рендеру/черги: сповіщення і дія після завершення (вимкнути ПК / сон)
    void on_job_finished(render::JobState state, const std::string& title, const std::string& text, bool test_run);
    void draw_power_countdown();
    void draw_after_done_combo();
    // Довідка → «Перевірити оновлення» (запит у фоні)
    struct UpdateResult {
        std::optional<ReleaseInfo> release;
        std::string                error;
    };
    void check_updates();
    void poll_update_check();

    // ---- стан ----
    render::RenderSettings                          s_;
    std::string                                     settings_path_;
    bool                                            dirty_ = false;
    float                                           save_timer_ = 0;
    bool                                            quit_ = false;

    std::unique_ptr<render::AnalyzeJob>             analyze_job_;
    std::shared_ptr<const demo::DemoAnalysis>       analysis_;
    std::shared_ptr<const voice::VoiceDecodeResult> voices_;
    std::unique_ptr<render::Job>                    job_;
    bool                                            job_reported_ = true;
    std::optional<render::ResumeRecord>             resume_offer_;       // урваний збоєм рендер, який можна дописати
    bool                                            open_resume_popup_ = false;
    bool                                            show_game_ = false;

    std::optional<game::GModInstall>                gmod_;
    game::DriverState                               driver_state_ = game::DriverState::NotInstalled;
    std::string                                     gmod_status_;
    std::string                                     rtx_dir_;   // знайдена копія GMod RTX (RTXLauncher)

    std::mutex                                      log_mutex_;
    std::deque<LogLine>                             log_;
    bool                                            log_scroll_to_bottom_ = true;
    int                                             log_sink_id_ = 0;
    std::ofstream                                   log_file_;
    bool                                            show_debug_log_ = false;

    std::vector<CodecEntry>                         video_codecs_;
    std::vector<CodecEntry>                         audio_codecs_;
    mutable std::mutex                              gpu_mutex_;
    std::map<std::string, int>                      gpu_status_;   // 1 працює, 0 ні, -1 перевіряється
    std::thread                                     gpu_thread_;
    std::atomic<bool>                               gpu_probe_running_{false};

    // Прев'ю кадрів під час рендеру
    uint64_t                                        preview_tex_ = 0;
    uint64_t                                        preview_serial_ = 0;
    int                                             preview_w_ = 0, preview_h_ = 0;

    // Часова шкала
    const voice::VoiceDecodeResult*                 timeline_src_ = nullptr;
    std::vector<TimelineLane>                       timeline_;
    std::vector<uint8_t>                            activity_;     // скільки гравців говорить (кошики по часу)
    float                                           view_t0_ = 0, view_t1_ = 0;   // видимий відрізок, с
    bool                                            tl_selecting_ = false;
    float                                           tl_sel_from_ = 0;
    float                                           tl_ctx_time_ = 0;   // час під курсором для контекстного меню
    float                                           playhead_t_ = 0;    // синій курсор таймлайну, с
    int                                             tl_nav_drag_ = 0;   // смуга масштабу: 1 — зсув, 2/3 — лівий/правий край
    std::vector<render::Marker>                     markers_;

    std::vector<QueueEntry>                         queue_;
    std::vector<demo::LibraryEntry>                 library_;
    std::future<std::vector<demo::LibraryEntry>>    library_future_;
    bool                                            library_scanned_ = false;
    std::string                                     library_search_;

    // Після завершення рендеру/черги (лише на цей запуск програми, не зберігається)
    PowerAction                                     after_done_ = PowerAction::None;
    bool                                            power_countdown_ = false;
    double                                          power_deadline_ = 0;   // ImGui::GetTime()
    double                                          tray_update_t_ = -10;
    std::future<UpdateResult>                       update_future_;

    // Прослуховування голосу
    VoicePlayer                                     player_;
    std::future<audio::VoiceClip>                   clip_future_;
    std::string                                     clip_key_;      // чий уривок готується
    std::string                                     playing_key_;   // чий уривок грає

    // Сторінка «Чат і мовлення»
    std::string chat_search_;
    bool        chat_show_chat_ = true, chat_show_server_ = true, chat_show_joins_ = true, chat_only_range_ = false;
    int         chat_selected_ = -1;
    bool        chat_show_speech_ = true;
    // Розшифровка розмов поточного демо (speech::load_transcript)
    std::optional<speech::Transcript> transcript_;
    bool                              whisper_ok_ = false;
    std::string                       whisper_status_;
    double                            whisper_checked_at_ = -100;
    bool                              open_model_popup_ = false;
    int                               model_choice_ = 0;

    // Сторінка «Переклад і озвучення»
    std::vector<dub::VoiceProfile>     voice_profiles_;
    bool                               voice_profiles_loaded_ = false;
    std::map<std::string, std::string> key_edit_;         // поле ключа → що вводять (до збереження)
    std::map<std::string, bool>        key_editing_;
    int                                nvidia_gpu_ = -1;  // -1 — ще не перевіряли
    std::string                        service_status_;   // результат останньої перевірки сервісу
    bool                               service_status_ok_ = false;
    bool                               open_consent_popup_ = false, consent_for_library_ = false, consent_checked_ = false;
    bool                               open_engine_popup_ = false, engine_cuda_ = true;
    bool                               open_clear_voices_popup_ = false;

    // Попапи
    std::string popup_title_, popup_text_, popup_result_;
    std::vector<render::CheckItem> popup_checks_;
    bool        popup_is_folder_ = false;   // результат — папка (збережені голоси), а не відео
    bool        popup_test_ok_ = false;     // тестовий прогін пройшов — можна одразу почати рендер
    std::string start_time_buf_, end_time_buf_;   // поля "Точний час" на сторінці "Фрагмент і позначки"
    bool        start_time_active_ = false, end_time_active_ = false;
    bool        open_popup_ = false;
    bool        confirm_overwrite_ = false;
    bool        confirm_quit_ = false;
    void        start_resume();
    void        refresh_resume_offer();
    bool        show_about_ = false;
    bool        show_help_ = false;
    bool        open_overwrite_popup_ = false;
    bool        out_editing_ = false, out_focus_ = false;   // вихідний файл: шлях вводять вручну (олівець)

    // Розкладка («Вигляд → Скинути розкладку»): частка ширини сторінки поруч із монітором і
    // частка висоти над таймлайном; що показано
    Page        page_ = Page::Home;
    float       split_x_ = 0.56f, split_y_ = 0.64f;
    bool        show_monitor_ = true, show_timeline_ = true;
    float       dpi_ = 1.0f;
    bool        theme_dirty_ = false;   // тему змінено — застосувати перед наступним кадром

    // Допоміжне для віджетів
    std::string custom_codec_;
    bool        whole_demo_ = true;
    int         last_taskbar_state_ = -1;
};

} // namespace gmdr::gui
