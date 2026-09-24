// =============================================================================
//  app_shell.cpp — каркас вікна: верхня панель (логотип, меню, поточне демо,
//  режим і кнопки дій), бічна навігація по сторінках, розкладка сторінки з
//  монітором і таймлайном, рядок стану, тема і гарячі клавіші.
// =============================================================================
#include "app.hpp"

#include "app_ui.hpp"
#include "platform.hpp"

#include "core/util/file_assoc.hpp"
#include "core/util/file_util.hpp"
#include "core/util/strings.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "core/util/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>

namespace gmdr::gui {

namespace fs = std::filesystem;
using namespace ui;

// ================================= Сторінки =======================================
const std::vector<App::PageInfo>& App::pages() {
    // Порядок — як у переліку Page
    static const std::vector<PageInfo> k = {
        {Page::Home, "home", N_("Огляд"), Icon::Home, false, true, false},
        {Page::Video, "video", N_("Відео"), Icon::Film, false, true, false},
        {Page::Audio, "audio", N_("Звук і голоси"), Icon::Speaker, false, true, false},
        {Page::Game, "game", N_("Гра"), Icon::Gamepad, false, true, false},
        {Page::Fragment, "fragment", N_("Фрагмент і позначки"), Icon::Scissors, true, true, false},
        {Page::Chat, "chat", N_("Чат і мовлення"), Icon::Chat, true, true, true},
        {Page::Translate, "translate", N_("Переклад і озвучення"), Icon::Globe, false, false, false},
        {Page::Library, "library", N_("Бібліотека"), Icon::Library, false, false, true},
        {Page::Queue, "queue", N_("Черга"), Icon::Queue, false, false, true},
        {Page::Log, "log", N_("Журнал"), Icon::Terminal, true, false, true},
        {Page::Settings, "settings", N_("Налаштування"), Icon::Gear, false, false, false},
    };
    return k;
}

const App::PageInfo& App::page_info(Page p) const { return pages()[static_cast<size_t>(p)]; }

bool App::page_visible(Page p) const { return s_.ui_advanced || !page_info(p).advanced; }

void App::go_to(Page p) {
    if (!page_visible(p)) return;
    page_ = p;
    if (s_.ui_page != page_info(p).id) {
        s_.ui_page = page_info(p).id;
        mark_dirty();
    }
}

// =================================== Тема =========================================
void App::apply_ui_theme() {
    ThemePrefs tp;
    tp.theme = theme_index(s_.ui_theme);
    if (tp.theme == 2) tp.theme = platform_prefers_light_theme() ? 1 : 0;
    tp.accent = std::max(0, accent_index(s_.ui_accent));
    tp.scale = static_cast<float>(std::clamp(s_.ui_scale, 0.8, 2.0));
    tp.compact = s_.ui_compact;
    apply_theme(dpi_, tp);
    const ImVec4 c = ImGui::ColorConvertU32ToFloat4(kChrome);
    platform_set_frame_style(!theme_is_light(), (static_cast<unsigned>(c.x * 255) << 16) | (static_cast<unsigned>(c.y * 255) << 8) |
                                                    static_cast<unsigned>(c.z * 255));
}

void App::before_frame() {
    if (!theme_dirty_) return;
    theme_dirty_ = false;
    apply_ui_theme();
}

// =================================== Кадр =========================================
void App::frame() {
    poll();
    update_taskbar();
    handle_shortcuts();
    // Для автотестів інтерфейсу: GMDR_TEST_PAGE=video|audio|... — відкрити сторінку при старті
    static const char* forced_page = std::getenv("GMDR_TEST_PAGE");
    if (forced_page) {
        for (const auto& p : pages())
            if (std::string(forced_page) == p.id) page_ = p.page;
        forced_page = nullptr;
    }
    if (!page_visible(page_)) page_ = Page::Home;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float fs_ = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    // Головне вікно — «підкладка»; висота верхньої панелі — з FramePadding у момент Begin
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, std::round(fs_ * 0.72f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kGutter);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    draw_top_bar();

    const float status_h = std::round(fs_ * 1.9f);
    const float top = ImGui::GetCursorScreenPos().y;
    const float bottom = vp->WorkPos.y + vp->WorkSize.y - status_h;
    const float side_w = std::round(fs_ * (s_.ui_sidebar_collapsed ? 3.7f : 13.0f));
    const float gap = std::round(fs_ * 0.5f);
    draw_sidebar(ImVec2(vp->WorkPos.x, top), ImVec2(side_w, bottom - top));
    draw_content(ImVec2(vp->WorkPos.x + side_w + gap, top + gap), ImVec2(vp->WorkSize.x - side_w - gap * 2, bottom - top - gap * 2));
    draw_status_bar(ImVec2(vp->WorkPos.x, bottom), ImVec2(vp->WorkSize.x, status_h));
    draw_popups();
    ImGui::End();
}

// Клавіші: Ctrl+O — відкрити демо, Ctrl+1…9 — сторінки, Ctrl+B — бічна панель, Ctrl+, — налаштування;
// як у програмах монтажу: I / O — початок і кінець фрагмента в курсорі, M — позначка,
// Shift+I / Shift+O — перейти до них, Home / End — на початок і кінець демо
void App::handle_shortcuts() {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O) && !job_running()) open_demo_dialog();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_B)) {
        s_.ui_sidebar_collapsed = !s_.ui_sidebar_collapsed;
        mark_dirty();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) go_to(Page::Settings);
    {
        int n = 0;
        for (const auto& p : pages()) {
            if (!page_visible(p.page)) continue;
            if (n < 9 && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | static_cast<ImGuiKey>(ImGuiKey_1 + n))) go_to(p.page);
            ++n;
        }
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (!analysis_ || io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    if (io.KeyCtrl || io.KeyAlt || io.KeySuper) return;
    const double ti = analysis_->tick_interval;
    const int32_t tick = static_cast<int32_t>(std::llround(playhead_t_ / ti));
    if (io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) set_playhead(std::max(0, s_.start_tick) * ti);
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) set_playhead((s_.end_tick > 0 ? s_.end_tick : analysis_->last_tick) * ti);
        return;
    }
    if (!job_running()) {
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) set_fragment_start(tick);
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) set_fragment_end(tick);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) add_marker_at(tick, {});
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) set_playhead(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) set_playhead(analysis_->duration_seconds);
}

