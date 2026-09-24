// =============================================================================
//  app_ui.hpp — спільні дрібниці інтерфейсу для всіх файлів вікна: палітра теми,
//  віджети (картки, кнопки, перемикачі, повзунки, бічна навігація), списки кодеків
//  і форматів. Тема — ui_theme.cpp, віджети — ui_widgets.cpp.
//
//  Кольори палітри — змінні: apply_theme() заповнює їх для темної чи світлої теми
//  і вибраного акценту, тож увесь код малює через них, а не через готові числа.
// =============================================================================
#pragma once

#include "imgui.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace gmdr::gui::ui {

// ---- Палітра (ImU32 — для малювання; заповнює apply_theme) ----
inline ImU32 kGutter;       // підкладка вікна: проміжки між панелями
inline ImU32 kChrome;       // верхня панель, бічна навігація, рядок стану
inline ImU32 kPanel;        // тло панелей
inline ImU32 kCard;         // картки на панелях
inline ImU32 kRaised;       // підсвітка під курсором
inline ImU32 kPanelLine;    // межі карток і полів, лінії
inline ImU32 kField;        // поля вводу
inline ImU32 kMonitor;      // тло монітора (завжди темне)
inline ImU32 kText;
inline ImU32 kTextDim;
inline ImU32 kTextFaint;
inline ImU32 kAccent;       // головна дія, курсор таймлайну, увімкнені перемикачі
inline ImU32 kAccentHover;
inline ImU32 kAccentText;   // «гарячі» значення, таймкод, посилання
inline ImU32 kAccentSoft;   // напівпрозорий акцент (виділення, активний пункт навігації)
inline ImU32 kOnAccent;     // текст на акценті
inline ImU32 kGreen;
inline ImU32 kRed;
inline ImU32 kOrange;
// Таймлайн
inline ImU32 kTlHead;       // заголовки доріжок
inline ImU32 kTlLaneHead;   // заголовок однієї доріжки
inline ImU32 kTlBody;       // шкала
inline ImU32 kTlLane;       // доріжка
inline ImU32 kTlLine;       // лінії між доріжками
inline ImU32 kTlRuler;      // лінійка
inline ImU32 kTlTickMajor, kTlTickMinor, kTlRulerText;
inline ImU32 kTlShade;      // затемнення поза фрагментом
// ---- Кольори тексту (ImVec4 — для TextColored) ----
inline ImVec4 kColWarn, kColErr, kColDim, kColOk, kColAccent;

// ---- Тема ----
struct ThemePrefs {
    int   theme = 0;        // 0 — темна, 1 — світла, 2 — як у Windows
    int   accent = 0;       // індекс у accent_presets()
    float scale = 1.0f;     // масштаб інтерфейсу поверх DPI (0.8..2)
    bool  compact = false;  // щільніше: менші відступи
};
struct AccentPreset { const char* id; const char* label; ImU32 color; };
const std::vector<AccentPreset>& accent_presets();
int  accent_index(const std::string& id);   // -1 — невідомий
int  theme_index(const std::string& id);    // "dark"/"light"/"system"
const char* theme_id(int index);
// Застосувати тему до всього вікна (кольори, відступи, заокруглення, масштаб).
void apply_theme(float dpi, const ThemePrefs& prefs);
bool theme_is_light();   // після apply_theme: світла тема

// ---- Шрифти (задає App::init) ----
void    set_fonts(ImFont* regular, ImFont* bold);
ImFont* bold_font();

// ---- Іконки (малюються лініями — однаково чіткі на будь-якому DPI) ----
enum class Icon {
    None, Play, Stop, Record, Menu, ChevronDown, ChevronRight, ChevronUp, ChevronLeft, Folder, Plus, Close, Search,
    MarkIn, MarkOut, GoToIn, GoToOut, Marker, Speaker, Headphones, Eye, Check, Warning, Info, Film, Queue, Refresh, Pencil,
    Home, Globe, Gamepad, Scissors, Chat, Library, Terminal, Gear, Sidebar, Maximize, Sparkle, User, Clock, File, Mic,
    Monitor, Timeline, Download, Link,
};
void draw_icon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 col);
// Логотип програми: заокруглений квадрат із градієнтом акценту і «кадром» зі стрілкою відтворення.
void draw_logo(ImDrawList* dl, ImVec2 pos, float size);

// Кнопка-іконка без рамки (підсвічується під курсором). active — увімкнена (акцентна).
bool icon_button(const char* id, Icon icon, const char* tooltip = nullptr, bool active = false, float size = 0);
// Кнопка з літерою (M/S на доріжках таймлайну): увімкнена — кольоровий квадрат.
bool letter_toggle(const char* id, const char* letter, bool on, ImU32 on_col, const char* tooltip);

// Кнопки дій: Cta — головна (акцентна), Secondary — звичайна, Negative — небезпечна (червона),
// Positive — запуск (зелена), Ghost — лише текст.
enum class Kind { Cta, Secondary, Negative, Positive, Ghost };
bool  action_button(const char* label, Kind kind, float width = 0, Icon icon = Icon::None);
float action_width(const char* label, Kind kind, Icon icon = Icon::None);   // ширина такої кнопки

