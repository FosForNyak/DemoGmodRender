// =============================================================================
//  project_service.hpp — поточне демо для QML (синглтон Project): аналіз, гравці з
//  голосом, позначки, фрагмент, курсор (playhead), чат і розшифровка мовлення,
//  прослуховування голосу.
//
//  Фрагмент і позначки живуть у налаштуваннях (start_tick/end_tick, markers), тож
//  змінюються через ConfigModel і так само перевіряються правилами. Позначки
//  кожного демо зберігаються окремо (gmdr_markers.json), як і раніше.
// =============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/audio/voice_preview.hpp"
#include "core/demo/analysis.hpp"
#include "core/render/jobs.hpp"
#include "core/render/markers.hpp"
#include "core/speech/transcribe.hpp"
#include "core/voice/voice_decoder.hpp"

namespace gmdr::qt {

class ConfigModel;
class VoicePlayer;

// Доріжка голосу гравця на шкалі: відрізки мовлення в секундах демо
struct TimelineLane {
    std::string                        key, name;
    bool                               local = false;
    std::vector<std::pair<float, float>> spans;
};

class ProjectService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(double loadProgress READ loadProgress NOTIFY progressChanged)
    Q_PROPERTY(QString demoPath READ demoPath NOTIFY changed)
    Q_PROPERTY(QString demoName READ demoName NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(QVariantList players READ players NOTIFY playersChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    Q_PROPERTY(double tickInterval READ tickInterval NOTIFY changed)
    Q_PROPERTY(double fragmentStart READ fragmentStart NOTIFY fragmentChanged)
    Q_PROPERTY(double fragmentEnd READ fragmentEnd NOTIFY fragmentChanged)
    Q_PROPERTY(bool wholeDemo READ wholeDemo WRITE setWholeDemo NOTIFY fragmentChanged)
    Q_PROPERTY(double playhead READ playhead WRITE setPlayhead NOTIFY playheadChanged)
    Q_PROPERTY(QVariantList chat READ chat NOTIFY changed)
    Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY transcriptChanged)
    Q_PROPERTY(QString listeningKey READ listeningKey NOTIFY listenChanged)
    Q_PROPERTY(QString preparingKey READ preparingKey NOTIFY listenChanged)
    Q_PROPERTY(bool canListen READ canListen CONSTANT)

public:
    ProjectService(ConfigModel* config, QObject* parent = nullptr);
    ~ProjectService() override;

    std::shared_ptr<const demo::DemoAnalysis>       analysis() const { return analysis_; }
    std::shared_ptr<const voice::VoiceDecodeResult> voices() const { return voices_; }
    const std::vector<TimelineLane>&                lanes() const { return lanes_; }
    const std::vector<render::Marker>&              marker_list() const { return markers_; }
    bool player_muted(const std::string& key) const;   // гучність гравця 0 (для шкали)
    void reload_transcript();
    void set_transcript(std::optional<speech::Transcript> t);
    // Позначки з гри (перегляд у грі: F9/F11/F6)
    void apply_marks(const std::vector<game::DriverMark>& marks);

    bool         loaded() const { return analysis_ != nullptr; }
    bool         loading() const { return analyze_ && analyze_->running(); }
    double       loadProgress() const;
    QString      demoPath() const;
    QString      demoName() const;
    QVariantMap  info() const;
    QVariantList players() const;
    QVariantList markers() const;
    double       duration() const { return analysis_ ? analysis_->duration_seconds : 0.0; }
    double       tickInterval() const { return analysis_ ? analysis_->tick_interval : 1.0 / 66.0; }
    double       fragmentStart() const;
    double       fragmentEnd() const;
    bool         wholeDemo() const;
    void         setWholeDemo(bool on);
    double       playhead() const { return playhead_; }
    void         setPlayhead(double t);
    QVariantList chat() const;
    QVariantList transcript() const;
    bool         hasTranscript() const { return transcript_ && !transcript_->lines.empty(); }
    QString      listeningKey() const;
    QString      preparingKey() const;
    bool         canListen() const;

    Q_INVOKABLE void open(const QString& path);
    Q_INVOKABLE void close();
    // Чат, події й розпізнане мовлення в .txt (UTF-8 з BOM — Блокнот одразу показує кирилицю)
    Q_INVOKABLE QString saveChat(const QString& path) const;   // "" — збережено, інакше помилка
    Q_INVOKABLE QString defaultChatPath() const;
    Q_INVOKABLE void setFragmentStart(double seconds);
    Q_INVOKABLE void setFragmentEnd(double seconds);
    Q_INVOKABLE void setFragment(double from, double to);
    Q_INVOKABLE void markInAtPlayhead() { setFragmentStart(playhead_); }
    Q_INVOKABLE void markOutAtPlayhead() { setFragmentEnd(playhead_); }
    Q_INVOKABLE void addMarker(double seconds, const QString& title);
    Q_INVOKABLE void addMarkerAtPlayhead() { addMarker(playhead_, {}); }
    Q_INVOKABLE void removeMarker(int index);
    Q_INVOKABLE void renameMarker(int index, const QString& title);
    Q_INVOKABLE void moveMarker(int index, double seconds);
    Q_INVOKABLE void clearMarkers();
    // Гравці: гучність окремого гравця (0 — вимкнено), соло, прослуховування
    Q_INVOKABLE void setPlayerVolume(const QString& key, double volume);
    Q_INVOKABLE void togglePlayerMute(const QString& key);
    Q_INVOKABLE void togglePlayerSolo(const QString& key);
    Q_INVOKABLE void setPlayerSelected(const QString& key, bool on);
    Q_INVOKABLE void togglePlayerDenoise(const QString& key);
    Q_INVOKABLE void listen(const QString& key);
    Q_INVOKABLE void stopListening();
    Q_INVOKABLE QString formatTime(double seconds) const;
    Q_INVOKABLE QString formatTimecode(double seconds) const;
    Q_INVOKABLE double  parseTime(const QString& text) const;   // -1 — не розпізнано

signals:
    void changed();
    void progressChanged();
    void playersChanged();
    void markersChanged();
    void fragmentChanged();
    void playheadChanged();
    void transcriptChanged();
    void listenChanged();
    void openFailed(const QString& path, const QString& error);
    void demoLoaded();

private:
    void poll();
    void on_loaded();
    void build_lanes();
    void set_markers(std::vector<render::Marker> m);
    void update_context();
    int32_t to_tick(double seconds) const;

    ConfigModel*                                    config_;
    std::unique_ptr<render::AnalyzeJob>             analyze_;
    std::shared_ptr<const demo::DemoAnalysis>       analysis_;
    std::shared_ptr<const voice::VoiceDecodeResult> voices_;
    std::vector<TimelineLane>                       lanes_;
    std::vector<render::Marker>                     markers_;
    std::optional<speech::Transcript>               transcript_;
    double                                          playhead_ = 0;
    QTimer                                          poll_timer_;
    std::unique_ptr<VoicePlayer>               player_;
    std::future<audio::VoiceClip>                   clip_future_;
    std::string                                     clip_key_, playing_key_;
};

} // namespace gmdr::qt
