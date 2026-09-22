// =============================================================================
//  app_tab_range.cpp — вкладка «Фрагмент» і часова шкала голосів.
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

// ============================= Вкладка "Фрагмент" =================================
void App::draw_tab_range() {
    const float fs_ = ImGui::GetFontSize();
    const float lw = fs_ * 11.5f;
    bool changed = false;
    if (!analysis_) {
        ImGui::TextColored(kColDim, "Спершу відкрийте демо.");
        return;
    }
    const double ti = analysis_->tick_interval;
    const int32_t last = analysis_->last_tick;
    if (ImGui::Checkbox("Увесь запис", &whole_demo_)) {
        if (whole_demo_) {
            s_.start_tick = 0;
            s_.end_tick = -1;
        }
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(kColDim, "   Шкала: протягніть мишею — вибрати фрагмент, коліщатко — масштаб, правою — зсунути");
    draw_timeline(0);
    ImGui::BeginDisabled(whole_demo_);
    float start_s = static_cast<float>(std::max(0, s_.start_tick) * ti);
    float end_s = static_cast<float>((s_.end_tick > 0 ? s_.end_tick : last) * ti);
    const float max_s = static_cast<float>(last * ti);
    label("Початок", lw);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - fs_ * 8);
    if (ImGui::SliderFloat("##start", &start_s, 0.0f, max_s, format_duration(start_s).c_str())) {
        s_.start_tick = static_cast<int32_t>(start_s / ti);
        if (s_.end_tick > 0 && s_.start_tick >= s_.end_tick) s_.end_tick = std::min(last, s_.start_tick + 1);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::Text("тік %d", s_.start_tick);
    label("Кінець", lw);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - fs_ * 8);
    if (ImGui::SliderFloat("##end", &end_s, 0.0f, max_s, format_duration(end_s).c_str())) {
        s_.end_tick = static_cast<int32_t>(end_s / ti);
        if (s_.end_tick >= last) s_.end_tick = -1;
        if (s_.end_tick > 0 && s_.end_tick <= s_.start_tick) s_.start_tick = std::max(0, s_.end_tick - 1);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::Text("тік %d", s_.end_tick > 0 ? s_.end_tick : last);
    label("Точні тіки", lw);
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
    label("Точний час", lw);
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
                log_warn("Не розумію час «{}». Приклади: 95.5 — секунди, 1:35 — хв:с, 1:02:03 — год:хв:с", buf);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("год:хв:сек, хв:сек або секунди, напр. 1:02:03.5");
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
    ImGui::TextColored(kColAccent, "Тривалість відео: %s", format_duration(dur).c_str());
    if (auto fps = parse_rational(s_.fps))
        ImGui::TextColored(kColDim, "Кадрів відео: %.0f, кадрів рендеру гри: %.0f", dur * fps->value(),
                           dur * fps->value() * std::max(1, s_.motion_blur));
    if (dur > 30 * 60) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColWarn);
        ImGui::TextWrapped("Це довгий відрізок: рендер триватиме годинами, а файл буде великим. "
                           "Для кліпу зніміть «Увесь запис» і виберіть фрагмент.");
        ImGui::PopStyleColor();
    }
    ImGui::TextWrapped("Підказка: у самій грі номер тіку видно в панелі демо (Shift+F2). "
                       "До далекого фрагмента гра швидко перемотає демо (demo_gototick) і почне запис "
                       "за кілька секунд до нього, тож чекати, поки програється початок, не доведеться.");
    if (changed) mark_dirty();
}

void App::rebuild_timeline() {
    timeline_.clear();
    activity_.clear();
    timeline_src_ = voices_.get();
    if (!voices_ || !analysis_) return;
    const double rate = voice::kVoiceRate;
    std::vector<const voice::SpeakerTrack*> order;
    for (const auto& sp : voices_->speakers) order.push_back(&sp);
    std::stable_sort(order.begin(), order.end(), [](auto* a, auto* b) { return a->seconds > b->seconds; });
    for (const auto* sp : order) {
        TimelineLane lane;
        lane.name = sp->name.empty() ? sp->display_name() : sp->name;
        lane.key = sp->key;
        for (const auto& seg : sp->segments)
            lane.spans.push_back({static_cast<float>(seg.start / rate), static_cast<float>(seg.end() / rate)});
        timeline_.push_back(std::move(lane));
    }
    // Скільки гравців говорить одночасно — по кошиках часу
    const double dur = analysis_->duration_seconds;
    if (dur <= 0) return;
    constexpr int kBuckets = 8192;
    activity_.assign(kBuckets, 0);
    for (const auto& lane : timeline_) {
        int last = -1;
        for (const auto& [a, b] : lane.spans) {
            const int b0 = std::clamp(static_cast<int>(a / dur * kBuckets), 0, kBuckets - 1);
            const int b1 = std::clamp(static_cast<int>(std::ceil(b / dur * kBuckets)), b0 + 1, kBuckets);
            for (int i = std::max(b0, last + 1); i < b1; ++i) activity_[static_cast<size_t>(i)] = static_cast<uint8_t>(std::min(255, activity_[static_cast<size_t>(i)] + 1));
            last = std::max(last, b1 - 1);
        }
    }
    view_t0_ = 0;
    view_t1_ = static_cast<float>(dur);
}

