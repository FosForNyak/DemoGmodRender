// =============================================================================
//  ui_theme.cpp — тема вікна: темна й світла палітри, колір акценту, щільність
//  і масштаб. Власний стиль програми: глибоке нейтральне тло з легким холодним
//  відтінком, заокруглені картки, акцент (типово фіолетовий) на головних діях.
//
//  Усі розміри задано в «пікселях макета» для шрифту 15 px; ScaleAllSizes і
//  FontScaleDpi множать їх на DPI монітора і масштаб із налаштувань.
// =============================================================================
#include "app_ui.hpp"

#include "core/util/i18n.hpp"

#include <algorithm>
#include <cmath>

namespace gmdr::gui::ui {

namespace {
ImFont* g_regular = nullptr;
ImFont* g_bold = nullptr;
bool    g_light = false;

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImVec4 vec(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = a;
    return v;
}
// Змішати два кольори (t = 0 — a, 1 — b)
ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = vec(a), y = vec(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t,
                                                 x.w + (y.w - x.w) * t));
}
ImU32 with_alpha(ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT); }
} // namespace

const std::vector<AccentPreset>& accent_presets() {
    static const std::vector<AccentPreset> k = {
        {"violet", N_("Фіолетовий"), rgb(123, 97, 255)}, {"blue", N_("Синій"), rgb(59, 130, 246)},
        {"teal", N_("Бірюзовий"), rgb(20, 184, 166)},    {"green", N_("Зелений"), rgb(34, 179, 94)},
        {"orange", N_("Помаранчевий"), rgb(249, 115, 22)}, {"pink", N_("Рожевий"), rgb(236, 72, 153)},
        {"red", N_("Червоний"), rgb(239, 68, 68)},
    };
    return k;
}

int accent_index(const std::string& id) {
    const auto& a = accent_presets();
    for (size_t i = 0; i < a.size(); ++i)
        if (id == a[i].id) return static_cast<int>(i);
    return -1;
}

int theme_index(const std::string& id) { return id == "light" ? 1 : id == "system" ? 2 : 0; }
const char* theme_id(int index) { return index == 1 ? "light" : index == 2 ? "system" : "dark"; }
bool theme_is_light() { return g_light; }

void set_fonts(ImFont* regular, ImFont* bold) {
    g_regular = regular;
    g_bold = bold;
}

ImFont* bold_font() { return g_bold ? g_bold : g_regular; }

