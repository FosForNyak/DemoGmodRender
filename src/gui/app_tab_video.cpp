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
    const float lw = fs_ * 11.5f;
    const float ww = std::max(fs_ * 14.0f, ImGui::GetContentRegionAvail().x - lw - fs_);
    bool changed = false;

    // ---- Пресети ----
    label("Пресет", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    if (ImGui::BeginCombo("##qpreset", "вибрати готовий набір...")) {
        for (int i = 0; i < IM_ARRAYSIZE(kQuickPresets); ++i) {
            if (ImGui::Selectable(kQuickPresets[i].label)) apply_preset(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kQuickPresets[i].tip);
        }
        ImGui::EndCombo();
    }
    help_marker("Налаштування в один клік: роздільна здатність, FPS, кодек, формат і звук. Далі їх можна підправити вручну. "
                "Для Discord бітрейт розраховується під розмір файлу для вибраного фрагмента.");

    // ---- Роздільна здатність ----
    label("Роздільна здатність", lw);
    int res_idx = -1;
    for (int i = 0; i < IM_ARRAYSIZE(kResolutions); ++i)
        if (kResolutions[i].w == s_.width && kResolutions[i].h == s_.height) res_idx = i;
    const std::string res_preview = res_idx >= 0 ? kResolutions[res_idx].label : std::format("{} × {} (своя)", s_.width, s_.height);
    ImGui::SetNextItemWidth(ww * 0.55f);
    if (ImGui::BeginCombo("##res", res_preview.c_str())) {
        for (int i = 0; i < IM_ARRAYSIZE(kResolutions); ++i)
            if (ImGui::Selectable(kResolutions[i].label, i == res_idx)) {
                s_.width = kResolutions[i].w;
                s_.height = kResolutions[i].h;
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs_ * 4.5f);
    changed |= ImGui::InputInt("##w", &s_.width, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted("×");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fs_ * 4.5f);
    changed |= ImGui::InputInt("##h", &s_.height, 0);
    s_.width = std::clamp(s_.width, 16, 16384);
    s_.height = std::clamp(s_.height, 16, 16384);

    // ---- FPS ----
    label("Частота кадрів (FPS)", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    if (ImGui::BeginCombo("##fps", (s_.fps + " кадрів/с").c_str())) {
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
    help_marker("Будь-яке значення: 60, 144, 59.94 або дріб 60000/1001. Гра рендерить демо з фіксованим кроком часу, тож FPS може бути будь-яким — навіть вищим за FPS самої гри.");
    const auto fps_r = parse_rational(s_.fps);
    if (!fps_r) ImGui::TextColored(kColErr, "Неправильна частота кадрів");

    // ---- Motion blur ----
    label("Розмиття руху", lw);
    ImGui::SetNextItemWidth(ww * 0.55f);
    changed |= ImGui::SliderInt("##blur", &s_.motion_blur, 1, 64, s_.motion_blur <= 1 ? "вимкнено" : "%d під-кадрів");
    help_marker("Motion blur як у справжньої камери: гра рендерить N під-кадрів на кожен кадр відео, програма усереднює їх на всіх ядрах процесора. Чим більше — тим плавніше, але рендер довший у N разів. 8–16 — хороший вибір.");
    if (s_.motion_blur > 1) {
        label("Кут затвора", lw);
        ImGui::SetNextItemWidth(ww * 0.55f);
        float sh = static_cast<float>(s_.shutter);
        if (ImGui::SliderFloat("##shutter", &sh, 30.0f, 360.0f, "%.0f°")) {
            s_.shutter = sh;
            changed = true;
        }
        help_marker("180° — класичний кіно-вигляд (розмиття впродовж половини кадру). 360° — максимальне розмиття.");
        if (fps_r)
            ImGui::TextColored(kColDim, "Гра рендеритиме %.0f кадрів/с часу демо", fps_r->value() * s_.motion_blur / s_.speed);
    }

    // ---- Швидкість: уповільнення / прискорення ----
    label("Швидкість відео", lw);
    {
        static const struct { double v; const char* label; } kSpeeds[] = {
            {0.25, "×0.25 — учетверо повільніше"}, {0.5, "×0.5 — удвічі повільніше"}, {0.75, "×0.75"},
            {1.0, "×1 — звичайна"}, {1.5, "×1.5"}, {2.0, "×2 — удвічі швидше"}, {4.0, "×4"}, {8.0, "×8 — таймлапс"}};
        std::string cur = std::format("×{:g}", s_.speed);
        for (const auto& sp : kSpeeds)
            if (std::abs(sp.v - s_.speed) < 1e-6) cur = sp.label;
        ImGui::SetNextItemWidth(ww * 0.55f);
        if (ImGui::BeginCombo("##speed", cur.c_str())) {
            for (const auto& sp : kSpeeds)
                if (ImGui::Selectable(sp.label, std::abs(sp.v - s_.speed) < 1e-6)) {
                    s_.speed = sp.v;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 5.0f);
        float sp = static_cast<float>(s_.speed);
        if (ImGui::InputFloat("##speedcustom", &sp, 0, 0, "%.2f")) {
            s_.speed = std::clamp(static_cast<double>(sp), 0.1, 16.0);
            changed = true;
        }
        help_marker("Уповільнення (slow motion): гра рендерить демо з меншим кроком часу, тож кожен кадр чесний, "
                    "а не домальований. ×0.5 — удвічі повільніше, рендер удвічі довший. Прискорення (таймлапс) — "
                    "навпаки. З motion blur прискорення виглядає плавно.");
        if (std::abs(s_.speed - 1.0) > 1e-6) {
            label("Звук при цьому", lw);
            if (ImGui::RadioButton("розтягнути", s_.speed_audio != "mute")) {
                s_.speed_audio = "stretch";
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("без звуку", s_.speed_audio == "mute")) {
                s_.speed_audio = "mute";
                changed = true;
            }
            help_marker("Розтягнути — звук гри, голоси і мікрофон сповільнюються чи прискорюються разом з відео, а "
                        "висота тону лишається тією самою (фільтр atempo). Без звуку — для монтажу під музику.");
        }
    }

    ImGui::SeparatorText("Кодек і формат");
    // ---- Контейнер ----
    const std::string cont = current_container();
    label("Формат файлу", lw);
    ImGui::SetNextItemWidth(ww);
    std::string cont_label = cont;
    for (const auto& c : kContainers)
        if (cont == c.ext) cont_label = c.label;
    if (ImGui::BeginCombo("##cont", cont_label.c_str())) {
        for (const auto& c : kContainers)
            if (ImGui::Selectable(c.label, cont == c.ext)) set_container(c.ext);
        ImGui::EndCombo();
    }

    // ---- Кодек ----
    label("Відеокодек", lw);
    ImGui::SetNextItemWidth(ww);
    std::string codec_label = s_.video_codec;
    for (const auto& c : video_codecs_)
        if (c.name == s_.video_codec) codec_label = c.label;
    if (ImGui::BeginCombo("##codec", codec_label.c_str(), ImGuiComboFlags_HeightLarge)) {
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
            if (c.gpu && st == -1) text += "  (перевіряється...)";
            if (media::container_supports(cont, c.name) == 0) text += "  [не для ." + cont + "]";
            if (ImGui::Selectable((text + "##" + c.name).c_str(), c.name == s_.video_codec)) {
                s_.video_codec = c.name;
                s_.preset.clear();
                s_.quality = -1;
                changed = true;
            }
        }
        ImGui::SeparatorText("Інше");
        ImGui::SetNextItemWidth(fs_ * 12);
        ImGui::InputTextWithHint("##customcodec", "назва енкодера FFmpeg", &custom_codec_);
        ImGui::SameLine();
        if (ImGui::Button("Обрати") && avcodec_find_encoder_by_name(custom_codec_.c_str())) {
            s_.video_codec = custom_codec_;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
    }
    {
        std::lock_guard lock(gpu_mutex_);
        const bool any_gpu = std::any_of(gpu_status_.begin(), gpu_status_.end(), [](const auto& p) { return p.second == 1; });
        if (gpu_probe_running_) ImGui::TextColored(kColDim, "Перевіряю, які GPU-кодеки підтримує ваша відеокарта...");
        else if (!any_gpu) ImGui::TextColored(kColDim, "GPU-кодеки недоступні — використовуються кодеки процесора");
    }
    if (media::container_supports(cont, s_.video_codec) == 0)
        ImGui::TextColored(kColErr, "Формат .%s не підтримує цей кодек — оберіть MKV або інший кодек", cont.c_str());

    // ---- Якість ----
    const media::QualityInfo qi = media::quality_info_for(s_.video_codec);
    if (!qi.param.empty()) {
        const bool prores = qi.param == "profile";
        label(prores ? "Профіль ProRes" : "Якість", lw);
        if (prores) {
            ImGui::SetNextItemWidth(ww * 0.55f);
            int p = s_.quality < 0 ? qi.def : s_.quality;
            const char* names[] = {"0 — Proxy", "1 — LT", "2 — Standard", "3 — HQ", "4 — 4444", "5 — 4444 XQ"};
            if (ImGui::Combo("##prores", &p, names, 6)) {
                s_.quality = p;
                changed = true;
            }
        } else {
            bool use_bitrate = !s_.video_bitrate.empty();
            ImGui::BeginDisabled(use_bitrate);
            ImGui::SetNextItemWidth(ww * 0.55f);
            int q = s_.quality < 0 ? qi.def : s_.quality;
            const std::string fmt = std::format("{} = %d{}", qi.param, s_.quality < 0 ? " (типово)" : "");
            if (ImGui::SliderInt("##quality", &q, qi.min, qi.max, fmt.c_str())) {
                s_.quality = q;
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("типово")) {
                s_.quality = -1;
                changed = true;
            }
            help_marker(qi.lower_is_better ? "Менше число — вища якість і більший файл. Для YouTube зазвичай достатньо 16–20 (x264/NVENC)."
                                           : "Більше число — вища якість.");
            label("Бітрейт замість якості", lw);
            if (ImGui::Checkbox("##usebr", &use_bitrate)) {
                s_.video_bitrate = use_bitrate ? "40M" : "";
                changed = true;
            }
            if (use_bitrate) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(fs_ * 6);
                changed |= ImGui::InputText("біт/с##br", &s_.video_bitrate);
                help_marker("Напр. 20M = 20 Мбіт/с, 80M для 4K.");
            }
        }
    }
    // ---- Цільовий розмір файлу ----
    if (qi.param != "profile") {
        label("Розмір файлу", lw);
        bool use_size = s_.target_size_mb > 0;
        if (ImGui::Checkbox("##usesize", &use_size)) {
            s_.target_size_mb = use_size ? 10 : 0;
            changed = true;
        }
        if (use_size) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fs_ * 6);
            float mb = static_cast<float>(s_.target_size_mb);
            if (ImGui::InputFloat("МБ##tsize", &mb, 0, 0, "%.0f")) {
                s_.target_size_mb = std::clamp(static_cast<double>(mb), 1.0, 100000.0);
                changed = true;
            }
            const double secs = fragment_seconds();
            if (secs > 0) {
                const int64_t abr = s_.audio ? parse_bitrate(s_.audio_bitrate).value_or(320000) : 0;
                const int64_t vbr = render::bitrate_for_target_size(s_.target_size_mb, secs, abr);
                ImGui::SameLine();
                ImGui::TextColored(vbr < 800000 ? kColWarn : kColDim, "≈ %.2f Мбіт/с на %s", vbr / 1e6, format_duration(secs).c_str());
            }
        }
        help_marker("Бітрейт розраховується так, щоб увесь фрагмент уклався в заданий розмір (напр. 10 МБ для Discord). "
                    "Замінює «Якість» і «Бітрейт». Якщо бітрейт виходить замалим — зменште роздільну здатність або FPS.");
    }
    // ---- Додаткові версії з тих самих кадрів ----
    {
        label("Ще версії", lw);
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
            if (ImGui::Checkbox((v.label + "##ver_" + v.id).c_str(), &checked)) {
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
        help_marker("Гра рендерить демо один раз, а з тих самих кадрів одночасно кодуються й ці версії — файли з "
                    "суфіксом (_discord, _480p, _vertical, _master) поруч з основним. Кожна версія додає роботи "
                    "процесору, тож рендер іде трохи повільніше. Обкладинка, GIF і WebP робляться вже з готового "
                    "відео, після рендеру (відео.jpg, відео.gif, відео.webp).");
    }
    // ---- Пресет ----
    if (!qi.presets.empty()) {
        label("Швидкість кодування", lw);
        ImGui::SetNextItemWidth(ww * 0.55f);
        const std::string cur = s_.preset.empty() ? qi.default_preset + " (типово)" : s_.preset;
        if (ImGui::BeginCombo("##preset", cur.c_str())) {
            for (const auto& p : qi.presets) {
                const std::string value = p.substr(0, p.find(' '));
                if (ImGui::Selectable(p.c_str(), s_.preset == value)) {
                    s_.preset = value;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        help_marker("Повільніший пресет — менший файл за тієї ж якості. Для GPU-кодеків p1 — найшвидший, p7 — найякісніший.");
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
    label("Бітність кольору", lw);
    for (int d : {8, 10, 12}) {
        ImGui::BeginDisabled(!supports(d, 0));
        if (ImGui::RadioButton(std::format("{} біт##bd{}", d, d).c_str(), s_.bit_depth == d)) {
            s_.bit_depth = d;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    help_marker("10 біт прибирає смуги на градієнтах (небо, туман), особливо разом з motion blur. Підтримують: x265, SVT-AV1, VP9, ProRes, HEVC/AV1 NVENC/AMF/QSV. Старі програвачі можуть не відтворити 10-біт H.264.");
    label("Субдискретизація", lw);
    const std::pair<int, const char*> chromas[] = {{420, "4:2:0"}, {422, "4:2:2"}, {444, "4:4:4"}};
    for (const auto& [c, n] : chromas) {
        ImGui::BeginDisabled(!supports(8, c) && !supports(10, c));
        if (ImGui::RadioButton(n, s_.chroma == c)) {
            s_.chroma = c;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    help_marker("4:2:0 — стандарт для YouTube і плеєрів. 4:4:4 — повний колір (дрібний кольоровий текст), для монтажу.");

    if (ImGui::TreeNode("Додатково")) {
        label("Формат пікселів", lw);
        ImGui::SetNextItemWidth(fs_ * 10);
        changed |= ImGui::InputText("##pixfmt", &s_.pix_fmt);
        help_marker("auto — підібрати автоматично. Або вкажіть назву FFmpeg: yuv420p10le, p010le, yuv444p, gbrp ...");
        label("Параметри кодека", lw);
        ImGui::SetNextItemWidth(ww);
        changed |= ImGui::InputTextWithHint("##vopts", "напр.: tune=film; x264-params=aq-mode=3", &s_.video_options);
        help_marker("Будь-які параметри енкодера FFmpeg через «;». Мають пріоритет над налаштуваннями вище.");
        label("Масштабування", lw);
        ImGui::SetNextItemWidth(fs_ * 10);
        const char* scalers[] = {"lanczos", "bicubic", "spline", "bilinear", "area"};
        if (ImGui::BeginCombo("##scaler", s_.scaler.c_str())) {
            for (const char* sc : scalers)
                if (ImGui::Selectable(sc, s_.scaler == sc)) {
                    s_.scaler = sc;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        label("Ключовий кадр кожні", lw);
        ImGui::SetNextItemWidth(fs_ * 6);
        changed |= ImGui::InputInt("с##gop", &s_.gop_seconds);
        s_.gop_seconds = std::clamp(s_.gop_seconds, 0, 60);
        label("Точність кольору", lw);
        changed |= ImGui::Checkbox("максимальна (повільніше)##acc", &s_.accurate_color);
        help_marker("Точне округлення і фільтр масштабування навіть без зміни розміру. Різниця зі звичайним режимом "
                    "(~48 дБ PSNR) на око не видна, а перетворення кольору повільніше в кілька разів.");
        label("Повний діапазон", lw);
        changed |= ImGui::Checkbox("0–255 замість 16–235##fr", &s_.full_range);
        label("Потоків CPU", lw);
        ImGui::SetNextItemWidth(fs_ * 6);
        changed |= ImGui::InputInt("##threads", &s_.threads);
        s_.threads = std::clamp(s_.threads, 0, 256);
        ImGui::SameLine();
        ImGui::TextColored(kColDim, "0 = усі ядра");
        label("Швидкий старт MP4", lw);
        changed |= ImGui::Checkbox("faststart (для інтернету)##fs", &s_.faststart);
        label("Захист від збою", lw);
        changed |= ImGui::Checkbox("MP4/MOV пишеться фрагментами##cs", &s_.crash_safe);
        help_marker("Під час рендеру файл MP4/MOV пишеться фрагментами: якщо гра чи ПК впадуть, уже записане відео "
                    "відкриється. Наприкінці (з увімкненим faststart) файл без перекодування переупаковується у звичайний MP4. "
                    "MKV і так відкривається після збою.");
        ImGui::TreePop();
    }
    if (changed) mark_dirty();
}

} // namespace gmdr::gui
