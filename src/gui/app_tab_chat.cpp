// =============================================================================
//  app_tab_chat.cpp — вкладка «Чат»: чат і події з демо, пошук, перехід до моменту;
//  позначки (список на вкладці «Фрагмент») і перегляд демо в грі з позначками;
//  розпізнані репліки голосового чату (whisper.cpp) — у тому самому списку.
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
#include <cmath>
#include <filesystem>
#include <format>

namespace gmdr::gui {

using namespace ui;
namespace fs = std::filesystem;

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
        log_warn("{}", trf("Не вдалося зберегти позначки: {}", err));
    mark_dirty();
}

void App::add_marker_at(int32_t tick, const std::string& title) {
    auto m = markers_;
    render::add_marker(m, {std::max(0, tick), title.empty() ? trf("Позначка {}", m.size() + 1) : title});
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
    ImGui::SeparatorText(tr("Позначки"));
    if (markers_.empty()) {
        ImGui::TextColored(kColDim, "%s", tr("Позначок ще немає. Ctrl+клік на шкалі (або правий клік → «Додати позначку»), "
                                          "F6 під час перегляду в грі чи кнопка нижче."));
    }
    int remove = -1;
    bool changed = false;
    auto& list = markers_;   // назви редагуються на місці, зберігаються після редагування
    if (!list.empty() && ImGui::BeginTable("##markers", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(tr("Час"), ImGuiTableColumnFlags_WidthFixed, fs_ * 5.5f);
        ImGui::TableSetupColumn(tr("Назва"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, fs_ * 9.5f);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, fs_ * 1.8f);
        for (size_t i = 0; i < list.size(); ++i) {
            auto& m = list[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(format_timecode(m.tick * ti).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("тік %d"), m.tick);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##t", &m.title, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
                changed = true;
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(job_running());
            if (ImGui::SmallButton(tr("Звідси"))) set_fragment_start(m.tick);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Почати фрагмент з цієї позначки"));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Сюди"))) set_fragment_end(m.tick);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Закінчити фрагмент на цій позначці"));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("У грі"))) start_watch(m.tick);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Переглянути демо в грі з цього місця"));
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Видалити позначку"));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove >= 0) {
        list.erase(list.begin() + remove);
        changed = true;
    }
    if (changed) set_markers(markers_);
    if (ImGui::Button(tr("Позначка на початку фрагмента"))) add_marker_at(std::max(0, s_.start_tick), {});
    ImGui::SameLine();
    if (ImGui::Checkbox(tr("Розділи у відео з позначок"), &s_.chapters)) mark_dirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Позначки всередині фрагмента стануть розділами MP4/MOV/MKV (плеєри показують їх на шкалі),\n"
                                "а поруч із відео з'явиться .chapters.txt з таймкодами для опису на YouTube."));
}

// ============================== Перегляд у грі ==============================
void App::start_watch(int32_t tick) {
    if (!analysis_ || job_running()) return;
    if (!gmod_) {
        popup_title_ = tr("Не знайдено Garry's Mod");
        popup_text_ = tr("Вкажіть папку гри на вкладці «Гра» (…\\steamapps\\common\\GarrysMod).");
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
            render::add_marker(list, {m.tick, trf("Позначка {}", list.size() + 1)});
            markers_changed = true;
        }
        focus_timeline(m.tick * analysis_->tick_interval);
    }
    if (markers_changed) set_markers(std::move(list));
}

// ============================== Вкладка «Чат» ==============================
// ---- Розпізнавання мовлення ----------------------------------------------------------
void App::refresh_whisper_status(bool force) {
    const double now = ImGui::GetTime();
    if (!force && now - whisper_checked_at_ < 5.0) return;   // файлова система — не щокадру
    whisper_checked_at_ = now;
    std::string why;
    if (auto t = speech::find_whisper(s_.whisper_cli, s_.whisper_model, &why)) {
        whisper_ok_ = true;
        whisper_status_ = tr("Модель: ") + path_to_utf8(t->model.filename());
    } else {
        whisper_ok_ = false;
        whisper_status_ = why;
    }
}

