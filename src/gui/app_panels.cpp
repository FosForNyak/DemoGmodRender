// =============================================================================
//  app_panels.cpp — панелі вікна: інформація про демо, голоси, прев'ю,
//  вихідний файл і кнопки, прогрес, журнал.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/media/ffmpeg_util.hpp"
#include "core/media/muxer.hpp"
#include "core/media/video_encoder.hpp"
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <map>

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

// ============================ Панель інформації =================================
void App::draw_info_panel() {
    const float fs_ = ImGui::GetFontSize();
    if (analyze_job_ && analyze_job_->running()) {
        const auto p = analyze_job_->progress();
        ImGui::TextUnformatted(p.stage.c_str());
        ImGui::ProgressBar(static_cast<float>(std::max(0.0, p.fraction)), ImVec2(-1, 0));
        return;
    }
    const auto* render_job = dynamic_cast<const render::RenderJob*>(job_.get());
    if (job_ && (job_->running() || preview_tex_) && (render_job || dynamic_cast<const render::EncodeFramesJob*>(job_.get()))) {
        draw_preview();
        if (render_job) {
            const auto p = job_->progress();
            if (job_->running() || render_job->is_test_run()) {
                ImGui::SeparatorText(render_job->is_test_run() ? "Тестовий прогін" : "Кроки");
                draw_checks(p.checks);
            }
        }
        if (job_->running()) return;
        ImGui::Spacing();
    }
    if (!analysis_) {
        ImGui::TextColored(kColAccent, "Як почати:");
        ImGui::BulletText("Відкрийте або перетягніть у вікно демо (.dem)");
        ImGui::BulletText("Налаштуйте відео і звук ліворуч");
        ImGui::BulletText("Натисніть «Тест 3 с», щоб перевірити, що все працює");
        ImGui::BulletText("Натисніть «Почати рендер» — програма сама\nзапустить Garry's Mod у фоні і все запише");
        ImGui::Spacing();
        ImGui::TextWrapped("Демо GMod лежать у папці garrysmod\\demos. Записати демо в грі: консоль → record назва, зупинити → stop.");
        return;
    }
    const auto& a = *analysis_;
    if (ImGui::BeginTable("##info", 2, ImGuiTableFlags_SizingFixedFit)) {
        auto row = [](const char* k, const std::string& v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(kColDim, "%s", k);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.c_str());
        };
        row("Карта", a.header.map_name);
        row("Сервер", a.header.server_name.empty() ? a.server_info.host_name : a.header.server_name);
        if (!a.server_info.gamemode.empty()) row("Режим", a.server_info.gamemode);
        row("Записав", a.header.client_name);
        row("Тривалість", std::format("{}  ({} тіків, {:.0f} тік/с)", format_duration(a.duration_seconds), a.last_tick,
                                      1.0 / a.tick_interval));
        row("Гравців", std::to_string(a.players.size()));
        if (a.packets_failed > 0)
            row("Розбір", std::format("{} з {} пакетів частково", a.packets_failed, a.packets_total));
        ImGui::EndTable();
    }
    for (const auto& w : a.warnings) ImGui::TextColored(kColWarn, "%s", w.c_str());
    ImGui::Spacing();
    draw_voice_table();
    (void)fs_;
}

