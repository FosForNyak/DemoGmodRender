// =============================================================================
//  ui_widgets.cpp — віджети програми: іконки й логотип, кнопки, картки, форма
//  (підписи, галочки, вимикачі, повзунки, «гарячі» значення), бічна навігація,
//  панелі із заголовком і роздільники між ними.
//
//  Усі розміри задано в «пікселях макета» для шрифту 15 px і множаться на
//  u() = розмір шрифту / 15 — тож інтерфейс однаково виглядає на будь-якому DPI
//  і масштабі. Кольори беруться з палітри теми (app_ui.hpp) і проходять через
//  ImGui::GetColorU32 — тоді всередині BeginDisabled() вони самі стають блідішими.
// =============================================================================
#include "app_ui.hpp"

#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <initializer_list>
#include <type_traits>

namespace gmdr::gui::ui {

namespace {
float u() { return ImGui::GetFontSize() / 15.0f; }
ImU32 col(ImU32 c) { return ImGui::GetColorU32(c); }
ImU32 alpha(ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(a, 0, 255)) << IM_COL32_A_SHIFT); }
ImU32 blend(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}
ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
bool disabled_now() { return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0; }

// Стан відкритої панелі (begin_panel … end_panel)
struct PanelFrame {
    ImVec2 pos, size;
    float  header_h = 0;
    float  footer_h = 0;
    bool   in_content = false;
};
std::vector<PanelFrame> g_panels;

void polyline(ImDrawList* dl, ImVec2 c, float h, std::initializer_list<ImVec2> pts, ImU32 cl, float t, bool closed = false) {
    for (const ImVec2& p : pts) dl->PathLineTo(ImVec2(c.x + p.x * h, c.y + p.y * h));
    dl->PathStroke(cl, closed ? ImDrawFlags_Closed : ImDrawFlags_None, t);
}
void polyfill(ImDrawList* dl, ImVec2 c, float h, std::initializer_list<ImVec2> pts, ImU32 cl) {
    for (const ImVec2& p : pts) dl->PathLineTo(ImVec2(c.x + p.x * h, c.y + p.y * h));
    dl->PathFillConvex(cl);
}

// Плавне наближення значення, що зберігається у сховищі стану вікна (для анімацій)
float animate(ImGuiID id, float target, float speed = 16.0f) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    *v += (target - *v) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed);
    if (std::abs(target - *v) < 0.002f) *v = target;
    return *v;
}

// «Гаряче» значення: спільне для float та int
template <typename T>
bool hot_value(const char* id, T* v, float speed, T min, T max, const char* fmt) {
    const ImGuiID iid = ImGui::GetID(id);
    const bool editing = ImGui::TempInputIsActive(iid);
    char buf[96];
    ImFormatString(buf, sizeof(buf), fmt, *v);
    const ImGuiStyle& st = ImGui::GetStyle();
    const float text_w = ImGui::CalcTextSize(buf).x;
    ImGui::SetNextItemWidth(editing ? std::max(ImGui::GetFontSize() * 5.0f, text_w + st.FramePadding.x * 2)
                                    : text_w + st.FramePadding.x * 2);
    if (!editing) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, alpha(kText, 14));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, alpha(kText, 22));
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentText);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    }
    bool changed;
    if constexpr (std::is_same_v<T, int>)
        changed = ImGui::DragInt(id, v, speed, min, max, fmt, ImGuiSliderFlags_AlwaysClamp);
    else
        changed = ImGui::DragFloat(id, v, speed, min, max, fmt, ImGuiSliderFlags_AlwaysClamp);
    if (!editing) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            // Пунктирне підкреслення — підказка, що значення можна тягнути
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            const float y = b.y - st.FramePadding.y + 1.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (float x = a.x + st.FramePadding.x; x < b.x - st.FramePadding.x; x += 3.0f * u())
                dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + 1.5f * u(), b.x - st.FramePadding.x), y), col(kAccentText), 1.0f);
        }
    }
    return changed;
}

template <typename T>
bool slider_impl(const char* id, T* v, T min, T max, const char* fmt, float width) {
    ImGui::PushID(id);
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight();
    // Місце для значення праворуч — з запасом, щоб доріжка не «стрибала» при зміні числа
    char buf[96];
    ImFormatString(buf, sizeof(buf), fmt, *v);
    const float reserve = std::max(fs * 4.0f, ImGui::CalcTextSize(buf).x + st.FramePadding.x * 2);
    const float track_w = std::max(fs * 3.0f, width - reserve - st.ItemInnerSpacing.x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##track", ImVec2(track_w, h));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float r = h * 0.26f;
    const float x0 = p.x + r + 1, x1 = p.x + track_w - r - 1;
    bool changed = false;
    if (active && x1 > x0) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
        T nv;
        if constexpr (std::is_same_v<T, int>) nv = static_cast<int>(std::lround(min + t * static_cast<float>(max - min)));
        else nv = min + t * (max - min);
        if (nv != *v) {
            *v = nv;
            changed = true;
        }
    }
    const float t = max > min ? std::clamp(static_cast<float>(*v - min) / static_cast<float>(max - min), 0.0f, 1.0f) : 0.0f;
    const float hx = x0 + t * (x1 - x0), cy = p.y + h * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = std::max(3.0f, 4.0f * u());
    dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), col(kPanelLine), th);
    dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(std::max(x0 + th, hx), cy + th * 0.5f), col(kAccent), th);
    if (hovered || active) dl->AddCircleFilled(ImVec2(hx, cy), r * 1.7f, col(alpha(kAccent, 50)));
    dl->AddCircleFilled(ImVec2(hx, cy), r, col(IM_COL32(255, 255, 255, 255)));
    dl->AddCircle(ImVec2(hx, cy), r, col(active ? kAccentHover : kAccent), 0, std::max(1.5f, 2.0f * u()));
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    const float speed = static_cast<float>(max - min) / std::max(50.0f, track_w);
    changed |= hot_value<T>("##val", v, speed, min, max, fmt);
    ImGui::PopID();
    return changed;
}
} // namespace

