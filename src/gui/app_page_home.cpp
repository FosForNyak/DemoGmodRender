// =============================================================================
//  app_page_home.cpp — сторінка «Огляд»: усе про відкрите демо на одному екрані
//  (цифри, сервер, гравці, протокол, попередження) і що вийде після рендеру
//  (файл, формат, фрагмент, версії, субтитри); без демо — привітання, нещодавні
//  демо і стан гри.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/render/versions.hpp"
#include "core/speech/transcribe.hpp"
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
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
// Плитка з великою цифрою: значок, значення, підпис
void stat_tile(const char* label, const std::string& value, Icon icon, float w) {
    const float fs = ImGui::GetFontSize();
    const float u = fs / 15.0f;
    const float h = std::round(fs * 3.5f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kCard, 10 * u);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), kPanelLine, 10 * u);
    const float box = std::round(fs * 2.1f);
    const ImVec2 b0(p.x + std::round(12 * u), p.y + std::round((h - box) * 0.5f));
    dl->AddRectFilled(b0, ImVec2(b0.x + box, b0.y + box), kAccentSoft, 8 * u);
    draw_icon(dl, icon, ImVec2(b0.x + box * 0.5f, b0.y + box * 0.5f), fs * 1.1f, kAccentText);
    const float tx = b0.x + box + std::round(10 * u);
    dl->PushClipRect(p, ImVec2(p.x + w - 6 * u, p.y + h), true);
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 1.3f);
    const float vh = ImGui::GetFontSize();
    dl->AddText(ImVec2(tx, p.y + std::round(h * 0.5f - vh + 2 * u)), kText, value.c_str());
    ImGui::PopFont();
    dl->AddText(ImVec2(tx, p.y + std::round(h * 0.5f + 3 * u)), kTextDim, label);
    dl->PopClipRect();
}

// Ряд плиток на всю ширину (переносяться, якщо не влазять)
void stat_row(const std::vector<std::tuple<const char*, std::string, Icon>>& tiles) {
    const float fs = ImGui::GetFontSize();
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int per_row = std::max(1, std::min(static_cast<int>(tiles.size()), static_cast<int>((avail + sp) / (fs * 10.5f + sp))));
    const float w = std::floor((avail - sp * (per_row - 1)) / per_row);
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (i % per_row != 0) ImGui::SameLine(0, sp);
        stat_tile(std::get<0>(tiles[i]), std::get<1>(tiles[i]), std::get<2>(tiles[i]), w);
    }
}

std::string file_date(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return {};
    // Годинник файлів переводиться в системний через «зараз» обох (однаково в MSVC і GCC)
    const auto sys = std::chrono::system_clock::now() +
                     std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
    return format_date_time(std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count());
}
} // namespace

