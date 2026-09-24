// =============================================================================
//  ui_widgets.cpp — віджети в стилі Adobe (Spectrum, темна тема Premiere Pro /
//  Media Encoder): тема, іконки, панелі з вкладками, роздільники між панелями,
//  «гарячі» значення, повзунки, кнопки-«пігулки».
//
//  Усі розміри задано в «пікселях макета» для шрифту 15 px і множаться на
//  u() = розмір шрифту / 15 — тож інтерфейс однаково виглядає на будь-якому DPI.
//  Кольори для малювання проходять через ImGui::GetColorU32 — тоді всередині
//  BeginDisabled() вони самі стають блідішими.
// =============================================================================
#include "app_ui.hpp"

#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <initializer_list>
#include <type_traits>

namespace gmdr::gui::ui {

namespace {
ImFont* g_regular = nullptr;
ImFont* g_bold = nullptr;

float u() { return ImGui::GetFontSize() / 15.0f; }
ImU32 col(ImU32 c) { return ImGui::GetColorU32(c); }
ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

// Стан відкритої панелі (begin_panel … end_panel)
struct PanelFrame {
    ImVec2 pos, size;
    float  footer_h = 0;
    bool   in_content = false;
    bool   focused = false;
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
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 10));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 16));
        ImGui::PushStyleColor(ImGuiCol_Text, kBlueText);
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
            // Пунктирне підкреслення, як у Premiere
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            const float y = b.y - st.FramePadding.y + 1.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (float x = a.x + st.FramePadding.x; x < b.x - st.FramePadding.x; x += 3.0f * u())
                dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + 1.5f * u(), b.x - st.FramePadding.x), y), col(kBlueText), 1.0f);
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
    const float r = h * 0.24f;
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
    const float th = std::max(2.0f, 2.0f * u());
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col(IM_COL32(84, 84, 84, 255)), th);
    dl->AddLine(ImVec2(x0, cy), ImVec2(hx, cy), col(IM_COL32(185, 185, 185, 255)), th);
    dl->AddCircleFilled(ImVec2(hx, cy), r, col(kPanel));
    dl->AddCircle(ImVec2(hx, cy), r, col(active ? kBlueText : hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255)),
                  0, std::max(1.5f, 2.0f * u()));
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    const float speed = static_cast<float>(max - min) / std::max(50.0f, track_w);
    changed |= hot_value<T>("##val", v, speed, min, max, fmt);
    ImGui::PopID();
    return changed;
}
} // namespace

// ================================ Тема і шрифти =================================
void set_fonts(ImFont* regular, ImFont* bold) {
    g_regular = regular;
    g_bold = bold;
}

ImFont* bold_font() { return g_bold ? g_bold : g_regular; }