// ==================================== Іконки ======================================
void draw_icon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 cl) {
    const float h = size * 0.5f;
    const float t = std::max(1.0f, size * 0.09f);
    switch (icon) {
    case Icon::None: break;
    case Icon::Play: polyfill(dl, c, h, {{-0.55f, -0.72f}, {0.75f, 0.0f}, {-0.55f, 0.72f}}, cl); break;
    case Icon::Stop: dl->AddRectFilled(ImVec2(c.x - h * 0.55f, c.y - h * 0.55f), ImVec2(c.x + h * 0.55f, c.y + h * 0.55f), cl, h * 0.12f); break;
    case Icon::Record: dl->AddCircleFilled(c, h * 0.6f, cl); break;
    case Icon::Menu:
        for (int i = -1; i <= 1; ++i)
            dl->AddLine(ImVec2(c.x - h * 0.7f, c.y + i * h * 0.45f), ImVec2(c.x + h * 0.7f, c.y + i * h * 0.45f), cl, t);
        break;
    case Icon::ChevronDown: polyline(dl, c, h, {{-0.55f, -0.28f}, {0.0f, 0.28f}, {0.55f, -0.28f}}, cl, t * 1.2f); break;
    case Icon::ChevronUp: polyline(dl, c, h, {{-0.55f, 0.28f}, {0.0f, -0.28f}, {0.55f, 0.28f}}, cl, t * 1.2f); break;
    case Icon::ChevronRight: polyline(dl, c, h, {{-0.28f, -0.55f}, {0.28f, 0.0f}, {-0.28f, 0.55f}}, cl, t * 1.2f); break;
    case Icon::ChevronLeft: polyline(dl, c, h, {{0.28f, -0.55f}, {-0.28f, 0.0f}, {0.28f, 0.55f}}, cl, t * 1.2f); break;
    case Icon::Folder:
        polyline(dl, c, h, {{-0.8f, -0.62f}, {-0.2f, -0.62f}, {0.02f, -0.38f}, {0.8f, -0.38f}, {0.8f, 0.62f}, {-0.8f, 0.62f}}, cl, t, true);
        break;
    case Icon::Plus:
        dl->AddLine(ImVec2(c.x - h * 0.65f, c.y), ImVec2(c.x + h * 0.65f, c.y), cl, t * 1.2f);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.65f), ImVec2(c.x, c.y + h * 0.65f), cl, t * 1.2f);
        break;
    case Icon::Close:
        dl->AddLine(ImVec2(c.x - h * 0.5f, c.y - h * 0.5f), ImVec2(c.x + h * 0.5f, c.y + h * 0.5f), cl, t * 1.2f);
        dl->AddLine(ImVec2(c.x + h * 0.5f, c.y - h * 0.5f), ImVec2(c.x - h * 0.5f, c.y + h * 0.5f), cl, t * 1.2f);
        break;
    case Icon::Search:
        dl->AddCircle(ImVec2(c.x - h * 0.15f, c.y - h * 0.15f), h * 0.48f, cl, 0, t);
        dl->AddLine(ImVec2(c.x + h * 0.2f, c.y + h * 0.2f), ImVec2(c.x + h * 0.7f, c.y + h * 0.7f), cl, t * 1.3f);
        break;
    case Icon::MarkIn: polyline(dl, c, h, {{0.35f, -0.7f}, {-0.2f, -0.7f}, {-0.2f, 0.7f}, {0.35f, 0.7f}}, cl, t * 1.2f); break;
    case Icon::MarkOut: polyline(dl, c, h, {{-0.35f, -0.7f}, {0.2f, -0.7f}, {0.2f, 0.7f}, {-0.35f, 0.7f}}, cl, t * 1.2f); break;
    case Icon::GoToIn:
        dl->AddLine(ImVec2(c.x - h * 0.6f, c.y - h * 0.65f), ImVec2(c.x - h * 0.6f, c.y + h * 0.65f), cl, t * 1.3f);
        polyfill(dl, c, h, {{0.65f, -0.65f}, {0.65f, 0.65f}, {-0.35f, 0.0f}}, cl);
        break;
    case Icon::GoToOut:
        dl->AddLine(ImVec2(c.x + h * 0.6f, c.y - h * 0.65f), ImVec2(c.x + h * 0.6f, c.y + h * 0.65f), cl, t * 1.3f);
        polyfill(dl, c, h, {{-0.65f, -0.65f}, {0.35f, 0.0f}, {-0.65f, 0.65f}}, cl);
        break;
    case Icon::Marker: polyfill(dl, c, h, {{-0.5f, -0.65f}, {0.5f, -0.65f}, {0.5f, 0.2f}, {0.0f, 0.7f}, {-0.5f, 0.2f}}, cl); break;
    case Icon::Speaker:
        polyfill(dl, c, h, {{-0.75f, -0.25f}, {-0.4f, -0.25f}, {0.0f, -0.65f}, {0.0f, 0.65f}, {-0.4f, 0.25f}, {-0.75f, 0.25f}}, cl);
        dl->PathArcTo(ImVec2(c.x + h * 0.05f, c.y), h * 0.38f, -0.9f, 0.9f);
        dl->PathStroke(cl, 0, t);
        dl->PathArcTo(ImVec2(c.x + h * 0.05f, c.y), h * 0.7f, -0.9f, 0.9f);
        dl->PathStroke(cl, 0, t);
        break;
    case Icon::Headphones:
        dl->PathArcTo(ImVec2(c.x, c.y + h * 0.1f), h * 0.62f, IM_PI, IM_PI * 2.0f);
        dl->PathStroke(cl, 0, t * 1.2f);
        dl->AddRectFilled(ImVec2(c.x - h * 0.78f, c.y + h * 0.05f), ImVec2(c.x - h * 0.38f, c.y + h * 0.7f), cl, h * 0.12f);
        dl->AddRectFilled(ImVec2(c.x + h * 0.38f, c.y + h * 0.05f), ImVec2(c.x + h * 0.78f, c.y + h * 0.7f), cl, h * 0.12f);
        break;
    case Icon::Eye:
        dl->PathArcTo(ImVec2(c.x, c.y + h * 0.62f), h * 1.0f, -IM_PI * 0.8f, -IM_PI * 0.2f);
        dl->PathStroke(cl, 0, t);
        dl->PathArcTo(ImVec2(c.x, c.y - h * 0.62f), h * 1.0f, IM_PI * 0.2f, IM_PI * 0.8f);
        dl->PathStroke(cl, 0, t);
        dl->AddCircleFilled(c, h * 0.26f, cl);
        break;
    case Icon::Check: polyline(dl, c, h, {{-0.6f, 0.0f}, {-0.15f, 0.45f}, {0.65f, -0.5f}}, cl, t * 1.4f); break;
    case Icon::Warning:
        polyline(dl, c, h, {{0.0f, -0.72f}, {0.78f, 0.62f}, {-0.78f, 0.62f}}, cl, t, true);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.22f), ImVec2(c.x, c.y + h * 0.18f), cl, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y + h * 0.38f), t * 0.7f, cl);
        break;
    case Icon::Info:
        dl->AddCircle(c, h * 0.72f, cl, 0, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y - h * 0.32f), t * 0.75f, cl);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.08f), ImVec2(c.x, c.y + h * 0.38f), cl, t);
        break;
    case Icon::Film:
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.6f), ImVec2(c.x + h * 0.8f, c.y + h * 0.6f), cl, h * 0.1f, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.45f, c.y - h * 0.6f), ImVec2(c.x - h * 0.45f, c.y + h * 0.6f), cl, t);
        dl->AddLine(ImVec2(c.x + h * 0.45f, c.y - h * 0.6f), ImVec2(c.x + h * 0.45f, c.y + h * 0.6f), cl, t);
        for (float y : {-0.2f, 0.2f}) {
            dl->AddLine(ImVec2(c.x - h * 0.8f, c.y + y * h), ImVec2(c.x - h * 0.45f, c.y + y * h), cl, t);
            dl->AddLine(ImVec2(c.x + h * 0.45f, c.y + y * h), ImVec2(c.x + h * 0.8f, c.y + y * h), cl, t);
        }
        break;
    case Icon::Queue:
        for (int i = -1; i <= 1; ++i) {
            const float y = c.y + i * h * 0.5f;
            dl->AddRectFilled(ImVec2(c.x - h * 0.78f, y - t), ImVec2(c.x - h * 0.5f, y + t), cl);
            dl->AddLine(ImVec2(c.x - h * 0.3f, y), ImVec2(c.x + h * 0.78f, y), cl, t);
        }
        break;
    case Icon::Pencil:
        polyline(dl, c, h, {{-0.62f, 0.62f}, {-0.62f, 0.25f}, {0.35f, -0.72f}, {0.72f, -0.35f}, {-0.25f, 0.62f}}, cl, t, true);
        dl->AddLine(ImVec2(c.x + h * 0.15f, c.y - h * 0.52f), ImVec2(c.x + h * 0.52f, c.y - h * 0.15f), cl, t);
        break;
    case Icon::Refresh: {
        dl->PathArcTo(c, h * 0.6f, -IM_PI * 0.3f, IM_PI * 1.35f);
        dl->PathStroke(cl, 0, t);
        const ImVec2 e(c.x + std::cos(-IM_PI * 0.3f) * h * 0.6f, c.y + std::sin(-IM_PI * 0.3f) * h * 0.6f);
        dl->AddTriangleFilled(ImVec2(e.x - h * 0.3f, e.y - h * 0.15f), ImVec2(e.x + h * 0.25f, e.y - h * 0.35f),
                              ImVec2(e.x + h * 0.1f, e.y + h * 0.25f), cl);
        break;
    }
    case Icon::Home:
        polyline(dl, c, h, {{-0.78f, -0.02f}, {0.0f, -0.74f}, {0.78f, -0.02f}}, cl, t * 1.1f);
        polyline(dl, c, h, {{-0.55f, -0.2f}, {-0.55f, 0.7f}, {0.55f, 0.7f}, {0.55f, -0.2f}}, cl, t * 1.1f);
        dl->AddRectFilled(ImVec2(c.x - h * 0.16f, c.y + h * 0.22f), ImVec2(c.x + h * 0.16f, c.y + h * 0.7f), cl, h * 0.05f);
        break;
    case Icon::Globe:
        dl->AddCircle(c, h * 0.76f, cl, 0, t);
        dl->AddEllipse(c, ImVec2(h * 0.34f, h * 0.76f), cl, 0, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.76f, c.y), ImVec2(c.x + h * 0.76f, c.y), cl, t);
        dl->AddLine(ImVec2(c.x - h * 0.64f, c.y - h * 0.38f), ImVec2(c.x + h * 0.64f, c.y - h * 0.38f), cl, t * 0.8f);
        dl->AddLine(ImVec2(c.x - h * 0.64f, c.y + h * 0.38f), ImVec2(c.x + h * 0.64f, c.y + h * 0.38f), cl, t * 0.8f);
        break;
    case Icon::Gamepad:
        dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.45f), ImVec2(c.x + h * 0.85f, c.y + h * 0.5f), cl, h * 0.42f, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.55f, c.y + h * 0.02f), ImVec2(c.x - h * 0.15f, c.y + h * 0.02f), cl, t);
        dl->AddLine(ImVec2(c.x - h * 0.35f, c.y - h * 0.18f), ImVec2(c.x - h * 0.35f, c.y + h * 0.22f), cl, t);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.3f, c.y - h * 0.04f), t * 0.95f, cl);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.52f, c.y + h * 0.16f), t * 0.95f, cl);
        break;
    case Icon::Scissors:
        dl->AddCircle(ImVec2(c.x - h * 0.45f, c.y + h * 0.48f), h * 0.24f, cl, 0, t);
        dl->AddCircle(ImVec2(c.x + h * 0.45f, c.y + h * 0.48f), h * 0.24f, cl, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.3f, c.y + h * 0.3f), ImVec2(c.x + h * 0.45f, c.y - h * 0.72f), cl, t);
        dl->AddLine(ImVec2(c.x + h * 0.3f, c.y + h * 0.3f), ImVec2(c.x - h * 0.45f, c.y - h * 0.72f), cl, t);
        break;
    case Icon::Chat:
        dl->AddRect(ImVec2(c.x - h * 0.78f, c.y - h * 0.62f), ImVec2(c.x + h * 0.78f, c.y + h * 0.36f), cl, h * 0.24f, 0, t);
        polyfill(dl, c, h, {{-0.42f, 0.3f}, {-0.1f, 0.3f}, {-0.5f, 0.74f}}, cl);
        for (float x : {-0.36f, 0.0f, 0.36f}) dl->AddCircleFilled(ImVec2(c.x + x * h, c.y - h * 0.13f), t * 0.8f, cl);
        break;
    case Icon::Library:
        dl->AddRect(ImVec2(c.x - h * 0.72f, c.y - h * 0.62f), ImVec2(c.x - h * 0.36f, c.y + h * 0.66f), cl, h * 0.06f, 0, t);
        dl->AddRect(ImVec2(c.x - h * 0.2f, c.y - h * 0.62f), ImVec2(c.x + h * 0.16f, c.y + h * 0.66f), cl, h * 0.06f, 0, t);
        polyline(dl, c, h, {{0.32f, -0.5f}, {0.62f, -0.6f}, {0.86f, 0.56f}, {0.56f, 0.66f}}, cl, t, true);
        break;
    case Icon::Terminal:
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.62f), ImVec2(c.x + h * 0.8f, c.y + h * 0.62f), cl, h * 0.14f, 0, t);
        polyline(dl, c, h, {{-0.48f, -0.26f}, {-0.2f, 0.0f}, {-0.48f, 0.26f}}, cl, t);
        dl->AddLine(ImVec2(c.x + h * 0.02f, c.y + h * 0.3f), ImVec2(c.x + h * 0.44f, c.y + h * 0.3f), cl, t);
        break;
    case Icon::Gear: {
        const int teeth = 8;
        for (int i = 0; i < teeth; ++i) {
            const float a = IM_PI * 2.0f * i / teeth;
            const ImVec2 d(std::cos(a), std::sin(a));
            dl->AddLine(ImVec2(c.x + d.x * h * 0.5f, c.y + d.y * h * 0.5f), ImVec2(c.x + d.x * h * 0.8f, c.y + d.y * h * 0.8f), cl, t * 1.9f);
        }
        dl->AddCircle(c, h * 0.52f, cl, 0, t * 1.2f);
        dl->AddCircle(c, h * 0.2f, cl, 0, t);
        break;
    }
    case Icon::Sidebar:
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.64f), ImVec2(c.x + h * 0.8f, c.y + h * 0.64f), cl, h * 0.14f, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.25f, c.y - h * 0.64f), ImVec2(c.x - h * 0.25f, c.y + h * 0.64f), cl, t);
        break;
    case Icon::Maximize:
        polyline(dl, c, h, {{-0.7f, -0.2f}, {-0.7f, -0.7f}, {-0.2f, -0.7f}}, cl, t);
        polyline(dl, c, h, {{0.2f, -0.7f}, {0.7f, -0.7f}, {0.7f, -0.2f}}, cl, t);
        polyline(dl, c, h, {{0.7f, 0.2f}, {0.7f, 0.7f}, {0.2f, 0.7f}}, cl, t);
        polyline(dl, c, h, {{-0.2f, 0.7f}, {-0.7f, 0.7f}, {-0.7f, 0.2f}}, cl, t);
        break;
    case Icon::Sparkle:
        polyfill(dl, c, h, {{0.0f, -0.8f}, {0.18f, -0.18f}, {0.8f, 0.0f}, {0.18f, 0.18f}}, cl);
        polyfill(dl, c, h, {{0.0f, 0.8f}, {-0.18f, 0.18f}, {-0.8f, 0.0f}, {-0.18f, -0.18f}}, cl);
        polyfill(dl, c, h, {{0.0f, -0.8f}, {0.18f, -0.18f}, {0.0f, 0.0f}, {-0.18f, -0.18f}}, cl);
        polyfill(dl, c, h, {{0.0f, 0.8f}, {-0.18f, 0.18f}, {0.0f, 0.0f}, {0.18f, 0.18f}}, cl);
        break;
    case Icon::User:
        dl->AddCircle(ImVec2(c.x, c.y - h * 0.3f), h * 0.32f, cl, 0, t);
        dl->PathArcTo(ImVec2(c.x, c.y + h * 0.78f), h * 0.66f, IM_PI * 1.1f, IM_PI * 1.9f);
        dl->PathStroke(cl, 0, t);
        break;
    case Icon::Clock:
        dl->AddCircle(c, h * 0.74f, cl, 0, t);
        polyline(dl, c, h, {{0.0f, -0.42f}, {0.0f, 0.0f}, {0.3f, 0.2f}}, cl, t);
        break;
    case Icon::File:
        polyline(dl, c, h, {{-0.55f, -0.75f}, {0.18f, -0.75f}, {0.55f, -0.38f}, {0.55f, 0.75f}, {-0.55f, 0.75f}}, cl, t, true);
        polyline(dl, c, h, {{0.18f, -0.75f}, {0.18f, -0.38f}, {0.55f, -0.38f}}, cl, t);
        break;
    case Icon::Mic:
        dl->AddRect(ImVec2(c.x - h * 0.24f, c.y - h * 0.78f), ImVec2(c.x + h * 0.24f, c.y + h * 0.2f), cl, h * 0.24f, 0, t);
        dl->PathArcTo(ImVec2(c.x, c.y - h * 0.05f), h * 0.46f, 0.1f, IM_PI - 0.1f);
        dl->PathStroke(cl, 0, t);
        dl->AddLine(ImVec2(c.x, c.y + h * 0.42f), ImVec2(c.x, c.y + h * 0.75f), cl, t);
        break;
    case Icon::Monitor:
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.62f), ImVec2(c.x + h * 0.8f, c.y + h * 0.34f), cl, h * 0.1f, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.35f, c.y + h * 0.7f), ImVec2(c.x + h * 0.35f, c.y + h * 0.7f), cl, t);
        dl->AddLine(ImVec2(c.x, c.y + h * 0.34f), ImVec2(c.x, c.y + h * 0.7f), cl, t);
        break;
    case Icon::Timeline:
        dl->AddLine(ImVec2(c.x - h * 0.8f, c.y - h * 0.5f), ImVec2(c.x + h * 0.2f, c.y - h * 0.5f), cl, t * 1.6f);
        dl->AddLine(ImVec2(c.x - h * 0.5f, c.y), ImVec2(c.x + h * 0.8f, c.y), cl, t * 1.6f);
        dl->AddLine(ImVec2(c.x - h * 0.8f, c.y + h * 0.5f), ImVec2(c.x + h * 0.5f, c.y + h * 0.5f), cl, t * 1.6f);
        break;
    case Icon::Download:
        dl->AddLine(ImVec2(c.x, c.y - h * 0.75f), ImVec2(c.x, c.y + h * 0.25f), cl, t);
        polyline(dl, c, h, {{-0.38f, -0.1f}, {0.0f, 0.28f}, {0.38f, -0.1f}}, cl, t);
        polyline(dl, c, h, {{-0.72f, 0.3f}, {-0.72f, 0.72f}, {0.72f, 0.72f}, {0.72f, 0.3f}}, cl, t);
        break;
    case Icon::Link:
        dl->AddRect(ImVec2(c.x - h * 0.82f, c.y - h * 0.3f), ImVec2(c.x + h * 0.08f, c.y + h * 0.3f), cl, h * 0.3f, 0, t);
        dl->AddRect(ImVec2(c.x - h * 0.08f, c.y - h * 0.3f), ImVec2(c.x + h * 0.82f, c.y + h * 0.3f), cl, h * 0.3f, 0, t);
        break;
    }
}

