// =============================================================================
//  app_page_settings.cpp — сторінка «Налаштування»: вигляд (тема, акцент,
//  масштаб, щільність, режим), мова, поведінка (сповіщення, трей, файли .dem,
//  оновлення), Garry's Mod (драйвер, пошук гри, GPU-кодеки), файли програми.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/util/file_assoc.hpp"
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

void App::draw_page_settings() {
    const float fs_ = ImGui::GetFontSize();
    page_header(tr("Налаштування"), tr("Вигляд програми, мова і поведінка. Налаштування рендеру — на сторінках «Відео», «Звук і голоси» і «Гра»."));

    // ---- Вигляд ----
    if (card_begin(tr("Вигляд"), tr("Тема, колір акценту, масштаб і щільність"), Icon::Sparkle, false)) {
        label(tr("Тема"));
        int theme = theme_index(s_.ui_theme);
        if (segmented("##theme", &theme, {tr("Темна"), tr("Світла"), tr("Як у Windows")}, std::min(field_width(), fs_ * 21))) {
            s_.ui_theme = theme_id(theme);
            theme_dirty_ = true;
            mark_dirty();
        }
        label(tr("Колір акценту"));
        const auto& accents = accent_presets();
        const int cur_accent = std::max(0, accent_index(s_.ui_accent));
        for (size_t i = 0; i < accents.size(); ++i) {
            if (i > 0) ImGui::SameLine(0, fs_ * 0.35f);
            if (swatch(std::format("##acc{}", i).c_str(), accents[i].color, static_cast<int>(i) == cur_accent, tr(accents[i].label))) {
                s_.ui_accent = accents[i].id;
                theme_dirty_ = true;
                mark_dirty();
            }
        }
        label(tr("Масштаб"));
        static const int kScales[] = {80, 90, 100, 110, 125, 150, 175, 200};
        const int cur_scale = static_cast<int>(std::lround(s_.ui_scale * 100));
        ImGui::SetNextItemWidth(field_width(9));
        if (begin_combo("##scale", std::format("{}%", cur_scale).c_str())) {
            for (int sc : kScales)
                if (ImGui::Selectable(std::format("{}%", sc).c_str(), sc == cur_scale)) {
                    s_.ui_scale = sc / 100.0;
                    theme_dirty_ = true;
                    mark_dirty();
                }
            ImGui::EndCombo();
        }
        help_marker(tr("Розмір тексту й елементів поверх масштабу Windows. Корисно на великих моніторах чи, навпаки, на ноутбуці."));
        label(tr("Щільність"));
        int dens = s_.ui_compact ? 1 : 0;
        if (segmented("##density", &dens, {tr("Звичайна"), tr("Компактна")}, std::min(field_width(), fs_ * 15))) {
            s_.ui_compact = dens == 1;
            theme_dirty_ = true;
            mark_dirty();
        }
        label(tr("Режим"));
        int mode = s_.ui_advanced ? 1 : 0;
        if (segmented("##uimode_s", &mode, {tr("Стандартний"), tr("Розширений")}, std::min(field_width(), fs_ * 15))) {
            s_.ui_advanced = mode == 1;
            mark_dirty();
        }
        help_marker(tr("Стандартний режим — лише головні налаштування.\n"
                    "Розширений — усі параметри кодеків, гри й звуку, а також сторінки\n"
                    "«Фрагмент і позначки», «Чат і мовлення» і «Журнал»."));
        bool labels = !s_.ui_sidebar_collapsed;
        if (toggle(tr("Підписи в бічній панелі"), &labels)) {
            s_.ui_sidebar_collapsed = !labels;
            mark_dirty();
        }
    }
    card_end();

    // ---- Мова ----
    if (card_begin(tr("Мова"), "Мова / Language", Icon::Globe, false)) {
        label(tr("Мова інтерфейсу"));
        std::string cur = tr("Як у Windows");
        for (const auto& l : ui_languages())
            if (s_.ui_language == l.code) cur = l.native;
        ImGui::SetNextItemWidth(field_width(16));
        if (begin_combo("##uilang", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable(tr("Як у Windows"), s_.ui_language.empty()) && !s_.ui_language.empty())
                set_language_after_restart("");
            for (const auto& l : ui_languages()) {
                if (ImGui::Selectable(l.native, s_.ui_language == l.code) && s_.ui_language != l.code)
                    set_language_after_restart(l.code);
                if (ImGui::IsItemHovered() && std::string(l.native) != l.english) ImGui::SetTooltip("%s", l.english);
            }
            ImGui::EndCombo();
        }
        help_marker(tr("Мова змінюється після перезапуску програми. Консольна версія: --lang або змінна GMDR_LANG."));
    }
    card_end();

    // ---- Поведінка ----
    if (card_begin(tr("Поведінка"), tr("Сповіщення, трей, файли .dem, оновлення"), Icon::Info, false)) {
        if (toggle(tr("Сповіщати, коли рендер готовий"), &s_.notify_when_done)) mark_dirty();
        help_marker(tr("Сповіщення Windows, коли рендер чи черга закінчились, а вікно згорнуте."));
        if (toggle(tr("Згортати в трей"), &s_.minimize_to_tray)) {
            platform_set_minimize_to_tray(s_.minimize_to_tray);
            mark_dirty();
        }
        help_marker(tr("Згорнуте вікно зникає з панелі задач, лишається значок біля годинника\n"
                    "(з прогресом рендеру в підказці). Клік по значку повертає вікно."));
#ifdef _WIN32
        {
            const fs::path exe = executable_dir() / "gmdr.exe";
            bool assoc = dem_association_registered(exe);
            if (toggle(tr("Відкривати .dem подвійним кліком"), &assoc)) {
                std::string err;
                if (assoc ? register_dem_association(exe, &err) : unregister_dem_association(&err))
                    log_info("{}", assoc ? tr("Файли .dem тепер відкриваються в GMod Demo Render (якщо Windows спитає, чим "
                                              "відкривати, — виберіть її). Вимкнути — тут само.")
                                         : tr("Файли .dem більше не відкриваються цією програмою"));
                else
                    log_error("{}", trf("Не вдалося змінити асоціацію .dem: {}", err));
            }
            help_marker(tr("Лише для вашого облікового запису (HKCU), без прав адміністратора.\n"
                        "Якщо програма вже відкрита, демо відкриється в ній."));
        }
#endif
        ImGui::BeginDisabled(update_future_.valid());
        if (action_button(tr("Перевірити оновлення"), Kind::Secondary, 0, Icon::Refresh)) check_updates();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kColDim, "%s", trf("Версія {}", GMDR_VERSION).c_str());
    }
    card_end();

    // ---- Garry's Mod ----
    if (card_begin("Garry's Mod", tr("Драйвер у меню гри, пошук гри, кодеки відеокарти"), Icon::Gamepad, false)) {
        const bool can = gmod_.has_value() && !job_running();
        ImGui::BeginDisabled(!can);
        if (action_button(tr("Встановити драйвер у GMod"), Kind::Secondary)) {
            std::string err;
            if (!game::install_driver(*gmod_, &err)) log_error("{}", err);
            driver_state_ = game::driver_state(*gmod_);
        }
        ImGui::SameLine();
        if (action_button(tr("Видалити драйвер з GMod"), Kind::Secondary)) {
            std::string err;
            if (!game::uninstall_driver(*gmod_, &err)) log_error("{}", err);
            driver_state_ = game::driver_state(*gmod_);
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(job_running());
        if (action_button(tr("Знайти Garry's Mod автоматично"), Kind::Secondary, 0, Icon::Search)) detect_gmod(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(gpu_probe_running_);
        if (action_button(tr("Перевірити GPU-кодеки ще раз"), Kind::Secondary, 0, Icon::Refresh)) start_gpu_probe();
        ImGui::EndDisabled();
    }
    card_end();

    // ---- Файли програми ----
    if (card_begin(tr("Файли програми"), nullptr, Icon::Folder, false)) {
        info_row(tr("Налаштування"), settings_path_);
        info_row(tr("Журнал"), path_to_utf8(app_data_dir() / "gmdr_log.txt"));
        if (action_button(tr("Відкрити журнал"), Kind::Secondary, 0, Icon::File)) open_path(path_to_utf8(app_data_dir() / "gmdr_log.txt"));
        ImGui::SameLine();
        if (action_button(tr("Відкрити папку програми"), Kind::Secondary, 0, Icon::Folder)) open_path(path_to_utf8(executable_dir()));
        ImGui::SameLine();
        if (action_button(tr("Зібрати звіт про проблему..."), Kind::Secondary)) make_report();
        ImGui::BeginDisabled(job_running());
        if (action_button(tr("Скинути налаштування рендеру"), Kind::Negative)) {
            // Вигляд, мова і шляхи лишаються — скидаються лише параметри рендеру
            render::RenderSettings fresh;
            fresh.demo_path = s_.demo_path;
            fresh.output_path = s_.output_path;
            fresh.game_dir = s_.game_dir;
            fresh.rtx_game_dir = s_.rtx_game_dir;
            fresh.rtx = s_.rtx;
            fresh.ui_language = s_.ui_language;
            fresh.ui_advanced = s_.ui_advanced;
            fresh.ui_theme = s_.ui_theme;
            fresh.ui_accent = s_.ui_accent;
            fresh.ui_scale = s_.ui_scale;
            fresh.ui_compact = s_.ui_compact;
            fresh.ui_sidebar_collapsed = s_.ui_sidebar_collapsed;
            fresh.ui_page = s_.ui_page;
            fresh.library_dirs = s_.library_dirs;
            fresh.notify_when_done = s_.notify_when_done;
            fresh.minimize_to_tray = s_.minimize_to_tray;
            s_ = fresh;
            mark_dirty();
        }
        ImGui::EndDisabled();
        help_marker(tr("Повертає типові значення відео, звуку, гри й фрагмента. Вигляд, мова, папки гри і бібліотеки лишаються."));
    }
    card_end();

    // ---- Про програму ----
    if (card_begin(tr("Про програму"), nullptr, Icon::Info, false)) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float ls = std::round(fs_ * 2.6f);
        draw_logo(ImGui::GetWindowDrawList(), p, ls);
        ImGui::Dummy(ImVec2(ls, ls));
        ImGui::SameLine(0, fs_ * 0.8f);
        ImGui::BeginGroup();
        ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 1.15f);
        ImGui::Text("GMod Demo Render %s", GMDR_VERSION);
        ImGui::PopFont();
        ImGui::TextColored(kColDim, "%s", tr("Рендер демо Garry's Mod у відео. Вільна програма з відкритим кодом (MIT)."));
        if (ImGui::TextLink("github.com/FosForNyak/DemoGmodRender")) open_path("https://github.com/FosForNyak/DemoGmodRender");
        ImGui::EndGroup();
    }
    card_end();
}

} // namespace gmdr::gui
