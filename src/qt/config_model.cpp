#include "config_model.hpp"

#include "qt_convert.hpp"

#include "core/config/formats.hpp"
#include "core/config/presets.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/secret.hpp"
#include "core/util/strings.hpp"

namespace gmdr::qt {

using config::Severity;
using config::SettingId;

ConfigModel::ConfigModel(QObject* parent) : QObject(parent) {
    save_timer_.setSingleShot(true);
    save_timer_.setInterval(1200);
    connect(&save_timer_, &QTimer::timeout, this, [this] { save_now(); });
    evaluate();
}

ConfigModel::~ConfigModel() {
    if (save_timer_.isActive()) save_now();
}

bool ConfigModel::load(const std::string& path, std::string* error) {
    path_ = path;
    const bool ok = render::load_settings(s_, path, error);
    evaluate();
    return ok;
}

void ConfigModel::save_now() {
    save_timer_.stop();
    if (path_.empty()) return;
    std::string err;
    if (!render::save_settings(s_, path_, &err)) log_debug("Не вдалося зберегти налаштування: {}", err);
}

void ConfigModel::schedule_save() { save_timer_.start(); }

void ConfigModel::modify(const std::function<void(render::RenderSettings&)>& f) {
    f(s_);
    evaluate();
    schedule_save();
}

void ConfigModel::set_environment(const config::EnvironmentCapabilities& env) {
    env_ = env;
    evaluate();
}

void ConfigModel::set_context(const config::ValidationContext& ctx) {
    ctx_ = ctx;
    evaluate();
}

void ConfigModel::evaluate() {
    r_ = config::evaluate(s_, env_, ctx_);
    ++revision_;
    emit changed();
}

void ConfigModel::setAdvanced(bool on) {
    if (s_.ui_advanced == on) return;
    s_.ui_advanced = on;
    schedule_save();
    ++revision_;
    emit changed();
}

void ConfigModel::setDeveloper(bool on) {
    if (developer_ == on) return;
    developer_ = on;
    ++revision_;
    emit changed();
}

QVariant ConfigModel::value(const QString& key) const {
    const auto id = config::find_setting(ss(key));
    return id ? to_variant(config::get_setting(s_, *id)) : QVariant();
}

bool ConfigModel::set(const QString& key, const QVariant& v) {
    const auto id = config::find_setting(ss(key));
    if (!id) {
        log_warn("Невідоме налаштування: {}", ss(key));
        return false;
    }
    const json::Value jv = to_json(v, config::setting_info(*id).type);
    if (jv.is_null()) return false;
    if (config::get_setting(s_, *id).dump() == jv.dump()) return true;   // не змінилось
    if (!config::set_setting(s_, *id, jv)) return false;
    evaluate();
    schedule_save();
    emit settingChanged(key);
    return true;
}

bool ConfigModel::setSecret(const QString& key, const QString& plain) {
    const auto id = config::find_setting(ss(key));
    if (!id || !config::setting_info(*id).secret) return false;
    const std::string t = trim(ss(plain));
    return set(key, t.empty() ? QString() : qs(protect_secret(t)));
}

QVariantMap ConfigModel::state(const QString& key) const {
    QVariantMap m;
    const auto id = config::find_setting(ss(key));
    if (!id) return m;
    const config::SettingState& st = r_.state(*id);
    m["visible"] = st.visible;
    m["enabled"] = st.enabled;
    m["reason"] = qs(st.reason);
    m["note"] = qs(st.note);
    if (st.min) m["min"] = *st.min;
    if (st.max) m["max"] = *st.max;
    QVariantList opts;
    for (const auto& o : st.options) {
        QVariantMap om;
        om["value"] = qs(o.value);
        om["label"] = qs(o.label);
        om["available"] = o.available;
        om["reason"] = qs(o.reason);
        om["group"] = qs(o.group);
        om["recommended"] = o.recommended;
        om["warning"] = o.warning;
        opts << om;
    }
    m["options"] = opts;
    // Значок — найсерйозніше зауваження з цим полем (головним чи пов'язаним); текст під полем —
    // лише зауваження, де це поле головне (інакше одне й те саме повторювалось би в кількох рядках)
    int worst = -1;
    QStringList all, own;
    for (const auto* i : r_.issues_for(*id)) {
        worst = std::max(worst, static_cast<int>(i->severity));
        all << qs(i->message);
        if (i->setting == *id) own << qs(i->message);
    }
    m["severity"] = worst < 0 ? QString() : QString(config::severity_id(static_cast<Severity>(worst)));
    m["messages"] = all;
    m["ownMessages"] = own;
    return m;
}

QString ConfigModel::label(const QString& key) const {
    const auto id = config::find_setting(ss(key));
    if (!id) return key;
    const std::string& l = config::setting_info(*id).label;
    return l.empty() ? key : qs(gmdr::tr(l.c_str()));
}

bool ConfigModel::isAdvancedSetting(const QString& key) const {
    const auto id = config::find_setting(ss(key));
    return id && config::setting_info(*id).advanced;
}

QVariantMap ConfigModel::issue_map(const config::Issue& i, int index) const {
    QVariantMap m;
    m["index"] = index;
    m["rule"] = qs(i.rule);
    m["severity"] = config::severity_id(i.severity);
    m["kind"] = config::kind_id(i.kind);
    m["setting"] = i.setting == SettingId::Count ? QString() : qs(config::setting_key(i.setting));
    QStringList related;
    for (auto r : i.related) related << qs(config::setting_key(r));
    m["related"] = related;
    m["message"] = qs(i.message);
    m["explanation"] = qs(i.explanation);
    QVariantList fixes;
    for (const auto& f : i.fixes) fixes << qs(f.label);
    m["fixes"] = fixes;
    m["action"] = config::action_id(i.action);
    return m;
}

QVariantList ConfigModel::issues() const {
    QVariantList out;
    for (size_t k = 0; k < r_.issues.size(); ++k) out << issue_map(r_.issues[k], static_cast<int>(k));
    return out;
}

QVariantList ConfigModel::issuesFor(const QString& key) const {
    QVariantList out;
    const auto id = config::find_setting(ss(key));
    if (!id) return out;
    for (size_t k = 0; k < r_.issues.size(); ++k) {
        const auto& i = r_.issues[k];
        if (i.setting == *id || std::find(i.related.begin(), i.related.end(), *id) != i.related.end())
            out << issue_map(i, static_cast<int>(k));
    }
    return out;
}

void ConfigModel::applyFix(int issue, int fix) {
    if (issue < 0 || issue >= static_cast<int>(r_.issues.size())) return;
    const auto& fixes = r_.issues[issue].fixes;
    if (fix < 0 || fix >= static_cast<int>(fixes.size())) return;
    const config::Fix f = fixes[fix];   // копія: evaluate() перебудує список
    s_ = config::apply_fix(s_, f);
    log_info("{}", trf("Виправлено: {}", f.label));
    evaluate();
    schedule_save();
    emit fixApplied(qs(f.label));
}

QString ConfigModel::summary() const { return qs(config::summary_text(r_)); }

QVariantMap ConfigModel::derived() const {
    const config::Derived& d = r_.derived;
    QVariantMap m;
    m["renderer"] = qs(d.renderer);
    m["container"] = qs(d.container);
    m["imageSequence"] = d.image_sequence;
    m["fps"] = d.fps;
    m["subframes"] = d.subframes;
    m["speed"] = d.speed;
    m["gameFps"] = d.game_fps;
    m["demoSeconds"] = d.demo_seconds;
    m["videoSeconds"] = d.video_seconds;
    m["videoFrames"] = static_cast<double>(d.video_frames);
    m["gameFrames"] = static_cast<double>(d.game_frames);
    m["width"] = d.width;
    m["height"] = d.height;
    m["gameWidth"] = d.game_width;
    m["gameHeight"] = d.game_height;
    m["pixFmt"] = qs(d.pix_fmt);
    m["bitDepth"] = d.bit_depth;
    m["chroma"] = d.chroma;
    m["parallel"] = d.parallel;
    m["parallelMax"] = d.parallel_max;
    m["parallelReason"] = qs(d.parallel_reason);
    m["windowMode"] = qs(d.window_mode);
    m["videoBitrate"] = static_cast<double>(d.video_bitrate);
    m["audioBitrate"] = static_cast<double>(d.audio_bitrate);
    m["estimatedBytes"] = static_cast<double>(d.estimated_bytes);
    m["audioInFile"] = d.audio_in_file;
    m["extraOutputs"] = qs_list(d.extra_outputs);
    return m;
}

QVariantList ConfigModel::presets() const {
    QVariantList out;
    for (const auto& p : config::presets()) {
        QVariantMap m;
        m["id"] = qs(p.id);
        m["label"] = qs(gmdr::tr(p.label.c_str()));
        m["description"] = qs(gmdr::tr(p.description.c_str()));
        out << m;
    }
    return out;
}

void ConfigModel::applyPreset(const QString& id) {
    const config::PresetInfo* p = config::find_preset(ss(id));
    if (!p) return;
    s_ = config::apply_preset(s_, p->id, env_);
    log_info("{}", trf("Пресет «{}»: {}", gmdr::tr(p->label.c_str()), gmdr::tr(p->description.c_str())));
    evaluate();
    schedule_save();
    emit settingChanged(QStringLiteral("output_path"));
}

void ConfigModel::setContainer(const QString& ext) {
    config::set_container(s_, ss(ext));
    evaluate();
    schedule_save();
    emit settingChanged(QStringLiteral("output_path"));
}

void ConfigModel::resetToDefaults() {
    // Демо, вихід, папки гри й уподобання вікна лишаються
    render::RenderSettings d;
    d.demo_path = s_.demo_path;
    d.output_path = s_.output_path;
    d.game_dir = s_.game_dir;
    d.rtx_game_dir = s_.rtx_game_dir;
    d.markers = s_.markers;
    d.ui_language = s_.ui_language;
    d.ui_theme = s_.ui_theme;
    d.ui_accent = s_.ui_accent;
    d.ui_scale = s_.ui_scale;
    d.ui_advanced = s_.ui_advanced;
    d.ui_graphics_api = s_.ui_graphics_api;
    d.deepl_key = s_.deepl_key;
    d.google_key = s_.google_key;
    d.libre_key = s_.libre_key;
    d.openai_key = s_.openai_key;
    d.elevenlabs_key = s_.elevenlabs_key;
    s_ = d;
    evaluate();
    schedule_save();
}

QVariantList ConfigModel::rules() const {
    QVariantList out;
    for (const auto& r : config::rule_list()) {
        QVariantMap m;
        m["id"] = qs(r.id);
        m["description"] = qs(r.description);
        out << m;
    }
    return out;
}

} // namespace gmdr::qt
