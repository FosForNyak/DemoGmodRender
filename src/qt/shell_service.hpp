// =============================================================================
//  shell_service.hpp — усе, що пов'язує програму з ОС (синглтон Shell): відкрити
//  файл чи теку, показати файл у провіднику, буфер обміну, асоціація .dem, прогрес
//  на кнопці в панелі задач, значок у треї і сповіщення, блимання кнопкою, одна
//  копія програми (подвійний клік на .dem відкриває його у вже запущеній).
//
//  Windows — Win32 (ті самі механізми, що й у старому вікні); в інших ОС частина
//  з цього нічого не робить.
// =============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <string>
#include <vector>

class QWindow;

namespace gmdr::qt {

class ShellService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool demAssociated READ demAssociated NOTIFY associationChanged)
    Q_PROPERTY(bool isWindows READ isWindows CONSTANT)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY trayChanged)
    Q_PROPERTY(bool systemLight READ systemLight CONSTANT)   // у системі — світла тема застосунків

public:
    explicit ShellService(QObject* parent = nullptr);
    ~ShellService() override;

    // Друга копія програми: передати файл першій і вийти (true). Лише Windows.
    static bool forward_to_running_instance(const std::vector<std::string>& args);

    void attach(QWindow* window);
    void taskbar(int state, double fraction);   // 0 немає, 1 звичайний, 2 пауза, 3 помилка, 4 невизначений
    void tray(bool show, const std::string& tooltip);
    void notify(const std::string& title, const std::string& text);
    void flash();
    bool window_hidden() const;
    void restore();

    bool isWindows() const;
    bool systemLight() const;
    bool demAssociated() const;
    bool minimizeToTray() const { return minimize_to_tray_; }
    void setMinimizeToTray(bool on);

    Q_INVOKABLE void    openPath(const QString& path);
    Q_INVOKABLE void    showInFolder(const QString& path);
    Q_INVOKABLE void    copyText(const QString& text);
    Q_INVOKABLE QString localPath(const QUrl& url) const;
    Q_INVOKABLE QUrl    fileUrl(const QString& path) const;
    Q_INVOKABLE QUrl    folderUrl(const QString& path) const;   // тека файлу (для діалогів)
    Q_INVOKABLE QString parentFolder(const QString& path) const;
    Q_INVOKABLE bool    exists(const QString& path) const;
    Q_INVOKABLE bool    isDirectory(const QString& path) const;
    Q_INVOKABLE bool    setDemAssociation(bool on);
    Q_INVOKABLE void    openAppFolder();
    Q_INVOKABLE void    openLogFile();
    Q_INVOKABLE QString formatBytes(double bytes) const;
    Q_INVOKABLE QString formatDate(double unix_seconds) const;
    Q_INVOKABLE void    restartApp(const QStringList& extraArgs);

signals:
    void openRequested(const QString& path);   // друга копія передала файл
    void associationChanged();
    void trayChanged();

private:
    void on_window_state();

    QWindow* window_ = nullptr;
    bool     minimize_to_tray_ = false;
    struct Native;
    Native*  native_ = nullptr;
};

} // namespace gmdr::qt
