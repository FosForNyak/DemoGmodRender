// =============================================================================
//  app.cpp — інтерфейс програми GMod Demo Render (Dear ImGui): запуск, дії,
//  розкладка панелей, меню й попапи. Вкладки налаштувань і панелі — в
//  app_tab_*.cpp і app_panels.cpp, віджети в стилі Adobe — в ui_widgets.cpp.
//
//  Dear ImGui — "immediate mode" бібліотека: весь інтерфейс заново описується
//  кожен кадр звичайним кодом (if (ImGui::Button(...)) { ... }). Тому тут
//  немає класів кнопок чи подій — лише функції draw_*().
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/media/ffmpeg_util.hpp"
#include "core/media/muxer.hpp"
#include "core/media/video_encoder.hpp"
#include "core/render/report.hpp"
#include "core/util/file_assoc.hpp"
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"
#include "core/game/audio_mute.hpp"
#include "core/game/rtx.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>

namespace gmdr::gui {
namespace {
std::string now_time() {
    const std::time_t t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return buf;
}
} // namespace
} // namespace gmdr::gui

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

// =============================== Ініціалізація ===================================
App::App() = default;

App::~App() {
    if (gpu_thread_.joinable()) gpu_thread_.join();
    if (job_ && job_->running()) {
        job_->kill();
        job_->wait();
    }
    if (analyze_job_) {
        analyze_job_->cancel();
        analyze_job_->wait();
    }
    save_settings_now();
    if (preview_tex_) platform_destroy_texture(preview_tex_);
    platform_set_taskbar_progress(TaskbarState::None, 0);
    if (log_sink_id_) remove_log_sink(log_sink_id_);
}

void App::init(const std::vector<std::string>& args) {
    // ---- журнал: у вікно і у файл поруч з програмою ----
    const fs::path log_path = app_data_dir() / "gmdr_log.txt";
    if (file_size_or_zero(log_path) > 8ull * 1024 * 1024) {   // не даємо журналу рости без кінця
        std::error_code rec;
        fs::rename(log_path, app_data_dir() / "gmdr_log.old.txt", rec);
        if (rec) remove_file_quiet(log_path);
    }
    log_file_.open(log_path, std::ios::app);
    set_min_log_level(LogLevel::Debug);
    log_sink_id_ = add_log_sink([this](LogLevel l, const std::string& text) {
        std::lock_guard lock(log_mutex_);
        const std::string t = now_time();
        if (log_file_) log_file_ << t << " [" << log_level_name(l) << "] " << text << "\n" << std::flush;
        log_.push_back({l, t, text});
        while (log_.size() > 5000) log_.pop_front();
        log_scroll_to_bottom_ = true;
    });
    media::install_ffmpeg_log_bridge(AV_LOG_ERROR);

    // ---- шрифти з кирилицею + тема в стилі Adobe ----
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // розташування вікон не зберігаємо
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigDragClickToInputText = true;   // клік по «гарячому» значенню — ввести число, як у Premiere
    ui::apply_theme(platform_dpi_scale());
    // Звичайний і напівжирний (заголовки панелей, секції, кнопки дій); символи ✓ ✗ ▶ тощо —
    // з резервного шрифту, у основному їх може не бути
    auto load_font = [&](const std::vector<std::string>& candidates) -> ImFont* {
        ImFont* font = nullptr;
        for (const auto& f : candidates) {
            std::error_code ec;
            if (fs::exists(path_from_utf8(f), ec) && (font = io.Fonts->AddFontFromFileTTF(f.c_str(), 15.0f))) break;
        }
        if (!font) return nullptr;
        for (const auto& f : ui_symbol_font_candidates()) {
            std::error_code ec;
            if (!fs::exists(path_from_utf8(f), ec)) continue;
            ImFontConfig cfg;
            cfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(f.c_str(), 15.0f, &cfg);
            break;
        }
        return font;
    };
    ImFont* regular = load_font(ui_font_candidates());
    if (!regular) regular = io.Fonts->AddFontDefault();
    ui::set_fonts(regular, load_font(ui_bold_font_candidates()));

    // ---- налаштування ----
    settings_path_ = path_to_utf8(app_data_dir() / "gmdr_settings.json");
    std::error_code ec;
    std::string settings_err;
    const bool settings_ok = fs::exists(path_from_utf8(settings_path_), ec) && render::load_settings(s_, settings_path_, &settings_err);
    // Мова інтерфейсу — одразу після налаштувань, до першого тексту (змінюється лише з перезапуском)
    set_ui_language(s_.ui_language);
    if (settings_ok) log_info("{}", trf("Налаштування завантажено"));
    else if (!settings_err.empty()) log_warn("{}", trf("Не вдалося прочитати налаштування: {}", settings_err));
    whole_demo_ = s_.start_tick <= 0 && s_.end_tick <= 0;
    platform_set_minimize_to_tray(s_.minimize_to_tray);
    load_queue();
    // Для автотестів: сповіщення і відлік після рендеру — без самого рендеру
    // (разом із GMDR_TEST_POWER_DRYRUN=1, щоб нічого не вимкнулось)
    if (const char* a = std::getenv("GMDR_TEST_AFTER_DONE"); a && std::getenv("GMDR_TEST_POWER_DRYRUN")) {
        if (auto pa = parse_power_action(a)) after_done_ = *pa;
        on_job_finished(render::JobState::Succeeded, tr("Готово!"), tr("Відео збережено:\nтест.mp4"), false);
    }

    log_info("{}", trf("GMod Demo Render {} — рендер демо Garry's Mod у відео", GMDR_VERSION));
    log_info("FFmpeg: libavcodec {}.{}.{}", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);
    refresh_codec_lists();
    // Старі налаштування: копію GMod RTX вказано як звичайну папку гри. Тепер у неї своє поле
    // і режим «RTX», а звичайна гра знаходиться автоматично.
    if (s_.rtx_game_dir.empty() && !s_.game_dir.empty())
        if (auto g = game::gmod_from_dir(path_from_utf8(s_.game_dir)); g && g->valid() && game::is_rtx_install(*g)) {
            log_info("{}", trf("Папку {} перенесено в режим RTX (це копія GMod RTX)", s_.game_dir));
            s_.rtx_game_dir = s_.game_dir;
            s_.game_dir.clear();
            s_.game_exe.clear();
            s_.rtx = true;
            mark_dirty();
        }
    detect_gmod(false);
    detect_rtx_launcher();
    start_gpu_probe();
    if (game::game_audio_mute_pending())
        log_warn("{}", trf("Минулий рендер перервався, і звук Garry's Mod міг лишитися вимкненим у мікшері гучності Windows. "
                 "Програма ввімкне його під час наступного рендеру (або ввімкніть його в мікшері вручну)."));

    // Рендер, урваний збоєм програми чи ПК, — запропонувати дописати
    refresh_resume_offer();
    if (resume_offer_) open_resume_popup_ = true;

    // Демо з командного рядка (перетягнули на .exe)
    for (size_t i = 1; i < args.size(); ++i)
        if (ends_with_i(args[i], ".dem")) {
            load_demo(args[i]);
            break;
        }
}

void App::refresh_codec_lists() {
    video_codecs_.clear();
    audio_codecs_.clear();
    for (const auto& k : kVideoCodecs)
        if (avcodec_find_encoder_by_name(k.name)) video_codecs_.push_back({k.name, tr(k.label), k.gpu, tr(k.group)});
    for (const auto& k : kAudioCodecs)
        if (avcodec_find_encoder_by_name(k.name)) audio_codecs_.push_back({k.name, tr(k.label), false, ""});
}