void apply_theme(float dpi) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(10, 8);
    s.WindowRounding = 0;
    s.WindowBorderSize = 0;
    s.ChildRounding = 0;
    s.ChildBorderSize = 0;
    s.PopupRounding = 6;
    s.PopupBorderSize = 1;
    s.FramePadding = ImVec2(7, 4);
    s.FrameRounding = 4;
    s.FrameBorderSize = 1;
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 3);
    s.IndentSpacing = 18;
    s.ScrollbarSize = 11;
    s.ScrollbarRounding = 6;
    s.ScrollbarPadding = 2;
    s.GrabMinSize = 10;
    s.GrabRounding = 6;
    s.TabRounding = 0;
    s.TabBorderSize = 0;
    s.TabBarBorderSize = 1;
    s.TabBarOverlineSize = 0;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(0, 4);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    auto rgb = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = rgb(226, 226, 226);
    c[ImGuiCol_TextDisabled] = rgb(128, 128, 128);
    c[ImGuiCol_WindowBg] = rgb(35, 35, 35);
    c[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = rgb(44, 44, 44);
    c[ImGuiCol_Border] = rgb(64, 64, 64);
    c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = rgb(27, 27, 27);
    c[ImGuiCol_FrameBgHovered] = rgb(32, 32, 32);
    c[ImGuiCol_FrameBgActive] = rgb(24, 24, 24);
    c[ImGuiCol_TitleBg] = rgb(30, 30, 30);
    c[ImGuiCol_TitleBgActive] = rgb(30, 30, 30);
    c[ImGuiCol_TitleBgCollapsed] = rgb(30, 30, 30);
    c[ImGuiCol_MenuBarBg] = rgb(28, 28, 28);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(78, 78, 78);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(98, 98, 98);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(118, 118, 118);
    c[ImGuiCol_CheckMark] = rgb(255, 255, 255);
    c[ImGuiCol_SliderGrab] = rgb(200, 200, 200);
    c[ImGuiCol_SliderGrabActive] = rgb(255, 255, 255);
    c[ImGuiCol_Button] = rgb(58, 58, 58);
    c[ImGuiCol_ButtonHovered] = rgb(72, 72, 72);
    c[ImGuiCol_ButtonActive] = rgb(86, 86, 86);
    c[ImGuiCol_Header] = rgb(38, 128, 235, 0.30f);
    c[ImGuiCol_HeaderHovered] = rgb(255, 255, 255, 0.07f);
    c[ImGuiCol_HeaderActive] = rgb(38, 128, 235, 0.45f);
    c[ImGuiCol_Separator] = rgb(56, 56, 56);
    c[ImGuiCol_SeparatorHovered] = rgb(38, 128, 235);
    c[ImGuiCol_SeparatorActive] = rgb(55, 142, 240);
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = rgb(38, 128, 235, 0.6f);
    c[ImGuiCol_ResizeGripActive] = rgb(38, 128, 235);
    c[ImGuiCol_InputTextCursor] = rgb(226, 226, 226);
    c[ImGuiCol_Tab] = rgb(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = rgb(255, 255, 255, 0.07f);
    c[ImGuiCol_TabSelected] = rgb(0, 0, 0, 0);
    c[ImGuiCol_TabSelectedOverline] = rgb(38, 128, 235);
    c[ImGuiCol_TabDimmed] = rgb(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = rgb(0, 0, 0, 0);
    c[ImGuiCol_PlotHistogram] = rgb(38, 128, 235);
    c[ImGuiCol_PlotHistogramHovered] = rgb(55, 142, 240);
    c[ImGuiCol_TableHeaderBg] = rgb(42, 42, 42);
    c[ImGuiCol_TableBorderStrong] = rgb(56, 56, 56);
    c[ImGuiCol_TableBorderLight] = rgb(46, 46, 46);
    c[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.025f);
    c[ImGuiCol_TextLink] = rgb(75, 156, 245);
    c[ImGuiCol_TextSelectedBg] = rgb(38, 128, 235, 0.45f);
    c[ImGuiCol_DragDropTarget] = rgb(38, 128, 235);
    c[ImGuiCol_NavCursor] = rgb(38, 128, 235);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);
    s.ScaleAllSizes(dpi);
    s.FontScaleDpi = dpi;
    s.FontSizeBase = 15.0f;
}

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
    }
}

