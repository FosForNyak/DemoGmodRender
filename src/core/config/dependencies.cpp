#include "dependencies.hpp"

#include "../game/game_renderer.hpp"
#include "../render/dubbing.hpp"
#include "../translate/translate.hpp"
#include "../util/i18n.hpp"

namespace gmdr::config {

const char* dependency_state_id(DependencyState s) {
    switch (s) {
    case DependencyState::Ready: return "ready";
    case DependencyState::Missing: return "missing";
    case DependencyState::NotConfigured: return "notConfigured";
    case DependencyState::Failed: return "failed";
    default: return "unknown";
    }
}

namespace {

DependencyState from(const CapabilityState& c, DependencyState missing = DependencyState::Missing) {
    switch (c.state) {
    case Availability::Available: return DependencyState::Ready;
    case Availability::Unavailable: return missing;
    case Availability::Failed: return DependencyState::Failed;
    default: return DependencyState::Unknown;
    }
}

} // namespace

std::vector<DependencyStatus> dependencies(const render::RenderSettings& s, const EnvironmentCapabilities& env) {
    std::vector<DependencyStatus> out;
    auto add = [&](DependencyStatus d) { out.push_back(std::move(d)); };

    add({"ffmpeg", "FFmpeg", tr("кодування відео і звуку"), DependencyKind::Bundled,
         env.media.ffmpeg_version.empty() ? DependencyState::Unknown : DependencyState::Ready, env.media.ffmpeg_version, true});

    const std::string selected = render::renderer_of(s).id();
    for (const auto* r : game::game_renderers()) {
        DependencyStatus d{"game." + r->id(), r->label(), r->description(), DependencyKind::Game};
        if (const GameInstallInfo* inst = env.game.find(r->id())) {
            d.state = from(inst->install);
            d.detail = inst->install.detail;
            if (d.state == DependencyState::Ready && !inst->accepted) {
                d.state = DependencyState::Failed;
                d.detail = trf("Схоже, це не копія для «{}»: у папці гри немає того, що їй потрібно.", r->label());
            }
        }
        d.required = r->id() == selected;
        d.action = ActionId::DetectGame;
        d.url = r->install_url();
        add(std::move(d));
    }

    const bool speech = (s.subtitles_srt && s.speech_subtitles) || s.translate_subtitles || s.dub;
    add({"whisper.cli", "whisper-cli", tr("розпізнавання мовлення (текст субтитрів, переклад, озвучення)"),
         DependencyKind::External, from(env.speech.whisper_cli), env.speech.whisper_cli.detail, speech,
         ActionId::InstallWhisperCli});
    add({"whisper.model", tr("Модель розпізнавання"), tr("розпізнавання мовлення"), DependencyKind::External,
         from(env.speech.model), env.speech.model.detail, speech, ActionId::InstallWhisperModel});

    const bool translate = s.translate_subtitles || s.dub;
    for (const auto& p : translate::providers()) {
        if (std::string(p.id) == "fake") continue;
        auto it = env.translation.providers.find(p.id);
        const CapabilityState st = it != env.translation.providers.end() ? it->second : CapabilityState{};
        add({std::string("translate.") + p.id, tr(p.label), tr("переклад субтитрів і озвучення"), DependencyKind::Service,
             from(st, DependencyState::NotConfigured), st.detail, translate && s.translator == p.id,
             ActionId::ConfigureTranslator});
    }

    add({"tts.omnivoice", tr("Локально (OmniVoice)"), tr("озвучення на цьому комп'ютері"), DependencyKind::External,
         from(env.dubbing.omnivoice), env.dubbing.omnivoice.detail, s.dub && s.tts_engine == "omnivoice",
         ActionId::InstallVoiceEngine});
    add({"tts.elevenlabs", "ElevenLabs", tr("онлайн-озвучення"), DependencyKind::Service,
         from(env.dubbing.elevenlabs, DependencyState::NotConfigured), env.dubbing.elevenlabs.detail,
         s.dub && s.tts_engine == "elevenlabs", ActionId::ConfigureElevenLabs});
    add({"gpu.cuda", "NVIDIA CUDA", tr("швидке локальне озвучення на відеокарті"), DependencyKind::External,
         from(env.dubbing.nvidia_gpu), env.dubbing.nvidia_gpu.detail,
         s.dub && s.tts_engine == "omnivoice" && s.tts_device == "cuda"});

    add({"rnnoise.model", tr("Модель шумодава (RNNoise)"), tr("шумодав голосів (без неї — простіший afftdn)"),
         DependencyKind::Bundled, from(env.media.rnnoise_model), env.media.rnnoise_model.detail,
         s.audio && (s.voice_denoise || !s.voice_denoise_players.empty())});
    return out;
}

std::string format_dependency(const DependencyStatus& d) {
    const char* mark = d.state == DependencyState::Ready     ? "✓"
                       : d.state == DependencyState::Unknown ? "?"
                                                             : (d.required ? "✗" : "–");
    std::string line = std::string(mark) + " " + d.label;
    if (!d.detail.empty()) line += " — " + d.detail;
    if (d.required && d.state != DependencyState::Ready) line += tr(" (потрібно для поточних налаштувань)");
    return line;
}

} // namespace gmdr::config
