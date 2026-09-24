// =============================================================================
//  app_page_queue.cpp — сторінка «Черга»: кілька фрагментів чи демо підряд (на ніч),
//  гра запускається один раз. Черга зберігається в gmdr_queue.json.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/util/file_util.hpp"
#include "core/util/json.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

static fs::path queue_file() { return app_data_dir() / "gmdr_queue.json"; }

void App::load_queue() {
    queue_.clear();
    auto text = read_file_text(queue_file());
    if (!text) return;
    auto j = json::parse(*text);
    if (!j || !j->is_array()) return;
    for (const json::Value& e : j->items()) {
        QueueEntry q;
        q.s = render::RenderSettings::from_json(e["settings"]);
        q.tick_interval = e["tick_interval"].as_number(0);
        if (!q.s.demo_path.empty()) queue_.push_back(std::move(q));
    }
    if (!queue_.empty()) log_info("{}", trf("Черга рендерів: {} пункт(ів) з минулого разу — сторінка «Черга»", queue_.size()));
}

void App::save_queue() {
    json::Value arr = json::Value::array();
    for (const auto& q : queue_) {
        json::Value e = json::Value::object();
        e.set("tick_interval", json::Value::number(q.tick_interval));
        e.set("settings", q.s.to_json());
        arr.push(e);
    }
    std::string err;
    if (!write_file_atomic(queue_file(), arr.dump(), &err)) log_debug("Не вдалося зберегти чергу: {}", err);
}

void App::add_to_queue() {
    if (!analysis_ || s_.output_path.empty()) return;
    QueueEntry q;
    q.s = s_;
    q.tick_interval = analysis_->tick_interval;
    push_queue_entry(std::move(q));
}

// З бібліотеки: ціле демо з поточними налаштуваннями відео і звуку
void App::add_demo_to_queue(const std::string& demo) {
    QueueEntry q;
    q.s = s_;
    q.s.demo_path = demo;
    q.s.start_tick = 0;
    q.s.end_tick = -1;
    if (q.s.voice_mode == "selected") q.s.voice_mode = "all";   // вибрані гравці — з іншого демо
    q.s.markers = render::format_markers(render::load_demo_markers(app_data_dir() / "gmdr_markers.json", demo));
    q.s.output_path = render::default_output_path(demo, current_container());
    push_queue_entry(std::move(q));
}

void App::push_queue_entry(QueueEntry q) {
    // Той самий файл уже в черзі (той самий демо без іншої назви) — не перезаписувати
    auto taken = [&](const std::string& p) {
        return std::any_of(queue_.begin(), queue_.end(), [&](const QueueEntry& e) { return to_lower(e.s.output_path) == to_lower(p); });
    };
    if (taken(q.s.output_path) && q.s.output_path.find('%') == std::string::npos) {
        const fs::path p = path_from_utf8(q.s.output_path);
        for (int k = 2; k < 1000; ++k) {
            const std::string cand = path_to_utf8(p.parent_path() / path_from_utf8(std::format(
                "{}_{}{}", path_to_utf8(p.stem()), k, path_to_utf8(p.extension()))));
            if (!taken(cand)) {
                q.s.output_path = cand;
                break;
            }
        }
    }
    log_info("{}", trf("До черги: {} → {}", path_to_utf8(path_from_utf8(q.s.demo_path).filename()), q.s.output_path));
    queue_.push_back(std::move(q));
    save_queue();
}

void App::start_queue() {
    if (queue_.empty() || job_running()) return;
    if (!gmod_) {
        popup_title_ = tr("Не знайдено Garry's Mod");
        popup_text_ = tr("Вкажіть папку гри на сторінці «Гра» (…\\steamapps\\common\\GarrysMod).");
        open_popup_ = true;
        return;
    }
    save_settings_now();
    std::vector<render::RenderSettings> items;
    for (const auto& q : queue_) {
        std::error_code ec;
        fs::create_directories(path_from_utf8(q.s.output_path).parent_path(), ec);
        items.push_back(q.s);
    }
    show_game_ = false;
    job_ = std::make_unique<render::QueueJob>(std::move(items));
    job_reported_ = false;
    job_->start();
}

// Черга закінчилась: готові пункти прибираємо, невдалі й не початі лишаються
void App::finish_queue(const render::QueueJob& q) {
    const auto res = q.results();
    std::vector<QueueEntry> left;
    for (size_t i = 0; i < queue_.size(); ++i)
        if (i >= res.size() || res[i].state != render::JobState::Succeeded) left.push_back(queue_[i]);
    queue_ = std::move(left);
    save_queue();
}

