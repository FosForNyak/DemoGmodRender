// =============================================================================
//  app_tab_chat.cpp — вкладка «Чат»: чат і події з демо, пошук, перехід до моменту;
//  позначки (список на вкладці «Фрагмент») і перегляд демо в грі з позначками.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <format>

namespace gmdr::gui {

using namespace ui;

namespace {
ImVec4 event_color(demo::DemoEventKind k) {
    switch (k) {
    case demo::DemoEventKind::Server: return ImVec4(0.95f, 0.78f, 0.40f, 1.0f);
    case demo::DemoEventKind::Join: return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
    case demo::DemoEventKind::Leave: return ImVec4(0.95f, 0.45f, 0.40f, 1.0f);
    case demo::DemoEventKind::NameChange: return ImVec4(0.65f, 0.65f, 0.95f, 1.0f);
    case demo::DemoEventKind::Kill: return ImVec4(0.95f, 0.55f, 0.30f, 1.0f);
    default: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

fs::path markers_store() { return app_data_dir() / "gmdr_markers.json"; }
} // namespace

// ============================== Позначки ==============================
void App::load_markers_for_demo() {
    markers_ = s_.demo_path.empty() ? std::vector<render::Marker>{} : render::load_demo_markers(markers_store(), s_.demo_path);
    s_.markers = render::format_markers(markers_);
}

void App::set_markers(std::vector<render::Marker> m) {
    markers_ = std::move(m);
    s_.markers = render::format_markers(markers_);
    std::string err;
    if (!s_.demo_path.empty() && !render::save_demo_markers(markers_store(), s_.demo_path, markers_, &err))
        log_warn("Не вдалося зберегти позначки: {}", err);
    mark_dirty();
}

void App::add_marker_at(int32_t tick, const std::string& title) {
    auto m = markers_;
    render::add_marker(m, {std::max(0, tick), title.empty() ? std::format("Позначка {}", m.size() + 1) : title});
    set_markers(std::move(m));
}

void App::set_fragment_start(int32_t tick) {
    if (!analysis_) return;
    const int32_t last = analysis_->last_tick;
    s_.start_tick = std::clamp(tick, 0, std::max(0, last - 1));
    if (s_.end_tick > 0 && s_.end_tick <= s_.start_tick) s_.end_tick = -1;
    whole_demo_ = false;
    mark_dirty();
}

void App::set_fragment_end(int32_t tick) {
    if (!analysis_) return;
    const int32_t last = analysis_->last_tick;
    s_.end_tick = tick >= last ? -1 : std::max(1, tick);
    if (s_.end_tick > 0 && s_.start_tick >= s_.end_tick) s_.start_tick = std::max(0, s_.end_tick - 1);
    whole_demo_ = false;
    mark_dirty();
}

// Показати момент на шкалі (якщо він поза видимою частиною — зсунути вигляд)
void App::focus_timeline(double seconds) {
    const double span = view_t1_ - view_t0_;
    if (seconds >= view_t0_ && seconds <= view_t1_) return;
    const double dur = analysis_ ? analysis_->duration_seconds : 0;
    const double a = std::clamp(seconds - span / 2, 0.0, std::max(0.0, dur - span));
    view_t0_ = static_cast<float>(a);
    view_t1_ = static_cast<float>(a + span);
}

void App::draw_markers_list() {
    if (!analysis_) return;
    const float fs_ = ImGui::GetFontSize();
    const double ti = analysis_->tick_interval;
    ImGui::SeparatorText("Позначки");
    if (markers_.empty()) {
        ImGui::TextColored(kColDim, "Позначок ще немає. Ctrl+клік на шкалі (або правий клік → «Додати позначку»), "
                                    "F6 під час перегляду в грі чи кнопка нижче.");
    }
    int remove = -1;
    bool changed = false;
    auto& list = markers_;   // назви редагуються на місці, зберігаються після редагування
    if (!list.empty() && ImGui::BeginTable("##markers", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Час", ImGuiTableColumnFlags_WidthFixed, fs_ * 5.5f);
        ImGui::TableSetupColumn("Назва", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, fs_ * 9.5f);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, fs_ * 1.8f);
        for (size_t i = 0; i < list.size(); ++i) {
            auto& m = list[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(format_timecode(m.tick * ti).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("тік %d", m.tick);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##t", &m.title, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
                changed = true;
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(job_running());
            if (ImGui::SmallButton("Звідси")) set_fragment_start(m.tick);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Почати фрагмент з цієї позначки");
            ImGui::SameLine();
            if (ImGui::SmallButton("Сюди")) set_fragment_end(m.tick);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Закінчити фрагмент на цій позначці");
            ImGui::SameLine();
            if (ImGui::SmallButton("У грі")) start_watch(m.tick);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Переглянути демо в грі з цього місця");
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Видалити позначку");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove >= 0) {
        list.erase(list.begin() + remove);
        changed = true;
    }
    if (changed) set_markers(markers_);
    if (ImGui::Button("Позначка на початку фрагмента")) add_marker_at(std::max(0, s_.start_tick), {});
    ImGui::SameLine();
    if (ImGui::Checkbox("Розділи у відео з позначок", &s_.chapters)) mark_dirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Позначки всередині фрагмента стануть розділами MP4/MOV/MKV (плеєри показують їх на шкалі),\n"
                          "а поруч із відео з'явиться .chapters.txt з таймкодами для опису на YouTube.");
}

// ============================== Перегляд у грі ==============================
void App::start_watch(int32_t tick) {
    if (!analysis_ || job_running()) return;
    if (!gmod_) {
        popup_title_ = "Не знайдено Garry's Mod";
        popup_text_ = "Вкажіть папку гри на вкладці «Гра» (…\\steamapps\\common\\GarrysMod).";
        open_popup_ = true;
        return;
    }
    save_settings_now();
    job_ = std::make_unique<render::WatchJob>(s_, analysis_, tick);
    job_reported_ = false;
    job_->start();
}

void App::apply_watch_marks() {
    auto* w = dynamic_cast<render::WatchJob*>(job_.get());
    if (!w) return;
    const auto marks = w->take_marks();
    if (marks.empty()) return;
    auto list = markers_;
    bool markers_changed = false;
    for (const auto& m : marks) {
        if (m.kind == "start") set_fragment_start(m.tick);
        else if (m.kind == "end") set_fragment_end(m.tick);
        else {
            render::add_marker(list, {m.tick, std::format("Позначка {}", list.size() + 1)});
            markers_changed = true;
        }
        focus_timeline(m.tick * analysis_->tick_interval);
    }
    if (markers_changed) set_markers(std::move(list));
}

// ============================== Вкладка «Чат» ==============================
void App::draw_tab_chat() {
    const float fs_ = ImGui::GetFontSize();
    if (!analysis_) {
        ImGui::TextColored(kColDim, "Спершу відкрийте демо.");
        return;
    }
    const auto& A = *analysis_;
    const double ti = A.tick_interval;
    const size_t n_chat = A.count_events(demo::DemoEventKind::Chat);
    ImGui::TextColored(kColDim, "Повідомлень чату: %zu, від сервера: %zu, входів: %zu, виходів: %zu", n_chat,
                       A.count_events(demo::DemoEventKind::Server), A.count_events(demo::DemoEventKind::Join),
                       A.count_events(demo::DemoEventKind::Leave));
    if (A.events.empty()) {
        ImGui::TextWrapped("У цьому демо не знайдено ні чату, ні подій гравців. Чат є в демо, записаних на сервері "
                           "(стандартний чат GMod і аддони чату, що передають текст через net-повідомлення).");
        return;
    }
    // ---- Фільтри ----
    ImGui::SetNextItemWidth(fs_ * 16);
    ImGui::InputTextWithHint("##chatsearch", "Пошук (текст або ім'я)", &chat_search_);
    ImGui::SameLine();
    ImGui::Checkbox("Чат", &chat_show_chat_);
    ImGui::SameLine();
    ImGui::Checkbox("Сервер", &chat_show_server_);
    ImGui::SameLine();
    ImGui::Checkbox("Входи й виходи", &chat_show_joins_);
    ImGui::SameLine();
    ImGui::Checkbox("Лише у фрагменті", &chat_only_range_);
    if (ImGui::Button("Зберегти чат у .txt")) {
        const fs::path demo = path_from_utf8(s_.demo_path);
        auto f = save_file_dialog("Зберегти чат", {{"Текст (*.txt)", "*.txt"}},
                                  path_to_utf8(demo.parent_path() / (path_to_utf8(demo.stem()) + "_chat.txt")), "txt");
        if (!f.empty()) {
            std::string err;
            // BOM — щоб Блокнот і старі редактори одразу показали кирилицю
            if (write_file_text(path_from_utf8(f), "\xEF\xBB\xBF" + demo::format_chat_log(A.events, ti), &err))
                log_info("Чат збережено: {}", f);
            else
                log_warn("Не вдалося зберегти чат: {}", err);
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Субтитри з чатом у відео (.srt)", &s_.chat_srt)) mark_dirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Поруч із відео — .srt з повідомленнями чату з фрагмента (кожне видно 7 с).\n"
                          "Корисно, якщо HUD приховано. Разом із субтитрами «хто говорить» — файл .chat.srt.");

    const std::string needle = to_lower(trim(chat_search_));
    const int32_t range_a = std::max(0, s_.start_tick);
    const int32_t range_b = s_.end_tick > 0 ? s_.end_tick : A.last_tick;
    std::vector<int> rows;
    rows.reserve(A.events.size());
    for (size_t i = 0; i < A.events.size(); ++i) {
        const auto& e = A.events[i];
        const bool is_join = e.kind == demo::DemoEventKind::Join || e.kind == demo::DemoEventKind::Leave ||
                             e.kind == demo::DemoEventKind::NameChange;
        if (e.kind == demo::DemoEventKind::Chat && !chat_show_chat_) continue;
        if ((e.kind == demo::DemoEventKind::Server || e.kind == demo::DemoEventKind::Kill) && !chat_show_server_) continue;
        if (is_join && !chat_show_joins_) continue;
        if (chat_only_range_ && !whole_demo_ && (e.tick < range_a || e.tick >= range_b)) continue;
        if (!needle.empty() && to_lower(e.text).find(needle) == std::string::npos &&
            to_lower(e.who).find(needle) == std::string::npos)
            continue;
        rows.push_back(static_cast<int>(i));
    }
    ImGui::TextColored(kColDim, "Показано: %zu. Подвійний клік — фрагмент з цього моменту, правий клік — більше дій.", rows.size());

    // ---- Таблиця ----
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##chat", 3, flags, ImVec2(0, std::max(fs_ * 8, ImGui::GetContentRegionAvail().y)))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Час", ImGuiTableColumnFlags_WidthFixed, fs_ * 4.8f);
    ImGui::TableSetupColumn("Хто", ImGuiTableColumnFlags_WidthFixed, fs_ * 9.0f);
    ImGui::TableSetupColumn("Повідомлення", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const int idx = rows[static_cast<size_t>(r)];
            const auto& e = A.events[static_cast<size_t>(idx)];
            const double t = e.tick * ti;
            ImGui::PushID(idx);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string ts = format_duration(t);
            const bool selected = chat_selected_ == idx;
            if (ImGui::Selectable(ts.substr(0, ts.find('.')).c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                chat_selected_ = idx;
                focus_timeline(t);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !job_running())
                    set_fragment_start(e.tick - static_cast<int32_t>(3.0 / ti));
            }
            if (ImGui::BeginPopupContextItem("##ctx")) {
                chat_selected_ = idx;
                ImGui::BeginDisabled(job_running());
                if (ImGui::MenuItem("Почати фрагмент за 3 с до цього")) set_fragment_start(e.tick - static_cast<int32_t>(3.0 / ti));
                if (ImGui::MenuItem("Закінчити фрагмент через 3 с після цього")) set_fragment_end(e.tick + static_cast<int32_t>(3.0 / ti));
                if (ImGui::MenuItem("Переглянути в грі звідси")) start_watch(e.tick - static_cast<int32_t>(3.0 / ti));
                ImGui::EndDisabled();
                if (ImGui::MenuItem("Додати позначку"))
                    add_marker_at(e.tick, e.kind == demo::DemoEventKind::Chat ? e.who + ": " + e.text : demo::format_event(e));
                if (ImGui::MenuItem("Копіювати")) clipboard_text_set(demo::format_event(e));
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.kind == demo::DemoEventKind::Chat ? e.who.c_str() : "");
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, event_color(e.kind));
            const std::string text = e.kind == demo::DemoEventKind::Chat
                                         ? (e.channel.empty() || e.channel == "global" ? "" : "(" + e.channel + ") ") + e.text
                                         : demo::format_event(e);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

} // namespace gmdr::gui
