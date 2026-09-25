// =============================================================================
//  main_qt.cpp — програма з вікном на Qt Quick.
//
//  Порядок запуску:
//    1. журнал (gmdr_log.txt), налаштування, мова;
//    2. друга копія програми — передати файл першій і вийти;
//    3. графічний API вікна: з налаштувань (--graphics-api перебиває). Якщо минулий
//       запуск з цим API не дійшов до першого кадру — «Автоматично» і пояснення;
//    4. сервіси для QML (Config, Env, Project, Jobs, Queue, Library, Log, Shell, Voices),
//       інтерфейс — src/qt/qml.
//  Тести: GMDR_SCREENSHOT=файл.png — знімок вікна після завантаження і вихід
//  (разом із QT_QPA_PLATFORM=offscreen), GMDR_TEST_PAGE — робочий простір.
// =============================================================================
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml>

#include "config_model.hpp"
#include "env_service.hpp"
#include "graphics_api.hpp"
#include "icon_item.hpp"
#include "job_service.hpp"
#include "library_model.hpp"
#include "log_model.hpp"
#include "preview_item.hpp"
#include "project_service.hpp"
#include "qt_convert.hpp"
#include "queue_model.hpp"
#include "shell_service.hpp"
#include "timeline_item.hpp"
#include "translator.hpp"
#include "voices_service.hpp"

#include "core/game/audio_mute.hpp"
#include "core/media/ffmpeg_util.hpp"
#include "core/render/jobs.hpp"
#include "core/util/crash_dump.hpp"
#include "core/util/file_util.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

using namespace gmdr;
namespace fs = std::filesystem;