void draw_logo(ImDrawList* dl, ImVec2 p, float s) {
    // Заокруглений квадрат із діагональним градієнтом: акцент → холодніший відтінок
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), IM_COL32_WHITE, s * 0.26f);
    const ImU32 c0 = blend(kAccent, IM_COL32(255, 255, 255, 255), 0.12f);
    const ImU32 c1 = blend(kAccent, IM_COL32(24, 180, 230, 255), 0.55f);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, p, ImVec2(p.x + s, p.y + s), c0, c1);
    // Кадр плівки зі стрілкою відтворення
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    const float fw = s * 0.6f, fh = s * 0.46f;
    const float t = std::max(1.0f, s * 0.07f);
    dl->AddRect(ImVec2(c.x - fw * 0.5f, c.y - fh * 0.5f), ImVec2(c.x + fw * 0.5f, c.y + fh * 0.5f), IM_COL32(255, 255, 255, 235),
                s * 0.08f, 0, t);
    const float tr = fh * 0.3f;
    dl->AddTriangleFilled(ImVec2(c.x - tr * 0.7f, c.y - tr), ImVec2(c.x + tr * 1.05f, c.y), ImVec2(c.x - tr * 0.7f, c.y + tr),
                          IM_COL32_WHITE);
    // Дві «перфорації» над і під кадром
    for (float dx : {-0.18f, 0.18f}) {
        dl->AddCircleFilled(ImVec2(c.x + dx * s, c.y - fh * 0.5f - s * 0.09f), s * 0.035f, IM_COL32(255, 255, 255, 200));
        dl->AddCircleFilled(ImVec2(c.x + dx * s, c.y + fh * 0.5f + s * 0.09f), s * 0.035f, IM_COL32(255, 255, 255, 200));
    }
}

