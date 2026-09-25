// =============================================================================
//  voices_service.hpp — те, що потрібно сторінці «ШІ та озвучення» поза правилами
//  налаштувань: мови перекладу (назви), шаблони публікації, бібліотека зразків
//  голосів гравців і видалення локального рушія озвучення. Для QML — Voices.
// =============================================================================
#pragma once

#include <QObject>
#include <QVariantList>

namespace gmdr::qt {

class ConfigModel;
class EnvService;

class VoicesService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)           // {code, native, english}
    Q_PROPERTY(QVariantList templates READ templates CONSTANT)           // {id, label, hint}
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)           // {id, needsKey, usesUrl, usesModel, defaultUrl, local}
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)   // {key, name, samples, seconds, cloned}
    Q_PROPERTY(QString engineDir READ engineDir CONSTANT)
    Q_PROPERTY(QString voicesDir READ voicesDir CONSTANT)

public:
    VoicesService(ConfigModel* config, EnvService* env, QObject* parent = nullptr);

    QVariantList languages() const;
    QVariantList templates() const;
    QVariantList providers() const;
    QVariantList profiles() const { return profiles_; }
    QString      engineDir() const;
    QString      voicesDir() const;

    Q_INVOKABLE void    applyTemplate(const QString& id);   // шаблон публікації + озвучення увімкнено
    Q_INVOKABLE void    refreshProfiles();
    Q_INVOKABLE void    deleteProfile(const QString& key);
    Q_INVOKABLE void    clearProfiles();
    Q_INVOKABLE void    openFolder();
    Q_INVOKABLE QString removeEngine();                     // "" — видалено, інакше помилка

signals:
    void profilesChanged();

private:
    ConfigModel* config_;
    EnvService*  env_;
    QVariantList profiles_;
};

} // namespace gmdr::qt
