// =============================================================================
//  app_ui.hpp — спільні дрібниці інтерфейсу (кольори, підписи, списки кодеків і
//  форматів) для всіх файлів вікна: app.cpp, app_tab_*.cpp, app_panels.cpp.
// =============================================================================
#pragma once

#include "imgui.h"

#include <string>

namespace gmdr::gui::ui {

// ---- Кольори ----
inline const ImVec4 kColWarn(1.00f, 0.80f, 0.30f, 1.0f);
inline const ImVec4 kColErr(1.00f, 0.42f, 0.42f, 1.0f);
inline const ImVec4 kColDim(0.60f, 0.62f, 0.66f, 1.0f);
inline const ImVec4 kColOk(0.45f, 0.85f, 0.50f, 1.0f);
inline const ImVec4 kColAccent(0.35f, 0.65f, 1.00f, 1.0f);

// Підказка "(?)" праворуч від попереднього віджета.
inline void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Підпис ліворуч від віджета фіксованої ширини (охайніша "форма").
inline void label(const char* text, float width = 0) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(width > 0 ? width : ImGui::GetFontSize() * 11.0f);
}

// SameLine перед галочкою з підписом next_label — або новий рядок, якщо вона не влазить
// (англійські підписи бувають довшими за українські).
inline void same_line_if_fits(const char* next_label) {
    const float w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                    ImGui::CalcTextSize(next_label, nullptr, true).x;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
}

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
