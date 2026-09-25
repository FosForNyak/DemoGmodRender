#include "preflight.hpp"

#include "formats.hpp"

#include "../demo/analysis.hpp"
#include "../util/i18n.hpp"

namespace gmdr::config {

ValidationContext context_for(ValidationContext::Purpose purpose, const demo::DemoAnalysis* analysis, int speakers) {
    ValidationContext ctx;
    ctx.purpose = purpose;
    if (analysis && analysis->tick_interval > 0) {
        ctx.demo_known = true;
        ctx.tick_interval = analysis->tick_interval;
        ctx.last_tick = analysis->last_tick;
    }
    ctx.speakers = speakers;
    return ctx;
}

namespace {

EnvironmentCapabilities scan_for(const render::RenderSettings& s, const PreflightOptions& opt, bool all_renderers) {
    ScanOptions scan;
    scan.all_renderers = all_renderers;
    // У черзі гру свідомо лишає відкритою попередній пункт
    scan.processes = opt.purpose != ValidationContext::Purpose::QueueItem;
    return scan_environment(s, scan);
}

void probe_selected(EnvironmentCapabilities& env, const render::RenderSettings& s, const PreflightOptions& opt) {
    if (!opt.probe_gpu || is_image_sequence(s)) return;
    const VideoEncoderInfo* e = find_video_encoder(s.video_codec);
    if (!e || !e->gpu) return;
    auto it = env.encoders.video.find(e->name);
    if (it != env.encoders.video.end() && it->second.probed) return;
    set_encoder_probe(env, e->name, probe_video_encoder(e->name));
}

} // namespace

Preflight preflight(const render::RenderSettings& s, const PreflightOptions& opt) {
    Preflight p;
    p.env = scan_for(s, opt, false);
    if (opt.resume) p.env.filesystem.output_exists = {Availability::NotApplicable, {}};
    probe_selected(p.env, s, opt);
    p.result = evaluate(s, p.env, context_for(opt.purpose, opt.analysis, opt.speakers));
    return p;
}

ValidationResult PreflightCache::check(const render::RenderSettings& s, const PreflightOptions& opt) {
    if (!scanned_) {
        env_ = scan_for(s, opt, true);
        scanned_ = true;
    } else {
        rescan_paths(env_, s);
    }
    probe_selected(env_, s, opt);
    EnvironmentCapabilities env = env_;
    if (opt.resume) env.filesystem.output_exists = {Availability::NotApplicable, {}};
    return evaluate(s, env, context_for(opt.purpose, opt.analysis, opt.speakers));
}

std::string preflight_error(const ValidationResult& r) {
    std::string out = tr("Налаштування не дозволяють почати рендер:");
    for (const auto& i : r.issues)
        if (i.severity == Severity::Error) out += "\n• " + format_issue(i, false);
    return out;
}

} // namespace gmdr::config
