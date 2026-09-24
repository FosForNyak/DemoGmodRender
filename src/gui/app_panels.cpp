// =============================================================================
//  app_panels.cpp — панелі поруч зі сторінками: монітор (прев'ю, «титр» демо,
//  таймкоди й кнопки фрагмента), рамка таймлайну, таблиця голосів, кроки
//  перевірки і журнал.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/media/ffmpeg_util.hpp"
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

namespace {
// Текст по центру прямокутника (обрізаний його межами)
void centered_text(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float y, ImU32 col, const std::string& text) {
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const float x = std::max(p0.x + 8, (p0.x + p1.x - ts.x) * 0.5f);
    dl->PushClipRect(p0, p1, true);
    dl->AddText(ImVec2(std::round(x), std::round(y)), col, text.c_str());
    dl->PopClipRect();
}

std::string demo_stem(const std::string& path) { return path_to_utf8(path_from_utf8(path).stem()); }
} // namespace

// ============================ Монітор програми ===================================
void App::draw_monitor_panel(ImVec2 pos, ImVec2 size) {
    std::string title = tr("Монітор");
    if (analysis_) title += " · " + demo_stem(s_.demo_path);
    bool hide = false;
    begin_panel("##monitor", pos, size, {title + "###program"}, nullptr, nullptr, 0,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
                {{"##hidemon", Icon::Close, tr("Сховати монітор (повернути — кнопкою в рядку стану або в меню «Вигляд»)"), false, &hide}});
    if (hide) show_monitor_ = false;
    const float fs_ = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 c0 = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const auto* render_job = dynamic_cast<const render::RenderJob*>(job_.get());
    const bool encoding = job_ && (render_job || dynamic_cast<const render::EncodeFramesJob*>(job_.get()));
    const bool show_details = encoding && (job_->running() || (render_job && render_job->is_test_run()));
    const bool show_warnings = !show_details && analysis_ && !analysis_->warnings.empty() && !(analyze_job_ && analyze_job_->running());
    float details_h = 0;
    if (show_details) details_h = std::min(avail.y * 0.42f, fs_ * 9.5f);
    else if (show_warnings)
        details_h = std::min(avail.y * 0.3f, ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(analysis_->warnings.size()) + 4);
    const float transport_h = ImGui::GetFrameHeight();
    const float frame_h = std::max(fs_ * 3, avail.y - transport_h - details_h - st.ItemSpacing.y * (details_h > 0 ? 2 : 1));
    draw_monitor_frame(c0, ImVec2(c0.x + avail.x, c0.y + frame_h));
    ImGui::SetCursorScreenPos(ImVec2(c0.x, c0.y + frame_h + st.ItemSpacing.y));
    draw_transport();
    if (details_h > 0) {
        ImGui::BeginChild("##mdetails", ImVec2(0, 0));
        if (show_details) {
            const auto p = job_->progress();
            if (render_job) draw_checks(p.checks);
            if (!p.video_desc.empty()) ImGui::TextColored(kColDim, "%s  •  %s", p.video_desc.c_str(), p.audio_desc.c_str());
            // Час етапів конвеєра — щоб було видно, що гальмує
            if (p.frames > 0) {
                std::string sts = trf("Етапи, мс/кадр: читання {:.1f} · декодування {:.1f}", p.stat_read_ms, p.stat_decode_ms);
                if (p.stat_blend_ms > 0) sts += trf(" · змішування {:.1f}", p.stat_blend_ms);
                sts += trf(" · колір {:.1f} · кодування {:.1f} · звук {:.1f}", p.stat_convert_ms, p.stat_encode_ms, p.stat_audio_ms);
                if (p.stat_game_wait >= 0) sts += trf("  |  чекали на гру {:.0f}% часу", p.stat_game_wait * 100);
                if (p.frames_via_pipe) sts += tr("  |  кадри йдуть з гри каналом, без диска");
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(kColDim, "%s", sts.c_str());
                ImGui::PopTextWrapPos();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tr("Читання і декодування йдуть паралельно на кількох ядрах, змішування — у потоці подачі кадрів,\n"
                                            "колір, кодування і звук — в окремому потоці кодера.\n"
                                            "«Чекали на гру» — частка часу, коли всі кадри вже оброблено і програма чекала нових від гри:\n"
                                            "близько 100% — швидкість визначає гра (роздільна здатність, motion blur, RTX), а не кодування."));
            }
        } else {
            for (const auto& w : analysis_->warnings) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                draw_icon(ImGui::GetWindowDrawList(), Icon::Warning, ImVec2(p.x + fs_ * 0.5f, p.y + ImGui::GetTextLineHeight() * 0.5f),
                          fs_ * 0.9f, ImGui::GetColorU32(kColWarn));
                ImGui::Dummy(ImVec2(fs_ * 1.2f, 0));
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(kColWarn, "%s", w.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::EndChild();
    }
    end_panel();
}

// Кадр монітора: прев'ю рендеру, «титр» демо або підказка, з чого почати
void App::draw_monitor_frame(ImVec2 p0, ImVec2 p1) {
    if (job_) {
        render::PreviewFrame f;
        if (job_->preview().get_if_newer(preview_serial_, f)) {
            preview_tex_ = platform_update_texture(preview_tex_, f.width, f.height, f.rgba.data());
            preview_serial_ = f.serial;
            preview_w_ = f.width;
            preview_h_ = f.height;
        }
    }
    // Монітор темний і в світлій темі — текст на ньому світлий
    const ImU32 kMonText = IM_COL32(222, 225, 232, 255), kMonDim = IM_COL32(140, 147, 160, 255);
    const float fs_ = ImGui::GetFontSize();
    const float base = ImGui::GetStyle().FontSizeBase;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = p1.x - p0.x, H = p1.y - p0.y;
    dl->AddRectFilled(p0, p1, IM_COL32(14, 15, 18, 255), 6 * fs_ / 15.0f);
    // Кадр у пропорціях відео, по центру (решта — «поля», як у моніторах програм монтажу)
    double aspect = s_.width > 0 && s_.height > 0 ? static_cast<double>(s_.width) / s_.height : 16.0 / 9.0;
    const bool have_preview = preview_tex_ && preview_w_ > 0 && preview_h_ > 0;
    if (have_preview) aspect = static_cast<double>(preview_w_) / preview_h_;
    float fw = W, fh = static_cast<float>(W / aspect);
    if (fh > H) {
        fh = H;
        fw = static_cast<float>(H * aspect);
    }
    const ImVec2 f0(std::round(p0.x + (W - fw) * 0.5f), std::round(p0.y + (H - fh) * 0.5f));
    const ImVec2 f1(f0.x + std::round(fw), f0.y + std::round(fh));
    dl->AddRectFilled(f0, f1, kMonitor);
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(ImVec2(W, H));
    if (have_preview) {
        dl->AddImage(static_cast<ImTextureID>(preview_tex_), f0, f1);
        return;
    }
    const float cy = (f0.y + f1.y) * 0.5f;
    if (analyze_job_ && analyze_job_->running()) {
        const auto p = analyze_job_->progress();
        centered_text(dl, f0, f1, cy - fs_ * 1.4f, kMonText, p.stage);
        const float mw = std::min(fw * 0.5f, fs_ * 16);
        ImGui::SetCursorScreenPos(ImVec2((f0.x + f1.x - mw) * 0.5f, cy));
        meter(static_cast<float>(std::max(0.0, p.fraction)), ImVec2(mw, std::round(fs_ * 0.3f)));
        return;
    }
    if (encoding_preview_pending()) {
        centered_text(dl, f0, f1, cy - fs_ * 0.5f, kMonDim, tr("Прев'ю з'явиться, щойно гра почне записувати кадри"));
        return;
    }
    if (analysis_) {
        // «Титр» демо: карта великими літерами, під нею сервер і подробиці
        const auto& a = *analysis_;
        const std::string server = a.header.server_name.empty() ? a.server_info.host_name : a.header.server_name;
        std::string info = trf("Записав: {}", a.header.client_name);
        if (!a.server_info.gamemode.empty()) info = a.server_info.gamemode + " · " + info;
        const std::string info2 = trf("{}  ·  {} тіків, {:.0f} тік/с  ·  гравців: {}", format_duration(a.duration_seconds), a.last_tick,
                                      1.0 / a.tick_interval, a.players.size());
        const float big = std::clamp(fh / 9.0f / fs_, 1.2f, 2.6f);
        ImGui::PushFont(bold_font(), base * big);
        const float big_h = ImGui::GetFontSize();
        const float block = big_h + fs_ * 3.6f;
        float y = cy - block * 0.5f;
        centered_text(dl, f0, f1, y, IM_COL32(236, 236, 236, 255), a.header.map_name);
        ImGui::PopFont();
        y += big_h + fs_ * 0.35f;
        centered_text(dl, f0, f1, y, kMonText, server);
        y += fs_ * 1.45f;
        centered_text(dl, f0, f1, y, kMonDim, info);
        y += fs_ * 1.25f;
        centered_text(dl, f0, f1, y, kMonDim, info2);
        // Рамка «безпечної зони» — як у моніторах програм монтажу
        const float mx = fw * 0.1f, my = fh * 0.1f;
        dl->AddRect(ImVec2(f0.x + mx, f0.y + my), ImVec2(f1.x - mx, f1.y - my), IM_COL32(255, 255, 255, 18));
        return;
    }
    // Порожній монітор: з чого почати
    draw_icon(dl, Icon::Film, ImVec2((f0.x + f1.x) * 0.5f, cy - fs_ * 2.6f), fs_ * 2.6f, IM_COL32(96, 102, 116, 255));
    centered_text(dl, f0, f1, cy - fs_ * 0.9f, kMonText, tr("Відкрийте або перетягніть у вікно демо (.dem)"));
    const char* open = tr("Відкрити демо...");
    const float bw = action_width(open, Kind::Cta);
    ImGui::SetCursorScreenPos(ImVec2(std::round((f0.x + f1.x - bw) * 0.5f), cy + fs_ * 0.6f));
    ImGui::BeginDisabled(job_running());
    if (action_button(open, Kind::Cta)) open_demo_dialog();
    ImGui::EndDisabled();
    if (fh > fs_ * 9) {
        ImGui::PushFont(nullptr, base * 0.9f);
        centered_text(dl, f0, f1, cy + fs_ * 3.0f, kMonDim, tr("Демо GMod лежать у папці garrysmod\\demos. Записати демо в грі: консоль → record назва, зупинити → stop."));
        ImGui::PopFont();
    }
}

bool App::encoding_preview_pending() const {
    return job_ && job_->running() &&
           (dynamic_cast<const render::RenderJob*>(job_.get()) || dynamic_cast<const render::EncodeFramesJob*>(job_.get()));
}

// Рядок під кадром: курсор (синій таймкод), кнопки фрагмента, тривалість фрагмента
void App::draw_transport() {
    const float fs_ = ImGui::GetFontSize();
    const float fh = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const double fps = parse_rational(s_.fps).value_or(Rational{60, 1}).value();
    const double ti = analysis_ ? analysis_->tick_interval : 0.0;
    const int32_t last = analysis_ ? analysis_->last_tick : 0;
    const double in_t = std::max(0, s_.start_tick) * ti, out_t = (s_.end_tick > 0 ? s_.end_tick : last) * ti;
    const float ty = p.y + (fh - fs_) * 0.5f;
    const float bs = fh, sp = 2.0f, group = fs_ * 0.7f;
    const float total = bs * 6 + sp * 4 + group * 2;
    ImGui::PushFont(bold_font(), 0.0f);
    const std::string cur = timecode(analysis_ ? playhead_t_ : 0.0, fps);
    const float cw = ImGui::CalcTextSize(cur.c_str()).x;
    const std::string dur = timecode(analysis_ ? std::max(0.0, out_t - in_t) : 0.0, fps);
    const float dw = ImGui::CalcTextSize(dur.c_str()).x;
    // Вузький монітор: спершу зникає тривалість, потім і курсор (обидва є на таймлайні)
    const bool show_cur = w >= total + cw * 2 + fs_ * 2;
    const bool show_dur = w >= total + (cw + dw) * 1.1f + fs_ * 3;
    if (show_cur) dl->AddText(ImVec2(p.x + 2, ty), ImGui::GetColorU32(kAccentText), cur.c_str());
    if (show_dur) dl->AddText(ImVec2(p.x + w - dw - 2, ty), ImGui::GetColorU32(kText), dur.c_str());
    ImGui::PopFont();
    if (show_cur) {
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y));
        ImGui::InvisibleButton("##tc_cur", ImVec2(cw + 2, fh));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Курсор таймлайну. Клацніть по лінійці таймлайну, щоб поставити його."));
    }
    if (show_dur) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - dw - 2, p.y));
        ImGui::InvisibleButton("##tc_dur", ImVec2(dw + 2, fh));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Тривалість фрагмента"));
    }

    // Кнопки посередині
    const bool busy = job_running();
    ImGui::BeginDisabled(!analysis_);
    ImGui::SetCursorScreenPos(ImVec2(std::round(p.x + (w - total) * 0.5f), p.y));
    if (icon_button("##goin", Icon::GoToIn, tr("Перейти до початку фрагмента (Shift+I)"))) set_playhead(in_t);
    ImGui::SameLine(0, sp);
    ImGui::BeginDisabled(busy);
    if (icon_button("##markin", Icon::MarkIn, tr("Початок фрагмента в курсорі (I)")))
        set_fragment_start(static_cast<int32_t>(std::llround(playhead_t_ / std::max(ti, 1e-9))));
    ImGui::SameLine(0, sp);
    if (icon_button("##markout", Icon::MarkOut, tr("Кінець фрагмента в курсорі (O)")))
        set_fragment_end(static_cast<int32_t>(std::llround(playhead_t_ / std::max(ti, 1e-9))));
    ImGui::EndDisabled();
    ImGui::SameLine(0, sp);
    if (icon_button("##goout", Icon::GoToOut, tr("Перейти до кінця фрагмента (Shift+O)"))) set_playhead(out_t);
    ImGui::SameLine(0, group);
    if (icon_button("##marker", Icon::Marker, tr("Позначка в курсорі (M)")))
        add_marker_at(static_cast<int32_t>(std::llround(playhead_t_ / std::max(ti, 1e-9))), {});
    ImGui::SameLine(0, group);
    ImGui::BeginDisabled(busy);
    if (icon_button("##watch", Icon::Play, tr("Переглянути в грі з курсора (у реальному часі, зі звуком).\n"
                                             "У грі: F9 — початок фрагмента, F11 — кінець, F6 — позначка.")))
        start_watch(static_cast<int32_t>(std::llround(playhead_t_ / std::max(ti, 1e-9))));
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + fh + ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0, 0));
}

