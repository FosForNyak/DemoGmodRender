// =============================================================================
//  app_page_video.cpp — сторінка «Відео»: файл результату, роздільна здатність,
//  FPS, motion blur, швидкість, кодек, якість, пресети і додаткові версії.
//  У стандартному режимі — лише головне; тонкі параметри кодека — у розширеному.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/media/ffmpeg_util.hpp"
#include "core/media/muxer.hpp"
#include "core/media/video_encoder.hpp"
#include "core/render/versions.hpp"
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

namespace fs = std::filesystem;
using namespace ui;

// ============================== Файл результату ===================================
// Ім'я файлу — посиланням (клік — «Зберегти як»), тека — тьмяно; олівець — ввести шлях;
// під ними — підсумок: кодек · кадр · FPS · звук · тривалість
void App::draw_output_card() {
    const float fs_ = ImGui::GetFontSize();
    auto save_as = [&] {
        const std::string ext = current_container();
        auto f = save_file_dialog(tr("Зберегти відео як"), {{tr("Відео (*.") + ext + ")", "*." + ext}, {tr("Усі файли"), "*.*"}},
                                  s_.output_path, ext);
        if (!f.empty()) {
            s_.output_path = f;
            mark_dirty();
        }
    };
    ImGui::BeginDisabled(job_running());
    const float fh = ImGui::GetFrameHeight();
    const float avail = std::max(fs_ * 6, ImGui::GetContentRegionAvail().x - (fh + 4) * 2);
    if (out_editing_) {
        ImGui::SetNextItemWidth(avail);
        if (out_focus_) {
            ImGui::SetKeyboardFocusHere();
            out_focus_ = false;
        }
        if (ImGui::InputText("##out", &s_.output_path, ImGuiInputTextFlags_EnterReturnsTrue)) out_editing_ = false;
        if (ImGui::IsItemEdited()) mark_dirty();
        if (ImGui::IsItemDeactivated()) out_editing_ = false;
    } else {
        const fs::path out = path_from_utf8(s_.output_path);
        const std::string name = s_.output_path.empty() ? std::string(tr("спершу відкрийте демо")) : path_to_utf8(out.filename());
        const std::string dir = s_.output_path.empty() ? std::string() : path_to_utf8(out.parent_path());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(s_.output_path.empty());
        if (ImGui::InvisibleButton("##outlink", ImVec2(avail, fh))) save_as();
        ImGui::EndDisabled();
        const bool hov = ImGui::IsItemHovered();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", (s_.output_path + tr("\n\nКлацніть, щоб вибрати інший файл; олівець — ввести шлях вручну.")).c_str());
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float ty = p.y + std::round((fh - fs_) * 0.5f);
        dl->PushClipRect(p, ImVec2(p.x + avail, p.y + fh), true);
        const ImU32 name_col = s_.output_path.empty() ? ImGui::GetColorU32(kTextDim) : ImGui::GetColorU32(kAccentText);
        ImGui::PushFont(bold_font(), 0.0f);
        dl->AddText(ImVec2(p.x, ty), name_col, name.c_str());
        const float nw = ImGui::CalcTextSize(name.c_str()).x;
        ImGui::PopFont();
        if (hov && !s_.output_path.empty()) dl->AddLine(ImVec2(p.x, ty + fs_ + 1), ImVec2(p.x + nw, ty + fs_ + 1), name_col);
        if (!dir.empty()) dl->AddText(ImVec2(p.x + nw + fs_ * 0.6f, ty), ImGui::GetColorU32(kTextDim), dir.c_str());
        dl->PopClipRect();
    }
    ImGui::SameLine(0, 4);
    if (icon_button("##editout", Icon::Pencil, tr("Ввести шлях вручну"), out_editing_)) {
        out_editing_ = !out_editing_;
        out_focus_ = out_editing_;
    }
    ImGui::SameLine(0, 4);
    if (icon_button("##browseout", Icon::Folder, tr("Зберегти як..."))) save_as();
    ImGui::EndDisabled();
    // Підсумок: кодек · кадр · FPS · звук · тривалість
    std::string codec = s_.video_codec;
    for (const auto& c : video_codecs_)
        if (c.name == s_.video_codec) codec = c.label.substr(0, c.label.find(" — "));
    std::string audio = tr("без звуку");
    if (s_.audio) {
        audio = s_.audio_codec;
        for (const auto& c : audio_codecs_)
            if (c.name == s_.audio_codec) audio = c.label;
        const bool lossless = s_.audio_codec.rfind("pcm_", 0) == 0 || s_.audio_codec == "flac" || s_.audio_codec == "alac";
        if (!lossless) audio += " " + s_.audio_bitrate;
    }
    std::string summary = std::format("{} · {}×{} · {} {} · {}", codec, s_.width, s_.height, s_.fps, tr("кадр/с"), audio);
    if (analysis_) summary += " · " + format_duration(fragment_seconds());
    if (s_.target_size_mb > 0) summary += std::format(" · ≤ {:.0f} {}", s_.target_size_mb, tr("МБ"));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(summary.c_str());
    ImGui::PopStyleColor();
}

