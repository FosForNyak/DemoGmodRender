#include "job_service.hpp"

#include "config_model.hpp"
#include "project_service.hpp"
#include "qt_convert.hpp"
#include "queue_model.hpp"
#include "shell_service.hpp"

#include "core/config/formats.hpp"
#include "core/render/dub_jobs.hpp"
#include "core/render/report.hpp"
#include "core/speech/transcribe.hpp"
#include "core/util/file_util.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

#include <QUrl>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>

namespace gmdr::qt {

namespace fs = std::filesystem;

namespace {

std::string local_path(const QString& s) {
    if (s.startsWith("file:")) return ss(QUrl(s).toLocalFile());
    return ss(s);
}

QVariantList checks_list(const std::vector<render::CheckItem>& checks) {
    QVariantList out;
    for (const auto& c : checks) {
        QVariantMap m;
        m["name"] = qs(c.name);
        m["state"] = c.state == render::CheckItem::Ok       ? "ok"
                     : c.state == render::CheckItem::Failed  ? "failed"
                     : c.state == render::CheckItem::Skipped ? "skipped"
                                                             : "pending";
        m["detail"] = qs(c.detail);
        out << m;
    }
    return out;
}

QVariantList issues_list(const std::vector<config::Issue>& issues) {
    QVariantList out;
    for (const auto& i : issues) {
        QVariantMap m;
        m["rule"] = qs(i.rule);
        m["severity"] = config::severity_id(i.severity);
        m["message"] = qs(i.message);
        m["explanation"] = qs(i.explanation);
        QStringList fixes;
        for (const auto& f : i.fixes) fixes << qs(f.label);
        m["fixes"] = fixes;
        out << m;
    }
    return out;
}

} // namespace

JobService::JobService(ConfigModel* config, ProjectService* project, QueueModel* queue, ShellService* shell, QObject* parent)
    : QObject(parent), config_(config), project_(project), queue_(queue), shell_(shell) {
    timer_.setInterval(150);
    connect(&timer_, &QTimer::timeout, this, &JobService::poll);
    countdown_timer_.setInterval(1000);
    connect(&countdown_timer_, &QTimer::timeout, this, [this] {
        if (--countdown_ <= 0) powerActionNow();
        emit afterDoneChanged();
    });
    update_timer_.setInterval(250);
    connect(&update_timer_, &QTimer::timeout, this, [this] {
        if (!update_future_.valid() || update_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        update_timer_.stop();
        const UpdateResult r = update_future_.get();
        emit updatesChanged();
        if (r.release && compare_versions(r.release->version, GMDR_VERSION) > 0) {
            QString text = qs(trf("У вас {}. Нова версія опублікована {}.", GMDR_VERSION, r.release->published));
            if (!r.release->notes.empty()) text += "\n\n" + qs(r.release->notes);
            log_info("{}", trf("Є нова версія {}: {}", r.release->version, r.release->url));
            emit updateResult(qs(gmdr::tr("Є нова версія ") + r.release->version), text, qs(r.release->url));
        } else if (r.release) {
            log_info("{}", trf("Оновлень немає (остання — {})", r.release->version));
            emit updateResult(qs(gmdr::tr("Оновлень немає")), qs(trf("У вас остання версія ({}).", GMDR_VERSION)), {});
        } else {
            log_warn("{}", trf("Перевірка оновлень: {}", r.error));
            emit updateResult(qs(gmdr::tr("Не вдалося перевірити оновлення")), qs(r.error), {});
        }
    });
    refresh_resume();
}

JobService::~JobService() {
    if (job_ && job_->running()) {
        job_->kill();
        job_->wait();
    }
    shell_->taskbar(0, 0);
}

bool JobService::start(std::unique_ptr<render::Job> job, const QString& kind) {
    if (busy()) {
        emit message(qs(gmdr::tr("Зачекайте")), qs(gmdr::tr("Зачекайте завершення поточного завдання")));
        return false;
    }
    config_->save_now();
    job_ = std::move(job);
    kind_ = kind;
    reported_ = false;
    show_game_ = false;
    progress_ = {};
    job_->start();
    timer_.start();
    emit stateChanged();
    emit progressChanged();
    return true;
}

bool JobService::outputExists() const {
    const std::string& out = config_->settings().output_path;
    std::error_code ec;
    return !out.empty() && out.find('%') == std::string::npos && fs::exists(path_from_utf8(out), ec);
}

bool JobService::startRender(bool confirmed) {
    auto a = project_->analysis();
    if (!a) return false;
    if (!confirmed && outputExists()) {
        emit confirmOverwrite(qs(config_->settings().output_path));
        return false;
    }
    std::error_code ec;
    fs::create_directories(path_from_utf8(config_->settings().output_path).parent_path(), ec);
    return start(std::make_unique<render::RenderJob>(config_->settings(), a, project_->voices(), false), "render");
}

bool JobService::startTest() {
    auto a = project_->analysis();
    if (!a) return false;
    return start(std::make_unique<render::RenderJob>(config_->settings(), a, project_->voices(), true), "test");
}

bool JobService::startQueue() {
    const auto items = queue_->settings_list();
    if (items.empty()) return false;
    for (const auto& s : items) {
        std::error_code ec;
        fs::create_directories(path_from_utf8(s.output_path).parent_path(), ec);
    }
    const bool ok = start(std::make_unique<render::QueueJob>(items), "queue");
    if (ok) queue_->set_running(true);
    return ok;
}

bool JobService::resume() {
    if (!resume_) return false;
    auto job = std::make_unique<render::RenderJob>(resume_->settings, nullptr, nullptr);
    job->set_resume(*resume_);
    return start(std::move(job), "resume");
}

void JobService::forgetResume() {
    if (!resume_) return;
    render::forget_resume(resume_->id);
    log_info("{}", trf("Запис про урваний рендер прибрано (частковий файл лишився як був)"));
    refresh_resume();
}

void JobService::refresh_resume() {
    auto list = render::pending_resumes();
    if (list.empty()) resume_.reset();
    else resume_ = list.front();
    emit resumeChanged();
}

QVariantMap JobService::resumeOffer() const {
    QVariantMap m;
    if (!resume_) return m;
    const auto& r = *resume_;
    const double fps = parse_rational(r.settings.fps).value_or(Rational{60, 1}).value();
    const double done = fps > 0 ? r.frames / fps : 0;
    m["output"] = qs(r.settings.output_path);
    m["demo"] = qs(r.settings.demo_path);
    m["doneSeconds"] = done;
    m["totalSeconds"] = r.seconds;
    m["fraction"] = r.seconds > 0 ? std::clamp(done / r.seconds, 0.0, 1.0) : 0.0;
    m["updated"] = static_cast<double>(r.updated);
    return m;
}

bool JobService::transcribe(bool again) {
    auto a = project_->analysis();
    auto v = project_->voices();
    if (!a || !v) return false;
    if (again) {
        std::error_code ec;
        fs::remove(speech::transcript_path(config_->settings().demo_path), ec);
        project_->set_transcript(std::nullopt);
    }
    return start(std::make_unique<render::TranscribeJob>(config_->settings(), a, v), "transcribe");
}

bool JobService::translateOnly(bool range_only) {
    auto a = project_->analysis();
    auto v = project_->voices();
    if (!a || !v) return false;
    return start(std::make_unique<render::TranslateJob>(config_->settings(), a, v, range_only), "translate");
}

bool JobService::exportVoices(const QString& folder) {
    auto a = project_->analysis();
    auto v = project_->voices();
    if (!a || !v) return false;
    return start(std::make_unique<render::ExportVoicesJob>(config_->settings(), a, v, path_from_utf8(local_path(folder))),
                 "voices");
}

bool JobService::encodeFrames(const QString& qfolder) {
    const std::string dir = local_path(qfolder);
    render::RenderSettings s = config_->settings();
    int64_t count = 0;
    const std::string prefix = render::detect_frame_prefix(path_from_utf8(dir), &count);
    if (count == 0) {
        emit message(qs(gmdr::tr("Кадрів не знайдено")),
                     qs(gmdr::tr("У вибраній папці немає послідовності кадрів (name0000.tga / .jpg / .png).")));
        return false;
    }
    std::string ext = config::container_of(s);
    if (const auto* c = config::find_container(ext); c && !c->image_codec.empty()) ext = "mp4";
    const std::string stem = prefix.empty() ? "video" : prefix;
    s.output_path = path_to_utf8(path_from_utf8(dir) / (stem + "." + ext));
    std::error_code ec;
    for (int i = 2; fs::exists(path_from_utf8(s.output_path), ec) && i < 1000; ++i)
        s.output_path = path_to_utf8(path_from_utf8(dir) / std::format("{} ({}).{}", stem, i, ext));
    log_info("{}", trf("Кодую {} кадрів «{}» з папки {}", count, prefix, dir));
    return start(std::make_unique<render::EncodeFramesJob>(s, path_from_utf8(dir), prefix, fs::path(), project_->analysis(),
                                                           project_->voices()),
                 "encode");
}

bool JobService::watchInGame(double seconds) {
    auto a = project_->analysis();
    if (!a) return false;
    const int32_t tick = static_cast<int32_t>(std::llround(seconds / a->tick_interval));
    return start(std::make_unique<render::WatchJob>(config_->settings(), a, tick), "watch");
}

bool JobService::voiceEngine(bool install, bool cuda) {
    return start(std::make_unique<render::VoiceEngineJob>(
                     install ? render::VoiceEngineJob::Action::Install : render::VoiceEngineJob::Action::Check, cuda,
                     config_->settings()),
                 "voiceEngine");
}

bool JobService::checkService(bool elevenlabs) {
    return start(std::make_unique<render::ServiceCheckJob>(
                     elevenlabs ? render::ServiceCheckJob::What::ElevenLabs : render::ServiceCheckJob::What::Translator,
                     config_->settings()),
                 "serviceCheck");
}

bool JobService::downloadWhisperModel(int index) {
    const auto& models = speech::known_models();
    if (index < 0 || index >= static_cast<int>(models.size())) return false;
    const auto& m = models[static_cast<size_t>(index)];
    const fs::path dest = speech::models_download_dir() / m.file;
    config_->modify([](render::RenderSettings& s) { s.whisper_model.clear(); });   // після завантаження — найкраща наявна
    return start(std::make_unique<render::DownloadJob>(speech::model_url(m.file), dest, std::string(gmdr::tr("модель ")) + m.file),
                 "download");
}

QVariantList JobService::whisperModels() const {
    QVariantList out;
    const auto& models = speech::known_models();
    for (size_t i = 0; i < models.size(); ++i) {
        const auto& m = models[i];
        const fs::path p = speech::models_download_dir() / m.file;
        std::error_code ec;
        QVariantMap v;
        v["index"] = static_cast<int>(i);
        v["file"] = qs(m.file);
        v["label"] = qs(m.label);
        v["sizeMb"] = m.size_mb;
        v["installed"] = fs::exists(p, ec);
        v["path"] = qs(path_to_utf8(p));
        out << v;
    }
    return out;
}

QString JobService::whisperModelsFolder() const { return qs(path_to_utf8(speech::models_download_dir())); }

void JobService::cancel() {
    if (busy()) job_->cancel();
}

void JobService::kill() {
    if (busy()) job_->kill();
}

void JobService::setShowGame(bool on) {
    if (!busy()) return;
    show_game_ = on;
    job_->set_show_game(on);
    emit stateChanged();
}

QString JobService::title() const { return job_ ? qs(job_->name()) : QString(); }

void JobService::poll() {
    if (!job_) {
        timer_.stop();
        return;
    }
    progress_ = job_->progress();
    // Живе прев'ю: копіюємо лише новий кадр
    render::PreviewFrame f;
    if (job_->preview().get_if_newer(preview_serial_, f) && f.width > 0 && f.height > 0) {
        preview_ = QImage(f.rgba.data(), f.width, f.height, f.width * 4, QImage::Format_RGBA8888).copy();
        preview_serial_ = f.serial;
        emit previewChanged();
    }
    if (kind_ == "watch")
        if (auto* w = dynamic_cast<render::WatchJob*>(job_.get())) project_->apply_marks(w->take_marks());
    if (kind_ == "queue")
        if (auto* q = dynamic_cast<render::QueueJob*>(job_.get())) queue_->update_results(q->results(), q->current_index());
    // Панель задач і трей
    const bool paused = progress_.game_paused;
    const int state = paused && progress_.disk_low ? 3 : paused ? 2 : (progress_.frames > 0 || progress_.fraction > 0) ? 1 : 4;
    shell_->taskbar(job_->running() ? state : 0, std::max(0.0, progress_.fraction));
    if (++tray_tick_ % 7 == 0) {
        std::string tip = "GMod Demo Render";
        if (job_->running() && kind_ != "watch") {
            tip += " — " + progress_.stage;
            if (progress_.fraction > 0) tip += std::format(" {:.0f}%", progress_.fraction * 100);
            if (progress_.eta >= 0) tip += gmdr::tr(", залишилось ~") + format_duration(progress_.eta);
            if (after_done_ != PowerAction::None) tip += std::string(gmdr::tr("; потім — ")) + power_action_name(after_done_);
        }
        shell_->tray(job_->running() && kind_ != "watch", tip);
    }
    emit progressChanged();
    if (!job_->running() && !reported_) {
        reported_ = true;
        timer_.stop();
        on_finished();
    }
}

void JobService::on_finished() {
    const render::JobState st = job_->state();
    const bool ok = st == render::JobState::Succeeded;
    const bool cancelled = st == render::JobState::Cancelled;
    QVariantMap r;
    r["kind"] = kind_;
    r["state"] = ok ? "succeeded" : cancelled ? "cancelled" : "failed";
    r["result"] = qs(job_->result());
    r["isFolder"] = false;
    r["testOk"] = false;
    r["settingsError"] = job_->settings_error();
    r["issues"] = issues_list(job_->settings_issues());
    r["checks"] = QVariantList();
    QString title, text;
    bool show = true;
    const std::string error = job_->error();

    if (kind_ == "watch") {
        show = st == render::JobState::Failed;
        title = qs(gmdr::tr("Перегляд у грі: помилка"));
        text = qs(error);
    } else if (kind_ == "queue") {
        auto* q = static_cast<render::QueueJob*>(job_.get());
        queue_->finish(q->results());
        title = qs(ok ? gmdr::tr("Черга завершена") : cancelled ? gmdr::tr("Чергу зупинено") : gmdr::tr("Черга: помилка"));
        text = qs(job_->report().empty() ? error : job_->report());
        for (const auto& res : q->results())
            if (res.state == render::JobState::Succeeded && !res.output.empty()) {
                r["result"] = qs(path_to_utf8(path_from_utf8(res.output).parent_path()));
                r["isFolder"] = true;
                break;
            }
    } else if (kind_ == "transcribe") {
        auto* t = static_cast<render::TranscribeJob*>(job_.get());
        if (ok) {
            project_->set_transcript(t->transcript());
            show = false;
            log_info("{}", trf("Розпізнано реплік: {}", t->transcript() ? t->transcript()->lines.size() : 0));
        }
        title = qs(gmdr::tr("Розпізнавання мовлення: помилка"));
        text = qs(error);
        show = show && st == render::JobState::Failed;
    } else if (kind_ == "serviceCheck" || kind_ == "voiceEngine") {
        title = qs(ok ? gmdr::tr("Перевірка пройшла") : kind_ == "voiceEngine" ? gmdr::tr("Рушій озвучення: помилка") : gmdr::tr("Перевірка не вдалася"));
        text = qs(ok ? job_->result() : error);
        show = !cancelled;
    } else if (kind_ == "translate") {
        if (ok) {
            title = qs(gmdr::tr("Субтитри перекладено"));
            text = qs(trf("Перекладені субтитри лежать поруч:\n{}", job_->result()));
            r["result"] = qs(path_to_utf8(path_from_utf8(job_->result()).parent_path()));
            r["isFolder"] = true;
            project_->reload_transcript();
        } else {
            title = qs(gmdr::tr("Переклад: помилка"));
            text = qs(error);
            show = !cancelled;
        }
    } else if (kind_ == "download") {
        show = st == render::JobState::Failed;
        title = qs(gmdr::tr("Завантаження: помилка"));
        text = qs(error);
    } else {
        const bool test = kind_ == "test";
        if ((kind_ == "render" || kind_ == "resume") && config_->settings().speech_subtitles) project_->reload_transcript();
        if (ok) {
            title = qs(test ? gmdr::tr("Тестовий прогін") : gmdr::tr("Готово!"));
            r["isFolder"] = kind_ == "voices";
            if (test) {
                text = qs(job_->report());
                r["testOk"] = job_->report().rfind(gmdr::tr("Усе працює."), 0) == 0;
            } else {
                text = qs((kind_ == "voices" ? gmdr::tr("Голоси збережено в папку:\n") : gmdr::tr("Відео збережено:\n")) + job_->result());
            }
        } else if (cancelled) {
            title = qs(gmdr::tr("Скасовано"));
            text = qs(kind_ == "voices" ? gmdr::tr("Збереження голосів перервано.") : gmdr::tr("Рендер перервано."));
        } else {
            title = qs(job_->settings_error() ? gmdr::tr("Налаштування не дозволяють почати рендер")
                       : test                 ? gmdr::tr("Тестовий прогін: помилка")
                                              : gmdr::tr("Помилка"));
            text = qs(error);
            r["checks"] = checks_list(progress_.checks);
        }
    }
    r["title"] = title;
    r["text"] = text;
    r["show"] = show;
    last_ = r;
    shell_->taskbar(0, 0);
    shell_->tray(false, {});
    if (show) shell_->flash();
    // Сповіщення і дія після рендеру
    const bool render_like = kind_ == "render" || kind_ == "queue" || kind_ == "resume";
    if (render_like || kind_ == "test") {
        refresh_resume();
        if (config_->settings().notify_when_done && (shell_->window_hidden() || after_done_ != PowerAction::None)) {
            std::string body = ss(text).substr(0, ss(text).find("\n\n"));
            if (body.size() > 220) body = body.substr(0, 217) + "...";
            shell_->notify(ss(title), body);
        }
        if (render_like && after_done_ != PowerAction::None) {
            if (cancelled) {
                log_info("{}", trf("Рендер зупинено вручну — «{}» після завершення скасовано", power_action_name(after_done_)));
                after_done_ = PowerAction::None;
            } else {
                countdown_ = power_countdown_seconds();
                countdown_timer_.start();
                log_info("{}", trf("Після завершення: {} через {} с (можна скасувати)", power_action_name(after_done_), countdown_));
                shell_->notify(after_done_ == PowerAction::Shutdown ? gmdr::tr("ПК вимкнеться за хвилину") : gmdr::tr("ПК засне за хвилину"),
                               gmdr::tr("Рендер завершено. Відкрийте GMod Demo Render, щоб скасувати."));
                shell_->restore();
            }
            emit afterDoneChanged();
        }
    }
    if (kind_ == "queue") queue_->set_running(false);
    emit stateChanged();
    emit finished();
}

QString JobService::phase() const {
    if (!job_) return "idle";
    if (!job_->running()) {
        switch (job_->state()) {
        case render::JobState::Succeeded: return "completed";
        case render::JobState::Cancelled: return "cancelled";
        case render::JobState::Failed: return "failed";
        default: return "idle";
        }
    }
    if (kind_ != "render" && kind_ != "test" && kind_ != "queue" && kind_ != "resume") return "processing";
    const auto& p = progress_;
    if (p.stage == gmdr::tr("Перевірка налаштувань")) return "validating";
    if (p.game_restarts > 0 && p.frames > 0 && !p.game_running) return "recovering";
    if (p.frames > 0 && !p.game_running && p.fraction >= 0.99) return "finalizing";
    if (p.frames > 0) return "rendering";
    if (p.game_running) return "loading";
    if (p.stage.find("Garry") != std::string::npos || p.stage == gmdr::tr("Запуск Garry's Mod")) return "starting";
    return "preparing";
}

QString JobService::phaseLabel() const {
    const QString ph = phase();
    if (ph == "validating") return qs(gmdr::tr("Перевірка налаштувань"));
    if (ph == "preparing") return qs(gmdr::tr("Підготовка"));
    if (ph == "starting") return qs(gmdr::tr("Запуск гри"));
    if (ph == "loading") return qs(gmdr::tr("Завантаження демо"));
    if (ph == "rendering") return qs(gmdr::tr("Рендер"));
    if (ph == "recovering") return qs(gmdr::tr("Відновлення після збою гри"));
    if (ph == "finalizing") return qs(gmdr::tr("Завершення файлу"));
    if (ph == "processing") return qs(progress_.stage);
    if (ph == "completed") return qs(gmdr::tr("Готово"));
    if (ph == "failed") return qs(gmdr::tr("Помилка"));
    if (ph == "cancelled") return qs(gmdr::tr("Скасовано"));
    return {};
}

QVariantMap JobService::progress() const {
    const auto& p = progress_;
    QVariantMap m;
    m["stage"] = qs(p.stage);
    m["fraction"] = p.fraction;
    m["frames"] = static_cast<double>(p.frames);
    m["videoSeconds"] = p.video_seconds;
    m["expectedSeconds"] = p.expected_seconds;
    m["demoTick"] = p.demo_tick;
    m["demoTotal"] = p.demo_total;
    m["speed"] = p.speed_fps;
    m["elapsed"] = p.elapsed;
    m["eta"] = p.eta;
    m["bytes"] = static_cast<double>(p.bytes_written);
    m["pendingFiles"] = static_cast<double>(p.pending_files);
    m["viaPipe"] = p.frames_via_pipe;
    m["gameRunning"] = p.game_running;
    m["gamePaused"] = p.game_paused;
    m["diskLow"] = p.disk_low;
    m["gameRestarts"] = p.game_restarts;
    m["gameHidden"] = p.game_hidden;
    m["driver"] = qs(p.driver_state);
    m["video"] = qs(p.video_desc);
    m["audio"] = qs(p.audio_desc);
    m["gameWait"] = p.stat_game_wait;
    m["encodeMs"] = p.stat_encode_ms;
    m["blendMs"] = p.stat_blend_ms;
    m["readMs"] = p.stat_read_ms;
    m["decodeMs"] = p.stat_decode_ms;
    m["convertMs"] = p.stat_convert_ms;
    m["audioMs"] = p.stat_audio_ms;
    return m;
}

QVariantList JobService::checks() const { return checks_list(progress_.checks); }

QString JobService::afterDone() const {
    return after_done_ == PowerAction::Shutdown ? "shutdown" : after_done_ == PowerAction::Sleep ? "sleep" : "none";
}

void JobService::setAfterDone(const QString& id) {
    const auto a = parse_power_action(ss(id));
    after_done_ = a ? *a : PowerAction::None;
    emit afterDoneChanged();
}

void JobService::cancelPowerAction() {
    if (after_done_ != PowerAction::None)
        log_info("{}", trf("«{}» після рендеру скасовано", power_action_name(after_done_)));
    after_done_ = PowerAction::None;
    countdown_ = 0;
    countdown_timer_.stop();
    emit afterDoneChanged();
}

void JobService::powerActionNow() {
    countdown_timer_.stop();
    countdown_ = 0;
    const PowerAction a = after_done_;
    after_done_ = PowerAction::None;
    emit afterDoneChanged();
    if (a == PowerAction::None) return;
    config_->save_now();
    log_info("{}", trf("Після рендеру: {}", power_action_name(a)));
    std::string err;
    if (!do_power_action(a, &err)) log_error("{}", trf("Не вдалося {}: {}", power_action_name(a), err));
}

void JobService::checkUpdates() {
    if (update_future_.valid()) return;
    log_info("{}", trf("Перевіряю оновлення на GitHub ({})...", kUpdateRepo));
    update_future_ = std::async(std::launch::async, [] {
        UpdateResult r;
        r.release = fetch_latest_release(kUpdateRepo, &r.error);
        return r;
    });
    update_timer_.start();
    emit updatesChanged();
}

QString JobService::defaultReportName() const { return qs(render::default_report_name()); }

QString JobService::makeReport(const QString& qpath) {
    const std::string path = local_path(qpath);
    config_->save_now();
    render::ReportInput in;
    in.settings_path = path_to_utf8(app_data_dir() / "gmdr_settings.json");
    // Перевірка GPU-кодеків у програмі — у звіт (як і раніше)
    std::string gpu;
    for (const auto& [name, st] : config_->environment().encoders.video)
        if (st.gpu && st.compiled)
            gpu += std::format("  {}: {}\n", name, st.state.ok() ? gmdr::tr("працює")
                                                  : st.state.state == config::Availability::Failed ? gmdr::tr("не працює")
                                                                                                   : gmdr::tr("перевіряється"));
    if (!gpu.empty()) in.extra_text = gmdr::tr("Перевірка GPU-кодеків у програмі:\n") + gpu;
    std::vector<std::string> contents;
    std::string err;
    if (!render::make_problem_report(path_from_utf8(path), in, &contents, &err)) return qs(err.empty() ? "?" : err);
    log_info("{}", trf("Звіт про проблему: {}", path));
    QString list;
    for (const auto& c : contents) list += "\n  • " + qs(c);
    emit message(qs(gmdr::tr("Звіт готовий")),
                 qs(gmdr::tr("Файл: ")) + qs(path) + qs(gmdr::tr("\n\nУсередині:")) + list +
                     qs(gmdr::tr("\n\nШлях до вашого профілю Windows у текстах замінено на %USERPROFILE%. Архів нікуди не "
                           "надсилається — перегляньте його і передайте сам (Discord, GitHub, пошта).")));
    return {};
}

} // namespace gmdr::qt
