#include "voices_service.hpp"

#include "config_model.hpp"
#include "env_service.hpp"
#include "qt_convert.hpp"

#include "core/dub/tts.hpp"
#include "core/dub/voice_library.hpp"
#include "core/render/dubbing.hpp"
#include "core/translate/translate.hpp"
#include "core/util/file_util.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

#include <QDesktopServices>
#include <QUrl>

#include <filesystem>

namespace gmdr::qt {

namespace fs = std::filesystem;

VoicesService::VoicesService(ConfigModel* config, EnvService* env, QObject* parent)
    : QObject(parent), config_(config), env_(env) {
    refreshProfiles();
}

QVariantList VoicesService::languages() const {
    QVariantList out;
    for (const auto& l : translate::languages()) {
        QVariantMap m;
        m["code"] = qs(l.code);
        m["native"] = qs(l.native);
        m["english"] = qs(l.english);
        out << m;
    }
    return out;
}

QVariantList VoicesService::templates() const {
    QVariantList out;
    for (const auto& t : render::publish_templates()) {
        QVariantMap m;
        m["id"] = qs(t.id);
        m["label"] = qs(gmdr::tr(t.label));
        m["hint"] = qs(gmdr::tr(t.hint));
        out << m;
    }
    return out;
}

QVariantList VoicesService::providers() const {
    QVariantList out;
    for (const auto& p : translate::providers()) {
        QVariantMap m;
        m["id"] = qs(p.id);
        m["needsKey"] = p.needs_key;
        m["usesUrl"] = p.uses_url;
        m["usesModel"] = p.uses_model;
        m["defaultUrl"] = qs(p.default_url ? p.default_url : "");
        const std::string id = p.id;
        m["local"] = id == "libre" || id == "openai" || id == "fake";   // може працювати на цьому ПК
        out << m;
    }
    return out;
}

QString VoicesService::engineDir() const { return qs(path_to_utf8(dub::engine_dir())); }
QString VoicesService::voicesDir() const { return qs(path_to_utf8(dub::voices_dir())); }

void VoicesService::applyTemplate(const QString& id) {
    const render::PublishTemplate* t = render::find_template(ss(id));
    if (!t) return;
    config_->modify([t](render::RenderSettings& s) {
        render::apply_template(s, *t);
        s.dub = true;   // шаблони — про те, куди озвучення
    });
}

void VoicesService::refreshProfiles() {
    profiles_.clear();
    for (const auto& p : dub::list_profiles()) {
        QVariantMap m;
        m["key"] = qs(p.key);
        m["name"] = qs(p.name());
        m["samples"] = static_cast<int>(p.samples.size());
        m["seconds"] = p.total_seconds();
        m["cloned"] = !p.elevenlabs_voice_id.empty();
        profiles_ << m;
    }
    emit profilesChanged();
}

void VoicesService::deleteProfile(const QString& key) {
    if (!dub::delete_profile(ss(key))) log_warn("{}", trf("Не вдалося видалити: {}", ss(key)));
    refreshProfiles();
}

void VoicesService::clearProfiles() {
    std::error_code ec;
    fs::remove_all(dub::voices_dir(), ec);
    if (ec) log_warn("{}", trf("Не вдалося видалити: {}", ec.message()));
    refreshProfiles();
}

void VoicesService::openFolder() {
    std::error_code ec;
    fs::create_directories(dub::voices_dir(), ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(voicesDir()));
}

QString VoicesService::removeEngine() {
    std::string err;
    const bool ok = dub::remove_engine(&err);
    if (!ok) log_warn("{}", trf("Не вдалося видалити: {}", err));
    env_->refreshServices();
    return ok ? QString() : qs(err);
}

} // namespace gmdr::qt
