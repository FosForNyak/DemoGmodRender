#include "shell_service.hpp"

#include "qt_convert.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QWindow>

#include "core/util/file_assoc.hpp"
#include "core/util/file_util.hpp"
#include "core/util/i18n.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace gmdr::qt {

#ifdef _WIN32
namespace {

constexpr ULONG_PTR kCopyDataOpen = 0x52444D47;    // "GMDR": шлях від другого запуску (як у старому вікні)
constexpr UINT      kTrayMessage = WM_APP + 1;
const wchar_t*      kWindowClass = L"GModDemoRenderWnd";   // за цим класом друга копія шукає першу

ShellService* g_shell = nullptr;

} // namespace

// Приховане вікно-приймач: повідомлення від другої копії програми і від значка в треї
struct ShellService::Native {
    HWND           msg = nullptr;
    HWND           main = nullptr;
    HICON          icon = nullptr;
    ITaskbarList3* taskbar = nullptr;
    bool           taskbar_tried = false;
    NOTIFYICONDATAW tray{};
    bool           tray_shown = false;

    static LRESULT CALLBACK proc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp) {
        if (m == WM_COPYDATA && g_shell) {
            const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lp);
            if (!cds || cds->dwData != kCopyDataOpen) return FALSE;
            const std::string file = cds->lpData && cds->cbData > 0
                                         ? std::string(static_cast<const char*>(cds->lpData), cds->cbData)
                                         : std::string();
            QMetaObject::invokeMethod(g_shell, [file] {
                g_shell->restore();
                if (!file.empty()) emit g_shell->openRequested(qs(file));
            }, Qt::QueuedConnection);
            return TRUE;
        }
        if (m == kTrayMessage && g_shell) {
            if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK || lp == WM_RBUTTONUP || lp == NIN_BALLOONUSERCLICK)
                QMetaObject::invokeMethod(g_shell, [] { g_shell->restore(); }, Qt::QueuedConnection);
            return 0;
        }
        return DefWindowProcW(hwnd, m, wp, lp);
    }

    bool tray_add() {
        if (tray_shown) return true;
        tray = {};
        tray.cbSize = sizeof(tray);
        tray.hWnd = msg;
        tray.uID = 1;
        tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        tray.uCallbackMessage = kTrayMessage;
        tray.hIcon = icon;
        wcsncpy(tray.szTip, L"GMod Demo Render", ARRAYSIZE(tray.szTip) - 1);
        tray_shown = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
        return tray_shown;
    }
    void tray_remove() {
        if (!tray_shown) return;
        Shell_NotifyIconW(NIM_DELETE, &tray);
        tray_shown = false;
    }
};

bool ShellService::forward_to_running_instance(const std::vector<std::string>& args) {
    // Для розробки й автотестів; GMDR_RESTARTED — перезапуск самої програми (стара копія ще закривається)
    if (std::getenv("GMDR_MULTI_INSTANCE") || std::getenv("GMDR_RESTARTED")) return false;
    // М'ютекс живе, доки працює перша копія програми
    static const HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\GModDemoRender.instance");
    static const bool another = mutex && GetLastError() == ERROR_ALREADY_EXISTS;
    if (!another) return false;
    HWND other = FindWindowW(kWindowClass, nullptr);
    if (!other) return false;   // перша копія ще не створила вікно — працюємо як звичайно
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);
    std::string file;
    for (size_t i = 1; i < args.size(); ++i)
        if (!args[i].empty() && args[i][0] != '-') {
            file = args[i];
            break;
        }
    COPYDATASTRUCT cds{kCopyDataOpen, static_cast<DWORD>(file.size()), file.empty() ? nullptr : file.data()};
    DWORD_PTR result = 0;
    SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 5000, &result);
    return true;
}

ShellService::ShellService(QObject* parent) : QObject(parent), native_(new Native) {
    g_shell = this;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &Native::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
    // Звичайне (не message-only) приховане вікно: FindWindow знаходить і сховані вікна верхнього рівня
    native_->msg = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"GMod Demo Render", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                   wc.hInstance, nullptr);
    native_->icon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(101));
    if (!native_->icon) native_->icon = LoadIconW(nullptr, IDI_APPLICATION);
}

ShellService::~ShellService() {
    native_->tray_remove();
    if (native_->taskbar) native_->taskbar->Release();
    if (native_->msg) DestroyWindow(native_->msg);
    g_shell = nullptr;
    delete native_;
}

