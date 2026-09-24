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

// =============================== Вкладка "Звук" ===================================
void App::draw_tab_audio() {
    const float fs_ = ImGui::GetFontSize();
    const float lw = fs_ * 10.5f;
    const float ww = std::max(fs_ * 14.0f, ImGui::GetContentRegionAvail().x - lw - fs_ * 1.5f);
    bool changed = false;
    changed |= checkbox(tr("Записувати звук"), &s_.audio);
    ImGui::Spacing();
    ImGui::BeginDisabled(!s_.audio);
    if (section(tr("Формат звуку"))) {
        label(tr("Аудіокодек"), lw);
        ImGui::SetNextItemWidth(ww * 0.55f);
        std::string ac_label = s_.audio_codec;
        for (const auto& c : audio_codecs_)
            if (c.name == s_.audio_codec) ac_label = c.label;
        const std::string cont = current_container();
        if (begin_combo("##acodec", ac_label.c_str())) {
            for (const auto& c : audio_codecs_) {
                std::string t = c.label;
                if (!is_image_container(cont) && media::container_supports(cont, c.name) == 0) t += tr("  [не для .") + cont + "]";
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
            if (begin_combo("##abr", s_.audio_bitrate.c_str())) {
                for (const char* r : rates)
                    if (ImGui::Selectable(r, s_.audio_bitrate == r)) {
                        s_.audio_bitrate = r;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
        }
        label(tr("Частота"), lw);
        for (int r : {44100, 48000, 96000}) {
            if (radio(trf("{} Гц##sr{}", r, r).c_str(), s_.sample_rate == r)) {
                s_.sample_rate = r;
                changed = true;
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (!is_image_container(cont) && media::container_supports(cont, s_.audio_codec) == 0)
            ImGui::TextColored(kColErr, tr("Формат .%s не підтримує цей аудіокодек"), cont.c_str());
    }

    ImGui::Spacing();
    if (section(tr("Звук гри"))) {
        changed |= checkbox(tr("Звук гри (постріли, кроки, музика...)"), &s_.game_audio);
        ImGui::BeginDisabled(!s_.game_audio);
        label(tr("Гучність"), lw);
        float gv = static_cast<float>(s_.game_volume * 100);
        if (slider_float("##gv", &gv, 0, 200, "%.0f%%", ww * 0.75f)) {
            s_.game_volume = gv / 100.0;
            changed = true;
        }
        label(tr("Зсув звуку гри"), lw);
        float go = static_cast<float>(s_.game_audio_offset * 1000);
        if (slider_float("##go", &go, -500, 500, tr("%.0f мс"), ww * 0.75f)) {
            s_.game_audio_offset = go / 1000.0;
            changed = true;
        }
        help_marker(tr("Якщо звук гри трохи відстає або випереджає картинку — підкрутіть тут. Зазвичай 0."));
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    if (section(tr("Голоси гравців з демо"))) {
        label(tr("Чиї голоси"), lw);
        ImGui::SetNextItemWidth(ww * 0.55f);
        const std::pair<const char*, const char*> modes[] = {{"all", tr("Усі гравці")},
                                                             {"local", tr("Лише мій голос")},
                                                             {"others", tr("Усі, крім мене")},
                                                             {"selected", tr("Вибрані (галочки в панелі «Голоси»)")},
                                                             {"none", tr("Без голосів")}};
        std::string mode_label = s_.voice_mode;
        for (const auto& [k, v] : modes)
            if (s_.voice_mode == k) mode_label = v;
        if (begin_combo("##vmode", mode_label.c_str())) {
            for (const auto& [k, v] : modes)
                if (ImGui::Selectable(v, s_.voice_mode == k)) {
                    s_.voice_mode = k;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(s_.voice_mode == "none");
        label(tr("Гучність голосу"), lw);
        float vv = static_cast<float>(s_.voice_volume * 100);
        if (slider_float("##vv", &vv, 0, 300, "%.0f%%", ww * 0.75f)) {
            s_.voice_volume = vv / 100.0;
            changed = true;
        }
        label(tr("Затримка голосу"), lw);
        float vd = static_cast<float>(s_.voice_delay * 1000);
        if (slider_float("##vd", &vd, -500, 1000, tr("%.0f мс"), ww * 0.75f)) {
            s_.voice_delay = vd / 1000.0;
            changed = true;
        }
        help_marker(tr("Голос ставиться на час, коли пакет прийшов у демо. Додатна затримка зсуває голос пізніше. "
                    "Гучність окремих гравців — повзунки в панелі «Голоси» і кнопки M/S на доріжках таймлайну."));
        ImGui::EndDisabled();
        changed |= checkbox(tr("Вимкнути голос усередині гри (рекомендовано)"), &s_.mute_engine_voice);
        help_marker(tr("Голоси декодуються програмою прямо з демо — чисто і точно. Якщо залишити голос у грі, він потрапить у «звук гри» і може задвоїтися з декодованим."));
        changed |= checkbox(tr("Субтитри «хто говорить» (.srt поруч із відео)"), &s_.subtitles_srt);
        help_marker(tr("Файл .srt з іменами гравців у моменти, коли вони говорять. VLC і mpv підхоплюють його самі, "
                    "а в програмі монтажу за ним легко знайти потрібні репліки."));
        if (s_.subtitles_srt) {
            ImGui::Indent();
            refresh_whisper_status();
            changed |= checkbox(tr("з текстом розмов (розпізнати мовлення)"), &s_.speech_subtitles);
            help_marker(tr("Замість самих імен — що саме гравці кажуть: «Ім'я: текст». Мовлення розпізнається локально "
                        "(whisper.cpp) перед рендером, лише для фрагмента; уже розпізнане (кнопка на вкладці «Чат») "
                        "береться готовим. Мова — на вкладці «Чат»."));
            if (s_.speech_subtitles && !whisper_ok_) {
                ImGui::SameLine();
                ImGui::TextColored(kColWarn, "%s", tr("немає моделі — див. вкладку «Чат»"));
            }
            ImGui::Unindent();
        }
        changed |= checkbox(tr("Підписи «хто говорить» прямо на відео"), &s_.speaker_overlay);
        help_marker(tr("Поки гравець говорить, праворуч унизу кадру видно плашку з його ніком — як індикатор голосового "
                    "чату в самій грі. Зручно, коли HUD приховано або голос гри вимкнено. Потрапляє в усі версії відео."));
        changed |= checkbox(tr("Окремі звукові доріжки (для монтажу)"), &s_.separate_tracks);
        help_marker(tr("Крім загального міксу, у файл буде записано окремі доріжки: гра, кожен гравець, мікрофон. Зручно для Premiere/DaVinci Resolve."));
        changed |= checkbox(tr("Пакет для монтажу (WAV + проєкт XML)"), &s_.edit_package);
        help_marker(tr("Поруч із відео з'явиться тека «назва_монтаж»: окремі WAV гри, кожного гравця і мікрофона (24 біт, "
                    "рівно від першого кадру) і проєкт XML. Premiere Pro і DaVinci Resolve відкривають його через "
                    "File → Import: відео і всі доріжки одразу на шкалі, позначки — маркерами."));
    }

    ImGui::Spacing();
    if (section(tr("Обробка звуку"))) {
        ImGui::BeginDisabled(s_.voice_mode == "none");
        changed |= checkbox(tr("Вирівняти гучність гравців"), &s_.voice_level);
        help_marker(tr("Кожного гравця доводимо до однакової гучності (-18 LUFS за EBU R128), виміряної по всьому його "
                    "мовленню в демо: тихих стає чутно, гучні не оглушують. Підсилення постійне, без «дихання». "
                    "Повзунки гучності в панелі «Голоси» діють поверх цього."));
        changed |= checkbox(tr("Шумодав для всіх гравців"), &s_.voice_denoise);
        help_marker(tr("Нейромережевий шумодав RNNoise (фільтр arnndn) прибирає фон — шипіння, клавіатуру, звук гри з "
                    "колонок — навіть коли він майже такий гучний, як мова. Потім гейт глушить паузи між фразами; його "
                    "поріг рахується для кожного гравця з його ж мовлення, тож тихі гравці не обрізаються.\n"
                    "Лише для окремих гравців — правий клік на імені в панелі «Голоси»."));
        if (!s_.voice_denoise) {
            size_t n = 0;
            for (const auto& k : split(s_.voice_denoise_players, ','))
                if (!trim(k).empty()) ++n;
            if (n > 0) {
                ImGui::SameLine();
                ImGui::TextColored(kColDim, tr("(зараз — для %zu гравц%s)"), n, n == 1 ? tr("я") : tr("ів"));
            }
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!s_.game_audio || s_.voice_mode == "none");
        changed |= checkbox(tr("Приглушувати звук гри, коли хтось говорить"), &s_.duck_game);
        help_marker(tr("Постріли й музика стихають, поки звучить голос, і плавно повертаються після фрази "
                    "(фільтр sidechaincompress). Окрема доріжка гри для монтажу лишається без змін."));
        ImGui::EndDisabled();
        label(tr("Гучність результату"), lw);
        ImGui::SetNextItemWidth(ww * 0.55f);
        const std::pair<double, const char*> targets[] = {{0.0, tr("Не змінювати")},
                                                          {-14.0, tr("-14 LUFS (YouTube, стрімінг)")},
                                                          {-16.0, tr("-16 LUFS (подкасти)")},
                                                          {-23.0, tr("-23 LUFS (EBU R128, ТБ)")}};
        std::string target_label = std::format("{:.0f} LUFS", s_.loudness_target);
        for (const auto& [v, t] : targets)
            if (std::abs(s_.loudness_target - v) < 0.05) target_label = t;
        if (begin_combo("##loud", target_label.c_str())) {
            for (const auto& [v, t] : targets)
                if (ImGui::Selectable(t, std::abs(s_.loudness_target - v) < 0.05)) {
                    s_.loudness_target = v;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        help_marker(tr("Загальний мікс доводиться до цієї гучності за EBU R128 (фільтр loudnorm), з обмеженням "
                    "піків -1.5 dBTP. YouTube і стрімінгові сервіси самі приглушують гучніше -14 LUFS."));
    }

    ImGui::Spacing();
    if (section(tr("Власний мікрофон (окремий запис)"))) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColDim);
        ImGui::TextWrapped("%s", tr("Ваш власний голос є в демо, лише якщо під час запису було ввімкнено voice_loopback 1. Інакше можна додати окремий запис мікрофона (OBS, Audacity тощо)."));
        ImGui::PopStyleColor();
        label(tr("Файл"), lw);
        ImGui::SetNextItemWidth(ww - ImGui::GetFrameHeight() - 4);
        changed |= ImGui::InputTextWithHint("##mic", tr("не вибрано (можна перетягнути у вікно)"), &s_.mic_file);
        ImGui::SameLine(0, 4);
        if (icon_button("##browsemic", Icon::Folder, tr("Огляд..."))) {
            auto f = open_file_dialog(tr("Запис мікрофона"), {{tr("Аудіо"), "*.wav;*.mp3;*.ogg;*.flac;*.m4a;*.opus;*.aac;*.mka"}, {tr("Усі файли"), "*.*"}});
            if (!f.empty()) {
                s_.mic_file = f;
                changed = true;
            }
        }
        ImGui::BeginDisabled(s_.mic_file.empty());
        label(tr("Зсув мікрофона"), lw);
        float mo = static_cast<float>(s_.mic_offset);
        if (hot_float("##mo", &mo, 0.01f, -3600.0f, 3600.0f, tr("%.3f с"))) {
            s_.mic_offset = mo;
            changed = true;
        }
        help_marker(tr("Час у відео, де починається файл мікрофона. Від'ємне значення обрізає початок файлу."));
        label(tr("Гучність мікрофона"), lw);
        float mv = static_cast<float>(s_.mic_volume * 100);
        if (slider_float("##mv", &mv, 0, 300, "%.0f%%", ww * 0.75f)) {
            s_.mic_volume = mv / 100.0;
            changed = true;
        }
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    if (changed) mark_dirty();
}

} // namespace gmdr::gui