void App::start_gpu_probe() {
    if (gpu_probe_running_) return;
    if (gpu_thread_.joinable()) gpu_thread_.join();
    gpu_probe_running_ = true;
    std::vector<std::string> names;
    for (const auto& c : video_codecs_)
        if (c.gpu) names.push_back(c.name);
    {
        std::lock_guard lock(gpu_mutex_);
        for (const auto& n : names) gpu_status_[n] = -1;
    }
    gpu_thread_ = std::thread([this, names] {
        // Пробне відкриття кожного GPU-кодека (помилки FFmpeg під час перевірки приховуємо)
        const int old_level = av_log_get_level();
        av_log_set_level(AV_LOG_QUIET);
        int ok_count = 0;
        for (const auto& n : names) {
            media::VideoEncoderSettings vs;
            vs.codec = n;
            vs.width = 1280;
            vs.height = 720;
            media::VideoEncoder enc;
            std::string err;
            const bool ok = enc.open(vs, 1280, 720, false, &err);
            ok_count += ok;
            std::lock_guard lock(gpu_mutex_);
            gpu_status_[n] = ok ? 1 : 0;
        }
        av_log_set_level(old_level);
        gpu_probe_running_ = false;
        log_info("{}", trf("Перевірка відеокарти: доступно GPU-кодеків — {}", ok_count));
    });
}

// Копія гри для вибраного режиму: звичайна (папка або автопошук через Steam) чи RTX (своя
// папка або та, що в налаштуваннях RTXLauncher; поле папки тоді лишається порожнім).
void App::detect_gmod(bool force) {
    gmod_.reset();
    std::vector<std::string> log;
    if (s_.rtx) {
        if (force) s_.rtx_game_dir.clear();
        gmod_ = render::locate_game(s_, &log);
    } else {
        if (!s_.game_dir.empty() && !force) gmod_ = game::gmod_from_dir(path_from_utf8(s_.game_dir));
        if (!gmod_) {
            gmod_ = game::detect_gmod(&log);
            if (gmod_) s_.game_dir = path_to_utf8(gmod_->root);
        }
    }
    for (const auto& l : log) log_debug("{}", l);
    if (gmod_ && gmod_->valid()) {
        driver_state_ = game::driver_state(*gmod_);
        gmod_status_ = trf("Знайдено: {}", path_to_utf8(gmod_->root));
        log_info("{}: {}", s_.rtx ? "GMod RTX" : "Garry's Mod", path_to_utf8(gmod_->root));
        render::recover_leftovers(*gmod_);
    } else {
        gmod_.reset();
        gmod_status_ = s_.rtx ? tr("Копію GMod RTX не знайдено — встановіть її через RTXLauncher або вкажіть папку")
                              : tr("Garry's Mod не знайдено — вкажіть папку гри вручну");
        log_warn("{}", gmod_status_);
    }
}

// ================================= Дії ============================================
void App::load_demo(const std::string& path_in) {
    player_.stop();
    playing_key_.clear();
    // Повний шлях з "рідними" розділювачами (на Windows — "\"), щоб і шлях до
    // відео поруч виглядав охайно.
    std::error_code aec;
    fs::path demo_fs = fs::absolute(path_from_utf8(path_in), aec);
    if (aec) demo_fs = path_from_utf8(path_in);
    const std::string path = path_to_utf8(demo_fs.make_preferred());
    if (job_running()) {
        log_warn("{}", trf("Зачекайте завершення рендеру"));
        return;
    }
    if (analyze_job_ && analyze_job_->running()) analyze_job_->cancel();
    if (analyze_job_) analyze_job_->wait();
    analysis_.reset();
    voices_.reset();
    if (preview_tex_) {   // монітор покаже нове демо, а не останній кадр минулого рендеру
        platform_destroy_texture(preview_tex_);
        preview_tex_ = 0;
        preview_w_ = preview_h_ = 0;
    }
    s_.demo_path = path;
    const std::string ext = current_container();
    if (is_image_container(ext)) {
        const fs::path p = path_from_utf8(path);
        s_.output_path = path_to_utf8(p.parent_path() / (path_to_utf8(p.stem()) + "_frames") / ("frame_%06d." + ext));
    } else {
        s_.output_path = render::default_output_path(path, ext);
    }
    analyze_job_ = std::make_unique<render::AnalyzeJob>(path);
    analyze_job_->start();
    mark_dirty();
}

bool App::job_running() const { return job_ && job_->running(); }

void App::check_updates() {
    log_info("{}", trf("Перевіряю оновлення на GitHub ({})...", kUpdateRepo));
    update_future_ = std::async(std::launch::async, [] {
        UpdateResult r;
        r.release = fetch_latest_release(kUpdateRepo, &r.error);
        return r;
    });
}