void ShellService::attach(QWindow* window) {
    window_ = window;
    native_->main = reinterpret_cast<HWND>(window->winId());
    connect(window, &QWindow::windowStateChanged, this, &ShellService::on_window_state);
}

void ShellService::taskbar(int state, double fraction) {
    auto& n = *native_;
    if (!n.taskbar_tried) {
        n.taskbar_tried = true;
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&n.taskbar)))) n.taskbar = nullptr;
        else if (FAILED(n.taskbar->HrInit())) {
            n.taskbar->Release();
            n.taskbar = nullptr;
        }
    }
    if (!n.taskbar || !n.main) return;
    const TBPFLAG flags[] = {TBPF_NOPROGRESS, TBPF_NORMAL, TBPF_PAUSED, TBPF_ERROR, TBPF_INDETERMINATE};
    n.taskbar->SetProgressState(n.main, flags[std::clamp(state, 0, 4)]);
    if (state >= 1 && state <= 3)
        n.taskbar->SetProgressValue(n.main, static_cast<ULONGLONG>(std::clamp(fraction, 0.0, 1.0) * 1000), 1000);
}

void ShellService::tray(bool show, const std::string& tooltip) {
    auto& n = *native_;
    if (!show) {
        if (window_hidden() && minimize_to_tray_) return;   // вікно в треї — значок потрібен, щоб його повернути
        n.tray_remove();
        return;
    }
    if (!n.tray_add()) return;
    const std::wstring tip = utf8_to_wide(tooltip.empty() ? "GMod Demo Render" : tooltip);
    if (tip == n.tray.szTip) return;
    wcsncpy(n.tray.szTip, tip.c_str(), ARRAYSIZE(n.tray.szTip) - 1);
    n.tray.szTip[ARRAYSIZE(n.tray.szTip) - 1] = 0;
    n.tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &n.tray);
}

void ShellService::notify(const std::string& title, const std::string& text) {
    auto& n = *native_;
    if (!n.tray_add()) return;   // сповіщення показується від імені значка в треї
    NOTIFYICONDATAW d = n.tray;
    d.uFlags = NIF_INFO;
    wcsncpy(d.szInfoTitle, utf8_to_wide(title).c_str(), ARRAYSIZE(d.szInfoTitle) - 1);
    wcsncpy(d.szInfo, utf8_to_wide(text).c_str(), ARRAYSIZE(d.szInfo) - 1);
    d.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    d.hBalloonIcon = n.icon;
    Shell_NotifyIconW(NIM_MODIFY, &d);
}

void ShellService::flash() {
    HWND h = native_->main;
    if (!h || GetForegroundWindow() == h) return;
    FLASHWINFO fi{};
    fi.cbSize = sizeof(fi);
    fi.hwnd = h;
    fi.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
    fi.uCount = 3;
    FlashWindowEx(&fi);
}

bool ShellService::isWindows() const { return true; }

bool ShellService::demAssociated() const {
    return dem_association_registered(path_from_utf8(ss(QCoreApplication::applicationFilePath())));
}