void App::draw_timeline(float) {
    if (!analysis_) return;
    if (timeline_src_ != voices_.get()) rebuild_timeline();
    const double dur = analysis_->duration_seconds;
    const double ti = analysis_->tick_interval;
    const int32_t last_tick = analysis_->last_tick;
    if (dur <= 0 || ti <= 0) return;
    if (!(view_t1_ > view_t0_)) {
        view_t0_ = 0;
        view_t1_ = static_cast<float>(dur);
    }
    const float fs = ImGui::GetFontSize();
    const int max_lanes = 8;
    const int lanes = std::min<int>(static_cast<int>(timeline_.size()), max_lanes);
    const float lane_h = fs * 0.95f, act_h = fs * 0.75f, axis_h = fs * 1.3f;
    const float W = std::max(50.0f, ImGui::GetContentRegionAvail().x);
    const float body_h = act_h + lanes * lane_h + (lanes == 0 ? fs * 1.2f : 0.0f);
    const float H = body_h + axis_h;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##timeline", ImVec2(W, H), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);   // коліщатко — масштаб шкали, а не прокрутка вкладки
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const double span = view_t1_ - view_t0_;
    auto t2x = [&](double t) { return p0.x + static_cast<float>((t - view_t0_) / span * W); };
    auto x2t = [&](float x) { return view_t0_ + (x - p0.x) / W * span; };
    ImGuiIO& io = ImGui::GetIO();

    // ---- Керування ----
    if (hovered && io.MouseWheel != 0) {
        const double tm = std::clamp(x2t(io.MousePos.x), 0.0, dur);
        const double ns = std::clamp(span * (io.MouseWheel > 0 ? 0.8 : 1.25), std::min(5.0, dur), dur);
        double a = tm - (tm - view_t0_) * ns / span;
        a = std::clamp(a, 0.0, dur - ns);
        view_t0_ = static_cast<float>(a);
        view_t1_ = static_cast<float>(a + ns);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const double shift = -io.MouseDelta.x / W * span;
        const double a = std::clamp(view_t0_ + shift, 0.0, dur - span);
        view_t0_ = static_cast<float>(a);
        view_t1_ = static_cast<float>(a + span);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        view_t0_ = 0;
        view_t1_ = static_cast<float>(dur);
    }
    if (!job_running()) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            tl_selecting_ = true;
            tl_sel_from_ = static_cast<float>(std::clamp(x2t(io.MousePos.x), 0.0, dur));
        }
        if (tl_selecting_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const double t = std::clamp(x2t(io.MousePos.x), 0.0, dur);
                const double a = std::min<double>(tl_sel_from_, t), b = std::max<double>(tl_sel_from_, t);
                if (b - a >= 0.25) {
                    s_.start_tick = static_cast<int32_t>(std::llround(a / ti));
                    const int32_t e = static_cast<int32_t>(std::llround(b / ti));
                    s_.end_tick = e >= last_tick ? -1 : e;
                    whole_demo_ = false;
                    mark_dirty();
                }
            } else {
                tl_selecting_ = false;
            }
        }
    }

    // ---- Малювання ----
    dl->PushClipRect(p0, ImVec2(p0.x + W, p0.y + H), true);
    dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + body_h), IM_COL32(24, 26, 31, 255), 4.0f);
    // Смуга активності: скільки гравців говорить одночасно
    if (!activity_.empty()) {
        const int nb = static_cast<int>(activity_.size());
        for (int x = 0; x < static_cast<int>(W); ++x) {
            const double ta = x2t(p0.x + x), tb = x2t(p0.x + x + 1);
            const int b0 = std::clamp(static_cast<int>(ta / dur * nb), 0, nb - 1);
            const int b1 = std::clamp(static_cast<int>(std::ceil(tb / dur * nb)), b0 + 1, nb);
            int m = 0;
            for (int i = b0; i < b1; ++i) m = std::max<int>(m, activity_[static_cast<size_t>(i)]);
            if (!m) continue;
            const ImU32 col = m == 1 ? IM_COL32(70, 110, 170, 255) : m == 2 ? IM_COL32(220, 160, 60, 255) : IM_COL32(235, 80, 70, 255);
            dl->AddRectFilled(ImVec2(p0.x + x, p0.y + 2), ImVec2(p0.x + x + 1, p0.y + act_h - 2), col);
        }
    }
    // Доріжки гравців
    static const ImU32 palette[] = {IM_COL32(90, 170, 255, 230), IM_COL32(120, 220, 130, 230), IM_COL32(250, 190, 80, 230),
                                    IM_COL32(230, 110, 200, 230), IM_COL32(150, 130, 250, 230), IM_COL32(90, 220, 220, 230),
                                    IM_COL32(250, 130, 110, 230), IM_COL32(200, 200, 110, 230)};
    std::vector<float> vol_cache;
    const auto kv = parse_key_values(s_.voice_volumes);
    for (int i = 0; i < lanes; ++i) {
        const auto& lane = timeline_[static_cast<size_t>(i)];
        const float y = p0.y + act_h + i * lane_h;
        if (i % 2 == 0) dl->AddRectFilled(ImVec2(p0.x, y), ImVec2(p0.x + W, y + lane_h), IM_COL32(255, 255, 255, 8));
        bool muted = false;
        for (const auto& [k, v] : kv)
            if (k == lane.key && parse_double(v).value_or(1.0) <= 0.0) muted = true;
        const ImU32 col = muted ? IM_COL32(110, 110, 110, 160) : palette[i % IM_ARRAYSIZE(palette)];
        for (const auto& [a, b] : lane.spans) {
            if (b < view_t0_ || a > view_t1_) continue;
            const float xa = std::max(p0.x, t2x(a)), xb = std::min(p0.x + W, std::max(t2x(a) + 1.0f, t2x(b)));
            dl->AddRectFilled(ImVec2(xa, y + 2), ImVec2(xb, y + lane_h - 2), col, 2.0f);
        }
        const ImVec2 tp(p0.x + 5, y + (lane_h - fs) * 0.5f);
        dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 200), lane.name.c_str());
        dl->AddText(tp, IM_COL32(235, 238, 245, 255), lane.name.c_str());
    }
    if (lanes == 0) {
        const char* msg = "Голосу в демо немає — шкала показує лише фрагмент";
        dl->AddText(ImVec2(p0.x + 6, p0.y + act_h + fs * 0.1f), IM_COL32(150, 155, 165, 255), msg);
    }
    // Вибраний фрагмент
    const double fa = std::max(0, s_.start_tick) * ti;
    const double fb = (s_.end_tick > 0 ? s_.end_tick : last_tick) * ti;
    if (!whole_demo_) {
        const float xa = t2x(fa), xb = t2x(fb);
        dl->AddRectFilled(ImVec2(std::max(p0.x, xa), p0.y), ImVec2(std::min(p0.x + W, xb), p0.y + body_h), IM_COL32(80, 140, 255, 45));
        dl->AddLine(ImVec2(xa, p0.y), ImVec2(xa, p0.y + body_h), IM_COL32(120, 180, 255, 255), 2.0f);
        dl->AddLine(ImVec2(xb, p0.y), ImVec2(xb, p0.y + body_h), IM_COL32(120, 180, 255, 255), 2.0f);
    }
    // Вісь часу з "круглими" поділками
    static const double steps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400};
    double step = steps[IM_ARRAYSIZE(steps) - 1];
    for (double st : steps)
        if (W / (span / st) >= fs * 6) {
            step = st;
            break;
        }
    const float ay = p0.y + body_h;
    for (double t = std::ceil(view_t0_ / step) * step; t <= view_t1_ + 1e-6; t += step) {
        const float x = t2x(t);
        dl->AddLine(ImVec2(x, ay), ImVec2(x, ay + fs * 0.3f), IM_COL32(150, 155, 165, 255));
        const std::string lbl = format_duration(t);
        const std::string shown = lbl.size() > 2 && lbl.substr(lbl.size() - 2) == ".0" ? lbl.substr(0, lbl.size() - 2) : lbl;
        dl->AddText(ImVec2(x + 3, ay + fs * 0.1f), IM_COL32(150, 155, 165, 255), shown.c_str());
    }
    // Курсор і підказка: час і хто говорить
    if (hovered) {
        const double t = std::clamp(x2t(io.MousePos.x), 0.0, dur);
        dl->AddLine(ImVec2(io.MousePos.x, p0.y), ImVec2(io.MousePos.x, p0.y + body_h), IM_COL32(255, 255, 255, 120));
        std::string who;
        for (const auto& lane : timeline_)
            for (const auto& [a, b] : lane.spans)
                if (t >= a && t <= b) {
                    who += (who.empty() ? "" : ", ") + lane.name;
                    break;
                }
        ImGui::SetTooltip("%s (тік %d)%s%s", format_timecode(t).c_str(), static_cast<int>(t / ti), who.empty() ? "" : "\nГоворить: ",
                          who.c_str());
    }
    dl->PopClipRect();
    if (static_cast<int>(timeline_.size()) > lanes)
        ImGui::TextColored(kColDim, "На шкалі — %d гравців, що говорили найбільше (ще %d — у таблиці голосів). "
                           "Верхня смуга: синій — говорить один, жовтий — двоє, червоний — троє і більше.",
                           lanes, static_cast<int>(timeline_.size()) - lanes);
}

} // namespace gmdr::gui
