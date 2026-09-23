// =============================================================================
//  app.hpp — головне вікно програми (Dear ImGui).
//
//  Реалізація розкладена по файлах за частинами вікна:
//    app.cpp            — запуск, дії (відкрити демо, рендер...), меню, попапи
//    app_tab_video.cpp  — вкладка «Відео» (кодек, якість, пресети)
//    app_tab_audio.cpp  — вкладка «Звук і голос»
//    app_tab_game.cpp   — вкладка «Гра»
//    app_tab_range.cpp  — вкладка «Фрагмент» і часова шкала голосів
//    app_tab_chat.cpp   — вкладка «Чат», позначки, перегляд демо в грі
//    app_panels.cpp     — інформація про демо, голоси, прев'ю, прогрес, журнал
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
#include "core/game/gmod_install.hpp"
#include "core/game/lua_driver.hpp"
#include "core/render/jobs.hpp"
#include "core/render/markers.hpp"
#include "core/render/settings.hpp"
#include "core/util/log.hpp"
#include "core/util/power.hpp"
#include "core/util/update_check.hpp"
#include "voice_player.hpp"

namespace gmdr::gui {

class App {
public:
    App();
    ~App();

    void init(const std::vector<std::string>& args);
    void frame();   // малювання інтерфейсу (між NewFrame і Render)
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

    // ---- секції інтерфейсу ----
    void draw_menu_bar();
    void draw_demo_bar();
    void draw_settings_tabs();
    void draw_tab_video();
    void draw_tab_audio();
    void draw_tab_game();
    void draw_tab_range();
    void draw_tab_chat();
    void draw_markers_list();
    void draw_timeline(float height);
    void draw_info_panel();
    void draw_voice_table();
    void draw_preview();
    void draw_checks(const std::vector<render::CheckItem>& checks);
    void draw_output_bar();
    void draw_progress();
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
    // Черга рендерів (вкладка «Черга», gmdr_queue.json)
    struct QueueEntry {
        render::RenderSettings s;
        double                 tick_interval = 0;   // для показу часу фрагмента
    };
    void draw_tab_queue();
    void add_to_queue();
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
    std::vector<render::Marker>                     markers_;

    std::vector<QueueEntry>                         queue_;

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

    // Вкладка «Чат»
    std::string chat_search_;
    bool        chat_show_chat_ = true, chat_show_server_ = true, chat_show_joins_ = true, chat_only_range_ = false;
    int         chat_selected_ = -1;

    // Попапи
    std::string popup_title_, popup_text_, popup_result_;
    std::vector<render::CheckItem> popup_checks_;
    bool        popup_is_folder_ = false;   // результат — папка (збережені голоси), а не відео
    bool        popup_test_ok_ = false;     // тестовий прогін пройшов — можна одразу почати рендер
    std::string start_time_buf_, end_time_buf_;   // поля "Точний час" на вкладці "Фрагмент"
    bool        start_time_active_ = false, end_time_active_ = false;
    bool        open_popup_ = false;
    bool        confirm_overwrite_ = false;
    bool        confirm_quit_ = false;
    bool        show_about_ = false;
    bool        show_help_ = false;
    std::string pending_encode_dir_;

    // Допоміжне для віджетів
    std::string custom_codec_;
    bool        whole_demo_ = true;
    int         last_taskbar_state_ = -1;
};

} // namespace gmdr::gui