void apply_theme(float dpi, const ThemePrefs& p) {
    g_light = p.theme == 1;
    const auto& accents = accent_presets();
    const ImU32 accent = accents[static_cast<size_t>(std::clamp(p.accent, 0, static_cast<int>(accents.size()) - 1))].color;
    const ImU32 white = rgb(255, 255, 255), black = rgb(0, 0, 0);

    // ---- Палітра ----
    if (!g_light) {
        kGutter = rgb(12, 13, 17);
        kChrome = rgb(17, 19, 24);
        kPanel = rgb(21, 23, 29);
        kCard = rgb(27, 30, 37);
        kRaised = rgb(36, 40, 49);
        kPanelLine = rgb(43, 47, 57);
        kField = rgb(15, 17, 21);
        kText = rgb(230, 232, 237);
        kTextDim = rgb(150, 157, 171);
        kTextFaint = rgb(102, 109, 124);
        kGreen = rgb(46, 196, 118);
        kRed = rgb(240, 82, 82);
        kOrange = rgb(245, 166, 35);
        kAccentHover = mix(accent, white, 0.14f);
        kAccentText = mix(accent, white, 0.32f);
        kTlHead = rgb(24, 26, 32);
        kTlLaneHead = rgb(32, 35, 43);
        kTlBody = rgb(16, 18, 22);
        kTlLane = rgb(20, 22, 27);
        kTlLine = rgb(11, 12, 15);
        kTlRuler = rgb(26, 28, 34);
        kTlTickMajor = rgb(146, 152, 165);
        kTlTickMinor = rgb(78, 84, 96);
        kTlRulerText = rgb(160, 166, 179);
        kTlShade = rgb(0, 0, 0, 105);
    } else {
        kGutter = rgb(222, 225, 232);
        kChrome = rgb(242, 243, 247);
        kPanel = rgb(249, 250, 252);
        kCard = rgb(255, 255, 255);
        kRaised = rgb(236, 238, 243);
        kPanelLine = rgb(214, 218, 226);
        kField = rgb(255, 255, 255);
        kText = rgb(26, 29, 36);
        kTextDim = rgb(86, 94, 108);
        kTextFaint = rgb(140, 147, 160);
        kGreen = rgb(22, 150, 80);
        kRed = rgb(214, 45, 45);
        kOrange = rgb(206, 110, 6);
        kAccentHover = mix(accent, black, 0.12f);
        kAccentText = mix(accent, black, 0.18f);
        kTlHead = rgb(236, 238, 243);
        kTlLaneHead = rgb(228, 231, 237);
        kTlBody = rgb(247, 248, 250);
        kTlLane = rgb(241, 243, 246);
        kTlLine = rgb(218, 222, 229);
        kTlRuler = rgb(232, 235, 240);
        kTlTickMajor = rgb(98, 106, 120);
        kTlTickMinor = rgb(172, 178, 190);
        kTlRulerText = rgb(78, 86, 100);
        kTlShade = rgb(0, 0, 0, 38);
    }
    kMonitor = rgb(7, 8, 10);
    kAccent = accent;
    kAccentSoft = with_alpha(accent, g_light ? 34 : 46);
    kOnAccent = white;
    kColWarn = vec(kOrange);
    kColErr = vec(kRed);
    kColDim = vec(kTextDim);
    kColOk = vec(kGreen);
    kColAccent = vec(kAccentText);

    // ---- Відступи й заокруглення ----
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    if (g_light) ImGui::StyleColorsLight(&s);
    else ImGui::StyleColorsDark(&s);
    const bool c = p.compact;
    s.WindowPadding = c ? ImVec2(10, 8) : ImVec2(14, 12);
    s.WindowRounding = 0;
    s.WindowBorderSize = 0;
    s.ChildRounding = 10;
    s.ChildBorderSize = 1;
    s.PopupRounding = 8;
    s.PopupBorderSize = 1;
    s.FramePadding = c ? ImVec2(7, 3) : ImVec2(9, 5);
    s.FrameRounding = 6;
    s.FrameBorderSize = 1;
    s.ItemSpacing = c ? ImVec2(8, 5) : ImVec2(10, 8);
    s.ItemInnerSpacing = c ? ImVec2(5, 4) : ImVec2(7, 5);
    s.CellPadding = c ? ImVec2(6, 2) : ImVec2(8, 4);
    s.IndentSpacing = 18;
    s.ScrollbarSize = 10;
    s.ScrollbarRounding = 6;
    s.ScrollbarPadding = 2;
    s.GrabMinSize = 10;
    s.GrabRounding = 6;
    s.TabRounding = 6;
    s.TabBorderSize = 0;
    s.TabBarBorderSize = 0;
    s.TabBarOverlineSize = 0;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(0, 4);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SelectableTextAlign = ImVec2(0, 0.5f);

    ImVec4* col = s.Colors;
    col[ImGuiCol_Text] = vec(kText);
    col[ImGuiCol_TextDisabled] = vec(kTextFaint);
    col[ImGuiCol_WindowBg] = vec(kPanel);
    col[ImGuiCol_ChildBg] = vec(kCard, 0.0f);
    col[ImGuiCol_PopupBg] = vec(g_light ? kCard : kRaised);
    col[ImGuiCol_Border] = vec(kPanelLine);
    col[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_FrameBg] = vec(kField);
    col[ImGuiCol_FrameBgHovered] = vec(mix(kField, kRaised, 0.6f));
    col[ImGuiCol_FrameBgActive] = vec(mix(kField, kRaised, 0.3f));
    col[ImGuiCol_TitleBg] = vec(kChrome);
    col[ImGuiCol_TitleBgActive] = vec(kChrome);
    col[ImGuiCol_TitleBgCollapsed] = vec(kChrome);
    col[ImGuiCol_MenuBarBg] = vec(kChrome);
    col[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ScrollbarGrab] = vec(mix(kPanelLine, kTextFaint, 0.35f));
    col[ImGuiCol_ScrollbarGrabHovered] = vec(kTextFaint);
    col[ImGuiCol_ScrollbarGrabActive] = vec(kTextDim);
    col[ImGuiCol_CheckMark] = vec(kOnAccent);
    col[ImGuiCol_SliderGrab] = vec(kAccent);
    col[ImGuiCol_SliderGrabActive] = vec(kAccentHover);
    col[ImGuiCol_Button] = vec(kRaised);
    col[ImGuiCol_ButtonHovered] = vec(mix(kRaised, kText, 0.08f));
    col[ImGuiCol_ButtonActive] = vec(mix(kRaised, kText, 0.14f));
    col[ImGuiCol_Header] = vec(kAccentSoft);
    col[ImGuiCol_HeaderHovered] = vec(with_alpha(kText, g_light ? 16 : 14));
    col[ImGuiCol_HeaderActive] = vec(with_alpha(kAccent, 90));
    col[ImGuiCol_Separator] = vec(kPanelLine);
    col[ImGuiCol_SeparatorHovered] = vec(kAccent);
    col[ImGuiCol_SeparatorActive] = vec(kAccentHover);
    col[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ResizeGripHovered] = vec(kAccent, 0.6f);
    col[ImGuiCol_ResizeGripActive] = vec(kAccent);
    col[ImGuiCol_InputTextCursor] = vec(kText);
    col[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TabHovered] = vec(with_alpha(kText, 14));
    col[ImGuiCol_TabSelected] = vec(kAccentSoft);
    col[ImGuiCol_TabSelectedOverline] = vec(kAccent);
    col[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TabDimmedSelected] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_PlotHistogram] = vec(kAccent);
    col[ImGuiCol_PlotHistogramHovered] = vec(kAccentHover);
    col[ImGuiCol_TableHeaderBg] = vec(mix(kCard, kRaised, 0.5f));
    col[ImGuiCol_TableBorderStrong] = vec(kPanelLine);
    col[ImGuiCol_TableBorderLight] = vec(mix(kPanelLine, kCard, 0.4f));
    col[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TableRowBgAlt] = vec(with_alpha(kText, g_light ? 7 : 6));
    col[ImGuiCol_TextLink] = vec(kAccentText);
    col[ImGuiCol_TextSelectedBg] = vec(kAccent, 0.4f);
    col[ImGuiCol_DragDropTarget] = vec(kAccent);
    col[ImGuiCol_NavCursor] = vec(kAccent);
    col[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, g_light ? 0.35f : 0.6f);

    const float scale = dpi * std::clamp(p.scale, 0.5f, 3.0f);
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
    s.FontSizeBase = 15.0f;
}

} // namespace gmdr::gui::ui