void App::poll_update_check() {
    if (!update_future_.valid() ||
        update_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    const UpdateResult r = update_future_.get();
    popup_checks_.clear();
    popup_test_ok_ = false;
    popup_is_folder_ = false;
    popup_result_.clear();
    if (r.release && compare_versions(r.release->version, GMDR_VERSION) > 0) {
        popup_title_ = tr("Є нова версія ") + r.release->version;
        popup_text_ = trf("У вас {}. Нова версія опублікована {}.", GMDR_VERSION, r.release->published);
        if (!r.release->notes.empty()) popup_text_ += "\n\n" + r.release->notes;
        popup_result_ = r.release->url;
        log_info("{}", trf("Є нова версія {}: {}", r.release->version, r.release->url));
    } else if (r.release) {
        popup_title_ = tr("Оновлень немає");
        popup_text_ = trf("У вас остання версія ({}).", GMDR_VERSION);
        log_info("{}", trf("Оновлень немає (остання — {})", r.release->version));
    } else {
        popup_title_ = tr("Не вдалося перевірити оновлення");
        popup_text_ = r.error;
        log_warn("{}", trf("Перевірка оновлень: {}", r.error));
    }
    open_popup_ = true;
}

void App::make_report() {
    const std::string path = save_file_dialog(tr("Зберегти звіт про проблему"), {{tr("ZIP-архів"), "*.zip"}},
                                              render::default_report_name(), "zip");
    if (path.empty()) return;
    save_settings_now();
    render::ReportInput in;
    in.settings_path = settings_path_;
    {
        std::lock_guard lock(gpu_mutex_);
        std::string gpu;
        for (const auto& [name, st] : gpu_status_)
            gpu += std::format("  {}: {}\n", name, st == 1 ? tr("працює") : st == 0 ? tr("не працює") : tr("перевіряється"));
        if (!gpu.empty()) in.extra_text = tr("Перевірка GPU-кодеків у програмі:\n") + gpu;
    }
    std::vector<std::string> contents;
    std::string err;
    popup_checks_.clear();
    popup_test_ok_ = false;
    popup_is_folder_ = false;
    if (render::make_problem_report(path_from_utf8(path), in, &contents, &err)) {
        log_info("{}", trf("Звіт про проблему: {}", path));
        std::string list;
        for (const auto& c : contents) list += "\n  • " + c;
        popup_title_ = tr("Звіт готовий");
        popup_text_ = tr("Файл: ") + path + tr("\n\nУсередині:") + list +
                      tr("\n\nШлях до вашого профілю Windows у текстах замінено на %USERPROFILE%. Архів нікуди не "
                      "надсилається — перегляньте його і передайте сам (Discord, GitHub, пошта).");
        popup_result_ = path;
    } else {
        popup_title_ = tr("Не вдалося створити звіт");
        popup_text_ = err;
        popup_result_.clear();
    }
    open_popup_ = true;
}

void App::start_render(bool test_run) {
    if (!analysis_) return;
    if (!gmod_) {
        popup_title_ = tr("Не знайдено Garry's Mod");
        popup_text_ = tr("Вкажіть папку гри на вкладці «Гра» (…\\steamapps\\common\\GarrysMod).");
        open_popup_ = true;
        return;
    }
    std::error_code ec;
    if (!test_run && !confirm_overwrite_ && !s_.output_path.empty() && s_.output_path.find('%') == std::string::npos &&
        fs::exists(path_from_utf8(s_.output_path), ec)) {
        open_overwrite_popup_ = true;   // питання — у draw_popups (попап відкривається з головного вікна)
        return;
    }
    confirm_overwrite_ = false;
    if (!test_run) fs::create_directories(path_from_utf8(s_.output_path).parent_path(), ec);
    save_settings_now();
    show_game_ = false;
    job_ = std::make_unique<render::RenderJob>(s_, analysis_, voices_, test_run);
    job_reported_ = false;
    job_->start();
}

void App::refresh_resume_offer() {
    auto list = render::pending_resumes();
    if (list.empty()) resume_offer_.reset();
    else resume_offer_ = list.front();
}

// Дописати урваний рендер: налаштування — з запису, частковий файл обрізається до ключового кадру
void App::start_resume() {
    if (!resume_offer_ || job_running()) return;
    save_settings_now();
    show_game_ = false;
    auto job = std::make_unique<render::RenderJob>(resume_offer_->settings, nullptr, nullptr);
    job->set_resume(*resume_offer_);
    job_ = std::move(job);
    job_reported_ = false;
    job_->start();
}

void App::start_encode_frames(const std::string& dir) {
    render::RenderSettings s = s_;
    int64_t count = 0;
    const std::string prefix = render::detect_frame_prefix(path_from_utf8(dir), &count);
    if (count == 0) {
        popup_title_ = tr("Кадрів не знайдено");
        popup_text_ = tr("У вибраній папці немає послідовності кадрів (name0000.tga / .jpg / .png).");
        open_popup_ = true;
        return;
    }
    const std::string ext = is_image_container(current_container()) ? "mp4" : current_container();
    s.output_path = path_to_utf8(path_from_utf8(dir) / ((prefix.empty() ? "video" : prefix) + "." + ext));
    std::error_code ec;
    for (int i = 2; fs::exists(path_from_utf8(s.output_path), ec) && i < 1000; ++i)
        s.output_path = path_to_utf8(path_from_utf8(dir) / std::format("{} ({}).{}", prefix.empty() ? "video" : prefix, i, ext));
    log_info("{}", trf("Кодую {} кадрів «{}» з папки {}", count, prefix, dir));
    job_ = std::make_unique<render::EncodeFramesJob>(s, path_from_utf8(dir), prefix, fs::path(), analysis_, voices_);
    job_reported_ = false;
    job_->start();
}

bool App::job_has_fraction_only() const {
    return dynamic_cast<render::ExportVoicesJob*>(job_.get()) || dynamic_cast<render::TranscribeJob*>(job_.get()) ||
           dynamic_cast<render::DownloadJob*>(job_.get());
}

void App::export_voices(const std::string& dir) {
    if (!voices_ || !analysis_) return;
    if (job_running()) {
        log_warn("{}", trf("Зачекайте завершення поточного завдання"));
        return;
    }
    job_ = std::make_unique<render::ExportVoicesJob>(s_, analysis_, voices_, path_from_utf8(dir));
    job_reported_ = false;
    job_->start();
}

void App::save_settings_now() {
    std::string err;
    if (!render::save_settings(s_, settings_path_, &err)) log_debug("Не вдалося зберегти налаштування: {}", err);
    dirty_ = false;
}

std::string App::current_container() const {
    if (!s_.container.empty()) return s_.container;
    const std::string ext = to_lower(path_to_utf8(path_from_utf8(s_.output_path).extension()));
    return ext.size() > 1 ? ext.substr(1) : "mp4";
}

void App::set_container(const std::string& ext) {
    s_.container.clear();
    fs::path p = s_.output_path.empty() && !s_.demo_path.empty()
                     ? path_from_utf8(render::default_output_path(s_.demo_path, ext))
                     : path_from_utf8(s_.output_path);
    if (p.empty()) p = path_from_utf8("video." + ext);
    const bool was_seq = s_.output_path.find('%') != std::string::npos;
    if (is_image_container(ext)) {
        for (const auto& c : kContainers)
            if (ext == c.ext) s_.video_codec = c.image_codec;
        fs::path base = was_seq ? p.parent_path().parent_path() : p.parent_path();
        std::string stem = was_seq ? path_to_utf8(p.parent_path().filename()) : path_to_utf8(p.stem()) + "_frames";
        s_.output_path = path_to_utf8(base / stem / ("frame_%06d." + ext));
    } else {
        if (was_seq) {
            std::string stem = path_to_utf8(p.parent_path().filename());
            if (stem.size() > 7 && stem.substr(stem.size() - 7) == "_frames") stem.resize(stem.size() - 7);
            p = p.parent_path().parent_path() / (stem + "." + ext);
        } else {
            p.replace_extension("." + ext);
        }
        s_.output_path = path_to_utf8(p);
        const std::string c = s_.video_codec;
        if (c == "png" || c == "tiff" || c == "bmp" || c == "mjpeg") s_.video_codec = "libx264";
        if (ext == "webm" && s_.video_codec != "libvpx-vp9" && s_.video_codec.find("av1") == std::string::npos)
            s_.video_codec = "libvpx-vp9";
        if (ext == "webm" && s_.audio_codec != "libopus" && s_.audio_codec != "libvorbis") s_.audio_codec = "libopus";
        if (ext == "mov" && s_.audio_codec == "libopus") s_.audio_codec = "aac";
    }
    mark_dirty();
}

void App::poll() {
    poll_voice_clip();
    poll_update_check();
    // Аналіз демо завершився?
    if (analyze_job_ && !analyze_job_->running() && !analysis_) {
        if (analyze_job_->state() == render::JobState::Succeeded) {
            analysis_ = analyze_job_->analysis();
            voices_ = analyze_job_->voices();
            if (analysis_) {
                platform_set_title("GMod Demo Render — " + path_to_utf8(path_from_utf8(s_.demo_path).filename()));
                load_markers_for_demo();
                transcript_ = speech::load_transcript(s_.demo_path);
                chat_selected_ = -1;
                if (s_.end_tick > analysis_->last_tick) s_.end_tick = -1;
                playhead_t_ = static_cast<float>(std::max(0, s_.start_tick) * analysis_->tick_interval);
                if (std::getenv("GMDR_TEST_AUTOSTART")) start_render();   // лише для автотестів
            }
        } else if (analyze_job_->state() == render::JobState::Failed) {
            popup_title_ = tr("Не вдалося відкрити демо");
            popup_text_ = analyze_job_->error();
            open_popup_ = true;
            s_.demo_path.clear();
            analyze_job_.reset();
        }
    }
    apply_watch_marks();
    // Рендер завершився?
    if (job_ && !job_->running() && !job_reported_) {
        job_reported_ = true;
        if (dynamic_cast<render::WatchJob*>(job_.get())) {
            // Перегляд у грі: без попапу, позначки вже на шкалі
            if (job_->state() == render::JobState::Failed) {
                popup_title_ = tr("Перегляд у грі: помилка");
                popup_text_ = job_->error();
                popup_result_.clear();
                popup_checks_.clear();
                open_popup_ = true;
            }
            return;
        }
        if (auto* qj = dynamic_cast<render::QueueJob*>(job_.get())) {
            // Черга: підсумок по пунктах; готові прибираємо зі списку
            finish_queue(*qj);
            popup_checks_.clear();
            popup_test_ok_ = false;
            popup_title_ = job_->state() == render::JobState::Succeeded   ? tr("Черга завершена")
                           : job_->state() == render::JobState::Cancelled ? tr("Чергу зупинено")
                                                                          : tr("Черга: помилка");
            popup_text_ = job_->report().empty() ? job_->error() : job_->report();
            popup_result_.clear();
            for (const auto& r : qj->results())
                if (r.state == render::JobState::Succeeded && !r.output.empty()) {
                    popup_result_ = path_to_utf8(path_from_utf8(r.output).parent_path());
                    break;
                }
            popup_is_folder_ = true;
            open_popup_ = true;
            if (gmod_) driver_state_ = game::driver_state(*gmod_);
            platform_flash_window();
            on_job_finished(job_->state(), popup_title_, popup_text_, false);
            return;
        }
        if (auto* tj = dynamic_cast<render::TranscribeJob*>(job_.get())) {
            // Розпізнавання мовлення: репліки одразу з'являються у вкладці «Чат»
            if (tj->state() == render::JobState::Succeeded) {
                transcript_ = tj->transcript();
                log_info("{}", trf("Розпізнано реплік: {} — вони у вкладці «Чат»", transcript_ ? transcript_->lines.size() : 0));
            } else if (tj->state() == render::JobState::Failed) {
                popup_title_ = tr("Розпізнавання мовлення: помилка");
                popup_text_ = tj->error();
                popup_result_.clear();
                popup_checks_.clear();
                open_popup_ = true;
            }
            platform_flash_window();
            return;
        }
        if (auto* dj = dynamic_cast<render::DownloadJob*>(job_.get())) {
            refresh_whisper_status(true);
            if (dj->state() == render::JobState::Failed) {
                popup_title_ = tr("Завантаження: помилка");
                popup_text_ = dj->error();
                popup_result_.clear();
                popup_checks_.clear();
                open_popup_ = true;
            }
            return;
        }
        const auto* render_job = dynamic_cast<render::RenderJob*>(job_.get());
        const bool test = render_job && render_job->is_test_run();
        if (render_job && s_.speech_subtitles) transcript_ = speech::load_transcript(s_.demo_path);   // могли дорозпізнати
        popup_checks_.clear();
        popup_test_ok_ = false;
        switch (job_->state()) {
        case render::JobState::Succeeded:
            popup_title_ = test ? tr("Тестовий прогін") : tr("Готово!");
            popup_is_folder_ = dynamic_cast<render::ExportVoicesJob*>(job_.get()) != nullptr;
            if (test) {
                popup_text_ = job_->report();
                popup_test_ok_ = popup_text_.rfind(tr("Усе працює."), 0) == 0;
            } else {
                popup_text_ = (popup_is_folder_ ? tr("Голоси збережено в папку:\n") : tr("Відео збережено:\n")) + job_->result();
            }
            popup_result_ = job_->result();
            break;
        case render::JobState::Cancelled:
            popup_title_ = tr("Скасовано");
            popup_text_ = dynamic_cast<render::ExportVoicesJob*>(job_.get()) ? tr("Збереження голосів перервано.") : tr("Рендер перервано.");
            popup_result_.clear();
            break;
        default:
            popup_title_ = test ? tr("Тестовий прогін: помилка") : tr("Помилка");
            popup_text_ = job_->error();
            popup_result_.clear();
            if (render_job) popup_checks_ = job_->progress().checks;
            break;
        }
        open_popup_ = true;
        if (gmod_) driver_state_ = game::driver_state(*gmod_);
        platform_flash_window();
        on_job_finished(job_->state(), popup_title_, popup_text_, test);
    }
    // Автозбереження налаштувань
    if (dirty_) {
        save_timer_ += ImGui::GetIO().DeltaTime;
        if (save_timer_ > 1.5f) {
            save_timer_ = 0;
            save_settings_now();
        }
    }
}

void App::on_files_dropped(const std::vector<std::string>& files) {
    for (const auto& f : files) {
        if (ends_with_i(f, ".dem")) {
            load_demo(f);
            return;
        }
        std::error_code ec;
        if (fs::is_directory(path_from_utf8(f), ec)) {
            if (!job_running()) start_encode_frames(f);
            return;
        }
        if (ends_with_i(f, ".wav") || ends_with_i(f, ".mp3") || ends_with_i(f, ".ogg") || ends_with_i(f, ".flac") ||
            ends_with_i(f, ".m4a") || ends_with_i(f, ".opus")) {
            s_.mic_file = f;
            log_info("{}", trf("Файл мікрофона: {}", f));
            mark_dirty();
            return;
        }
    }
    log_warn("{}", trf("Перетягніть файл демо (.dem), папку з кадрами або аудіофайл мікрофона"));
}

bool App::on_close_request() {
    if (job_running()) {
        confirm_quit_ = true;
        return false;
    }
    quit_ = true;
    return true;
}

// ================================ Малювання =======================================
void App::frame() {
    poll();
    update_taskbar();
    handle_shortcuts();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    // Головне вікно — лише «підкладка» темного кольору: у проміжках між панелями видно її
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kGutter);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    draw_menu_bar();
    draw_header_bar();

    const float fs_ = ImGui::GetFontSize();
    const float gap = std::round(fs_ * 0.3f);
    const float status_h = std::round(fs_ * 1.9f);
    const float top = ImGui::GetCursorScreenPos().y;
    const float bottom = vp->WorkPos.y + vp->WorkSize.y - status_h;
    draw_workspace(ImVec2(vp->WorkPos.x + gap, top), ImVec2(vp->WorkSize.x - gap * 2, bottom - top - gap));
    draw_status_bar(ImVec2(vp->WorkPos.x, bottom), ImVec2(vp->WorkSize.x, status_h));
    draw_popups();
    ImGui::End();
}