namespace {

std::string now_stamp() {
    const std::time_t t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return buf;
}

// Журнал у файл поруч із даними програми (як і раніше), не більше 8 МБ
void open_log_file() {
    static std::ofstream file;
    static std::mutex    m;
    const fs::path path = app_data_dir() / "gmdr_log.txt";
    if (file_size_or_zero(path) > 8ull * 1024 * 1024) {
        std::error_code ec;
        fs::rename(path, app_data_dir() / "gmdr_log.old.txt", ec);
        if (ec) remove_file_quiet(path);
    }
    file.open(path, std::ios::app);
    add_log_sink([](LogLevel l, const std::string& text) {
        std::lock_guard lock(m);
        if (file) file << now_stamp() << " [" << log_level_name(l) << "] " << text << "\n" << std::flush;
    });
}

std::vector<std::string> utf8_args(int argc, char** argv) {
    std::vector<std::string> out;
    const QStringList list = QCoreApplication::arguments();
    if (!list.isEmpty())
        for (const auto& a : list) out.push_back(qt::ss(a));
    else
        for (int i = 0; i < argc; ++i) out.push_back(argv[i]);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    install_crash_handler(app_data_dir());
    set_min_log_level(LogLevel::Debug);
    open_log_file();
    media::install_ffmpeg_log_bridge(AV_LOG_ERROR);

    // ---- налаштування і мова (до першого тексту) ----
    render::RenderSettings early;
    const std::string settings_path = path_to_utf8(app_data_dir() / "gmdr_settings.json");
    std::string settings_err;
    std::error_code ec;
    const bool settings_ok = fs::exists(path_from_utf8(settings_path), ec) && render::load_settings(early, settings_path, &settings_err);
    // Мова: налаштування, а для тестів — змінна GMDR_LANG
    set_ui_language(std::getenv("GMDR_LANG") ? std::string(std::getenv("GMDR_LANG")) : early.ui_language);

    // ---- графічний API вікна (до створення QGuiApplication) ----
    std::string api = early.ui_graphics_api.empty() ? "auto" : early.ui_graphics_api;
    std::string recovered;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--graphics-api") api = argv[i + 1];
    const std::string failed = qt::take_failed_graphics_api();
    if (!failed.empty() && failed == api) {
        // Минулого разу з цим API вікно не показалось (збій, зависання) — повертаємо «Автоматично»
        recovered = api;
        api = "auto";
        log_warn("{}", trf("Минулого разу вікно не запустилось з графічним API «{}» — повернуто «Автоматично»",
                           qt::graphics_api_label(recovered)));
    }
    const std::string requested = api;   // з чим запущено (для «потрібен перезапуск» у налаштуваннях)
    if (std::getenv("QT_QPA_PLATFORM") && std::string(std::getenv("QT_QPA_PLATFORM")) == "offscreen" && api == "auto")
        api = "software";   // знімки вікна в CI: без відеокарти
    const std::string applied = qt::apply_graphics_api(api);
    qt::mark_graphics_pending(applied);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("GMod Demo Render");
    QGuiApplication::setOrganizationName("GModDemoRender");
    QGuiApplication::setApplicationVersion(GMDR_VERSION);
    QGuiApplication::setWindowIcon(QIcon(":/gmdr/app_icon.png"));
    const auto args = utf8_args(argc, argv);
    if (qt::ShellService::forward_to_running_instance(args)) {
        qt::mark_graphics_ok();
        return 0;
    }
    QQuickStyle::setStyle("Basic");   // вигляд — з власних компонентів (qml/components)
    auto* translator = new qt::CoreTranslator(&app);
    QCoreApplication::installTranslator(translator);

    log_info("{}", trf("GMod Demo Render {} — рендер демо Garry's Mod у відео", GMDR_VERSION));
    log_info("FFmpeg: libavcodec {}.{}.{}; Qt {}", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO,
             qVersion());

    // ---- сервіси ----
    auto* config = new qt::ConfigModel(&app);
    if (settings_ok) {
        config->load(settings_path, nullptr);
        log_info("{}", trf("Налаштування завантажено"));
    } else {
        if (!settings_err.empty()) log_warn("{}", trf("Не вдалося прочитати налаштування: {}", settings_err));
        config->load(settings_path, nullptr);   // шлях для збереження
    }
    if (!recovered.empty()) config->set("ui_graphics_api", "auto");
    auto* shell = new qt::ShellService(&app);
    shell->setMinimizeToTray(config->settings().minimize_to_tray);
    shell->set_startup_language(qt::qs(early.ui_language));
    auto* env = new qt::EnvService(config, requested, recovered, &app);
    auto* project = new qt::ProjectService(config, &app);
    auto* queue = new qt::QueueModel(config, project, &app);
    auto* jobs = new qt::JobService(config, project, queue, shell, &app);
    auto* library = new qt::LibraryModel(config, env, &app);
    auto* logs = new qt::LogModel(&app);
    auto* voices = new qt::VoicesService(config, env, &app);
    QObject::connect(env, &qt::EnvService::changed, queue, &qt::QueueModel::revalidate);
    QObject::connect(shell, &qt::ShellService::openRequested, project, &qt::ProjectService::open);
    // Після завдання: встановлено рушій чи модель — огляд сервісів; бібліотека голосів могла поповнитись
    QObject::connect(jobs, &qt::JobService::finished, env, [jobs, env, voices] {
        const QString kind = jobs->lastResult().value("kind").toString();
        if (kind == "voiceEngine" || kind == "download") env->refreshServices();
        voices->refreshProfiles();
    });
    QObject::connect(shell, &qt::ShellService::restarting, config, [config] { config->save_now(); });
    QObject::connect(config, &qt::ConfigModel::settingChanged, shell, [config, shell](const QString& key) {
        if (key == "minimize_to_tray") shell->setMinimizeToTray(config->settings().minimize_to_tray);
    });
    qt::PreviewItem::set_source(jobs);
    qt::TimelineItem::set_project(project);

    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Config", config);
    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Env", env);
    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Project", project);
    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Jobs", jobs);
    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Queue", queue);
    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Library", library);
    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Log", logs);
    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Shell", shell);
    qmlRegisterSingletonInstance("Gmdr", 1, 0, "Voices", voices);
    qmlRegisterType<qt::IconItem>("Gmdr", 1, 0, "Icon");
    qmlRegisterType<qt::LogoItem>("Gmdr", 1, 0, "Logo");
    qmlRegisterType<qt::PreviewItem>("Gmdr", 1, 0, "Preview");
    qmlRegisterType<qt::TimelineItem>("Gmdr", 1, 0, "Timeline");

    if (game::game_audio_mute_pending())
        log_warn("{}", trf("Минулий рендер перервався, і звук Garry's Mod міг лишитися вимкненим у мікшері гучності Windows. "
                           "Програма ввімкне його під час наступного рендеру (або ввімкніть його в мікшері вручну)."));

    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/");
    engine.rootContext()->setContextProperty("startupPage", qt::qs(std::getenv("GMDR_TEST_PAGE") ? std::getenv("GMDR_TEST_PAGE") : ""));
    engine.load(QUrl("qrc:/Gmdr/Ui/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        log_error("Не вдалося завантажити інтерфейс (QML)");
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window) {
        // Для знімків вікна в тестах: GMDR_WINDOW_SIZE=1920x1080
        if (const char* sz = std::getenv("GMDR_WINDOW_SIZE"))
            if (auto wh = parse_size(sz)) window->resize(wh->first, wh->second);
        shell->attach(window);
        env->set_window(window);
        // Перший кадр показано — графічний API працює
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [] { qt::mark_graphics_ok(); }, Qt::SingleShotConnection);
        // Відеокарта не змогла малювати вікно з цим API: «Автоматично» і перезапуск (замість аварійного виходу Qt)
        QObject::connect(window, &QQuickWindow::sceneGraphError, window,
                         [config, shell, applied](QQuickWindow::SceneGraphError, const QString& msg) {
                             log_error("{}", trf("Графічний API «{}» не запустився: {}", qt::graphics_api_label(applied), qt::ss(msg)));
                             qt::mark_graphics_ok();
                             if (applied == "auto" || applied == "software") {
                                 config->set("ui_graphics_api", "software");
                                 config->save_now();
                                 shell->restartApp({"--graphics-api", "software"});
                             } else {
                                 config->set("ui_graphics_api", "auto");
                                 config->save_now();
                                 shell->restartApp({"--graphics-api", "auto"});
                             }
                         });
    }

    // Демо з командного рядка (перетягнули на .exe, подвійний клік на .dem)
    for (size_t i = 1; i < args.size(); ++i)
        if (ends_with_i(args[i], ".dem")) {
            project->open(qt::qs(args[i]));
            break;
        }

    // Знімок вікна для автотестів
    if (const char* shot = std::getenv("GMDR_SCREENSHOT"); shot && window) {
        const int delay = std::getenv("GMDR_SCREENSHOT_DELAY") ? std::atoi(std::getenv("GMDR_SCREENSHOT_DELAY")) : 1500;
        QTimer::singleShot(delay, window, [window, shot] {
            const QImage img = window->grabWindow();
            const bool ok = !img.isNull() && img.save(QString::fromLocal8Bit(shot));
            log_info("Знімок вікна: {} ({})", shot, ok ? "ok" : "помилка");
            QCoreApplication::exit(ok ? 0 : 3);
        });
    }
    const int rc = app.exec();
    config->save_now();
    return rc;
}
