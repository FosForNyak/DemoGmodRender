// =============================================================================
//  app_tab_video.cpp — вкладка «Відео»: роздільна здатність, FPS, motion blur,
//  кодек, якість, пресети.
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
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

// ================================ Вкладка "Відео" =================================
void App::draw_tab_video() {
    const float fs_ = ImGui::GetFontSize();
    const float lw = fs_ * 10.5f;
    const float ww = std::max(fs_ * 14.0f, ImGui::GetContentRegionAvail().x - lw - fs_ * 1.5f);
    bool changed = false;

    // ---- Пресети ----
    label(tr("Пресет"), lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    if (begin_combo("##qpreset", tr("вибрати готовий набір..."))) {
        for (int i = 0; i < IM_ARRAYSIZE(kQuickPresets); ++i) {
            if (ImGui::Selectable(tr(kQuickPresets[i].label))) apply_preset(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(kQuickPresets[i].tip));
        }
        ImGui::EndCombo();
    }
    help_marker(tr("Налаштування в один клік: роздільна здатність, FPS, кодек, формат і звук. Далі їх можна підправити вручну. "
                "Для Discord бітрейт розраховується під розмір файлу для вибраного фрагмента."));
    ImGui::Spacing();

    if (section(tr("Кадр"))) {
        // ---- Роздільна здатність ----
        label(tr("Роздільна здатність"), lw);
        int res_idx = -1;
        for (int i = 0; i < IM_ARRAYSIZE(kResolutions); ++i)
            if (kResolutions[i].w == s_.width && kResolutions[i].h == s_.height) res_idx = i;
        const std::string res_preview = res_idx >= 0 ? tr(kResolutions[res_idx].label) : trf("{} × {} (своя)", s_.width, s_.height);
        ImGui::SetNextItemWidth(ww * 0.55f);
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
        label(tr("Частота кадрів (FPS)"), lw);
        ImGui::SetNextItemWidth(ww * 0.55f);
        if (begin_combo("##fps", (s_.fps + tr(" кадрів/с")).c_str())) {
            for (const char* f : kFps)
                if (ImGui::Selectable(f, s_.fps == f)) {
                    s_.fps = f;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 6.0f);
        changed |= ImGui::InputText("##fpscustom", &s_.fps);
        help_marker(tr("Будь-яке значення: 60, 144, 59.94 або дріб 60000/1001. Гра рендерить демо з фіксованим кроком часу, тож FPS може бути будь-яким — навіть вищим за FPS самої гри."));
        const auto fps_r = parse_rational(s_.fps);
        if (!fps_r) ImGui::TextColored(kColErr, "%s", tr("Неправильна частота кадрів"));

        // ---- Motion blur ----
        label(tr("Розмиття руху"), lw);
        changed |= slider_int("##blur", &s_.motion_blur, 1, 64, s_.motion_blur <= 1 ? tr("вимкнено") : tr("%d під-кадрів"), ww * 0.75f);
        help_marker(tr("Motion blur як у справжньої камери: гра рендерить N під-кадрів на кожен кадр відео, програма усереднює їх на всіх ядрах процесора. Чим більше — тим плавніше, але рендер довший у N разів. 8–16 — хороший вибір."));
        if (s_.motion_blur > 1) {
            label(tr("Кут затвора"), lw);
            float sh = static_cast<float>(s_.shutter);
            if (slider_float("##shutter", &sh, 30.0f, 360.0f, "%.0f°", ww * 0.75f)) {
                s_.shutter = sh;
                changed = true;
            }
            help_marker(tr("180° — класичний кіно-вигляд (розмиття впродовж половини кадру). 360° — максимальне розмиття."));
            if (fps_r)
                ImGui::TextColored(kColDim, tr("Гра рендеритиме %.0f кадрів/с часу демо"), fps_r->value() * s_.motion_blur / s_.speed);
        }

        // ---- Швидкість: уповільнення / прискорення ----
        label(tr("Швидкість відео"), lw);
        {
            static const struct { double v; const char* label; } kSpeeds[] = {
                {0.25, tr("×0.25 — учетверо повільніше")}, {0.5, tr("×0.5 — удвічі повільніше")}, {0.75, "×0.75"},
                {1.0, tr("×1 — звичайна")}, {1.5, "×1.5"}, {2.0, tr("×2 — удвічі швидше")}, {4.0, "×4"}, {8.0, tr("×8 — таймлапс")}};
            std::string cur = std::format("×{:g}", s_.speed);
            for (const auto& sp : kSpeeds)
                if (std::abs(sp.v - s_.speed) < 1e-6) cur = sp.label;
            ImGui::SetNextItemWidth(ww * 0.55f);
            if (begin_combo("##speed", cur.c_str())) {
                for (const auto& sp : kSpeeds)
                    if (ImGui::Selectable(sp.label, std::abs(sp.v - s_.speed) < 1e-6)) {
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
                label(tr("Звук при цьому"), lw);
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
    }   // «Кадр»

    ImGui::Spacing();
    if (section(tr("Кодек і формат"))) {
        // ---- Контейнер ----
        const std::string cont = current_container();
        label(tr("Формат файлу"), lw);
        ImGui::SetNextItemWidth(ww);
        std::string cont_label = cont;
        for (const auto& c : kContainers)
            if (cont == c.ext) cont_label = tr(c.label);
        if (begin_combo("##cont", cont_label.c_str())) {
            for (const auto& c : kContainers)
                if (ImGui::Selectable(tr(c.label), cont == c.ext)) set_container(c.ext);
            ImGui::EndCombo();
        }

        // ---- Кодек ----
        label(tr("Відеокодек"), lw);
        ImGui::SetNextItemWidth(ww);
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
            ImGui::SeparatorText(tr("Інше"));
            ImGui::SetNextItemWidth(fs_ * 12);
            ImGui::InputTextWithHint("##customcodec", tr("назва енкодера FFmpeg"), &custom_codec_);
            ImGui::SameLine();
            if (ImGui::Button(tr("Обрати")) && avcodec_find_encoder_by_name(custom_codec_.c_str())) {
                s_.video_codec = custom_codec_;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndCombo();
        }
        {
            std::lock_guard lock(gpu_mutex_);
            const bool any_gpu = std::any_of(gpu_status_.begin(), gpu_status_.end(), [](const auto& p) { return p.second == 1; });
            if (gpu_probe_running_) ImGui::TextColored(kColDim, "%s", tr("Перевіряю, які GPU-кодеки підтримує ваша відеокарта..."));
            else if (!any_gpu) ImGui::TextColored(kColDim, "%s", tr("GPU-кодеки недоступні — використовуються кодеки процесора"));
        }
        if (media::container_supports(cont, s_.video_codec) == 0)
            ImGui::TextColored(kColErr, tr("Формат .%s не підтримує цей кодек — оберіть MKV або інший кодек"), cont.c_str());

        // ---- Якість ----
        const media::QualityInfo qi = media::quality_info_for(s_.video_codec);
        if (!qi.param.empty()) {
            const bool prores = qi.param == "profile";
            label(prores ? tr("Профіль ProRes") : tr("Якість"), lw);
            if (prores) {
                ImGui::SetNextItemWidth(ww * 0.55f);
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
                ImGui::BeginDisabled(use_bitrate);
                int q = s_.quality < 0 ? qi.def : s_.quality;
                const std::string fmt = std::format("{} = %d{}", qi.param, s_.quality < 0 ? tr(" (типово)") : "");
                if (slider_int("##quality", &q, qi.min, qi.max, fmt.c_str(), ww * 0.75f)) {
                    s_.quality = q;
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("типово"))) {
                    s_.quality = -1;
                    changed = true;
                }
                help_marker(qi.lower_is_better ? tr("Менше число — вища якість і більший файл. Для YouTube зазвичай достатньо 16–20 (x264/NVENC).")
                                               : tr("Більше число — вища якість."));
                label(tr("Бітрейт замість якості"), lw);
                if (checkbox("##usebr", &use_bitrate)) {
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
        // ---- Цільовий розмір файлу ----
        if (qi.param != "profile") {
            label(tr("Розмір файлу"), lw);
            bool use_size = s_.target_size_mb > 0;
            if (checkbox("##usesize", &use_size)) {
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
                    ImGui::TextColored(vbr < 800000 ? kColWarn : kColDim, tr("≈ %.2f Мбіт/с на %s"), vbr / 1e6, format_duration(secs).c_str());
                }
            }
            help_marker(tr("Бітрейт розраховується так, щоб увесь фрагмент уклався в заданий розмір (напр. 10 МБ для Discord). "
                        "Замінює «Якість» і «Бітрейт». Якщо бітрейт виходить замалим — зменште роздільну здатність або FPS."));
        }
        // ---- Додаткові версії з тих самих кадрів ----
        {
            label(tr("Ще версії"), lw);
            std::vector<std::string> on;
            for (const auto& id : split(s_.extra_versions, ','))
                if (!trim(id).empty()) on.push_back(trim(id));
            bool first = true;
            for (const auto& v : render::version_presets()) {
                bool checked = std::find(on.begin(), on.end(), v.id) != on.end();
                if (!first) {
                    // Переносимо на новий рядок, якщо галочка не влазить
                    ImGui::SameLine();
                    if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(v.label.c_str()).x + ImGui::GetFrameHeight() * 2) {
                        ImGui::NewLine();
                        label("", lw);
                    }
                }
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
        // ---- Пресет ----
        if (!qi.presets.empty()) {
            label(tr("Швидкість кодування"), lw);
            ImGui::SetNextItemWidth(ww * 0.55f);
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
        label(tr("Бітність кольору"), lw);
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
        label(tr("Субдискретизація"), lw);
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
    }   // «Кодек і формат»

    ImGui::Spacing();
    if (section(tr("Додатково"), false)) {
        label(tr("Формат пікселів"), lw);
        ImGui::SetNextItemWidth(fs_ * 10);
        changed |= ImGui::InputText("##pixfmt", &s_.pix_fmt);
        help_marker(tr("auto — підібрати автоматично. Або вкажіть назву FFmpeg: yuv420p10le, p010le, yuv444p, gbrp ..."));
        label(tr("Параметри кодека"), lw);
        ImGui::SetNextItemWidth(ww);
        changed |= ImGui::InputTextWithHint("##vopts", tr("напр.: tune=film; x264-params=aq-mode=3"), &s_.video_options);
        help_marker(tr("Будь-які параметри енкодера FFmpeg через «;». Мають пріоритет над налаштуваннями вище."));
        label(tr("Масштабування"), lw);
        ImGui::SetNextItemWidth(fs_ * 10);
        const char* scalers[] = {"lanczos", "bicubic", "spline", "bilinear", "area"};
        if (begin_combo("##scaler", s_.scaler.c_str())) {
            for (const char* sc : scalers)
                if (ImGui::Selectable(sc, s_.scaler == sc)) {
                    s_.scaler = sc;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        label(tr("Ключовий кадр кожні"), lw);
        changed |= hot_int("##gop", &s_.gop_seconds, 0.1f, 0, 60, tr("%d с"));
        s_.gop_seconds = std::clamp(s_.gop_seconds, 0, 60);
        label(tr("Точність кольору"), lw);
        changed |= checkbox(tr("максимальна (повільніше)##acc"), &s_.accurate_color);
        help_marker(tr("Точне округлення і фільтр масштабування навіть без зміни розміру. Різниця зі звичайним режимом "
                    "(~48 дБ PSNR) на око не видна, а перетворення кольору повільніше в кілька разів."));
        label(tr("Повний діапазон"), lw);
        changed |= checkbox(tr("0–255 замість 16–235##fr"), &s_.full_range);
        label(tr("Потоків CPU"), lw);
        changed |= hot_int("##threads", &s_.threads, 0.2f, 0, 256, "%d");
        s_.threads = std::clamp(s_.threads, 0, 256);
        ImGui::SameLine();
        ImGui::TextColored(kColDim, "%s", tr("0 = усі ядра"));
        label(tr("Швидкий старт MP4"), lw);
        changed |= checkbox(tr("faststart (для інтернету)##fs"), &s_.faststart);
        label(tr("Захист від збою"), lw);
        changed |= checkbox(tr("MP4/MOV пишеться фрагментами##cs"), &s_.crash_safe);
        help_marker(tr("Під час рендеру файл MP4/MOV пишеться фрагментами: якщо гра чи ПК впадуть, уже записане відео "
                    "відкриється. Наприкінці (з увімкненим faststart) файл без перекодування переупаковується у звичайний MP4. "
                    "MKV і так відкривається після збою."));
    }
    if (changed) mark_dirty();
}

} // namespace gmdr::gui