// ================================ Таймлайн ======================================
void App::draw_timeline_panel(ImVec2 pos, ImVec2 size) {
    std::string title = tr("Таймлайн");
    if (analysis_) title += " · " + demo_stem(s_.demo_path);
    bool hide = false;
    begin_panel("##timeline", pos, size, {title + "###timeline"}, nullptr, "##tlmenu", 0,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
                {{"##hidetl", Icon::Close, tr("Сховати таймлайн (повернути — кнопкою в рядку стану або в меню «Вигляд»)"), false, &hide}});
    if (hide) show_timeline_ = false;
    if (begin_panel_menu("##tlmenu")) {
        const bool have = analysis_ != nullptr;
        if (ImGui::MenuItem(tr("Показати весь запис"), nullptr, false, have) && have) {
            view_t0_ = 0;
            view_t1_ = static_cast<float>(analysis_->duration_seconds);
        }
        if (ImGui::MenuItem(tr("Показати фрагмент"), nullptr, false, have && !whole_demo_) && have) {
            const double ti = analysis_->tick_interval;
            const double a = std::max(0, s_.start_tick) * ti, b = (s_.end_tick > 0 ? s_.end_tick : analysis_->last_tick) * ti;
            const double pad = std::max(0.5, (b - a) * 0.05);
            view_t0_ = static_cast<float>(std::max(0.0, a - pad));
            view_t1_ = static_cast<float>(std::min(analysis_->duration_seconds, b + pad));
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Розділи у відео з позначок"), nullptr, s_.chapters)) {
            s_.chapters = !s_.chapters;
            mark_dirty();
        }
        ImGui::EndPopup();
    }
    if (!analysis_) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 a = ImGui::GetContentRegionAvail();
        centered_text(ImGui::GetWindowDrawList(), p, ImVec2(p.x + a.x, p.y + a.y), p.y + a.y * 0.45f, kTextDim,
                      tr("Тут з'являться голоси гравців і чат на шкалі часу"));
    } else {
        draw_timeline();
    }
    end_panel();
}

