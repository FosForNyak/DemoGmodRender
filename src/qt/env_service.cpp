#include "env_service.hpp"

#include "config_model.hpp"
#include "graphics_api.hpp"
#include "qt_convert.hpp"

#include <QMetaObject>
#include <QQuickWindow>
#include <QSysInfo>
#include <QtGlobal>

#include "core/config/dependencies.hpp"
#include "core/config/formats.hpp"
#include "core/game/game_renderer.hpp"
#include "core/game/process.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"
#include "core/util/system_info.hpp"

#include <format>
#include <set>

namespace gmdr::qt {

namespace {

// Ключі налаштувань, від яких залежить огляд середовища
const std::set<std::string> kPathKeys = {"demo_path", "mic_file", "output_path", "container"};
const std::set<std::string> kGameKeys = {"game_dir", "rtx_game_dir", "game_renderer", "game_exe"};
const std::set<std::string> kServiceKeys = {"whisper_cli",  "whisper_model", "deepl_key",  "google_key", "libre_key",
                                            "openai_key",   "elevenlabs_key", "tts_python", "tts_engine", "translator",
                                            "translator_url"};

} // namespace

EnvService::EnvService(ConfigModel* config, std::string startup_api, std::string recovered_api, QObject* parent)
    : QObject(parent), config_(config), startup_api_(std::move(startup_api)), recovered_api_(std::move(recovered_api)) {
    apis_ = platform_graphics_apis();
    env_.graphics.apis = apis_;
    services_timer_.setSingleShot(true);
    services_timer_.setInterval(400);
    connect(&services_timer_, &QTimer::timeout, this, [this] {
        const render::RenderSettings s = config_->settings();
        run_async([this, s] {
            config::EnvironmentCapabilities part;
            config::rescan_services(part, s);
            return [this, part] {
                env_.speech = part.speech;
                env_.translation = part.translation;
                env_.dubbing.omnivoice = part.dubbing.omnivoice;
                env_.dubbing.elevenlabs = part.dubbing.elevenlabs;
                publish();
            };
        });
    });
    game_timer_.setSingleShot(true);
    game_timer_.setInterval(400);
    connect(&game_timer_, &QTimer::timeout, this, [this] {
        const render::RenderSettings s = config_->settings();
        run_async([this, s] {
            config::EnvironmentCapabilities part;
            config::rescan_game(part, s, false);
            return [this, part] {
                const auto running = env_.game.running;
                env_.game = part.game;
                env_.game.running = running;
                env_.filesystem.game_free = part.filesystem.game_free;
                publish();
            };
        });
    });
    connect(config_, &ConfigModel::settingChanged, this, &EnvService::on_setting_changed);
    start_scan();
}

EnvService::~EnvService() {
    closing_ = true;
    std::lock_guard lock(threads_mutex_);
    for (auto& w : threads_)
        if (w.thread.joinable()) w.thread.join();
}

// Робота у фоні: f() повертає функцію, яку виконати у потоці інтерфейсу з результатом
template <class F>
void EnvService::run_async(F&& f) {
    std::lock_guard lock(threads_mutex_);
    // Завершені потоки прибираємо, щоб не накопичувались
    for (auto it = threads_.begin(); it != threads_.end();) {
        if (*it->done) {
            it->thread.join();
            it = threads_.erase(it);
        } else {
            ++it;
        }
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    threads_.push_back({std::thread([this, done, f = std::forward<F>(f)]() mutable {
                            auto apply = f();
                            if (!closing_)
                                QMetaObject::invokeMethod(this, [this, apply = std::move(apply)]() mutable {
                                    if (!closing_) apply();
                                }, Qt::QueuedConnection);
                            *done = true;
                        }),
                        done});
}

void EnvService::start_scan() {
    scanning_ = true;
    emit changed();
    const render::RenderSettings s = config_->settings();
    run_async([this, s] {
        config::EnvironmentCapabilities e = config::scan_environment(s);
        return [this, e]() mutable {
            // Проби GPU-кодеків, що вже є, не губимо
            for (const auto& [name, st] : env_.encoders.video)
                if (st.probed && e.encoders.video.count(name)) e.encoders.video[name] = st;
            e.graphics = env_.graphics;
            env_ = e;
            scanning_ = false;
            publish();
            if (!config::gpu_encoders_to_probe(env_).empty()) start_gpu_probe();
        };
    });
}

void EnvService::start_gpu_probe() {
    if (probing_gpu_) return;
    const auto names = config::gpu_encoders_to_probe(env_);
    if (names.empty()) return;
    probing_gpu_ = true;
    for (const auto& n : names) env_.encoders.video[n].state = {config::Availability::Checking, {}};
    publish();
    run_async([this, names] {
        std::vector<std::pair<std::string, config::CapabilityState>> results;
        int ok = 0;
        for (const auto& n : names) {
            if (closing_) break;
            results.emplace_back(n, config::probe_video_encoder(n));
            ok += results.back().second.ok();
        }
        log_info("{}", trf("Перевірка відеокарти: доступно GPU-кодеків — {}", ok));
        return [this, results] {
            for (const auto& [n, st] : results) config::set_encoder_probe(env_, n, st);
            env_.encoders.gpu_probe_done = true;
            probing_gpu_ = false;
            publish();
        };
    });
}

void EnvService::publish() {
    env_.graphics.apis = apis_;
    if (window_) env_.graphics.active = active_graphics_api(window_);
    config_->set_environment(env_);
    emit changed();
}

void EnvService::set_window(QQuickWindow* w) {
    window_ = w;
    if (!w) return;
    connect(w, &QQuickWindow::sceneGraphInitialized, this, [this] { publish(); }, Qt::QueuedConnection);
}

void EnvService::on_setting_changed(const QString& key) {
    const std::string k = ss(key);
    if (kPathKeys.count(k)) {
        config::rescan_paths(env_, config_->settings());
        publish();
    }
    if (kGameKeys.count(k)) game_timer_.start();
    if (kServiceKeys.count(k)) services_timer_.start();
}

void EnvService::rescan() {
    for (auto& [name, st] : env_.encoders.video) st.probed = false;
    start_scan();
}

void EnvService::probeGpuEncoders() {
    for (auto& [name, st] : env_.encoders.video)
        if (st.gpu) st.probed = false;
    start_gpu_probe();
}

void EnvService::probeGraphicsApis() {
    if (probing_graphics_) return;
    probing_graphics_ = true;
    for (auto& a : apis_)
        if (a.state == config::Availability::Unknown) a.state = config::Availability::Checking;
    emit changed();
    // OpenGL і Vulkan перевіряються з потоку інтерфейсу (контекст OpenGL прив'язаний до потоку),
    // але після того, як QML встиг показати «перевіряється...»
    QTimer::singleShot(50, this, [this] {
        apis_ = probe_graphics_apis();
        probing_graphics_ = false;
        publish();
    });
}

void EnvService::checkGameRunning() {
    run_async([this] {
        const bool on = !game::GameProcess::find_by_name({"gmod.exe", "hl2.exe", "gmod", "hl2_linux"}, true).empty();
        const config::CapabilityState running = on ? config::CapabilityState{config::Availability::Available, gmdr::tr("Garry's Mod уже запущено")}
                                                   : config::CapabilityState{config::Availability::Unavailable, {}};
        return [this, running] {
            if (running.state == env_.game.running.state) return;
            env_.game.running = running;
            publish();
        };
    });
}

QString EnvService::graphicsApiLabel(const QString& id) const { return qs(graphics_api_label(ss(id))); }

QVariantList EnvService::gpus() const {
    QVariantList out;
    for (const auto& g : env_.hardware.gpus) {
        QVariantMap m;
        m["name"] = qs(g.name);
        m["vendor"] = qs(g.vendor);
        m["vram"] = static_cast<double>(g.vram_bytes);
        m["driver"] = qs(g.driver);
        m["software"] = g.software;
        out << m;
    }
    return out;
}

QVariantList EnvService::gpuEncoders() const {
    QVariantList out;
    for (const auto& e : config::video_encoders()) {
        if (!e.gpu) continue;
        const auto it = env_.encoders.video.find(e.name);
        if (it == env_.encoders.video.end() || !it->second.compiled) continue;
        QVariantMap m;
        m["name"] = qs(e.name);
        m["label"] = qs(gmdr::tr(e.label.c_str()));
        m["vendor"] = qs(e.vendor);
        m["state"] = config::availability_id(it->second.state.state);
        m["detail"] = qs(it->second.state.detail);
        out << m;
    }
    return out;
}

QVariantList EnvService::graphicsApis() const {
    QVariantList out;
    const std::string active = window_ ? active_graphics_api(window_) : std::string();
    for (const auto& a : apis_) {
        QVariantMap m;
        m["id"] = qs(a.id);
        m["label"] = qs(a.label);
        m["state"] = config::availability_id(a.state);
        m["detail"] = qs(a.detail);
        m["active"] = !active.empty() && a.id == active;
        out << m;
    }
    return out;
}

QString EnvService::activeGraphicsApi() const { return window_ ? qs(active_graphics_api(window_)) : QString(); }
QString EnvService::startupGraphicsApi() const { return qs(startup_api_); }
QString EnvService::recoveredGraphicsApi() const { return qs(recovered_api_); }

QVariantMap EnvService::system() const {
    QVariantMap m;
    m["app"] = QStringLiteral(GMDR_VERSION);
    m["qt"] = QString::fromLatin1(qVersion());
    m["os"] = qs(env_.platform.os_label.empty() ? os_description() : env_.platform.os_label);
    m["cpu"] = qs(cpu_name());
    m["cpuThreads"] = static_cast<int>(env_.hardware.cpu_threads);
    m["ram"] = static_cast<double>(env_.hardware.ram_bytes);
    m["ramFree"] = static_cast<double>(available_memory());
    m["ffmpeg"] = qs(env_.media.ffmpeg_version);
    m["gpuListOk"] = env_.hardware.gpu_info.ok();
    m["gpuListDetail"] = qs(env_.hardware.gpu_info.detail);
    m["outputFree"] = static_cast<double>(env_.filesystem.output_free);
    m["gameFree"] = static_cast<double>(env_.filesystem.game_free);
    m["arch"] = QSysInfo::currentCpuArchitecture();
    return m;
}

QVariantList EnvService::dependencies() const {
    QVariantList out;
    for (const auto& d : config::dependencies(config_->settings(), env_)) {
        QVariantMap m;
        m["id"] = qs(d.id);
        m["label"] = qs(d.label);
        m["purpose"] = qs(d.purpose);
        m["state"] = config::dependency_state_id(d.state);
        m["detail"] = qs(d.detail);
        m["required"] = d.required;
        m["action"] = config::action_id(d.action);
        m["url"] = qs(d.url);
        out << m;
    }
    return out;
}

QVariantList EnvService::games() const {
    QVariantList out;
    for (const auto* r : game::game_renderers()) {
        QVariantMap m;
        m["renderer"] = qs(r->id());
        m["label"] = qs(r->label());
        m["description"] = qs(r->description());
        m["url"] = qs(r->install_url());
        m["dirSetting"] = qs(r->traits().dir_setting);
        m["maxParallel"] = r->traits().max_parallel;
        if (const auto* g = env_.game.find(r->id())) {
            m["state"] = config::availability_id(g->install.state);
            m["detail"] = qs(g->install.detail);
            m["fromSettings"] = g->from_settings;
            m["has64bit"] = g->has_64bit;
            m["accepted"] = g->accepted;
            m["driver"] = qs(g->driver);
        } else {
            m["state"] = config::availability_id(config::Availability::Unknown);
        }
        out << m;
    }
    return out;
}

} // namespace gmdr::qt