// Робочий простір як у Premiere Pro: чотири панелі, межі між ними можна тягати
void App::draw_workspace(ImVec2 pos, ImVec2 size) {
    const float fs_ = ImGui::GetFontSize();
    const float gap = std::round(fs_ * 0.3f);
    const float min_w = fs_ * 18, min_h = fs_ * 8;
    float top_h = std::clamp(std::round(split_y_ * size.y), min_h, std::max(min_h, size.y - min_h - gap));
    float lt = std::clamp(std::round(split_x_top_ * size.x), min_w, std::max(min_w, size.x - min_w - gap));
    float lb = std::clamp(std::round(split_x_bottom_ * size.x), min_w, std::max(min_w, size.x - min_w - gap));
    const float bottom_h = size.y - top_h - gap;
    const float by = pos.y + top_h + gap;
    draw_settings_panel(pos, ImVec2(lt, top_h));
    draw_monitor_panel(ImVec2(pos.x + lt + gap, pos.y), ImVec2(size.x - lt - gap, top_h));
    draw_project_panel(ImVec2(pos.x, by), ImVec2(lb, bottom_h));
    draw_timeline_panel(ImVec2(pos.x + lb + gap, by), ImVec2(size.x - lb - gap, bottom_h));
    ui::splitter("##split_top", true, ImVec2(pos.x + lt, pos.y), ImVec2(gap, top_h), &lt, min_w, size.x - min_w - gap);
    ui::splitter("##split_bottom", true, ImVec2(pos.x + lb, by), ImVec2(gap, bottom_h), &lb, min_w, size.x - min_w - gap);
    ui::splitter("##split_y", false, ImVec2(pos.x, pos.y + top_h), ImVec2(size.x, gap), &top_h, min_h, size.y - min_h - gap);
    if (size.x > 0 && size.y > 0) {
        split_x_top_ = lt / size.x;
        split_x_bottom_ = lb / size.x;
        split_y_ = top_h / size.y;
    }
}