// ================================== Кнопки ========================================
bool icon_button(const char* id, Icon icon, const char* tooltip, bool active, float size) {
    const float sz = size > 0 ? size : ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) dl->AddRectFilled(p, p + ImVec2(sz, sz), col(kAccentSoft), 6 * u());
    else if (hovered || held) dl->AddRectFilled(p, p + ImVec2(sz, sz), col(held ? blend(kRaised, kText, 0.1f) : kRaised), 6 * u());
    const ImU32 c = active ? kAccentText : hovered ? kText : kTextDim;
    draw_icon(dl, icon, ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), std::min(sz * 0.6f, ImGui::GetFontSize() * 1.05f), col(c));
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool letter_toggle(const char* id, const char* letter, bool on, ImU32 on_col, const char* tooltip) {
    const float sz = std::round(ImGui::GetFontSize() * 1.2f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 4 * u();
    if (on) dl->AddRectFilled(p, p + ImVec2(sz, sz), col(on_col), r);
    else dl->AddRect(p, p + ImVec2(sz, sz), col(hovered ? kTextDim : kPanelLine), r, 0, 1.0f);
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 0.78f);
    const ImVec2 ts = ImGui::CalcTextSize(letter);
    dl->AddText(ImVec2(p.x + (sz - ts.x) * 0.5f, p.y + (sz - ts.y) * 0.5f),
                col(on ? IM_COL32(18, 18, 22, 255) : hovered ? kText : kTextDim), letter);
    ImGui::PopFont();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool action_button(const char* label, Kind kind, float width, Icon icon) {
    const bool strong = kind == Kind::Cta || kind == Kind::Positive;
    if (strong) ImGui::PushFont(bold_font(), 0.0f);
    const float h = ImGui::GetFrameHeight();
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float icon_w = icon != Icon::None ? h * 0.72f : 0.0f;
    const float w = width > 0 ? width : std::round(ts.x + icon_w + h * 1.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    const bool disabled = disabled_now();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 7 * u();
    ImU32 fill = 0, border = 0, text = kText;
    switch (kind) {
    case Kind::Cta:
        fill = held ? blend(kAccent, kText, 0.12f) : hov ? kAccentHover : kAccent;
        text = kOnAccent;
        break;
    case Kind::Positive:
        fill = held ? blend(kGreen, IM_COL32_WHITE, 0.2f) : hov ? blend(kGreen, IM_COL32_WHITE, 0.1f) : kGreen;
        text = IM_COL32_WHITE;
        break;
    case Kind::Secondary:
        fill = held ? blend(kRaised, kText, 0.12f) : hov ? blend(kRaised, kText, 0.06f) : kRaised;
        border = kPanelLine;
        break;
    case Kind::Negative:
        fill = alpha(kRed, held ? 90 : hov ? 60 : 34);
        text = theme_is_light() ? kRed : blend(kRed, IM_COL32_WHITE, 0.35f);
        break;
    case Kind::Ghost:
        fill = held ? blend(kRaised, kText, 0.08f) : hov ? kRaised : 0;
        text = hov ? kText : kTextDim;
        break;
    }
    if (disabled) {   // вимкнена — без кольору, бліда
        fill = strong ? kRaised : kind == Kind::Ghost ? 0 : kRaised;
        border = strong ? 0 : border;
        text = kTextFaint;
    }
    if (fill) dl->AddRectFilled(p, p + ImVec2(w, h), disabled ? fill : col(fill), r);
    if (border) dl->AddRect(p, p + ImVec2(w, h), disabled ? border : col(border), r, 0, 1.0f);
    if (!disabled) text = col(text);
    const float x = p.x + std::round((w - ts.x - icon_w) * 0.5f);
    if (icon != Icon::None) draw_icon(dl, icon, ImVec2(x + icon_w * 0.38f, p.y + h * 0.5f), h * 0.46f, text);
    dl->AddText(ImVec2(x + icon_w, p.y + std::round((h - ts.y) * 0.5f)), text, label, end);
    if (strong) ImGui::PopFont();
    return pressed;
}

float action_width(const char* label, Kind kind, Icon icon) {
    const bool strong = kind == Kind::Cta || kind == Kind::Positive;
    if (strong) ImGui::PushFont(bold_font(), 0.0f);
    const float h = ImGui::GetFrameHeight();
    const float w = std::round(ImGui::CalcTextSize(label, nullptr, true).x + (icon != Icon::None ? h * 0.72f : 0.0f) + h * 1.0f);
    if (strong) ImGui::PopFont();
    return w;
}

// ================================== Картки =========================================
bool card_begin(const char* title, const char* subtitle, Icon icon, bool collapsible, bool default_open) {
    const float fs = ImGui::GetFontSize();
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID open_id = ImGui::GetID(title);
    bool open = !collapsible || store->GetBool(open_id, default_open);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
    ImGui::PushStyleColor(ImGuiCol_Border, kPanelLine);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10 * u());
    ImGui::BeginChild(title, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar);
    const char* end = ImGui::FindRenderedTextEnd(title);
    if (end != title) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float line = std::round(fs * 1.5f);
        const float text_x = icon != Icon::None ? std::round(fs * 1.5f) + std::round(fs * 0.6f) : 0.0f;
        const float wrap = std::max(fs * 6, w - text_x - fs * 1.6f);
        const float sub_h = subtitle && *subtitle ? ImGui::CalcTextSize(subtitle, nullptr, false, wrap).y : 0.0f;
        const float h = sub_h > 0 ? line + sub_h + std::round(fs * 0.2f) : line;
        ImGui::PushID("##cardhead");
        if (ImGui::InvisibleButton("##toggle", ImVec2(w, h)) && collapsible) {
            open = !open;
            store->SetBool(open_id, open);
        }
        ImGui::PopID();
        const bool hov = collapsible && ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = p.x;
        if (icon != Icon::None) {
            const float box = std::round(fs * 1.5f);
            dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + box, p.y + box), col(kAccentSoft), 6 * u());
            draw_icon(dl, icon, ImVec2(x + box * 0.5f, p.y + box * 0.5f), fs * 0.95f, col(kAccentText));
            x += box + std::round(fs * 0.6f);
        }
        ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 1.05f);
        dl->AddText(ImVec2(x, p.y + std::round((line - ImGui::GetFontSize()) * 0.5f)), col(kText), title, end);
        ImGui::PopFont();
        if (sub_h > 0)
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, p.y + line), col(kTextDim), subtitle, nullptr, wrap);
        if (collapsible)
            draw_icon(dl, open ? Icon::ChevronUp : Icon::ChevronDown, ImVec2(p.x + w - fs * 0.6f, p.y + line * 0.5f), fs * 0.8f,
                      col(hov ? kText : kTextFaint));
        if (open) ImGui::Dummy(ImVec2(0, std::round(fs * 0.15f)));
    }
    return open;
}

