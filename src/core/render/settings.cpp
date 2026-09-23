#include "settings.hpp"

#include "../util/file_util.hpp"
#include "../util/strings.hpp"

namespace gmdr::render {

// Макроси, щоб не писати кожне поле двічі (серіалізація/десеріалізація).
#define GMDR_SETTINGS_FIELDS(X)                                                                       \
    X(demo_path) X(game_dir) X(game_exe) X(render_width) X(render_height) X(capture_format)          \
    X(jpeg_quality) X(hide_hud) X(hide_viewmodel) X(extra_commands) X(extra_launch_args)             \
    X(max_pending_frames) X(quit_game_when_done) X(manual_mode) X(mute_engine_voice) X(menu_delay)   \
    X(high_priority) X(game_window) X(mute_game_sound) X(rtx) X(start_tick) X(end_tick)              \
    X(width) X(height) X(fps) X(motion_blur) X(shutter)                                              \
    X(video_codec) X(pix_fmt) X(bit_depth) X(chroma) X(quality) X(video_bitrate) X(preset)           \
    X(video_options) X(scaler) X(accurate_color) X(full_range) X(gop_seconds) X(audio) X(audio_codec) \
    X(audio_bitrate) X(sample_rate) X(game_audio) X(game_volume) X(game_audio_offset) X(voice_mode)   \
    X(voice_selected) X(voice_volume) X(voice_delay) X(voice_volumes) X(separate_tracks) X(mic_file) \
    X(mic_offset) X(mic_volume) X(voice_level) X(voice_denoise) X(voice_denoise_players) X(duck_game) \
    X(loudness_target) X(output_path) X(container) X(faststart) X(crash_safe)                         \
    X(subtitles_srt) X(chat_srt) X(chapters) X(markers) X(target_size_mb) X(threads) X(keep_temp_files) \
    X(extra_versions)

namespace {
json::Value to_value(const std::string& v) { return json::Value::string(v); }
json::Value to_value(bool v) { return json::Value::boolean(v); }
json::Value to_value(int v) { return json::Value::number(v); }
json::Value to_value(double v) { return json::Value::number(v); }

void from_value(const json::Value& j, std::string& v) { if (!j.is_null()) v = j.as_string(v); }
void from_value(const json::Value& j, bool& v) { if (!j.is_null()) v = j.as_bool(v); }
void from_value(const json::Value& j, int& v) { if (!j.is_null()) v = static_cast<int>(j.as_int(v)); }
void from_value(const json::Value& j, double& v) { if (!j.is_null()) v = j.as_number(v); }
} // namespace

json::Value RenderSettings::to_json() const {
    json::Value j = json::Value::object();
#define X(name) j.set(#name, to_value(name));
    GMDR_SETTINGS_FIELDS(X)
#undef X
    return j;
}

RenderSettings RenderSettings::from_json(const json::Value& j) {
    RenderSettings s;
#define X(name) from_value(j[#name], s.name);
    GMDR_SETTINGS_FIELDS(X)
#undef X
    return s;
}

bool save_settings(const RenderSettings& s, const std::string& path, std::string* error) {
    // Трохи "красивіший" JSON: кожне поле з нового рядка
    std::string text = "{\n";
    const auto obj = s.to_json();
    bool first = true;
    for (const auto& [k, v] : obj.members()) {
        if (!first) text += ",\n";
        first = false;
        text += "  " + json::escape_string(k) + ": " + v.dump();
    }
    text += "\n}\n";
    return write_file_atomic(path_from_utf8(path), text, error);
}

bool load_settings(RenderSettings& s, const std::string& path, std::string* error) {
    auto text = read_file_text(path_from_utf8(path), error);
    if (!text) return false;
    auto j = json::parse(*text, error);
    if (!j) return false;
    s = RenderSettings::from_json(*j);
    return true;
}

} // namespace gmdr::render
