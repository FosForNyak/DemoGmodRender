// =============================================================================
//  app_page_fragment.cpp — сторінка «Фрагмент і позначки»: увесь запис чи частина
//  (повзунки, точні тіки й час), тривалість і позначки (розділи у відео).
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"

#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <cmath>

namespace gmdr::gui {

using namespace ui;

void App::draw_page_fragment() {
    const float fs_ = ImGui::GetFontSize();
    bool changed = false;
    page_header(tr("Фрагмент і позначки"), tr("Яку частину демо рендерити і де у відео будуть розділи."));
    if (!analysis_) {
        ImGui::TextColored(kColDim, "%s", tr("Спершу відкрийте демо."));
        return;
    }
    const double ti = analysis_->tick_interval;
    const int32_t last = analysis_->last_tick;
    if (card_begin(tr("Фрагмент"), tr("Мишею на таймлайні, клавішами I і O або точно тут"), Icon::Scissors, false)) {
        if (toggle(tr("Увесь запис"), &whole_demo_)) {
            if (whole_demo_) {
                s_.start_tick = 0;
                s_.end_tick = -1;
            }
            changed = true;
        }
        ImGui::SameLine();
        if (action_button(tr("Переглянути в грі"), Kind::Secondary, 0, Icon::Play)) start_watch(whole_demo_ ? 0 : std::max(0, s_.start_tick));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Запустити гру і програти демо з початку фрагмента (у реальному часі, зі звуком).\n"
                                    "У грі: F9 — початок фрагмента, F11 — кінець, F6 — позначка. Вони одразу з'являться тут.\n"
                                    "Коли надивитеся — просто закрийте гру."));
        ImGui::BeginDisabled(whole_demo_);
        float start_s = static_cast<float>(std::max(0, s_.start_tick) * ti);
        float end_s = static_cast<float>((s_.end_tick > 0 ? s_.end_tick : last) * ti);
        const float max_s = static_cast<float>(last * ti);
        label(tr("Початок"));
        const float sw = std::max(fs_ * 10, field_width() - fs_ * 5.0f);
        if (slider_float("##start", &start_s, 0.0f, max_s, format_duration(start_s).c_str(), sw)) {
            s_.start_tick = static_cast<int32_t>(start_s / ti);
            if (s_.end_tick > 0 && s_.start_tick >= s_.end_tick) s_.end_tick = std::min(last, s_.start_tick + 1);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kColDim, tr("тік %d"), s_.start_tick);
        label(tr("Кінець"));
        if (slider_float("##end", &end_s, 0.0f, max_s, format_duration(end_s).c_str(), sw)) {
            s_.end_tick = static_cast<int32_t>(end_s / ti);
            if (s_.end_tick >= last) s_.end_tick = -1;
            if (s_.end_tick > 0 && s_.end_tick <= s_.start_tick) s_.start_tick = std::max(0, s_.end_tick - 1);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kColDim, tr("тік %d"), s_.end_tick > 0 ? s_.end_tick : last);
        label(tr("Точні тіки"));
        ImGui::SetNextItemWidth(fs_ * 6);
        changed |= ImGui::InputInt("##st", &s_.start_tick, 0);
        ImGui::SameLine();
        ImGui::TextUnformatted("—");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs_ * 6);
        int end_shown = s_.end_tick > 0 ? s_.end_tick : last;
        if (ImGui::InputInt("##et", &end_shown, 0)) {
            s_.end_tick = end_shown >= last ? -1 : end_shown;
            changed = true;
        }
        // Точний час — зручно для довгих демо (повзунок на кількагодинному записі грубий)
        label(tr("Точний час"));
        auto time_input = [&](const char* id, std::string& buf, bool& active, int32_t tick, auto&& apply) {
            if (!active) buf = format_timecode(tick * ti);
            ImGui::SetNextItemWidth(fs_ * 7);
            ImGui::InputText(id, &buf);
            active = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (auto t = parse_timecode(buf)) {
                    apply(static_cast<int32_t>(std::llround(*t / ti)));
                    changed = true;
                } else {
                    log_warn("{}", trf("Не розумію час «{}». Приклади: 95.5 — секунди, 1:35 — хв:с, 1:02:03 — год:хв:с", buf));
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("год:хв:сек, хв:сек або секунди, напр. 1:02:03.5"));
        };
        time_input("##stime", start_time_buf_, start_time_active_, s_.start_tick, [&](int32_t t) {
            s_.start_tick = t;
            if (s_.end_tick > 0 && s_.start_tick >= s_.end_tick) s_.end_tick = std::min(last, s_.start_tick + 1);
        });
        ImGui::SameLine();
        ImGui::TextUnformatted("—");
        ImGui::SameLine();
        time_input("##etime", end_time_buf_, end_time_active_, s_.end_tick > 0 ? s_.end_tick : last, [&](int32_t t) {
            s_.end_tick = t >= last ? -1 : t;
            if (s_.end_tick > 0 && s_.end_tick <= s_.start_tick) s_.start_tick = std::max(0, s_.end_tick - 1);
        });
        s_.start_tick = std::clamp(s_.start_tick, 0, std::max(0, last - 1));
        if (s_.end_tick == 0 || s_.end_tick > last) s_.end_tick = -1;
        ImGui::EndDisabled();
        const double dur = ((s_.end_tick > 0 ? s_.end_tick : last) - s_.start_tick) * ti;
        label(tr("Тривалість"));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kColAccent, "%s", format_duration(dur).c_str());
        if (auto fps = parse_rational(s_.fps)) {
            ImGui::SameLine();
            ImGui::TextColored(kColDim, tr("Кадрів відео: %.0f, кадрів рендеру гри: %.0f"), dur * fps->value(),
                               dur * fps->value() * std::max(1, s_.motion_blur));
        }
        if (dur > 30 * 60) {
            ImGui::PushStyleColor(ImGuiCol_Text, kColWarn);
            ImGui::TextWrapped("%s", tr("Це довгий відрізок: рендер триватиме годинами, а файл буде великим. "
                                     "Для кліпу зніміть «Увесь запис» і виберіть фрагмент."));
            ImGui::PopStyleColor();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kColDim);
        ImGui::TextWrapped("%s", tr("Підказка: у самій грі номер тіку видно в панелі демо (Shift+F2). "
                                 "До далекого фрагмента гра швидко перемотає демо (demo_gototick) і почне запис "
                                 "за кілька секунд до нього, тож чекати, поки програється початок, не доведеться."));
        ImGui::PopStyleColor();
    }
    card_end();
    if (changed) mark_dirty();
    if (card_begin(tr("Позначки"), tr("Стануть розділами у відео і таймкодами для опису на YouTube"), Icon::Marker, false))
        draw_markers_list();
    card_end();
}

} // namespace gmdr::gui
