#include "formats.hpp"

#include "../util/file_util.hpp"
#include "../util/i18n.hpp"
#include "../util/strings.hpp"

namespace gmdr::config {

const std::vector<ContainerInfo>& containers() {
    static const std::vector<ContainerInfo> list = {
        {"mp4", N_("MP4 — найсумісніший"), "", true, true, true},
        {"mkv", N_("MKV (Matroska) — будь-які кодеки"), "", true, true, false},
        {"mov", N_("MOV (QuickTime) — для монтажу"), "", true, true, true},
        {"webm", N_("WebM — VP9/AV1 + Opus"), "", false, true, false},
        {"avi", "AVI", "", false, false, false},
        {"nut", "NUT", "", false, false, false},
        {"png", N_("Кадри PNG (послідовність)"), "png", false, false, false},
        {"tiff", N_("Кадри TIFF (послідовність)"), "tiff", false, false, false},
        {"bmp", N_("Кадри BMP (послідовність)"), "bmp", false, false, false},
        {"jpg", N_("Кадри JPEG (послідовність)"), "mjpeg", false, false, false},
    };
    return list;
}

const ContainerInfo* find_container(const std::string& ext) {
    const std::string e = to_lower(ext);
    const std::string key = e == "m4v" ? "mp4" : e == "matroska" ? "mkv" : e == "jpeg" ? "jpg" : e;
    for (const auto& c : containers())
        if (c.ext == key) return &c;
    return nullptr;
}

std::string container_of(const render::RenderSettings& s) {
    if (!s.container.empty()) return to_lower(s.container);
    const std::string ext = to_lower(path_to_utf8(path_from_utf8(s.output_path).extension()));
    return ext.size() > 1 ? ext.substr(1) : "mp4";
}

bool is_image_sequence(const render::RenderSettings& s) {
    const ContainerInfo* c = find_container(container_of(s));
    return (c && !c->image_codec.empty()) || s.output_path.find('%') != std::string::npos;
}

std::string replace_ext(const std::string& path, const std::string& ext) {
    if (path.empty()) return path;
    fs::path p = path_from_utf8(path);
    p.replace_extension("." + ext);
    return path_to_utf8(p);
}

const std::vector<VideoEncoderInfo>& video_encoders() {
    static const std::vector<VideoEncoderInfo> list = {
        {"libx264", N_("H.264 (x264) — найсумісніший"), "h264", "", false},
        {"libx265", N_("H.265 / HEVC (x265) — менший файл"), "hevc", "", false},
        {"libsvtav1", N_("AV1 (SVT-AV1) — найкраще стиснення"), "av1", "", false},
        {"libaom-av1", N_("AV1 (libaom) — дуже повільно"), "av1", "", false},
        {"libvpx-vp9", N_("VP9 (libvpx) — для WebM/YouTube"), "vp9", "", false},
        {"prores_ks", N_("Apple ProRes — для монтажу"), "prores", "", false},
        {"dnxhd", N_("Avid DNxHR — для монтажу"), "dnxhd", "", false},
        {"ffv1", N_("FFV1 — без втрат (архів)"), "ffv1", "", false},
        {"utvideo", N_("UT Video — без втрат, швидкий"), "utvideo", "", false},
        {"png", N_("PNG — без втрат"), "png", "", false},
        {"tiff", "TIFF", "tiff", "", false},
        {"bmp", "BMP", "bmp", "", false},
        {"mjpeg", "JPEG", "mjpeg", "", false},
        {"h264_nvenc", N_("H.264 — NVIDIA NVENC"), "h264", "nvidia", true},
        {"hevc_nvenc", N_("H.265/HEVC — NVIDIA NVENC"), "hevc", "nvidia", true},
        {"av1_nvenc", N_("AV1 — NVIDIA NVENC (RTX 40+)"), "av1", "nvidia", true},
        {"h264_amf", N_("H.264 — AMD AMF"), "h264", "amd", true},
        {"hevc_amf", N_("H.265/HEVC — AMD AMF"), "hevc", "amd", true},
        {"av1_amf", N_("AV1 — AMD AMF (RX 7000+)"), "av1", "amd", true},
        {"h264_qsv", N_("H.264 — Intel Quick Sync"), "h264", "intel", true},
        {"hevc_qsv", N_("H.265/HEVC — Intel Quick Sync"), "hevc", "intel", true},
        {"av1_qsv", N_("AV1 — Intel Quick Sync (Arc)"), "av1", "intel", true},
        {"h264_vulkan", N_("H.264 — Vulkan"), "h264", "vulkan", true},
        {"hevc_vulkan", N_("H.265/HEVC — Vulkan"), "hevc", "vulkan", true},
        {"av1_vulkan", N_("AV1 — Vulkan"), "av1", "vulkan", true},
        {"h264_d3d12va", N_("H.264 — Direct3D 12"), "h264", "d3d12", true},
        {"hevc_d3d12va", N_("H.265/HEVC — Direct3D 12"), "hevc", "d3d12", true},
        {"h264_vaapi", N_("H.264 — VAAPI (Linux)"), "h264", "vaapi", true},
        {"hevc_vaapi", N_("H.265/HEVC — VAAPI (Linux)"), "hevc", "vaapi", true},
        {"av1_vaapi", N_("AV1 — VAAPI (Linux)"), "av1", "vaapi", true},
    };
    return list;
}

const VideoEncoderInfo* find_video_encoder(const std::string& name) {
    for (const auto& e : video_encoders())
        if (e.name == name) return &e;
    return nullptr;
}

std::string cpu_equivalent(const std::string& encoder) {
    const VideoEncoderInfo* e = find_video_encoder(encoder);
    if (!e) return {};
    if (!e->gpu) return e->name;
    if (e->family == "h264") return "libx264";
    if (e->family == "hevc") return "libx265";
    if (e->family == "av1") return "libsvtav1";
    return {};
}

const std::vector<AudioEncoderInfo>& audio_encoders() {
    static const std::vector<AudioEncoderInfo> list = {
        {"aac", "AAC", false},
        {"libopus", "Opus", false},
        {"flac", N_("FLAC (без втрат)"), true},
        {"pcm_s16le", N_("PCM 16 біт (WAV)"), true},
        {"pcm_s24le", N_("PCM 24 біт"), true},
        {"alac", N_("ALAC (Apple, без втрат)"), true},
        {"libmp3lame", "MP3", false},
        {"libvorbis", "Vorbis", false},
        {"ac3", N_("AC-3 (Dolby Digital)"), false},
    };
    return list;
}

const AudioEncoderInfo* find_audio_encoder(const std::string& name) {
    for (const auto& e : audio_encoders())
        if (e.name == name) return &e;
    return nullptr;
}

bool audio_lossless(const std::string& codec) {
    if (const AudioEncoderInfo* e = find_audio_encoder(codec)) return e->lossless;
    return codec.rfind("pcm_", 0) == 0;
}

const std::vector<ResolutionInfo>& resolutions() {
    static const std::vector<ResolutionInfo> list = {
        {N_("854 × 480 (480p)"), 854, 480},          {N_("1280 × 720 (HD)"), 1280, 720},
        {"1600 × 900", 1600, 900},                  {N_("1920 × 1080 (Full HD)"), 1920, 1080},
        {N_("2560 × 1440 (2K / QHD)"), 2560, 1440}, {N_("3840 × 2160 (4K)"), 3840, 2160},
        {N_("5120 × 2880 (5K)"), 5120, 2880},       {N_("7680 × 4320 (8K)"), 7680, 4320},
        {N_("1080 × 1920 (вертикальне)"), 1080, 1920}, {N_("1080 × 1080 (квадрат)"), 1080, 1080},
    };
    return list;
}

const std::vector<std::string>& frame_rates() {
    static const std::vector<std::string> list = {"24", "25", "30", "48", "50", "60", "90", "120", "144", "165", "240",
                                                  "23.976", "29.97", "59.94"};
    return list;
}

const std::vector<double>& speeds() {
    static const std::vector<double> list = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0};
    return list;
}

const std::vector<double>& loudness_targets() {
    static const std::vector<double> list = {0.0, -14.0, -16.0, -23.0};
    return list;
}

} // namespace gmdr::config