void App::open_demo_dialog() {
    if (job_running()) return;
    auto f = open_file_dialog(tr("Відкрити демо Garry's Mod"), {{tr("Демо GMod (*.dem)"), "*.dem"}, {tr("Усі файли"), "*.*"}},
                              s_.demo_path.empty() && gmod_ ? path_to_utf8(gmod_->garrysmod / "demos") : s_.demo_path);
    if (!f.empty()) load_demo(f);
}

void App::set_playhead(double seconds) {
    const double dur = analysis_ ? analysis_->duration_seconds : 0.0;
    playhead_t_ = static_cast<float>(std::clamp(seconds, 0.0, std::max(0.0, dur)));
    focus_timeline(playhead_t_);
}

// Клавіші як у Premiere: I / O — початок і кінець фрагмента в курсорі, M — позначка,
// Shift+I / Shift+O — перейти до них, Home / End — на початок і кінець демо
void App::handle_shortcuts() {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O) && !job_running()) open_demo_dialog();
    const ImGuiIO& io = ImGui::GetIO();
    if (!analysis_ || io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    if (io.KeyCtrl || io.KeyAlt || io.KeySuper) return;
    const double ti = analysis_->tick_interval;
    const int32_t tick = static_cast<int32_t>(std::llround(playhead_t_ / ti));
    if (io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) set_playhead(std::max(0, s_.start_tick) * ti);
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) set_playhead((s_.end_tick > 0 ? s_.end_tick : analysis_->last_tick) * ti);
        return;
    }
    if (!job_running()) {
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) set_fragment_start(tick);
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) set_fragment_end(tick);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) add_marker_at(tick, {});
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) set_playhead(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) set_playhead(analysis_->duration_seconds);
}