void App::start_transcribe(bool again) {
    if (!analysis_ || !voices_) return;
    if (job_running()) {
        log_warn("{}", trf("Зачекайте завершення поточного завдання"));
        return;
    }
    if (again) {
        std::error_code ec;
        fs::remove(speech::transcript_path(s_.demo_path), ec);
        transcript_.reset();
    }
    job_ = std::make_unique<render::TranscribeJob>(s_, analysis_, voices_);
    job_reported_ = false;
    job_->start();
}

void App::draw_speech_controls() {
    const float fs_ = ImGui::GetFontSize();
    refresh_whisper_status();
    const bool has_voices = voices_ && !voices_->speakers.empty();
    ImGui::BeginDisabled(job_running() || !has_voices || !whisper_ok_);
    // Розпізнано все демо — «ще раз» (заново); лише фрагменти (під час рендеру) — «все демо» (доповнити)
    const bool has = transcript_ && !transcript_->covered.empty();
    const bool full = has && std::any_of(transcript_->covered.begin(), transcript_->covered.end(),
                                         [](const speech::Coverage& c) { return c.from <= 0 && c.to >= 1e8; });
    if (ImGui::Button(full ? tr("Розпізнати ще раз") : has ? tr("Розпізнати все демо") : tr("Розпізнати мовлення")))
        start_transcribe(full);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr("Перетворити голосовий чат на текст (whisper.cpp, локально, без інтернету).\n"
                                "Репліки гравців з'являться в цьому списку поруч із чатом, і їх можна шукати.\n"
                                "Розпізнаються гравці, вибрані на вкладці «Звук і голос»; це займає кілька хвилин\n"
                                "(на процесорі — приблизно як тривалість самого мовлення)."));
    ImGui::SameLine();
    ImGui::TextColored(kColDim, "%s", tr("Мова:"));
    ImGui::SameLine();
    static const struct { const char* code; const char* label; } kLangs[] = {
        {"auto", tr("визначити")}, {"uk", tr("українська")}, {"ru", tr("російська")}, {"en", tr("англійська")},
        {"pl", tr("польська")},    {"de", tr("німецька")}};
    const char* cur = s_.whisper_language.c_str();
    for (const auto& l : kLangs)
        if (s_.whisper_language == l.code) cur = l.label;
    ImGui::SetNextItemWidth(fs_ * 7.5f);
    if (ImGui::BeginCombo("##wlang", cur)) {
        for (const auto& l : kLangs)
            if (ImGui::Selectable(l.label, s_.whisper_language == l.code)) {
                s_.whisper_language = l.code;
                mark_dirty();
            }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Якщо всі говорять однією мовою, краще вказати її — «визначити» дивиться лише\n"
                                "на перші 30 секунд мовлення кожного гравця."));
    ImGui::SameLine();
    if (whisper_ok_) {
        ImGui::TextColored(kColDim, "%s", whisper_status_.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Інша модель..."))) open_model_popup_ = true;
    } else {
        ImGui::TextColored(kColWarn, "%s", tr("Розпізнавання недоступне"));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", whisper_status_.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(job_running());
        if (ImGui::SmallButton(tr("Завантажити модель..."))) open_model_popup_ = true;
        ImGui::EndDisabled();
    }
    if (transcript_ && !transcript_->lines.empty())
        ImGui::TextColored(kColDim, tr("Розпізнано реплік: %zu (модель %s, мова %s)"), transcript_->lines.size(),
                           transcript_->model.c_str(), transcript_->language.c_str());
    draw_model_popup();
}

