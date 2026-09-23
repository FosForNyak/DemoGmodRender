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
    if (s_.subtitles_srt) {
        ImGui::Indent();
        refresh_whisper_status();
        changed |= ImGui::Checkbox("з текстом розмов (розпізнати мовлення)", &s_.speech_subtitles);
        help_marker("Замість самих імен — що саме гравці кажуть: «Ім'я: текст». Мовлення розпізнається локально "
                    "(whisper.cpp) перед рендером, лише для фрагмента; уже розпізнане (кнопка на вкладці «Чат») "
                    "береться готовим. Мова — на вкладці «Чат».");
        if (s_.speech_subtitles && !whisper_ok_) {
            ImGui::SameLine();
            ImGui::TextColored(kColWarn, "немає моделі — див. вкладку «Чат»");
        }
        ImGui::Unindent();
    }
    changed |= ImGui::Checkbox("Підписи «хто говорить» прямо на відео", &s_.speaker_overlay);
    help_marker("Поки гравець говорить, праворуч унизу кадру видно плашку з його ніком — як індикатор голосового "
                "чату в самій грі. Зручно, коли HUD приховано або голос гри вимкнено. Потрапляє в усі версії відео.");
    changed |= ImGui::Checkbox("Окремі звукові доріжки (для монтажу)", &s_.separate_tracks);
    help_marker("Крім загального міксу, у файл буде записано окремі доріжки: гра, кожен гравець, мікрофон. Зручно для Premiere/DaVinci Resolve.");
    changed |= ImGui::Checkbox("Пакет для монтажу (WAV + проєкт XML)", &s_.edit_package);
    help_marker("Поруч із відео з'явиться тека «назва_монтаж»: окремі WAV гри, кожного гравця і мікрофона (24 біт, "
                "рівно від першого кадру) і проєкт XML. Premiere Pro і DaVinci Resolve відкривають його через "
                "File → Import: відео і всі доріжки одразу на шкалі, позначки — маркерами.");

    ImGui::SeparatorText("Обробка звуку");
    ImGui::BeginDisabled(s_.voice_mode == "none");
    changed |= ImGui::Checkbox("Вирівняти гучність гравців", &s_.voice_level);
    help_marker("Кожного гравця доводимо до однакової гучності (-18 LUFS за EBU R128), виміряної по всьому його "
                "мовленню в демо: тихих стає чутно, гучні не оглушують. Підсилення постійне, без «дихання». "
                "Повзунки гучності в таблиці голосів діють поверх цього.");
    changed |= ImGui::Checkbox("Шумодав для всіх гравців", &s_.voice_denoise);
    help_marker("Нейромережевий шумодав RNNoise (фільтр arnndn) прибирає фон — шипіння, клавіатуру, звук гри з "
                "колонок — навіть коли він майже такий гучний, як мова. Потім гейт глушить паузи між фразами; його "
                "поріг рахується для кожного гравця з його ж мовлення, тож тихі гравці не обрізаються.\n"
                "Лише для окремих гравців — правий клік на імені в таблиці голосів.");
    if (!s_.voice_denoise) {
        size_t n = 0;
        for (const auto& k : split(s_.voice_denoise_players, ','))
            if (!trim(k).empty()) ++n;
        if (n > 0) {
            ImGui::SameLine();
            ImGui::TextColored(kColDim, "(зараз — для %zu гравц%s)", n, n == 1 ? "я" : "ів");
        }
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!s_.game_audio || s_.voice_mode == "none");
    changed |= ImGui::Checkbox("Приглушувати звук гри, коли хтось говорить", &s_.duck_game);
    help_marker("Постріли й музика стихають, поки звучить голос, і плавно повертаються після фрази "
                "(фільтр sidechaincompress). Окрема доріжка гри для монтажу лишається без змін.");
    ImGui::EndDisabled();
    label("Гучність результату", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    const std::pair<double, const char*> targets[] = {{0.0, "Не змінювати"},
                                                      {-14.0, "-14 LUFS (YouTube, стрімінг)"},
                                                      {-16.0, "-16 LUFS (подкасти)"},
                                                      {-23.0, "-23 LUFS (EBU R128, ТБ)"}};
    std::string target_label = std::format("{:.0f} LUFS", s_.loudness_target);
    for (const auto& [v, t] : targets)
        if (std::abs(s_.loudness_target - v) < 0.05) target_label = t;
    if (ImGui::BeginCombo("##loud", target_label.c_str())) {
        for (const auto& [v, t] : targets)
            if (ImGui::Selectable(t, std::abs(s_.loudness_target - v) < 0.05)) {
                s_.loudness_target = v;
                changed = true;
            }
        ImGui::EndCombo();
    }
    help_marker("Загальний мікс доводиться до цієї гучності за EBU R128 (фільтр loudnorm), з обмеженням "
                "піків -1.5 dBTP. YouTube і стрімінгові сервіси самі приглушують гучніше -14 LUFS.");

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
