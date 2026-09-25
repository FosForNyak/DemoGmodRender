// Головне вікно: верхня панель (демо, режим, перевірка, тест і рендер), бічна навігація по
// робочих просторах, робочий простір, рядок стану; діалоги, що потребують дії користувача,
// гарячі клавіші і перетягування файлів.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Gmdr
import Gmdr.Ui

B.ApplicationWindow {
    id: win
    width: Math.min(Screen.desktopAvailableWidth * 0.9, Theme.px(1500))
    height: Math.min(Screen.desktopAvailableHeight * 0.9, Theme.px(940))
    minimumWidth: Theme.px(960)
    minimumHeight: Theme.px(600)
    visible: true
    color: Theme.bg
    title: "GMod Demo Render" + (Project.demoName !== "" ? " — " + Project.demoName : "")
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody

    readonly property bool busy: Jobs.busy
    readonly property bool sidebarCollapsed: (Config.revision, Config.value("ui_sidebar_collapsed") === true)

    // ---- робочі простори ----
    readonly property var pages: [
        { id: "project", label: qsTr("Проєкт"), icon: "project", group: qsTr("Проєкт"), advanced: false },
        { id: "edit", label: qsTr("Монтаж"), icon: "edit", group: qsTr("Робота"), advanced: false },
        { id: "render", label: qsTr("Рендер"), icon: "render", group: qsTr("Робота"), advanced: false },
        { id: "audio", label: qsTr("Звук"), icon: "wave", group: qsTr("Робота"), advanced: false },
        { id: "ai", label: qsTr("Переклад і озвучення"), icon: "translate", group: qsTr("Робота"), advanced: false },
        { id: "library", label: qsTr("Бібліотека"), icon: "library", group: qsTr("Медіа"), advanced: false },
        { id: "queue", label: qsTr("Черга"), icon: "queue", group: qsTr("Медіа"), advanced: false },
        { id: "settings", label: qsTr("Налаштування"), icon: "gear", group: qsTr("Система"), advanced: false },
        { id: "log", label: qsTr("Журнал"), icon: "terminal", group: qsTr("Система"), advanced: true }
    ]
    readonly property var visiblePages: (Config.revision, pages.filter(p => !p.advanced || Config.advanced))
    function pageIndex(id) {
        for (let i = 0; i < pages.length; ++i) if (pages[i].id === id) return i
        return 0
    }

    Component.onCompleted: {
        const p = startupPage !== "" ? startupPage : String(Config.value("ui_page") || "project")
        Ui.page = pages.some(x => x.id === p) ? p : "project"
        if (Object.keys(Jobs.resumeOffer).length > 0) resumeDialog.open()
        if (Env.recoveredGraphicsApi !== "") graphicsRecovered.open()
    }
    Connections {
        target: Ui
        function onPageChanged() { Config.set("ui_page", Ui.page) }
        function onOpenDemoRequested() { openDemoDialog.open() }
        function onToast(text, severity) { toast.show(text, severity) }
        function onConsentRequested(forLibrary) { consentDialog.forLibrary = forLibrary; consentDialog.open() }
        function onModelInstallRequested() { modelDialog.open() }
        function onVoiceEngineInstallRequested() { engineDialog.open() }
    }

    // ---- дії ----
    function startRender() {
        if (!Project.loaded) { openDemoDialog.open(); return }
        if (Config.errorCount > 0) { Ui.goTo("render"); toast.show(qsTr("Спершу виправте помилки налаштувань"), "error"); return }
        Jobs.startRender(false)
    }
    function startTest() {
        if (!Project.loaded) { openDemoDialog.open(); return }
        Jobs.startTest()
    }

    // ---- верхня панель ----
    header: TopBar {
        onOpenDemo: openDemoDialog.open()
        onStartRender: win.startRender()
        onStartTest: win.startTest()
        onShowMenu: (item) => appMenu.popup(item, 0, item.height)
    }

    // ---- рядок стану ----
    footer: StatusBar {}

    RowLayout {
        anchors.fill: parent
        spacing: 0
        // ---- бічна навігація ----
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: win.sidebarCollapsed ? Theme.sidebarCollapsed : Theme.sidebarWidth
            color: Theme.surface
            Behavior on Layout.preferredWidth { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.s2
                anchors.topMargin: Theme.s3
                spacing: 1
                Repeater {
                    model: win.visiblePages
                    ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            visible: !win.sidebarCollapsed && (index === 0 || win.visiblePages[index - 1].group !== modelData.group)
                            Layout.fillWidth: true
                            Layout.topMargin: index === 0 ? 0 : Theme.s3
                            Layout.bottomMargin: Theme.s1
                            leftPadding: Theme.s3
                            text: modelData.group
                            role: "meta"
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.6
                        }
                        Rectangle {
                            visible: win.sidebarCollapsed && index > 0 && win.visiblePages[index - 1].group !== modelData.group
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            Layout.margins: Theme.s2
                            color: Theme.border
                        }
                        NavItem {
                            Layout.fillWidth: true
                            iconName: modelData.icon
                            label: modelData.label
                            collapsed: win.sidebarCollapsed
                            current: Ui.page === modelData.id
                            shortcut: index < 9 ? "Ctrl+" + (index + 1) : ""
                            badge: modelData.id === "render" && Config.errorCount + Config.warningCount > 0
                                   ? String(Config.errorCount > 0 ? Config.errorCount : Config.warningCount)
                                   : modelData.id === "queue" && Queue.count > 0 ? String(Queue.count) : ""
                            badgeSeverity: modelData.id === "render" ? (Config.errorCount > 0 ? "error" : "warning")
                                           : modelData.id === "queue" && Queue.errorItems > 0 ? "error" : ""
                            onClicked: Ui.goTo(modelData.id)
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                NavItem {
                    Layout.fillWidth: true
                    iconName: "sidebar"
                    label: qsTr("Згорнути панель")
                    collapsed: win.sidebarCollapsed
                    shortcut: "Ctrl+B"
                    onClicked: Config.set("ui_sidebar_collapsed", !win.sidebarCollapsed)
                }
            }
        }
        // ---- робочий простір ----
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: win.pageIndex(Ui.page)
            ProjectPage {}
            EditPage {}
            RenderPage {}
            AudioPage {}
            AiPage {}
            LibraryPage {}
            QueuePage {}
            SettingsPage {}
            LogPage {}
        }
    }

    // ---- перетягування файлів: демо, тека кадрів, файл мікрофона ----
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            for (const u of drop.urls) {
                const p = Shell.localPath(u)
                const lower = p.toLowerCase()
                if (lower.endsWith(".dem")) { Project.open(p); return }
                if (Shell.isDirectory(p)) { Jobs.encodeFrames(p); return }
                if ([".wav", ".mp3", ".ogg", ".flac", ".m4a", ".opus"].some(e => lower.endsWith(e))) {
                    Config.set("mic_file", p)
                    toast.show(qsTr("Файл мікрофона: %1").arg(p), "info")
                    return
                }
            }
            toast.show(qsTr("Перетягніть файл демо (.dem), папку з кадрами або аудіофайл мікрофона"), "warning")
        }
        Rectangle {
            anchors.fill: parent
            visible: parent.containsDrag
            color: Theme.tint(Theme.accent, 0.08)
            border.color: Theme.accent
            border.width: 2
            Label {
                anchors.centerIn: parent
                text: qsTr("Відпустіть, щоб відкрити")
                role: "title"
            }
        }
    }

    // ---- гарячі клавіші ----
    Shortcut { sequence: StandardKey.Open; onActivated: openDemoDialog.open() }
    Shortcut { sequence: "Ctrl+B"; onActivated: Config.set("ui_sidebar_collapsed", !win.sidebarCollapsed) }
    Shortcut { sequence: "Ctrl+,"; onActivated: Ui.goTo("settings") }
    Repeater {
        model: 9
        Item {
            required property int index
            Shortcut {
                sequence: "Ctrl+" + (index + 1)
                onActivated: if (index < win.visiblePages.length) Ui.goTo(win.visiblePages[index].id)
            }
        }
    }
    Shortcut { sequence: "I"; enabled: Project.loaded && !win.busy; onActivated: Project.markInAtPlayhead() }
    Shortcut { sequence: "O"; enabled: Project.loaded && !win.busy; onActivated: Project.markOutAtPlayhead() }
    Shortcut { sequence: "M"; enabled: Project.loaded; onActivated: Project.addMarkerAtPlayhead() }
    Shortcut { sequence: "Shift+I"; enabled: Project.loaded; onActivated: Project.playhead = Project.fragmentStart }
    Shortcut { sequence: "Shift+O"; enabled: Project.loaded; onActivated: Project.playhead = Project.fragmentEnd }
    Shortcut { sequence: "Home"; enabled: Project.loaded; onActivated: Project.playhead = 0 }
    Shortcut { sequence: "End"; enabled: Project.loaded; onActivated: Project.playhead = Project.duration }
    Shortcut { sequence: "Left"; enabled: Project.loaded; onActivated: Project.playhead = Project.playhead - 1 }
    Shortcut { sequence: "Right"; enabled: Project.loaded; onActivated: Project.playhead = Project.playhead + 1 }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: { Ui.developer = !Ui.developer; toast.show(Ui.developer ? qsTr("Режим розробника увімкнено") : qsTr("Режим розробника вимкнено"), "info") } }

    // ---- меню програми ----
    B.Menu {
        id: appMenu
        B.MenuItem { text: qsTr("Відкрити демо…") + "\tCtrl+O"; enabled: !win.busy; onTriggered: openDemoDialog.open() }
        B.MenuItem { text: qsTr("Закодувати готові кадри…"); enabled: !win.busy; onTriggered: framesDialog.open() }
        B.MenuItem { text: qsTr("Дописати урваний рендер…"); enabled: !win.busy && Object.keys(Jobs.resumeOffer).length > 0; onTriggered: resumeDialog.open() }
        B.MenuSeparator {}
        B.MenuItem { text: qsTr("Відкрити папку з відео"); enabled: String(Config.value("output_path") || "") !== ""; onTriggered: Shell.openPath(Shell.parentFolder(Config.value("output_path"))) }
        B.MenuItem { text: qsTr("Скинути налаштування рендеру"); enabled: !win.busy; onTriggered: resetDialog.open() }
        B.MenuSeparator {}
        B.MenuItem { text: qsTr("Перевірити систему ще раз"); onTriggered: Env.rescan() }
        B.MenuItem { text: qsTr("Відкрити журнал (gmdr_log.txt)"); onTriggered: Shell.openLogFile() }
        B.MenuItem { text: qsTr("Відкрити папку програми"); onTriggered: Shell.openAppFolder() }
        B.MenuSeparator {}
        B.MenuItem { text: qsTr("Перевірити оновлення"); enabled: !Jobs.checkingUpdates; onTriggered: Jobs.checkUpdates() }
        B.MenuItem { text: qsTr("Зібрати звіт про проблему…"); onTriggered: reportDialog.open() }
        B.MenuItem { text: qsTr("Про програму"); onTriggered: aboutDialog.open() }
        B.MenuSeparator {}
        B.MenuItem { text: qsTr("Вихід"); onTriggered: win.close() }
    }

    // ---- діалоги файлів ----
    FileDialog {
        id: openDemoDialog
        title: qsTr("Відкрити демо Garry's Mod")
        nameFilters: [qsTr("Демо GMod (*.dem)"), qsTr("Усі файли (*)")]
        currentFolder: Project.demoPath !== "" ? Shell.folderUrl(Project.demoPath) : ""
        onAccepted: Project.open(Shell.localPath(selectedFile))
    }
    FolderDialog {
        id: framesDialog
        title: qsTr("Папка з кадрами startmovie (TGA/JPG) і WAV")
        onAccepted: Jobs.encodeFrames(Shell.localPath(selectedFolder))
    }
    FileDialog {
        id: reportDialog
        title: qsTr("Зберегти звіт про проблему")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ZIP-архів (*.zip)")]
        currentFile: "file:///" + Jobs.defaultReportName()
        onAccepted: {
            const err = Jobs.makeReport(Shell.localPath(selectedFile))
            if (err !== "") messageDialog.show(qsTr("Не вдалося створити звіт"), err, "error", "")
        }
    }

    // ---- результати завдань ----
    Connections {
        target: Jobs
        function onFinished() {
            const r = Jobs.lastResult
            if (r.show) resultDialog.showResult(r)
        }
        function onConfirmOverwrite(path) { overwriteDialog.path = path; overwriteDialog.open() }
        function onMessage(title, text) { messageDialog.show(title, text, "info", "") }
        function onUpdateResult(title, text, url) { messageDialog.show(title, text, url !== "" ? "sparkle" : "info", url) }
        function onAfterDoneChanged() { if (Jobs.powerCountdown > 0 && !powerDialog.visible) powerDialog.open() }
    }
    Connections {
        target: Project
        function onOpenFailed(path, error) { messageDialog.show(qsTr("Не вдалося відкрити демо"), error, "error", "") }
    }

    ResultDialog { id: resultDialog; onStartRender: win.startRender() }

    Dialog {
        id: messageDialog
        property string body: ""
        property string url: ""
        function show(t, text, icon, link) {
            title = t
            body = text
            iconName = icon === "error" ? "error" : icon === "sparkle" ? "sparkle" : "info"
            iconColor = icon === "error" ? Theme.error : Theme.accentText
            url = link
            open()
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s4
            B.ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(bodyText.implicitHeight, Theme.px(360))
                Label {
                    id: bodyText
                    width: messageDialog.availableWidth
                    text: messageDialog.body
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.s2
                Btn { visible: messageDialog.url !== ""; kind: "primary"; text: qsTr("Відкрити"); onClicked: { Qt.openUrlExternally(messageDialog.url); messageDialog.close() } }
                Btn { text: qsTr("Закрити"); onClicked: messageDialog.close() }
            }
        }
    }

    Dialog {
        id: overwriteDialog
        property string path: ""
        title: qsTr("Файл уже існує")
        iconName: "warning"
        iconColor: Theme.warning
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s4
            Label {
                Layout.fillWidth: true
                text: qsTr("Перезаписати його новим рендером?") + "\n" + overwriteDialog.path
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideNone
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.s2
                Btn { kind: "danger"; text: qsTr("Перезаписати"); onClicked: { overwriteDialog.close(); Jobs.startRender(true) } }
                Btn { text: qsTr("Скасувати"); onClicked: overwriteDialog.close() }
            }
        }
    }

    Dialog {
        id: resetDialog
        title: qsTr("Скинути налаштування рендеру?")
        iconName: "refresh"
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s4
            Label {
                Layout.fillWidth: true
                text: qsTr("Відео, звук і гра повернуться до типових. Демо, файл результату, папки гри, ключі сервісів і вигляд програми лишаться.")
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.s2
                Btn { kind: "danger"; text: qsTr("Скинути"); onClicked: { Config.resetToDefaults(); resetDialog.close() } }
                Btn { text: qsTr("Скасувати"); onClicked: resetDialog.close() }
            }
        }
    }

    // Рендер, урваний збоєм програми чи ПК
    Dialog {
        id: resumeDialog
        readonly property var r: Jobs.resumeOffer
        title: qsTr("Знайдено урваний рендер")
        iconName: "refresh"
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s3
            Label {
                Layout.fillWidth: true
                text: String(resumeDialog.r.output || "")
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideNone
                font.weight: Font.Medium
            }
            Label {
                Layout.fillWidth: true
                role: "secondary"
                text: qsTr("Записано ≈ %1 з %2 (%3 %). Програма може дописати решту з того самого місця.")
                      .arg(Project.formatTime(resumeDialog.r.doneSeconds || 0)).arg(Project.formatTime(resumeDialog.r.totalSeconds || 0))
                      .arg(Math.round((resumeDialog.r.fraction || 0) * 100))
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Bar { Layout.fillWidth: true; value: resumeDialog.r.fraction || 0 }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.s2
                Btn { kind: "primary"; text: qsTr("Дописати"); enabled: !win.busy; onClicked: { resumeDialog.close(); Jobs.resume() } }
                Btn { kind: "danger"; text: qsTr("Забути"); onClicked: { Jobs.forgetResume(); resumeDialog.close() } }
                Btn { text: qsTr("Пізніше"); onClicked: resumeDialog.close() }
            }
        }
    }

    // Після рендеру — вимкнути ПК / сон: хвилина, щоб скасувати
    Dialog {
        id: powerDialog
        title: Jobs.afterDone === "shutdown" ? qsTr("Вимкнення ПК") : qsTr("Сон")
        iconName: "clock"
        iconColor: Theme.warning
        closePolicy: B.Popup.NoAutoClose
        onClosed: if (Jobs.powerCountdown > 0) Jobs.cancelPowerAction()
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s4
            Label {
                text: qsTr("Рендер завершено. %1 через %2 с.").arg(Jobs.afterDone === "shutdown" ? qsTr("ПК вимкнеться") : qsTr("ПК засне")).arg(Jobs.powerCountdown)
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.s2
                Btn { kind: "primary"; text: qsTr("Скасувати"); onClicked: { Jobs.cancelPowerAction(); powerDialog.close() } }
                Btn { kind: "danger"; text: qsTr("Зараз"); onClicked: { powerDialog.close(); Jobs.powerActionNow() } }
            }
        }
    }

    // Вікно не запустилось з вибраним графічним API — повернуто «Автоматично»
    Dialog {
        id: graphicsRecovered
        title: qsTr("Графічний API повернуто на «Автоматично»")
        iconName: "warning"
        iconColor: Theme.warning
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s4
            Label {
                Layout.fillWidth: true
                text: qsTr("Минулого разу вікно програми не запустилось з «%1» (збій або зависання відеодрайвера). Тепер вікно малює автоматично вибраний API. Інший можна вибрати в «Налаштування → Графіка».").arg(Env.graphicsApiLabel(Env.recoveredGraphicsApi))
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Btn { text: qsTr("Зрозуміло"); onClicked: graphicsRecovered.close() }
            }
        }
    }

    Dialog {
        id: quitDialog
        title: qsTr("Рендер ще йде")
        iconName: "warning"
        iconColor: Theme.warning
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s4
            Label {
                Layout.fillWidth: true
                text: qsTr("Закрити програму і перервати рендер? Уже записане збережеться, і рендер можна буде дописати.")
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.s2
                Btn { kind: "danger"; text: qsTr("Перервати й закрити"); onClicked: { win.forceQuit = true; Jobs.kill(); win.close() } }
                Btn { text: qsTr("Продовжити рендер"); onClicked: quitDialog.close() }
            }
        }
    }
    property bool forceQuit: false
    onClosing: (close) => {
        if (Jobs.busy && !forceQuit) {
            close.accepted = false
            quitDialog.open()
        }
    }

    ConsentDialog { id: consentDialog }
    ModelDialog { id: modelDialog }
    EngineDialog { id: engineDialog }
    AboutDialog { id: aboutDialog }

    Toast { id: toast }
    Connections {
        target: Config
        function onFixApplied(label) { toast.show(qsTr("Виправлено: %1").arg(label), "success") }
    }
}
