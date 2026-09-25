#include "project_service.hpp"

#include "config_model.hpp"
#include "qt_convert.hpp"

#include "core/config/formats.hpp"
#include "core/config/presets.hpp"
#include "core/demo/chat.hpp"
#include "core/util/file_util.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"
#include "gui/voice_player.hpp"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>

namespace gmdr::qt {

namespace fs = std::filesystem;

namespace {

fs::path markers_store() { return app_data_dir() / "gmdr_markers.json"; }

std::map<std::string, double> parse_volumes(const std::string& text) {
    std::map<std::string, double> out;
    for (const auto& [k, v] : parse_key_values(text)) out[k] = parse_double(v).value_or(1.0);
    return out;
}

std::string format_volumes(const std::map<std::string, double>& v) {
    std::string out;
    for (const auto& [k, x] : v)
        if (std::abs(x - 1.0) > 0.005) out += std::format("{}{}={:.2f}", out.empty() ? "" : "; ", k, x);
    return out;
}

std::vector<std::string> key_list(const std::string& csv) {
    std::vector<std::string> out;
    for (const auto& k : split(csv, ',')) {
        const std::string t = trim(k);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

} // namespace

ProjectService::ProjectService(ConfigModel* config, QObject* parent)
    : QObject(parent), config_(config), player_(std::make_unique<gui::VoicePlayer>()) {
    poll_timer_.setInterval(100);
    connect(&poll_timer_, &QTimer::timeout, this, &ProjectService::poll);
    connect(config_, &ConfigModel::changed, this, [this] { emit fragmentChanged(); });
    connect(config_, &ConfigModel::settingChanged, this, [this](const QString& key) {
        if (key == "voice_volumes" || key == "voice_mode" || key == "voice_selected" || key == "voice_denoise_players")
            emit playersChanged();
    });
}

ProjectService::~ProjectService() {
    player_->stop();
    if (analyze_) {
        analyze_->cancel();
        analyze_->wait();
    }
    if (clip_future_.valid()) clip_future_.wait();
}

void ProjectService::open(const QString& qpath) {
    std::string path_in = ss(qpath);
    if (path_in.rfind("file://", 0) == 0) path_in = ss(QUrl(qpath).toLocalFile());
    // Повний шлях з «рідними» роздільниками (у Windows — «\»), щоб і шлях до відео поруч
    // виглядав охайно
    std::error_code ec;
    fs::path p = fs::absolute(path_from_utf8(path_in), ec);
    if (ec) p = path_from_utf8(path_in);
    const std::string path = path_to_utf8(p.make_preferred());
    stopListening();
    if (analyze_) {
        analyze_->cancel();
        analyze_->wait();
    }
    analysis_.reset();
    voices_.reset();
    lanes_.clear();
    markers_.clear();
    transcript_.reset();
    // Файл результату — поруч із демо, у тому самому форматі
    config_->modify([&](render::RenderSettings& s) {
        const std::string ext = config::container_of(s);
        s.demo_path = path;
        s.output_path.clear();
        config::set_container(s, ext);
    });
    emit config_->settingChanged(QStringLiteral("demo_path"));
    emit config_->settingChanged(QStringLiteral("output_path"));
    analyze_ = std::make_unique<render::AnalyzeJob>(path);
    analyze_->start();
    poll_timer_.start();
    update_context();
    emit changed();
    emit playersChanged();
    emit markersChanged();
    emit transcriptChanged();
}

void ProjectService::close() {
    stopListening();
    if (analyze_) {
        analyze_->cancel();
        analyze_->wait();
        analyze_.reset();
    }
    analysis_.reset();
    voices_.reset();
    lanes_.clear();
    markers_.clear();
    transcript_.reset();
    update_context();
    emit changed();
    emit playersChanged();
    emit markersChanged();
}

void ProjectService::poll() {
    // Прослуховування: уривок готовий?
    if (!playing_key_.empty() && !player_->playing()) {
        playing_key_.clear();
        emit listenChanged();
    }
    if (clip_future_.valid() && clip_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const audio::VoiceClip clip = clip_future_.get();
        std::string err;
        if (clip.mono.empty()) log_info("{}", trf("Прослуховування: у цього гравця немає мовлення"));
        else if (player_->play(clip.mono, &err)) playing_key_ = clip_key_;
        else log_warn("{}", trf("Не вдалося відтворити: {}", err));
        clip_key_.clear();
        emit listenChanged();
    }
    if (analyze_) {
        emit progressChanged();
        if (!analyze_->running()) {
            if (analyze_->state() == render::JobState::Succeeded) {
                analysis_ = analyze_->analysis();
                voices_ = analyze_->voices();
                analyze_.reset();
                on_loaded();
            } else {
                const QString path = demoPath();
                const QString err = qs(analyze_->error());
                analyze_.reset();
                config_->modify([](render::RenderSettings& s) { s.demo_path.clear(); });
                emit changed();
                if (!err.isEmpty()) emit openFailed(path, err);
            }
        }
    }
    if (!analyze_ && playing_key_.empty() && !clip_future_.valid()) poll_timer_.stop();
}

void ProjectService::on_loaded() {
    const std::string& path = config_->settings().demo_path;
    std::vector<render::Marker> stored = render::load_demo_markers(markers_store(), path);
    markers_ = stored;
    config_->modify([&](render::RenderSettings& s) {
        s.markers = render::format_markers(markers_);
        if (s.end_tick > analysis_->last_tick) s.end_tick = -1;
    });
    transcript_ = speech::load_transcript(path);
    playhead_ = std::max(0, config_->settings().start_tick) * static_cast<double>(analysis_->tick_interval);
    build_lanes();
    update_context();
    log_info("{}", trf("Демо: {}, {}, голосів: {}", analysis_->header.map_name, format_duration(analysis_->duration_seconds),
                       voices_ ? voices_->speakers.size() : 0));
    emit changed();
    emit playersChanged();
    emit markersChanged();
    emit transcriptChanged();
    emit playheadChanged();
    emit fragmentChanged();
    emit demoLoaded();
}

void ProjectService::build_lanes() {
    lanes_.clear();
    if (!voices_) return;
    std::vector<const voice::SpeakerTrack*> order;
    for (const auto& sp : voices_->speakers) order.push_back(&sp);
    std::stable_sort(order.begin(), order.end(), [](auto* a, auto* b) { return a->seconds > b->seconds; });
    const double rate = voice::kVoiceRate;
    for (const auto* sp : order) {
        TimelineLane lane;
        lane.key = sp->key;
        lane.name = sp->name.empty() ? sp->display_name() : sp->name;
        lane.local = sp->is_local;
        for (const auto& seg : sp->segments)
            lane.spans.push_back({static_cast<float>(seg.start / rate), static_cast<float>(seg.end() / rate)});
        lanes_.push_back(std::move(lane));
    }
}

void ProjectService::update_context() {
    config::ValidationContext ctx;
    if (analysis_) {
        ctx.demo_known = true;
        ctx.tick_interval = analysis_->tick_interval;
        ctx.last_tick = analysis_->last_tick;
        ctx.speakers = voices_ ? static_cast<int>(voices_->speakers.size()) : -1;
        if (voices_)
            ctx.local_speaker = std::any_of(voices_->speakers.begin(), voices_->speakers.end(),
                                            [](const voice::SpeakerTrack& t) { return t.is_local; })
                                    ? 1
                                    : 0;
    }
    config_->set_context(ctx);
}

void ProjectService::reload_transcript() {
    transcript_ = speech::load_transcript(config_->settings().demo_path);
    emit transcriptChanged();
}

void ProjectService::set_transcript(std::optional<speech::Transcript> t) {
    transcript_ = std::move(t);
    emit transcriptChanged();
}

double ProjectService::loadProgress() const { return analyze_ ? std::max(0.0, analyze_->progress().fraction) : 0.0; }

QString ProjectService::demoPath() const { return qs(config_->settings().demo_path); }

QString ProjectService::demoName() const {
    const std::string& p = config_->settings().demo_path;
    return p.empty() ? QString() : qs(path_to_utf8(path_from_utf8(p).filename()));
}

QVariantMap ProjectService::info() const {
    QVariantMap m;
    if (!analysis_) return m;
    const auto& a = *analysis_;
    m["map"] = qs(a.header.map_name);
    m["server"] = qs(a.header.server_name);
    m["recordedBy"] = qs(a.header.client_name);
    m["gamemode"] = a.has_server_info ? qs(a.server_info.gamemode) : QString();
    m["duration"] = a.duration_seconds;
    m["ticks"] = a.last_tick;
    m["tickrate"] = a.tick_interval > 0 ? 1.0 / a.tick_interval : 0.0;
    m["players"] = static_cast<int>(a.players.size());
    m["voiceCodec"] = qs(a.voice_codec);
    m["voicePackets"] = static_cast<int>(a.voice_packets.size());
    m["speakers"] = voices_ ? static_cast<int>(voices_->speakers.size()) : 0;
    m["chatMessages"] = static_cast<int>(a.count_events(demo::DemoEventKind::Chat));
    m["protocol"] = a.header.demo_protocol;
    m["networkProtocol"] = a.header.network_protocol;
    m["packetsFailed"] = a.packets_failed;
    m["packetsTotal"] = a.packets_total;
    std::error_code ec;
    m["size"] = static_cast<double>(fs::file_size(a.path, ec));
    QStringList warnings;
    for (const auto& w : a.warnings) warnings << qs(w);
    if (voices_)
        for (const auto& w : voices_->warnings) warnings << qs(w);
    m["warnings"] = warnings;
    return m;
}

QVariantList ProjectService::players() const {
    QVariantList out;
    if (!voices_) return out;
    const render::RenderSettings& s = config_->settings();
    const auto volumes = parse_volumes(s.voice_volumes);
    const auto selected = key_list(s.voice_selected);
    const auto denoise = key_list(s.voice_denoise_players);
    const bool solo_mode = s.voice_mode == "selected" && selected.size() == 1;
    int index = 0;
    for (const auto& lane : lanes_) {
        const voice::SpeakerTrack* sp = nullptr;
        for (const auto& t : voices_->speakers)
            if (t.key == lane.key) sp = &t;
        if (!sp) continue;
        QVariantMap m;
        m["index"] = index++;
        m["key"] = qs(sp->key);
        m["name"] = qs(lane.name);
        m["steamid"] = sp->steamid64 ? qs(std::to_string(sp->steamid64)) : QString();
        m["local"] = sp->is_local;
        m["seconds"] = sp->seconds;
        m["segments"] = static_cast<int>(sp->segments.size());
        const auto it = volumes.find(sp->key);
        const double vol = it == volumes.end() ? 1.0 : it->second;
        m["volume"] = vol;
        m["muted"] = vol <= 0.0;
        m["selected"] = std::find(selected.begin(), selected.end(), sp->key) != selected.end();
        m["solo"] = solo_mode && selected.front() == sp->key;
        m["denoise"] = std::find(denoise.begin(), denoise.end(), sp->key) != denoise.end();
        m["lost"] = sp->frames > 0 ? static_cast<double>(sp->lost_frames) / sp->frames : 0.0;
        out << m;
    }
    return out;
}

bool ProjectService::player_muted(const std::string& key) const {
    const auto v = parse_volumes(config_->settings().voice_volumes);
    const auto it = v.find(key);
    return it != v.end() && it->second <= 0.0;
}

QVariantList ProjectService::markers() const {
    QVariantList out;
    const double ti = tickInterval();
    for (size_t i = 0; i < markers_.size(); ++i) {
        QVariantMap m;
        m["index"] = static_cast<int>(i);
        m["tick"] = markers_[i].tick;
        m["time"] = markers_[i].tick * ti;
        m["title"] = qs(markers_[i].title);
        out << m;
    }
    return out;
}

int32_t ProjectService::to_tick(double seconds) const {
    const double ti = tickInterval();
    const int32_t last = analysis_ ? analysis_->last_tick : 0;
    return std::clamp(static_cast<int32_t>(std::llround(seconds / ti)), 0, std::max(0, last));
}

double ProjectService::fragmentStart() const { return std::max(0, config_->settings().start_tick) * tickInterval(); }

double ProjectService::fragmentEnd() const {
    const int32_t e = config_->settings().end_tick;
    return (e > 0 ? e : (analysis_ ? analysis_->last_tick : 0)) * tickInterval();
}

bool ProjectService::wholeDemo() const { return config_->settings().start_tick <= 0 && config_->settings().end_tick <= 0; }

void ProjectService::setWholeDemo(bool on) {
    if (!on || wholeDemo()) return;
    config_->modify([](render::RenderSettings& s) {
        s.start_tick = 0;
        s.end_tick = -1;
    });
    emit fragmentChanged();
}

void ProjectService::setFragmentStart(double seconds) {
    if (!analysis_) return;
    const int32_t t = to_tick(seconds);
    config_->modify([&](render::RenderSettings& s) {
        s.start_tick = t;
        if (s.end_tick > 0 && s.end_tick <= t) s.end_tick = -1;   // кінець раніше за новий початок — до кінця запису
    });
    emit fragmentChanged();
}

void ProjectService::setFragmentEnd(double seconds) {
    if (!analysis_) return;
    const int32_t t = to_tick(seconds);
    config_->modify([&](render::RenderSettings& s) {
        s.end_tick = t >= analysis_->last_tick ? -1 : t;
        if (s.start_tick >= t) s.start_tick = 0;
    });
    emit fragmentChanged();
}

void ProjectService::setFragment(double from, double to) {
    if (!analysis_) return;
    if (to < from) std::swap(from, to);
    const int32_t a = to_tick(from), b = to_tick(to);
    if (b <= a) return;
    config_->modify([&](render::RenderSettings& s) {
        s.start_tick = a;
        s.end_tick = b >= analysis_->last_tick ? -1 : b;
    });
    emit fragmentChanged();
}

void ProjectService::setPlayhead(double t) {
    const double v = std::clamp(t, 0.0, std::max(0.0, duration()));
    if (std::abs(v - playhead_) < 1e-9) return;
    playhead_ = v;
    emit playheadChanged();
}

void ProjectService::set_markers(std::vector<render::Marker> m) {
    std::sort(m.begin(), m.end(), [](const render::Marker& a, const render::Marker& b) { return a.tick < b.tick; });
    markers_ = std::move(m);
    const std::string text = render::format_markers(markers_);
    config_->modify([&](render::RenderSettings& s) { s.markers = text; });
    if (!config_->settings().demo_path.empty())
        render::save_demo_markers(markers_store(), config_->settings().demo_path, markers_);
    emit markersChanged();
}

void ProjectService::addMarker(double seconds, const QString& title) {
    if (!analysis_) return;
    auto m = markers_;
    render::add_marker(m, {to_tick(seconds), ss(title)});
    set_markers(std::move(m));
}

void ProjectService::removeMarker(int index) {
    if (index < 0 || index >= static_cast<int>(markers_.size())) return;
    auto m = markers_;
    m.erase(m.begin() + index);
    set_markers(std::move(m));
}

void ProjectService::renameMarker(int index, const QString& title) {
    if (index < 0 || index >= static_cast<int>(markers_.size())) return;
    auto m = markers_;
    m[static_cast<size_t>(index)].title = ss(title);
    set_markers(std::move(m));
}

void ProjectService::moveMarker(int index, double seconds) {
    if (index < 0 || index >= static_cast<int>(markers_.size())) return;
    auto m = markers_;
    m[static_cast<size_t>(index)].tick = to_tick(seconds);
    set_markers(std::move(m));
}

void ProjectService::clearMarkers() { set_markers({}); }

void ProjectService::apply_marks(const std::vector<game::DriverMark>& marks) {
    for (const auto& mk : marks) {
        const double t = mk.tick * tickInterval();
        if (mk.kind == "start") setFragmentStart(t);
        else if (mk.kind == "end") setFragmentEnd(t);
        else addMarker(t, qs(trf("Позначка {}", markers_.size() + 1)));
        setPlayhead(t);
    }
}

void ProjectService::setPlayerVolume(const QString& key, double volume) {
    config_->modify([&](render::RenderSettings& s) {
        auto v = parse_volumes(s.voice_volumes);
        v[ss(key)] = std::clamp(volume, 0.0, 4.0);
        s.voice_volumes = format_volumes(v);
    });
    emit playersChanged();
}

void ProjectService::togglePlayerMute(const QString& key) {
    config_->modify([&](render::RenderSettings& s) {
        auto v = parse_volumes(s.voice_volumes);
        const std::string k = ss(key);
        if (v.count(k) && v[k] <= 0.0) v.erase(k);
        else v[k] = 0.0;
        s.voice_volumes = format_volumes(v);
    });
    emit playersChanged();
}

void ProjectService::togglePlayerSolo(const QString& key) {
    config_->modify([&](render::RenderSettings& s) {
        const std::string k = ss(key);
        if (s.voice_mode == "selected" && trim(s.voice_selected) == k) {
            s.voice_mode = "all";
        } else {
            s.voice_mode = "selected";
            s.voice_selected = k;
        }
    });
    emit playersChanged();
}

void ProjectService::setPlayerSelected(const QString& key, bool on) {
    config_->modify([&](render::RenderSettings& s) {
        auto list = key_list(s.voice_selected);
        const std::string k = ss(key);
        list.erase(std::remove(list.begin(), list.end(), k), list.end());
        if (on) list.push_back(k);
        s.voice_selected = join(list, ",");
    });
    emit playersChanged();
}

void ProjectService::togglePlayerDenoise(const QString& key) {
    config_->modify([&](render::RenderSettings& s) {
        auto list = key_list(s.voice_denoise_players);
        const std::string k = ss(key);
        const auto it = std::find(list.begin(), list.end(), k);
        if (it != list.end()) list.erase(it);
        else list.push_back(k);
        s.voice_denoise_players = join(list, ",");
    });
    emit playersChanged();
}

bool ProjectService::canListen() const { return gui::VoicePlayer::supported(); }

void ProjectService::listen(const QString& qkey) {
    if (!voices_ || !analysis_ || clip_future_.valid()) return;
    const std::string key = ss(qkey);
    player_->stop();
    playing_key_.clear();
    const voice::SpeakerTrack* track = nullptr;
    for (const auto& t : voices_->speakers)
        if (t.key == key) track = &t;
    if (!track) return;
    clip_key_ = key;
    auto voices = voices_;   // тримає доріжку живою, поки уривок готується у фоні
    const render::RenderSettings settings = config_->settings();
    const int64_t from = static_cast<int64_t>(
        std::llround(std::max(0, settings.start_tick) * static_cast<double>(analysis_->tick_interval) * voice::kVoiceRate));
    clip_future_ = std::async(std::launch::async, [voices, track, settings, from] {
        std::vector<audio::VoiceCleanup> fx;
        std::atomic<bool> cancel{false};
        if (render::needs_voice_cleanup(settings)) fx = render::voice_cleanup_for(settings, {track}, cancel);
        return audio::make_voice_clip(*track, from, fx.empty() ? nullptr : &fx[0]);
    });
    poll_timer_.start();
    emit listenChanged();
}

void ProjectService::stopListening() {
    player_->stop();
    playing_key_.clear();
    emit listenChanged();
}

QString ProjectService::listeningKey() const { return qs(playing_key_); }
QString ProjectService::preparingKey() const { return qs(clip_key_); }

QVariantList ProjectService::chat() const {
    QVariantList out;
    if (!analysis_) return out;
    const double ti = analysis_->tick_interval;
    for (const auto& e : analysis_->events) {
        QVariantMap m;
        m["time"] = e.tick * ti;
        static const char* kinds[] = {"chat", "server", "join", "leave", "name", "kill"};
        m["kind"] = kinds[std::min<size_t>(static_cast<size_t>(e.kind), 5)];
        m["who"] = qs(e.who);
        m["text"] = qs(e.text);
        m["channel"] = qs(e.channel);
        out << m;
    }
    return out;
}

QVariantList ProjectService::transcript() const {
    QVariantList out;
    if (!transcript_) return out;
    for (const auto& l : transcript_->lines) {
        QVariantMap m;
        m["start"] = l.start;
        m["end"] = l.end;
        m["key"] = qs(l.speaker_key);
        m["speaker"] = qs(l.speaker);
        m["text"] = qs(l.text);
        out << m;
    }
    return out;
}

QString ProjectService::defaultChatPath() const {
    if (!analysis_) return {};
    const fs::path demo = path_from_utf8(config_->settings().demo_path);
    return qs(path_to_utf8(demo.parent_path() / (path_to_utf8(demo.stem()) + "_chat.txt")));
}

QString ProjectService::saveChat(const QString& qpath) const {
    if (!analysis_) return {};
    std::string path = ss(qpath);
    if (path.rfind("file:", 0) == 0) path = ss(QUrl(qpath).toLocalFile());
    struct Row {
        double      t;
        std::string text;
    };
    std::vector<Row> rows;
    const double ti = analysis_->tick_interval;
    for (const auto& e : analysis_->events) rows.push_back({e.tick * ti, demo::format_event(e)});
    if (transcript_)
        for (const auto& l : transcript_->lines) rows.push_back({l.start, l.speaker + gmdr::tr(" (голос): ") + l.text});
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.t < b.t; });
    std::string text;
    for (const auto& r : rows) {
        const int h = static_cast<int>(r.t / 3600), m = static_cast<int>(std::fmod(r.t, 3600) / 60), s = static_cast<int>(std::fmod(r.t, 60));
        text += (h > 0 ? std::format("[{}:{:02}:{:02}] ", h, m, s) : std::format("[{:02}:{:02}] ", m, s)) + r.text + "\n";
    }
    std::string err;
    if (!write_file_text(path_from_utf8(path), "\xEF\xBB\xBF" + text, &err)) {
        log_warn("{}", trf("Не вдалося зберегти чат: {}", err));
        return qs(err);
    }
    log_info("{}", trf("Чат збережено: {}", path));
    return {};
}

QString ProjectService::formatTime(double seconds) const { return qs(format_duration(seconds)); }
QString ProjectService::formatTimecode(double seconds) const { return qs(format_timecode(seconds)); }

double ProjectService::parseTime(const QString& text) const {
    const auto t = parse_timecode(ss(text));
    return t ? *t : -1.0;
}

} // namespace gmdr::qt