bool ShellService::setDemAssociation(bool on) {
    std::string err;
    const auto exe = path_from_utf8(ss(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    const bool ok = on ? register_dem_association(exe, &err) : unregister_dem_association(&err);
    if (ok)
        log_info("{}", on ? gmdr::tr("Файли .dem тепер відкриваються в GMod Demo Render (якщо Windows спитає, чим "
                               "відкривати, — виберіть її). Вимкнути — тут само.")
                          : gmdr::tr("Файли .dem більше не відкриваються цією програмою"));
    else
        log_error("{}", trf("Не вдалося змінити асоціацію .dem: {}", err));
    emit associationChanged();
    return ok;
}

void ShellService::showInFolder(const QString& path) {
    const std::wstring args = L"/select,\"" + utf8_to_wide(ss(QDir::toNativeSeparators(path))) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

#else  // ---- інші ОС ----

struct ShellService::Native {};

bool ShellService::forward_to_running_instance(const std::vector<std::string>&) { return false; }
ShellService::ShellService(QObject* parent) : QObject(parent), native_(new Native) {}
ShellService::~ShellService() { delete native_; }
void ShellService::attach(QWindow* window) {
    window_ = window;
    connect(window, &QWindow::windowStateChanged, this, &ShellService::on_window_state);
}
void ShellService::taskbar(int, double) {}
void ShellService::tray(bool, const std::string&) {}
void ShellService::notify(const std::string& title, const std::string& text) { log_info("{}: {}", title, text); }
void ShellService::flash() {
    if (window_) window_->alert(3000);
}
bool ShellService::isWindows() const { return false; }
bool ShellService::demAssociated() const { return false; }
bool ShellService::setDemAssociation(bool) { return false; }
void ShellService::showInFolder(const QString& path) { QDesktopServices::openUrl(folderUrl(path)); }

#endif

bool ShellService::systemLight() const {
#ifdef _WIN32
    const QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        QSettings::NativeFormat);
    return reg.value("AppsUseLightTheme", 0).toInt() != 0;
#else
    return false;
#endif
}

bool ShellService::window_hidden() const {
    return window_ && (!window_->isVisible() || window_->windowStates().testFlag(Qt::WindowMinimized));
}

void ShellService::restore() {
    if (!window_) return;
    window_->show();
    if (window_->windowStates().testFlag(Qt::WindowMinimized)) window_->setWindowStates(window_->windowStates() & ~Qt::WindowMinimized);
    window_->raise();
    window_->requestActivate();
}

void ShellService::setMinimizeToTray(bool on) {
    minimize_to_tray_ = on;
    emit trayChanged();
}

// Згорнуте вікно — у трей (якщо ввімкнено): зникає з панелі задач, лишається значок
void ShellService::on_window_state() {
    if (!window_ || !minimize_to_tray_ || !isWindows()) return;
    if (window_->windowStates().testFlag(Qt::WindowMinimized)) {
        tray(true, "GMod Demo Render");
        window_->hide();
    }
}

void ShellService::openPath(const QString& path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }

void ShellService::copyText(const QString& text) {
    if (auto* c = QGuiApplication::clipboard()) c->setText(text);
}

QString ShellService::localPath(const QUrl& url) const { return QDir::toNativeSeparators(url.toLocalFile()); }
QUrl    ShellService::fileUrl(const QString& path) const { return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path); }

QUrl ShellService::folderUrl(const QString& path) const {
    if (path.isEmpty()) return {};
    QFileInfo fi(path);
    return QUrl::fromLocalFile(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
}

QString ShellService::parentFolder(const QString& path) const { return QDir::toNativeSeparators(QFileInfo(path).absolutePath()); }
bool    ShellService::exists(const QString& path) const { return !path.isEmpty() && QFileInfo::exists(path); }
bool    ShellService::isDirectory(const QString& path) const { return QFileInfo(path).isDir(); }

void ShellService::openAppFolder() { openPath(QCoreApplication::applicationDirPath()); }
void ShellService::openLogFile() { openPath(logPath()); }

QString ShellService::logPath() const { return qs(path_to_utf8(app_data_dir() / "gmdr_log.txt")); }
QString ShellService::settingsPath() const { return qs(path_to_utf8(app_data_dir() / "gmdr_settings.json")); }

QVariantList ShellService::uiLanguages() const {
    QVariantList out;
    for (const auto& l : ui_languages()) {
        QVariantMap m;
        m["code"] = qs(l.code);
        m["native"] = qs(l.native);
        m["english"] = qs(l.english);
        out << m;
    }
    return out;
}

QString ShellService::formatBytes(double bytes) const { return qs(format_bytes(static_cast<uint64_t>(std::max(0.0, bytes)))); }

QString ShellService::formatDate(double unix_seconds) const {
    const QDateTime t = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(unix_seconds));
    return QLocale().toString(t, QLocale::ShortFormat);
}

void ShellService::restartApp(const QStringList& extra) {
    // Графічний API з минулого перезапуску не перебиває новий вибір у налаштуваннях
    QStringList args;
    const QStringList was = QCoreApplication::arguments().mid(1);
    for (int i = 0; i < was.size(); ++i) {
        if (was[i] == "--graphics-api") {
            ++i;
            continue;
        }
        args << was[i];
    }
    args << extra;
    emit restarting();   // зберегти налаштування до запуску нової копії
    QProcess p;
    p.setProgram(QCoreApplication::applicationFilePath());
    p.setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GMDR_RESTARTED", "1");
    p.setProcessEnvironment(env);
    p.startDetached();
    QCoreApplication::quit();
}

} // namespace gmdr::qt