void App::draw_page_home() {
    const float fs_ = ImGui::GetFontSize();

    // ---- Без демо: привітання ----
    if (!analysis_) {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float logo = std::round(fs_ * 4.2f);
        ImGui::Dummy(ImVec2(0, fs_ * 1.2f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        draw_logo(ImGui::GetWindowDrawList(), ImVec2(p.x + std::round((avail - logo) * 0.5f), p.y), logo);
        ImGui::Dummy(ImVec2(avail, logo + fs_ * 0.8f));
        auto centered = [&](const char* text, bool bold, float scale, ImU32 col) {
            if (bold) ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * scale);
            const float tw = ImGui::CalcTextSize(text).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - tw) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            if (bold) ImGui::PopFont();
        };
        centered("GMod Demo Render", true, 1.7f, kText);
        centered(tr("Рендер демо Garry's Mod у відео — з голосами гравців, у будь-якій якості"), false, 1.0f, kTextDim);
        ImGui::Dummy(ImVec2(0, fs_ * 0.6f));
        const char* open = tr("Відкрити демо...");
        const float bw = action_width(open, Kind::Cta, Icon::Folder) + fs_ * 2;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - bw) * 0.5f);
        ImGui::BeginDisabled(job_running());
        if (action_button(open, Kind::Cta, bw, Icon::Folder)) open_demo_dialog();
        ImGui::EndDisabled();
        centered(tr("або перетягніть файл .dem у вікно"), false, 1.0f, kTextFaint);
        ImGui::Dummy(ImVec2(0, fs_ * 1.0f));

        // Нещодавні демо з бібліотеки
        if (!library_scanned_ && !library_future_.valid()) rescan_library();
        poll_library();
        std::vector<const demo::LibraryEntry*> recent;
        for (const auto& e : library_)
            if (e.error.empty()) recent.push_back(&e);
        std::sort(recent.begin(), recent.end(), [](auto* a, auto* b) { return a->modified > b->modified; });
        if (card_begin(tr("Нещодавні демо"), tr("З теки гри і ваших тек — клацніть, щоб відкрити"), Icon::Clock, false)) {
            if (recent.empty()) {
                ImGui::TextColored(kColDim, "%s", library_future_.valid() ? tr("Шукаю демо...")
                                                                         : tr("Демо не знайдено. Записати демо в грі: консоль → record назва, зупинити → stop."));
            } else if (ImGui::BeginTable("##recent", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(tr("Демо"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn(tr("Карта"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn(tr("Тривалість"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn(tr("Дата"), ImGuiTableColumnFlags_WidthStretch, 1.7f);
                ImGui::TableHeadersRow();
                ImGui::BeginDisabled(job_running());
                for (size_t i = 0; i < std::min<size_t>(recent.size(), 8); ++i) {
                    const auto& e = *recent[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(e.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) load_demo(e.path);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.path.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(e.map.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(e.seconds > 0 ? format_duration(e.seconds).c_str() : "?");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kColDim, "%s", format_date_time(e.modified).c_str());
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
                ImGui::EndTable();
            }
            if (!recent.empty() && action_button(tr("Уся бібліотека"), Kind::Ghost, 0, Icon::Library)) go_to(Page::Library);
        }
        card_end();
        if (card_begin(tr("Гра"), nullptr, Icon::Gamepad, false)) {
            info_row(tr("Режим"), (s_.game_renderer == "rtx") ? "GMod RTX" : tr("Стандарт"));
            info_row(tr("Папка"), gmod_status_, gmod_ ? kGreen : kRed);
            if (action_button(tr("Налаштувати гру"), Kind::Ghost, 0, Icon::Gear)) go_to(Page::Game);
        }
        card_end();
        return;
    }

    // ---- Демо відкрито ----
    const auto& A = *analysis_;
    const std::string server = A.header.server_name.empty() ? A.server_info.host_name : A.header.server_name;
    page_header(A.header.map_name.c_str(), server.c_str());

    double speech = 0;
    if (voices_)
        for (const auto& sp : voices_->speakers) speech += sp.seconds;
    stat_row({{tr("тривалість"), format_duration(A.duration_seconds), Icon::Clock},
              {tr("гравців"), std::to_string(A.players.size()), Icon::User},
              {tr("голосового чату"), format_duration(speech), Icon::Mic},
              {tr("повідомлень чату"), std::to_string(A.count_events(demo::DemoEventKind::Chat)), Icon::Chat},
              {tr("позначок"), std::to_string(markers_.size()), Icon::Marker}});
    ImGui::Spacing();

    const float avail = ImGui::GetContentRegionAvail().x;
    const bool two = avail > fs_ * 44;
    if (two) {
        ImGui::BeginTable("##homecols", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
    }

    // ---- Демо ----
    if (card_begin(tr("Демо-запис"), nullptr, Icon::Film)) {
        const fs::path path = path_from_utf8(s_.demo_path);
        info_row(tr("Файл"), path_to_utf8(path.filename()));
        info_row(tr("Розмір і дата"), format_bytes(file_size_or_zero(path)) + "  ·  " + file_date(path));
        info_row(tr("Карта"), A.header.map_name);
        if (!A.server_info.gamemode.empty()) info_row(tr("Режим гри"), A.server_info.gamemode);
        info_row(tr("Сервер"), server);
        info_row(tr("Записав"), A.header.client_name);
        info_row(tr("Тривалість"), trf("{}  ·  {} тіків, {:.0f} тік/с", format_duration(A.duration_seconds), A.last_tick, 1.0 / A.tick_interval));
        if (A.server_info.max_clients > 0) info_row(tr("Місць на сервері"), std::to_string(A.server_info.max_clients));
        info_row(tr("Формат"), std::format("{} · demo {} · net {}{}", A.header.stamp, A.header.demo_protocol, A.header.network_protocol,
                                           A.variant.gmod_2026 ? " · GMod 2026" : ""));
        if (!A.voice_codec.empty()) info_row(tr("Голосовий кодек"), trf("{}, якість {}", A.voice_codec, A.voice_quality));
        info_row(tr("Події"), trf("входів {}, виходів {}, від сервера {}", A.count_events(demo::DemoEventKind::Join),
                                  A.count_events(demo::DemoEventKind::Leave), A.count_events(demo::DemoEventKind::Server)));
        if (A.packets_failed > 0)
            info_row(tr("Пакети"), trf("не розібрано {} з {}", A.packets_failed, A.packets_total), kOrange);
        if (action_button(tr("Показати в папці"), Kind::Ghost, 0, Icon::Folder)) show_in_folder(s_.demo_path);
    }
    card_end();

    // ---- Гравці ----
    if (card_begin(tr("Гравці"), tr("Хто був на сервері, скільки говорив і писав у чат"), Icon::User)) {
        std::map<std::string, double> speech_by_name;
        std::map<uint64_t, double> speech_by_id;
        if (voices_)
            for (const auto& sp : voices_->speakers) {
                if (sp.steamid64) speech_by_id[sp.steamid64] += sp.seconds;
                speech_by_name[sp.name] += sp.seconds;
            }
        std::map<std::string, int> msgs;
        for (const auto& e : A.events)
            if (e.kind == demo::DemoEventKind::Chat) ++msgs[e.who];
        const float row_h = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2;
        const float h = std::min(row_h * static_cast<float>(A.players.size() + 1) + 2, fs_ * 18);
        if (ImGui::BeginTable("##players", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0, h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(tr("Гравець"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
            ImGui::TableSetupColumn("SteamID", ImGuiTableColumnFlags_WidthStretch, 1.8f);
            ImGui::TableSetupColumn(tr("Мовлення"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(tr("Чат"), ImGuiTableColumnFlags_WidthStretch, 0.7f);
            ImGui::TableHeadersRow();
            for (const auto& [slot, pl] : A.players) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool local = slot == A.local_slot;
                if (local) ImGui::TextColored(kColAccent, "%s%s", pl.name.c_str(), tr(" (записав)"));
                else ImGui::TextUnformatted(pl.name.c_str());
                if (pl.fake_player) {
                    ImGui::SameLine();
                    ImGui::TextColored(kColDim, "%s", tr("бот"));
                }
                ImGui::TableNextColumn();
                ImGui::TextColored(kColDim, "%s", pl.guid.empty() ? "—" : pl.guid.c_str());
                ImGui::TableNextColumn();
                double sec = 0;
                if (pl.steamid64 && speech_by_id.count(pl.steamid64)) sec = speech_by_id[pl.steamid64];
                else if (speech_by_name.count(pl.name)) sec = speech_by_name[pl.name];
                ImGui::TextUnformatted(sec > 0 ? format_duration(sec).c_str() : "—");
                ImGui::TableNextColumn();
                const int m = msgs.count(pl.name) ? msgs[pl.name] : 0;
                ImGui::TextUnformatted(m > 0 ? std::to_string(m).c_str() : "—");
            }
            ImGui::EndTable();
        }
        if (action_button(tr("Голоси і гучність"), Kind::Ghost, 0, Icon::Speaker)) go_to(Page::Audio);
    }
    card_end();

    if (two) ImGui::TableNextColumn();

    // ---- Результат рендеру ----
    if (card_begin(tr("Результат рендеру"), tr("Що вийде, якщо натиснути «Почати рендер»"), Icon::Sparkle)) {
        draw_output_card();
        const double ti = A.tick_interval;
        info_row(tr("Фрагмент"), whole_demo_ ? std::string(tr("увесь запис"))
                                              : format_duration(std::max(0, s_.start_tick) * ti) + " – " +
                                                    format_duration((s_.end_tick > 0 ? s_.end_tick : A.last_tick) * ti));
        std::string extras;
        for (const auto& v : render::version_presets())
            if (("," + s_.extra_versions + ",").find("," + v.id + ",") != std::string::npos) extras += (extras.empty() ? "" : ", ") + v.label;
        info_row(tr("Ще версії"), extras.empty() ? std::string(tr("немає")) : extras);
        std::string subs;
        auto add = [&](bool on, const char* what) {
            if (on) subs += (subs.empty() ? "" : ", ") + std::string(what);
        };
        add(s_.subtitles_srt, s_.speech_subtitles ? tr("текст розмов") : tr("хто говорить"));
        add(s_.chat_srt, tr("чат"));
        add(s_.speaker_overlay, tr("підписи на кадрі"));
        add(s_.chapters && !markers_.empty(), tr("розділи"));
        add(s_.edit_package, tr("пакет для монтажу"));
        info_row(tr("Додатково"), subs.empty() ? std::string(tr("немає")) : subs);
        if (s_.motion_blur > 1 || std::abs(s_.speed - 1.0) > 1e-6)
            info_row(tr("Рух"), trf("розмиття ×{}, швидкість ×{:g}", s_.motion_blur, s_.speed));
        ImGui::Spacing();
        const bool can_start = !job_running() && !(analyze_job_ && analyze_job_->running());
        ImGui::BeginDisabled(!can_start);
        if (action_button(tr("Почати рендер"), Kind::Cta, 0, Icon::Play)) start_render();
        ImGui::SameLine();
        ImGui::BeginDisabled(s_.manual_mode);
        if (action_button(tr("Тест 3 с"), Kind::Secondary)) start_render(true);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (action_button(tr("Налаштувати відео"), Kind::Ghost, 0, Icon::Gear)) go_to(Page::Video);
    }
    card_end();

    // ---- Гра і система ----
    if (card_begin(tr("Гра і система"), nullptr, Icon::Gamepad)) {
        info_row(tr("Режим"), (s_.game_renderer == "rtx") ? "GMod RTX" : tr("Стандарт"));
        info_row(tr("Гра"), gmod_status_, gmod_ ? kGreen : kRed);
        if (gmod_)
            info_row(tr("Драйвер рендеру"), driver_state_ == game::DriverState::Installed  ? std::string(tr("встановлено"))
                                            : driver_state_ == game::DriverState::Outdated ? std::string(tr("застарів — буде оновлено"))
                                                                                            : std::string(tr("буде встановлено автоматично")));
        {
            std::lock_guard lock(gpu_mutex_);
            std::string gpu;
            for (const auto& [name, st] : gpu_status_)
                if (st == 1) gpu += (gpu.empty() ? "" : ", ") + name;
            info_row(tr("GPU-кодеки"), gpu_probe_running_ ? std::string(tr("перевіряються...")) : gpu.empty() ? std::string(tr("немає")) : gpu);
        }
        refresh_whisper_status();
        info_row(tr("Розпізнавання мовлення"), whisper_ok_ ? whisper_status_ : std::string(tr("модель не завантажено")),
                 whisper_ok_ ? 0 : kTextDim);
        if (action_button(tr("Налаштувати гру"), Kind::Ghost, 0, Icon::Gear)) go_to(Page::Game);
    }
    card_end();

    // ---- Попередження ----
    if (!A.warnings.empty()) {
        if (card_begin(tr("Попередження"), nullptr, Icon::Warning)) {
            for (const auto& w : A.warnings) {
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
        card_end();
    }
    if (two) ImGui::EndTable();
}

} // namespace gmdr::gui