// ============================== Верхня панель =====================================
// Ліворуч — логотип, назва і меню; посередині — поточне демо (клік — нещодавні й відкрити інше);
// праворуч — режим інтерфейсу і кнопки дій
void App::draw_top_bar() {
    if (!ImGui::BeginMenuBar()) return;
    const float menu_y = ImGui::GetCursorScreenPos().y;   // де ImGui ставить пункти меню
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const ImRect bar = win->MenuBarRect();
    const float fs_ = ImGui::GetFontSize();
    const float u = fs_ / 15.0f;
    const float H = bar.GetHeight(), W = bar.GetWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(bar.Min.x, bar.Max.y - 1), ImVec2(bar.Max.x, bar.Max.y - 1), kPanelLine);

    // ---- Логотип і назва ----
    const float ls = std::round(H * 0.62f);
    const ImVec2 lp(bar.Min.x + std::round(12 * u), bar.Min.y + std::round((H - ls) * 0.5f));
    draw_logo(dl, lp, ls);
    float x = lp.x + ls + std::round(10 * u);
    if (W > fs_ * 72) {
        ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * 1.02f);
        const char* name = "GMod Demo Render";
        dl->AddText(ImVec2(x, bar.Min.y + std::round((H - ImGui::GetFontSize()) * 0.5f)), kText, name);
        x += ImGui::CalcTextSize(name).x + std::round(18 * u);
        ImGui::PopFont();
    } else {
        x += std::round(6 * u);
    }
    // Без окремого елемента (він змінив би висоту рядка меню) — лише підказка під курсором
    if (ImGui::IsMouseHoveringRect(lp, ImVec2(lp.x + ls, lp.y + ls)) && ImGui::IsWindowHovered())
        ImGui::SetTooltip("GMod Demo Render %s", GMDR_VERSION);

    // ---- Меню ----
    ImGui::SetCursorScreenPos(ImVec2(x, menu_y));
    draw_menus();
    const float menus_end = ImGui::GetCursorScreenPos().x + std::round(12 * u);

    // ---- Праворуч: дії ----
    const float fh = ImGui::GetFrameHeight();
    const float by = bar.Min.y + std::round((H - fh) * 0.5f);
    const float spacing = std::round(8 * u);
    const bool can_start = analysis_ && !job_running() && !(analyze_job_ && analyze_job_->running());
    float rx = bar.Max.x - std::round(12 * u);
    auto place = [&](const char* label, Kind kind, Icon icon) {
        rx -= action_width(label, kind, icon);
        ImGui::SetCursorScreenPos(ImVec2(rx, by));
        const bool r = action_button(label, kind, 0, icon);
        rx -= spacing;
        return r;
    };
    if (!job_running()) {
        ImGui::BeginDisabled(!can_start);
        if (place(tr("Почати рендер"), Kind::Cta, Icon::None)) start_render();
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!can_start || s_.manual_mode || s_.output_path.empty());
        if (place(tr("До черги"), Kind::Secondary, Icon::Plus)) add_to_queue();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Додати цей рендер (демо, фрагмент, файл і всі налаштування) до черги.\n"
                                    "Черга — на сторінці «Черга»: кілька рендерів підряд, гра запускається один раз."));
        ImGui::BeginDisabled(!can_start || s_.manual_mode);
        if (place(tr("Тест 3 с"), Kind::Secondary, Icon::Play)) start_render(true);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Тестовий прогін: 3 секунди з початку фрагмента в тимчасовий файл.\n"
                                    "Перевіряє кожен крок (гра, драйвер, демо, кадри, звук, кодек) і рахує,\n"
                                    "скільки триватиме весь рендер і скільки важитиме файл."));
    } else {
        if (place(tr("Перервати"), Kind::Negative, Icon::None)) job_->kill();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Негайно закрити гру і перервати рендер"));
        const bool watching = dynamic_cast<render::WatchJob*>(job_.get()) != nullptr;
        if (place(watching ? tr("Закрити гру") : tr("Зупинити"), Kind::Secondary, Icon::Stop)) job_->cancel();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", watching ? tr("Закрити гру (позначки, зроблені в грі, вже збережено)")
                                       : tr("Зупинити запис і зберегти вже відрендерену частину"));
        if (job_->can_show_game()) {
            if (place(show_game_ ? tr("Сховати гру") : tr("Показати гру"), Kind::Secondary, Icon::Eye)) {
                show_game_ = !show_game_;
                job_->set_show_game(show_game_);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Тимчасово показати вікно гри поверх інших, щоб перевірити, що відбувається.\n"
                                        "Не клацайте в самій грі під час рендеру."));
        }
    }
    // Режим інтерфейсу: стандартний (головне) чи розширений (усі параметри й сторінки)
    if (rx - menus_end > fs_ * 30) {
        const float mw = std::round(fs_ * 13.5f);
        rx -= mw + std::round(8 * u);
        ImGui::SetCursorScreenPos(ImVec2(rx, by));
        int mode = s_.ui_advanced ? 1 : 0;
        if (segmented("##uimode", &mode, {tr("Стандартний"), tr("Розширений")}, mw)) {
            s_.ui_advanced = mode == 1;
            mark_dirty();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tr("Стандартний режим — лише головні налаштування.\n"
                                    "Розширений — усі параметри кодеків, гри й звуку, а також сторінки\n"
                                    "«Фрагмент і позначки», «Чат і мовлення» і «Журнал»."));
        rx -= std::round(12 * u);
    }

    // ---- Посередині: поточне демо ----
    const float cx0 = menus_end, cx1 = rx;
    if (cx1 - cx0 > fs_ * 8) {
        std::string title, sub;
        if (analyze_job_ && analyze_job_->running()) {
            title = path_to_utf8(path_from_utf8(s_.demo_path).filename());
            sub = analyze_job_->progress().stage;
        } else if (analysis_) {
            const auto& a = *analysis_;
            title = path_to_utf8(path_from_utf8(s_.demo_path).filename());
            sub = trf("{} · {} · гравців: {}", a.header.map_name, format_duration(a.duration_seconds), a.players.size());
        } else {
            title = tr("Демо не відкрито");
            sub = tr("перетягніть .dem у вікно");
        }
        ImGui::PushFont(bold_font(), 0.0f);
        const float tw = ImGui::CalcTextSize(title.c_str()).x;
        ImGui::PopFont();
        const float sw = ImGui::CalcTextSize(sub.c_str()).x;
        const float pad = std::round(12 * u), icon_w = std::round(fs_ * 1.5f), chev = std::round(fs_ * 1.2f);
        const float full = icon_w + tw + std::round(10 * u) + sw + chev + pad * 2;
        const float cw = std::min(full, cx1 - cx0);
        const float chip_x = std::round(std::clamp(bar.Min.x + (W - cw) * 0.5f, cx0, cx1 - cw));
        const ImVec2 c0(chip_x, by), c1(chip_x + cw, by + fh);
        ImGui::SetCursorScreenPos(c0);
        if (ImGui::InvisibleButton("##demochip", ImVec2(cw, fh))) {
            if (!library_scanned_ && !library_future_.valid()) rescan_library();
            ImGui::OpenPopup("##demomenu");
        }
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(c0, c1, hov ? kRaised : kField, 8 * u);
        dl->AddRect(c0, c1, kPanelLine, 8 * u);
        dl->PushClipRect(c0, ImVec2(c1.x - chev, c1.y), true);
        float tx = c0.x + pad;
        draw_icon(dl, Icon::Film, ImVec2(tx + fs_ * 0.5f, c0.y + fh * 0.5f), fs_ * 0.95f, analysis_ ? kAccentText : kTextFaint);
        tx += icon_w;
        const float ty = c0.y + std::round((fh - fs_) * 0.5f);
        ImGui::PushFont(bold_font(), 0.0f);
        dl->AddText(ImVec2(tx, ty), analysis_ ? kText : kTextDim, title.c_str());
        ImGui::PopFont();
        dl->AddText(ImVec2(tx + tw + std::round(10 * u), ty), kTextDim, sub.c_str());
        dl->PopClipRect();
        draw_icon(dl, Icon::ChevronDown, ImVec2(c1.x - chev * 0.75f, c0.y + fh * 0.5f), fs_ * 0.7f, kTextDim);
        if (hov) ImGui::SetTooltip("%s", s_.demo_path.empty() ? tr("Відкрити демо (Ctrl+O)") : s_.demo_path.c_str());
        ImGui::SetNextWindowPos(ImVec2(c0.x, c1.y + 4 * u));
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(cw, fs_ * 22), 0), ImVec2(fs_ * 40, fs_ * 30));
        if (ImGui::BeginPopup("##demomenu")) {
            ImGui::BeginDisabled(job_running());
            if (ImGui::MenuItem(tr("Відкрити демо..."), "Ctrl+O")) open_demo_dialog();
            ImGui::EndDisabled();
            if (ImGui::MenuItem(tr("Показати в папці"), nullptr, false, !s_.demo_path.empty())) show_in_folder(s_.demo_path);
            if (ImGui::MenuItem(tr("Бібліотека демо"))) go_to(Page::Library);
            // Нещодавні: демо з бібліотеки, найновіші першими
            std::vector<const demo::LibraryEntry*> recent;
            for (const auto& e : library_)
                if (e.error.empty()) recent.push_back(&e);
            std::sort(recent.begin(), recent.end(), [](auto* a, auto* b) { return a->modified > b->modified; });
            if (!recent.empty()) {
                ImGui::SeparatorText(tr("Нещодавні"));
                ImGui::BeginDisabled(job_running());
                for (size_t i = 0; i < std::min<size_t>(recent.size(), 10); ++i) {
                    const auto& e = *recent[i];
                    const std::string item = e.name + (e.map.empty() ? "" : "  ·  " + e.map) +
                                             (e.seconds > 0 ? "  ·  " + format_duration(e.seconds) : "") + "##recent" + e.path;
                    if (ImGui::MenuItem(item.c_str(), nullptr, e.path == s_.demo_path)) load_demo(e.path);
                }
                ImGui::EndDisabled();
            } else if (library_future_.valid()) {
                ImGui::TextColored(kColDim, "%s", tr("Шукаю демо..."));
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndMenuBar();
}

// ================================ Бічна навігація =================================
void App::draw_sidebar(ImVec2 pos, ImVec2 size) {
    const float fs_ = ImGui::GetFontSize();
    const float u = fs_ / 15.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kChrome);
    dl->AddLine(ImVec2(pos.x + size.x - 1, pos.y), ImVec2(pos.x + size.x - 1, pos.y + size.y), kPanelLine);
    const bool collapsed = s_.ui_sidebar_collapsed;
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(8 * u), std::round(10 * u)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, std::round(3 * u)));
    ImGui::BeginChild("##sidebar", ImVec2(size.x - 1, size.y), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    const float item_h = std::round(fs_ * 2.3f) + std::round(3 * u);
    for (const auto& p : pages()) {
        if (p.page == Page::Settings || !page_visible(p.page)) continue;
        std::string badge_text;
        if (p.page == Page::Queue && !queue_.empty()) badge_text = std::to_string(queue_.size());
        if (p.page == Page::Library && library_scanned_) badge_text = std::to_string(library_.size());
        if (nav_item(tr(p.label), p.icon, page_ == p.page, collapsed, badge_text.empty() ? nullptr : badge_text.c_str())) go_to(p.page);
    }
    // Унизу — налаштування і згортання панелі
    const float bottom_y = size.y - item_h * 2 - std::round(12 * u);
    if (ImGui::GetCursorPosY() < bottom_y) ImGui::SetCursorPosY(bottom_y);
    if (nav_item(tr("Налаштування"), Icon::Gear, page_ == Page::Settings, collapsed)) go_to(Page::Settings);
    if (nav_item(collapsed ? tr("Розгорнути панель") : tr("Згорнути панель"), collapsed ? Icon::ChevronRight : Icon::ChevronLeft,
                 false, collapsed)) {
        s_.ui_sidebar_collapsed = !collapsed;
        mark_dirty();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

// ========================= Сторінка, монітор і таймлайн ===========================
void App::draw_content(ImVec2 pos, ImVec2 size) {
    const float fs_ = ImGui::GetFontSize();
    const float gap = std::round(fs_ * 0.5f);
    const PageInfo& pi = page_info(page_);
    // Без демо монітор і таймлайн порожні — більше місця сторінці (крім кодування готових кадрів)
    const bool media = pi.media && (analysis_ || (analyze_job_ && analyze_job_->running()) || job_running());
    const bool mon = media && show_monitor_;
    const bool tl = media && show_timeline_;
    const float min_page_w = fs_ * 22, min_mon_w = fs_ * 16, min_top_h = fs_ * 10, min_tl_h = fs_ * 10;
    float top_h = size.y;
    if (tl) top_h = std::clamp(std::round(split_y_ * size.y), min_top_h, std::max(min_top_h, size.y - min_tl_h - gap));
    float page_w = size.x;
    if (mon) page_w = std::clamp(std::round(split_x_ * size.x), min_page_w, std::max(min_page_w, size.x - min_mon_w - gap));
    draw_page(pos, ImVec2(page_w, top_h));
    if (mon) draw_monitor_panel(ImVec2(pos.x + page_w + gap, pos.y), ImVec2(size.x - page_w - gap, top_h));
    if (tl) draw_timeline_panel(ImVec2(pos.x, pos.y + top_h + gap), ImVec2(size.x, size.y - top_h - gap));
    if (mon) {
        splitter("##split_x", true, ImVec2(pos.x + page_w, pos.y), ImVec2(gap, top_h), &page_w, min_page_w, size.x - min_mon_w - gap);
        if (size.x > 0) split_x_ = page_w / size.x;
    }
    if (tl) {
        splitter("##split_y", false, ImVec2(pos.x, pos.y + top_h), ImVec2(size.x, gap), &top_h, min_top_h, size.y - min_tl_h - gap);
        if (size.y > 0) split_y_ = top_h / size.y;
    }
}

void App::draw_page(ImVec2 pos, ImVec2 size) {
    const PageInfo& pi = page_info(page_);
    begin_panel("##page", pos, size, {}, nullptr, nullptr, 0,
                pi.fill ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
    // Налаштування рендеру не змінюються, поки він іде
    const bool locks = page_ == Page::Video || page_ == Page::Audio || page_ == Page::Game || page_ == Page::Fragment;
    ImGui::BeginDisabled(locks && job_running());
    ImGui::PushID(pi.id);
    switch (page_) {
    case Page::Home: draw_page_home(); break;
    case Page::Video: draw_page_video(); break;
    case Page::Audio: draw_page_audio(); break;
    case Page::Game: draw_page_game(); break;
    case Page::Fragment: draw_page_fragment(); break;
    case Page::Chat: draw_page_chat(); break;
    case Page::Translate: draw_page_translate(); break;
    case Page::Library: draw_page_library(); break;
    case Page::Queue: draw_page_queue(); break;
    case Page::Log: draw_page_log(); break;
    case Page::Settings: draw_page_settings(); break;
    case Page::Count: break;
    }
    ImGui::PopID();
    ImGui::EndDisabled();
    // GMDR_TEST_SCROLL=0..1 — прокрутити сторінку (частка від кінця) у перших кадрах
    static const double forced_scroll = [] {
        const char* e = std::getenv("GMDR_TEST_SCROLL");
        return e ? std::atof(e) : -1.0;
    }();
    static int scroll_frames = 60;
    if (forced_scroll >= 0 && scroll_frames > 0) {
        --scroll_frames;
        ImGui::SetScrollY(ImGui::GetScrollMaxY() * static_cast<float>(forced_scroll));
    }
    end_panel();
}

// ================================== Рядок стану ===================================
void App::draw_status_bar(ImVec2 pos, ImVec2 size) {
    const float fs_ = ImGui::GetFontSize();
    const float u = fs_ / 15.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kChrome);
    dl->AddLine(pos, ImVec2(pos.x + size.x, pos.y), kPanelLine);
    const float pad = std::round(12 * u);
    const float cy = pos.y + size.y * 0.5f;
    const float ty = std::round(cy - fs_ * 0.5f);

    // Стан: що відбувається і наскільки просунулося
    std::string stage = tr("Готово до роботи");
    float fraction = -2;   // -2 — без смуги, -1 — невизначений прогрес
    ImU32 dot = kTextFaint, bar = kAccent;
    render::Progress p;
    bool have_p = false;
    if (analyze_job_ && analyze_job_->running()) {
        const auto ap = analyze_job_->progress();
        stage = ap.stage;
        fraction = static_cast<float>(std::max(0.0, ap.fraction));
        dot = kAccent;
    } else if (job_) {
        p = job_->progress();
        have_p = true;
        stage = p.stage;
        const bool running = job_->running();
        const bool fraction_only = job_has_fraction_only();
        const auto state = job_->state();
        if (p.frames > 0 || fraction_only || state == render::JobState::Succeeded) fraction = static_cast<float>(std::clamp(p.fraction, 0.0, 1.0));
        else if (running) fraction = -1;
        if (running) dot = kAccent;
        else if (state == render::JobState::Succeeded) dot = bar = kGreen;
        else if (state == render::JobState::Failed) dot = bar = kRed;
        if (running && p.disk_low) bar = kRed;
        else if (running && p.game_paused) bar = kOrange;
    }
    float x = pos.x + pad;
    dl->AddCircleFilled(ImVec2(x + 4 * u, cy), 4 * u, dot);
    x += std::round(14 * u);
    const float stage_w = std::min(ImGui::CalcTextSize(stage.c_str()).x, fs_ * 16);
    dl->PushClipRect(ImVec2(x, pos.y), ImVec2(x + stage_w, pos.y + size.y), true);
    dl->AddText(ImVec2(x, ty), kText, stage.c_str());
    dl->PopClipRect();
    x += stage_w + std::round(12 * u);
    if (fraction > -1.5f) {
        const float mw = std::round(fs_ * 9);
        ImGui::SetCursorScreenPos(ImVec2(x, std::round(cy - 2 * u)));
        meter(fraction, ImVec2(mw, std::round(4 * u)), bar);
        x += mw + std::round(8 * u);
        if (fraction >= 0) {
            const std::string pct = std::format("{:.1f}%", fraction * 100);
            dl->AddText(ImVec2(x, ty), kText, pct.c_str());
            x += ImGui::CalcTextSize(pct.c_str()).x + std::round(14 * u);
        }
    }

    // Праворуч: кнопки розкладки (бічна панель, монітор, таймлайн)
    float right = pos.x + size.x - pad;
    const float bs = std::round(size.y - 6 * u);
    auto layout_button = [&](const char* id, Icon icon, const char* tip, bool on) {
        right -= bs;
        ImGui::SetCursorScreenPos(ImVec2(right, pos.y + std::round((size.y - bs) * 0.5f)));
        const bool r = icon_button(id, icon, tip, on, bs);
        right -= std::round(2 * u);
        return r;
    };
    if (page_info(page_).media) {
        if (layout_button("##lay_tl", Icon::Timeline, tr("Показати або сховати таймлайн"), show_timeline_)) show_timeline_ = !show_timeline_;
        if (layout_button("##lay_mon", Icon::Monitor, tr("Показати або сховати монітор"), show_monitor_)) show_monitor_ = !show_monitor_;
    }
    if (layout_button("##lay_side", Icon::Sidebar, tr("Бічна панель: значки або значки з підписами (Ctrl+B)"), !s_.ui_sidebar_collapsed)) {
        s_.ui_sidebar_collapsed = !s_.ui_sidebar_collapsed;
        mark_dirty();
    }
    right -= std::round(10 * u);

    // Що зробити після рендеру (під час рендеру)
    const bool after_combo = job_ && job_->running() && !dynamic_cast<render::WatchJob*>(job_.get()) && !job_has_fraction_only();
    if (after_combo) {
        const float cw = fs_ * 9;
        right -= cw;
        ImGui::SetCursorScreenPos(ImVec2(right, std::round(cy - ImGui::GetFrameHeight() * 0.5f)));
        draw_after_done_combo();
        const char* then = tr("Потім:");
        right -= ImGui::CalcTextSize(then).x + std::round(8 * u);
        dl->AddText(ImVec2(right, ty), kTextDim, then);
        right -= std::round(16 * u);
    }

    // Посередині: кадри, час відео, швидкість, скільки лишилось, розмір файлу; попередження
    if (have_p) {
        std::string stats;
        if (p.frames > 0 || p.subframes > 0) {
            stats = trf("Кадрів: {}  |  Відео: {} з {}", p.frames, format_duration(p.video_seconds), format_duration(p.expected_seconds));
            if (p.speed_fps > 0) stats += trf("  |  {:.1f} кадр/с", p.speed_fps);
            if (p.eta >= 0 && job_->running()) stats += tr("  |  Залишилось ~") + format_duration(p.eta);
            stats += tr("  |  Файл: ") + format_bytes(static_cast<uint64_t>(std::max<int64_t>(0, p.bytes_written)));
            if (p.pending_files > 0) stats += trf("  |  Черга: {}", p.pending_files);
        } else if (p.demo_total > 0 && job_->running()) {
            stats = trf("Гра: {}  |  тік {} / {}", p.driver_state, p.demo_tick, p.demo_total);
        }
        if (p.elapsed > 0) stats += (stats.empty() ? "" : "  |  ") + std::string(tr("Минуло: ")) + format_duration(p.elapsed);
        std::string warn;
        ImU32 warn_col = ImGui::GetColorU32(kColWarn);
        if (job_->running() && p.disk_low) {
            warn = tr("гру призупинено: закінчується місце на диску");
            warn_col = ImGui::GetColorU32(kColErr);
        } else if (job_->running() && p.game_paused) {
            warn = tr("гра на паузі — кодер наздоганяє");
        } else if (p.game_restarts > 0) {
            warn = trf("гру перезапущено після збою: {}", p.game_restarts);
        }
        dl->PushClipRect(ImVec2(x, pos.y), ImVec2(std::max(x, right), pos.y + size.y), true);
        dl->AddText(ImVec2(x, ty), kTextDim, stats.c_str());
        float wx = x + ImGui::CalcTextSize(stats.c_str()).x + std::round(16 * u);
        if (!warn.empty()) {
            draw_icon(dl, Icon::Warning, ImVec2(wx + fs_ * 0.45f, cy), fs_ * 0.9f, warn_col);
            dl->AddText(ImVec2(wx + fs_ * 1.2f, ty), warn_col, warn.c_str());
            const float ww = fs_ * 1.2f + ImGui::CalcTextSize(warn.c_str()).x;
            if (p.game_restarts > 0 && ImGui::IsMouseHoveringRect(ImVec2(wx, pos.y), ImVec2(wx + ww, pos.y + size.y)))
                ImGui::SetTooltip("%s", tr("Гра впала чи зависла, і програма перезапустила її з того самого місця демо.\n"
                                        "Відео дописується в той самий файл без шва — деталі в журналі."));
        }
        dl->PopClipRect();
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));
    ImGui::Dummy(ImVec2(0, 0));
}

} // namespace gmdr::gui