void App::draw_menu_bar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu(tr("Файл"))) {
        if (ImGui::MenuItem(tr("Відкрити демо..."), "Ctrl+O", false, !job_running())) open_demo_dialog();
        if (ImGui::MenuItem(tr("Закодувати готові кадри..."), nullptr, false, !job_running())) {
            auto d = pick_folder_dialog(tr("Папка з кадрами startmovie (TGA/JPG) і WAV"));
            if (!d.empty()) start_encode_frames(d);
        }
        if (resume_offer_ && ImGui::MenuItem(tr("Дописати урваний рендер..."), nullptr, false, !job_running()))
            open_resume_popup_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Відкрити папку з відео"), nullptr, false, !s_.output_path.empty()))
            open_path(path_to_utf8(path_from_utf8(s_.output_path).parent_path()));
        if (ImGui::MenuItem(tr("Скинути налаштування"), nullptr, false, !job_running())) {
            const std::string demo = s_.demo_path, out = s_.output_path, gd = s_.game_dir;
            s_ = render::RenderSettings{};
            s_.demo_path = demo;
            s_.output_path = out;
            s_.game_dir = gd;
            mark_dirty();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Вихід"))) {
            if (on_close_request()) quit_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Інструменти"))) {
        if (ImGui::MenuItem(tr("Сповіщати, коли рендер готовий"), nullptr, s_.notify_when_done)) {
            s_.notify_when_done = !s_.notify_when_done;
            mark_dirty();
        }
        // Мова інтерфейсу: назви мов — кожна своєю мовою, щоб знайти свою
        if (ImGui::BeginMenu("Мова / Language")) {
            const std::string cur = s_.ui_language.empty() ? system_ui_language() : s_.ui_language;
            for (const auto& [code, name] : {std::pair<const char*, const char*>{"uk", "Українська"}, {"en", "English"}})
                if (ImGui::MenuItem(name, nullptr, cur == code) && cur != code) {
                    s_.ui_language = code;
                    mark_dirty();
                    popup_title_ = code == std::string("en") ? "Language" : "Мова";
                    popup_text_ = code == std::string("en") ? "The interface will switch to English after the program is restarted."
                                                            : "Інтерфейс стане українським після перезапуску програми.";
                    popup_result_.clear();
                    popup_checks_.clear();
                    open_popup_ = true;
                }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("Згортати в трей"), nullptr, s_.minimize_to_tray)) {
            s_.minimize_to_tray = !s_.minimize_to_tray;
            platform_set_minimize_to_tray(s_.minimize_to_tray);
            mark_dirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tr("Згорнуте вікно зникає з панелі задач, лишається значок біля годинника\n"
                                    "(з прогресом рендеру в підказці). Клік по значку повертає вікно."));
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Перевірити GPU-кодеки ще раз"), nullptr, false, !gpu_probe_running_)) start_gpu_probe();
        if (ImGui::MenuItem(tr("Знайти Garry's Mod автоматично"), nullptr, false, !job_running())) detect_gmod(true);
        ImGui::Separator();
        const bool can = gmod_.has_value() && !job_running();
        if (ImGui::MenuItem(tr("Встановити драйвер у GMod"), nullptr, false, can)) {
            std::string err;
            if (!game::install_driver(*gmod_, &err)) log_error("{}", err);
            driver_state_ = game::driver_state(*gmod_);
        }
        if (ImGui::MenuItem(tr("Видалити драйвер з GMod"), nullptr, false, can)) {
            std::string err;
            if (!game::uninstall_driver(*gmod_, &err)) log_error("{}", err);
            driver_state_ = game::driver_state(*gmod_);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Відкрити журнал (gmdr_log.txt)"))) open_path(path_to_utf8(app_data_dir() / "gmdr_log.txt"));
#ifdef _WIN32
        {
            // Реєстр читаємо лише поки меню відкрите
            const fs::path exe = executable_dir() / "gmdr.exe";
            const bool assoc = dem_association_registered(exe);
            if (ImGui::MenuItem(tr("Відкривати .dem подвійним кліком"), nullptr, assoc)) {
                std::string err;
                if (assoc ? unregister_dem_association(&err) : register_dem_association(exe, &err))
                    log_info("{}", assoc ? tr("Файли .dem більше не відкриваються цією програмою")
                                   : tr("Файли .dem тепер відкриваються в GMod Demo Render (якщо Windows спитає, чим "
                                     "відкривати, — виберіть її). Вимкнути — тут само."));
                else
                    log_error("{}", trf("Не вдалося змінити асоціацію .dem: {}", err));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Лише для вашого облікового запису (HKCU), без прав адміністратора.\n"
                                        "Якщо програма вже відкрита, демо відкриється в ній."));
        }
#endif
        if (ImGui::MenuItem(tr("Відкрити папку програми"))) open_path(path_to_utf8(executable_dir()));
        ImGui::EndMenu();
    }
    // Позначки і фрагмент — у синьому курсорі таймлайну (клавіші як у Premiere)
    if (ImGui::BeginMenu(tr("Позначки"))) {
        const bool have = analysis_ != nullptr;
        const double ti = have ? analysis_->tick_interval : 0.0;
        const int32_t tick = have ? static_cast<int32_t>(std::llround(playhead_t_ / ti)) : 0;
        if (ImGui::MenuItem(tr("Початок фрагмента в курсорі"), "I", false, have && !job_running())) set_fragment_start(tick);
        if (ImGui::MenuItem(tr("Кінець фрагмента в курсорі"), "O", false, have && !job_running())) set_fragment_end(tick);
        if (ImGui::MenuItem(tr("Позначка в курсорі"), "M", false, have)) add_marker_at(tick, {});
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Перейти до початку фрагмента"), "Shift+I", false, have))
            set_playhead(std::max(0, s_.start_tick) * ti);
        if (ImGui::MenuItem(tr("Перейти до кінця фрагмента"), "Shift+O", false, have))
            set_playhead((s_.end_tick > 0 ? s_.end_tick : analysis_->last_tick) * ti);
        if (ImGui::MenuItem(tr("Увесь запис"), nullptr, whole_demo_, have && !job_running())) {
            whole_demo_ = !whole_demo_;
            if (whole_demo_) {
                s_.start_tick = 0;
                s_.end_tick = -1;
            }
            mark_dirty();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Переглянути в грі з курсора"), nullptr, false, have && !job_running())) start_watch(tick);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Вікно"))) {
        if (ImGui::MenuItem(tr("Скинути розкладку панелей"))) {
            split_x_top_ = split_x_bottom_ = 0.42f;
            split_y_ = 0.56f;
        }
        ImGui::Separator();
        const char* project_tabs[] = {tr("Голоси"), tr("Бібліотека"), tr("Чат"), tr("Черга"), tr("Журнал")};
        for (int i = 0; i < IM_ARRAYSIZE(project_tabs); ++i)
            if (ImGui::MenuItem(project_tabs[i], nullptr, project_tab_ == i)) project_tab_ = i;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Довідка"))) {
        if (ImGui::MenuItem(tr("Як це працює"))) show_help_ = true;
        if (ImGui::MenuItem(tr("Перевірити оновлення"), nullptr, false, !update_future_.valid())) check_updates();
        if (ImGui::MenuItem(tr("Зібрати звіт про проблему..."))) make_report();
        if (ImGui::MenuItem(tr("Про програму"))) show_about_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

// Панель налаштувань експорту (як Effect Controls / Export Settings у Premiere): вкладки,
// а знизу — вихідний файл і підсумок налаштувань
void App::draw_settings_panel(ImVec2 pos, ImVec2 size) {
    // Для автотестів інтерфейсу: GMDR_TEST_TAB=0..6 — відкрити вкладку при старті
    // (0 Відео, 1 Звук і голос, 2 Гра, 3 Фрагмент — тут; 4 Чат, 5 Черга, 6 Бібліотека — у нижній панелі)
    static int forced_tab = [] {
        const char* e = std::getenv("GMDR_TEST_TAB");
        return e ? std::atoi(e) : -1;
    }();
    if (forced_tab >= 0) {
        if (forced_tab <= 3) settings_tab_ = forced_tab;
        else project_tab_ = forced_tab == 4 ? 2 : forced_tab == 5 ? 3 : 1;
        forced_tab = -1;
    }
    const std::vector<std::string> tabs = {tr("Відео"), tr("Звук і голос"), tr("Гра"), tr("Фрагмент")};
    const ImGuiStyle& st = ImGui::GetStyle();
    const float footer_h = ImGui::GetFrameHeight() + ImGui::GetTextLineHeight() + st.ItemSpacing.y + st.WindowPadding.y * 2;
    ui::begin_panel("##settings", pos, size, tabs, &settings_tab_, nullptr, footer_h);
    ImGui::BeginDisabled(job_running());
    switch (settings_tab_) {
    case 0: draw_tab_video(); break;
    case 1: draw_tab_audio(); break;
    case 2: draw_tab_game(); break;
    default: draw_tab_range(); break;
    }
    ImGui::EndDisabled();
    // GMDR_TEST_SCROLL=0..1 — прокрутити панель налаштувань (частка від кінця) у перших кадрах
    static const double forced_scroll = [] {
        const char* e = std::getenv("GMDR_TEST_SCROLL");
        return e ? std::atof(e) : -1.0;
    }();
    static int scroll_frames = 60;
    if (forced_scroll >= 0 && scroll_frames > 0) {
        --scroll_frames;
        ImGui::SetScrollY(ImGui::GetScrollMaxY() * static_cast<float>(forced_scroll));
    }
    ui::panel_footer();
    draw_output_footer();
    ui::end_panel();
}

void App::update_taskbar() {
    TaskbarState st = TaskbarState::None;
    double fr = 0;
    if (job_ && job_->running()) {
        const auto p = job_->progress();
        fr = p.fraction;
        st = p.game_paused && p.disk_low ? TaskbarState::Error
             : p.game_paused            ? TaskbarState::Paused
             : p.frames > 0 || job_has_fraction_only() ? TaskbarState::Normal
                                          : TaskbarState::Indeterminate;
    } else if (analyze_job_ && analyze_job_->running()) {
        st = TaskbarState::Normal;
        fr = analyze_job_->progress().fraction;
    }
    // Значок у треї: поки йде рендер (прогрес у підказці) або поки вікно сховане в трей
    const double now = ImGui::GetTime();
    if (now - tray_update_t_ > 1.0) {
        tray_update_t_ = now;
        const bool busy = job_ && job_->running() && !dynamic_cast<render::WatchJob*>(job_.get());
        std::string tip = "GMod Demo Render";
        if (busy) {
            const auto p = job_->progress();
            tip += " — " + p.stage;
            if (p.fraction > 0) tip += std::format(" {:.0f}%", p.fraction * 100);
            if (p.eta >= 0) tip += tr(", залишилось ~") + format_duration(p.eta);
            if (after_done_ != PowerAction::None) tip += std::string(tr("; потім — ")) + power_action_name(after_done_);
        }
        platform_tray(busy || (s_.minimize_to_tray && platform_window_hidden()), tip);
    }
    const int key = static_cast<int>(st) * 1000 + static_cast<int>(fr * 999);
    if (key == last_taskbar_state_) return;
    last_taskbar_state_ = key;
    platform_set_taskbar_progress(st, fr);
}

void App::detect_rtx_launcher() {
    rtx_dir_.clear();
    // Шлях до копії гри RTXLauncher пише у свій settings.xml
    std::vector<std::string> log;
    if (auto g = game::detect_rtx_install(&log)) {
        rtx_dir_ = path_to_utf8(g->root);
        for (const auto& l : log) log_debug("{}", l);
    }
}

std::string App::best_gpu_codec(const char* family) const {
    std::lock_guard lock(gpu_mutex_);
    for (const char* vendor : {"_nvenc", "_amf", "_qsv"}) {
        const std::string name = std::string(family) + vendor;
        auto it = gpu_status_.find(name);
        if (it != gpu_status_.end() && it->second == 1) return name;
    }
    return {};
}

double App::fragment_seconds() const {
    if (!analysis_) return 0;
    const int32_t last = analysis_->last_tick;
    // Тривалість відео: фрагмент демо з урахуванням уповільнення/прискорення
    return std::max(0, (s_.end_tick > 0 ? s_.end_tick : last) - std::max(0, s_.start_tick)) *
           static_cast<double>(analysis_->tick_interval) / (s_.speed > 0 ? s_.speed : 1.0);
}

void App::apply_preset(int index) {
    auto base = [&](int w, int h, const char* fps) {
        s_.width = w;
        s_.height = h;
        s_.fps = fps;
        s_.render_width = s_.render_height = 0;
        s_.quality = -1;
        s_.preset.clear();
        s_.video_bitrate.clear();
        s_.video_options.clear();
        s_.pix_fmt = "auto";
        s_.target_size_mb = 0;
        s_.chroma = 420;
        s_.bit_depth = 8;
        s_.separate_tracks = false;
    };
    switch (index) {
    case 0:   // YouTube 1080p60
        base(1920, 1080, "60");
        set_container("mp4");
        s_.video_codec = "libx264";
        s_.preset = "slow";
        s_.audio_codec = "aac";
        s_.audio_bitrate = "320k";
        break;
    case 1: {   // YouTube 4K60, 10 біт
        base(3840, 2160, "60");
        set_container("mp4");
        const std::string gpu = best_gpu_codec("hevc");
        s_.video_codec = gpu.empty() ? "libx265" : gpu;
        s_.bit_depth = 10;
        s_.audio_codec = "aac";
        s_.audio_bitrate = "320k";
        break;
    }
    case 2:   // Монтаж — ProRes
        base(s_.width, s_.height, s_.fps.c_str());
        set_container("mov");
        s_.video_codec = "prores_ks";
        s_.quality = 3;
        s_.chroma = 422;
        s_.bit_depth = 10;
        s_.audio_codec = "pcm_s24le";
        s_.separate_tracks = true;
        break;
    case 3: case 4: case 5: {   // Discord
        const bool small = index == 3;
        base(small ? 1280 : 1920, small ? 720 : 1080, small ? "30" : "60");
        set_container("mp4");
        s_.video_codec = "libx264";
        s_.preset = "slow";
        s_.audio_codec = "aac";
        s_.audio_bitrate = small ? "96k" : "160k";
        s_.target_size_mb = index == 3 ? 10 : index == 4 ? 50 : 500;
        break;
    }
    case 6:   // Архів без втрат
        base(s_.width, s_.height, s_.fps.c_str());
        set_container("mkv");
        s_.video_codec = "ffv1";
        s_.chroma = 444;
        s_.audio_codec = "flac";
        break;
    default:
        return;
    }
    log_info("{}", trf("Пресет «{}»: {}", tr(kQuickPresets[index].label), tr(kQuickPresets[index].tip)));
    mark_dirty();
}

// ================================== Попапи ========================================
void App::on_job_finished(render::JobState state, const std::string& title, const std::string& text, bool test_run) {
    refresh_resume_offer();   // дописаний рендер свій запис прибрав
    if (s_.notify_when_done && (platform_window_hidden() || after_done_ != PowerAction::None)) {
        std::string body = text.substr(0, text.find("\n\n"));   // перший абзац — без довгих подробиць
        if (body.size() > 220) body = body.substr(0, 217) + "...";
        platform_notify(title, body);
    }
    if (after_done_ == PowerAction::None || test_run) return;
    if (state == render::JobState::Cancelled) {
        log_info("{}", trf("Рендер зупинено вручну — «{}» після завершення скасовано", power_action_name(after_done_)));
        after_done_ = PowerAction::None;
        return;
    }
    // Хвилина, щоб передумати: вікно виходить наперед із відліком і кнопкою «Скасувати»
    power_countdown_ = true;
    power_deadline_ = ImGui::GetTime() + power_countdown_seconds();
    log_info("{}", trf("Після завершення: {} через {} с (можна скасувати)", power_action_name(after_done_), power_countdown_seconds()));
    platform_notify(after_done_ == PowerAction::Shutdown ? tr("ПК вимкнеться за хвилину") : tr("ПК засне за хвилину"),
                    tr("Рендер завершено. Відкрийте GMod Demo Render, щоб скасувати."));
    platform_restore_window();
}

void App::draw_after_done_combo() {
    const float fs_ = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(fs_ * 9);
    const PowerAction opts[] = {PowerAction::None, PowerAction::Shutdown, PowerAction::Sleep};
    const char* labels[] = {tr("нічого не робити"), tr("вимкнути ПК"), tr("сон")};
    if (ui::begin_combo("##afterdone", labels[static_cast<int>(after_done_)])) {
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(labels[i], after_done_ == opts[i])) {
                after_done_ = opts[i];
                if (after_done_ != PowerAction::None)
                    log_info("{}", trf("Коли рендер закінчиться: {} (з хвилиною на скасування)", power_action_name(after_done_)));
            }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Що зробити, коли рендер або вся черга закінчиться (успішно чи з помилкою).\n"
                                "Перед цим — хвилина з кнопкою «Скасувати». Діє лише цього разу, не зберігається."));
}