void App::draw_voice_table() {
    ImGui::SeparatorText("Голоси в демо");
    if (!voices_ || voices_->speakers.empty()) {
        ImGui::TextColored(kColDim, "Голосового чату в демо немає.");
        ImGui::TextWrapped("Щоб у майбутніх демо записувався ваш голос: перед record введіть у консолі voice_loopback 1.");
        return;
    }
    const bool selectable = s_.voice_mode == "selected";
    std::vector<std::string> keys = split(s_.voice_selected, ',');
    for (auto& k : keys) k = trim(k);
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
    if (ImGui::BeginTable("##voices", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::min(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.5f,
                                             ImGui::GetFrameHeightWithSpacing() * (voices_->speakers.size() + 1.3f))))) {
        ImGui::TableSetupColumn("Гравець", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("SteamID", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Мовлення", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Гучність", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& sp : voices_->speakers) {
            has_local |= sp.is_local;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool on = std::find(keys.begin(), keys.end(), sp.key) != keys.end();
            if (!selectable) {
                on = s_.voice_mode == "all" || (s_.voice_mode == "local" && sp.is_local) ||
                     (s_.voice_mode == "others" && !sp.is_local);
            }
            ImGui::BeginDisabled(!selectable || job_running());
            if (ImGui::Checkbox(("##v" + sp.key).c_str(), &on) && selectable) {
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
            ImGui::SameLine();
            const double vol = volumes.count(sp.key) ? volumes[sp.key] : 1.0;
            const ImVec4 name_col = vol <= 0.0 ? kColDim : sp.is_local ? kColAccent : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(name_col, "%s%s", sp.name.c_str(), sp.is_local ? " (ви)" : "");
            if (ImGui::BeginPopupContextItem(("##vctx" + sp.key).c_str())) {
                if (ImGui::MenuItem("Лише цей гравець (соло)", nullptr, false, !job_running())) {
                    s_.voice_mode = "selected";
                    s_.voice_selected = sp.key;
                    mark_dirty();
                }
                if (ImGui::MenuItem("Вимкнути цього гравця", nullptr, false, !job_running())) {
                    volumes[sp.key] = 0.0;
                    store_volumes();
                }
                if (ImGui::MenuItem("Гучність 100%", nullptr, false, !job_running())) {
                    volumes.erase(sp.key);
                    store_volumes();
                }
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(kColDim, "%s", sp.steamid64 ? format_steamid(sp.steamid64).c_str() : "—");
            ImGui::TableNextColumn();
            ImGui::Text("%s", format_duration(sp.seconds).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(job_running());
            int pct = static_cast<int>(std::lround(vol * 100));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt(("##vol" + sp.key).c_str(), &pct, 0, 300, pct == 0 ? "вимк." : "%d%%")) {
                volumes[sp.key] = pct / 100.0;
                store_volumes();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Гучність цього гравця (0 — вимкнути). Ctrl+клік — ввести число.\nПравий клік на імені — соло.");
            ImGui::EndDisabled();
        }
        ImGui::EndTable();
    }
    if (!has_local)
        ImGui::TextColored(kColDim, "Вашого голосу немає (не було voice_loopback 1).");
    if (!selectable) ImGui::TextColored(kColDim, "Правий клік на імені — соло; повзунок — гучність гравця.");
    ImGui::BeginDisabled(job_running());
    if (ImGui::Button("Зберегти голоси у файли...")) {
        auto d = pick_folder_dialog("Папка для голосів", path_to_utf8(path_from_utf8(s_.demo_path).parent_path()));
        if (!d.empty()) export_voices(d);
    }
    ImGui::EndDisabled();
}

void App::draw_preview() {
    if (job_) {
        render::PreviewFrame f;
        if (job_->preview().get_if_newer(preview_serial_, f)) {
            preview_tex_ = platform_update_texture(preview_tex_, f.width, f.height, f.rgba.data());
            preview_serial_ = f.serial;
            preview_w_ = f.width;
            preview_h_ = f.height;
        }
    }
    const float w = ImGui::GetContentRegionAvail().x;
    if (!preview_tex_ || preview_w_ <= 0) {
        // Кадрів ще немає — рамка з підписом
        const float h = w * 9.0f / 16.0f;
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(20, 22, 26, 255), 4.0f);
        const char* msg = job_running() ? "Прев'ю з'явиться, щойно гра почне записувати кадри" : "Прев'ю";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p0.x + (w - ts.x) / 2, p0.y + (h - ts.y) / 2), IM_COL32(150, 155, 165, 255), msg);
        ImGui::Dummy(ImVec2(w, h));
        return;
    }
    const float h = w * static_cast<float>(preview_h_) / static_cast<float>(preview_w_);
    ImGui::Image(static_cast<ImTextureID>(preview_tex_), ImVec2(w, h));
}