void App::draw_page_queue() {
    const float fs_ = ImGui::GetFontSize();
    page_header(tr("Черга"), tr("Кілька рендерів підряд — гра запускається один раз."));
    auto* running = dynamic_cast<render::QueueJob*>(job_.get());
    const bool active = running && running->running();
    const auto results = running ? running->results() : std::vector<render::QueueJob::ItemResult>{};
    if (queue_.empty()) {
        // Порожня черга: значок, що це таке і як додати
        const float avail = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(0, fs_ * 2));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float box = std::round(fs_ * 3.4f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b0(p.x + std::round((avail - box) * 0.5f), p.y);
        dl->AddRectFilled(b0, ImVec2(b0.x + box, b0.y + box), kAccentSoft, box * 0.3f);
        draw_icon(dl, Icon::Queue, ImVec2(b0.x + box * 0.5f, b0.y + box * 0.5f), fs_ * 1.8f, kAccentText);
        ImGui::Dummy(ImVec2(avail, box + fs_ * 0.6f));
        auto centered = [&](const char* text, ImU32 col, bool bold) {
            if (bold) ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 1.2f);
            const float wrap = std::min(avail, fs_ * 36);
            const float tw = std::min(wrap, ImGui::CalcTextSize(text, nullptr, false, wrap).x);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - tw) * 0.5f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tw);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            if (bold) ImGui::PopFont();
        };
        centered(tr("Черга порожня"), kText, true);
        centered(tr("Кілька фрагментів чи демо підряд — наприклад, на ніч. Гра запускається один раз: після кожного "
                    "пункту вона не закривається, а одразу вмикає наступне демо."), kTextDim, false);
        centered(tr("Додати: налаштуйте демо, фрагмент і файл, як для звичайного рендеру, і натисніть «До черги» "
                    "вгорі праворуч. Ціле демо — правим кліком на сторінці «Бібліотека»."), kTextFaint, false);
        ImGui::Dummy(ImVec2(0, fs_ * 0.5f));
        const char* add = tr("Додати поточний рендер");
        const char* lib = tr("Бібліотека");
        const float bw = action_width(add, Kind::Secondary, Icon::Plus) + action_width(lib, Kind::Ghost, Icon::Library) +
                         ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - bw) * 0.5f));
        ImGui::BeginDisabled(!analysis_ || job_running() || s_.manual_mode || s_.output_path.empty());
        if (action_button(add, Kind::Secondary, 0, Icon::Plus)) add_to_queue();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (action_button(lib, Kind::Ghost, 0, Icon::Library)) go_to(Page::Library);
        return;
    }
    int move_from = -1, move_to = -1, remove = -1;
    if (ImGui::BeginTable("##queue", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(ImGui::GetFrameHeightWithSpacing() * 4,
                                             ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 1.4f)))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs_ * 1.6f);
        ImGui::TableSetupColumn(tr("Демо і фрагмент"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(tr("Файл"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn(tr("Стан"), ImGuiTableColumnFlags_WidthFixed, fs_ * 6.5f);
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 3 + 4);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < queue_.size(); ++i) {
            const auto& q = queue_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kColDim, "%zu", i + 1);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            std::string range = tr("усе демо");
            if ((q.s.start_tick > 0 || q.s.end_tick > 0) && q.tick_interval > 0)
                range = format_duration(std::max(0, q.s.start_tick) * q.tick_interval) + " – " +
                        (q.s.end_tick > 0 ? format_duration(q.s.end_tick * q.tick_interval) : std::string(tr("кінець")));
            ImGui::TextUnformatted(path_to_utf8(path_from_utf8(q.s.demo_path).filename()).c_str());
            ImGui::SameLine();
            ImGui::TextColored(kColDim, "%s", range.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%dx%d, %s fps, %s", q.s.demo_path.c_str(), q.s.width, q.s.height, q.s.fps.c_str(),
                                  q.s.video_codec.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(path_to_utf8(path_from_utf8(q.s.output_path).filename()).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", q.s.output_path.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (running && i < results.size()) {
                const auto& r = results[i];
                if (active && static_cast<int>(i) == running->current_index()) ImGui::TextColored(kColAccent, "%s", tr("рендер..."));
                else if (r.state == render::JobState::Succeeded) ImGui::TextColored(kColOk, "%s", tr("✓ готово"));
                else if (r.state == render::JobState::Failed) {
                    ImGui::TextColored(kColErr, "%s", tr("✗ помилка"));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.error.c_str());
                } else if (r.state == render::JobState::Cancelled) ImGui::TextColored(kColDim, "%s", tr("скасовано"));
                else ImGui::TextColored(kColDim, "%s", tr("чекає"));
            } else {
                ImGui::TextColored(kColDim, "%s", tr("чекає"));
            }
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(active);
            ImGui::BeginDisabled(i == 0);
            if (icon_button("##up", Icon::ChevronUp, tr("Вище"))) move_from = static_cast<int>(i), move_to = static_cast<int>(i) - 1;
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(i + 1 == queue_.size());
            if (icon_button("##down", Icon::ChevronDown, tr("Нижче"))) move_from = static_cast<int>(i), move_to = static_cast<int>(i) + 1;
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            if (icon_button("##remove", Icon::Close, tr("Прибрати з черги"))) remove = static_cast<int>(i);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (move_from >= 0 && move_to >= 0) {
        std::swap(queue_[static_cast<size_t>(move_from)], queue_[static_cast<size_t>(move_to)]);
        save_queue();
    }
    if (remove >= 0) {
        queue_.erase(queue_.begin() + remove);
        save_queue();
    }
    // Запуск черги — зелена кнопка
    ImGui::BeginDisabled(job_running() || queue_.empty());
    if (action_button(trf("Почати чергу ({})", queue_.size()).c_str(), Kind::Positive, 0, Icon::Play)) start_queue();
    ImGui::SameLine();
    if (action_button(tr("Очистити"), Kind::Secondary)) {
        queue_.clear();
        save_queue();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, fs_);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kColDim, "%s", tr("Потім:"));
    ImGui::SameLine();
    draw_after_done_combo();
    ImGui::SameLine();
    help_marker(tr("Якщо пункт не вдасться (чи гра впаде), черга піде далі — наступний пункт запустить гру заново. "
                "Розмір вікна гри, RTX і параметри запуску беруться з пункту; якщо вони інші, ніж у попереднього, "
                "гра перезапуститься. Готові пункти після черги зникають зі списку, невдалі лишаються."));
}

} // namespace gmdr::gui
