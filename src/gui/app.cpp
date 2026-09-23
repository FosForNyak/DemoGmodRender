// =============================================================================
//  app.cpp — інтерфейс програми GMod Demo Render (Dear ImGui): запуск, дії,
//  меню й попапи. Вкладки налаштувань і панелі — в app_tab_*.cpp і app_panels.cpp.
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
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"
#include "core/game/audio_mute.hpp"
#include "core/game/rtx.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"

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

    // ---- шрифт з кирилицею + стиль ----
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // розташування вікон не зберігаємо
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const float dpi = platform_dpi_scale();
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 7);
    style.WindowPadding = ImVec2(12, 10);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.155f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.36f, 0.62f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.45f, 0.76f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.36f, 0.62f, 0.55f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.20f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.30f, 0.62f, 0.98f, 1.0f);
    style.ScaleAllSizes(dpi);
    style.FontScaleDpi = dpi;
    style.FontSizeBase = 17.0f;
    bool font_ok = false;
    for (const auto& f : ui_font_candidates()) {
        std::error_code ec;
        if (fs::exists(path_from_utf8(f), ec) && io.Fonts->AddFontFromFileTTF(f.c_str(), 17.0f)) {
            font_ok = true;
            break;
        }
    }
    if (!font_ok) io.Fonts->AddFontDefault();
    // Символи ✓ ✗ ▶ тощо — з резервного шрифту (в основному їх може не бути)
    for (const auto& f : ui_symbol_font_candidates()) {
        std::error_code ec;
        if (!fs::exists(path_from_utf8(f), ec)) continue;
        ImFontConfig cfg;
        cfg.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(f.c_str(), 17.0f, &cfg);
        break;
    }

    // ---- налаштування ----
    settings_path_ = path_to_utf8(app_data_dir() / "gmdr_settings.json");
    std::error_code ec;
    if (fs::exists(path_from_utf8(settings_path_), ec)) {
        std::string err;
        if (render::load_settings(s_, settings_path_, &err)) log_info("Налаштування завантажено");
        else log_warn("Не вдалося прочитати налаштування: {}", err);
    }
    whole_demo_ = s_.start_tick <= 0 && s_.end_tick <= 0;
    load_queue();

    log_info("GMod Demo Render {} — рендер демо Garry's Mod у відео", GMDR_VERSION);
    log_info("FFmpeg: libavcodec {}.{}.{}", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);
    refresh_codec_lists();
    detect_gmod(false);
    detect_rtx_launcher();
    start_gpu_probe();
    if (game::game_audio_mute_pending())
        log_warn("Минулий рендер перервався, і звук Garry's Mod міг лишитися вимкненим у мікшері гучності Windows. "
                 "Програма ввімкне його під час наступного рендеру (або ввімкніть його в мікшері вручну).");

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
        if (avcodec_find_encoder_by_name(k.name)) video_codecs_.push_back({k.name, k.label, k.gpu, k.group});
    for (const auto& k : kAudioCodecs)
        if (avcodec_find_encoder_by_name(k.name)) audio_codecs_.push_back({k.name, k.label, false, ""});
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
        log_info("Перевірка відеокарти: доступно GPU-кодеків — {}", ok_count);
    });
}