// ================================= Голоси ========================================
void App::listen_voice(const voice::SpeakerTrack& sp) {
    if (clip_future_.valid()) return;   // попередній уривок ще готується
    player_.stop();
    playing_key_.clear();
    clip_key_ = sp.key;
    // voices_ тримає доріжку живою, поки уривок готується у фоні
    auto voices = voices_;
    const voice::SpeakerTrack* track = &sp;
    const render::RenderSettings settings = s_;
    const int64_t from =
        analysis_ ? static_cast<int64_t>(std::llround(std::max(0, s_.start_tick) * static_cast<double>(analysis_->tick_interval) *
                                                      voice::kVoiceRate))
                  : 0;
    clip_future_ = std::async(std::launch::async, [voices, track, settings, from] {
        std::vector<audio::VoiceCleanup> fx;
        std::atomic<bool> cancel{false};
        if (render::needs_voice_cleanup(settings)) fx = render::voice_cleanup_for(settings, {track}, cancel);
        return audio::make_voice_clip(*track, from, fx.empty() ? nullptr : &fx[0]);
    });
}

void App::poll_voice_clip() {
    if (playing_key_.size() && !player_.playing()) playing_key_.clear();
    if (!clip_future_.valid() || clip_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const audio::VoiceClip clip = clip_future_.get();
    if (clip.mono.empty()) {
        log_info("{}", trf("Прослуховування: у цього гравця немає мовлення"));
        return;
    }
    std::string err;
    if (player_.play(clip.mono, &err)) {
        playing_key_ = clip_key_;
        log_info("{}", trf("Прослуховування: {} с мовлення з {} демо{}", static_cast<int>(std::lround(clip.speech_seconds)),
                 format_duration(static_cast<double>(clip.start) / voice::kVoiceRate),
                 render::needs_voice_cleanup(s_) ? tr(" (з обробкою, як у відео)") : ""));
    } else {
        log_warn("{}", trf("Прослуховування: {}", err));
    }
}

void App::draw_voice_table(float max_height) {
    const float fs_ = ImGui::GetFontSize();
    if (!analysis_) {
        ImGui::TextColored(kColDim, "%s", tr("Спершу відкрийте демо."));
        return;
    }
    if (!voices_ || voices_->speakers.empty()) {
        ImGui::TextColored(kColDim, "%s", tr("Голосового чату в демо немає."));
        ImGui::TextWrapped("%s", tr("Щоб у майбутніх демо записувався ваш голос: перед record введіть у консолі voice_loopback 1."));
        return;
    }
    const bool selectable = s_.voice_mode == "selected";
    std::vector<std::string> keys = split(s_.voice_selected, ',');
    for (auto& k : keys) k = trim(k);
    std::vector<std::string> denoise_keys;
    for (const auto& k : split(s_.voice_denoise_players, ','))
        if (!trim(k).empty()) denoise_keys.push_back(trim(k));
    // Гучність окремих гравців: "key=gain; key2=gain" (лише відмінні від 100%)
    std::map<std::string, double> volumes;
    for (const auto& [k, v] : parse_key_values(s_.voice_volumes)) volumes[k] = parse_double(v).value_or(1.0);
    auto store_volumes = [&] {
        std::string out;
        for (const auto& [k, v] : volumes)
            if (std::abs(v - 1.0) > 0.005) out += std::format("{}{}={:.2f}", out.empty() ? "" : "; ", k, v);
        s_.voice_volumes = out;
        mark_dirty();
    };
    bool has_local = false;
    const float row_h = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2;
    float table_h = row_h * static_cast<float>(voices_->speakers.size() + 1) + 2;
    if (max_height > 0) table_h = std::min(table_h, max_height);
    if (ImGui::BeginTable("##voices", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, table_h))) {
        ImGui::TableSetupColumn(tr("Гравець"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("SteamID", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(tr("Мовлення"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(tr("Гучність"), ImGuiTableColumnFlags_WidthFixed, fs_ * 9.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& sp : voices_->speakers) {
            has_local |= sp.is_local;
            ImGui::PushID(sp.key.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool on = std::find(keys.begin(), keys.end(), sp.key) != keys.end();
            if (!selectable) {
                on = s_.voice_mode == "all" || (s_.voice_mode == "local" && sp.is_local) ||
                     (s_.voice_mode == "others" && !sp.is_local);
            }
            ImGui::BeginDisabled(!selectable || job_running());
            if (checkbox("##v", &on) && selectable) {
                std::vector<std::string> nk;
                for (const auto& k : keys)
                    if (k != sp.key && !k.empty()) nk.push_back(k);
                if (on) nk.push_back(sp.key);
                std::string joined;
                for (const auto& k : nk) joined += (joined.empty() ? "" : ",") + k;
                s_.voice_selected = joined;
                mark_dirty();
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            // Прослухати уривок (як «соло-прослуховування» доріжки)
            const bool playing = playing_key_ == sp.key;
            ImGui::BeginDisabled(!VoicePlayer::supported() || (clip_future_.valid() && !playing));
            if (icon_button("##listen", Icon::Headphones, playing ? tr("Зупинити прослуховування") : tr("Прослухати (15 с з початку фрагмента)"),
                            playing)) {
                if (playing) {
                    player_.stop();
                    playing_key_.clear();
                } else {
                    listen_voice(sp);
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            const double vol = volumes.count(sp.key) ? volumes[sp.key] : 1.0;
            const bool denoised = s_.voice_denoise ||
                                  std::find(denoise_keys.begin(), denoise_keys.end(), sp.key) != denoise_keys.end();
            const ImVec4 name_col = vol <= 0.0 ? kColDim : sp.is_local ? kColAccent : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(name_col, "%s%s", sp.name.c_str(), sp.is_local ? tr(" (ви)") : "");
            if (ImGui::BeginPopupContextItem("##vctx")) {
                if (playing) {
                    if (ImGui::MenuItem(tr("Зупинити прослуховування"))) {
                        player_.stop();
                        playing_key_.clear();
                    }
                } else if (ImGui::MenuItem(tr("Прослухати (15 с з початку фрагмента)"), nullptr, false,
                                           VoicePlayer::supported() && !clip_future_.valid())) {
                    listen_voice(sp);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Лише цей гравець (соло)"), nullptr, false, !job_running())) {
                    s_.voice_mode = "selected";
                    s_.voice_selected = sp.key;
                    mark_dirty();
                }
                if (ImGui::MenuItem(tr("Вимкнути цього гравця"), nullptr, false, !job_running())) {
                    volumes[sp.key] = 0.0;
                    store_volumes();
                }
                if (ImGui::MenuItem(tr("Гучність 100%"), nullptr, false, !job_running())) {
                    volumes.erase(sp.key);
                    store_volumes();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(s_.voice_denoise ? tr("Шумодав (увімкнено для всіх)") : tr("Шумодав для цього гравця"), nullptr,
                                    denoised, !job_running() && !s_.voice_denoise)) {
                    std::string list;
                    for (const auto& k : denoise_keys)
                        if (k != sp.key) list += (list.empty() ? "" : ",") + k;
                    if (!denoised) list += (list.empty() ? "" : ",") + sp.key;
                    s_.voice_denoise_players = list;
                    mark_dirty();
                }
                ImGui::EndPopup();
            }
            if (denoised) {
                ImGui::SameLine();
                ImGui::TextColored(kColDim, "%s", tr("· шумодав"));
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kColDim, "%s", sp.steamid64 ? format_steamid(sp.steamid64).c_str() : "—");
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(format_duration(sp.seconds).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(job_running());
            int pct = static_cast<int>(std::lround(vol * 100));
            if (slider_int("##vol", &pct, 0, 300, pct == 0 ? tr("вимк.") : "%d%%", ImGui::GetContentRegionAvail().x)) {
                volumes[sp.key] = pct / 100.0;
                store_volumes();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Гучність цього гравця (0 — вимкнути): тягніть повзунок або клацніть число, щоб ввести.\n"
                                        "Правий клік на імені — соло."));
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // Прослуховування: чий голос грає і кнопка «Стоп»
    auto speaker_name = [&](const std::string& key) {
        for (const auto& sp : voices_->speakers)
            if (sp.key == key) return sp.name.empty() ? sp.display_name() : sp.name;
        return key;
    };
    if (!playing_key_.empty()) {
        ImGui::TextColored(kColAccent, tr("Грає: %s  %s / %s"), speaker_name(playing_key_).c_str(),
                           format_duration(player_.position()).c_str(), format_duration(player_.duration()).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Стоп##listen"))) {
            player_.stop();
            playing_key_.clear();
        }
    } else if (clip_future_.valid()) {
        ImGui::TextColored(kColDim, tr("Готую уривок голосу: %s..."), speaker_name(clip_key_).c_str());
    }
    ImGui::BeginDisabled(job_running());
    if (action_button(tr("Зберегти голоси у файли..."), Kind::Secondary, 0, Icon::Download)) {
        auto d = pick_folder_dialog(tr("Папка для голосів"), path_to_utf8(path_from_utf8(s_.demo_path).parent_path()));
        if (!d.empty()) export_voices(d);
    }
    ImGui::EndDisabled();
    help_marker(tr("Правий клік на імені — прослухати, соло, шумодав; повзунок — гучність.\n"
                "Навушники — прослухати 15 с з початку фрагмента (з обробкою, як у відео).\n"
                "M і S на доріжках таймлайну — вимкнути гравця і лише цей гравець (соло)."));
    if (!has_local) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kColDim, "%s", tr("Вашого голосу немає (не було voice_loopback 1)."));
    }
}

void App::draw_checks(const std::vector<render::CheckItem>& checks) {
    const float fs_ = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& c : checks) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 ic(p.x + fs_ * 0.5f, p.y + ImGui::GetTextLineHeight() * 0.5f);
        switch (c.state) {
        case render::CheckItem::Ok: draw_icon(dl, Icon::Check, ic, fs_ * 0.85f, ImGui::GetColorU32(kColOk)); break;
        case render::CheckItem::Failed: draw_icon(dl, Icon::Close, ic, fs_ * 0.85f, ImGui::GetColorU32(kColErr)); break;
        case render::CheckItem::Skipped:
            dl->AddLine(ImVec2(ic.x - fs_ * 0.25f, ic.y), ImVec2(ic.x + fs_ * 0.25f, ic.y), ImGui::GetColorU32(kColDim), 1.5f);
            break;
        default: dl->AddCircle(ic, fs_ * 0.22f, ImGui::GetColorU32(kColDim), 0, 1.5f); break;
        }
        ImGui::Dummy(ImVec2(fs_ * 1.1f, ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        ImGui::TextUnformatted(c.name.c_str());
        if (!c.detail.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kColDim, "— %s", c.detail.c_str());
        }
    }
}

// ================================ Журнал ========================================
void App::draw_page_log() {
    page_header(tr("Журнал"), tr("Усе, що програма робила в цьому сеансі: кроки рендеру, попередження і помилки."));
    if (action_button(tr("Копіювати журнал"), Kind::Secondary)) {
        std::string all;
        std::lock_guard lock(log_mutex_);
        for (const auto& l : log_) all += l.time + " " + l.text + "\n";
        clipboard_text_set(all);
    }
    ImGui::SameLine();
    if (action_button(tr("Очистити"), Kind::Secondary)) {
        std::lock_guard lock(log_mutex_);
        log_.clear();
    }
    ImGui::SameLine();
    if (action_button(tr("Відкрити файл журналу"), Kind::Ghost, 0, Icon::File)) open_path(path_to_utf8(app_data_dir() / "gmdr_log.txt"));
    ImGui::SameLine();
    toggle(tr("Технічні повідомлення"), &show_debug_log_);
    ImGui::Spacing();
    draw_log();
}

void App::draw_log() {
    const float fs_ = ImGui::GetFontSize();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kField);
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleColor();
    {
        std::lock_guard lock(log_mutex_);
        ImGuiListClipper clipper;
        std::vector<const LogLine*> lines;
        lines.reserve(log_.size());
        for (const auto& l : log_)
            if (show_debug_log_ || l.level != LogLevel::Debug) lines.push_back(&l);
        clipper.Begin(static_cast<int>(lines.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const LogLine& l = *lines[static_cast<size_t>(i)];
                ImGui::TextColored(kColDim, "%s", l.time.c_str());
                ImGui::SameLine();
                const ImVec4 col = l.level == LogLevel::Error ? kColErr
                                   : l.level == LogLevel::Warn ? kColWarn
                                   : l.level == LogLevel::Debug ? kColDim
                                                                : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(l.text.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (log_scroll_to_bottom_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - fs_ * 3) ImGui::SetScrollHereY(1.0f);
        log_scroll_to_bottom_ = false;
    }
    if (ImGui::BeginPopupContextWindow("##logctx")) {
        if (ImGui::MenuItem(tr("Копіювати журнал"))) {
            std::string all;
            std::lock_guard lock(log_mutex_);
            for (const auto& l : log_) all += l.time + " " + l.text + "\n";
            clipboard_text_set(all);
        }
        if (ImGui::MenuItem(tr("Очистити"))) {
            std::lock_guard lock(log_mutex_);
            log_.clear();
        }
        ImGui::MenuItem(tr("Показувати технічні повідомлення"), nullptr, &show_debug_log_);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}

} // namespace gmdr::gui
