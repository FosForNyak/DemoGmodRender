// =============================================================================
//  config_model.hpp — налаштування рендеру для QML (синглтон Config).
//
//  QML не знає правил: питає значення (value), стан поля (state — видиме,
//  доступне, дозволені значення з причинами, межі, зауваження), зауваження
//  (issues з виправленнями) і похідні величини (derived). Будь-яка зміна
//  (set, applyFix, пресет) — одразу перевірка тими самими правилами, що в CLI і
//  перед рендером (config::evaluate), і сигнал changed(). Прив'язки в QML
//  залежать від revision, тож оновлюються після кожної перевірки.
//
//  Налаштування зберігаються у gmdr_settings.json (як і раніше) із затримкою.
// =============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <string>

#include "core/config/constraints.hpp"

namespace gmdr::qt {

class ConfigModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantList issues READ issues NOTIFY changed)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY changed)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantMap derived READ derived NOTIFY changed)
    Q_PROPERTY(bool advanced READ advanced WRITE setAdvanced NOTIFY changed)
    Q_PROPERTY(bool developer READ developer WRITE setDeveloper NOTIFY changed)

public:
    explicit ConfigModel(QObject* parent = nullptr);
    ~ConfigModel() override;

    bool load(const std::string& path, std::string* error);
    void save_now();

    const render::RenderSettings& settings() const { return s_; }
    // Змінити налаштування з C++ (позначки, фрагмент...): f змінює, потім перевірка і збереження
    void modify(const std::function<void(render::RenderSettings&)>& f);
    const config::ValidationResult& result() const { return r_; }
    const config::EnvironmentCapabilities& environment() const { return env_; }
    void set_environment(const config::EnvironmentCapabilities& env);
    void set_context(const config::ValidationContext& ctx);

    int          revision() const { return revision_; }
    QVariantList issues() const;
    int          errorCount() const { return r_.count(config::Severity::Error); }
    int          warningCount() const { return r_.count(config::Severity::Warning); }
    QString      summary() const;
    QVariantMap  derived() const;
    bool         advanced() const { return s_.ui_advanced; }
    void         setAdvanced(bool on);
    bool         developer() const { return developer_; }
    void         setDeveloper(bool on);

    Q_INVOKABLE QVariant    value(const QString& key) const;
    Q_INVOKABLE bool        set(const QString& key, const QVariant& v);
    // API-ключ: зберігається лише зашифрованим (util/secret.hpp), порожній — видалити
    Q_INVOKABLE bool        setSecret(const QString& key, const QString& plain);
    // Стан поля: visible, enabled, reason, options [{value, label, available, reason, group,
    // recommended, warning}], min, max, note, severity ("error"/"warning"/"info"/""), messages
    Q_INVOKABLE QVariantMap state(const QString& key) const;
    Q_INVOKABLE QString     label(const QString& key) const;
    Q_INVOKABLE bool        isAdvancedSetting(const QString& key) const;
    // Зауваження з цим полем (головним чи пов'язаним)
    Q_INVOKABLE QVariantList issuesFor(const QString& key) const;
    Q_INVOKABLE void        applyFix(int issue, int fix);
    Q_INVOKABLE QVariantList presets() const;
    Q_INVOKABLE void        applyPreset(const QString& id);
    Q_INVOKABLE void        setContainer(const QString& ext);
    Q_INVOKABLE void        resetToDefaults();
    Q_INVOKABLE QVariantList rules() const;   // для розробника: ідентифікатори й описи правил

signals:
    void changed();
    void settingChanged(const QString& key);
    void fixApplied(const QString& label);

private:
    void evaluate();
    void schedule_save();
    QVariantMap issue_map(const config::Issue& i, int index) const;

    render::RenderSettings          s_;
    std::string                     path_;
    config::EnvironmentCapabilities env_;
    config::ValidationContext       ctx_;
    config::ValidationResult        r_;
    int                             revision_ = 0;
    bool                            developer_ = false;
    QTimer                          save_timer_;
};

} // namespace gmdr::qt
