#include "constraints.hpp"

#include "constraints_internal.hpp"
#include "formats.hpp"

#include "../game/game_renderer.hpp"
#include "../game/process.hpp"
#include "../media/ffmpeg_util.hpp"
#include "../media/video_encoder.hpp"
#include "../render/jobs.hpp"
#include "../render/parallel.hpp"
#include "../util/i18n.hpp"
#include "../util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace gmdr::config {

const char* severity_id(Severity s) {
    switch (s) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    default: return "info";
    }
}

const char* kind_id(IssueKind k) {
    switch (k) {
    case IssueKind::Availability: return "availability";
    case IssueKind::Dependency: return "dependency";
    case IssueKind::Conflict: return "conflict";
    case IssueKind::Range: return "range";
    case IssueKind::Resource: return "resource";
    case IssueKind::Runtime: return "runtime";
    case IssueKind::Platform: return "platform";
    case IssueKind::External: return "external";
    case IssueKind::Renderer: return "renderer";
    default: return "consequence";
    }
}

const char* action_id(ActionId a) {
    switch (a) {
    case ActionId::OpenDemo: return "openDemo";
    case ActionId::DetectGame: return "detectGame";
    case ActionId::InstallWhisperModel: return "installWhisperModel";
    case ActionId::InstallWhisperCli: return "installWhisperCli";
    case ActionId::InstallVoiceEngine: return "installVoiceEngine";
    case ActionId::ConfigureTranslator: return "configureTranslator";
    case ActionId::ConfigureElevenLabs: return "configureElevenLabs";
    case ActionId::ConfirmVoiceConsent: return "confirmVoiceConsent";
    case ActionId::CloseGame: return "closeGame";
    default: return "";
    }
}

int ValidationResult::count(Severity s) const {
    return static_cast<int>(std::count_if(issues.begin(), issues.end(), [&](const Issue& i) { return i.severity == s; }));
}

std::vector<const Issue*> ValidationResult::issues_for(SettingId id) const {
    std::vector<const Issue*> out;
    for (const auto& i : issues)
        if (i.setting == id || std::find(i.related.begin(), i.related.end(), id) != i.related.end()) out.push_back(&i);
    return out;
}

const Issue* ValidationResult::find(const std::string& rule) const {
    for (const auto& i : issues)
        if (i.rule == rule) return &i;
    return nullptr;
}