void App::draw_power_countdown() {
    if (!power_countdown_) return;
    const double left = power_deadline_ - ImGui::GetTime();
    if (!ImGui::IsPopupOpen("##power")) ImGui::OpenPopup("##power");
    if (ImGui::BeginPopupModal("##power", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(ui::bold_font(), ImGui::GetStyle().FontSizeBase * 1.15f);
        ImGui::TextColored(kColWarn, "%s", after_done_ == PowerAction::Shutdown ? tr("Вимкнення ПК") : tr("Сон"));
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::Text(tr("Рендер завершено. %s через %d с."), after_done_ == PowerAction::Shutdown ? tr("ПК вимкнеться") : tr("ПК засне"),
                    std::max(0, static_cast<int>(std::ceil(left))));
        ImGui::Spacing();
        ImGui::Spacing();
        bool now = left <= 0;
        if (ui::pill_button(tr("Скасувати"), ui::Kind::Cta)) {
            log_info("{}", trf("«{}» після рендеру скасовано", power_action_name(after_done_)));
            after_done_ = PowerAction::None;
            power_countdown_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::pill_button(tr("Зараз"), ui::Kind::Negative)) now = true;
        if (now && power_countdown_) {
            power_countdown_ = false;
            ImGui::CloseCurrentPopup();
            save_settings_now();
            std::string err;
            const PowerAction a = after_done_;
            after_done_ = PowerAction::None;
            log_info("{}", trf("Після рендеру: {}", power_action_name(a)));
            if (!do_power_action(a, &err)) log_error("{}", trf("Не вдалося {}: {}", power_action_name(a), err));
        }
        ImGui::EndPopup();
    }
}

// Заголовок діалогу: напівжирний, колір — за змістом
static void dialog_title(const std::string& text, const ImVec4& color) {
    ImGui::PushFont(ui::bold_font(), ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextColored(color, "%s", text.c_str());
    ImGui::PopFont();
    ImGui::Spacing();
}

void App::draw_popups() {
    const float fs_ = ImGui::GetFontSize();
    draw_power_countdown();
    if (open_popup_ && !power_countdown_) {   // під час відліку перед вимкненням — лише він
        ImGui::OpenPopup("##result");
        open_popup_ = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(fs_ * 24, 0), ImVec2(fs_ * 50, fs_ * 40));
    if (ImGui::BeginPopupModal("##result", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        // Заголовки помилок: «…помилка» / «…error» (англійський інтерфейс)
        const bool popup_error = popup_title_.find("омилка") != std::string::npos || popup_title_.find("rror") != std::string::npos;
        dialog_title(popup_title_, popup_title_ == tr("Готово!") ? kColOk : popup_error ? kColErr : ImVec4(1, 1, 1, 1));
        ImGui::PushTextWrapPos(fs_ * 48);
        ImGui::TextUnformatted(popup_text_.c_str());
        ImGui::PopTextWrapPos();
        if (!popup_checks_.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText(tr("Кроки"));
            draw_checks(popup_checks_);
        }
        ImGui::Spacing();
        ImGui::Spacing();
        // Головна дія — синя, решта — контурні
        bool primary_used = false;
        auto action = [&](const char* label) {
            const bool r = ui::pill_button(label, primary_used ? ui::Kind::Secondary : ui::Kind::Cta);
            primary_used = true;
            ImGui::SameLine();
            return r;
        };
        if (popup_test_ok_ && analysis_ && !job_running()) {
            if (action(tr("Почати рендер"))) {
                ImGui::CloseCurrentPopup();
                start_render(false);
            }
        }
        if (!popup_result_.empty()) {
            if (popup_result_.rfind("https://", 0) == 0) {
                if (action(tr("Відкрити сторінку"))) {
                    open_path(popup_result_);
                    ImGui::CloseCurrentPopup();
                }
            } else if (ends_with_i(popup_result_, ".zip")) {
                if (action(tr("Показати в папці"))) {
                    show_in_folder(popup_result_);
                    ImGui::CloseCurrentPopup();
                }
            } else if (popup_is_folder_) {
                if (action(tr("Відкрити папку"))) {
                    open_path(popup_result_);
                    ImGui::CloseCurrentPopup();
                }
            } else {
                if (action(popup_title_.rfind(tr("Тестовий прогін"), 0) == 0 ? tr("Відкрити тестове відео") : tr("Відкрити відео"))) {
                    if (popup_result_.find('%') == std::string::npos) open_path(popup_result_);
                    else open_path(path_to_utf8(path_from_utf8(popup_result_).parent_path()));
                    ImGui::CloseCurrentPopup();
                }
                if (action(tr("Показати в папці"))) {
                    show_in_folder(popup_result_);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        if (ui::pill_button("OK", primary_used ? ui::Kind::Secondary : ui::Kind::Cta, fs_ * 5)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Файл уже є — перезаписати?
    if (open_overwrite_popup_) {
        ImGui::OpenPopup("##overwrite");
        open_overwrite_popup_ = false;
    }
    if (ImGui::BeginPopupModal("##overwrite", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        dialog_title(tr("Перезаписати?"), ImVec4(1, 1, 1, 1));
        ImGui::Text(tr("Файл уже існує:\n%s\n\nПерезаписати?"), s_.output_path.c_str());
        ImGui::Spacing();
        ImGui::Spacing();
        if (ui::pill_button(tr("Так"), ui::Kind::Cta, fs_ * 6)) {
            confirm_overwrite_ = true;
            ImGui::CloseCurrentPopup();
            start_render();
        }
        ImGui::SameLine();
        if (ui::pill_button(tr("Ні"), ui::Kind::Secondary, fs_ * 6)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (open_resume_popup_ && resume_offer_) ImGui::OpenPopup("##resume");
    open_resume_popup_ = false;
    if (ImGui::BeginPopupModal("##resume", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (resume_offer_) {
            const auto& r = *resume_offer_;
            const double fps = parse_rational(r.settings.fps).value_or(Rational{60, 1}).value();
            dialog_title(tr("Дописати урваний рендер?"), ImVec4(1, 1, 1, 1));
            ImGui::PushTextWrapPos(fs_ * 34);
            ImGui::TextWrapped("%s", trf("Минулого разу рендер урвався — програма чи ПК зупинились посеред запису:\n{}\n\n"
                                         "Записано ≈ {} з {}. Програма дорендерить решту і склеїть без перекодування — "
                                         "рендер не почнеться з нуля.",
                                         r.settings.output_path, format_duration(static_cast<double>(r.frames) / fps),
                                         format_duration(r.seconds)).c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::BeginDisabled(job_running());
            if (ui::pill_button(tr("Дорендерити"), ui::Kind::Cta)) {
                start_resume();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::pill_button(tr("Пізніше"), ui::Kind::Secondary)) ImGui::CloseCurrentPopup();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Запитати знову наступного разу (або «Файл → Дописати урваний рендер»)"));
            ImGui::SameLine();
            if (ui::pill_button(tr("Забути"), ui::Kind::Secondary)) {
                render::forget_resume(r.id);
                resume_offer_.reset();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Більше не пропонувати; частковий файл лишиться як є"));
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (confirm_quit_) {
        ImGui::OpenPopup("##quit");
        confirm_quit_ = false;
    }
    if (ImGui::BeginPopupModal("##quit", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        dialog_title(tr("Вийти?"), ImVec4(1, 1, 1, 1));
        ImGui::TextUnformatted(tr("Рендер ще триває. Перервати його і закрити програму?"));
        ImGui::Spacing();
        ImGui::Spacing();
        if (ui::pill_button(tr("Так, вийти"), ui::Kind::Negative)) {
            if (job_) job_->kill();
            quit_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::pill_button(tr("Ні"), ui::Kind::Cta, fs_ * 6)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (show_about_) {
        ImGui::OpenPopup("##about");
        show_about_ = false;
    }
    if (ImGui::BeginPopupModal("##about", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        dialog_title("GMod Demo Render " GMDR_VERSION, kColAccent);
        ImGui::TextUnformatted(tr("Рендер демо-записів Garry's Mod у відео будь-якого формату."));
        ImGui::TextColored(kColDim, tr("Кодування: FFmpeg (libavcodec %d.%d). Інтерфейс: Dear ImGui %s."), LIBAVCODEC_VERSION_MAJOR,
                           LIBAVCODEC_VERSION_MINOR, IMGUI_VERSION);
        ImGui::TextColored(kColDim, tr("Ядер процесора: %u"), std::max(1u, std::thread::hardware_concurrency()));
        ImGui::Spacing();
        if (ui::pill_button("OK", ui::Kind::Cta, fs_ * 6)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (show_help_) {
        ImGui::OpenPopup("##help");
        show_help_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(fs_ * 44, fs_ * 32), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##help", nullptr, ImGuiWindowFlags_NoTitleBar)) {
        dialog_title(tr("Як це працює"), ImVec4(1, 1, 1, 1));
        ImGui::BeginChild("##helptext", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y));
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kColAccent, "%s", tr("Чому потрібна сама гра?"));
        ImGui::TextUnformatted(tr("Файл .dem не містить картинки — лише мережеві пакети гри (рух сутностей, звуки, голос). "
                               "Правильно намалювати їх може тільки рушій GMod з вашими картами, моделями й аддонами."));
        ImGui::TextColored(kColAccent, "%s", tr("Що робить програма"));
        ImGui::BulletText("%s", tr("Розбирає демо: карта, тривалість, гравці, голосовий чат (Steam Voice / Opus)."));
        ImGui::BulletText("%s", tr("Запускає GMod з фіксованим кроком часу (host_framerate): кожен кадр відео — рівно 1/FPS секунди демо, "
                                "незалежно від потужності ПК. Тому FPS і роздільна здатність можуть бути будь-якими."));
        ImGui::BulletText("%s", tr("Невеликий Lua-драйвер у меню GMod вмикає startmovie точно на потрібному тіку і вимикає в кінці."));
        ImGui::BulletText("%s", tr("Кадри (TGA/JPEG) і звук гри забираються одразу, як з'являються, декодуються на всіх ядрах, "
                                "змішуються для motion blur, масштабуються і кодуються FFmpeg (CPU або GPU: NVENC/AMF/QSV)."));
        ImGui::BulletText("%s", tr("Голоси гравців декодуються прямо з демо і накладаються точно за часом. "
                                "Можна додати окремий запис мікрофона."));
        ImGui::TextColored(kColAccent, "%s", tr("Поради"));
        ImGui::BulletText("%s", tr("Перед довгим рендером натисніть «Тест 3 с»: програма перевірить кожен крок і порахує, скільки "
                                "триватиме рендер і скільки важитиме файл."));
        ImGui::BulletText("%s", tr("Фрагмент і позначки — як у Premiere: клацніть по лінійці таймлайну, щоб поставити курсор, "
                                "і натисніть I (початок), O (кінець) або M (позначка)."));
        ImGui::BulletText("%s", tr("Гра працює у фоні (вікно за межами екрана), її звук у мікшері Windows вимкнено — на відео це "
                                "не впливає. Подивитися на гру можна кнопкою «Показати гру»."));
        ImGui::BulletText("%s", tr("Якщо звук гри під час рендеру чути (режим «на екрані»), він грає пришвидшено — це нормально, "
                                "у відео він правильний."));
        ImGui::BulletText("%s", tr("Свій голос у демо: перед записом демо введіть voice_loopback 1."));
        ImGui::BulletText("%s", tr("Демо з сервера програється, лише якщо у вас є ті самі карти й аддони."));
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        if (ui::pill_button(tr("Зрозуміло"), ui::Kind::Cta, fs_ * 8)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace gmdr::gui
