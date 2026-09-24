#include "capabilities.hpp"

#include "formats.hpp"

#include "../dub/tts.hpp"
#include "../frames/frame_pipe.hpp"
#include "../game/game_renderer.hpp"
#include "../game/lua_driver.hpp"
#include "../game/process.hpp"
#include "../media/ffmpeg_util.hpp"
#include "../media/video_encoder.hpp"
#include "../render/dubbing.hpp"
#include "../render/jobs.hpp"
#include "../speech/transcribe.hpp"
#include "../translate/translate.hpp"
#include "../util/file_util.hpp"
#include "../util/i18n.hpp"
#include "../util/strings.hpp"
#include "../util/system_info.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <thread>

extern "C" {
#include <libavfilter/avfilter.h>
}

namespace gmdr::config {

namespace fs = std::filesystem;

const char* availability_id(Availability a) {
    switch (a) {
    case Availability::Available: return "available";
    case Availability::Unavailable: return "unavailable";
    case Availability::Failed: return "failed";
    case Availability::NotApplicable: return "notApplicable";
    case Availability::Checking: return "checking";
    default: return "unknown";
    }
}

const GameInstallInfo* GameCapabilities::find(const std::string& renderer) const {
    for (const auto& i : installs)
        if (i.renderer == renderer) return &i;
    return nullptr;
}

CapabilityState EnvironmentCapabilities::video_encoder(const std::string& name) const {
    auto it = encoders.video.find(name);
    if (it != encoders.video.end()) return it->second.state;
    // Не з каталогу (свій енкодер у розширеному режимі): лише чи є у збірці
    const AVCodec* c = avcodec_find_encoder_by_name(name.c_str());
    return c ? CapabilityState{Availability::Available, {}}
             : CapabilityState{Availability::Unavailable, tr("немає в цій збірці FFmpeg")};
}

CapabilityState EnvironmentCapabilities::audio_encoder(const std::string& name) const {
    auto it = encoders.audio.find(name);
    if (it != encoders.audio.end()) return it->second;
    const AVCodec* c = avcodec_find_encoder_by_name(name.c_str());
    return c ? CapabilityState{Availability::Available, {}}
             : CapabilityState{Availability::Unavailable, tr("немає в цій збірці FFmpeg")};
}

namespace {

CapabilityState yes(std::string detail = {}) { return {Availability::Available, std::move(detail)}; }
CapabilityState no(std::string detail = {}) { return {Availability::Unavailable, std::move(detail)}; }

void scan_hardware(EnvironmentCapabilities& env) {
    auto& h = env.hardware;
    h.cpu_threads = std::max(1u, std::thread::hardware_concurrency());
    h.ram_bytes = total_memory();
    bool ok = false;
    for (const auto& g : system_gpus(&ok)) {
        GpuInfo i;
        i.name = g.name;
        i.vendor_id = g.vendor_id;
        i.vendor = gpu_vendor_name(g.vendor_id);
        i.vram_bytes = g.vram_bytes;
        i.driver = g.driver;
        i.software = g.software;
        h.gpus.push_back(std::move(i));
    }
    h.gpu_info = ok ? yes() : CapabilityState{Availability::Unknown, tr("список відеокарт прочитати не вдалося")};
}

void scan_encoders(EnvironmentCapabilities& env) {
    for (const auto& e : video_encoders()) {
        EncoderState st;
        st.gpu = e.gpu;
        st.compiled = avcodec_find_encoder_by_name(e.name.c_str()) != nullptr;
        if (!st.compiled) st.state = no(tr("немає в цій збірці FFmpeg"));
        else if (e.gpu) st.state = {Availability::Unknown, tr("ще не перевірено на цій відеокарті")};
        else st.state = yes();
        env.encoders.video[e.name] = st;
    }
    for (const auto& e : audio_encoders())
        env.encoders.audio[e.name] = avcodec_find_encoder_by_name(e.name.c_str()) ? yes() : no(tr("немає в цій збірці FFmpeg"));

    auto& m = env.media;
    m.ffmpeg_version = std::format("libavcodec {}.{}.{}", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR,
                                   LIBAVCODEC_VERSION_MICRO);
    for (const char* f : {"thumbnail", "palettegen", "paletteuse", "arnndn", "afftdn", "loudnorm", "sidechaincompress", "atempo",
                          "ebur128"})
        m.filters[f] = avfilter_get_by_name(f) ? yes() : no(tr("немає в цій збірці FFmpeg"));
    for (const char* e : {"gif", "libwebp_anim", "libwebp", "mjpeg"})
        m.extra_encoders[e] = avcodec_find_encoder_by_name(e) ? yes() : no(tr("немає в цій збірці FFmpeg"));
    const fs::path model = executable_dir() / "rnnoise-voice.rnnn";
    std::error_code ec;
    m.rnnoise_model = fs::exists(model, ec) ? yes(path_to_utf8(model))
                                            : no(tr("немає моделі RNNoise поруч із програмою — шумодав візьме afftdn"));
}

void scan_game(EnvironmentCapabilities& env, const render::RenderSettings& s, bool processes) {
    auto& g = env.game;
    g.installs.clear();
    for (const auto* r : game::game_renderers()) {
        GameInstallInfo info;
        info.renderer = r->id();
        render::RenderSettings probe = s;
        probe.game_renderer = r->id();
        info.from_settings = !render::renderer_game_dir(probe).empty();
        auto inst = render::locate_game(probe);
        if (inst && inst->valid()) {
            info.install = yes(path_to_utf8(inst->root));
            info.accepted = r->accepts(*inst);
            info.has_64bit = std::any_of(inst->executables.begin(), inst->executables.end(), [](const fs::path& e) {
                return to_lower(path_to_utf8(e.parent_path().filename())) == "win64";
            });
            const auto st = game::driver_state(*inst);
            info.driver = st == game::DriverState::Installed ? "installed" : st == game::DriverState::Outdated ? "outdated" : "missing";
            if (r->id() == render::renderer_of(s).id()) env.filesystem.game_free = free_disk_space(inst->root);
        } else {
            info.install = no(r->not_found_message());
        }
        g.installs.push_back(std::move(info));
    }
    if (processes) {
        const bool running = !game::GameProcess::find_by_name({"gmod.exe", "hl2.exe", "gmod", "hl2_linux"}, true).empty();
        g.running = running ? yes(tr("Garry's Mod уже запущено")) : no();
    }
    g.frame_pipes = frames::frame_pipes_supported() ? yes() : no(tr("канали не підтримуються в цій системі"));
}

void scan_services(EnvironmentCapabilities& env, const render::RenderSettings& s) {
    // Розпізнавання: whisper-cli і модель окремо — «немає програми» і «немає моделі» — різні дії
    speech::WhisperTools found;
    std::string why;
    const auto tools = speech::find_whisper(s.whisper_cli, s.whisper_model, &why, &found);
    env.speech.whisper_cli = found.cli.empty() ? no(why) : yes(path_to_utf8(found.cli));
    if (tools) env.speech.model = yes(path_to_utf8(tools->model.filename()));
    else if (!found.cli.empty()) env.speech.model = no(why);
    else env.speech.model = {Availability::Unknown, tr("спершу потрібен whisper-cli")};

    // Сервіси перекладу: налаштовано — є ключ (де без нього не працює) або адреса
    for (const auto& p : translate::providers()) {
        const std::string id = p.id;
        const std::string* key = render::translator_key_field(s, id);
        const bool has_key = key && !key->empty();
        if (p.needs_key && !has_key) env.translation.providers[id] = no(tr("немає ключа API"));
        else env.translation.providers[id] = yes();
    }
    // Озвучення
    const auto py = dub::engine_python(render::tts_config(s));
    env.dubbing.omnivoice = py ? yes(path_to_utf8(*py)) : no(tr("локальний рушій озвучення не встановлено"));
    env.dubbing.elevenlabs = s.elevenlabs_key.empty() ? no(tr("немає ключа API")) : yes();
    bool nvidia = false;
    for (const auto& gpu : env.hardware.gpus) nvidia = nvidia || gpu.vendor == "nvidia";
    env.dubbing.nvidia_gpu = env.hardware.gpu_info.ok() ? (nvidia ? yes() : no(tr("GPU NVIDIA не знайдено")))
                                                        : CapabilityState{Availability::Unknown, {}};
}

} // namespace

void rescan_paths(EnvironmentCapabilities& env, const render::RenderSettings& s) {
    std::error_code ec;
    auto& f = env.filesystem;
    f.demo = s.demo_path.empty() ? CapabilityState{Availability::NotApplicable, {}}
             : fs::is_regular_file(path_from_utf8(s.demo_path), ec) ? yes()
                                                                     : no(tr("файлу демо немає"));
    f.mic = s.mic_file.empty() ? CapabilityState{Availability::NotApplicable, {}}
            : fs::is_regular_file(path_from_utf8(s.mic_file), ec) ? yes()
                                                                   : no(tr("файлу мікрофона немає"));
    const bool seq = s.output_path.find('%') != std::string::npos;
    f.output_exists = s.output_path.empty() || seq ? CapabilityState{Availability::NotApplicable, {}}
                      : fs::exists(path_from_utf8(s.output_path), ec) ? yes()
                                                                       : no();
    // Вільне місце — на найближчій наявній теці шляху виходу
    f.output_free = 0;
    for (fs::path p = path_from_utf8(s.output_path).parent_path(); !p.empty(); p = p.parent_path()) {
        if (fs::exists(p, ec)) {
            f.output_free = free_disk_space(p);
            break;
        }
        if (p == p.parent_path()) break;
    }
    scan_services(env, s);
}

EnvironmentCapabilities scan_environment(const render::RenderSettings& s, const ScanOptions& opt) {
    EnvironmentCapabilities env;
    env.platform.os = os_id();
    env.platform.os_label = os_description();
    if (opt.hardware) scan_hardware(env);
    scan_encoders(env);
    if (opt.game) scan_game(env, s, opt.processes);
    rescan_paths(env, s);
    return env;
}

CapabilityState probe_video_encoder(const std::string& name) {
    if (!avcodec_find_encoder_by_name(name.c_str())) return no(tr("немає в цій збірці FFmpeg"));
    // Пробне відкриття (помилки FFmpeg під час перевірки не пишемо в журнал)
    const int old_level = av_log_get_level();
    av_log_set_level(AV_LOG_QUIET);
    media::VideoEncoderSettings vs;
    vs.codec = name;
    vs.width = 1280;
    vs.height = 720;
    media::VideoEncoder enc;
    std::string err;
    const bool ok = enc.open(vs, 1280, 720, false, &err);
    av_log_set_level(old_level);
    if (ok) return yes();
    return {Availability::Failed, err.empty() ? tr("кодек не відкрився на цій відеокарті") : err};
}

std::vector<std::string> gpu_encoders_to_probe(const EnvironmentCapabilities& env) {
    std::vector<std::string> out;
    for (const auto& [name, st] : env.encoders.video)
        if (st.gpu && st.compiled && !st.probed) out.push_back(name);
    return out;
}

void set_encoder_probe(EnvironmentCapabilities& env, const std::string& name, const CapabilityState& result) {
    auto& st = env.encoders.video[name];
    st.state = result;
    st.probed = true;
    st.gpu = true;
    st.compiled = result.state != Availability::Unavailable;
    env.encoders.gpu_probe_done = gpu_encoders_to_probe(env).empty();
}

std::string best_gpu_encoder(const EnvironmentCapabilities& env, const std::string& family) {
    for (const char* vendor : {"_nvenc", "_amf", "_qsv"}) {
        const std::string name = family + vendor;
        if (env.video_encoder(name).ok()) return name;
    }
    return {};
}

} // namespace gmdr::config