// Картка: заокруглена плашка із заголовком (і розкривачкою, якщо collapsible). card_begin
// повертає true, якщо вміст треба малювати; card_end — завжди.
bool card_begin(const char* title, const char* subtitle = nullptr, Icon icon = Icon::None, bool collapsible = true,
                bool default_open = true);
void card_end();
// Заголовок сторінки: назва й підзаголовок (що тут налаштовується).
void page_header(const char* title, const char* subtitle = nullptr);
// Підзаголовок-розділювач усередині картки.
void subheading(const char* text);

// Підпис ліворуч від віджета. width 0 — за найдовшим підписом у цьому вікні (запам'ятовується).
void label(const char* text, float width = 0);
// Ширина поля праворуч від підпису (з місцем під «?»); max_em > 0 — не ширше за стільки em.
float field_width(float max_em = 0);
// Підказка-іконка «?» праворуч від попереднього віджета.
void help_marker(const char* text);
// SameLine перед галочкою з підписом next_label — або новий рядок, якщо вона не влазить.
void same_line_if_fits(const char* next_label);
// Рядок «назва — значення» (довідка на картках).
void info_row(const char* name, const std::string& value, ImU32 value_col = 0);
// Невеликий бейдж (кількість, стан).
void badge(const char* text, ImU32 col);

// Галочка, перемикач-вимикач (switch) і радіокнопка.
bool checkbox(const char* label, bool* v);
bool toggle(const char* label, bool* v);
bool radio(const char* label, bool active);
// Перемикач-«капсула» з кількох варіантів (виділення плавно переїжджає); width 0 — за текстом
bool segmented(const char* id, int* current, std::initializer_list<const char*> labels, float width = 0);
// Випадний список: як ImGui::BeginCombo, але в стилі поля.
bool begin_combo(const char* id, const char* preview, ImGuiComboFlags flags = 0);
// Кольоровий кружечок-вибір (акцент теми). true — клацнули.
bool swatch(const char* id, ImU32 col, bool selected, const char* tooltip);

// «Гаряче» значення: акцентний текст, тягніть мишею вліво-вправо, клік — ввести число.
bool hot_float(const char* id, float* v, float speed, float min, float max, const char* fmt);
bool hot_int(const char* id, int* v, float speed, int min, int max, const char* fmt);
// Повзунок: доріжка з круглою ручкою і «гарячим» значенням праворуч.
bool slider_float(const char* id, float* v, float min, float max, const char* fmt, float width);
bool slider_int(const char* id, int* v, int min, int max, const char* fmt, float width);

// Тонка смуга прогресу (fraction < 0 — невизначений, «біжить»). col 0 — акцент.
void meter(float fraction, ImVec2 size, ImU32 col = 0);
// Поле пошуку з лупою.
bool search_input(const char* id, const char* hint, std::string* text, float width);

// ---- Бічна навігація ----
// Пункт: значок і підпис (collapsed — лише значок, підпис у підказці); badge_text — праворуч.
bool nav_item(const char* label, Icon icon, bool active, bool collapsed, const char* badge_text = nullptr);

// ---- Панелі (розташування задає App::frame) ----
// Кнопка-іконка в заголовку панелі: *pressed стає true, якщо її натиснули.
struct PanelButton { const char* id; Icon icon; const char* tooltip; bool active; bool* pressed; };
// Заокруглена панель із заголовком (кілька заголовків — вкладки). menu_id — меню «≡» праворуч
// (відкрити: begin_panel_menu(menu_id)). footer_h — висота нижньої смуги (після panel_footer()).
// buttons — кнопки праворуч у заголовку. Завжди закривати end_panel().
void begin_panel(const char* id, ImVec2 pos, ImVec2 size, const std::vector<std::string>& tabs, int* current,
                 const char* menu_id = nullptr, float footer_h = 0, ImGuiWindowFlags content_flags = 0,
                 std::initializer_list<PanelButton> buttons = {});
void panel_footer();
void end_panel();
bool begin_panel_menu(const char* menu_id);   // true — меню відкрите (закрити ImGui::EndPopup())
// Роздільник між панелями: перетягування змінює *value (у пікселях).
void splitter(const char* id, bool vertical, ImVec2 pos, ImVec2 size, float* value, float min_v, float max_v);

// Таймкод як у програмах монтажу: 00:01:23:15 (кадри — за частотою fps).
std::string timecode(double seconds, double fps);
// Дата й час файлу (unix-час) у місцевому часі: 2026-09-24 15:30.
std::string format_date_time(int64_t unix_time);

// ---- Дані для списків ----
struct Resolution { const char* label; int w, h; };
inline constexpr Resolution kResolutions[] = {
    {"854 × 480 (480p)", 854, 480},        {"1280 × 720 (HD)", 1280, 720},       {"1600 × 900", 1600, 900},
    {"1920 × 1080 (Full HD)", 1920, 1080}, {"2560 × 1440 (2K / QHD)", 2560, 1440}, {"3840 × 2160 (4K)", 3840, 2160},
    {"5120 × 2880 (5K)", 5120, 2880},      {"7680 × 4320 (8K)", 7680, 4320},     {"1080 × 1920 (вертикальне)", 1080, 1920},
    {"1080 × 1080 (квадрат)", 1080, 1080},
};
inline constexpr const char* kFps[] = {"24", "25", "30", "48", "50", "60", "90", "120", "144", "165", "240",
                                       "23.976", "29.97", "59.94"};