void App::draw_model_popup() {
    const float fs_ = ImGui::GetFontSize();
    if (open_model_popup_) {
        ImGui::OpenPopup(tr("Модель розпізнавання мовлення"));
        open_model_popup_ = false;
    }
    if (!ImGui::BeginPopupModal(tr("Модель розпізнавання мовлення"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("%s", tr("Розпізнавання працює локально (whisper.cpp). Потрібна модель — один файл, який\n"
                             "завантажується один раз з Hugging Face (ggerganov/whisper.cpp) у теку програми:"));
    ImGui::TextColored(kColDim, "%s", path_to_utf8(speech::models_download_dir()).c_str());
    ImGui::Spacing();
    const auto& models = speech::known_models();
    model_choice_ = std::clamp(model_choice_, 0, static_cast<int>(models.size()) - 1);
    for (int i = 0; i < static_cast<int>(models.size()); ++i) {
        const auto& m = models[static_cast<size_t>(i)];
        std::error_code ec;
        const bool have = fs::exists(speech::models_download_dir() / m.file, ec);
        ImGui::RadioButton(trf("{} — {} МБ{}", m.label, m.size_mb, have ? tr(" (є)") : "").c_str(), &model_choice_, i);
    }
    ImGui::Spacing();
    const auto& m = models[static_cast<size_t>(model_choice_)];
    std::error_code ec;
    const fs::path dest = speech::models_download_dir() / m.file;
    const bool have = fs::exists(dest, ec);
    ImGui::BeginDisabled(job_running());
    if (ImGui::Button(have ? tr("Використовувати цю") : trf("Завантажити ({} МБ)", m.size_mb).c_str(),
                      ImVec2(fs_ * 12, 0))) {
        if (have) {
            s_.whisper_model = path_to_utf8(dest);
            mark_dirty();
            refresh_whisper_status(true);
        } else {
            s_.whisper_model.clear();   // після завантаження візьметься найкраща наявна
            mark_dirty();
            job_ = std::make_unique<render::DownloadJob>(speech::model_url(m.file), dest,
                                                         std::string(tr("модель ")) + m.file);
            job_reported_ = false;
            job_->start();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Закрити"), ImVec2(fs_ * 7, 0))) ImGui::CloseCurrentPopup();
    if (!whisper_ok_ && whisper_status_.find("whisper-cli") != std::string::npos) {
        ImGui::Spacing();
        ImGui::TextColored(kColWarn, "%s", tr("Ще немає програми whisper-cli:"));
        ImGui::TextWrapped("%s", whisper_status_.c_str());
    }
    ImGui::EndPopup();
}

void App::draw_tab_chat() {
    const float fs_ = ImGui::GetFontSize();
    if (!analysis_) {
        ImGui::TextColored(kColDim, "%s", tr("Спершу відкрийте демо."));
        return;
    }
    const auto& A = *analysis_;
    const double ti = A.tick_interval;
    const size_t n_chat = A.count_events(demo::DemoEventKind::Chat);
    ImGui::TextColored(kColDim, tr("Повідомлень чату: %zu, від сервера: %zu, входів: %zu, виходів: %zu"), n_chat,
                       A.count_events(demo::DemoEventKind::Server), A.count_events(demo::DemoEventKind::Join),
                       A.count_events(demo::DemoEventKind::Leave));
    draw_speech_controls();
    const bool has_speech = transcript_ && !transcript_->lines.empty();
    if (A.events.empty() && !has_speech) {
        ImGui::TextWrapped("%s", tr("У цьому демо не знайдено ні чату, ні подій гравців. Чат є в демо, записаних на сервері "
                                 "(стандартний чат GMod і аддони чату, що передають текст через net-повідомлення). "
                                 "Голосовий чат можна перетворити на текст кнопкою «Розпізнати мовлення»."));
        return;
    }
    // ---- Фільтри ----
    ImGui::SetNextItemWidth(fs_ * 16);
    ImGui::InputTextWithHint("##chatsearch", tr("Пошук (текст або ім'я)"), &chat_search_);
    same_line_if_fits(tr("Чат"));
    ImGui::Checkbox(tr("Чат"), &chat_show_chat_);
    if (has_speech) {
        same_line_if_fits(tr("Голос"));
        ImGui::Checkbox(tr("Голос"), &chat_show_speech_);
    }
    same_line_if_fits(tr("Сервер"));
    ImGui::Checkbox(tr("Сервер"), &chat_show_server_);
    same_line_if_fits(tr("Входи й виходи"));
    ImGui::Checkbox(tr("Входи й виходи"), &chat_show_joins_);
    same_line_if_fits(tr("Лише у фрагменті"));
    ImGui::Checkbox(tr("Лише у фрагменті"), &chat_only_range_);

    // Рядки: події демо і (якщо є) розпізнані репліки — разом, за часом
    constexpr int kSpeechBase = 1000000000;   // номери рядків-реплік
    struct Row {
        int    id;   // < kSpeechBase — подія A.events, інакше репліка transcript_->lines
        double t;
    };
    auto format_row = [&](const Row& r) {
        if (r.id < kSpeechBase) return demo::format_event(A.events[static_cast<size_t>(r.id)]);
        const auto& l = transcript_->lines[static_cast<size_t>(r.id - kSpeechBase)];
        return l.speaker + tr(" (голос): ") + l.text;
    };
    auto all_rows = [&](bool filtered) {
        const std::string needle = to_lower(trim(chat_search_));
        const int32_t range_a = std::max(0, s_.start_tick);
        const int32_t range_b = s_.end_tick > 0 ? s_.end_tick : A.last_tick;
        auto in_range = [&](double t) {
            return !filtered || !chat_only_range_ || whole_demo_ || (t >= range_a * ti && t < range_b * ti);
        };
        auto matches = [&](const std::string& a, const std::string& b) {
            return !filtered || needle.empty() || to_lower(a).find(needle) != std::string::npos ||
                   to_lower(b).find(needle) != std::string::npos;
        };
        std::vector<Row> rows;
        rows.reserve(A.events.size());
        for (size_t i = 0; i < A.events.size(); ++i) {
            const auto& e = A.events[i];
            const bool is_join = e.kind == demo::DemoEventKind::Join || e.kind == demo::DemoEventKind::Leave ||
                                 e.kind == demo::DemoEventKind::NameChange;
            if (filtered) {
                if (e.kind == demo::DemoEventKind::Chat && !chat_show_chat_) continue;
                if ((e.kind == demo::DemoEventKind::Server || e.kind == demo::DemoEventKind::Kill) && !chat_show_server_)
                    continue;
                if (is_join && !chat_show_joins_) continue;
            }
            if (!in_range(e.tick * ti) || !matches(e.text, e.who)) continue;
            rows.push_back({static_cast<int>(i), e.tick * ti});
        }
        if (has_speech && (!filtered || chat_show_speech_)) {
            const size_t events_end = rows.size();
            for (size_t i = 0; i < transcript_->lines.size(); ++i) {
                const auto& l = transcript_->lines[i];
                if (in_range(l.start) && matches(l.text, l.speaker)) rows.push_back({kSpeechBase + static_cast<int>(i), l.start});
            }
            std::inplace_merge(rows.begin(), rows.begin() + static_cast<ptrdiff_t>(events_end), rows.end(),
                               [](const Row& a, const Row& b) { return a.t < b.t; });
        }
        return rows;
    };

    if (ImGui::Button(has_speech ? tr("Зберегти чат і розмови в .txt") : tr("Зберегти чат у .txt"))) {
        const fs::path demo = path_from_utf8(s_.demo_path);
        auto f = save_file_dialog(tr("Зберегти чат"), {{tr("Текст (*.txt)"), "*.txt"}},
                                  path_to_utf8(demo.parent_path() / (path_to_utf8(demo.stem()) + "_chat.txt")), "txt");
        if (!f.empty()) {
            std::string text;
            for (const auto& r : all_rows(false)) {
                const int h = static_cast<int>(r.t / 3600), mnt = static_cast<int>(std::fmod(r.t, 3600) / 60),
                          sec = static_cast<int>(std::fmod(r.t, 60));
                text += (h > 0 ? std::format("[{}:{:02}:{:02}] ", h, mnt, sec) : std::format("[{:02}:{:02}] ", mnt, sec)) +
                        format_row(r) + "\n";
            }
            std::string err;
            // BOM — щоб Блокнот і старі редактори одразу показали кирилицю
            if (write_file_text(path_from_utf8(f), "\xEF\xBB\xBF" + text, &err))
                log_info("{}", trf("Чат збережено: {}", f));
            else
                log_warn("{}", trf("Не вдалося зберегти чат: {}", err));
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(tr("Субтитри з чатом у відео (.srt)"), &s_.chat_srt)) mark_dirty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr("Поруч із відео — .srt з повідомленнями чату з фрагмента (кожне видно 7 с).\n"
                                "Корисно, якщо HUD приховано. Разом із субтитрами «хто говорить» — файл .chat.srt."));

    const std::vector<Row> rows = all_rows(true);
    ImGui::TextColored(kColDim, tr("Показано: %zu. Подвійний клік — фрагмент з цього моменту, правий клік — більше дій."), rows.size());

    // ---- Таблиця ----
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##chat", 3, flags, ImVec2(0, std::max(fs_ * 8, ImGui::GetContentRegionAvail().y)))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(tr("Час"), ImGuiTableColumnFlags_WidthFixed, fs_ * 4.8f);
    ImGui::TableSetupColumn(tr("Хто"), ImGuiTableColumnFlags_WidthFixed, fs_ * 9.0f);
    ImGui::TableSetupColumn(tr("Повідомлення"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    const ImVec4 kSpeechColor(0.62f, 0.84f, 1.0f, 1.0f);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const Row& row = rows[static_cast<size_t>(r)];
            const bool speech = row.id >= kSpeechBase;
            const demo::DemoEvent* e = speech ? nullptr : &A.events[static_cast<size_t>(row.id)];
            const speech::Line* l = speech ? &transcript_->lines[static_cast<size_t>(row.id - kSpeechBase)] : nullptr;
            const double t = row.t;
            const int32_t tick = static_cast<int32_t>(std::llround(t / ti));
            ImGui::PushID(row.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string ts = format_duration(t);
            const bool selected = chat_selected_ == row.id;
            if (ImGui::Selectable(ts.substr(0, ts.find('.')).c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                chat_selected_ = row.id;
                focus_timeline(t);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !job_running())
                    set_fragment_start(tick - static_cast<int32_t>(3.0 / ti));
            }
            if (ImGui::BeginPopupContextItem("##ctx")) {
                chat_selected_ = row.id;
                ImGui::BeginDisabled(job_running());
                if (ImGui::MenuItem(tr("Почати фрагмент за 3 с до цього"))) set_fragment_start(tick - static_cast<int32_t>(3.0 / ti));
                if (ImGui::MenuItem(tr("Закінчити фрагмент через 3 с після цього")))
                    set_fragment_end((speech ? static_cast<int32_t>(std::llround(l->end / ti)) : tick) +
                                     static_cast<int32_t>(3.0 / ti));
                if (ImGui::MenuItem(tr("Переглянути в грі звідси"))) start_watch(tick - static_cast<int32_t>(3.0 / ti));
                ImGui::EndDisabled();
                if (ImGui::MenuItem(tr("Додати позначку")))
                    add_marker_at(tick, speech ? l->speaker + ": " + l->text
                                               : e->kind == demo::DemoEventKind::Chat ? e->who + ": " + e->text
                                                                                      : demo::format_event(*e));
                if (ImGui::MenuItem(tr("Копіювати"))) clipboard_text_set(format_row(row));
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(speech ? l->speaker.c_str() : e->kind == demo::DemoEventKind::Chat ? e->who.c_str() : "");
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, speech ? kSpeechColor : event_color(e->kind));
            const std::string text =
                speech ? l->text
                       : e->kind == demo::DemoEventKind::Chat
                             ? (e->channel.empty() || e->channel == "global" ? "" : "(" + e->channel + ") ") + e->text
                             : demo::format_event(*e);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();
            if (speech && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Розпізнане мовлення (голосовий чат)"));
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

} // namespace gmdr::gui
