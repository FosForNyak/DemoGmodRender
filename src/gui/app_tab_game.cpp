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

    label("Папка Garry's Mod", lw);
    ImGui::SetNextItemWidth(ww - fs_ * 6.5f);
    if (ImGui::InputText("##gamedir", &s_.game_dir, ImGuiInputTextFlags_EnterReturnsTrue)) {
        detect_gmod(false);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Огляд...##gd")) {
        auto d = pick_folder_dialog("Папка Garry's Mod (…\\steamapps\\common\\GarrysMod)", s_.game_dir);
        if (!d.empty()) {
            s_.game_dir = d;
            detect_gmod(false);
            changed = true;
        }
    }
    ImGui::TextColored(gmod_ ? kColOk : kColErr, "%s", gmod_status_.c_str());
    if (gmod_) {
        label("Версія гри", lw);
        ImGui::SetNextItemWidth(ww * 0.55f);
        std::string cur = s_.game_exe.empty() ? "Автоматично: " + game::GModInstall::exe_label(gmod_->default_exe())
                                              : game::GModInstall::exe_label(path_from_utf8(s_.game_exe));
        if (ImGui::BeginCombo("##exe", cur.c_str())) {
            if (ImGui::Selectable("Автоматично (64-біт, якщо є)", s_.game_exe.empty())) {
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
        label("Драйвер рендеру", lw);
        switch (driver_state_) {
        case game::DriverState::Installed: ImGui::TextColored(kColOk, "встановлено"); break;
        case game::DriverState::Outdated: ImGui::TextColored(kColWarn, "застарів — буде оновлено"); break;
        default: ImGui::TextColored(kColDim, "буде встановлено автоматично"); break;
        }
        help_marker("Невеликий Lua-скрипт у меню GMod (lua/menu/gmdr_driver.lua + 1 рядок у menu.lua). Він запускає демо, вмикає startmovie точно на початку і вимикає в кінці. Сам нічого не робить, поки програма не створить завдання. Видалити — меню «Інструменти» або «Перевірити цілісність файлів» у Steam.");
    }

    // ---- GMod RTX ----
    if (!rtx_dir_.empty() || s_.rtx) {
        label("GMod RTX", lw);
        if (ImGui::Checkbox("копія від RTXLauncher##rtx", &s_.rtx)) changed = true;
        help_marker("Відео з трасуванням променів (RTX Remix) з копії гри, яку ставить RTXLauncher. Програма запускає її з "
                    "тими самими параметрами, що й RTXLauncher (-dxlevel 90 -nod3d9ex, -insecure), і довше розганяє демо, "
                    "щоб денойзер встиг зібрати історію кадрів. Рендер з RTX значно повільніший — спершу зробіть тестовий прогін.");
        if (!rtx_dir_.empty() && !iequals(path_to_utf8(path_from_utf8(s_.game_dir)), rtx_dir_)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Використати RTX-копію")) {
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
            ImGui::TextColored(kColWarn, "RTX потребує 64-бітної гри (гілка x86-64) — у цій папці її немає");
    }

    ImGui::SeparatorText("Рендер у грі");
    label("Вікно гри", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    const std::pair<const char*, const char*> modes[] = {{"offscreen", "За межами екрана (гра не видна)"},
                                                         {"behind", "Позаду інших вікон"},
                                                         {"normal", "На екрані (як раніше)"}};
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
    help_marker("За межами екрана — гра працює у фоні і не заважає: вікно створюється поза моніторами, без фокуса. "
                "Програма вимикає для гри енергозбереження Windows 11 (EcoQoS), через яке приховані вікна гальмують. "
                "Згортати гру не можна — згорнута гра не малює. Якщо кадри перестануть надходити, програма сама поверне "
                "вікно на екран (позаду інших). Подивитися на гру — кнопка «Показати гру» під час рендеру.");
    changed |= ImGui::Checkbox("Вимкнути звук гри в мікшері Windows на час рендеру", &s_.mute_game_sound);
    help_marker("Під час рендеру гра грає звук пришвидшено. У відео звук не зміниться: гра пише його у WAV окремо. "
                "Після рендеру звук GMod у мікшері вмикається назад.");
    label("Розмір вікна гри", lw);
    bool same = s_.render_width <= 0 || s_.render_height <= 0;
    if (ImGui::RadioButton("як відео", same)) {
        s_.render_width = s_.render_height = 0;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("×2 (суперсемплінг)", !same && s_.render_width == s_.width * 2)) {
        s_.render_width = s_.width * 2;
        s_.render_height = s_.height * 2;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("свій", !same && s_.render_width != s_.width * 2)) {
        if (same) {
            s_.render_width = s_.width;
            s_.render_height = s_.height;
        }
        changed = true;
    }
    help_marker("Гра рендерить у вікні такого розміру, а програма масштабує кадри до розміру відео. ×2 дає згладжування (сглажування країв), але вікно, більше за монітор, гра може не дозволити — тоді кадри просто масштабуються.");
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
    label("Формат кадрів", lw);
    if (ImGui::RadioButton("TGA (без втрат)", s_.capture_format != "jpg")) {
        s_.capture_format = "tga";
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("JPEG (менше диску)", s_.capture_format == "jpg")) {
        s_.capture_format = "jpg";
        changed = true;
    }
    if (s_.capture_format == "jpg") {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 7);
        changed |= ImGui::SliderInt("якість##jq", &s_.jpeg_quality, 50, 100);
    }
    help_marker("Гра передає кадри через тимчасові файли (прочитані одразу видаляються). TGA — найкраща якість. JPEG — у ~10 разів менше запису на диск.");
    label("Не показувати", lw);
    changed |= ImGui::Checkbox("HUD (інтерфейс)", &s_.hide_hud);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("руки і зброю", &s_.hide_viewmodel);
    label("Черга кадрів на диску", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    changed |= ImGui::SliderInt("##pending", &s_.max_pending_frames, 16, 600, "до %d кадрів");
    help_marker("Якщо кодування не встигає за грою, гра ставиться на паузу, щоб тимчасові кадри не заповнили диск.");
    changed |= ImGui::Checkbox("Закрити гру після рендеру", &s_.quit_game_when_done);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Високий пріоритет гри", &s_.high_priority);
    changed |= ImGui::Checkbox("Ручний режим (я сам керую записом у грі)", &s_.manual_mode);
    help_marker("Програма лише запустить гру з потрібними налаштуваннями. У консолі гри введіть gmdr_start щоб почати запис і gmdr_stop щоб зупинити — кадри кодуватимуться на льоту. Драйвер у меню не потрібен.");

    if (ImGui::TreeNode("Додатково##game")) {
        ImGui::TextUnformatted("Додаткові консольні команди (кожна з нового рядка):");
        changed |= ImGui::InputTextMultiline("##extra", &s_.extra_commands, ImVec2(-1, fs_ * 4.5f));
        help_marker("Виконуються перед запуском демо. Напр.: r_3dsky 0, mat_motion_blur_enabled 0, cl_drawhud 0, fov_desired 90, gmod_mcore_test 1.");
        label("Параметри запуску", lw);
        ImGui::SetNextItemWidth(ww);
        changed |= ImGui::InputTextWithHint("##launch", "напр. -dxlevel 95", &s_.extra_launch_args);
        label("Затримка меню", lw);
        ImGui::SetNextItemWidth(fs_ * 7);
        float md = static_cast<float>(s_.menu_delay);
        if (ImGui::SliderFloat("с##md", &md, 1, 30, "%.0f")) {
            s_.menu_delay = md;
            changed = true;
        }
        help_marker("Скільки секунд чекати в головному меню перед запуском демо (щоб встигли підвантажитися аддони).");
        changed |= ImGui::Checkbox("Залишати тимчасові файли (для діагностики)", &s_.keep_temp_files);
        ImGui::TreePop();
    }
    if (changed) mark_dirty();
}

} // namespace gmdr::gui