void card_end() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, std::round(ImGui::GetStyle().ItemSpacing.y * 0.4f)));
}

void page_header(const char* title, const char* subtitle) {
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle && *subtitle) {
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(subtitle);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, std::round(ImGui::GetFontSize() * 0.3f)));
}

void subheading(const char* text) {
    ImGui::Dummy(ImVec2(0, std::round(ImGui::GetFontSize() * 0.15f)));
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 0.86f);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextFaint);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// ============================= Підписи, галочки, вимикачі ===========================
void label(const char* text, float width) {
    const float fs = ImGui::GetFontSize();
    const char* end = ImGui::FindRenderedTextEnd(text);
    const float tw = ImGui::CalcTextSize(text, end).x;
    if (width <= 0) {
        // Ширина колонки підписів — за найдовшим підписом у цьому вікні (росте, поки не сягне 45%)
        ImGuiStorage* st = ImGui::GetStateStorage();
        const ImGuiID key = ImGui::GetID("##label_w");
        float w = st->GetFloat(key, fs * 8.0f);
        const float need = std::min(tw + fs * 1.2f, std::max(fs * 8.0f, ImGui::GetContentRegionAvail().x * 0.45f));
        if (need > w) {
            w = need;
            st->SetFloat(key, w);
        }
        width = w;
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::Dummy(ImVec2(width, h));
    const bool cut = tw > width - fs * 0.5f;
    if (cut && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%.*s", static_cast<int>(end - text), text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, ImVec2(p.x + width - fs * 0.5f, p.y + h), true);
    dl->AddText(ImVec2(p.x, p.y + std::round((h - fs) * 0.5f)), ImGui::GetColorU32(kTextDim), text, end);
    dl->PopClipRect();
    ImGui::SameLine(0, 0);
}

float field_width(float max_em) {
    const float fs = ImGui::GetFontSize();
    float w = ImGui::GetContentRegionAvail().x - fs * 1.7f;
    if (max_em > 0) w = std::min(w, max_em * fs);
    return std::max(w, fs * 4.0f);
}

void help_marker(const char* text) {
    ImGui::SameLine();
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::Dummy(ImVec2(fs, h));
    const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + fs * 0.5f, p.y + h * 0.5f);
    const ImU32 cl = ImGui::GetColorU32(hov ? kText : kTextFaint, 1.0f);
    dl->AddCircle(c, fs * 0.4f, cl, 0, std::max(1.0f, 1.2f * u()));
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 0.7f);
    const ImVec2 ts = ImGui::CalcTextSize("?");
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), cl, "?");
    ImGui::PopFont();
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void same_line_if_fits(const char* next_label) {
    const float w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(next_label, nullptr, true).x;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
}

