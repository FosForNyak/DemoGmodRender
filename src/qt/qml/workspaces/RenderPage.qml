// «Рендер»: усі налаштування рендеру в одному місці — джерело і рендерер гри, файл,
// відео, продуктивність, додаткові версії; праворуч — підсумок, зауваження перевірки з
// виправленнями і дії. Що доступно і чому ні — лише з перевірки налаштувань (Config.state).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Item {
    id: page
    readonly property var d: (Config.revision, Config.derived)
    readonly property bool wide: width > Theme.px(1180)

    RowLayout {
        anchors.fill: parent
        spacing: 0
        // ---- налаштування ----
        Page {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Рендер")
            subtitle: qsTr("Як гра рендерить демо і що буде у файлі")
            maxContentWidth: Theme.px(860)

            // ---- ДЖЕРЕЛО ----
            Section {
                title: qsTr("Джерело")
                iconName: "file"
                SettingRow {
                    label: qsTr("Демо")
                    RowLayout {
                        spacing: Theme.s2
                        Label { Layout.fillWidth: true; text: Project.demoPath !== "" ? Project.demoPath : qsTr("не відкрито"); elide: Text.ElideMiddle; role: Project.demoPath !== "" ? "body" : "secondary" }
                        Btn { text: qsTr("Відкрити…"); enabled: !Jobs.busy; onClicked: Ui.openDemoRequested() }
                    }
                }
                SettingRow {
                    label: qsTr("Фрагмент")
                    visible: Project.loaded
                    RowLayout {
                        spacing: Theme.s2
                        Toggle {
                            text: qsTr("Увесь запис")
                            checked: Project.wholeDemo
                            onToggled: { if (checked) Project.wholeDemo = true; else { Project.setFragment(0, Project.duration); Ui.goTo("edit") } }
                        }
                        Label {
                            visible: !Project.wholeDemo
                            text: Project.formatTime(Project.fragmentStart) + " – " + Project.formatTime(Project.fragmentEnd)
                        }
                        Item { Layout.fillWidth: true }
                        Btn { kind: "ghost"; text: qsTr("На шкалі…"); onClicked: Ui.goTo("edit") }
                    }
                }
                SettingRow {
                    key: "game_renderer"
                    hint: qsTr("Чим гра рендерить кадри. Це не графічний API вікна програми.")
                    Segmented {
                        options: (Config.revision, Config.state("game_renderer").options || [])
                        currentValue: (Config.revision, Config.value("game_renderer"))
                        onChosen: (v) => Config.set("game_renderer", v)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Ui.narrow ? 0 : Theme.labelWidth + Theme.s3
                    role: "meta"
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    text: (Env.games.find(g => g.renderer === page.d.renderer) || {}).description || ""
                }
                SettingPath { key: "game_dir"; mode: "folder"; placeholder: qsTr("знайти автоматично (Steam)") }
                SettingPath { key: "rtx_game_dir"; mode: "folder"; placeholder: qsTr("з налаштувань RTXLauncher") }
                SettingRow {
                    id: gameRow
                    label: qsTr("Копія гри")
                    readonly property var g: Env.games.find(x => x.renderer === page.d.renderer) || ({ state: "unknown" })
                    StatusDot {
                        state: gameRow.g.state === "available" && !gameRow.g.accepted ? "failed" : gameRow.g.state
                        text: gameRow.g.state === "available" ? String(gameRow.g.detail) : Env.scanning ? qsTr("пошук…") : String(gameRow.g.detail || qsTr("не перевірено"))
                    }
                }
            }

            // ---- ВИХІД ----
            Section {
                title: qsTr("Файл")
                iconName: "folder"
                SettingRow {
                    label: qsTr("Пресет")
                    hint: qsTr("Налаштування в один клік: роздільна здатність, FPS, кодек, формат і звук. Далі їх можна підправити вручну.")
                    Combo {
                        options: Config.presets().map(p => ({ value: p.id, label: p.label, reason: p.description }))
                        placeholder: qsTr("вибрати готовий набір…")
                        selected: ""
                        onChosen: (v) => { Config.applyPreset(v); Ui.toast(qsTr("Пресет застосовано"), "success") }
                    }
                }
                SettingPath {
                    key: "output_path"
                    mode: "save"
                    placeholder: qsTr("поруч із демо")
                    nameFilters: [qsTr("Відео (*.mp4 *.mkv *.mov *.webm *.avi)"), qsTr("Усі файли (*)")]
                }
                SettingRow {
                    id: containerRow
                    label: qsTr("Формат файлу")
                    hint: qsTr("Змінює розширення файлу результату. Кодек не підміняється мовчки: несумісний покаже перевірка з виправленням.")
                    readonly property var cst: (Config.revision, Config.state("container"))
                    Combo {
                        options: containerRow.cst.options || []
                        selected: page.d.container
                        onChosen: (v) => Config.setContainer(v)
                    }
                }
                SettingToggle { key: "chapters" }
                SettingToggle { key: "faststart" }
                SettingToggle { key: "crash_safe" }
            }

            // ---- ВІДЕО ----
            Section {
                title: qsTr("Відео")
                iconName: "film"
                SettingRow {
                    id: sizeRow
                    key: "width"
                    readonly property string cur: (Config.revision, Config.value("width") + "x" + Config.value("height"))
                    RowLayout {
                        spacing: Theme.s2
                        Combo {
                            // стискається у вузькому вікні, щоб поля розміру лишались видні
                            Layout.fillWidth: true
                            Layout.minimumWidth: Theme.px(110)
                            Layout.maximumWidth: Theme.px(230)
                            options: (Config.revision, Config.state("width").options || [])
                            selected: sizeRow.cur
                            placeholder: qsTr("свій розмір")
                            onChosen: (v) => { const p = String(v).split("x"); Config.set("width", Number(p[0])); Config.set("height", Number(p[1])) }
                        }
                        Field {
                            Layout.preferredWidth: Theme.px(70)
                            horizontalAlignment: TextInput.AlignRight
                            text: (Config.revision, String(Config.value("width")))
                            onEditingFinished: Config.set("width", Number(text))
                            Accessible.name: qsTr("Ширина")
                        }
                        Label { text: "×"; role: "secondary" }
                        Field {
                            Layout.preferredWidth: Theme.px(70)
                            horizontalAlignment: TextInput.AlignRight
                            text: (Config.revision, String(Config.value("height")))
                            onEditingFinished: Config.set("height", Number(text))
                            Accessible.name: qsTr("Висота")
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
                SettingRow {
                    key: "fps"
                    RowLayout {
                        spacing: Theme.s2
                        Combo {
                            Layout.preferredWidth: Theme.px(120)
                            options: (Config.revision, Config.state("fps").options || [])
                            selected: (Config.revision, Config.value("fps"))
                            onChosen: (v) => Config.set("fps", v)
                        }
                        Field {
                            Layout.preferredWidth: Theme.px(110)
                            text: (Config.revision, String(Config.value("fps")))
                            placeholderText: "60000/1001"
                            onEditingFinished: Config.set("fps", text)
                            Accessible.name: qsTr("Своя частота кадрів")
                        }
                        Label { text: qsTr("кадрів/с"); role: "secondary" }
                        Item { Layout.fillWidth: true }
                    }
                }
                SettingRow {
                    key: "speed"
                    Combo {
                        options: (Config.revision, Config.state("speed").options || [])
                        selected: (Config.revision, Number(Config.value("speed")).toString())
                        onChosen: (v) => Config.set("speed", Number(v))
                    }
                }
                SettingCombo { key: "speed_audio" }
                SettingRow {
                    key: "motion_blur"
                    hint: qsTr("Скільки кадрів гри змішуються в один кадр відео (1 — вимкнено). 8–16 зазвичай досить.")
                    Slide {
                        from: 1
                        to: 64
                        stepSize: 1
                        decimals: 0
                        value: (Config.revision, Number(Config.value("motion_blur")))
                        onMoved: (v) => Config.set("motion_blur", Math.round(v))
                    }
                }
                SettingSlider { key: "shutter"; from: 1; to: 360; stepSize: 1; decimals: 0; suffix: "°" }
                SettingCombo { key: "video_codec" }
                SettingRow {
                    id: qualityRow
                    key: "quality"
                    readonly property var qs: (Config.revision, Config.state("quality"))
                    RowLayout {
                        spacing: Theme.s2
                        Combo {
                            visible: (qualityRow.qs.options || []).length > 0
                            Layout.fillWidth: true
                            Layout.minimumWidth: Theme.px(110)
                            Layout.maximumWidth: Theme.px(200)
                            options: qualityRow.qs.options || []
                            selected: (Config.revision, String(Config.value("quality")))
                            onChosen: (v) => Config.set("quality", Number(v))
                        }
                        Slide {
                            visible: (qualityRow.qs.options || []).length === 0
                            Layout.fillWidth: true
                            from: qualityRow.qs.min !== undefined ? qualityRow.qs.min : 0
                            to: qualityRow.qs.max !== undefined ? qualityRow.qs.max : 51
                            stepSize: 1
                            decimals: 0
                            value: (Config.revision, Number(Config.value("quality")))
                            onMoved: (v) => Config.set("quality", Math.round(v))
                        }
                        Btn {
                            kind: "ghost"
                            text: qsTr("Типова")
                            visible: (Config.revision, Number(Config.value("quality")) >= 0)
                            onClicked: Config.set("quality", -1)
                        }
                    }
                }
                SettingNumber { key: "target_size_mb"; decimals: 0; suffix: qsTr("МБ (0 — за якістю)") }
                SettingText { key: "video_bitrate"; placeholder: qsTr("напр. 20M (порожньо — за якістю)") }
                SettingCombo { key: "preset" }
                SettingCombo { key: "bit_depth" }
                SettingCombo { key: "chroma" }
                SettingText { key: "pix_fmt"; placeholder: "auto" }
                SettingCombo { key: "scaler" }
                SettingNumber { key: "gop_seconds"; suffix: qsTr("с") }
                SettingNumber { key: "threads"; suffix: qsTr("(0 — усі)") }
                SettingToggle { key: "accurate_color" }
                SettingToggle { key: "full_range" }
                SettingText { key: "video_options"; placeholder: "key=value; key2=value2" }
            }

            // ---- ПРОДУКТИВНІСТЬ ----
            Section {
                title: qsTr("Продуктивність")
                iconName: "cpu"
                SettingRow {
                    id: parallelRow
                    key: "parallel_games"
                    hint: qsTr("Кілька копій гри рендерять фрагмент частинами одночасно, потім частини склеюються без перекодування.")
                    readonly property var ps: (Config.revision, Config.state("parallel_games"))
                    ColumnLayout {
                        spacing: Theme.s1
                        Segmented {
                            options: (parallelRow.ps.options || []).map(o => ({ value: Number(o.value), label: o.label, available: o.available, reason: o.reason }))
                            currentValue: (Config.revision, Number(Config.value("parallel_games")))
                            onChosen: (v) => Config.set("parallel_games", v)
                        }
                        Label {
                            role: "meta"
                            text: qsTr("Підтримує рендерер: до %1× · рендеритиме: %2×").arg(page.d.parallelMax).arg(page.d.parallel)
                                  + (page.d.parallelReason !== "" && page.d.parallel < Number(Config.value("parallel_games")) ? " (" + page.d.parallelReason + ")" : "")
                        }
                    }
                }
                SettingCombo { key: "game_window" }
                SettingRow {
                    label: qsTr("Відеокодек на відеокарті")
                    visible: Config.advanced
                    ColumnLayout {
                        spacing: 2
                        Repeater {
                            model: Env.gpuEncoders
                            StatusDot {
                                required property var modelData
                                state: modelData.state === "available" ? "ready" : modelData.state === "failed" ? "failed" : modelData.state
                                text: modelData.label + (modelData.state === "failed" || modelData.state === "unavailable" ? " — " + modelData.detail : modelData.state === "checking" ? " — " + qsTr("перевіряється…") : "")
                            }
                        }
                        Label { visible: Env.gpuEncoders.length === 0; role: "secondary"; text: qsTr("GPU-кодеків у цій збірці FFmpeg немає") }
                    }
                }
                SettingCombo { key: "frame_transport" }
                SettingNumber { key: "max_pending_frames" }
                SettingCombo { key: "capture_format" }
                SettingNumber { key: "jpeg_quality" }
                SettingRow {
                    key: "render_width"
                    RowLayout {
                        spacing: Theme.s2
                        Field {
                            Layout.preferredWidth: Theme.px(70)
                            horizontalAlignment: TextInput.AlignRight
                            text: (Config.revision, String(Config.value("render_width")))
                            onEditingFinished: Config.set("render_width", Number(text))
                        }
                        Label { text: "×"; role: "secondary" }
                        Field {
                            Layout.preferredWidth: Theme.px(70)
                            horizontalAlignment: TextInput.AlignRight
                            text: (Config.revision, String(Config.value("render_height")))
                            onEditingFinished: Config.set("render_height", Number(text))
                        }
                        Label { Layout.fillWidth: true; role: "meta"; text: qsTr("0 × 0 — як відео; більше — згладжування") }
                    }
                }
                SettingToggle { key: "high_priority" }
                SettingToggle { key: "keep_temp_files" }
            }

            // ---- ГРА ----
            Section {
                title: qsTr("Гра")
                iconName: "gamepad"
                expanded: Config.advanced
                SettingToggle { key: "hide_hud" }
                SettingToggle { key: "hide_viewmodel" }
                SettingToggle { key: "mute_game_sound" }
                SettingToggle { key: "quit_game_when_done" }
                SettingToggle { key: "mute_engine_voice" }
                SettingToggle { key: "manual_mode" }
                SettingNumber { key: "menu_delay"; decimals: 1; suffix: qsTr("с") }
                SettingText { key: "extra_commands"; placeholder: qsTr("консольні команди, кожна з нового рядка") }
                SettingText { key: "extra_launch_args" }
                SettingPath { key: "game_exe"; mode: "open"; placeholder: qsTr("автоматично (64-біт, якщо є)") }
            }

            // ---- ДОДАТКОВІ ВЕРСІЇ ----
            Section {
                id: versions
                title: qsTr("Додаткові версії")
                subtitle: qsTr("з тих самих кадрів, за один рендер")
                iconName: "copy"
                readonly property var vs: (Config.revision, Config.state("extra_versions"))
                readonly property var chosen: (Config.revision, String(Config.value("extra_versions") || "").split(",").map(x => x.trim()).filter(x => x !== ""))
                Each {
                    items: versions.vs.options || []
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.s3
                        Check {
                            text: modelData.label
                            checked: versions.chosen.indexOf(modelData.value) >= 0
                            enabled: modelData.available !== false || checked
                            onToggled: {
                                const list = versions.chosen.filter(x => x !== modelData.value)
                                if (checked) list.push(modelData.value)
                                Config.set("extra_versions", list.join(","))
                            }
                        }
                        Label { Layout.fillWidth: true; role: "meta"; text: modelData.reason || ""; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                    }
                }
            }

            // ---- ЗВУК, ПЕРЕКЛАД — коротко, докладно у своїх робочих просторах ----
            Section {
                title: qsTr("Звук і мова")
                iconName: "wave"
                SettingToggle { key: "audio" }
                KeyValue {
                    visible: (Config.revision, Config.value("audio") === true)
                    name: qsTr("У файлі")
                    value: page.d.audioInFile ? qsTr("звук %1").arg((Config.revision, String(Config.value("audio_codec")))) : qsTr("звук окремим файлом")
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredWidth: Theme.px(100)   // ширина — від сторінки; у вузькому вікні кнопки переносяться
                    spacing: Theme.s2
                    Btn { text: qsTr("Звук і голоси…"); iconName: "wave"; onClicked: Ui.goTo("audio") }
                    Btn { text: qsTr("Субтитри, переклад, озвучення…"); iconName: "translate"; onClicked: Ui.goTo("ai") }
                }
            }
            Item { Layout.preferredHeight: Theme.s6 }
        }

        // ---- підсумок і дії ----
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Math.max(Theme.px(250), Math.min(Theme.px(360), page.width * 0.36))
            color: Theme.surface
            Rectangle { width: 1; height: parent.height; color: Theme.border }
            B.ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.s4
                anchors.leftMargin: Theme.s4 + 1
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.s4
                    Label { text: qsTr("Підсумок"); role: "section" }
                    RenderSummary { Layout.fillWidth: true }
                    RowLayout {
                        spacing: Theme.s2
                        Btn {
                            Layout.fillWidth: true
                            kind: "primary"
                            iconName: "render"
                            text: Config.errorCount > 0 ? qsTr("Неможливо: помилок %1").arg(Config.errorCount) : qsTr("Почати рендер")
                            enabled: Project.loaded && !Jobs.busy && Config.errorCount === 0
                            onClicked: Jobs.startRender(false)
                        }
                        Btn {
                            text: qsTr("Тест 3 с")
                            enabled: Project.loaded && !Jobs.busy && Config.errorCount === 0
                            onClicked: Jobs.startTest()
                        }
                    }
                    RowLayout {
                        spacing: Theme.s2
                        Btn {
                            Layout.fillWidth: true
                            iconName: "queue"
                            text: Queue.editingIndex >= 0 ? qsTr("Зберегти в пункт черги %1").arg(Queue.editingIndex + 1) : qsTr("Додати до черги")
                            enabled: Project.loaded && !Queue.running
                            onClicked: {
                                if (Queue.editingIndex >= 0) { Queue.saveEdit(); Ui.toast(qsTr("Пункт черги оновлено"), "success") }
                                else if (Queue.addCurrent()) Ui.toast(qsTr("Додано до черги"), "success")
                            }
                        }
                        Btn { visible: Queue.editingIndex >= 0; kind: "ghost"; text: qsTr("Скасувати"); onClicked: Queue.cancelEdit() }
                    }
                    RowLayout {
                        spacing: Theme.s2
                        Label { text: qsTr("Після рендеру"); role: "secondary" }
                        Combo {
                            Layout.fillWidth: true
                            options: [{ value: "none", label: qsTr("нічого не робити") }, { value: "shutdown", label: qsTr("вимкнути ПК") }, { value: "sleep", label: qsTr("сон") }]
                            selected: Jobs.afterDone
                            onChosen: (v) => Jobs.afterDone = v
                        }
                    }
                    JobProgress { Layout.fillWidth: true; visible: Jobs.busy }
                    Rectangle {
                        visible: Jobs.busy && (Jobs.kind === "render" || Jobs.kind === "test" || Jobs.kind === "queue")
                        Layout.fillWidth: true
                        Layout.preferredHeight: width * 9 / 16
                        color: "#000000"
                        radius: Theme.r2
                        Preview { anchors.fill: parent; anchors.margins: 1; background: "#000000" }
                    }
                    Label {
                        text: Config.errorCount + Config.warningCount > 0 ? qsTr("Перевірка налаштувань") : qsTr("Перевірка налаштувань: усе гаразд")
                        role: "section"
                    }
                    IssueList {
                        Layout.fillWidth: true
                        issues: (Config.revision, Config.issues)
                        showInfo: true
                        emptyText: qsTr("Помилок і попереджень немає.")
                    }
                }
            }
        }
    }
}
