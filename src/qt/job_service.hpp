// =============================================================================
//  job_service.hpp — поточне фонове завдання для QML (синглтон Jobs): рендер,
//  тестовий прогін, черга, розпізнавання й переклад, збереження голосів,
//  кодування готових кадрів, перегляд у грі, рушій озвучення, завантаження моделі.
//
//  Одночасно — одне завдання (як і раніше). Прогрес читається таймером лише поки
//  завдання йде. Стан рендеру для всього інтерфейсу — одна модель (phase):
//  перевірка → підготовка → запуск гри → завантаження демо → рендер → завершення →
//  готово / помилка / скасовано; помилка налаштувань (settingsError) відрізняється від
//  збою під час роботи.
// =============================================================================
#pragma once

#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <future>
#include <memory>
#include <optional>
#include <string>

#include "core/render/jobs.hpp"
#include "core/render/resume.hpp"
#include "core/util/power.hpp"
#include "core/util/update_check.hpp"

namespace gmdr::qt {

class ConfigModel;
class ProjectService;
class QueueModel;
class ShellService;

class JobService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY progressChanged)
    Q_PROPERTY(QString phaseLabel READ phaseLabel NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QVariantList checks READ checks NOTIFY progressChanged)
    Q_PROPERTY(bool canShowGame READ canShowGame NOTIFY stateChanged)
    Q_PROPERTY(bool showGame READ showGame WRITE setShowGame NOTIFY stateChanged)
    Q_PROPERTY(int previewSerial READ previewSerial NOTIFY previewChanged)
    Q_PROPERTY(QString afterDone READ afterDone WRITE setAfterDone NOTIFY afterDoneChanged)
    Q_PROPERTY(int powerCountdown READ powerCountdown NOTIFY afterDoneChanged)
    Q_PROPERTY(QVariantMap resumeOffer READ resumeOffer NOTIFY resumeChanged)
    Q_PROPERTY(QVariantMap lastResult READ lastResult NOTIFY finished)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY updatesChanged)

public:
    JobService(ConfigModel* config, ProjectService* project, QueueModel* queue, ShellService* shell, QObject* parent = nullptr);
    ~JobService() override;

    bool         busy() const { return job_ && job_->running(); }
    QString      kind() const { return kind_; }
    QString      title() const;
    QString      phase() const;
    QString      phaseLabel() const;
    QVariantMap  progress() const;
    QVariantList checks() const;
    bool         canShowGame() const { return busy() && job_->can_show_game(); }
    bool         showGame() const { return show_game_; }
    void         setShowGame(bool on);
    int          previewSerial() const { return static_cast<int>(preview_serial_); }
    const QImage& preview_image() const { return preview_; }
    QString      afterDone() const;
    void         setAfterDone(const QString& id);
    int          powerCountdown() const { return countdown_; }
    QVariantMap  resumeOffer() const;
    QVariantMap  lastResult() const { return last_; }
    bool         checkingUpdates() const { return update_future_.valid(); }

    Q_INVOKABLE bool startRender(bool confirmedOverwrite = false);
    Q_INVOKABLE bool startTest();
    Q_INVOKABLE bool startQueue();
    Q_INVOKABLE bool resume();
    Q_INVOKABLE void forgetResume();
    Q_INVOKABLE bool transcribe(bool again);   // again — заново, інакше доповнити
    Q_INVOKABLE bool translateOnly(bool rangeOnly);
    Q_INVOKABLE bool exportVoices(const QString& folder);
    Q_INVOKABLE bool encodeFrames(const QString& folder);
    Q_INVOKABLE bool watchInGame(double seconds);
    Q_INVOKABLE bool voiceEngine(bool install, bool cuda);
    Q_INVOKABLE bool checkService(bool elevenlabs);
    Q_INVOKABLE bool downloadWhisperModel(int index);
    Q_INVOKABLE QVariantList whisperModels() const;   // {index, file, label, sizeMb, installed, path}
    Q_INVOKABLE QString whisperModelsFolder() const;
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void kill();
    Q_INVOKABLE void cancelPowerAction();
    Q_INVOKABLE void powerActionNow();
    Q_INVOKABLE void checkUpdates();
    Q_INVOKABLE QString makeReport(const QString& path);   // "" — готово, інакше помилка
    Q_INVOKABLE QString defaultReportName() const;
    Q_INVOKABLE bool outputExists() const;

signals:
    void stateChanged();
    void progressChanged();
    void previewChanged();
    void finished();                  // lastResult: kind, state, title, text, result, isFolder, testOk, checks, issues
    void afterDoneChanged();
    void resumeChanged();
    void updatesChanged();
    void updateResult(const QString& title, const QString& text, const QString& url);
    void confirmOverwrite(const QString& path);
    void message(const QString& title, const QString& text);

private:
    bool start(std::unique_ptr<render::Job> job, const QString& kind);
    void poll();
    void on_finished();
    void refresh_resume();

    ConfigModel*                          config_;
    ProjectService*                       project_;
    QueueModel*                           queue_;
    ShellService*                         shell_;
    std::unique_ptr<render::Job>          job_;
    QString                               kind_;
    bool                                  reported_ = true;
    bool                                  show_game_ = false;
    QTimer                                timer_, countdown_timer_;
    QImage                                preview_;
    uint64_t                              preview_serial_ = 0;
    render::Progress                      progress_;
    QVariantMap                           last_;
    std::optional<render::ResumeRecord>   resume_;
    PowerAction                           after_done_ = PowerAction::None;
    int                                   countdown_ = 0;
    struct UpdateResult {
        std::optional<ReleaseInfo> release;
        std::string                error;
    };
    std::future<UpdateResult>             update_future_;
    QTimer                                update_timer_;
    int                                   tray_tick_ = 0;
};

} // namespace gmdr::qt