void info_row(const char* name, const std::string& value, ImU32 value_col) {
    // Щільніше за рядки форми: висота рядка тексту, а не поля вводу
    const float fs = ImGui::GetFontSize();
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID("##info_w");
    const char* end = ImGui::FindRenderedTextEnd(name);
    float w = st->GetFloat(key, fs * 7.0f);
    const float need = std::min(ImGui::CalcTextSize(name, end).x + fs * 1.2f, std::max(fs * 7.0f, ImGui::GetContentRegionAvail().x * 0.42f));
    if (need > w) {
        w = need;
        st->SetFloat(key, w);
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, ImVec2(p.x + w - fs * 0.5f, p.y + ImGui::GetTextLineHeight()), true);
    dl->AddText(p, ImGui::GetColorU32(kTextDim), name, end);
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p.x + w, p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, value_col ? value_col : kText);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.empty() ? "—" : value.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void badge(const char* text, ImU32 c) {
    const float fs = ImGui::GetFontSize();
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 0.8f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float h = std::round(fs * 1.25f), w = std::max(h, ts.x + fs * 0.8f);
    const float fh = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, fh));
    const ImVec2 b0(p.x, p.y + std::round((fh - h) * 0.5f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(b0, ImVec2(b0.x + w, b0.y + h), col(alpha(c, 40)), h * 0.5f);
    dl->AddText(ImVec2(b0.x + std::round((w - ts.x) * 0.5f), b0.y + std::round((h - ts.y) * 0.5f)), col(c), text);
    ImGui::PopFont();
}

bool checkbox(const char* label, bool* v) {
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    const float box = std::round(fs * 1.0f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(box + (ts.x > 0 ? sp + ts.x : 0), h));
    if (pressed) *v = !*v;
    const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b0(p.x, std::round(p.y + (h - box) * 0.5f)), b1(b0.x + box, b0.y + box);
    const float r = 4 * u();
    if (*v) {
        dl->AddRectFilled(b0, b1, col(held ? blend(kAccent, kText, 0.15f) : hov ? kAccentHover : kAccent), r);
        draw_icon(dl, Icon::Check, ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f), box * 0.78f, col(kOnAccent));
    } else {
        dl->AddRectFilled(b0, b1, col(kField), r);
        dl->AddRect(b0, b1, col(hov ? kTextDim : kTextFaint), r, 0, std::max(1.0f, 1.4f * u()));
    }
    if (ts.x > 0) dl->AddText(ImVec2(b1.x + sp, p.y + std::round((h - fs) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), label, end);
    return pressed;
}

// Вимикач: доріжка-капсула з кружечком, що плавно переїжджає; увімкнений — акцентний
bool toggle(const char* label, bool* v) {
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    const float th = std::round(fs * 1.12f), tw = std::round(th * 1.8f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float sp = ImGui::GetStyle().ItemInnerSpacing.x + 2 * u();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(tw + (ts.x > 0 ? sp + ts.x : 0), h));
    if (pressed) *v = !*v;
    const bool hov = ImGui::IsItemHovered();
    const float k = animate(ImGui::GetItemID(), *v ? 1.0f : 0.0f, 18.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 t0(p.x, std::round(p.y + (h - th) * 0.5f)), t1(t0.x + tw, t0.y + th);
    const ImU32 off = hov ? blend(kPanelLine, kText, 0.15f) : blend(kPanelLine, kTextFaint, 0.25f);
    const ImU32 on = hov ? kAccentHover : kAccent;
    dl->AddRectFilled(t0, t1, col(blend(off, on, k)), th * 0.5f);
    const float r = th * 0.5f - std::max(2.0f, 2.5f * u());
    const float cx = t0.x + th * 0.5f + (tw - th) * k;
    dl->AddCircleFilled(ImVec2(cx, t0.y + th * 0.5f), r, col(IM_COL32_WHITE));
    if (ts.x > 0) dl->AddText(ImVec2(t1.x + sp, p.y + std::round((h - fs) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), label, end);
    return pressed;
}

bool radio(const char* label, bool active) {
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    const float d = std::round(fs * 1.0f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(d + (ts.x > 0 ? sp + ts.x : 0), h));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + d * 0.5f, p.y + h * 0.5f);
    const float r = d * 0.5f;
    if (active) {
        dl->AddCircleFilled(c, r, col(hov ? kAccentHover : kAccent));
        dl->AddCircleFilled(c, r * 0.4f, col(kOnAccent));
    } else {
        dl->AddCircleFilled(c, r, col(kField));
        dl->AddCircle(c, r - 0.7f * u(), col(hov ? kTextDim : kTextFaint), 0, std::max(1.0f, 1.4f * u()));
    }
    if (ts.x > 0) dl->AddText(ImVec2(p.x + d + sp, p.y + std::round((h - fs) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), label, end);
    return pressed;
}

// Перемикач-«капсула» з кількох варіантів: виділення плавно переїжджає до вибраного
bool segmented(const char* id, int* current, std::initializer_list<const char*> labels, float width) {
    const int n = static_cast<int>(labels.size());
    if (n == 0) return false;
    ImGui::PushID(id);
    const float h = ImGui::GetFrameHeight();
    ImGui::PushFont(bold_font(), 0.0f);
    float seg = 0;
    for (const char* l : labels) seg = std::max(seg, ImGui::CalcTextSize(l, nullptr, true).x + h * 1.3f);
    ImGui::PopFont();
    const float w = width > 0 ? width : seg * n;
    seg = w / n;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##seg", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    const int hover_i = hov ? std::clamp(static_cast<int>((ImGui::GetIO().MousePos.x - p.x) / seg), 0, n - 1) : -1;
    bool changed = false;
    if (ImGui::IsItemClicked() && hover_i >= 0 && hover_i != *current) {
        *current = hover_i;
        changed = true;
    }
    const float pos = animate(ImGui::GetID("##anim"), static_cast<float>(std::clamp(*current, 0, n - 1)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 8 * u(), inset = std::round(3.0f * u());
    dl->AddRectFilled(p, p + ImVec2(w, h), col(kField), r);
    dl->AddRect(p, p + ImVec2(w, h), col(kPanelLine), r, 0, 1.0f);
    const ImVec2 t0(p.x + pos * seg + inset, p.y + inset);
    dl->AddRectFilled(t0, ImVec2(t0.x + seg - inset * 2, p.y + h - inset), col(kAccent), r - inset * 0.5f);
    int i = 0;
    for (const char* l : labels) {
        const bool sel = i == *current;
        if (sel) ImGui::PushFont(bold_font(), 0.0f);
        const char* end = ImGui::FindRenderedTextEnd(l);
        const ImVec2 ts = ImGui::CalcTextSize(l, end);
        const ImU32 tc = sel ? kOnAccent : i == hover_i ? kText : kTextDim;
        dl->PushClipRect(ImVec2(p.x + i * seg, p.y), ImVec2(p.x + (i + 1) * seg, p.y + h), true);
        dl->AddText(ImVec2(p.x + i * seg + std::max(inset * 2, std::round((seg - ts.x) * 0.5f)), p.y + std::round((h - ts.y) * 0.5f)),
                    col(tc), l, end);
        dl->PopClipRect();
        if (sel) ImGui::PopFont();
        ++i;
    }
    ImGui::PopID();
    return changed;
}

bool begin_combo(const char* id, const char* preview, ImGuiComboFlags flags) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    const bool open = ImGui::BeginCombo(id, preview, flags);
    ImGui::PopStyleColor(2);
    return open;
}

bool swatch(const char* id, ImU32 c, bool selected, const char* tooltip) {
    const float fs = ImGui::GetFontSize();
    const float d = std::round(fs * 1.5f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(d, d));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ctr(p.x + d * 0.5f, p.y + d * 0.5f);
    if (selected || hov) dl->AddCircle(ctr, d * 0.5f - 0.5f, col(selected ? kText : kTextFaint), 0, std::max(1.5f, 2.0f * u()));
    dl->AddCircleFilled(ctr, d * 0.5f - std::round(4 * u()), col(c));
    if (tooltip && hov) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

// ============================ Значення, повзунки, смуги ============================
bool hot_float(const char* id, float* v, float speed, float min, float max, const char* fmt) {
    return hot_value<float>(id, v, speed, min, max, fmt);
}
bool hot_int(const char* id, int* v, float speed, int min, int max, const char* fmt) {
    return hot_value<int>(id, v, speed, min, max, fmt);
}
bool slider_float(const char* id, float* v, float min, float max, const char* fmt, float width) {
    return slider_impl<float>(id, v, min, max, fmt, width);
}
bool slider_int(const char* id, int* v, int min, int max, const char* fmt, float width) {
    return slider_impl<int>(id, v, min, max, fmt, width);
}

void meter(float fraction, ImVec2 size, ImU32 c) {
    if (!c) c = kAccent;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f;
    dl->AddRectFilled(p, p + size, col(alpha(kText, 26)), r);
    if (fraction >= 0) {
        const float w = size.x * std::clamp(fraction, 0.0f, 1.0f);
        if (w > 0.5f) dl->AddRectFilled(p, ImVec2(p.x + std::max(w, size.y), p.y + size.y), col(c), r);
    } else {
        // Невизначений прогрес: відрізок біжить зліва направо
        const float seg = size.x * 0.3f;
        const float t = static_cast<float>(std::fmod(ImGui::GetTime() * 0.7, 1.0));
        const float x = p.x - seg + (size.x + seg) * t;
        dl->PushClipRect(p, p + size, true);
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + seg, p.y + size.y), col(c), r);
        dl->PopClipRect();
    }
}

bool search_input(const char* id, const char* hint, std::string* text, float width) {
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(st.FramePadding.x + fs * 1.15f, st.FramePadding.y));
    ImGui::SetNextItemWidth(width);
    const bool r = ImGui::InputTextWithHint(id, hint, text);
    ImGui::PopStyleVar();
    draw_icon(ImGui::GetWindowDrawList(), Icon::Search, ImVec2(p.x + fs * 0.85f, p.y + ImGui::GetFrameHeight() * 0.5f),
              fs * 0.78f, col(kTextFaint));
    return r;
}

// ================================ Бічна навігація ==================================
bool nav_item(const char* label, Icon icon, bool active, bool collapsed, const char* badge_text) {
    const float fs = ImGui::GetFontSize();
    const float h = std::round(fs * 2.3f);
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 8 * u();
    if (active) {
        dl->AddRectFilled(p, p + ImVec2(w, h), col(kAccentSoft), r);
        dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.26f), ImVec2(p.x + std::round(3 * u()), p.y + h * 0.74f), col(kAccent), 2 * u());
    } else if (hov) {
        dl->AddRectFilled(p, p + ImVec2(w, h), col(kRaised), r);
    }
    const ImU32 ic = active ? kAccentText : hov ? kText : kTextDim;
    const float icx = collapsed ? p.x + w * 0.5f : p.x + std::round(fs * 1.35f);
    draw_icon(dl, icon, ImVec2(icx, p.y + h * 0.5f), fs * 1.12f, col(ic));
    const char* end = ImGui::FindRenderedTextEnd(label);
    if (!collapsed) {
        const float tx = p.x + std::round(fs * 2.7f);
        float right = p.x + w - fs * 0.6f;
        if (badge_text && *badge_text) {
            ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 0.78f);
            const ImVec2 bs = ImGui::CalcTextSize(badge_text);
            const float bh = std::round(fs * 1.2f), bw = std::max(bh, bs.x + fs * 0.7f);
            const ImVec2 b0(right - bw, p.y + std::round((h - bh) * 0.5f));
            dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), col(active ? kAccent : kRaised), bh * 0.5f);
            dl->AddText(ImVec2(b0.x + std::round((bw - bs.x) * 0.5f), b0.y + std::round((bh - bs.y) * 0.5f)),
                        col(active ? kOnAccent : kTextDim), badge_text);
            ImGui::PopFont();
            right = b0.x - fs * 0.4f;
        }
        if (active) ImGui::PushFont(bold_font(), 0.0f);
        dl->PushClipRect(ImVec2(tx, p.y), ImVec2(right, p.y + h), true);
        dl->AddText(ImVec2(tx, p.y + std::round((h - fs) * 0.5f)), col(active ? kText : hov ? kText : kTextDim), label, end);
        dl->PopClipRect();
        if (active) ImGui::PopFont();
    } else {
        if (badge_text && *badge_text) dl->AddCircleFilled(ImVec2(p.x + w * 0.5f + fs * 0.62f, p.y + h * 0.28f), fs * 0.22f, col(kAccent));
        if (hov) ImGui::SetTooltip("%.*s", static_cast<int>(end - label), label);
    }
    return pressed;
}