void App::draw_checks(const std::vector<render::CheckItem>& checks) {
    for (const auto& c : checks) {
        const char* mark = "…";
        ImVec4 col = kColDim;
        switch (c.state) {
        case render::CheckItem::Ok: mark = "✓"; col = kColOk; break;
        case render::CheckItem::Failed: mark = "✗"; col = kColErr; break;
        case render::CheckItem::Skipped: mark = "–"; col = kColDim; break;
        default: break;
        }
        ImGui::TextColored(col, "%s", mark);
        ImGui::SameLine();
        ImGui::TextUnformatted(c.name.c_str());
        if (!c.detail.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kColDim, "— %s", c.detail.c_str());
        }
    }
}

// ============================ Вихідний файл і кнопки ================================
void App::draw_output_bar() {
    const float fs_ = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Зберегти як:");
    ImGui::SameLine();
    ImGui::BeginDisabled(job_running());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - fs_ * 27.5f);
    if (ImGui::InputText("##out", &s_.output_path)) mark_dirty();
    ImGui::SameLine();
    if (ImGui::Button("Огляд...##out")) {
        const std::string ext = current_container();
        auto f = save_file_dialog("Зберегти відео як", {{"Відео (*." + ext + ")", "*." + ext}, {"Усі файли", "*.*"}},
                                  s_.output_path, ext);
        if (!f.empty()) {
            s_.output_path = f;
            mark_dirty();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool can_start = analysis_ && !job_running() && !(analyze_job_ && analyze_job_->running());
    if (!job_running()) {
        ImGui::BeginDisabled(!can_start || s_.manual_mode);
        if (ImGui::Button("Тест 3 с", ImVec2(fs_ * 5.5f, 0))) start_render(true);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Тестовий прогін: 3 секунди з початку фрагмента в тимчасовий файл.\n"
                              "Перевіряє кожен крок (гра, драйвер, демо, кадри, звук, кодек) і рахує,\n"
                              "скільки триватиме весь рендер і скільки важитиме файл.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!can_start);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.66f, 0.36f, 1.0f));
        if (ImGui::Button("Почати рендер", ImVec2(-1, 0))) start_render();
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
    } else {
        if (job_->can_show_game()) {
            if (ImGui::Button(show_game_ ? "Сховати гру" : "Показати гру", ImVec2(fs_ * 7.0f, 0))) {
                show_game_ = !show_game_;
                job_->set_show_game(show_game_);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Тимчасово показати вікно гри поверх інших, щоб перевірити, що відбувається.\n"
                                  "Не клацайте в самій грі під час рендеру.");
            ImGui::SameLine();
        }
        if (ImGui::Button("Зупинити", ImVec2(fs_ * 7.5f, 0))) job_->cancel();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Зупинити запис і зберегти вже відрендерену частину");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Перервати", ImVec2(-1, 0))) job_->kill();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Негайно закрити гру і перервати рендер");
    }
    if (ImGui::BeginPopupModal("Перезаписати?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Файл уже існує:\n%s\n\nПерезаписати?", s_.output_path.c_str());
        if (ImGui::Button("Так", ImVec2(fs_ * 6, 0))) {
            confirm_overwrite_ = true;
            ImGui::CloseCurrentPopup();
            start_render();
        }
        ImGui::SameLine();
        if (ImGui::Button("Ні", ImVec2(fs_ * 6, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::draw_progress() {
    if (!job_) {
        ImGui::ProgressBar(0.0f, ImVec2(-1, 0), "Очікування");
        return;
    }
    const auto p = job_->progress();
    std::string text = p.stage;
    const bool fraction_only = dynamic_cast<render::ExportVoicesJob*>(job_.get()) != nullptr;
    if (p.fraction >= 0 && (p.frames > 0 || fraction_only || job_->state() == render::JobState::Succeeded))
        text += std::format("  {:.1f}%", p.fraction * 100);
    ImGui::ProgressBar(static_cast<float>(std::clamp(p.fraction, 0.0, 1.0)), ImVec2(-1, 0), text.c_str());
    std::string stats;
    if (p.frames > 0 || p.subframes > 0) {
        stats = std::format("Кадрів: {}  |  Відео: {} з {}", p.frames, format_duration(p.video_seconds),
                            format_duration(p.expected_seconds));
        if (p.speed_fps > 0) stats += std::format("  |  {:.1f} кадр/с", p.speed_fps);
        if (p.eta >= 0 && job_->running()) stats += "  |  Залишилось ~" + format_duration(p.eta);
        stats += "  |  Файл: " + format_bytes(static_cast<uint64_t>(std::max<int64_t>(0, p.bytes_written)));
        if (p.pending_files > 0) stats += std::format("  |  Черга: {}", p.pending_files);
    } else if (p.demo_total > 0 && job_->running()) {
        stats = std::format("Гра: {}  |  тік {} / {}", p.driver_state, p.demo_tick, p.demo_total);
    }
    if (p.elapsed > 0) stats += (stats.empty() ? "" : "  |  ") + std::string("Минуло: ") + format_duration(p.elapsed);
    ImGui::TextColored(kColDim, "%s", stats.c_str());
    if (p.disk_low) {
        ImGui::SameLine();
        ImGui::TextColored(kColErr, "  [гру призупинено: закінчується місце на диску]");
    } else if (p.game_paused) {
        ImGui::SameLine();
        ImGui::TextColored(kColWarn, "  [гра на паузі — кодер наздоганяє]");
    }
    if (!p.video_desc.empty()) ImGui::TextColored(kColDim, "%s  •  %s", p.video_desc.c_str(), p.audio_desc.c_str());
    // Час етапів конвеєра — щоб було видно, що гальмує
    if (p.frames > 0) {
        std::string st = std::format("Етапи, мс/кадр: читання {:.1f} · декодування {:.1f}", p.stat_read_ms, p.stat_decode_ms);
        if (p.stat_blend_ms > 0) st += std::format(" · змішування {:.1f}", p.stat_blend_ms);
        st += std::format(" · колір {:.1f} · кодування {:.1f} · звук {:.1f}", p.stat_convert_ms, p.stat_encode_ms, p.stat_audio_ms);
        if (p.stat_game_wait >= 0) st += std::format("  |  чекали на гру {:.0f}% часу", p.stat_game_wait * 100);
        ImGui::TextColored(kColDim, "%s", st.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Читання і декодування йдуть паралельно на кількох ядрах, змішування — у потоці подачі кадрів,\n"
                              "колір, кодування і звук — в окремому потоці кодера.\n"
                              "«Чекали на гру» — частка часу, коли всі кадри вже оброблено і програма чекала нових від гри:\n"
                              "близько 100%% — швидкість визначає гра (роздільна здатність, motion blur, RTX), а не кодування.");
    }
}

void App::draw_log() {
    const float fs_ = ImGui::GetFontSize();
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
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
        if (ImGui::MenuItem("Копіювати журнал")) {
            std::string all;
            std::lock_guard lock(log_mutex_);
            for (const auto& l : log_) all += l.time + " " + l.text + "\n";
            clipboard_text_set(all);
        }
        if (ImGui::MenuItem("Очистити")) {
            std::lock_guard lock(log_mutex_);
            log_.clear();
        }
        ImGui::MenuItem("Показувати технічні повідомлення", nullptr, &show_debug_log_);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}

} // namespace gmdr::gui
