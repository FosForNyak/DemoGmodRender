// =============================================================================
//  app_ui.hpp — спільні дрібниці інтерфейсу для всіх файлів вікна (app.cpp,
//  app_tab_*.cpp, app_panels.cpp): палітра і віджети в стилі Adobe (темна тема
//  Spectrum, як у Premiere Pro і Media Encoder), списки кодеків і форматів.
//  Віджети реалізовано в ui_widgets.cpp.
// =============================================================================
#pragma once

#include "imgui.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace gmdr::gui::ui {

// ---- Кольори тексту (ImVec4 — для TextColored) ----
inline const ImVec4 kColWarn(0.95f, 0.66f, 0.26f, 1.0f);
inline const ImVec4 kColErr(0.97f, 0.43f, 0.45f, 1.0f);
inline const ImVec4 kColDim(0.58f, 0.58f, 0.58f, 1.0f);
inline const ImVec4 kColOk(0.40f, 0.78f, 0.53f, 1.0f);
inline const ImVec4 kColAccent(0.29f, 0.61f, 0.96f, 1.0f);   // «гарячий» синій Adobe

// ---- Палітра панелей (ImU32 — для малювання) ----
inline constexpr ImU32 kGutter = IM_COL32(16, 16, 16, 255);       // проміжки між панелями
inline constexpr ImU32 kPanel = IM_COL32(35, 35, 35, 255);        // тло панелі
inline constexpr ImU32 kPanelLine = IM_COL32(52, 52, 52, 255);    // лінії всередині панелі
inline constexpr ImU32 kField = IM_COL32(27, 27, 27, 255);        // поля, доріжки таймлайну
inline constexpr ImU32 kMonitor = IM_COL32(8, 8, 8, 255);         // тло монітора (кадр)
inline constexpr ImU32 kText = IM_COL32(226, 226, 226, 255);
inline constexpr ImU32 kTextDim = IM_COL32(148, 148, 148, 255);
inline constexpr ImU32 kBlue = IM_COL32(38, 128, 235, 255);       // акцент (кнопка дії, курсор)
inline constexpr ImU32 kBlueHover = IM_COL32(55, 142, 240, 255);
inline constexpr ImU32 kBlueText = IM_COL32(75, 156, 245, 255);   // «гарячі» значення, таймкод
inline constexpr ImU32 kGreen = IM_COL32(45, 157, 120, 255);      // запуск черги (як у Media Encoder)
inline constexpr ImU32 kRed = IM_COL32(215, 58, 73, 255);

// ---- Шрифти (задає App::init) ----
void   set_fonts(ImFont* regular, ImFont* bold);
ImFont* bold_font();
// Тема Adobe для всього вікна: кольори, відступи, заокруглення.
void apply_theme(float dpi);

// ---- Іконки (малюються лініями — однаково чіткі на будь-якому DPI) ----
enum class Icon {
    None, Play, Stop, Record, Menu, ChevronDown, ChevronRight, ChevronUp, Folder, Plus, Close, Search,
    MarkIn, MarkOut, GoToIn, GoToOut, Marker, Speaker, Headphones, Eye, Check, Warning, Info, Film, Queue, Refresh, Pencil,
};
void draw_icon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 col);

// Кнопка-іконка без рамки (підсвічується під курсором). active — увімкнена (синя).
bool icon_button(const char* id, Icon icon, const char* tooltip = nullptr, bool active = false, float size = 0);
// Кнопка з літерою (M/S на доріжках таймлайну): увімкнена — кольоровий квадрат.
bool letter_toggle(const char* id, const char* letter, bool on, ImU32 on_col, const char* tooltip);

// Кнопки-«пігулки» Spectrum: Cta — головна дія (синя), Secondary — контурна, Negative — червона,
// Positive — зелена (запуск черги).
enum class Kind { Cta, Secondary, Negative, Positive };
bool pill_button(const char* label, Kind kind, float width = 0, Icon icon = Icon::None);
float pill_width(const char* label, Kind kind, Icon icon = Icon::None);   // ширина такої кнопки

// Секція з трикутником-розкривачкою (як «fx» в Effect Controls). Повертає true, якщо розкрита.
bool section(const char* label, bool default_open = true);