// ==================================== Панелі ======================================
void begin_panel(const char* id, ImVec2 pos, ImVec2 size, const std::vector<std::string>& tabs, int* current,
                 const char* menu_id, float footer_h, ImGuiWindowFlags content_flags, std::initializer_list<PanelButton> buttons) {
    const float fs = ImGui::GetFontSize();
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = 10 * u();
    dl->AddRectFilled(pos, pos + size, col(kPanel), rounding);
    dl->AddRect(pos, pos + size, col(kPanelLine), rounding, 0, 1.0f);
    const float header_h = tabs.empty() ? 0.0f : std::round(fs * 2.3f);

    // Вкладки: активна — яскравим напівжирним текстом з акцентною рискою знизу, інші — тьмяні
    float x = pos.x + std::round(14 * u());
    const float menu_w = menu_id ? header_h : 0.0f;
    if (header_h > 0) {
        dl->PushClipRect(pos, ImVec2(pos.x + size.x - menu_w, pos.y + header_h), true);
        for (size_t i = 0; i < tabs.size(); ++i) {
            const char* lbl = tabs[i].c_str();
            const char* end = ImGui::FindRenderedTextEnd(lbl);
            const bool sel = !current || *current == static_cast<int>(i);
            if (sel) ImGui::PushFont(bold_font(), 0.0f);
            const ImVec2 ts = ImGui::CalcTextSize(lbl, end);
            ImGui::SetCursorScreenPos(ImVec2(x - 7 * u(), pos.y));
            ImGui::PushID(static_cast<int>(i));
            const bool clicked = ImGui::InvisibleButton("##tab", ImVec2(ts.x + 14 * u(), header_h - 1));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (clicked && current) *current = static_cast<int>(i);
            dl->AddText(ImVec2(x, pos.y + std::round((header_h - fs) * 0.5f)), col(sel ? kText : hov ? kText : kTextDim), lbl, end);
            if (sel && tabs.size() > 1)
                dl->AddRectFilled(ImVec2(x, pos.y + header_h - 3 * u()), ImVec2(x + ts.x, pos.y + header_h - 1), col(kAccent), 2 * u());
            if (sel) ImGui::PopFont();
            x += ts.x + std::round(24 * u());
        }
        dl->PopClipRect();
        dl->AddLine(ImVec2(pos.x + 1, pos.y + header_h - 1), ImVec2(pos.x + size.x - 1, pos.y + header_h - 1), col(kPanelLine));
    }
    float right = pos.x + size.x - std::round(6 * u());
    if (menu_id) {
        const float bs = header_h - 10 * u();
        right -= bs;
        ImGui::SetCursorScreenPos(ImVec2(right, pos.y + 5 * u()));
        if (icon_button("##panelmenu", Icon::Menu, nullptr, false, bs)) ImGui::OpenPopupEx(ImHashStr(menu_id));
        right -= 2 * u();
    }
    // Кнопки заголовка — справа наліво, перед меню
    if (header_h > 0)
        for (const PanelButton& b : buttons) {
            const float bs = header_h - 10 * u();
            right -= bs;
            ImGui::SetCursorScreenPos(ImVec2(right, pos.y + 5 * u()));
            const bool pressed = icon_button(b.id, b.icon, b.tooltip, b.active, bs);
            if (b.pressed) *b.pressed = pressed;
            right -= 2 * u();
        }
    PanelFrame f;
    f.pos = pos;
    f.size = size;
    f.header_h = header_h;
    f.footer_h = footer_h;
    f.in_content = true;
    g_panels.push_back(f);
    // Вміст — на 1 px усередині, щоб не перекривати рамку
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 1, pos.y + header_h + 1));
    ImGui::BeginChild("##content", ImVec2(size.x - 2, std::max(1.0f, size.y - header_h - footer_h - 2)),
                      ImGuiChildFlags_AlwaysUseWindowPadding, content_flags);
}

