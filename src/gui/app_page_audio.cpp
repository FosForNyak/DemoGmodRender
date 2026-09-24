// =============================================================================
//  app_page_audio.cpp — сторінка «Звук і голоси»: звук у відео, голоси гравців
//  (таблиця з гучністю й прослуховуванням), обробка голосу, субтитри й підписи,
//  пакет для монтажу, окремий запис мікрофона.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/media/ffmpeg_util.hpp"
#include "core/media/muxer.hpp"
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace gmdr::gui {

using namespace ui;

void App::draw_page_audio() {
    const float fs_ = ImGui::GetFontSize();
    const bool adv = s_.ui_advanced;
    bool changed = false;
    page_header(tr("Звук і голоси"), tr("Звук гри, голоси гравців із демо, обробка голосу і ваш мікрофон."));

    if (card_begin(tr("Звук у відео"), nullptr, Icon::Speaker)) {
        changed |= toggle(tr("Записувати звук"), &s_.audio);
        ImGui::BeginDisabled(!s_.audio);
        const std::string cont = current_container();
        if (adv) {
            label(tr("Аудіокодек"));
            ImGui::SetNextItemWidth(field_width(14));
            std::string ac_label = s_.audio_codec;
            for (const auto& c : audio_codecs_)
                if (c.name == s_.audio_codec) ac_label = c.label;
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
            label(tr("Частота"));
            for (int r : {44100, 48000, 96000}) {
                if (radio(trf("{} Гц##sr{}", r, r).c_str(), s_.sample_rate == r)) {
                    s_.sample_rate = r;
                    changed = true;
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
        if (!is_image_container(cont) && media::container_supports(cont, s_.audio_codec) == 0)
            ImGui::TextColored(kColErr, tr("Формат .%s не підтримує цей аудіокодек"), cont.c_str());
        changed |= toggle(tr("Звук гри (постріли, кроки, музика...)"), &s_.game_audio);
        ImGui::BeginDisabled(!s_.game_audio);
        label(tr("Гучність гри"));
        float gv = static_cast<float>(s_.game_volume * 100);
        if (slider_float("##gv", &gv, 0, 200, "%.0f%%", field_width(20))) {
            s_.game_volume = gv / 100.0;
            changed = true;
        }
        if (adv) {
            label(tr("Зсув звуку гри"));
            float go = static_cast<float>(s_.game_audio_offset * 1000);
            if (slider_float("##go", &go, -500, 500, tr("%.0f мс"), field_width(20))) {
                s_.game_audio_offset = go / 1000.0;
                changed = true;
            }
            help_marker(tr("Якщо звук гри трохи відстає або випереджає картинку — підкрутіть тут. Зазвичай 0."));
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    card_end();

    ImGui::BeginDisabled(!s_.audio);
    if (card_begin(tr("Голоси гравців"), tr("Декодовані прямо з демо — чисто і точно в часі"), Icon::User)) {
        label(tr("Чиї голоси"));
        ImGui::SetNextItemWidth(field_width(20));
        const std::pair<const char*, const char*> modes[] = {{"all", tr("Усі гравці")},
                                                             {"local", tr("Лише мій голос")},
                                                             {"others", tr("Усі, крім мене")},
                                                             {"selected", tr("Вибрані (галочки в таблиці нижче)")},
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
        label(tr("Гучність голосу"));
        float vv = static_cast<float>(s_.voice_volume * 100);
        if (slider_float("##vv", &vv, 0, 300, "%.0f%%", field_width(20))) {
            s_.voice_volume = vv / 100.0;
            changed = true;
        }
        if (adv) {
            label(tr("Затримка голосу"));
            float vd = static_cast<float>(s_.voice_delay * 1000);
            if (slider_float("##vd", &vd, -500, 1000, tr("%.0f мс"), field_width(20))) {
                s_.voice_delay = vd / 1000.0;
                changed = true;
            }
            help_marker(tr("Голос ставиться на час, коли пакет прийшов у демо. Додатна затримка зсуває голос пізніше."));
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        draw_voice_table(fs_ * 16);
        if (adv) {
            changed |= toggle(tr("Вимкнути голос усередині гри (рекомендовано)"), &s_.mute_engine_voice);
            help_marker(tr("Голоси декодуються програмою прямо з демо — чисто і точно. Якщо залишити голос у грі, він потрапить у «звук гри» і може задвоїтися з декодованим."));
        }
    }
    card_end();

    if (card_begin(tr("Обробка голосу"), tr("Рівна гучність, шумодав, приглушення гри"), Icon::Sparkle)) {
        ImGui::BeginDisabled(s_.voice_mode == "none");
        changed |= toggle(tr("Вирівняти гучність гравців"), &s_.voice_level);
        help_marker(tr("Кожного гравця доводимо до однакової гучності (-18 LUFS за EBU R128), виміряної по всьому його "
                    "мовленню в демо: тихих стає чутно, гучні не оглушують. Підсилення постійне, без «дихання». "
                    "Повзунки гучності в таблиці голосів діють поверх цього."));
        changed |= toggle(tr("Шумодав для всіх гравців"), &s_.voice_denoise);
        help_marker(tr("Нейромережевий шумодав RNNoise (фільтр arnndn) прибирає фон — шипіння, клавіатуру, звук гри з "
                    "колонок — навіть коли він майже такий гучний, як мова. Потім гейт глушить паузи між фразами; його "
                    "поріг рахується для кожного гравця з його ж мовлення, тож тихі гравці не обрізаються.\n"
                    "Лише для окремих гравців — правий клік на імені в таблиці голосів."));
        if (!s_.voice_denoise) {
            size_t n = 0;
            for (const auto& k : split(s_.voice_denoise_players, ','))
                if (!trim(k).empty()) ++n;
            if (n > 0) {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kColDim, tr("(зараз — для %zu гравц%s)"), n, n == 1 ? tr("я") : tr("ів"));
            }
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!s_.game_audio || s_.voice_mode == "none");
        changed |= toggle(tr("Приглушувати звук гри, коли хтось говорить"), &s_.duck_game);
        help_marker(tr("Постріли й музика стихають, поки звучить голос, і плавно повертаються після фрази "
                    "(фільтр sidechaincompress). Окрема доріжка гри для монтажу лишається без змін."));
        ImGui::EndDisabled();
        label(tr("Гучність результату"));
        ImGui::SetNextItemWidth(field_width(20));
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
    card_end();

    if (card_begin(tr("Субтитри й підписи"), tr("Хто говорить — у файлі .srt і прямо на кадрі"), Icon::Chat)) {
        changed |= toggle(tr("Субтитри «хто говорить» (.srt поруч із відео)"), &s_.subtitles_srt);
        help_marker(tr("Файл .srt з іменами гравців у моменти, коли вони говорять. VLC і mpv підхоплюють його самі, "
                    "а в програмі монтажу за ним легко знайти потрібні репліки."));
        if (s_.subtitles_srt) {
            ImGui::Indent();
            refresh_whisper_status();
            changed |= toggle(tr("з текстом розмов (розпізнати мовлення)"), &s_.speech_subtitles);
            help_marker(tr("Замість самих імен — що саме гравці кажуть: «Ім'я: текст». Мовлення розпізнається локально "
                        "(whisper.cpp) перед рендером, лише для фрагмента; уже розпізнане (сторінка «Чат і мовлення») "
                        "береться готовим."));
            if (s_.speech_subtitles && !whisper_ok_) {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kColWarn, "%s", tr("немає моделі розпізнавання"));
                ImGui::SameLine();
                if (action_button(tr("Завантажити модель..."), Kind::Ghost, 0, Icon::Download)) open_model_popup_ = true;
            }
            ImGui::Unindent();
        }
        changed |= toggle(tr("Підписи «хто говорить» прямо на відео"), &s_.speaker_overlay);
        help_marker(tr("Поки гравець говорить, праворуч унизу кадру видно плашку з його ніком — як індикатор голосового "
                    "чату в самій грі. Зручно, коли HUD приховано або голос гри вимкнено. Потрапляє в усі версії відео."));
        changed |= toggle(tr("Субтитри з чатом у відео (.srt)"), &s_.chat_srt);
        help_marker(tr("Поруч із відео — .srt з повідомленнями чату з фрагмента (кожне видно 7 с).\n"
                    "Корисно, якщо HUD приховано. Разом із субтитрами «хто говорить» — файл .chat.srt."));
        draw_model_popup();
    }
    card_end();

    if (adv) {
        if (card_begin(tr("Для монтажу"), tr("Окремі доріжки і проєкт для Premiere Pro / DaVinci Resolve"), Icon::Film)) {
            changed |= toggle(tr("Окремі звукові доріжки у файлі"), &s_.separate_tracks);
            help_marker(tr("Крім загального міксу, у файл буде записано окремі доріжки: гра, кожен гравець, мікрофон. Зручно для Premiere/DaVinci Resolve."));
            changed |= toggle(tr("Пакет для монтажу (WAV + проєкт XML)"), &s_.edit_package);
            help_marker(tr("Поруч із відео з'явиться тека: окремі WAV гри, кожного гравця і мікрофона (24 біт, "
                        "рівно від першого кадру) і проєкт XML. Premiere Pro і DaVinci Resolve відкривають його через "
                        "File → Import: відео і всі доріжки одразу на шкалі, позначки — маркерами."));
        }
        card_end();
    }

    if (card_begin(tr("Власний мікрофон"), tr("Окремий запис вашого голосу (OBS, Audacity, Discord)"), Icon::Mic, true,
                   !s_.mic_file.empty())) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColDim);
        ImGui::TextWrapped("%s", tr("Ваш власний голос є в демо, лише якщо під час запису було ввімкнено voice_loopback 1. Інакше можна додати окремий запис мікрофона (OBS, Audacity тощо)."));
        ImGui::PopStyleColor();
        label(tr("Файл"));
        ImGui::SetNextItemWidth(field_width() - ImGui::GetFrameHeight() - 4);
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
        label(tr("Зсув мікрофона"));
        float mo = static_cast<float>(s_.mic_offset);
        if (hot_float("##mo", &mo, 0.01f, -3600.0f, 3600.0f, tr("%.3f с"))) {
            s_.mic_offset = mo;
            changed = true;
        }
        help_marker(tr("Час у відео, де починається файл мікрофона. Від'ємне значення обрізає початок файлу."));
        label(tr("Гучність мікрофона"));
        float mv = static_cast<float>(s_.mic_volume * 100);
        if (slider_float("##mv", &mv, 0, 300, "%.0f%%", field_width(20))) {
            s_.mic_volume = mv / 100.0;
            changed = true;
        }
        ImGui::EndDisabled();
    }
    card_end();
    ImGui::EndDisabled();
    if (changed) mark_dirty();
}

} // namespace gmdr::gui
