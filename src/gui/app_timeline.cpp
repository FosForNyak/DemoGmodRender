// =============================================================================
//  app_timeline.cpp — таймлайн: доріжка на кожного гравця (заголовок з M/S і
//  прослуховуванням), доріжка чату, лінійка з таймкодами, курсор, позначки,
//  смуга масштабу знизу. Кольори — з палітри теми.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_stdlib.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace gmdr::gui {

using namespace ui;

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

// ================================= Таймлайн =======================================
void App::draw_timeline() {
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
    const double fps = parse_rational(s_.fps).value_or(Rational{60, 1}).value();
    const float fs = ImGui::GetFontSize();
    const float u = fs / 15.0f;
    const float base = ImGui::GetStyle().FontSizeBase;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const double in_t = std::max(0, s_.start_tick) * ti;
    const double out_t = (s_.end_tick > 0 ? s_.end_tick : last_tick) * ti;

    // ---- Верхній рядок: курсор великим таймкодом, «Увесь запис», вхід/вихід ----
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float right = p.x + ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(bold_font(), base * 1.3f);
        const std::string tc = timecode(playhead_t_, fps);
        ImGui::TextColored(kColAccent, "%s", tc.c_str());
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Курсор таймлайну. Клацніть по лінійці таймлайну, щоб поставити його."));
        ImGui::SameLine(0, fs * 1.2f);
        const float row_h = ImGui::GetItemRectSize().y;
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y + (row_h - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::BeginDisabled(job_running());
        if (toggle(tr("Увесь запис"), &whole_demo_)) {
            if (whole_demo_) {
                s_.start_tick = 0;
                s_.end_tick = -1;
            }
            mark_dirty();
        }
        ImGui::EndDisabled();
        if (!whole_demo_) {
            ImGui::SameLine(0, fs);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kColDim, "%s", tr("Початок"));
            ImGui::SameLine();
            ImGui::TextUnformatted(timecode(in_t, fps).c_str());
            ImGui::SameLine(0, fs);
            ImGui::TextColored(kColDim, "%s", tr("Кінець"));
            ImGui::SameLine();
            ImGui::TextUnformatted(timecode(out_t, fps).c_str());
        }
        ImGui::SameLine(0, fs);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kColDim, "%s", tr("Тривалість"));
        ImGui::SameLine();
        ImGui::TextUnformatted(timecode(out_t - in_t, fps).c_str());
        // Праворуч — підказка
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, right - fs * 1.1f), ImGui::GetCursorScreenPos().y));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::SameLine(0, 0);
        help_marker(tr("Клацніть або протягніть по лінійці — курсор; I / O — початок і кінець фрагмента в курсорі, M — позначка.\n"
                    "Протягніть по доріжках — вибрати фрагмент; Ctrl+клік — позначка (стане розділом у відео).\n"
                    "Коліщатко — масштаб, протягніть правою кнопкою — зсунути, подвійний клік — уся шкала;\n"
                    "смуга внизу — масштаб і зсув. Правий клік — меню: позначка, початок/кінець фрагмента, перегляд у грі.\n"
                    "M і S на доріжці — вимкнути гравця і лише цей гравець (соло), навушники — прослухати.\n"
                    "Доріжка «Чат»: тьмяні риски — повідомлення, зелені — входи, червоні — виходи; наведіть, щоб прочитати.\n"
                    "Смужка під лінійкою: синій — говорить один, жовтий — двоє, червоний — троє і більше."));
    }

    // ---- Геометрія ----
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float head_w = std::round(fs * 12.5f);
    const float ruler_h = std::round(fs * 1.6f);
    const float act_h = std::round(4 * u);
    const float ev_h = analysis_->events.empty() ? 0.0f : std::round(fs * 1.25f);
    const float lane_h = std::round(fs * 1.7f);
    const float nav_h = std::round(fs * 0.9f);
    const float nav_gap = std::round(4 * u);
    const float tracks_top = origin.y + ruler_h + act_h;
    const float tracks_space = std::max(0.0f, avail.y - ruler_h - act_h - nav_h - nav_gap - ev_h);
    const int total_lanes = static_cast<int>(timeline_.size());
    const int lanes = std::clamp(static_cast<int>(tracks_space / lane_h), 0, total_lanes);
    const float W = std::max(50.0f, avail.x - head_w);
    const float x0 = origin.x + head_w;
    const float tracks_bottom = origin.y + avail.y - nav_h - nav_gap;
    const float lanes_top = tracks_top + ev_h;
    const float ruler_y = origin.y;
    const double span = view_t1_ - view_t0_;
    auto t2x = [&](double t) { return x0 + static_cast<float>((t - view_t0_) / span * W); };
    auto x2t = [&](float x) { return view_t0_ + (x - x0) / W * span; };
    const double fps_eff = fps > 0 ? fps : 60.0;

    // Фон: заголовки доріжок і сама шкала
    dl->AddRectFilled(ImVec2(origin.x, origin.y), ImVec2(x0 - 1, tracks_bottom), kTlHead);
    dl->AddRectFilled(ImVec2(x0, tracks_top), ImVec2(x0 + W, tracks_bottom), kTlBody);

    // ---- Заголовки доріжок ----
    std::map<std::string, double> volumes;
    for (const auto& [k, v] : parse_key_values(s_.voice_volumes)) volumes[k] = parse_double(v).value_or(1.0);
    auto store_volumes = [&] {
        std::string out;
        for (const auto& [k, v] : volumes)
            if (std::abs(v - 1.0) > 0.005) out += std::format("{}{}={:.2f}", out.empty() ? "" : "; ", k, v);
        s_.voice_volumes = out;
        mark_dirty();
    };
    // Кольори доріжок гравців
    static const ImU32 kLabel[] = {IM_COL32(98, 135, 209, 255), IM_COL32(35, 170, 150, 255), IM_COL32(190, 130, 210, 255),
                                   IM_COL32(222, 160, 60, 255), IM_COL32(95, 170, 80, 255),  IM_COL32(214, 100, 145, 255),
                                   IM_COL32(50, 160, 215, 255), IM_COL32(140, 110, 215, 255)};
    if (ev_h > 0) {
        const float y = tracks_top;
        dl->AddLine(ImVec2(origin.x, y + ev_h - 1), ImVec2(x0 + W, y + ev_h - 1), kTlLine);
        dl->AddText(ImVec2(origin.x + 8 * u, y + (ev_h - fs) * 0.5f), kTextDim, tr("Чат"));
    }
    for (int i = 0; i < lanes; ++i) {
        const auto& lane = timeline_[static_cast<size_t>(i)];
        const float y = lanes_top + i * lane_h;
        dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(x0 - 1, y + lane_h - 1), kTlLaneHead);
        dl->AddLine(ImVec2(origin.x, y + lane_h - 1), ImVec2(x0 + W, y + lane_h - 1), kTlLine);
        // «A1» — номер аудіодоріжки
        const std::string an = std::format("A{}", i + 1);
        const float box_w = std::round(fs * 1.75f), box_h = std::round(fs * 1.1f);
        const ImVec2 b0(origin.x + 5 * u, y + std::round((lane_h - box_h) * 0.5f));
        dl->AddRectFilled(b0, ImVec2(b0.x + box_w, b0.y + box_h), kLabel[i % IM_ARRAYSIZE(kLabel)] & IM_COL32(255, 255, 255, 70), 4 * u);
        ImGui::PushFont(bold_font(), base * 0.8f);
        const ImVec2 ats = ImGui::CalcTextSize(an.c_str());
        dl->AddText(ImVec2(b0.x + (box_w - ats.x) * 0.5f, b0.y + (box_h - ats.y) * 0.5f), kText, an.c_str());
        ImGui::PopFont();
        // Кнопки праворуч: навушники, M, S
        const float bsz = std::round(fs * 1.2f);
        const float bx = x0 - 6 * u - bsz * 3 - 4 * u;
        const float by = y + std::round((lane_h - bsz) * 0.5f);
        // Назва гравця (обрізана)
        const float name_x = b0.x + box_w + 6 * u;
        dl->PushClipRect(ImVec2(name_x, y), ImVec2(bx - 4 * u, y + lane_h), true);
        dl->AddText(ImVec2(name_x, y + (lane_h - fs) * 0.5f), kText, lane.name.c_str());
        dl->PopClipRect();
        ImGui::PushID(i);
        const voice::SpeakerTrack* sp = nullptr;
        for (const auto& s : voices_->speakers)
            if (s.key == lane.key) sp = &s;
        const bool playing = playing_key_ == lane.key;
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        ImGui::BeginDisabled(!sp || !VoicePlayer::supported() || (clip_future_.valid() && !playing));
        if (icon_button("##listen", Icon::Headphones, playing ? tr("Зупинити прослуховування") : tr("Прослухати (15 с з початку фрагмента)"),
                        playing, bsz)) {
            if (playing) {
                player_.stop();
                playing_key_.clear();
            } else if (sp) {
                listen_voice(*sp);
            }
        }
        ImGui::EndDisabled();
        const bool muted = volumes.count(lane.key) && volumes[lane.key] <= 0.0;
        const bool solo = s_.voice_mode == "selected" && trim(s_.voice_selected) == lane.key;
        ImGui::BeginDisabled(job_running());
        ImGui::SetCursorScreenPos(ImVec2(bx + bsz + 2 * u, by));
        if (letter_toggle("##mute", "M", muted, IM_COL32(64, 190, 120, 255), tr("Вимкнути цього гравця"))) {
            if (muted) volumes.erase(lane.key);
            else volumes[lane.key] = 0.0;
            store_volumes();
        }
        ImGui::SetCursorScreenPos(ImVec2(bx + (bsz + 2 * u) * 2, by));
        if (letter_toggle("##solo", "S", solo, IM_COL32(230, 190, 60, 255), tr("Лише цей гравець (соло)"))) {
            if (solo) {
                s_.voice_mode = "all";
            } else {
                s_.voice_mode = "selected";
                s_.voice_selected = lane.key;
            }
            mark_dirty();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (lanes < total_lanes) {
        const std::string more = trf("+ ще {}", total_lanes - lanes);
        const float y = lanes_top + lanes * lane_h + 2 * u;
        if (y + fs < tracks_bottom) {
            dl->AddText(ImVec2(origin.x + 8 * u, y), kTextDim, more.c_str());
            if (ImGui::IsMouseHoveringRect(ImVec2(origin.x, y), ImVec2(x0, y + fs)))
                ImGui::SetTooltip("%s", tr("На шкалі — гравці, що говорили найбільше; решта — на сторінці «Звук і голоси»."));
        }
    }
    if (total_lanes == 0)
        dl->AddText(ImVec2(x0 + 8 * u, lanes_top + 6 * u), kTextDim, tr("Голосу в демо немає — шкала показує лише фрагмент"));

    // ---- Лінійка: клацніть або протягніть — курсор ----
    ImGui::SetCursorScreenPos(ImVec2(x0, ruler_y));
    ImGui::InvisibleButton("##ruler", ImVec2(W, ruler_h + act_h));
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const bool ruler_hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive()) playhead_t_ = static_cast<float>(std::clamp(x2t(io.MousePos.x), 0.0, dur));
    if (ruler_hovered) ImGui::SetTooltip("%s", timecode(std::clamp(x2t(io.MousePos.x), 0.0, dur), fps_eff).c_str());

    // ---- Доріжки: протягніть — фрагмент, Ctrl+клік — позначка, правий клік — меню ----
    const float body_h = std::max(1.0f, tracks_bottom - tracks_top);
    ImGui::SetCursorScreenPos(ImVec2(x0, tracks_top));
    ImGui::InvisibleButton("##timeline", ImVec2(W, body_h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);   // коліщатко — масштаб шкали, а не прокрутка
    const bool hovered = ImGui::IsItemHovered();
    if ((hovered || ruler_hovered) && io.MouseWheel != 0) {
        const double tm = std::clamp(x2t(io.MousePos.x), 0.0, dur);
        const double ns = std::clamp(span * (io.MouseWheel > 0 ? 0.8 : 1.25), std::min(2.0, dur), dur);
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
    // Позначка під курсором миші (±4 px)
    int hovered_marker = -1;
    if (hovered || ruler_hovered)
        for (size_t i = 0; i < markers_.size(); ++i)
            if (std::abs(t2x(markers_[i].tick * ti) - io.MousePos.x) <= 4.0f * u) hovered_marker = static_cast<int>(i);
    if (hovered && io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        add_marker_at(static_cast<int32_t>(std::llround(std::clamp(x2t(io.MousePos.x), 0.0, dur) / ti)), {});
    } else if ((hovered || ruler_hovered) && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
               io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 9.0f) {
        tl_ctx_time_ = static_cast<float>(std::clamp(x2t(io.MousePos.x), 0.0, dur));
        ImGui::OpenPopup("##tlctx");
    }
    if (ImGui::BeginPopup("##tlctx")) {
        const int32_t tick = static_cast<int32_t>(std::llround(tl_ctx_time_ / ti));
        ImGui::TextColored(kColDim, tr("%s (тік %d)"), format_timecode(tl_ctx_time_).c_str(), tick);
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Поставити курсор сюди"))) playhead_t_ = tl_ctx_time_;
        if (ImGui::MenuItem(tr("Додати позначку тут"))) add_marker_at(tick, {});
        ImGui::BeginDisabled(job_running());
        if (ImGui::MenuItem(tr("Почати фрагмент тут"))) set_fragment_start(tick);
        if (ImGui::MenuItem(tr("Закінчити фрагмент тут"))) set_fragment_end(tick);
        if (ImGui::MenuItem(tr("Переглянути в грі звідси"))) start_watch(tick);
        ImGui::EndDisabled();
        // Найближча позначка (у межах кількох пікселів) — видалити
        for (size_t i = 0; i < markers_.size(); ++i) {
            if (std::abs(t2x(markers_[i].tick * ti) - t2x(tl_ctx_time_)) > 4.0f * u) continue;
            ImGui::Separator();
            if (ImGui::MenuItem(trf("Видалити позначку «{}»", markers_[i].title).c_str())) {
                auto m = markers_;
                m.erase(m.begin() + static_cast<std::ptrdiff_t>(i));
                set_markers(std::move(m));
            }
            break;
        }
        ImGui::EndPopup();
    }
    if (!job_running()) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !io.KeyCtrl) {
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
                // Клік без протягування — поставити курсор
                if (io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f) playhead_t_ = tl_sel_from_;
                tl_selecting_ = false;
            }
        }
    }

    // ---- Малювання шкали ----
    dl->PushClipRect(ImVec2(x0, origin.y), ImVec2(x0 + W, tracks_bottom), true);
    // Лінійка з поділками й таймкодами
    dl->AddRectFilled(ImVec2(x0, ruler_y), ImVec2(x0 + W, ruler_y + ruler_h), kTlRuler);
    const float label_w = ImGui::CalcTextSize("00:00:00:00").x + fs * 1.2f;
    static const double steps[] = {1.0 / 30, 1.0 / 10, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400};
    double step = steps[IM_ARRAYSIZE(steps) - 1];
    for (double st : steps)
        if (W / (span / st) >= label_w) {
            step = st;
            break;
        }
    const int minor = 5;
    const double mstep = step / minor;
    ImGui::PushFont(nullptr, base * 0.85f);
    for (double t = std::floor(view_t0_ / mstep) * mstep; t <= view_t1_ + 1e-9; t += mstep) {
        if (t < 0) continue;
        const float x = std::round(t2x(t));
        const bool major = std::abs(std::remainder(t, step)) < mstep * 0.5;
        dl->AddLine(ImVec2(x, ruler_y + ruler_h - (major ? ruler_h * 0.45f : ruler_h * 0.2f)), ImVec2(x, ruler_y + ruler_h),
                    major ? kTlTickMajor : kTlTickMinor);
        if (major) dl->AddText(ImVec2(x + 3 * u, ruler_y + 2 * u), kTlRulerText, timecode(t, fps_eff).c_str());
    }
    ImGui::PopFont();
    // Фрагмент на лінійці — акцентна смуга; під час рендеру вже записана частина зеленіє
    if (!whole_demo_) {
        const float xa = t2x(in_t), xb = t2x(out_t);
        dl->AddRectFilled(ImVec2(xa, ruler_y + ruler_h - 5 * u), ImVec2(xb, ruler_y + ruler_h), kAccent);
    }
    if (job_ && job_->running() && dynamic_cast<const render::RenderJob*>(job_.get())) {
        const auto p = job_->progress();
        if (p.frames > 0 && p.fraction > 0) {
            const float xa = t2x(in_t), xb = t2x(in_t + (out_t - in_t) * std::clamp(p.fraction, 0.0, 1.0));
            dl->AddRectFilled(ImVec2(xa, ruler_y + ruler_h - 5 * u), ImVec2(xb, ruler_y + ruler_h), kGreen);
        }
    }
    // Смужка активності під лінійкою (скільки гравців говорить одночасно)
    const float act_y = ruler_y + ruler_h;
    dl->AddRectFilled(ImVec2(x0, act_y), ImVec2(x0 + W, act_y + act_h), kTlLine);
    if (!activity_.empty()) {
        const int nb = static_cast<int>(activity_.size());
        for (int x = 0; x < static_cast<int>(W); ++x) {
            const double ta = x2t(x0 + x), tb = x2t(x0 + x + 1);
            const int b0 = std::clamp(static_cast<int>(ta / dur * nb), 0, nb - 1);
            const int b1 = std::clamp(static_cast<int>(std::ceil(tb / dur * nb)), b0 + 1, nb);
            int m = 0;
            for (int i = b0; i < b1; ++i) m = std::max<int>(m, activity_[static_cast<size_t>(i)]);
            if (!m) continue;
            const ImU32 col = m == 1 ? IM_COL32(60, 120, 200, 255) : m == 2 ? IM_COL32(225, 175, 55, 255) : IM_COL32(225, 75, 70, 255);
            dl->AddRectFilled(ImVec2(x0 + x, act_y), ImVec2(x0 + x + 1, act_y + act_h), col);
        }
    }
    // Доріжка чату й подій
    if (ev_h > 0) {
        const float ey = tracks_top;
        dl->AddRectFilled(ImVec2(x0, ey), ImVec2(x0 + W, ey + ev_h - 1), kTlLane);
        for (const auto& e : analysis_->events) {
            const double t = e.tick * ti;
            if (t < view_t0_ || t > view_t1_) continue;
            const float x = std::floor(t2x(t));
            const ImU32 col = e.kind == demo::DemoEventKind::Chat     ? kTextDim
                              : e.kind == demo::DemoEventKind::Join   ? IM_COL32(110, 215, 125, 230)
                              : e.kind == demo::DemoEventKind::Leave  ? IM_COL32(240, 110, 100, 230)
                                                                      : IM_COL32(240, 200, 100, 220);
            dl->AddRectFilled(ImVec2(x, ey + 3 * u), ImVec2(x + std::max(1.5f, 2 * u), ey + ev_h - 4 * u), col, 1.0f);
        }
    }
    // Доріжки гравців: кліпи-відрізки мовлення
    for (int i = 0; i < lanes; ++i) {
        const auto& lane = timeline_[static_cast<size_t>(i)];
        const float y = lanes_top + i * lane_h;
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + W, y + lane_h - 1), kTlLane);
        const bool muted = volumes.count(lane.key) && volumes[lane.key] <= 0.0;
        const ImU32 col = muted ? IM_COL32(88, 88, 88, 255) : kLabel[i % IM_ARRAYSIZE(kLabel)];
        const ImU32 top = muted ? IM_COL32(120, 120, 120, 255) : (col | IM_COL32(40, 40, 40, 0));
        for (const auto& [a, b] : lane.spans) {
            if (b < view_t0_ || a > view_t1_) continue;
            const float xa = std::max(x0, t2x(a)), xb = std::min(x0 + W, std::max(t2x(a) + 1.5f, t2x(b)));
            const ImVec2 c0(xa, y + 3 * u), c1(xb, y + lane_h - 4 * u);
            dl->AddRectFilled(c0, c1, (col & 0x00FFFFFF) | (static_cast<ImU32>(210) << IM_COL32_A_SHIFT), 2 * u);
            if (xb - xa > 3) dl->AddLine(ImVec2(c0.x + 1, c0.y + 0.5f), ImVec2(c1.x - 1, c0.y + 0.5f), top);
        }
    }
    // Поза фрагментом — темніше
    if (!whole_demo_) {
        const float xa = t2x(in_t), xb = t2x(out_t);
        if (xa > x0) dl->AddRectFilled(ImVec2(x0, tracks_top), ImVec2(xa, tracks_bottom), kTlShade);
        if (xb < x0 + W) dl->AddRectFilled(ImVec2(xb, tracks_top), ImVec2(x0 + W, tracks_bottom), kTlShade);
        dl->AddLine(ImVec2(xa, tracks_top), ImVec2(xa, tracks_bottom), ((kAccent & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 170)));
        dl->AddLine(ImVec2(xb, tracks_top), ImVec2(xb, tracks_bottom), ((kAccent & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 170)));
    }
    // Позначки: зелений «прапорець» на лінійці і тонка лінія на доріжках
    for (size_t i = 0; i < markers_.size(); ++i) {
        const double t = markers_[i].tick * ti;
        if (t < view_t0_ || t > view_t1_) continue;
        const float x = std::round(t2x(t));
        const bool hot = static_cast<int>(i) == hovered_marker;
        const ImU32 col = hot ? IM_COL32(140, 230, 140, 255) : IM_COL32(88, 190, 96, 255);
        dl->AddLine(ImVec2(x, ruler_y + ruler_h * 0.5f), ImVec2(x, tracks_bottom), (col & 0x00FFFFFF) | IM_COL32(0, 0, 0, hot ? 200 : 110));
        draw_icon(dl, Icon::Marker, ImVec2(x, ruler_y + ruler_h * 0.32f), fs * 0.75f, col);
    }
    // Курсор (playhead) із «голівкою» на лінійці
    if (playhead_t_ >= view_t0_ && playhead_t_ <= view_t1_) {
        const float x = std::round(t2x(playhead_t_));
        dl->AddLine(ImVec2(x, ruler_y + ruler_h * 0.5f), ImVec2(x, tracks_bottom), kAccent, std::max(1.0f, 1.5f * u));
        const float hw = std::round(6 * u), hh = std::round(ruler_h * 0.55f);
        const float hy = ruler_y + ruler_h - hh;
        dl->PathLineTo(ImVec2(x - hw, hy));
        dl->PathLineTo(ImVec2(x + hw, hy));
        dl->PathLineTo(ImVec2(x + hw, hy + hh * 0.55f));
        dl->PathLineTo(ImVec2(x, hy + hh));
        dl->PathLineTo(ImVec2(x - hw, hy + hh * 0.55f));
        dl->PathFillConvex(kAccent);
    }
    // Лінія під мишею і підказка: час, хто говорить, позначка, чат поруч
    if (hovered) {
        const double t = std::clamp(x2t(io.MousePos.x), 0.0, dur);
        dl->AddLine(ImVec2(io.MousePos.x, tracks_top), ImVec2(io.MousePos.x, tracks_bottom), kTextFaint);
        std::string who;
        for (const auto& lane : timeline_)
            for (const auto& [a, b] : lane.spans)
                if (t >= a && t <= b) {
                    who += (who.empty() ? "" : ", ") + lane.name;
                    break;
                }
        std::string extra;
        if (hovered_marker >= 0) extra += tr("\nПозначка: ") + markers_[static_cast<size_t>(hovered_marker)].title;
        if (ev_h > 0) {
            const double tol = 4.0 * u / W * span;
            int shown = 0, more = 0;
            for (const auto& e : analysis_->events) {
                const double te = e.tick * ti;
                if (te < t - tol || te > t + tol) continue;
                if (shown++ < 6) extra += "\n" + format_duration(te).substr(0, format_duration(te).find('.')) + "  " + demo::format_event(e);
                else ++more;
            }
            if (more > 0) extra += trf("\n… і ще {}", more);
        }
        ImGui::SetTooltip(tr("%s (тік %d)%s%s%s"), timecode(t, fps_eff).c_str(), static_cast<int>(t / ti), who.empty() ? "" : tr("\nГоворить: "),
                          who.c_str(), extra.c_str());
    }
    dl->PopClipRect();
    // Межа між заголовками і шкалою
    dl->AddLine(ImVec2(x0 - 1, origin.y), ImVec2(x0 - 1, tracks_bottom), kTlLine);

    // ---- Смуга масштабу: ручка — видима частина; тягніть середину — зсув, краї — масштаб ----
    const float ny = tracks_bottom + nav_gap;
    const float nr = nav_h * 0.5f;
    const float na = x0 + static_cast<float>(view_t0_ / dur) * W, nb = x0 + static_cast<float>(view_t1_ / dur) * W;
    const float hx0 = std::min(na, x0 + W - nav_h * 2), hx1 = std::max(nb, hx0 + nav_h * 2);
    ImGui::SetCursorScreenPos(ImVec2(x0, ny));
    ImGui::InvisibleButton("##nav", ImVec2(W, nav_h));
    const bool nav_hov = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated()) {
        const float mx = io.MousePos.x;
        if (std::abs(mx - hx0) <= nr * 1.4f) tl_nav_drag_ = 2;
        else if (std::abs(mx - hx1) <= nr * 1.4f) tl_nav_drag_ = 3;
        else {
            tl_nav_drag_ = 1;
            if (mx < hx0 || mx > hx1) {   // клік поза ручкою — перенести її сюди
                const double c = std::clamp(static_cast<double>((mx - x0) / W) * dur, span * 0.5, dur - span * 0.5);
                view_t0_ = static_cast<float>(c - span * 0.5);
                view_t1_ = static_cast<float>(c + span * 0.5);
            }
        }
    }
    if (ImGui::IsItemActive() && io.MouseDelta.x != 0) {
        const double d = io.MouseDelta.x / W * dur;
        const double min_span = std::min(2.0, dur);
        if (tl_nav_drag_ == 1) {
            const double a = std::clamp(view_t0_ + d, 0.0, dur - span);
            view_t0_ = static_cast<float>(a);
            view_t1_ = static_cast<float>(a + span);
        } else if (tl_nav_drag_ == 2) {
            view_t0_ = static_cast<float>(std::clamp(view_t0_ + d, 0.0, view_t1_ - min_span));
        } else if (tl_nav_drag_ == 3) {
            view_t1_ = static_cast<float>(std::clamp(view_t1_ + d, view_t0_ + min_span, dur));
        }
    }
    if (!ImGui::IsItemActive()) tl_nav_drag_ = 0;
    if (nav_hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        view_t0_ = 0;
        view_t1_ = static_cast<float>(dur);
    }
    dl->AddRectFilled(ImVec2(x0, ny), ImVec2(x0 + W, ny + nav_h), kField, nr);
    const bool nav_act = ImGui::IsItemActive();
    dl->AddRectFilled(ImVec2(hx0, ny + 1), ImVec2(hx1, ny + nav_h - 1),
                      nav_act ? kTextDim : nav_hov ? kTextFaint : kPanelLine, nr);
    dl->AddCircleFilled(ImVec2(hx0 + nr, ny + nr), nr * 0.55f, kText);
    dl->AddCircleFilled(ImVec2(hx1 - nr, ny + nr), nr * 0.55f, kText);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + avail.y));
    ImGui::Dummy(ImVec2(0, 0));
}

} // namespace gmdr::gui