// Підпис ліворуч від віджета фіксованої ширини (охайніша «форма»).
void label(const char* text, float width = 0);
// Підказка-іконка «?» праворуч від попереднього віджета.
void help_marker(const char* text);
// SameLine перед галочкою з підписом next_label — або новий рядок, якщо вона не влазить
// (англійські підписи бувають довшими за українські).
void same_line_if_fits(const char* next_label);

// Галочка й перемикач у стилі Spectrum (увімкнені — сині).
bool checkbox(const char* label, bool* v);
bool radio(const char* label, bool active);
// Перемикач-«капсула» з кількох варіантів (виділення плавно переїжджає); width 0 — за текстом
bool segmented(const char* id, int* current, std::initializer_list<const char*> labels, float width = 0);
// Випадний список: як ImGui::BeginCombo, але стрілка без окремої «кнопки».
bool begin_combo(const char* id, const char* preview, ImGuiComboFlags flags = 0);

// «Гаряче» значення Adobe: синій текст, тягніть мишею вліво-вправо, подвійний клік — ввести число.
bool hot_float(const char* id, float* v, float speed, float min, float max, const char* fmt);
bool hot_int(const char* id, int* v, float speed, int min, int max, const char* fmt);
// Повзунок Spectrum: тонка доріжка з круглою ручкою і «гарячим» значенням праворуч.
bool slider_float(const char* id, float* v, float min, float max, const char* fmt, float width);
bool slider_int(const char* id, int* v, int min, int max, const char* fmt, float width);

// Тонка смуга прогресу (fraction < 0 — невизначений, «біжить»).
void meter(float fraction, ImVec2 size, ImU32 col = kBlue);
// Поле пошуку з лупою.
bool search_input(const char* id, const char* hint, std::string* text, float width);

// ---- Панелі (замість вікон ImGui: розташування задає App::frame) ----
// Панель із вкладками в заголовку: активна вкладка — біла з синьою рискою, панель у фокусі —
// з синьою рамкою. menu_id — меню «≡» праворуч (відкрити: begin_panel_menu(menu_id)).
// footer_h — висота нижньої смуги (малюється після panel_footer()). Завжди закривати end_panel().
void begin_panel(const char* id, ImVec2 pos, ImVec2 size, const std::vector<std::string>& tabs, int* current,
                 const char* menu_id = nullptr, float footer_h = 0, ImGuiWindowFlags content_flags = 0);
void panel_footer();
void end_panel();
bool begin_panel_menu(const char* menu_id);   // true — меню відкрите (закрити ImGui::EndPopup())
// Роздільник між панелями: перетягування змінює *value (у пікселях).
void splitter(const char* id, bool vertical, ImVec2 pos, ImVec2 size, float* value, float min_v, float max_v);

// Таймкод як у Premiere: 00:01:23:15 (кадри — за частотою fps).
std::string timecode(double seconds, double fps);

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

// Готові набори налаштувань "в один клік" (вкладка "Відео").
struct QuickPreset { const char* label; const char* tip; };
inline constexpr QuickPreset kQuickPresets[] = {
    {"YouTube 1080p60", "1920×1080, 60 кадрів/с, H.264, MP4 — підходить будь-де"},
    {"YouTube 4K60 (10 біт)", "3840×2160, 60 кадрів/с, HEVC 10 біт (на відеокарті, якщо вона вміє), MP4"},
    {"Монтаж — ProRes 422 HQ", "ProRes 422 HQ 10 біт, MOV, звук PCM 24 біт і окремі доріжки — для Premiere/DaVinci Resolve"},
    {"Discord — 10 МБ", "1280×720, 30 кадрів/с, бітрейт розраховується під 10 МБ на весь фрагмент"},
    {"Discord — 50 МБ", "1920×1080, 60 кадрів/с, під 50 МБ"},
    {"Discord Nitro — 500 МБ", "1920×1080, 60 кадрів/с, під 500 МБ"},
    {"Архів без втрат (FFV1)", "FFV1 4:4:4, MKV, звук FLAC — без жодних втрат якості, великий файл"},
};

} // namespace gmdr::gui::ui