void App::detect_gmod(bool force) {
    gmod_.reset();
    if (!s_.game_dir.empty() && !force) gmod_ = game::gmod_from_dir(path_from_utf8(s_.game_dir));
    if (!gmod_) {
        std::vector<std::string> log;
        gmod_ = game::detect_gmod(&log);
        for (const auto& l : log) log_debug("{}", l);
        if (gmod_) s_.game_dir = path_to_utf8(gmod_->root);
    }
    if (gmod_ && gmod_->valid()) {
        driver_state_ = game::driver_state(*gmod_);
        gmod_status_ = std::format("Знайдено: {}", path_to_utf8(gmod_->root));
        log_info("Garry's Mod: {}", path_to_utf8(gmod_->root));
        render::recover_leftovers(*gmod_);
    } else {
        gmod_.reset();
        gmod_status_ = "Garry's Mod не знайдено — вкажіть папку гри вручну";
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
        log_warn("Зачекайте завершення рендеру");
        return;
    }
    if (analyze_job_ && analyze_job_->running()) analyze_job_->cancel();
    if (analyze_job_) analyze_job_->wait();
    analysis_.reset();
    voices_.reset();
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

void App::make_report() {
    const std::string path = save_file_dialog("Зберегти звіт про проблему", {{"ZIP-архів", "*.zip"}},
                                              render::default_report_name(), "zip");
    if (path.empty()) return;
    save_settings_now();
    render::ReportInput in;
    in.settings_path = settings_path_;
    {
        std::lock_guard lock(gpu_mutex_);
        std::string gpu;
        for (const auto& [name, st] : gpu_status_)
            gpu += std::format("  {}: {}\n", name, st == 1 ? "працює" : st == 0 ? "не працює" : "перевіряється");
        if (!gpu.empty()) in.extra_text = "Перевірка GPU-кодеків у програмі:\n" + gpu;
    }
    std::vector<std::string> contents;
    std::string err;
    popup_checks_.clear();
    popup_test_ok_ = false;
    popup_is_folder_ = false;
    if (render::make_problem_report(path_from_utf8(path), in, &contents, &err)) {
        log_info("Звіт про проблему: {}", path);
        std::string list;
        for (const auto& c : contents) list += "\n  • " + c;
        popup_title_ = "Звіт готовий";
        popup_text_ = "Файл: " + path + "\n\nУсередині:" + list +
                      "\n\nШлях до вашого профілю Windows у текстах замінено на %USERPROFILE%. Архів нікуди не "
                      "надсилається — перегляньте його і передайте сам (Discord, GitHub, пошта).";
        popup_result_ = path;
    } else {
        popup_title_ = "Не вдалося створити звіт";
        popup_text_ = err;
        popup_result_.clear();
    }
    open_popup_ = true;
}

void App::start_render(bool test_run) {
    if (!analysis_) return;
    if (!gmod_) {
        popup_title_ = "Не знайдено Garry's Mod";
        popup_text_ = "Вкажіть папку гри на вкладці «Гра» (…\\steamapps\\common\\GarrysMod).";
        open_popup_ = true;
        return;
    }
    std::error_code ec;
    if (!test_run && !confirm_overwrite_ && !s_.output_path.empty() && s_.output_path.find('%') == std::string::npos &&
        fs::exists(path_from_utf8(s_.output_path), ec)) {
        ImGui::OpenPopup("Перезаписати?");
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

void App::start_encode_frames(const std::string& dir) {
    render::RenderSettings s = s_;
    int64_t count = 0;
    const std::string prefix = render::detect_frame_prefix(path_from_utf8(dir), &count);
    if (count == 0) {
        popup_title_ = "Кадрів не знайдено";
        popup_text_ = "У вибраній папці немає послідовності кадрів (name0000.tga / .jpg / .png).";
        open_popup_ = true;
        return;
    }
    const std::string ext = is_image_container(current_container()) ? "mp4" : current_container();
    s.output_path = path_to_utf8(path_from_utf8(dir) / ((prefix.empty() ? "video" : prefix) + "." + ext));
    std::error_code ec;
    for (int i = 2; fs::exists(path_from_utf8(s.output_path), ec) && i < 1000; ++i)
        s.output_path = path_to_utf8(path_from_utf8(dir) / std::format("{} ({}).{}", prefix.empty() ? "video" : prefix, i, ext));
    log_info("Кодую {} кадрів «{}» з папки {}", count, prefix, dir);
    job_ = std::make_unique<render::EncodeFramesJob>(s, path_from_utf8(dir), prefix, fs::path(), analysis_, voices_);
    job_reported_ = false;
    job_->start();
}

void App::export_voices(const std::string& dir) {
    if (!voices_ || !analysis_) return;
    if (job_running()) {
        log_warn("Зачекайте завершення поточного завдання");
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
    // Аналіз демо завершився?
    if (analyze_job_ && !analyze_job_->running() && !analysis_) {
        if (analyze_job_->state() == render::JobState::Succeeded) {
            analysis_ = analyze_job_->analysis();
            voices_ = analyze_job_->voices();
            if (analysis_) {
                platform_set_title("GMod Demo Render — " + path_to_utf8(path_from_utf8(s_.demo_path).filename()));
                load_markers_for_demo();
                chat_selected_ = -1;
                if (s_.end_tick > analysis_->last_tick) s_.end_tick = -1;
                if (std::getenv("GMDR_TEST_AUTOSTART")) start_render();   // лише для автотестів
            }
        } else if (analyze_job_->state() == render::JobState::Failed) {
            popup_title_ = "Не вдалося відкрити демо";
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
                popup_title_ = "Перегляд у грі: помилка";
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
            popup_title_ = job_->state() == render::JobState::Succeeded   ? "Черга завершена"
                           : job_->state() == render::JobState::Cancelled ? "Чергу зупинено"
                                                                          : "Черга: помилка";
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
            return;
        }
        const auto* render_job = dynamic_cast<render::RenderJob*>(job_.get());
        const bool test = render_job && render_job->is_test_run();
        popup_checks_.clear();
        popup_test_ok_ = false;
        switch (job_->state()) {
        case render::JobState::Succeeded:
            popup_title_ = test ? "Тестовий прогін" : "Готово!";
            popup_is_folder_ = dynamic_cast<render::ExportVoicesJob*>(job_.get()) != nullptr;
            if (test) {
                popup_text_ = job_->report();
                popup_test_ok_ = popup_text_.rfind("Усе працює", 0) == 0;
            } else {
                popup_text_ = (popup_is_folder_ ? "Голоси збережено в папку:\n" : "Відео збережено:\n") + job_->result();
            }
            popup_result_ = job_->result();
            break;
        case render::JobState::Cancelled:
            popup_title_ = "Скасовано";
            popup_text_ = dynamic_cast<render::ExportVoicesJob*>(job_.get()) ? "Збереження голосів перервано." : "Рендер перервано.";
            popup_result_.clear();
            break;
        default:
            popup_title_ = test ? "Тестовий прогін: помилка" : "Помилка";
            popup_text_ = job_->error();
            popup_result_.clear();
            if (render_job) popup_checks_ = job_->progress().checks;
            break;
        }
        open_popup_ = true;
        if (gmod_) driver_state_ = game::driver_state(*gmod_);
        platform_flash_window();
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
            log_info("Файл мікрофона: {}", f);
            mark_dirty();
            return;
        }
    }
    log_warn("Перетягніть файл демо (.dem), папку з кадрами або аудіофайл мікрофона");
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
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    draw_menu_bar();
    draw_demo_bar();

    const float fs_ = ImGui::GetFontSize();
    const float bottom_h = fs_ * 13.5f;
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float right_w = std::max(fs_ * 24.0f, avail_w * 0.38f);
    const float top_h = std::max(fs_ * 12.0f, ImGui::GetContentRegionAvail().y - bottom_h);

    ImGui::BeginChild("##settings", ImVec2(avail_w - right_w - ImGui::GetStyle().ItemSpacing.x, top_h), ImGuiChildFlags_Borders);
    ImGui::BeginDisabled(job_running());
    draw_settings_tabs();
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##info", ImVec2(0, top_h), ImGuiChildFlags_Borders);
    draw_info_panel();
    ImGui::EndChild();

    draw_output_bar();
    draw_progress();
    draw_log();
    draw_popups();
    ImGui::End();
}

void App::draw_menu_bar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("Файл")) {
        if (ImGui::MenuItem("Відкрити демо...", "Ctrl+O", false, !job_running())) {
            auto f = open_file_dialog("Відкрити демо Garry's Mod", {{"Демо GMod (*.dem)", "*.dem"}, {"Усі файли", "*.*"}},
                                      s_.demo_path.empty() && gmod_ ? path_to_utf8(gmod_->garrysmod / "demos") : s_.demo_path);
            if (!f.empty()) load_demo(f);
        }
        if (ImGui::MenuItem("Закодувати готові кадри...", nullptr, false, !job_running())) {
            auto d = pick_folder_dialog("Папка з кадрами startmovie (TGA/JPG) і WAV");
            if (!d.empty()) start_encode_frames(d);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Відкрити папку з відео", nullptr, false, !s_.output_path.empty()))
            open_path(path_to_utf8(path_from_utf8(s_.output_path).parent_path()));
        if (ImGui::MenuItem("Скинути налаштування", nullptr, false, !job_running())) {
            const std::string demo = s_.demo_path, out = s_.output_path, gd = s_.game_dir;
            s_ = render::RenderSettings{};
            s_.demo_path = demo;
            s_.output_path = out;
            s_.game_dir = gd;
            mark_dirty();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Вихід")) {
            if (on_close_request()) quit_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Інструменти")) {
        if (ImGui::MenuItem("Перевірити GPU-кодеки ще раз", nullptr, false, !gpu_probe_running_)) start_gpu_probe();
        if (ImGui::MenuItem("Знайти Garry's Mod автоматично", nullptr, false, !job_running())) detect_gmod(true);
        ImGui::Separator();
        const bool can = gmod_.has_value() && !job_running();
        if (ImGui::MenuItem("Встановити драйвер у GMod", nullptr, false, can)) {
            std::string err;
            if (!game::install_driver(*gmod_, &err)) log_error("{}", err);
            driver_state_ = game::driver_state(*gmod_);
        }
        if (ImGui::MenuItem("Видалити драйвер з GMod", nullptr, false, can)) {
            std::string err;
            if (!game::uninstall_driver(*gmod_, &err)) log_error("{}", err);
            driver_state_ = game::driver_state(*gmod_);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Відкрити журнал (gmdr_log.txt)")) open_path(path_to_utf8(app_data_dir() / "gmdr_log.txt"));
        if (ImGui::MenuItem("Відкрити папку програми")) open_path(path_to_utf8(executable_dir()));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Довідка")) {
        if (ImGui::MenuItem("Як це працює")) show_help_ = true;
        if (ImGui::MenuItem("Зібрати звіт про проблему...")) make_report();
        if (ImGui::MenuItem("Про програму")) show_about_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void App::draw_demo_bar() {
    const float fs_ = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Демо:");
    ImGui::SameLine();
    std::string shown = s_.demo_path.empty() ? std::string("перетягніть сюди файл .dem або натисніть «Відкрити»") : s_.demo_path;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - fs_ * 9.5f);
    ImGui::BeginDisabled(true);
    ImGui::InputText("##demo", &shown, ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(job_running());
    if (ImGui::Button("Відкрити...", ImVec2(-1, 0))) {
        auto f = open_file_dialog("Відкрити демо Garry's Mod", {{"Демо GMod (*.dem)", "*.dem"}, {"Усі файли", "*.*"}},
                                  s_.demo_path.empty() && gmod_ ? path_to_utf8(gmod_->garrysmod / "demos") : s_.demo_path);
        if (!f.empty()) load_demo(f);
    }
    ImGui::EndDisabled();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O) && !job_running()) {
        auto f = open_file_dialog("Відкрити демо Garry's Mod", {{"Демо GMod (*.dem)", "*.dem"}}, s_.demo_path);
        if (!f.empty()) load_demo(f);
    }
}

void App::draw_settings_tabs() {
    // Для автотестів інтерфейсу: GMDR_TEST_TAB=0..4 — відкрити вкладку при старті
    static int forced_tab = [] {
        const char* e = std::getenv("GMDR_TEST_TAB");
        return e ? std::atoi(e) : -1;
    }();
    auto flags = [&](int i) { return forced_tab == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
    if (!ImGui::BeginTabBar("##tabs")) return;
    if (ImGui::BeginTabItem("Відео", nullptr, flags(0))) {
        draw_tab_video();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Звук і голос", nullptr, flags(1))) {
        draw_tab_audio();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Гра", nullptr, flags(2))) {
        draw_tab_game();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Фрагмент", nullptr, flags(3))) {
        draw_tab_range();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Чат", nullptr, flags(4))) {
        draw_tab_chat();
        ImGui::EndTabItem();
    }
    const std::string queue_label = queue_.empty() ? "Черга###queue" : std::format("Черга ({})###queue", queue_.size());
    if (ImGui::BeginTabItem(queue_label.c_str(), nullptr, flags(5))) {
        draw_tab_queue();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    forced_tab = -1;
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
}

void App::update_taskbar() {
    TaskbarState st = TaskbarState::None;
    double fr = 0;
    if (job_ && job_->running()) {
        const auto p = job_->progress();
        fr = p.fraction;
        st = p.game_paused && p.disk_low ? TaskbarState::Error
             : p.game_paused            ? TaskbarState::Paused
             : p.frames > 0 || dynamic_cast<render::ExportVoicesJob*>(job_.get()) ? TaskbarState::Normal
                                          : TaskbarState::Indeterminate;
    } else if (analyze_job_ && analyze_job_->running()) {
        st = TaskbarState::Normal;
        fr = analyze_job_->progress().fraction;
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
    return std::max(0, (s_.end_tick > 0 ? s_.end_tick : last) - std::max(0, s_.start_tick)) *
           static_cast<double>(analysis_->tick_interval);
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
    log_info("Пресет «{}»: {}", kQuickPresets[index].label, kQuickPresets[index].tip);
    mark_dirty();
}

// ================================== Попапи ========================================
void App::draw_popups() {
    const float fs_ = ImGui::GetFontSize();
    if (open_popup_) {
        ImGui::OpenPopup("##result");
        open_popup_ = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(fs_ * 22, 0), ImVec2(fs_ * 50, fs_ * 40));
    if (ImGui::BeginPopupModal("##result", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextColored(popup_title_ == "Готово!" ? kColOk : popup_title_.find("омилка") != std::string::npos ? kColErr : kColAccent, "%s",
                           popup_title_.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(fs_ * 48);
        ImGui::TextUnformatted(popup_text_.c_str());
        ImGui::PopTextWrapPos();
        if (!popup_checks_.empty()) {
            ImGui::SeparatorText("Кроки");
            draw_checks(popup_checks_);
        }
        ImGui::Spacing();
        if (popup_test_ok_ && analysis_ && !job_running()) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.30f, 1.0f));
            if (ImGui::Button("Почати рендер")) {
                ImGui::CloseCurrentPopup();
                start_render(false);
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        if (!popup_result_.empty()) {
            if (ends_with_i(popup_result_, ".zip")) {
                if (ImGui::Button("Показати в папці")) {
                    show_in_folder(popup_result_);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
            } else if (popup_is_folder_) {
                if (ImGui::Button("Відкрити папку")) {
                    open_path(popup_result_);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
            } else {
                if (ImGui::Button(popup_title_.rfind("Тестовий", 0) == 0 ? "Відкрити тестове відео" : "Відкрити відео")) {
                    if (popup_result_.find('%') == std::string::npos) open_path(popup_result_);
                    else open_path(path_to_utf8(path_from_utf8(popup_result_).parent_path()));
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Показати в папці")) {
                    show_in_folder(popup_result_);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
            }
        }
        if (ImGui::Button("OK", ImVec2(fs_ * 6, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (confirm_quit_) {
        ImGui::OpenPopup("Вийти?");
        confirm_quit_ = false;
    }
    if (ImGui::BeginPopupModal("Вийти?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Рендер ще триває. Перервати його і закрити програму?");
        if (ImGui::Button("Так, вийти", ImVec2(fs_ * 8, 0))) {
            if (job_) job_->kill();
            quit_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Ні", ImVec2(fs_ * 6, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (show_about_) {
        ImGui::OpenPopup("Про програму");
        show_about_ = false;
    }
    if (ImGui::BeginPopupModal("Про програму", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(kColAccent, "GMod Demo Render " GMDR_VERSION);
        ImGui::TextUnformatted("Рендер демо-записів Garry's Mod у відео будь-якого формату.");
        ImGui::TextColored(kColDim, "Кодування: FFmpeg (libavcodec %d.%d). Інтерфейс: Dear ImGui %s.", LIBAVCODEC_VERSION_MAJOR,
                           LIBAVCODEC_VERSION_MINOR, IMGUI_VERSION);
        ImGui::TextColored(kColDim, "Ядер процесора: %u", std::max(1u, std::thread::hardware_concurrency()));
        if (ImGui::Button("OK", ImVec2(fs_ * 6, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (show_help_) {
        ImGui::OpenPopup("Як це працює");
        show_help_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(fs_ * 44, fs_ * 30), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Як це працює", nullptr)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kColAccent, "Чому потрібна сама гра?");
        ImGui::TextUnformatted("Файл .dem не містить картинки — лише мережеві пакети гри (рух сутностей, звуки, голос). "
                               "Правильно намалювати їх може тільки рушій GMod з вашими картами, моделями й аддонами.");
        ImGui::TextColored(kColAccent, "Що робить програма");
        ImGui::BulletText("Розбирає демо: карта, тривалість, гравці, голосовий чат (Steam Voice / Opus).");
        ImGui::BulletText("Запускає GMod з фіксованим кроком часу (host_framerate): кожен кадр відео — рівно 1/FPS секунди демо, "
                          "незалежно від потужності ПК. Тому FPS і роздільна здатність можуть бути будь-якими.");
        ImGui::BulletText("Невеликий Lua-драйвер у меню GMod вмикає startmovie точно на потрібному тіку і вимикає в кінці.");
        ImGui::BulletText("Кадри (TGA/JPEG) і звук гри забираються одразу, як з'являються, декодуються на всіх ядрах, "
                          "змішуються для motion blur, масштабуються і кодуються FFmpeg (CPU або GPU: NVENC/AMF/QSV).");
        ImGui::BulletText("Голоси гравців декодуються прямо з демо і накладаються точно за часом. "
                          "Можна додати окремий запис мікрофона.");
        ImGui::TextColored(kColAccent, "Поради");
        ImGui::BulletText("Перед довгим рендером натисніть «Тест 3 с»: програма перевірить кожен крок і порахує, скільки "
                          "триватиме рендер і скільки важитиме файл.");
        ImGui::BulletText("Гра працює у фоні (вікно за межами екрана), її звук у мікшері Windows вимкнено — на відео це "
                          "не впливає. Подивитися на гру можна кнопкою «Показати гру».");
        ImGui::BulletText("Якщо звук гри під час рендеру чути (режим «на екрані»), він грає пришвидшено — це нормально, "
                          "у відео він правильний.");
        ImGui::BulletText("Свій голос у демо: перед записом демо введіть voice_loopback 1.");
        ImGui::BulletText("Демо з сервера програється, лише якщо у вас є ті самі карти й аддони.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Зрозуміло", ImVec2(fs_ * 8, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace gmdr::gui