void panel_footer() {
    if (g_panels.empty() || !g_panels.back().in_content) return;
    PanelFrame& f = g_panels.back();
    ImGui::EndChild();
    f.in_content = false;
    const float y = f.pos.y + f.size.y - f.footer_h;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(f.pos.x + 1, y), ImVec2(f.pos.x + f.size.x - 1, y), col(kPanelLine));
    ImGui::SetCursorScreenPos(ImVec2(f.pos.x + ImGui::GetStyle().WindowPadding.x, y + ImGui::GetStyle().WindowPadding.y));
}

void end_panel() {
    if (g_panels.empty()) return;
    const PanelFrame f = g_panels.back();
    g_panels.pop_back();
    if (f.in_content) ImGui::EndChild();
    ImGui::EndChild();
}

bool begin_panel_menu(const char* menu_id) {
    return ImGui::BeginPopupEx(ImHashStr(menu_id),
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
}

void splitter(const char* id, bool vertical, ImVec2 pos, ImVec2 size, float* value, float min_v, float max_v) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    const bool active = ImGui::IsItemActive(), hov = ImGui::IsItemHovered();
    if (hov || active) ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (active) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        *value = std::clamp(*value + (vertical ? d.x : d.y), min_v, std::max(min_v, max_v));
    }
    if (hov || active) {
        const ImVec2 c(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 cl = col(alpha(kAccent, active ? 220 : 130));
        if (vertical) dl->AddLine(ImVec2(c.x, pos.y + 6 * u()), ImVec2(c.x, pos.y + size.y - 6 * u()), cl, 2 * u());
        else dl->AddLine(ImVec2(pos.x + 6 * u(), c.y), ImVec2(pos.x + size.x - 6 * u(), c.y), cl, 2 * u());
    }
}

std::string timecode(double seconds, double fps) {
    if (!(seconds >= 0) || !std::isfinite(seconds)) seconds = 0;
    if (!(fps > 0)) fps = 60;
    const int64_t whole = static_cast<int64_t>(std::floor(seconds + 1e-9));
    const int64_t nominal = std::max<int64_t>(1, std::llround(fps));
    const int64_t frame = std::clamp<int64_t>(static_cast<int64_t>(std::floor((seconds - static_cast<double>(whole)) * fps + 1e-6)),
                                              0, nominal - 1);
    return std::format("{:02}:{:02}:{:02}:{:02}", whole / 3600, (whole / 60) % 60, whole % 60, frame);
}

std::string format_date_time(int64_t unix_time) {
    const std::time_t t = static_cast<std::time_t>(unix_time);
    char buf[32] = {};
    if (const std::tm* tm = std::localtime(&t)) std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tm);
    return buf;
}

} // namespace gmdr::gui::ui
