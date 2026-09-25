// =============================================================================
//  env_service.hpp — середовище для QML (синглтон Env): що вміє цей комп'ютер.
//
//  Огляд (config::scan_environment) і проба GPU-кодеків ідуть у фоні — вікно не
//  чекає. Кожен результат передається в ConfigModel, і правила перераховуються.
//  Тут же — графічні API вікна (перевірка ініціалізації на вимогу), відомості про
//  систему для «Налаштування → Система» і складники (dependencies).
// =============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/config/capabilities.hpp"

class QQuickWindow;

namespace gmdr::qt {

class ConfigModel;

class EnvService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(bool probingGpu READ probingGpu NOTIFY changed)
    Q_PROPERTY(bool probingGraphics READ probingGraphics NOTIFY changed)
    Q_PROPERTY(QVariantList gpus READ gpus NOTIFY changed)
    Q_PROPERTY(QVariantList gpuEncoders READ gpuEncoders NOTIFY changed)
    Q_PROPERTY(QVariantList graphicsApis READ graphicsApis NOTIFY changed)
    Q_PROPERTY(QString activeGraphicsApi READ activeGraphicsApi NOTIFY changed)
    Q_PROPERTY(QString startupGraphicsApi READ startupGraphicsApi CONSTANT)
    Q_PROPERTY(QString recoveredGraphicsApi READ recoveredGraphicsApi CONSTANT)
    Q_PROPERTY(QVariantMap system READ system NOTIFY changed)
    Q_PROPERTY(QVariantList dependencies READ dependencies NOTIFY changed)
    Q_PROPERTY(QVariantList games READ games NOTIFY changed)
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY changed)

public:
    EnvService(ConfigModel* config, std::string startup_api, std::string recovered_api, QObject* parent = nullptr);
    ~EnvService() override;

    void set_window(QQuickWindow* w);
    const config::EnvironmentCapabilities& env() const { return env_; }

    bool         scanning() const { return scanning_; }
    bool         probingGpu() const { return probing_gpu_; }
    bool         probingGraphics() const { return probing_graphics_; }
    QVariantList gpus() const;
    QVariantList gpuEncoders() const;
    QVariantList graphicsApis() const;
    QString      activeGraphicsApi() const;
    QString      startupGraphicsApi() const;
    QString      recoveredGraphicsApi() const;
    QVariantMap  system() const;
    QVariantList dependencies() const;
    QVariantList games() const;
    bool         gameRunning() const { return env_.game.running.ok(); }

    Q_INVOKABLE void rescan();                 // усе заново (кнопка «Перевірити ще раз»)
    Q_INVOKABLE void probeGpuEncoders();       // GPU-кодеки ще раз
    Q_INVOKABLE void probeGraphicsApis();      // графічні API вікна
    Q_INVOKABLE void checkGameRunning();       // чи запущена гра (перед рендером, при поверненні у вікно)
    Q_INVOKABLE void refreshServices() { services_timer_.start(); }   // після встановлення чи видалення рушія, моделі
    Q_INVOKABLE QString graphicsApiLabel(const QString& id) const;
    // Garry's Mod: знайти копію гри автоматично (результат — gameSearchFinished) і драйвер у меню гри
    Q_INVOKABLE void    findGame(const QString& renderer);
    Q_INVOKABLE QString setDriverInstalled(const QString& renderer, bool install);   // "" — готово

signals:
    void changed();
    void gameSearchFinished(const QString& renderer, bool found, const QString& message);

private:
    void on_setting_changed(const QString& key);
    void start_scan();
    void start_gpu_probe();
    void publish();   // env_ → ConfigModel і QML
    template <class F>
    void run_async(F&& f);

    ConfigModel*                            config_;
    config::EnvironmentCapabilities         env_;
    std::vector<config::GraphicsApiInfo>    apis_;
    std::string                             startup_api_, recovered_api_;
    QQuickWindow*                           window_ = nullptr;
    bool                                    scanning_ = false, probing_gpu_ = false, probing_graphics_ = false;
    bool                                    leftovers_checked_ = false;
    QTimer                                  services_timer_, game_timer_;
    struct Worker {
        std::thread                        thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex                              threads_mutex_;
    std::vector<Worker>                     threads_;
    std::atomic<bool>                       closing_{false};
};

} // namespace gmdr::qt
