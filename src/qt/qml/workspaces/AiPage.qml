// «Переклад і озвучення»: мови, шаблон публікації і що зробити, сервіс перекладу, рушій
// озвучення (локальний OmniVoice чи ElevenLabs), клонування голосів гравців (лише після
// підтвердження згоди) і бібліотека зразків голосів.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Page {
    id: ai
    title: qsTr("Переклад і озвучення")
    subtitle: qsTr("Розмови гравців — іншими мовами: перекладені субтитри й озвучення голосом самого гравця чи готовими голосами. Робиться після рендеру, з розпізнаного мовлення.")
    maxContentWidth: Theme.px(900)

    readonly property int rev: Config.revision
    function dep(id) { return Env.dependencies.find(d => d.id === id) || ({ state: "unknown", detail: "" }) }
    readonly property bool speechReady: dep("whisper.cli").state === "ready" && dep("whisper.model").state === "ready"
    readonly property var langs: (rev, String(Config.value("dub_languages") || "")).split(",").map(x => x.trim()).filter(x => x !== "")
    readonly property bool dubOn: (rev, Config.value("dub") === true)
    readonly property string engine: (rev, String(Config.value("tts_engine")))
    readonly property string translator: (rev, String(Config.value("translator")))
    readonly property var provider: Voices.providers.find(p => p.id === translator) || null
    readonly property bool nvidia: Env.gpus.some(g => g.vendor === "nvidia")
    property string checking: ""          // що перевіряли востаннє: translator / elevenlabs / engine

    function setLangs(list) { Config.set("dub_languages", list.join(",")) }
    function outputOn(id) { return (rev, String(Config.value("dub_outputs") || "")).split(",").map(x => x.trim()).indexOf(id) >= 0 }
    function setOutput(id, on) {
        const v = []
        for (const k of ["tracks", "videos", "audio"])
            if (k === id ? on : outputOn(k)) v.push(k)
        Config.set("dub_outputs", v.join(","))
    }
    function statusFor(what) {
        const r = Jobs.lastResult
        if (ai.checking !== what || !r || (r.kind !== "serviceCheck" && r.kind !== "voiceEngine") || r.state === "cancelled") return null
        return { ok: r.state === "succeeded", text: r.state === "succeeded" ? r.text : r.title }
    }

    // ---- розпізнавання потрібне спершу ----
    Banner {
        visible: !ai.speechReady
        text: qsTr("Спершу потрібне розпізнавання мовлення: завантажте модель whisper.")
        actionText: qsTr("Завантажити модель...")
        actionIcon: "download"
        onAction: Ui.modelInstallRequested()
    }

    // ---- мови ----
    Section {
        title: qsTr("Мови")
        subtitle: qsTr("На які мови перекладати")
        iconName: "globe"
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s2
            Each {
                items: (ai.rev, Config.state("dub_languages").options || [])
                Chip {
                    required property var modelData
                    readonly property var info: Voices.languages.find(l => l.code === modelData.value) || ({ english: "" })
                    text: modelData.label
                    on: ai.langs.indexOf(modelData.value) >= 0
                    dim: modelData.warning === true
                    tip: info.english + " (" + modelData.value + ")" + (modelData.reason ? "\n" + modelData.reason : "")
                    onClicked: {
                        const l = ai.langs.slice()
                        const i = l.indexOf(modelData.value)
                        if (i >= 0) l.splice(i, 1)
                        else l.push(modelData.value)
                        ai.setLangs(l)
                    }
                }
            }
        }
        RowLayout {
            spacing: Theme.s2
            Label {
                role: "secondary"
                text: ai.langs.length === 0 ? qsTr("Виберіть одну чи кілька мов.")
                      : Ui.fmt(qsTr("Вибрано: {}"), ai.langs.map(c => (Voices.languages.find(l => l.code === c) || { native: c }).native).join(", "))
            }
            Btn { visible: ai.langs.length > 0; kind: "ghost"; text: qsTr("Очистити"); onClicked: Config.set("dub_languages", "") }
        }
        IssueList { Layout.fillWidth: true; issues: (ai.rev, Config.issuesFor("dub_languages")) }
        SettingCombo { key: "whisper_language"; hint: qsTr("Якщо всі говорять однією мовою, краще вказати її — «визначити» дивиться лише\nна перші 30 секунд мовлення кожного гравця.") }
    }

    // ---- що зробити ----
    Section {
        title: qsTr("Що зробити")
        subtitle: qsTr("Шаблон вмикає все потрібне для сервісу; далі можна змінити вручну.")
        iconName: "sparkle"
        Label { text: qsTr("Шаблон публікації"); role: "secondary"; font.weight: Font.DemiBold }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s2
            Repeater {
                model: Voices.templates
                Btn {
                    required property var modelData
                    kind: (ai.rev, Config.value("dub_template")) === modelData.id ? "primary" : "secondary"
                    text: modelData.label
                    tip: modelData.hint
                    onClicked: Voices.applyTemplate(modelData.id)
                }
            }
        }
        Label {
            readonly property var cur: Voices.templates.find(t => t.id === (ai.rev, Config.value("dub_template"))) || null
            visible: cur !== null
            Layout.fillWidth: true
            role: "secondary"
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            text: cur ? cur.hint : ""
        }
        Item { Layout.preferredHeight: Theme.s2 }
        Label { text: qsTr("Результат"); role: "secondary"; font.weight: Font.DemiBold }
        SettingToggle { key: "translate_subtitles" }
        SettingToggle {
            key: "dub"
            hint: qsTr("Синтезований голос читає переклад на місці кожної репліки; звук гри лишається. Оригінальні\nголоси можна залишити тихо під озвученням. Довша фраза трохи пришвидшується.")
        }
        SettingRow {
            id: outputsRow
            key: "dub_outputs"
            ColumnLayout {
                spacing: Theme.s1
                Each {
                    items: outputsRow.st.options || []
                    Check {
                        required property var modelData
                        text: modelData.label
                        enabled: modelData.available !== false
                        checked: ai.outputOn(modelData.value)
                        onToggled: ai.setOutput(modelData.value, checked)
                        B.ToolTip.visible: hovered && (modelData.reason || "") !== ""
                        B.ToolTip.text: modelData.reason || ""
                    }
                }
            }
        }
        SettingCombo { key: "dub_audio_format" }
        SettingRow {
            id: origRow
            key: "dub_original_volume"
            hint: qsTr("Гучність оригінальних голосів під озвученням: 0 — лише переклад, 10–15% — «закадровий» переклад.")
            Slide {
                from: 0
                to: 50
                stepSize: 1
                decimals: 0
                suffix: "%"
                value: (ai.rev, Number(Config.value("dub_original_volume")) * 100)
                onMoved: (v) => Config.set("dub_original_volume", v / 100)
            }
        }
        RowLayout {
            spacing: Theme.s2
            Btn {
                iconName: "chat"
                text: qsTr("Перекласти субтитри зараз")
                enabled: Project.loaded && Project.players.length > 0 && ai.speechReady && ai.langs.length > 0 && !Jobs.busy
                tip: qsTr("Без рендеру: розпізнати мовлення (якщо ще ні) і записати перекладені субтитри\nпоруч із вихідним файлом. Час — від початку фрагмента (або демо, якщо вибрано все).")
                onClicked: Jobs.translateOnly(!Project.wholeDemo)
            }
            Label {
                visible: !Project.loaded
                role: "meta"
                text: qsTr("Відкрийте демо, щоб перекласти субтитри без рендеру.")
            }
        }
    }

    // ---- сервіс перекладу ----
    Section {
        title: qsTr("Сервіс перекладу")
        subtitle: qsTr("Онлайн-сервіс з ключем або локальна мовна модель на вашому ПК.")
        iconName: "link"
        SettingCombo { key: "translator" }
        Label {
            Layout.fillWidth: true
            role: "secondary"
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            visible: text !== ""
            text: ai.translator === "deepl" ? qsTr("Найприродніший переклад. Безкоштовний ключ — 500 000 символів на місяць (deepl.com/pro-api).")
                  : ai.translator === "google" ? qsTr("Google Cloud Translation: ключ API з увімкненим Cloud Translation API.")
                  : ai.translator === "libre" ? qsTr("Свій сервер LibreTranslate (безкоштовно, локально) або публічний з ключем.")
                  : ai.translator === "openai" ? qsTr("Локально й безкоштовно: Ollama (ollama pull qwen2.5:7b) чи LM Studio. Або хмара з OpenAI-сумісним API (OpenAI, OpenRouter…) — тоді потрібен ключ.")
                  : ""
        }
        SettingText { key: "translator_url"; placeholder: ai.provider ? ai.provider.defaultUrl : "" }
        SettingText { key: "translator_model"; placeholder: "qwen2.5:7b" }
        Repeater {
            model: ["deepl", "google", "libre", "openai"]
            KeyField {
                required property string modelData
                key: modelData + "_key"
                label: ai.provider && ai.provider.id === modelData && !ai.provider.needsKey ? qsTr("Ключ API (якщо треба)") : qsTr("Ключ API")
            }
        }
        RowLayout {
            spacing: Theme.s3
            Btn {
                iconName: "check"
                text: qsTr("Перевірити")
                enabled: !Jobs.busy
                onClicked: { ai.checking = "translator"; Jobs.checkService(false) }
            }
            StatusText { status: ai.statusFor("translator") }
        }
        Label {
            visible: ai.provider !== null && !ai.provider.local
            Layout.fillWidth: true
            role: "meta"
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            text: qsTr("Текст реплік надсилається цьому сервісу. Переклади кешуються — той самий рядок удруге не оплачується.")
        }
    }

    // ---- озвучення ----
    Section {
        title: qsTr("Озвучення")
        subtitle: qsTr("Чим озвучувати переклад і чиїм голосом.")
        iconName: "mic"
        SettingRow {
            id: engineRow
            key: "tts_engine"
            Item {
                implicitHeight: Theme.controlHeight
                Segmented {
                    options: engineRow.st.options || []
                    currentValue: ai.engine
                    onChosen: (v) => Config.set("tts_engine", v)
                }
            }
        }
        // Локальний рушій
        ColumnLayout {
            visible: ai.engine === "omnivoice"
            Layout.fillWidth: true
            spacing: Theme.s2
            Label {
                Layout.fillWidth: true
                role: "secondary"
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                text: qsTr("OmniVoice (k2-fsa, Apache-2.0): 600+ мов, зокрема українська, англійська й російська; клонує голос із кількох секунд зразка. Працює на вашому ПК, нічого нікуди не надсилає.")
            }
            SettingRow {
                id: engineState
                label: qsTr("Стан")
                readonly property bool installed: ai.dep("tts.omnivoice").state === "ready"
                readonly property bool ownPython: (ai.rev, String(Config.value("tts_python") || "")) !== ""
                RowLayout {
                    spacing: Theme.s2
                    Label {
                        text: engineState.installed ? (engineState.ownPython ? qsTr("свій Python") : qsTr("встановлено")) : qsTr("не встановлено")
                        color: engineState.installed ? Theme.success : Theme.warning
                    }
                    Btn {
                        visible: !engineState.installed && !engineState.ownPython
                        kind: "primary"
                        iconName: "download"
                        text: qsTr("Встановити...")
                        enabled: !Jobs.busy
                        onClicked: Ui.voiceEngineInstallRequested()
                    }
                    Btn {
                        visible: engineState.installed || engineState.ownPython
                        iconName: "check"
                        text: qsTr("Перевірити")
                        enabled: !Jobs.busy
                        onClicked: { ai.checking = "engine"; Jobs.voiceEngine(false, false) }
                    }
                    Btn {
                        visible: engineState.installed && !engineState.ownPython
                        kind: "ghost"
                        text: qsTr("Видалити")
                        enabled: !Jobs.busy
                        onClicked: {
                            const err = Voices.removeEngine()
                            if (err !== "") Ui.toast(Ui.fmt(qsTr("Не вдалося видалити: {}"), err), "error")
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
            StatusText { status: ai.statusFor("engine") }
            SettingRow {
                id: deviceRow
                key: "tts_device"
                Item {
                    implicitHeight: Theme.controlHeight
                    Segmented {
                        options: deviceRow.st.options || []
                        currentValue: (ai.rev, Config.value("tts_device"))
                        onChosen: (v) => Config.set("tts_device", v)
                    }
                }
            }
            Label {
                visible: !ai.nvidia && Env.gpus.length > 0
                Layout.fillWidth: true
                role: "meta"
                color: Theme.warning
                wrapMode: Text.WordWrap
                text: qsTr("GPU NVIDIA не знайдено — на процесорі озвучення в рази повільніше.")
            }
            SettingText {
                key: "tts_python"
                placeholder: qsTr("порожньо — встановлений програмою")
                hint: qsTr("Python, у якому вже є пакет omnivoice (pip install omnivoice) — напр. на Linux.")
            }
        }
        // ElevenLabs
        ColumnLayout {
            visible: ai.engine === "elevenlabs"
            Layout.fillWidth: true
            spacing: Theme.s2
            Label {
                Layout.fillWidth: true
                role: "secondary"
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                text: qsTr("ElevenLabs — хмарний сервіс із природними голосами; потрібен ключ (elevenlabs.io → API Keys). Текст перекладу, а з клонуванням — і зразки голосів гравців надсилаються в ElevenLabs.")
            }
            KeyField { key: "elevenlabs_key" }
            SettingCombo { key: "elevenlabs_model"; hint: qsTr("У дужках — скільки мов. v3 — найбільше мов і найвиразніше; Flash — найшвидше й найдешевше.") }
            SettingText {
                key: "elevenlabs_voice"
                placeholder: qsTr("порожньо — різні готові голоси")
                hint: qsTr("voice_id голосу з вашої бібліотеки ElevenLabs — для гравців без клону.")
            }
            RowLayout {
                spacing: Theme.s3
                Btn {
                    iconName: "check"
                    text: qsTr("Перевірити ключ")
                    enabled: !Jobs.busy && (ai.rev, String(Config.value("elevenlabs_key") || "")) !== ""
                    onClicked: { ai.checking = "elevenlabs"; Jobs.checkService(true) }
                }
                StatusText { status: ai.statusFor("elevenlabs") }
            }
        }
        Item { Layout.preferredHeight: Theme.s2 }
        Label { text: qsTr("Голоси гравців"); role: "secondary"; font.weight: Font.DemiBold }
        SettingRow {
            key: "tts_clone"
            label: ""
            hint: qsTr("Голос кожного гравця клонується зі зразків його ж фраз у демо (3–10 с чистого мовлення).\nБез клонування кожен гравець отримує свій готовий голос.")
            Toggle {
                text: Config.label("tts_clone")
                checked: (ai.rev, Config.value("tts_clone") === true && Config.value("tts_clone_ack") === true)
                onToggled: {
                    if (checked && Config.value("tts_clone_ack") !== true) {
                        checked = Qt.binding(() => (ai.rev, Config.value("tts_clone") === true && Config.value("tts_clone_ack") === true))
                        Ui.consentRequested(false)
                    } else {
                        Config.set("tts_clone", checked)
                    }
                }
            }
        }
        RowLayout {
            visible: (ai.rev, Config.value("tts_clone_ack") === true)
            spacing: Theme.s2
            Label { role: "secondary"; text: qsTr("Згоду гравців підтверджено.") }
            Btn {
                kind: "ghost"
                text: qsTr("Відкликати")
                onClicked: {
                    Config.set("tts_clone_ack", false)
                    Config.set("tts_clone", false)
                    Config.set("voice_library_auto", false)
                }
            }
        }
    }

    // ---- бібліотека голосів ----
    Section {
        title: qsTr("Бібліотека голосів")
        subtitle: qsTr("Зразки голосів гравців (за SteamID) для клонування — з кожного нового демо додаються найчистіші фрази.")
        iconName: "library"
        SettingRow {
            key: "voice_library_auto"
            label: ""
            hint: qsTr("Що більше зразків, то точніше клон. Зберігаються лише на цьому ПК, у теці програми;\nгравці без SteamID (боти) не накопичуються.")
            Toggle {
                text: Config.label("voice_library_auto")
                checked: (ai.rev, Config.value("voice_library_auto") === true && Config.value("tts_clone_ack") === true)
                onToggled: {
                    if (checked && Config.value("tts_clone_ack") !== true) {
                        checked = Qt.binding(() => (ai.rev, Config.value("voice_library_auto") === true && Config.value("tts_clone_ack") === true))
                        Ui.consentRequested(true)
                    } else {
                        Config.set("voice_library_auto", checked)
                    }
                }
            }
        }
        Label { visible: Voices.profiles.length === 0; role: "secondary"; text: qsTr("Поки порожньо.") }
        Rectangle {
            visible: Voices.profiles.length > 0
            Layout.fillWidth: true
            implicitHeight: libCol.implicitHeight + 2
            radius: Theme.r2
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: libCol
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.rowHeight
                    Layout.leftMargin: Theme.s3
                    Layout.rightMargin: Theme.s3
                    spacing: Theme.s3
                    Label { Layout.fillWidth: true; text: qsTr("Гравець"); role: "meta"; font.weight: Font.DemiBold }
                    Label { Layout.preferredWidth: Theme.px(70); text: qsTr("Зразків"); role: "meta"; font.weight: Font.DemiBold }
                    Label { Layout.preferredWidth: Theme.px(70); text: qsTr("Секунд"); role: "meta"; font.weight: Font.DemiBold }
                    Label { Layout.preferredWidth: Theme.px(90); text: qsTr("Клон"); role: "meta"; font.weight: Font.DemiBold }
                    Item { Layout.preferredWidth: Theme.px(28) }
                }
                Repeater {
                    model: Voices.profiles
                    Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.rowHeight
                        color: index % 2 ? "transparent" : Theme.tint(Theme.surface2, 0.6)
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.s3
                            anchors.rightMargin: Theme.s3
                            spacing: Theme.s3
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                HoverHandler { id: nh }
                                B.ToolTip.visible: nh.hovered
                                B.ToolTip.text: modelData.key
                            }
                            Label { Layout.preferredWidth: Theme.px(70); text: String(modelData.samples); role: "secondary" }
                            Label { Layout.preferredWidth: Theme.px(70); text: Math.round(modelData.seconds); role: "secondary" }
                            Label { Layout.preferredWidth: Theme.px(90); text: modelData.cloned ? "ElevenLabs" : ""; color: Theme.success }
                            IconBtn {
                                iconName: "close"
                                tip: qsTr("Видалити зразки цього гравця")
                                implicitWidth: Theme.px(28)
                                implicitHeight: Theme.px(28)
                                onClicked: Voices.deleteProfile(modelData.key)
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            spacing: Theme.s2
            Btn { kind: "ghost"; iconName: "refresh"; text: qsTr("Оновити"); onClicked: Voices.refreshProfiles() }
            Btn { kind: "ghost"; iconName: "folder"; text: qsTr("Відкрити теку"); onClicked: Voices.openFolder() }
            Btn { kind: "ghost"; text: qsTr("Очистити все..."); enabled: Voices.profiles.length > 0; onClicked: clearDialog.open() }
        }
    }
    Item { Layout.preferredHeight: Theme.s6 }

    Dialog {
        id: clearDialog
        title: qsTr("Очистити бібліотеку голосів?")
        iconName: "warning"
        iconColor: Theme.warning
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s4
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: Ui.fmt(qsTr("Буде видалено зразки всіх гравців ({})."), Voices.profiles.length)
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.s2
                Btn { kind: "danger"; text: qsTr("Видалити все"); onClicked: { Voices.clearProfiles(); clearDialog.close() } }
                Btn { text: qsTr("Скасувати"); onClicked: clearDialog.close() }
            }
        }
    }
}
