// =============================================================================
//  app_tab_audio.cpp — вкладка «Звук і голос».
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

// =============================== Вкладка "Звук" ===================================
void App::draw_tab_audio() {
    const float fs_ = ImGui::GetFontSize();
    const float lw = fs_ * 11.5f;
    const float ww = std::max(fs_ * 14.0f, ImGui::GetContentRegionAvail().x - lw - fs_);
    bool changed = false;
    changed |= ImGui::Checkbox("Записувати звук", &s_.audio);
    ImGui::BeginDisabled(!s_.audio);

    label("Аудіокодек", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    std::string ac_label = s_.audio_codec;
    for (const auto& c : audio_codecs_)
        if (c.name == s_.audio_codec) ac_label = c.label;
    const std::string cont = current_container();
    if (ImGui::BeginCombo("##acodec", ac_label.c_str())) {
        for (const auto& c : audio_codecs_) {
            std::string t = c.label;
            if (!is_image_container(cont) && media::container_supports(cont, c.name) == 0) t += "  [не для ." + cont + "]";
            if (ImGui::Selectable((t + "##" + c.name).c_str(), c.name == s_.audio_codec)) {
                s_.audio_codec = c.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    const bool lossless = s_.audio_codec.rfind("pcm_", 0) == 0 || s_.audio_codec == "flac" || s_.audio_codec == "alac";
    if (!lossless) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 6.5f);
        const char* rates[] = {"96k", "128k", "192k", "256k", "320k", "384k", "512k"};
        if (ImGui::BeginCombo("##abr", s_.audio_bitrate.c_str())) {
            for (const char* r : rates)
                if (ImGui::Selectable(r, s_.audio_bitrate == r)) {
                    s_.audio_bitrate = r;
                    changed = true;
                }
            ImGui::EndCombo();
        }
    }
    label("Частота", lw);
    for (int r : {44100, 48000, 96000}) {
        if (ImGui::RadioButton(std::format("{} Гц##sr{}", r, r).c_str(), s_.sample_rate == r)) {
            s_.sample_rate = r;
            changed = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (!is_image_container(cont) && media::container_supports(cont, s_.audio_codec) == 0)
        ImGui::TextColored(kColErr, "Формат .%s не підтримує цей аудіокодек", cont.c_str());

    ImGui::SeparatorText("Звук гри");
    changed |= ImGui::Checkbox("Звук гри (постріли, кроки, музика...)", &s_.game_audio);
    ImGui::BeginDisabled(!s_.game_audio);
    label("Гучність", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    float gv = static_cast<float>(s_.game_volume * 100);
    if (ImGui::SliderFloat("##gv", &gv, 0, 200, "%.0f%%")) {
        s_.game_volume = gv / 100.0;
        changed = true;
    }
    label("Зсув звуку гри", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    float go = static_cast<float>(s_.game_audio_offset * 1000);
    if (ImGui::SliderFloat("##go", &go, -500, 500, "%.0f мс")) {
        s_.game_audio_offset = go / 1000.0;
        changed = true;
    }
    help_marker("Якщо звук гри трохи відстає або випереджає картинку — підкрутіть тут. Зазвичай 0.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Голоси гравців з демо");
    label("Чиї голоси", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    const std::pair<const char*, const char*> modes[] = {{"all", "Усі гравці"},
                                                         {"local", "Лише мій голос"},
                                                         {"others", "Усі, крім мене"},
                                                         {"selected", "Вибрані (праворуч у списку)"},
                                                         {"none", "Без голосів"}};
    std::string mode_label = s_.voice_mode;
    for (const auto& [k, v] : modes)
        if (s_.voice_mode == k) mode_label = v;
    if (ImGui::BeginCombo("##vmode", mode_label.c_str())) {
        for (const auto& [k, v] : modes)
            if (ImGui::Selectable(v, s_.voice_mode == k)) {
                s_.voice_mode = k;
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(s_.voice_mode == "none");
    label("Гучність голосу", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    float vv = static_cast<float>(s_.voice_volume * 100);
    if (ImGui::SliderFloat("##vv", &vv, 0, 300, "%.0f%%")) {
        s_.voice_volume = vv / 100.0;
        changed = true;
    }
    label("Затримка голосу", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    float vd = static_cast<float>(s_.voice_delay * 1000);
    if (ImGui::SliderFloat("##vd", &vd, -500, 1000, "%.0f мс")) {
        s_.voice_delay = vd / 1000.0;
        changed = true;
    }
    help_marker("Голос ставиться на час, коли пакет прийшов у демо. Додатна затримка зсуває голос пізніше. "
                "Гучність окремих гравців — повзунки в таблиці голосів праворуч.");
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox("Вимкнути голос усередині гри (рекомендовано)", &s_.mute_engine_voice);
    help_marker("Голоси декодуються програмою прямо з демо — чисто і точно. Якщо залишити голос у грі, він потрапить у «звук гри» і може задвоїтися з декодованим.");
    changed |= ImGui::Checkbox("Субтитри «хто говорить» (.srt поруч із відео)", &s_.subtitles_srt);
    help_marker("Файл .srt з іменами гравців у моменти, коли вони говорять. VLC і mpv підхоплюють його самі, "
                "а в програмі монтажу за ним легко знайти потрібні репліки.");
    changed |= ImGui::Checkbox("Окремі звукові доріжки (для монтажу)", &s_.separate_tracks);
    help_marker("Крім загального міксу, у файл буде записано окремі доріжки: гра, кожен гравець, мікрофон. Зручно для Premiere/DaVinci Resolve.");

    ImGui::SeparatorText("Власний мікрофон (окремий запис)");
    ImGui::TextWrapped("Ваш власний голос є в демо, лише якщо під час запису було ввімкнено voice_loopback 1. Інакше можна додати окремий запис мікрофона (OBS, Audacity тощо).");
    label("Файл", lw);
    ImGui::SetNextItemWidth(ww - fs_ * 6.5f);
    changed |= ImGui::InputTextWithHint("##mic", "не вибрано (можна перетягнути у вікно)", &s_.mic_file);
    ImGui::SameLine();
    if (ImGui::Button("Огляд...##mic")) {
        auto f = open_file_dialog("Запис мікрофона", {{"Аудіо", "*.wav;*.mp3;*.ogg;*.flac;*.m4a;*.opus;*.aac;*.mka"}, {"Усі файли", "*.*"}});
        if (!f.empty()) {
            s_.mic_file = f;
            changed = true;
        }
    }
    ImGui::BeginDisabled(s_.mic_file.empty());
    label("Зсув мікрофона", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    float mo = static_cast<float>(s_.mic_offset);
    if (ImGui::DragFloat("##mo", &mo, 0.01f, -3600.0f, 3600.0f, "%.3f с")) {
        s_.mic_offset = mo;
        changed = true;
    }
    help_marker("Час у відео, де починається файл мікрофона. Від'ємне значення обрізає початок файлу.");
    label("Гучність мікрофона", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    float mv = static_cast<float>(s_.mic_volume * 100);
    if (ImGui::SliderFloat("##mv", &mv, 0, 300, "%.0f%%")) {
        s_.mic_volume = mv / 100.0;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (changed) mark_dirty();
}

} // namespace gmdr::gui
