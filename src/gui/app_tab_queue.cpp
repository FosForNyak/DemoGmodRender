// =============================================================================
//  app_tab_queue.cpp — вкладка «Черга»: кілька фрагментів чи демо підряд (на ніч),
//  гра запускається один раз. Черга зберігається в gmdr_queue.json.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/util/file_util.hpp"
#include "core/util/json.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"

#include <algorithm>
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
    if (!queue_.empty()) log_info("Черга рендерів: {} пункт(ів) з минулого разу — вкладка «Черга»", queue_.size());
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
    log_info("До черги: {} → {}", path_to_utf8(path_from_utf8(q.s.demo_path).filename()), q.s.output_path);
    queue_.push_back(std::move(q));
    save_queue();
}

void App::start_queue() {
    if (queue_.empty() || job_running()) return;
    if (!gmod_) {
        popup_title_ = "Не знайдено Garry's Mod";
        popup_text_ = "Вкажіть папку гри на вкладці «Гра» (…\\steamapps\\common\\GarrysMod).";
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

void App::draw_tab_queue() {
    const float fs_ = ImGui::GetFontSize();
    auto* running = dynamic_cast<render::QueueJob*>(job_.get());
    const bool active = running && running->running();
    const auto results = running ? running->results() : std::vector<render::QueueJob::ItemResult>{};
    ImGui::TextWrapped("Кілька фрагментів чи демо підряд — наприклад, на ніч. Гра запускається один раз: після кожного "
                       "пункту вона не закривається, а одразу вмикає наступне демо.");
    ImGui::TextColored(kColDim, "Додати: налаштуйте демо, фрагмент і файл, як для звичайного рендеру, і натисніть «До черги» внизу.");
    ImGui::Spacing();
    if (queue_.empty()) {
        ImGui::TextColored(kColDim, "Черга порожня.");
        return;
    }
    int move_from = -1, move_to = -1, remove = -1;
    if (ImGui::BeginTable("##queue", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(ImGui::GetFrameHeightWithSpacing() * 4,
                                             ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.2f)))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs_ * 1.6f);
        ImGui::TableSetupColumn("Демо і фрагмент", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Файл", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Стан", ImGuiTableColumnFlags_WidthFixed, fs_ * 6.5f);
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, fs_ * 5.2f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < queue_.size(); ++i) {
            const auto& q = queue_[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn();
            std::string range = "усе демо";
            if ((q.s.start_tick > 0 || q.s.end_tick > 0) && q.tick_interval > 0)
                range = format_duration(std::max(0, q.s.start_tick) * q.tick_interval) + " – " +
                        (q.s.end_tick > 0 ? format_duration(q.s.end_tick * q.tick_interval) : std::string("кінець"));
            ImGui::TextUnformatted(path_to_utf8(path_from_utf8(q.s.demo_path).filename()).c_str());
            ImGui::SameLine();
            ImGui::TextColored(kColDim, "%s", range.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%dx%d, %s fps, %s", q.s.demo_path.c_str(), q.s.width, q.s.height, q.s.fps.c_str(),
                                  q.s.video_codec.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(path_to_utf8(path_from_utf8(q.s.output_path).filename()).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", q.s.output_path.c_str());
            ImGui::TableNextColumn();
            if (running && i < results.size()) {
                const auto& r = results[i];
                if (active && static_cast<int>(i) == running->current_index()) ImGui::TextColored(kColAccent, "рендер...");
                else if (r.state == render::JobState::Succeeded) ImGui::TextColored(kColOk, "✓ готово");
                else if (r.state == render::JobState::Failed) {
                    ImGui::TextColored(kColErr, "✗ помилка");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.error.c_str());
                } else if (r.state == render::JobState::Cancelled) ImGui::TextColored(kColDim, "скасовано");
                else ImGui::TextColored(kColDim, "чекає");
            } else {
                ImGui::TextColored(kColDim, "чекає");
            }
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(active);
            ImGui::BeginDisabled(i == 0);
            if (ImGui::SmallButton("↑")) move_from = static_cast<int>(i), move_to = static_cast<int>(i) - 1;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 == queue_.size());
            if (ImGui::SmallButton("↓")) move_from = static_cast<int>(i), move_to = static_cast<int>(i) + 1;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("✕")) remove = static_cast<int>(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Прибрати з черги");
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
    ImGui::BeginDisabled(job_running() || queue_.empty());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.66f, 0.36f, 1.0f));
    if (ImGui::Button(std::format("Почати чергу ({})", queue_.size()).c_str(), ImVec2(fs_ * 11, 0))) start_queue();
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Очистити")) {
        queue_.clear();
        save_queue();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    help_marker("Якщо пункт не вдасться (чи гра впаде), черга піде далі — наступний пункт запустить гру заново. "
                "Розмір вікна гри, RTX і параметри запуску беруться з пункту; якщо вони інші, ніж у попереднього, "
                "гра перезапуститься. Готові пункти після черги зникають зі списку, невдалі лишаються.");
}

} // namespace gmdr::gui