// ================================== Кнопки ========================================
bool icon_button(const char* id, Icon icon, const char* tooltip, bool active, float size) {
    const float sz = size > 0 ? size : ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered || held) dl->AddRectFilled(p, p + ImVec2(sz, sz), col(IM_COL32(255, 255, 255, held ? 36 : 20)), 4 * u());
    const ImU32 c = active ? kBlueText : hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(196, 196, 196, 255);
    draw_icon(dl, icon, ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), std::min(sz * 0.62f, ImGui::GetFontSize() * 1.05f), col(c));
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
    const float r = 3 * u();
    if (on) dl->AddRectFilled(p, p + ImVec2(sz, sz), col(on_col), r);
    else dl->AddRect(p, p + ImVec2(sz, sz), col(hovered ? IM_COL32(170, 170, 170, 255) : IM_COL32(92, 92, 92, 255)), r, 0, 1.0f);
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 0.8f);
    const ImVec2 ts = ImGui::CalcTextSize(letter);
    dl->AddText(ImVec2(p.x + (sz - ts.x) * 0.5f, p.y + (sz - ts.y) * 0.5f),
                col(on ? IM_COL32(20, 20, 20, 255) : hovered ? kText : kTextDim), letter);
    ImGui::PopFont();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool pill_button(const char* label, Kind kind, float width, Icon icon) {
    const bool strong = kind == Kind::Cta || kind == Kind::Positive;
    if (strong) ImGui::PushFont(bold_font(), 0.0f);
    const float h = ImGui::GetFrameHeight();
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float icon_w = icon != Icon::None ? h * 0.72f : 0.0f;
    const float w = width > 0 ? width : std::round(ts.x + icon_w + h * 1.1f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    const bool disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = h * 0.5f;
    ImU32 fill = 0, border = 0, text = kText;
    switch (disabled ? Kind::Secondary : kind) {
    case Kind::Cta:
        fill = held ? kBlueText : hov ? kBlueHover : kBlue;
        text = IM_COL32_WHITE;
        break;
    case Kind::Positive:
        fill = held ? IM_COL32(64, 186, 146, 255) : hov ? IM_COL32(51, 171, 132, 255) : kGreen;
        text = IM_COL32_WHITE;
        break;
    case Kind::Secondary:
        border = hov ? IM_COL32(196, 196, 196, 255) : IM_COL32(116, 116, 116, 255);
        fill = held ? IM_COL32(255, 255, 255, 40) : hov ? IM_COL32(255, 255, 255, 18) : 0;
        break;
    case Kind::Negative:
        border = IM_COL32(236, 91, 98, 255);
        fill = held ? IM_COL32(236, 91, 98, 90) : hov ? IM_COL32(236, 91, 98, 46) : 0;
        text = IM_COL32(255, 170, 174, 255);
        break;
    }
    if (disabled) {   // вимкнена кнопка в Spectrum — сіра, без кольору
        fill = strong ? IM_COL32(64, 64, 64, 255) : 0;
        border = strong ? 0 : IM_COL32(80, 80, 80, 255);
        text = IM_COL32(120, 120, 120, 255);
    }
    if (fill) dl->AddRectFilled(p, p + ImVec2(w, h), disabled ? fill : col(fill), r);
    if (border) {
        const float bt = std::max(1.0f, 1.5f * u());
        dl->AddRect(p + ImVec2(bt * 0.5f, bt * 0.5f), p + ImVec2(w - bt * 0.5f, h - bt * 0.5f), disabled ? border : col(border), r, 0, bt);
    }
    if (!disabled) text = col(text);
    const float x = p.x + std::round((w - ts.x - icon_w) * 0.5f);
    if (icon != Icon::None) draw_icon(dl, icon, ImVec2(x + icon_w * 0.38f, p.y + h * 0.5f), h * 0.46f, text);
    dl->AddText(ImVec2(x + icon_w, p.y + std::round((h - ts.y) * 0.5f)), text, label, end);
    if (strong) ImGui::PopFont();
    return pressed;
}

float pill_width(const char* label, Kind kind, Icon icon) {
    const bool strong = kind == Kind::Cta || kind == Kind::Positive;
    if (strong) ImGui::PushFont(bold_font(), 0.0f);
    const float h = ImGui::GetFrameHeight();
    const float w = std::round(ImGui::CalcTextSize(label, nullptr, true).x + (icon != Icon::None ? h * 0.72f : 0.0f) + h * 1.1f);
    if (strong) ImGui::PopFont();
    return w;
}

// ============================= Секції, підписи, галочки ============================
bool section(const char* label, bool default_open) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(label);
    bool open = store->GetBool(id, default_open);
    const float h = std::round(ImGui::GetFrameHeight() * 1.05f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::InvisibleButton(label, ImVec2(w, h))) {
        open = !open;
        store->SetBool(id, open);
    }
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h), col(IM_COL32(255, 255, 255, hov ? 16 : 9)), 3 * u());
    draw_icon(dl, open ? Icon::ChevronDown : Icon::ChevronRight, ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.42f,
              col(hov ? kText : kTextDim));
    ImGui::PushFont(bold_font(), 0.0f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    dl->AddText(ImVec2(p.x + h * 0.95f, p.y + (h - ImGui::GetFontSize()) * 0.5f), col(kText), label, end);
    ImGui::PopFont();
    if (open) ImGui::Dummy(ImVec2(0, 1 * u()));
    return open;
}

void label(const char* text, float width) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(172, 172, 172, 255));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::SameLine(width > 0 ? width : ImGui::GetFontSize() * 11.0f);
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
    const ImU32 cl = ImGui::GetColorU32(hov ? IM_COL32(230, 230, 230, 255) : IM_COL32(128, 128, 128, 255), 1.0f);
    dl->AddCircle(c, fs * 0.4f, cl, 0, std::max(1.0f, 1.2f * u()));
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 0.72f);
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

