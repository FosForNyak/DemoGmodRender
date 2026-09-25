// «Налаштування»: вигляд, мова, графіка вікна (API, відеокарти, GPU-кодеки), копії гри,
// рендер за замовчуванням, файли програми, сповіщення, система й залежності, для розробника.
// Категорії ліворуч, вміст — праворуч. Налаштування рендеру конкретного відео — у «Рендері».
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Item {
    id: sp
    readonly property string category: Ui.settingsCategory
    readonly property int rev: Config.revision
    readonly property var categories: [
        { id: "appearance", label: qsTr("Вигляд"), icon: "sparkle" },
        { id: "language", label: qsTr("Мова"), icon: "globe" },
        { id: "graphics", label: qsTr("Графіка"), icon: "cpu" },
        { id: "game", label: "Garry's Mod", icon: "gamepad" },
        { id: "rendering", label: qsTr("Рендер"), icon: "render" },
        { id: "storage", label: qsTr("Файли програми"), icon: "folder" },
        { id: "notifications", label: qsTr("Поведінка"), icon: "bell" },
        { id: "system", label: qsTr("Система"), icon: "info" },
        { id: "developer", label: qsTr("Для розробника"), icon: "code", advanced: true }
    ]
    readonly property bool apiRestartNeeded: (rev, String(Config.value("ui_graphics_api") || "auto")) !== (Env.startupGraphicsApi || "auto")
    readonly property bool languageRestartNeeded: (rev, String(Config.value("ui_language") || "")) !== Shell.startupLanguage
    property bool restartDismissed: false

    function gib(bytes) { return bytes > 0 ? Shell.formatBytes(bytes) : "—" }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        // ---- категорії ----
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.px(210)
            color: Theme.surface
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.s3
                anchors.rightMargin: Theme.s3 + 1
                spacing: 2
                Label { text: qsTr("Налаштування"); role: "section"; Layout.bottomMargin: Theme.s2; Layout.leftMargin: Theme.s2 }
                Repeater {
                    model: sp.categories.filter(c => !c.advanced || Config.advanced)
                    NavItem {
                        required property var modelData
                        Layout.fillWidth: true
                        label: modelData.label
                        iconName: modelData.icon
                        current: sp.category === modelData.id
                        onClicked: Ui.settingsCategory = modelData.id
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
        // ---- вміст ----
        Page {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            maxContentWidth: Theme.px(860)
            title: (sp.categories.find(c => c.id === sp.category) || { label: "" }).label
            subtitle: sp.category === "appearance" ? qsTr("Тема, колір акценту, масштаб і щільність")
                      : sp.category === "language" ? qsTr("Мова змінюється після перезапуску програми. Консольна версія: --lang або змінна GMDR_LANG.")
                      : sp.category === "graphics" ? qsTr("Чим малюється вікно програми, відеокарти і кодеки на них")
                      : sp.category === "game" ? qsTr("Копії гри для кожного рендерера, драйвер у меню гри")
                      : sp.category === "rendering" ? qsTr("Швидкодія рендеру і тимчасові файли")
                      : sp.category === "storage" ? qsTr("Де програма тримає налаштування, журнал, моделі й голоси")
                      : sp.category === "notifications" ? qsTr("Сповіщення, трей, файли .dem, оновлення")
                      : sp.category === "system" ? qsTr("Комп'ютер і складники, від яких залежать функції програми")
                      : qsTr("Правила перевірки налаштувань і похідні значення")

            // Перезапуск: API вікна чи мову не можна змінити на льоту
            Banner {
                visible: (sp.apiRestartNeeded || sp.languageRestartNeeded) && !sp.restartDismissed
                tone: "info"
                text: sp.apiRestartNeeded ? qsTr("Зміна графічного API вікна потребує перезапуску програми.")
                                          : qsTr("Мова зміниться після перезапуску програми.")
                actionText: qsTr("Перезапустити зараз")
                actionIcon: "refresh"
                secondaryText: qsTr("Пізніше")
                onAction: Jobs.busy ? Ui.toast(qsTr("Зачекайте завершення поточного завдання"), "warning") : Shell.restartApp([])
                onSecondaryAction: sp.restartDismissed = true
            }

            // ================= Вигляд =================
            ColumnLayout {
                visible: sp.category === "appearance"
                Layout.fillWidth: true
                spacing: Theme.s3
                SettingRow {
                    label: qsTr("Режим")
                    hint: qsTr("Стандартний режим — лише головні налаштування.\nРозширений — усі параметри кодеків, гри й звуку, а також «Журнал».")
                    Item {
                        implicitHeight: Theme.controlHeight
                        Segmented {
                            options: [{ value: false, label: qsTr("Стандартний") }, { value: true, label: qsTr("Розширений") }]
                            currentValue: Config.advanced
                            onChosen: (v) => Config.advanced = v
                        }
                    }
                }
                SettingRow {
                    label: qsTr("Тема")
                    Item {
                        implicitHeight: Theme.controlHeight
                        Segmented {
                            options: [{ value: "dark", label: qsTr("Темна") }, { value: "light", label: qsTr("Світла") }, { value: "system", label: qsTr("Як у Windows") }]
                            currentValue: Theme.themeId
                            onChosen: (v) => Config.set("ui_theme", v)
                        }
                    }
                }
                SettingRow {
                    label: qsTr("Колір акценту")
                    Row {
                        spacing: Theme.s2
                        Repeater {
                            model: Theme.accents
                            B.AbstractButton {
                                id: sw
                                required property var modelData
                                readonly property bool on: Theme.accentId === modelData.id
                                width: Theme.px(26)
                                height: Theme.px(26)
                                padding: Theme.px(6)
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                onClicked: Config.set("ui_accent", modelData.id)
                                Accessible.name: modelData.label
                                Accessible.role: Accessible.RadioButton
                                Accessible.checked: on
                                B.ToolTip.visible: hovered
                                B.ToolTip.text: modelData.label
                                background: Rectangle {
                                    radius: width / 2
                                    color: sw.modelData.color
                                    border.width: sw.on || sw.visualFocus ? 2 : 0
                                    border.color: Theme.text
                                }
                                contentItem: Icon {
                                    visible: sw.on
                                    name: "check"
                                    color: Theme.onAccent
                                }
                            }
                        }
                    }
                }
                SettingRow {
                    label: qsTr("Масштаб")
                    hint: qsTr("Розмір тексту й елементів поверх масштабу Windows. Корисно на великих моніторах чи, навпаки, на ноутбуці.")
                    RowLayout {
                        Combo {
                            Layout.preferredWidth: Theme.px(120)
                            options: [80, 90, 100, 110, 125, 150, 175, 200].map(v => ({ value: v, label: v + "%" }))
                            selected: Math.round(Theme.scale * 100)
                            onChosen: (v) => Config.set("ui_scale", v / 100)
                            Accessible.name: qsTr("Масштаб")
                        }
                        Label { role: "meta"; text: "Ctrl+=  /  Ctrl+−  /  Ctrl+0" }
                        Item { Layout.fillWidth: true }
                    }
                }
                SettingRow {
                    label: qsTr("Щільність")
                    Item {
                        implicitHeight: Theme.controlHeight
                        Segmented {
                            options: [{ value: false, label: qsTr("Звичайна") }, { value: true, label: qsTr("Компактна") }]
                            currentValue: Theme.compact
                            onChosen: (v) => Config.set("ui_compact", v)
                        }
                    }
                }
                SettingRow {
                    label: ""
                    Toggle {
                        text: qsTr("Підписи в бічній панелі")
                        checked: (sp.rev, Config.value("ui_sidebar_collapsed") !== true)
                        onToggled: Config.set("ui_sidebar_collapsed", !checked)
                    }
                }
            }

            // ================= Мова =================
            ColumnLayout {
                visible: sp.category === "language"
                Layout.fillWidth: true
                spacing: Theme.s3
                SettingRow {
                    label: qsTr("Мова інтерфейсу")
                    Combo {
                        options: [{ value: "", label: qsTr("Як у Windows") }].concat(Shell.uiLanguages.map(l => ({ value: l.code, label: l.native === l.english ? l.native : l.native + " — " + l.english })))
                        selected: (sp.rev, String(Config.value("ui_language") || ""))
                        onChosen: (v) => { Config.set("ui_language", v); sp.restartDismissed = false }
                        Accessible.name: qsTr("Мова інтерфейсу")
                    }
                }
            }

            // ================= Графіка =================
            ColumnLayout {
                visible: sp.category === "graphics"
                Layout.fillWidth: true
                spacing: Theme.s3
                Label { text: qsTr("Графічний API вікна"); role: "section" }
                SettingRow {
                    id: apiRow
                    key: "ui_graphics_api"
                    label: qsTr("Малювати вікно через")
                    hint: qsTr("Як саме програма малює своє вікно. «Автоматично» підходить майже завжди; інший API — якщо вікно блимає, гальмує чи не з'являється. На рендер відео не впливає.")
                    RowLayout {
                        Combo {
                            Layout.fillWidth: true
                            options: apiRow.st.options || []
                            selected: (sp.rev, String(Config.value("ui_graphics_api") || "auto"))
                            severity: apiRow.st.severity || ""
                            onChosen: (v) => { Config.set("ui_graphics_api", v); sp.restartDismissed = false }
                            Accessible.name: apiRow.label
                        }
                        Btn {
                            iconName: "refresh"
                            text: Env.probingGraphics ? qsTr("перевіряється…") : qsTr("Перевірити")
                            enabled: !Env.probingGraphics
                            tip: qsTr("Перевірити, які API справді запускаються на цьому комп'ютері")
                            onClicked: Env.probeGraphicsApis()
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: apiCol.implicitHeight + Theme.s3 * 2
                    radius: Theme.r2
                    color: Theme.surface
                    border.color: Theme.border
                    ColumnLayout {
                        id: apiCol
                        x: Theme.s3
                        y: Theme.s3
                        width: parent.width - Theme.s3 * 2
                        spacing: Theme.s2
                        Each {
                            items: Env.graphicsApis.filter(a => a.id !== "auto")
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.s3
                                StatusDot {
                                    Layout.fillWidth: true
                                    state: modelData.state === "available" ? "ready" : modelData.state === "unknown" ? "unknown" : modelData.state === "checking" ? "checking" : "failed"
                                    text: modelData.label + (modelData.state === "unknown" ? " — " + qsTr("не перевірено")
                                                             : modelData.state === "checking" ? " — " + qsTr("перевіряється…")
                                                             : (modelData.detail || "") !== "" && modelData.state !== "available" ? " — " + modelData.detail : "")
                                }
                                Label { visible: modelData.active; text: qsTr("зараз працює"); role: "meta"; color: Theme.accentText }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                        KeyValue { name: qsTr("Зараз"); value: Env.activeGraphicsApi !== "" ? Env.graphicsApiLabel(Env.activeGraphicsApi) : "—" }
                        KeyValue { visible: Env.recoveredGraphicsApi !== ""; name: qsTr("Повернуто після збою"); value: Env.graphicsApiLabel(Env.recoveredGraphicsApi); valueColor: Theme.warning }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    role: "meta"
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    text: qsTr("Якщо з вибраним API вікно не запуститься, наступного разу програма сама повернеться на «Автоматично». Також можна запустити її з параметром --graphics-api auto.")
                }

                Label { text: qsTr("Відеокарти"); role: "section"; Layout.topMargin: Theme.s3 }
                Label {
                    visible: Env.gpus.length === 0
                    Layout.fillWidth: true
                    role: "secondary"
                    wrapMode: Text.WordWrap
                    text: Env.scanning ? qsTr("пошук…") : (Env.system.gpuListDetail || qsTr("Відеокарт не знайдено"))
                }
                Each {
                    items: Env.gpus
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: gpuCol.implicitHeight + Theme.s3 * 2
                        radius: Theme.r2
                        color: Theme.surface
                        border.color: Theme.border
                        ColumnLayout {
                            id: gpuCol
                            x: Theme.s3
                            y: Theme.s3
                            width: parent.width - Theme.s3 * 2
                            spacing: Theme.s1
                            Label { text: modelData.name; font.weight: Font.DemiBold }
                            KeyValue { name: qsTr("Виробник"); value: modelData.vendor === "nvidia" ? "NVIDIA" : modelData.vendor === "amd" ? "AMD" : modelData.vendor === "intel" ? "Intel" : (modelData.vendor || "—") }
                            KeyValue { name: qsTr("Відеопам'ять"); value: sp.gib(modelData.vram) }
                            KeyValue { name: qsTr("Драйвер"); value: modelData.driver || "—" }
                            Label { visible: modelData.software; role: "meta"; color: Theme.warning; text: qsTr("програмний адаптер (без апаратного прискорення)") }
                        }
                    }
                }

                Label { text: qsTr("Кодеки на відеокарті"); role: "section"; Layout.topMargin: Theme.s3 }
                Label {
                    Layout.fillWidth: true
                    role: "meta"
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    text: qsTr("Апаратне кодування відео (NVENC, AMF, Quick Sync): перевіряється справжнім кодуванням кількох кадрів, а не лише наявністю драйвера.")
                }
                Each {
                    items: Env.gpuEncoders
                    StatusDot {
                        required property var modelData
                        Layout.fillWidth: true
                        state: modelData.state === "available" ? "ready" : modelData.state === "failed" ? "failed" : modelData.state
                        text: modelData.label + (modelData.state === "failed" || modelData.state === "unavailable" ? " — " + modelData.detail
                                                 : modelData.state === "checking" ? " — " + qsTr("перевіряється…")
                                                 : modelData.state === "unknown" ? " — " + qsTr("не перевірено") : "")
                    }
                }
                Label { visible: Env.gpuEncoders.length === 0; role: "secondary"; text: qsTr("У цій збірці FFmpeg немає кодеків для відеокарти.") }
                RowLayout {
                    Btn {
                        iconName: "refresh"
                        text: qsTr("Перевірити GPU-кодеки ще раз")
                        enabled: !Env.probingGpu && !Jobs.busy
                        onClicked: Env.probeGpuEncoders()
                    }
                }
            }

            // ================= Garry's Mod =================
            ColumnLayout {
                visible: sp.category === "game"
                Layout.fillWidth: true
                spacing: Theme.s4
                Repeater {
                    model: Env.games
                    Rectangle {
                        id: gcard
                        required property var modelData
                        readonly property bool found: modelData.state === "available"
                        readonly property bool current: (sp.rev, Config.value("game_renderer")) === modelData.renderer
                        Layout.fillWidth: true
                        implicitHeight: gcol.implicitHeight + Theme.s4 * 2
                        radius: Theme.r2
                        color: Theme.surface
                        border.color: current ? Theme.tint(Theme.accent, 0.6) : Theme.border
                        ColumnLayout {
                            id: gcol
                            x: Theme.s4
                            y: Theme.s4
                            width: parent.width - Theme.s4 * 2
                            spacing: Theme.s2
                            RowLayout {
                                spacing: Theme.s2
                                Label { text: gcard.modelData.label; role: "section" }
                                Label { visible: gcard.current; text: qsTr("вибрано для рендеру"); role: "meta"; color: Theme.accentText }
                            }
                            Label {
                                Layout.fillWidth: true
                                role: "secondary"
                                wrapMode: Text.WordWrap
                                elide: Text.ElideNone
                                text: gcard.modelData.description
                            }
                            StatusDot {
                                Layout.fillWidth: true
                                state: gcard.found && !gcard.modelData.accepted ? "failed" : gcard.modelData.state === "available" ? "ready" : gcard.modelData.state
                                text: gcard.found ? String(gcard.modelData.detail) + (gcard.modelData.accepted ? "" : " — " + qsTr("це не копія для цього рендерера"))
                                      : Env.scanning ? qsTr("пошук…") : String(gcard.modelData.detail || qsTr("не перевірено"))
                            }
                            SettingPath {
                                key: gcard.modelData.dirSetting
                                forceVisible: true
                                showMessages: false   // стан копії — рядком вище
                                mode: "folder"
                                placeholder: gcard.modelData.renderer === "rtx" ? qsTr("з налаштувань RTXLauncher") : qsTr("знайти автоматично (Steam)")
                            }
                            KeyValue {
                                visible: gcard.found
                                name: qsTr("64-біт")
                                value: gcard.modelData.has64bit ? qsTr("так") : qsTr("ні (лише 32-біт)")
                            }
                            KeyValue {
                                visible: gcard.found
                                name: qsTr("Драйвер у меню гри")
                                value: gcard.modelData.driver === "installed" ? qsTr("встановлено") : gcard.modelData.driver === "outdated" ? qsTr("застарів — встановіть ще раз") : qsTr("не встановлено")
                                valueColor: gcard.modelData.driver === "installed" ? Theme.success : gcard.modelData.driver === "outdated" ? Theme.warning : Theme.textSecondary
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.s2
                                Btn {
                                    iconName: "search"
                                    text: gcard.modelData.renderer === "standard" ? qsTr("Знайти Garry's Mod автоматично") : qsTr("Знайти автоматично")
                                    enabled: !Jobs.busy
                                    onClicked: Env.findGame(gcard.modelData.renderer)
                                }
                                Btn {
                                    visible: gcard.found
                                    text: gcard.modelData.driver === "installed" ? qsTr("Видалити драйвер з GMod") : qsTr("Встановити драйвер у GMod")
                                    enabled: !Jobs.busy
                                    tip: qsTr("Пункт GMod Demo Render у меню гри: рендер і перегляд демо прямо з гри.")
                                    onClicked: {
                                        const err = Env.setDriverInstalled(gcard.modelData.renderer, gcard.modelData.driver !== "installed")
                                        if (err !== "") Ui.toast(err, "error")
                                    }
                                }
                                Btn {
                                    visible: !gcard.found && (gcard.modelData.url || "") !== ""
                                    kind: "ghost"
                                    iconName: "external"
                                    text: qsTr("Як встановити")
                                    onClicked: Qt.openUrlExternally(gcard.modelData.url)
                                }
                            }
                        }
                    }
                }
                Connections {
                    target: Env
                    function onGameSearchFinished(renderer, found, message) { Ui.toast(message, found ? "success" : "warning") }
                }
            }

            // ================= Рендер =================
            ColumnLayout {
                visible: sp.category === "rendering"
                Layout.fillWidth: true
                spacing: Theme.s3
                Label {
                    Layout.fillWidth: true
                    role: "secondary"
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    text: qsTr("Формат, якість і звук конкретного відео — у робочому просторі «Рендер». Тут — те, що стосується швидкодії і будь-якого рендеру.")
                }
                SettingNumber { key: "parallel_games"; showAlways: true; decimals: 0 }
                SettingNumber { key: "threads"; decimals: 0; showAlways: true; hint: qsTr("0 — стільки, скільки ядер у процесора.") }
                SettingNumber { key: "max_pending_frames"; decimals: 0; showAlways: true }
                SettingCombo { key: "frame_transport"; showAlways: true }
                SettingToggle { key: "keep_temp_files"; showAlways: true }
                RowLayout {
                    Layout.topMargin: Theme.s3
                    Btn {
                        kind: "danger"
                        text: qsTr("Скинути налаштування рендеру")
                        enabled: !Jobs.busy
                        tip: qsTr("Повертає типові значення відео, звуку, гри й фрагмента. Вигляд, мова, папки гри і бібліотеки лишаються.")
                        onClicked: Ui.resetRequested()
                    }
                }
            }

            // ================= Файли програми =================
            ColumnLayout {
                visible: sp.category === "storage"
                Layout.fillWidth: true
                spacing: Theme.s2
                PathRow { name: qsTr("Налаштування"); path: Shell.settingsPath }
                PathRow { name: qsTr("Журнал"); path: Shell.logPath }
                PathRow { name: qsTr("Моделі розпізнавання"); path: Jobs.whisperModelsFolder() }
                PathRow { name: qsTr("Бібліотека голосів"); path: Voices.voicesDir }
                PathRow { name: qsTr("Рушій озвучення"); path: Voices.engineDir }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.topMargin: Theme.s2 }
                KeyValue { name: qsTr("Вільно на диску з відео"); value: sp.gib(Env.system.outputFree || 0) }
                KeyValue { name: qsTr("Вільно на диску з грою"); value: sp.gib(Env.system.gameFree || 0) }
                RowLayout {
                    Layout.topMargin: Theme.s3
                    spacing: Theme.s2
                    Btn { iconName: "folder"; text: qsTr("Відкрити папку програми"); onClicked: Shell.openAppFolder() }
                    Btn { iconName: "external"; text: qsTr("Відкрити журнал"); onClicked: Shell.openLogFile() }
                }
            }

            // ================= Поведінка =================
            ColumnLayout {
                visible: sp.category === "notifications"
                Layout.fillWidth: true
                spacing: Theme.s3
                SettingRow {
                    label: ""
                    hint: qsTr("Сповіщення Windows, коли рендер чи черга закінчились, а вікно згорнуте.")
                    Toggle {
                        text: qsTr("Сповіщати, коли рендер готовий")
                        checked: (sp.rev, Config.value("notify_when_done") === true)
                        onToggled: Config.set("notify_when_done", checked)
                    }
                }
                SettingRow {
                    label: ""
                    hint: qsTr("Згорнуте вікно зникає з панелі задач, лишається значок біля годинника\n(з прогресом рендеру в підказці). Клік по значку повертає вікно.")
                    Toggle {
                        text: qsTr("Згортати в трей")
                        checked: (sp.rev, Config.value("minimize_to_tray") === true)
                        onToggled: Config.set("minimize_to_tray", checked)
                    }
                }
                SettingRow {
                    visible: Shell.isWindows
                    label: ""
                    hint: qsTr("Лише для вашого облікового запису (HKCU), без прав адміністратора.\nЯкщо програма вже відкрита, демо відкриється в ній.")
                    Toggle {
                        text: qsTr("Відкривати .dem подвійним кліком")
                        checked: Shell.demAssociated
                        onToggled: {
                            if (Shell.setDemAssociation(checked))
                                Ui.toast(checked ? qsTr("Файли .dem тепер відкриваються в GMod Demo Render (якщо Windows спитає, чим відкривати, — виберіть її). Вимкнути — тут само.")
                                                 : qsTr("Файли .dem більше не відкриваються цією програмою"), "success")
                            checked = Qt.binding(() => Shell.demAssociated)
                        }
                    }
                }
                RowLayout {
                    Layout.topMargin: Theme.s2
                    spacing: Theme.s3
                    Btn { iconName: "refresh"; text: qsTr("Перевірити оновлення"); enabled: !Jobs.checkingUpdates; onClicked: Jobs.checkUpdates() }
                    Label { role: "secondary"; text: Ui.fmt(qsTr("Версія {}"), Env.system.app || "") }
                }
            }

            // ================= Система =================
            ColumnLayout {
                visible: sp.category === "system"
                Layout.fillWidth: true
                spacing: Theme.s2
                KeyValue { name: qsTr("Програма"); value: "GMod Demo Render " + (Env.system.app || "") }
                KeyValue { name: qsTr("Система"); value: Env.system.os || "—" }
                KeyValue { name: qsTr("Процесор"); value: (Env.system.cpu || "—") + (Env.system.cpuThreads ? " · " + Ui.fmt(qsTr("потоків: {}"), Env.system.cpuThreads) : "") }
                KeyValue { name: qsTr("Пам'ять"); value: sp.gib(Env.system.ram || 0) + (Env.system.ramFree ? " · " + Ui.fmt(qsTr("вільно {}"), sp.gib(Env.system.ramFree)) : "") }
                KeyValue { name: qsTr("Відеокарта"); value: Env.gpus.length > 0 ? Env.gpus.map(g => g.name).join(", ") : "—" }
                KeyValue { name: "FFmpeg"; value: Env.system.ffmpeg || "—" }
                KeyValue { name: "Qt"; value: (Env.system.qt || "") + " · " + (Env.activeGraphicsApi !== "" ? Env.graphicsApiLabel(Env.activeGraphicsApi) : "") }

                RowLayout {
                    Layout.topMargin: Theme.s4
                    spacing: Theme.s2
                    Label { Layout.fillWidth: true; text: qsTr("Складники"); role: "section" }
                    Btn { kind: "ghost"; iconName: "refresh"; text: Env.scanning ? qsTr("пошук…") : qsTr("Перевірити ще раз"); enabled: !Env.scanning; onClicked: Env.rescan() }
                }
                Each {
                    items: Env.dependencies
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.s3
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            StatusDot {
                                Layout.fillWidth: true
                                state: modelData.state
                                text: modelData.label
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.px(14) + Theme.s2
                                role: "meta"
                                wrapMode: Text.WordWrap
                                text: modelData.purpose + ((modelData.detail || "") !== "" ? " · " + modelData.detail : "")
                            }
                        }
                        Btn {
                            visible: modelData.state !== "ready" && (modelData.action || "") !== "" && Ui.actionLabel(modelData.action) !== ""
                            text: Ui.actionLabel(modelData.action || "")
                            onClicked: Ui.runAction(modelData.action)
                        }
                        Btn {
                            visible: modelData.state !== "ready" && (modelData.url || "") !== ""
                            kind: "ghost"
                            iconName: "external"
                            tip: modelData.url
                            onClicked: Qt.openUrlExternally(modelData.url)
                        }
                    }
                }

                Label { text: qsTr("Звіт про проблему"); role: "section"; Layout.topMargin: Theme.s4 }
                Label {
                    Layout.fillWidth: true
                    role: "secondary"
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    text: qsTr("Архів із журналом, налаштуваннями (без ключів API) і відомостями про систему. Шлях до вашого профілю в ньому замінено, нічого нікуди не надсилається — архів ви передаєте самі.")
                }
                RowLayout {
                    spacing: Theme.s2
                    Btn { iconName: "download"; text: qsTr("Зібрати звіт про проблему..."); onClicked: Ui.reportRequested() }
                    Btn { kind: "ghost"; text: qsTr("Про програму"); onClicked: Ui.aboutRequested() }
                }
            }

            // ================= Для розробника =================
            ColumnLayout {
                visible: sp.category === "developer"
                Layout.fillWidth: true
                spacing: Theme.s3
                SettingRow {
                    label: ""
                    hint: qsTr("Біля зауважень видно ідентифікатор правила і пов'язані налаштування.")
                    Toggle {
                        text: qsTr("Режим розробника")
                        checked: Ui.developer
                        onToggled: { Ui.developer = checked; Config.developer = checked }
                    }
                }
                Label { text: qsTr("Похідні значення"); role: "section"; Layout.topMargin: Theme.s2 }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: derivedText.implicitHeight + Theme.s3 * 2
                    radius: Theme.r2
                    color: Theme.surface
                    border.color: Theme.border
                    Label {
                        id: derivedText
                        x: Theme.s3
                        y: Theme.s3
                        width: parent.width - Theme.s3 * 2
                        role: "mono"
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WrapAnywhere
                        elide: Text.ElideNone
                        text: JSON.stringify(Config.derived, null, 2)
                    }
                }
                Label { text: Ui.fmt(qsTr("Правила перевірки ({})"), sp.rules.length); role: "section"; Layout.topMargin: Theme.s2 }
                Repeater {
                    model: sp.rules
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.s3
                        Label { Layout.preferredWidth: Theme.px(200); text: modelData.id; role: "mono"; font.pixelSize: Theme.fontMeta; color: Theme.textSecondary }
                        Label { Layout.fillWidth: true; text: modelData.description; role: "secondary"; wrapMode: Text.WordWrap }
                    }
                }
            }
            Item { Layout.preferredHeight: Theme.s6 }
        }
    }
    readonly property var rules: Config.rules()

    component PathRow: RowLayout {
        property string name: ""
        property string path: ""
        Layout.fillWidth: true
        spacing: Theme.s3
        Label { Layout.preferredWidth: Theme.labelWidth; text: parent.name; role: "secondary" }
        Label { Layout.fillWidth: true; text: parent.path; elide: Text.ElideMiddle; role: "mono"; font.pixelSize: Theme.fontSmall }
        IconBtn { iconName: "copy"; tip: qsTr("Копіювати шлях"); onClicked: Shell.copyText(parent.path) }
        IconBtn { iconName: "folder"; tip: qsTr("Показати в папці"); onClicked: Shell.showInFolder(parent.path) }
    }
}