struct Container { const char* ext; const char* label; const char* image_codec; };
inline constexpr Container kContainers[] = {
    {"mp4", "MP4 — найсумісніший", nullptr},        {"mkv", "MKV (Matroska) — будь-які кодеки", nullptr},
    {"mov", "MOV (QuickTime) — для монтажу", nullptr}, {"webm", "WebM — VP9/AV1 + Opus", nullptr},
    {"avi", "AVI", nullptr},                        {"nut", "NUT", nullptr},
    {"png", "Кадри PNG (послідовність)", "png"},     {"tiff", "Кадри TIFF (послідовність)", "tiff"},
    {"bmp", "Кадри BMP (послідовність)", "bmp"},     {"jpg", "Кадри JPEG (послідовність)", "mjpeg"},
};

inline bool is_image_container(const std::string& ext) {
    for (const auto& c : kContainers)
        if (ext == c.ext) return c.image_codec != nullptr;
    return false;
}

struct KnownCodec { const char* name; const char* label; bool gpu; const char* group; };
inline constexpr KnownCodec kVideoCodecs[] = {
    {"libx264", "H.264 (x264) — найсумісніший", false, "Процесор (CPU)"},
    {"libx265", "H.265 / HEVC (x265) — менший файл", false, "Процесор (CPU)"},
    {"libsvtav1", "AV1 (SVT-AV1) — найкраще стиснення", false, "Процесор (CPU)"},
    {"libaom-av1", "AV1 (libaom) — дуже повільно", false, "Процесор (CPU)"},
    {"libvpx-vp9", "VP9 (libvpx) — для WebM/YouTube", false, "Процесор (CPU)"},
    {"prores_ks", "Apple ProRes — для монтажу", false, "Процесор (CPU)"},
    {"dnxhd", "Avid DNxHR — для монтажу", false, "Процесор (CPU)"},
    {"ffv1", "FFV1 — без втрат (архів)", false, "Процесор (CPU)"},
    {"utvideo", "UT Video — без втрат, швидкий", false, "Процесор (CPU)"},
    {"png", "PNG — без втрат", false, "Процесор (CPU)"},
    {"h264_nvenc", "H.264 — NVIDIA NVENC", true, "Відеокарта (GPU)"},
    {"hevc_nvenc", "H.265/HEVC — NVIDIA NVENC", true, "Відеокарта (GPU)"},
    {"av1_nvenc", "AV1 — NVIDIA NVENC (RTX 40+)", true, "Відеокарта (GPU)"},
    {"h264_amf", "H.264 — AMD AMF", true, "Відеокарта (GPU)"},
    {"hevc_amf", "H.265/HEVC — AMD AMF", true, "Відеокарта (GPU)"},
    {"av1_amf", "AV1 — AMD AMF (RX 7000+)", true, "Відеокарта (GPU)"},
    {"h264_qsv", "H.264 — Intel Quick Sync", true, "Відеокарта (GPU)"},
    {"hevc_qsv", "H.265/HEVC — Intel Quick Sync", true, "Відеокарта (GPU)"},
    {"av1_qsv", "AV1 — Intel Quick Sync (Arc)", true, "Відеокарта (GPU)"},
    {"h264_vulkan", "H.264 — Vulkan", true, "Відеокарта (GPU)"},
    {"hevc_vulkan", "H.265/HEVC — Vulkan", true, "Відеокарта (GPU)"},
    {"av1_vulkan", "AV1 — Vulkan", true, "Відеокарта (GPU)"},
    {"h264_d3d12va", "H.264 — Direct3D 12", true, "Відеокарта (GPU)"},
    {"hevc_d3d12va", "H.265/HEVC — Direct3D 12", true, "Відеокарта (GPU)"},
    {"h264_vaapi", "H.264 — VAAPI (Linux)", true, "Відеокарта (GPU)"},
    {"hevc_vaapi", "H.265/HEVC — VAAPI (Linux)", true, "Відеокарта (GPU)"},
    {"av1_vaapi", "AV1 — VAAPI (Linux)", true, "Відеокарта (GPU)"},
};
inline constexpr KnownCodec kAudioCodecs[] = {
    {"aac", "AAC", false, ""},        {"libopus", "Opus", false, ""},         {"flac", "FLAC (без втрат)", false, ""},
    {"pcm_s16le", "PCM 16 біт (WAV)", false, ""}, {"pcm_s24le", "PCM 24 біт", false, ""}, {"alac", "ALAC (Apple, без втрат)", false, ""},
    {"libmp3lame", "MP3", false, ""}, {"libvorbis", "Vorbis", false, ""},     {"ac3", "AC-3 (Dolby Digital)", false, ""},
};

} // namespace gmdr::gui::ui