// Галочка Spectrum: маленький квадрат (синій з білою галочкою, коли увімкнено) і підпис
bool checkbox(const char* label, bool* v) {
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    const float box = std::round(fs * 0.95f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(box + (ts.x > 0 ? sp + ts.x : 0), h));
    if (pressed) *v = !*v;
    const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b0(p.x, std::round(p.y + (h - box) * 0.5f)), b1(b0.x + box, b0.y + box);
    const float r = 2.5f * u();
    if (*v) {
        dl->AddRectFilled(b0, b1, col(held ? kBlueText : hov ? kBlueHover : kBlue), r);
        draw_icon(dl, Icon::Check, ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f), box * 0.8f, col(IM_COL32_WHITE));
    } else {
        dl->AddRectFilled(b0, b1, col(kField), r);
        dl->AddRect(b0, b1, col(hov ? IM_COL32(215, 215, 215, 255) : IM_COL32(142, 142, 142, 255)), r, 0, std::max(1.0f, 1.6f * u()));
    }
    if (ts.x > 0) dl->AddText(ImVec2(b1.x + sp, p.y + std::round((h - fs) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), label, end);
    return pressed;
}

// Перемикач Spectrum: кружечок; вибраний — товсте синє кільце з темною серединою
bool radio(const char* label, bool active) {
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    const float d = std::round(fs * 0.95f);
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
        dl->AddCircleFilled(c, r, col(hov ? kBlueHover : kBlue));
        dl->AddCircleFilled(c, r * 0.42f, col(kField));
    } else {
        dl->AddCircleFilled(c, r, col(kField));
        dl->AddCircle(c, r - 0.8f * u(), col(hov ? IM_COL32(215, 215, 215, 255) : IM_COL32(142, 142, 142, 255)), 0,
                      std::max(1.0f, 1.6f * u()));
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
    for (const char* l : labels) seg = std::max(seg, ImGui::CalcTextSize(l, nullptr, true).x + h * 1.4f);
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
    // Виділення їде до вибраного сегмента (позиція — у сховищі стану вікна)
    float* pos = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("##anim"), static_cast<float>(*current));
    const float target = static_cast<float>(std::clamp(*current, 0, n - 1));
    *pos += (target - *pos) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
    if (std::abs(target - *pos) < 0.002f) *pos = target;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = h * 0.5f, inset = std::round(2.0f * u());
    dl->AddRectFilled(p, p + ImVec2(w, h), col(kField), r);
    dl->AddRect(p, p + ImVec2(w, h), col(hov ? IM_COL32(96, 96, 96, 255) : kPanelLine), r, 0, 1.0f);
    const ImVec2 t0(p.x + *pos * seg + inset, p.y + inset);
    dl->AddRectFilled(t0, ImVec2(t0.x + seg - inset * 2, p.y + h - inset), col(kBlue), r - inset);
    int i = 0;
    for (const char* l : labels) {
        const bool sel = i == *current;
        if (sel) ImGui::PushFont(bold_font(), 0.0f);
        const char* end = ImGui::FindRenderedTextEnd(l);
        const ImVec2 ts = ImGui::CalcTextSize(l, end);
        const ImU32 tc = sel ? IM_COL32_WHITE : i == hover_i ? kText : kTextDim;
        dl->AddText(ImVec2(p.x + i * seg + std::round((seg - ts.x) * 0.5f), p.y + std::round((h - ts.y) * 0.5f)), col(tc), l, end);
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
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f;
    dl->AddRectFilled(p, p + size, col(IM_COL32(255, 255, 255, 28)), r);
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
              fs * 0.78f, col(kTextDim));
    return r;
}