// ================================ Сторінка «Відео» =================================
void App::draw_page_video() {
    const float fs_ = ImGui::GetFontSize();
    const bool adv = s_.ui_advanced;
    bool changed = false;
    page_header(tr("Відео"), tr("Роздільна здатність, частота кадрів, кодек і куди зберегти результат."));

    if (card_begin(tr("Файл результату"), nullptr, Icon::File, false)) draw_output_card();
    card_end();

    if (card_begin(tr("Кадр"), tr("Готовий набір, розмір, частота кадрів, розмиття руху і швидкість"), Icon::Film)) {
        // ---- Пресети ----
        label(tr("Пресет"));
        ImGui::SetNextItemWidth(field_width(22));
        if (begin_combo("##qpreset", tr("вибрати готовий набір..."))) {
            for (int i = 0; i < IM_ARRAYSIZE(kQuickPresets); ++i) {
                if (ImGui::Selectable(tr(kQuickPresets[i].label))) apply_preset(i);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(kQuickPresets[i].tip));
            }
            ImGui::EndCombo();
        }
        help_marker(tr("Налаштування в один клік: роздільна здатність, FPS, кодек, формат і звук. Далі їх можна підправити вручну. "
                    "Для Discord бітрейт розраховується під розмір файлу для вибраного фрагмента."));

        // ---- Роздільна здатність ----
        label(tr("Роздільна здатність"));
        int res_idx = -1;
        for (int i = 0; i < IM_ARRAYSIZE(kResolutions); ++i)
            if (kResolutions[i].w == s_.width && kResolutions[i].h == s_.height) res_idx = i;
        const std::string res_preview = res_idx >= 0 ? tr(kResolutions[res_idx].label) : trf("{} × {} (своя)", s_.width, s_.height);
        ImGui::SetNextItemWidth(field_width(16));
        if (begin_combo("##res", res_preview.c_str())) {
            for (int i = 0; i < IM_ARRAYSIZE(kResolutions); ++i)
                if (ImGui::Selectable(tr(kResolutions[i].label), i == res_idx)) {
                    s_.width = kResolutions[i].w;
                    s_.height = kResolutions[i].h;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        changed |= hot_int("##w", &s_.width, 4.0f, 16, 16384, "%d");
        ImGui::SameLine(0, 2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kColDim, "×");
        ImGui::SameLine(0, 2);
        changed |= hot_int("##h", &s_.height, 4.0f, 16, 16384, "%d");
        s_.width = std::clamp(s_.width, 16, 16384);
        s_.height = std::clamp(s_.height, 16, 16384);

        // ---- FPS ----
        label(tr("Частота кадрів (FPS)"));
        ImGui::SetNextItemWidth(field_width(16));
        if (begin_combo("##fps", (s_.fps + tr(" кадрів/с")).c_str())) {
            for (const char* f : kFps)
                if (ImGui::Selectable(f, s_.fps == f)) {
                    s_.fps = f;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 5.5f);
        changed |= ImGui::InputText("##fpscustom", &s_.fps);
        help_marker(tr("Будь-яке значення: 60, 144, 59.94 або дріб 60000/1001. Гра рендерить демо з фіксованим кроком часу, тож FPS може бути будь-яким — навіть вищим за FPS самої гри."));
        const auto fps_r = parse_rational(s_.fps);
        if (!fps_r) ImGui::TextColored(kColErr, "%s", tr("Неправильна частота кадрів"));

        // ---- Motion blur ----
        label(tr("Розмиття руху"));
        changed |= slider_int("##blur", &s_.motion_blur, 1, 64, s_.motion_blur <= 1 ? tr("вимкнено") : tr("%d під-кадрів"), field_width(20));
        help_marker(tr("Motion blur як у справжньої камери: гра рендерить N під-кадрів на кожен кадр відео, програма усереднює їх на всіх ядрах процесора. Чим більше — тим плавніше, але рендер довший у N разів. 8–16 — хороший вибір."));
        if (s_.motion_blur > 1 && adv) {
            label(tr("Кут затвора"));
            float sh = static_cast<float>(s_.shutter);
            if (slider_float("##shutter", &sh, 30.0f, 360.0f, "%.0f°", field_width(20))) {
                s_.shutter = sh;
                changed = true;
            }
            help_marker(tr("180° — класичний кіно-вигляд (розмиття впродовж половини кадру). 360° — максимальне розмиття."));
        }
        if (s_.motion_blur > 1 && fps_r) {
            label("");
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kColDim, tr("Гра рендеритиме %.0f кадрів/с часу демо"), fps_r->value() * s_.motion_blur / s_.speed);
        }

        // ---- Швидкість: уповільнення / прискорення ----
        label(tr("Швидкість відео"));
        {
            static const struct { double v; const char* label; } kSpeeds[] = {
                {0.25, N_("×0.25 — учетверо повільніше")}, {0.5, N_("×0.5 — удвічі повільніше")}, {0.75, "×0.75"},
                {1.0, N_("×1 — звичайна")}, {1.5, "×1.5"}, {2.0, N_("×2 — удвічі швидше")}, {4.0, "×4"}, {8.0, N_("×8 — таймлапс")}};
            std::string cur = std::format("×{:g}", s_.speed);
            for (const auto& sp : kSpeeds)
                if (std::abs(sp.v - s_.speed) < 1e-6) cur = tr(sp.label);
            ImGui::SetNextItemWidth(field_width(16));
            if (begin_combo("##speed", cur.c_str())) {
                for (const auto& sp : kSpeeds)
                    if (ImGui::Selectable(tr(sp.label), std::abs(sp.v - s_.speed) < 1e-6)) {
                        s_.speed = sp.v;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            float sp = static_cast<float>(s_.speed);
            if (hot_float("##speedcustom", &sp, 0.01f, 0.1f, 16.0f, "×%.2f")) {
                s_.speed = std::clamp(static_cast<double>(sp), 0.1, 16.0);
                changed = true;
            }
            help_marker(tr("Уповільнення (slow motion): гра рендерить демо з меншим кроком часу, тож кожен кадр чесний, "
                        "а не домальований. ×0.5 — удвічі повільніше, рендер удвічі довший. Прискорення (таймлапс) — "
                        "навпаки. З motion blur прискорення виглядає плавно."));
            if (std::abs(s_.speed - 1.0) > 1e-6) {
                label(tr("Звук при цьому"));
                if (radio(tr("розтягнути"), s_.speed_audio != "mute")) {
                    s_.speed_audio = "stretch";
                    changed = true;
                }
                ImGui::SameLine();
                if (radio(tr("без звуку"), s_.speed_audio == "mute")) {
                    s_.speed_audio = "mute";
                    changed = true;
                }
                help_marker(tr("Розтягнути — звук гри, голоси і мікрофон сповільнюються чи прискорюються разом з відео, а "
                            "висота тону лишається тією самою (фільтр atempo). Без звуку — для монтажу під музику."));
            }
        }
    }
    card_end();

    if (card_begin(tr("Кодек і формат"), tr("Контейнер, кодек, якість і розмір файлу"), Icon::Sparkle)) {
        // ---- Контейнер ----
        const std::string cont = current_container();
        label(tr("Формат файлу"));
        ImGui::SetNextItemWidth(field_width(24));
        std::string cont_label = cont;
        for (const auto& c : kContainers)
            if (cont == c.ext) cont_label = tr(c.label);
        if (begin_combo("##cont", cont_label.c_str())) {
            for (const auto& c : kContainers)
                if (ImGui::Selectable(tr(c.label), cont == c.ext)) set_container(c.ext);
            ImGui::EndCombo();
        }

        // ---- Кодек ----
        label(tr("Відеокодек"));
        ImGui::SetNextItemWidth(field_width(24));
        std::string codec_label = s_.video_codec;
        for (const auto& c : video_codecs_)
            if (c.name == s_.video_codec) codec_label = c.label;
        if (begin_combo("##codec", codec_label.c_str(), ImGuiComboFlags_HeightLarge)) {
            std::string group;
            std::lock_guard lock(gpu_mutex_);
            for (const auto& c : video_codecs_) {
                if (is_image_container(cont) != (c.name == "png") && is_image_container(cont)) continue;
                int st = 1;
                if (c.gpu) {
                    auto it = gpu_status_.find(c.name);
                    st = it == gpu_status_.end() ? -1 : it->second;
                    if (st == 0) continue;   // відеокарта не підтримує — не показуємо
                }
                if (c.group != group) {
                    group = c.group;
                    ImGui::SeparatorText(group.c_str());
                }
                std::string text = c.label;
                if (c.gpu && st == -1) text += tr("  (перевіряється...)");
                if (media::container_supports(cont, c.name) == 0) text += tr("  [не для .") + cont + "]";
                if (ImGui::Selectable((text + "##" + c.name).c_str(), c.name == s_.video_codec)) {
                    s_.video_codec = c.name;
                    s_.preset.clear();
                    s_.quality = -1;
                    changed = true;
                }
            }
            if (adv) {
                ImGui::SeparatorText(tr("Інше"));
                ImGui::SetNextItemWidth(fs_ * 12);
                ImGui::InputTextWithHint("##customcodec", tr("назва енкодера FFmpeg"), &custom_codec_);
                ImGui::SameLine();
                if (ImGui::Button(tr("Обрати")) && avcodec_find_encoder_by_name(custom_codec_.c_str())) {
                    s_.video_codec = custom_codec_;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndCombo();
        }
        {
            std::lock_guard lock(gpu_mutex_);
            const bool any_gpu = std::any_of(gpu_status_.begin(), gpu_status_.end(), [](const auto& p) { return p.second == 1; });
            if (gpu_probe_running_ || !any_gpu) {
                label("");
                ImGui::AlignTextToFramePadding();
                if (gpu_probe_running_) ImGui::TextColored(kColDim, "%s", tr("Перевіряю, які GPU-кодеки підтримує ваша відеокарта..."));
                else ImGui::TextColored(kColDim, "%s", tr("GPU-кодеки недоступні — використовуються кодеки процесора"));
            }
        }
        if (media::container_supports(cont, s_.video_codec) == 0)
            ImGui::TextColored(kColErr, tr("Формат .%s не підтримує цей кодек — оберіть MKV або інший кодек"), cont.c_str());

        // ---- Якість ----
        const media::QualityInfo qi = media::quality_info_for(s_.video_codec);
        if (!qi.param.empty()) {
            const bool prores = qi.param == "profile";
            label(prores ? tr("Профіль ProRes") : tr("Якість"));
            if (prores) {
                ImGui::SetNextItemWidth(field_width(16));
                int p = s_.quality < 0 ? qi.def : s_.quality;
                const char* names[] = {"0 — Proxy", "1 — LT", "2 — Standard", "3 — HQ", "4 — 4444", "5 — 4444 XQ"};
                if (begin_combo("##prores", names[std::clamp(p, 0, 5)])) {
                    for (int i = 0; i < 6; ++i)
                        if (ImGui::Selectable(names[i], p == i)) {
                            s_.quality = i;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
            } else {
                bool use_bitrate = !s_.video_bitrate.empty();
                ImGui::BeginDisabled(use_bitrate || s_.target_size_mb > 0);
                int q = s_.quality < 0 ? qi.def : s_.quality;
                const std::string fmt = std::format("{} = %d{}", qi.param, s_.quality < 0 ? tr(" (типово)") : "");
                if (slider_int("##quality", &q, qi.min, qi.max, fmt.c_str(), field_width(20))) {
                    s_.quality = q;
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (action_button(tr("типово"), Kind::Ghost)) {
                    s_.quality = -1;
                    changed = true;
                }
                help_marker(qi.lower_is_better ? tr("Менше число — вища якість і більший файл. Для YouTube зазвичай достатньо 16–20 (x264/NVENC).")
                                               : tr("Більше число — вища якість."));
                if (adv) {
                    label(tr("Бітрейт замість якості"));
                    if (toggle("##usebr", &use_bitrate)) {
                        s_.video_bitrate = use_bitrate ? "40M" : "";
                        changed = true;
                    }
                    if (use_bitrate) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(fs_ * 6);
                        changed |= ImGui::InputText(tr("біт/с##br"), &s_.video_bitrate);
                        help_marker(tr("Напр. 20M = 20 Мбіт/с, 80M для 4K."));
                    }
                }
            }
        }
        // ---- Цільовий розмір файлу ----
        if (qi.param != "profile") {
            label(tr("Розмір файлу"));
            bool use_size = s_.target_size_mb > 0;
            if (toggle("##usesize", &use_size)) {
                s_.target_size_mb = use_size ? 10 : 0;
                changed = true;
            }
            if (use_size) {
                ImGui::SameLine();
                float mb = static_cast<float>(s_.target_size_mb);
                if (hot_float("##tsize", &mb, 1.0f, 1.0f, 100000.0f, tr("%.0f МБ"))) {
                    s_.target_size_mb = std::clamp(static_cast<double>(mb), 1.0, 100000.0);
                    changed = true;
                }
                const double secs = fragment_seconds();
                if (secs > 0) {
                    const int64_t abr = s_.audio ? parse_bitrate(s_.audio_bitrate).value_or(320000) : 0;
                    const int64_t vbr = render::bitrate_for_target_size(s_.target_size_mb, secs, abr);
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(vbr < 800000 ? kColWarn : kColDim, tr("≈ %.2f Мбіт/с на %s"), vbr / 1e6, format_duration(secs).c_str());
                }
            }
            help_marker(tr("Бітрейт розраховується так, щоб увесь фрагмент уклався в заданий розмір (напр. 10 МБ для Discord). "
                        "Замінює «Якість» і «Бітрейт». Якщо бітрейт виходить замалим — зменште роздільну здатність або FPS."));
        }
        if (adv) {
            // ---- Пресет енкодера ----
            if (!qi.presets.empty()) {
                label(tr("Швидкість кодування"));
                ImGui::SetNextItemWidth(field_width(16));
                const std::string cur = s_.preset.empty() ? qi.default_preset + tr(" (типово)") : s_.preset;
                if (begin_combo("##preset", cur.c_str())) {
                    for (const auto& p : qi.presets) {
                        const std::string value = p.substr(0, p.find(' '));
                        if (ImGui::Selectable(p.c_str(), s_.preset == value)) {
                            s_.preset = value;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                help_marker(tr("Повільніший пресет — менший файл за тієї ж якості. Для GPU-кодеків p1 — найшвидший, p7 — найякісніший."));
            }
            // ---- Бітність і субдискретизація ----
            const AVCodec* codec = avcodec_find_encoder_by_name(s_.video_codec.c_str());
            auto fmts = media::codec_pix_fmts(codec);
            auto supports = [&](int depth, int chroma) {
                if (fmts.empty()) return true;
                for (auto f : fmts) {
                    if (media::pix_fmt_is_hw(f)) continue;
                    if (media::pix_fmt_bit_depth(f) >= depth && (depth == 8 ? media::pix_fmt_bit_depth(f) == 8 : true) &&
                        (chroma == 0 || media::pix_fmt_chroma(f) == chroma))
                        return true;
                }
                return std::all_of(fmts.begin(), fmts.end(), [](AVPixelFormat f) { return media::pix_fmt_is_hw(f); }) && depth <= 10;
            };
            label(tr("Бітність кольору"));
            for (int d : {8, 10, 12}) {
                ImGui::BeginDisabled(!supports(d, 0));
                if (radio(trf("{} біт##bd{}", d, d).c_str(), s_.bit_depth == d)) {
                    s_.bit_depth = d;
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
            }
            help_marker(tr("10 біт прибирає смуги на градієнтах (небо, туман), особливо разом з motion blur. Підтримують: x265, SVT-AV1, VP9, ProRes, HEVC/AV1 NVENC/AMF/QSV. Старі програвачі можуть не відтворити 10-біт H.264."));
            label(tr("Субдискретизація"));
            const std::pair<int, const char*> chromas[] = {{420, "4:2:0"}, {422, "4:2:2"}, {444, "4:4:4"}};
            for (const auto& [c, n] : chromas) {
                ImGui::BeginDisabled(!supports(8, c) && !supports(10, c));
                if (radio(n, s_.chroma == c)) {
                    s_.chroma = c;
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
            }
            help_marker(tr("4:2:0 — стандарт для YouTube і плеєрів. 4:4:4 — повний колір (дрібний кольоровий текст), для монтажу."));
        }
    }
    card_end();

    // ---- Додаткові версії з тих самих кадрів ----
    if (card_begin(tr("Ще версії"), tr("З тих самих кадрів — одночасно з основним файлом"), Icon::Plus)) {
        std::vector<std::string> on;
        for (const auto& id : split(s_.extra_versions, ','))
            if (!trim(id).empty()) on.push_back(trim(id));
        bool first = true;
        for (const auto& v : render::version_presets()) {
            bool checked = std::find(on.begin(), on.end(), v.id) != on.end();
            if (!first) same_line_if_fits(v.label.c_str());
            first = false;
            if (checkbox((v.label + "##ver_" + v.id).c_str(), &checked)) {
                if (checked) on.push_back(v.id);
                else on.erase(std::remove(on.begin(), on.end(), v.id), on.end());
                std::string joined;
                for (const auto& p : render::version_presets())   // порядок як у списку
                    if (std::find(on.begin(), on.end(), p.id) != on.end()) joined += (joined.empty() ? "" : ",") + p.id;
                s_.extra_versions = joined;
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", v.hint.c_str());
        }
        help_marker(tr("Гра рендерить демо один раз, а з тих самих кадрів одночасно кодуються й ці версії — файли з "
                    "суфіксом (_discord, _480p, _vertical, _master) поруч з основним. Кожна версія додає роботи "
                    "процесору, тож рендер іде трохи повільніше. Обкладинка, GIF і WebP робляться вже з готового "
                    "відео, після рендеру (відео.jpg, відео.gif, відео.webp)."));
    }
    card_end();

    if (adv) {
        if (card_begin(tr("Додатково"), tr("Тонкі параметри кодування"), Icon::Gear, true, false)) {
            label(tr("Формат пікселів"));
            ImGui::SetNextItemWidth(field_width(10));
            changed |= ImGui::InputText("##pixfmt", &s_.pix_fmt);
            help_marker(tr("auto — підібрати автоматично. Або вкажіть назву FFmpeg: yuv420p10le, p010le, yuv444p, gbrp ..."));
            label(tr("Параметри кодека"));
            ImGui::SetNextItemWidth(field_width(30));
            changed |= ImGui::InputTextWithHint("##vopts", tr("напр.: tune=film; x264-params=aq-mode=3"), &s_.video_options);
            help_marker(tr("Будь-які параметри енкодера FFmpeg через «;». Мають пріоритет над налаштуваннями вище."));
            label(tr("Масштабування"));
            ImGui::SetNextItemWidth(field_width(10));
            const char* scalers[] = {"lanczos", "bicubic", "spline", "bilinear", "area"};
            if (begin_combo("##scaler", s_.scaler.c_str())) {
                for (const char* sc : scalers)
                    if (ImGui::Selectable(sc, s_.scaler == sc)) {
                        s_.scaler = sc;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            label(tr("Ключовий кадр кожні"));
            changed |= hot_int("##gop", &s_.gop_seconds, 0.1f, 0, 60, tr("%d с"));
            s_.gop_seconds = std::clamp(s_.gop_seconds, 0, 60);
            label(tr("Потоків CPU"));
            changed |= hot_int("##threads", &s_.threads, 0.2f, 0, 256, "%d");
            s_.threads = std::clamp(s_.threads, 0, 256);
            ImGui::SameLine();
            ImGui::TextColored(kColDim, "%s", tr("0 = усі ядра"));
            changed |= toggle(tr("Максимальна точність кольору (повільніше)"), &s_.accurate_color);
            help_marker(tr("Точне округлення і фільтр масштабування навіть без зміни розміру. Різниця зі звичайним режимом "
                        "(~48 дБ PSNR) на око не видна, а перетворення кольору повільніше в кілька разів."));
            changed |= toggle(tr("Повний діапазон (0–255 замість 16–235)"), &s_.full_range);
            changed |= toggle(tr("Швидкий старт MP4 (faststart, для інтернету)"), &s_.faststart);
            changed |= toggle(tr("Захист від збою: MP4/MOV пишеться фрагментами"), &s_.crash_safe);
            help_marker(tr("Під час рендеру файл MP4/MOV пишеться фрагментами: якщо гра чи ПК впадуть, уже записане відео "
                        "відкриється. Наприкінці (з увімкненим faststart) файл без перекодування переупаковується у звичайний MP4. "
                        "MKV і так відкривається після збою."));
        }
        card_end();
    }
    if (changed) mark_dirty();
}

} // namespace gmdr::gui
