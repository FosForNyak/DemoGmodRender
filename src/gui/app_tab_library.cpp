// =============================================================================
//  app_tab_library.cpp — вкладка «Демо»: усі демо з теки гри і ваших тек.
//  Пошук за назвою, картою, сервером і гравцем; подвійний клік — відкрити;
//  правий клік — відкрити, додати ціле демо до черги, показати в папці.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <format>

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

namespace {
std::string format_date(int64_t unix_time) {
    const std::time_t t = static_cast<std::time_t>(unix_time);
    char buf[32] = {};
    if (const std::tm* tm = std::localtime(&t)) std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tm);
    return buf;
}
} // namespace

std::vector<std::string> App::library_dirs() const {
    std::vector<std::string> dirs;
    if (gmod_) dirs.push_back(path_to_utf8(gmod_->garrysmod));
    for (const auto& d : split(s_.library_dirs, ';'))
        if (!trim(d).empty()) dirs.push_back(trim(d));
    return dirs;
}

void App::rescan_library() {
    if (library_future_.valid()) return;
    std::vector<fs::path> dirs;
    for (const auto& d : library_dirs()) dirs.push_back(path_from_utf8(d));
    library_future_ = std::async(std::launch::async, [dirs] { return demo::scan_demo_library(dirs); });
}

void App::poll_library() {
    if (!library_future_.valid() || library_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    library_ = library_future_.get();
    library_scanned_ = true;
}

void App::draw_tab_library() {
    const float fs_ = ImGui::GetFontSize();
    if (!library_scanned_ && !library_future_.valid()) rescan_library();
    poll_library();

    // ---- Панель: пошук, оновити, додати теку ----
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - fs_ * 17);
    ImGui::InputTextWithHint("##libsearch", tr("пошук: назва, карта, сервер, гравець"), &library_search_);
    ImGui::SameLine();
    ImGui::BeginDisabled(library_future_.valid());
    if (ImGui::Button(tr("Оновити"))) rescan_library();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Додати теку..."))) {
        const std::string d = pick_folder_dialog(tr("Тека з демо (разом із підтеками)"));
        if (!d.empty()) {
            s_.library_dirs += (s_.library_dirs.empty() ? "" : ";") + d;
            mark_dirty();
            rescan_library();
        }
    }

    std::vector<const demo::LibraryEntry*> rows;
    for (const auto& e : library_)
        if (demo::library_match(e, library_search_)) rows.push_back(&e);
    if (library_future_.valid()) ImGui::TextColored(kColDim, "%s", tr("Шукаю демо..."));
    else ImGui::TextColored(kColDim, tr("Демо: %zu%s"), rows.size(),
                            rows.size() != library_.size() ? trf(" з {}", library_.size()).c_str() : "");

    // ---- Таблиця ----
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp;
    const float table_h = std::max(fs_ * 8, ImGui::GetContentRegionAvail().y - fs_ * 3.2f);
    if (ImGui::BeginTable("##library", 5, tf, ImVec2(0, table_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("Демо"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn(tr("Карта"), ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn(tr("Тривалість"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(tr("Дата"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort |
                                            ImGuiTableColumnFlags_PreferSortDescending, 1.9f);
        ImGui::TableSetupColumn(tr("Розмір"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs(); ss && ss->SpecsCount > 0) {
            const auto& sp = ss->Specs[0];
            const bool asc = sp.SortDirection == ImGuiSortDirection_Ascending;
            std::stable_sort(rows.begin(), rows.end(), [&](const demo::LibraryEntry* a, const demo::LibraryEntry* b) {
                int c = 0;
                switch (sp.ColumnIndex) {
                case 0: c = to_lower(a->name).compare(to_lower(b->name)); break;
                case 1: c = a->map.compare(b->map); break;
                case 2: c = a->seconds < b->seconds ? -1 : a->seconds > b->seconds; break;
                case 3: c = a->modified < b->modified ? -1 : a->modified > b->modified; break;
                default: c = a->size < b->size ? -1 : a->size > b->size; break;
                }
                return asc ? c < 0 : c > 0;
            });
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& e = *rows[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool current = to_lower(e.path) == to_lower(s_.demo_path);
                ImGui::PushID(i);
                if (!e.error.empty()) ImGui::PushStyleColor(ImGuiCol_Text, kColDim);
                if (ImGui::Selectable(e.name.c_str(), current,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && e.error.empty())
                    load_demo(e.path);
                if (!e.error.empty()) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    std::string tip = e.folder;
                    if (!e.error.empty()) tip += "\n" + e.error;
                    else tip += trf("\nСервер: {}\nЗаписав: {}", e.server, e.recorded_by);
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                if (ImGui::BeginPopupContextItem("##libctx")) {
                    ImGui::BeginDisabled(!e.error.empty());
                    if (ImGui::MenuItem(tr("Відкрити"))) load_demo(e.path);
                    if (ImGui::MenuItem(tr("Додати до черги (усе демо)"))) add_demo_to_queue(e.path);
                    ImGui::EndDisabled();
                    if (ImGui::MenuItem(tr("Показати в папці"))) show_in_folder(e.path);
                    if (ImGui::MenuItem(tr("Копіювати шлях"))) clipboard_text_set(e.path);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(e.map.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(e.seconds > 0 ? format_duration(e.seconds).c_str() : (e.error.empty() ? "?" : "—"));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(format_date(e.modified).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(format_bytes(e.size).c_str());
            }
        }
        ImGui::EndTable();
    }

    // ---- Теки ----
    const auto dirs = library_dirs();
    std::string list;
    for (const auto& d : dirs) list += (list.empty() ? "" : "; ") + d;
    ImGui::TextColored(kColDim, tr("Теки: %s"), list.empty() ? tr("(гру не знайдено — додайте теку з демо)") : list.c_str());
    if (!s_.library_dirs.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Прибрати мої теки"))) {
            s_.library_dirs.clear();
            mark_dirty();
            rescan_library();
        }
    }
    help_marker(tr("Тека гри — верхній рівень garrysmod (туди пише консольна команда record) і garrysmod/demos з "
                "підтеками. Ваші теки переглядаються з підтеками. Подвійний клік — відкрити демо, правий клік — "
                "додати ціле демо до черги з поточними налаштуваннями."));
}

} // namespace gmdr::gui