// ==================================== Панелі ======================================
void begin_panel(const char* id, ImVec2 pos, ImVec2 size, const std::vector<std::string>& tabs, int* current,
                 const char* menu_id, float footer_h, ImGuiWindowFlags content_flags) {
    const float fs = ImGui::GetFontSize();
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = 4 * u();
    dl->AddRectFilled(pos, pos + size, kPanel, rounding);
    const float header_h = std::round(fs * 2.0f);
    dl->AddLine(ImVec2(pos.x, pos.y + header_h - 1), ImVec2(pos.x + size.x, pos.y + header_h - 1), kPanelLine);

    // Вкладки: активна — біла із синьою рискою знизу, інші — сірі
    float x = pos.x + std::round(12 * u());
    const float menu_w = menu_id ? header_h : 0.0f;
    dl->PushClipRect(pos, ImVec2(pos.x + size.x - menu_w, pos.y + header_h), true);
    for (size_t i = 0; i < tabs.size(); ++i) {
        const char* lbl = tabs[i].c_str();
        const char* end = ImGui::FindRenderedTextEnd(lbl);
        const ImVec2 ts = ImGui::CalcTextSize(lbl, end);
        ImGui::SetCursorScreenPos(ImVec2(x - 7 * u(), pos.y));
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = ImGui::InvisibleButton("##tab", ImVec2(ts.x + 14 * u(), header_h - 1));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (clicked && current) *current = static_cast<int>(i);
        const bool sel = current && *current == static_cast<int>(i);
        dl->AddText(ImVec2(x, pos.y + std::round((header_h - fs) * 0.5f)),
                    sel ? IM_COL32(242, 242, 242, 255) : hov ? IM_COL32(205, 205, 205, 255) : IM_COL32(146, 146, 146, 255), lbl, end);
        if (sel) dl->AddRectFilled(ImVec2(x, pos.y + header_h - 3 * u()), ImVec2(x + ts.x, pos.y + header_h - 1), kBlue);
        x += ts.x + std::round(22 * u());
    }
    dl->PopClipRect();
    if (menu_id) {
        const float bs = header_h - 8 * u();
        ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - bs - 6 * u(), pos.y + 4 * u()));
        if (icon_button("##panelmenu", Icon::Menu, nullptr, false, bs)) ImGui::OpenPopupEx(ImHashStr(menu_id));
    }
    PanelFrame f;
    f.pos = pos;
    f.size = size;
    f.footer_h = footer_h;
    f.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    f.in_content = true;
    g_panels.push_back(f);
    // Вміст — на 1 px усередині, щоб рамку фокуса не перекривало
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 1, pos.y + header_h));
    ImGui::BeginChild("##content", ImVec2(size.x - 2, std::max(1.0f, size.y - header_h - footer_h - 1)),
                      ImGuiChildFlags_AlwaysUseWindowPadding, content_flags);
}

void panel_footer() {
    if (g_panels.empty() || !g_panels.back().in_content) return;
    PanelFrame& f = g_panels.back();
    ImGui::EndChild();
    f.in_content = false;
    const float y = f.pos.y + f.size.y - f.footer_h;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(f.pos.x, y), ImVec2(f.pos.x + f.size.x, y), kPanelLine);
    ImGui::SetCursorScreenPos(ImVec2(f.pos.x + ImGui::GetStyle().WindowPadding.x, y + ImGui::GetStyle().WindowPadding.y));
}

void end_panel() {
    if (g_panels.empty()) return;
    const PanelFrame f = g_panels.back();
    g_panels.pop_back();
    if (f.in_content) ImGui::EndChild();
    if (f.focused) {
        const float t = std::max(1.0f, 1.5f * u());
        ImGui::GetWindowDrawList()->AddRect(f.pos + ImVec2(t * 0.5f, t * 0.5f), f.pos + ImVec2(f.size.x - t * 0.5f, f.size.y - t * 0.5f),
                                            IM_COL32(38, 128, 235, 200), 4 * u(), 0, t);
    }
    ImGui::EndChild();
}

bool begin_panel_menu(const char* menu_id) {
    return ImGui::BeginPopupEx(ImHashStr(menu_id),
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
}

void splitter(const char* id, bool vertical, ImVec2 pos, ImVec2 size, float* value, float min_v, float max_v) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    const bool active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered() || active) ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (active) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        *value = std::clamp(*value + (vertical ? d.x : d.y), min_v, std::max(min_v, max_v));
        const ImVec2 c(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (vertical) dl->AddLine(ImVec2(c.x, pos.y), ImVec2(c.x, pos.y + size.y), IM_COL32(38, 128, 235, 160), 2 * u());
        else dl->AddLine(ImVec2(pos.x, c.y), ImVec2(pos.x + size.x, c.y), IM_COL32(38, 128, 235, 160), 2 * u());
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

} // namespace gmdr::gui::ui