namespace detail {

Issue& RuleContext::add(Severity sev, IssueKind kind, SettingId setting, std::string message, std::string explanation,
                        std::vector<SettingId> related) {
    Issue i;
    i.rule = rule;
    i.severity = sev;
    i.kind = kind;
    i.setting = setting;
    i.related = std::move(related);
    i.message = std::move(message);
    i.explanation = std::move(explanation);
    r.issues.push_back(std::move(i));
    return r.issues.back();
}

void RuleContext::hide(SettingId id, const std::string& why) {
    auto& st = state(id);
    st.visible = false;
    if (st.reason.empty()) st.reason = why;
}

void RuleContext::disable(SettingId id, const std::string& why) {
    auto& st = state(id);
    st.enabled = false;
    if (st.reason.empty()) st.reason = why;
}

Derived compute_derived(const render::RenderSettings& s, const EnvironmentCapabilities& env, const ValidationContext& ctx) {
    Derived d;
    const game::GameRenderer& R = render::renderer_of(s);
    d.renderer = R.id();
    d.container = container_of(s);
    d.image_sequence = is_image_sequence(s);
    if (auto fr = parse_rational(s.fps); fr && fr->value() > 0 && std::isfinite(fr->value())) d.fps = fr->value();
    d.subframes = std::clamp(s.motion_blur, 1, 256);
    d.speed = s.speed > 0 ? std::clamp(s.speed, 0.1, 16.0) : 1.0;
    d.game_fps = d.fps * d.subframes / d.speed;
    if (ctx.demo_known && ctx.tick_interval > 0) {
        const int32_t a = std::max(0, s.start_tick);
        const int32_t b = s.end_tick > 0 ? std::min(s.end_tick, ctx.last_tick) : ctx.last_tick;
        d.demo_seconds = std::max(0, b - a) * ctx.tick_interval;
        d.video_seconds = d.demo_seconds / d.speed;
        d.video_frames = std::llround(d.video_seconds * d.fps);
        d.game_frames = d.video_frames * d.subframes;
    }

    // Формат пікселів — той самий вибір, що зробить кодер (media::choose_pix_fmt)
    d.width = std::clamp(s.width, 16, 16384);
    d.height = std::clamp(s.height, 16, 16384);
    d.bit_depth = s.bit_depth;
    d.chroma = s.chroma;
    if (const AVCodec* codec = avcodec_find_encoder_by_name(s.video_codec.c_str())) {
        const bool forced_ok = s.pix_fmt.empty() || s.pix_fmt == "auto" || av_get_pix_fmt(s.pix_fmt.c_str()) != AV_PIX_FMT_NONE;
        const AVPixelFormat f = media::choose_pix_fmt(codec, s.bit_depth, s.chroma, forced_ok ? s.pix_fmt : "auto");
        if (f != AV_PIX_FMT_NONE) {
            d.pix_fmt = av_get_pix_fmt_name(f) ? av_get_pix_fmt_name(f) : "";
            d.bit_depth = media::pix_fmt_bit_depth(f);
            d.chroma = media::pix_fmt_chroma(f);
            if (const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(f)) {
                if (desc->log2_chroma_w && (d.width & 1)) d.width -= 1;
                if (desc->log2_chroma_h && (d.height & 1)) d.height -= 1;
            }
        }
    }
    d.game_width = s.render_width > 0 && s.render_height > 0 ? s.render_width : s.width;
    d.game_height = s.render_width > 0 && s.render_height > 0 ? s.render_height : s.height;

    // Скільки копій гри справді рендеритимуть (та сама логіка, що в RenderJob::run)
    d.parallel_max = std::max(1, std::min(4, R.traits().max_parallel));
    const int want = std::clamp(s.parallel_games, 1, 4);
    d.parallel = want;
    if (want > 1) {
        if (ctx.purpose == ValidationContext::Purpose::TestRun) d.parallel_reason = tr("тестовий прогін");
        else if (ctx.purpose == ValidationContext::Purpose::QueueItem) d.parallel_reason = tr("пункт черги");
        else if (s.manual_mode) d.parallel_reason = tr("ручний режим");
        if (!d.parallel_reason.empty()) d.parallel = 1;
        if (d.parallel > d.parallel_max) {
            d.parallel = d.parallel_max;
            d.parallel_reason = d.parallel_max == 1 ? trf("{} рендерить однією копією гри", R.label())
                                                    : trf("{} рендерить не більше {} копій гри", R.label(), d.parallel_max);
        }
        if (d.parallel > 1) {
            const ContainerInfo* c = find_container(d.container);
            if (d.image_sequence || !c || !c->joinable) {
                d.parallel = 1;
                d.parallel_reason = tr("формат файлу — лише MP4, MOV, MKV або WebM");
            } else if (ctx.demo_known && d.video_seconds < 2 * render::min_part_seconds()) {
                d.parallel = 1;
                d.parallel_reason = trf("фрагмент коротший за {:.0f} с", 2 * render::min_part_seconds());
            }
        }
    }

    // Вікно гри: рендерер може не малювати за межами екрана
    d.window_mode = s.manual_mode ? "normal" : s.game_window;
    if (!R.traits().offscreen_window && d.window_mode == "offscreen") d.window_mode = "behind";

    // Звук і бітрейт
    d.audio_in_file = s.audio && !d.image_sequence;
    if (d.audio_in_file && !audio_lossless(s.audio_codec)) d.audio_bitrate = parse_bitrate(s.audio_bitrate).value_or(0);
    const media::QualityInfo qi = media::quality_info_for(s.video_codec);
    if (s.target_size_mb > 0 && qi.param != "profile" && d.video_seconds > 0)
        d.video_bitrate = render::bitrate_for_target_size(s.target_size_mb, d.video_seconds, d.audio_bitrate);
    else if (!s.video_bitrate.empty())
        d.video_bitrate = parse_bitrate(s.video_bitrate).value_or(0);
    if (d.video_bitrate > 0 && d.video_seconds > 0)
        d.estimated_bytes = static_cast<uint64_t>((d.video_bitrate + d.audio_bitrate) * d.video_seconds / 8.0);

    for (const auto& raw : split(s.extra_versions, ',')) {
        const std::string id = trim(raw);
        if (!id.empty()) d.extra_outputs.push_back(id);
    }
    (void)env;
    return d;
}

} // namespace detail

ValidationResult evaluate(const render::RenderSettings& s, const EnvironmentCapabilities& env, const ValidationContext& ctx) {
    ValidationResult r;
    r.states.resize(static_cast<size_t>(SettingId::Count));
    r.derived = detail::compute_derived(s, env, ctx);
    detail::RuleContext rc{s, env, ctx, r, {}};
    for (const auto& rule : detail::rules()) {
        rc.rule = rule.id;
        rule.fn(rc);
    }
    // Спершу помилки, потім попередження, потім інформація (порядок правил усередині — як є)
    std::stable_sort(r.issues.begin(), r.issues.end(),
                     [](const Issue& a, const Issue& b) { return static_cast<int>(a.severity) > static_cast<int>(b.severity); });
    return r;
}

render::RenderSettings apply_fix(const render::RenderSettings& s, const Fix& fix) {
    render::RenderSettings out = s;
    for (const auto& c : fix.changes) set_setting(out, c.id, c.value);
    return out;
}

std::vector<RuleInfo> rule_list() {
    std::vector<RuleInfo> out;
    for (const auto& r : detail::rules()) out.push_back({r.id, tr(r.description)});
    return out;
}

std::string format_issue(const Issue& i, bool with_severity) {
    std::string s = with_severity ? std::string(i.severity == Severity::Error     ? tr("Помилка")
                                                : i.severity == Severity::Warning ? tr("Попередження")
                                                                                  : tr("Інформація")) +
                                        ": " + i.message
                                  : i.message;
    if (!i.explanation.empty()) s += " " + i.explanation;
    if (!i.fixes.empty()) {
        s += tr(" Виправлення: ");
        for (size_t k = 0; k < i.fixes.size(); ++k) s += (k ? "; " : "") + i.fixes[k].label;
        s += ".";
    }
    return s;
}

std::string summary_text(const ValidationResult& r) {
    const int e = r.count(Severity::Error), w = r.count(Severity::Warning);
    if (e == 0 && w == 0) return tr("Усе гаразд");
    std::string out;
    if (e > 0) out = trf("помилок: {}", e);
    if (w > 0) out += (out.empty() ? "" : ", ") + trf("попереджень: {}", w);
    return out;
}

} // namespace gmdr::config
