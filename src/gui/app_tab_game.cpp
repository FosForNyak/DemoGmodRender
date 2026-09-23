// =============================================================================
//  app_tab_game.cpp — вкладка «Гра»: папка гри, драйвер, вікно гри, кадри.
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
#include "core/util/i18n.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

// ================================ Вкладка "Гра" ===================================
void App::draw_tab_game() {
    const float fs_ = ImGui::GetFontSize();
    const float lw = fs_ * 11.5f;
    const float ww = std::max(fs_ * 14.0f, ImGui::GetContentRegionAvail().x - lw - fs_);
    bool changed = false;

    label(tr("Папка Garry's Mod"), lw);
    ImGui::SetNextItemWidth(ww - fs_ * 6.5f);
    if (ImGui::InputText("##gamedir", &s_.game_dir, ImGuiInputTextFlags_EnterReturnsTrue)) {
        detect_gmod(false);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Огляд...##gd"))) {
        auto d = pick_folder_dialog(tr("Папка Garry's Mod (…\\steamapps\\common\\GarrysMod)"), s_.game_dir);
        if (!d.empty()) {
            s_.game_dir = d;
            detect_gmod(false);
            changed = true;
        }
    }
    ImGui::TextColored(gmod_ ? kColOk : kColErr, "%s", gmod_status_.c_str());
    if (gmod_) {
        label(tr("Версія гри"), lw);
        ImGui::SetNextItemWidth(ww * 0.55f);
        std::string cur = s_.game_exe.empty() ? tr("Автоматично: ") + game::GModInstall::exe_label(gmod_->default_exe())
                                              : game::GModInstall::exe_label(path_from_utf8(s_.game_exe));
        if (ImGui::BeginCombo("##exe", cur.c_str())) {
            if (ImGui::Selectable(tr("Автоматично (64-біт, якщо є)"), s_.game_exe.empty())) {
                s_.game_exe.clear();
                changed = true;
            }
            for (const auto& e : gmod_->executables) {
                const std::string p = path_to_utf8(e);
                if (ImGui::Selectable((game::GModInstall::exe_label(e) + "##" + p).c_str(), s_.game_exe == p)) {
                    s_.game_exe = p;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        label(tr("Драйвер рендеру"), lw);
        switch (driver_state_) {
        case game::DriverState::Installed: ImGui::TextColored(kColOk, "%s", tr("встановлено")); break;
        case game::DriverState::Outdated: ImGui::TextColored(kColWarn, "%s", tr("застарів — буде оновлено")); break;
        default: ImGui::TextColored(kColDim, "%s", tr("буде встановлено автоматично")); break;
        }
        help_marker(tr("Невеликий Lua-скрипт у меню GMod (lua/menu/gmdr_driver.lua + 1 рядок у menu.lua). Він запускає демо, вмикає startmovie точно на початку і вимикає в кінці. Сам нічого не робить, поки програма не створить завдання. Видалити — меню «Інструменти» або «Перевірити цілісність файлів» у Steam."));
    }

    // ---- GMod RTX ----
    if (!rtx_dir_.empty() || s_.rtx) {
        label("GMod RTX", lw);
        if (ImGui::Checkbox(tr("копія від RTXLauncher##rtx"), &s_.rtx)) changed = true;
        help_marker(tr("Відео з трасуванням променів (RTX Remix) з копії гри, яку ставить RTXLauncher. Програма запускає її з "
                    "тими самими параметрами, що й RTXLauncher (-dxlevel 90 -nod3d9ex, -insecure), і довше розганяє демо, "
                    "щоб денойзер встиг зібрати історію кадрів. Рендер з RTX значно повільніший — спершу зробіть тестовий прогін."));
        if (!rtx_dir_.empty() && !iequals(path_to_utf8(path_from_utf8(s_.game_dir)), rtx_dir_)) {
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Використати RTX-копію"))) {
                s_.game_dir = rtx_dir_;
                s_.game_exe.clear();
                s_.rtx = true;
                detect_gmod(false);
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", rtx_dir_.c_str());
        }
        if (s_.rtx && gmod_ && std::none_of(gmod_->executables.begin(), gmod_->executables.end(), [](const fs::path& e) {
                return to_lower(path_to_utf8(e.parent_path().filename())) == "win64";
            }))
            ImGui::TextColored(kColWarn, "%s", tr("RTX потребує 64-бітної гри (гілка x86-64) — у цій папці її немає"));
        if (s_.rtx && s_.game_window == "offscreen")
            ImGui::TextColored(kColDim, "%s", tr("З RTX вікно гри буде позаду інших вікон: за межами екрана Remix не малює."));
    }

    ImGui::SeparatorText(tr("Рендер у грі"));
    label(tr("Вікно гри"), lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    const std::pair<const char*, const char*> modes[] = {{"offscreen", tr("За межами екрана (гра не видна)")},
                                                         {"behind", tr("Позаду інших вікон")},
                                                         {"normal", tr("На екрані (як раніше)")}};
    std::string mode_label = s_.game_window;
    for (const auto& [k, v] : modes)
        if (s_.game_window == k) mode_label = v;
    if (ImGui::BeginCombo("##gwin", mode_label.c_str())) {
        for (const auto& [k, v] : modes)
            if (ImGui::Selectable(v, s_.game_window == k)) {
                s_.game_window = k;
                changed = true;
            }
        ImGui::EndCombo();
    }
    help_marker(tr("За межами екрана — гра працює у фоні і не заважає: вікно створюється поза моніторами, без фокуса. "
                "Програма вимикає для гри енергозбереження Windows 11 (EcoQoS), через яке приховані вікна гальмують. "
                "Згортати гру не можна — згорнута гра не малює. Якщо кадри перестануть надходити, програма сама поверне "
                "вікно на екран (позаду інших). Подивитися на гру — кнопка «Показати гру» під час рендеру."));
    changed |= ImGui::Checkbox(tr("Вимкнути звук гри в мікшері Windows на час рендеру"), &s_.mute_game_sound);
    help_marker(tr("Під час рендеру гра грає звук пришвидшено. У відео звук не зміниться: гра пише його у WAV окремо. "
                "Після рендеру звук GMod у мікшері вмикається назад."));
    label(tr("Розмір вікна гри"), lw);
    bool same = s_.render_width <= 0 || s_.render_height <= 0;
    if (ImGui::RadioButton(tr("як відео"), same)) {
        s_.render_width = s_.render_height = 0;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("×2 (суперсемплінг)"), !same && s_.render_width == s_.width * 2)) {
        s_.render_width = s_.width * 2;
        s_.render_height = s_.height * 2;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("свій"), !same && s_.render_width != s_.width * 2)) {
        if (same) {
            s_.render_width = s_.width;
            s_.render_height = s_.height;
        }
        changed = true;
    }
    help_marker(tr("Гра рендерить у вікні такого розміру, а програма масштабує кадри до розміру відео. ×2 дає згладжування (сглажування країв), але вікно, більше за монітор, гра може не дозволити — тоді кадри просто масштабуються."));
    if (!same && s_.render_width != s_.width * 2) {
        label("", lw);
        ImGui::SetNextItemWidth(fs_ * 4.5f);
        changed |= ImGui::InputInt("##rw", &s_.render_width, 0);
        ImGui::SameLine();
        ImGui::TextUnformatted("×");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 4.5f);
        changed |= ImGui::InputInt("##rh", &s_.render_height, 0);
    }
    label(tr("Формат кадрів"), lw);
    if (ImGui::RadioButton(tr("TGA (без втрат)"), s_.capture_format != "jpg")) {
        s_.capture_format = "tga";
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("JPEG (менше диску)"), s_.capture_format == "jpg")) {
        s_.capture_format = "jpg";
        changed = true;
    }
    if (s_.capture_format == "jpg") {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 7);
        changed |= ImGui::SliderInt(tr("якість##jq"), &s_.jpeg_quality, 50, 100);
    }
    help_marker(tr("Гра передає кадри через тимчасові файли (прочитані одразу видаляються). TGA — найкраща якість. JPEG — у ~10 разів менше запису на диск."));
    label(tr("Не показувати"), lw);
    changed |= ImGui::Checkbox(tr("HUD (інтерфейс)"), &s_.hide_hud);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(tr("руки і зброю"), &s_.hide_viewmodel);
    label(tr("Черга кадрів на диску"), lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    changed |= ImGui::SliderInt("##pending", &s_.max_pending_frames, 16, 600, tr("до %d кадрів"));
    help_marker(tr("Якщо кодування не встигає за грою, гра ставиться на паузу, щоб тимчасові кадри не заповнили диск."));
    changed |= ImGui::Checkbox(tr("Закрити гру після рендеру"), &s_.quit_game_when_done);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(tr("Високий пріоритет гри"), &s_.high_priority);
    changed |= ImGui::Checkbox(tr("Ручний режим (я сам керую записом у грі)"), &s_.manual_mode);
    help_marker(tr("Програма лише запустить гру з потрібними налаштуваннями. У консолі гри введіть gmdr_start щоб почати запис і gmdr_stop щоб зупинити — кадри кодуватимуться на льоту. Драйвер у меню не потрібен."));

    if (ImGui::TreeNode(tr("Додатково##game"))) {
        ImGui::TextUnformatted(tr("Додаткові консольні команди (кожна з нового рядка):"));
        changed |= ImGui::InputTextMultiline("##extra", &s_.extra_commands, ImVec2(-1, fs_ * 4.5f));
        help_marker(tr("Виконуються перед запуском демо. Напр.: r_3dsky 0, mat_motion_blur_enabled 0, cl_drawhud 0, fov_desired 90, gmod_mcore_test 1."));
        label(tr("Параметри запуску"), lw);
        ImGui::SetNextItemWidth(ww);
        changed |= ImGui::InputTextWithHint("##launch", tr("напр. -dxlevel 95"), &s_.extra_launch_args);
        label(tr("Затримка меню"), lw);
        ImGui::SetNextItemWidth(fs_ * 7);
        float md = static_cast<float>(s_.menu_delay);
        if (ImGui::SliderFloat(tr("с##md"), &md, 1, 30, "%.0f")) {
            s_.menu_delay = md;
            changed = true;
        }
        help_marker(tr("Скільки секунд чекати в головному меню перед запуском демо (щоб встигли підвантажитися аддони)."));
        changed |= ImGui::Checkbox(tr("Залишати тимчасові файли (для діагностики)"), &s_.keep_temp_files);
        ImGui::TreePop();
    }
    if (changed) mark_dirty();
}

} // namespace gmdr::gui
