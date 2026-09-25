#include "presets.hpp"

#include "formats.hpp"

#include "../render/jobs.hpp"
#include "../util/i18n.hpp"
#include "../util/strings.hpp"

namespace gmdr::config {

const std::vector<PresetInfo>& presets() {
    static const std::vector<PresetInfo> list = {
        {"youtube-1080p60", "YouTube 1080p60", N_("1920×1080, 60 кадрів/с, H.264, MP4 — підходить будь-де")},
        {"youtube-4k60", N_("YouTube 4K60 (10 біт)"),
         N_("3840×2160, 60 кадрів/с, HEVC 10 біт (на відеокарті, якщо вона вміє), MP4"), "hevc"},
        {"edit-prores", N_("Монтаж — ProRes 422 HQ"),
         N_("ProRes 422 HQ 10 біт, MOV, звук PCM 24 біт і окремі доріжки — для Premiere/DaVinci Resolve")},
        {"discord-10mb", N_("Discord — 10 МБ"), N_("1280×720, 30 кадрів/с, бітрейт розраховується під 10 МБ на весь фрагмент")},
        {"discord-50mb", N_("Discord — 50 МБ"), N_("1920×1080, 60 кадрів/с, під 50 МБ")},
        {"discord-500mb", N_("Discord Nitro — 500 МБ"), N_("1920×1080, 60 кадрів/с, під 500 МБ")},
        {"archive-ffv1", N_("Архів без втрат (FFV1)"), N_("FFV1 4:4:4, MKV, звук FLAC — без жодних втрат якості, великий файл")},
    };
    return list;
}

const PresetInfo* find_preset(const std::string& id) {
    for (const auto& p : presets())
        if (p.id == id) return &p;
    return nullptr;
}

void set_container(render::RenderSettings& s, const std::string& ext) {
    s.container.clear();
    fs::path p = s.output_path.empty() && !s.demo_path.empty() ? path_from_utf8(render::default_output_path(s.demo_path, ext))
                                                               : path_from_utf8(s.output_path);
    if (p.empty()) p = path_from_utf8("video." + ext);
    const bool was_seq = s.output_path.find('%') != std::string::npos;
    const ContainerInfo* c = find_container(ext);
    if (c && !c->image_codec.empty()) {
        s.video_codec = c->image_codec;
        const fs::path base = was_seq ? p.parent_path().parent_path() : p.parent_path();
        const std::string stem = was_seq ? path_to_utf8(p.parent_path().filename()) : path_to_utf8(p.stem()) + "_frames";
        s.output_path = path_to_utf8(base / stem / ("frame_%06d." + ext));
        return;
    }
    if (was_seq) {
        std::string stem = path_to_utf8(p.parent_path().filename());
        if (stem.size() > 7 && stem.substr(stem.size() - 7) == "_frames") stem.resize(stem.size() - 7);
        p = p.parent_path().parent_path() / (stem + "." + ext);
    } else {
        p.replace_extension("." + ext);
    }
    s.output_path = path_to_utf8(p);
}

render::RenderSettings apply_preset(const render::RenderSettings& in, const std::string& id, const EnvironmentCapabilities& env) {
    render::RenderSettings s = in;
    auto base = [&](int w, int h, const std::string& fps) {
        s.width = w;
        s.height = h;
        s.fps = fps;
        s.render_width = s.render_height = 0;
        s.quality = -1;
        s.preset.clear();
        s.video_bitrate.clear();
        s.video_options.clear();
        s.pix_fmt = "auto";
        s.target_size_mb = 0;
        s.chroma = 420;
        s.bit_depth = 8;
        s.separate_tracks = false;
    };
    if (id == "youtube-1080p60") {
        base(1920, 1080, "60");
        set_container(s, "mp4");
        s.video_codec = "libx264";
        s.preset = "slow";
        s.audio_codec = "aac";
        s.audio_bitrate = "320k";
    } else if (id == "youtube-4k60") {
        base(3840, 2160, "60");
        set_container(s, "mp4");
        const std::string gpu = best_gpu_encoder(env, "hevc");
        s.video_codec = gpu.empty() ? "libx265" : gpu;
        s.bit_depth = 10;
        s.audio_codec = "aac";
        s.audio_bitrate = "320k";
    } else if (id == "edit-prores") {
        base(in.width, in.height, in.fps);
        set_container(s, "mov");
        s.video_codec = "prores_ks";
        s.quality = 3;
        s.chroma = 422;
        s.bit_depth = 10;
        s.audio_codec = "pcm_s24le";
        s.separate_tracks = true;
    } else if (id == "discord-10mb" || id == "discord-50mb" || id == "discord-500mb") {
        const bool small = id == "discord-10mb";
        base(small ? 1280 : 1920, small ? 720 : 1080, small ? "30" : "60");
        set_container(s, "mp4");
        s.video_codec = "libx264";
        s.preset = "slow";
        s.audio_codec = "aac";
        s.audio_bitrate = small ? "96k" : "160k";
        s.target_size_mb = small ? 10 : id == "discord-50mb" ? 50 : 500;
    } else if (id == "archive-ffv1") {
        base(in.width, in.height, in.fps);
        set_container(s, "mkv");
        s.video_codec = "ffv1";
        s.chroma = 444;
        s.audio_codec = "flac";
    }
    return s;
}

} // namespace gmdr::config
