// Чат і події демо разом із розпізнаним голосовим чатом: пошук, фільтри, перехід до моменту,
// розпізнавання мовлення (whisper.cpp, локально).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

ColumnLayout {
    id: cp
    spacing: 0
    property string query: ""
    property bool showChat: true
    property bool showEvents: false
    property bool showSpeech: true
    property bool onlyFragment: false
    readonly property var whisper: Env.dependencies.find(d => d.id === "whisper.cli") || ({ state: "unknown" })
    readonly property var whisperModel: Env.dependencies.find(d => d.id === "whisper.model") || ({ state: "unknown" })
    readonly property bool speechReady: whisper.state === "ready" && whisperModel.state === "ready"
    readonly property var items: {
        let out = []
        const q = query.toLowerCase()
        for (const e of Project.chat) {
            const isChat = e.kind === "chat"
            if (isChat ? !showChat : !showEvents) continue
            out.push({ time: e.time, who: e.who, text: e.text, kind: e.kind })
        }
        if (showSpeech)
            for (const l of Project.transcript) out.push({ time: l.start, who: l.speaker, text: l.text, kind: "speech" })
        out = out.filter(x => (!onlyFragment || (x.time >= Project.fragmentStart && x.time <= Project.fragmentEnd))
                          && (q === "" || (x.who + " " + x.text).toLowerCase().indexOf(q) >= 0))
        out.sort((a, b) => a.time - b.time)
        return out
    }
    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Theme.s3
        spacing: Theme.s2
        RowLayout {
            spacing: Theme.s2
            Btn {
                iconName: "mic"
                kind: cp.speechReady ? "secondary" : "ghost"
                text: Project.hasTranscript ? qsTr("Розпізнати ще раз") : qsTr("Розпізнати мовлення")
                enabled: cp.speechReady && !Jobs.busy && Project.players.length > 0
                onClicked: Jobs.transcribe(Project.hasTranscript)
            }
            Btn {
                visible: !cp.speechReady
                kind: "ghost"
                iconName: "download"
                text: qsTr("Модель…")
                onClicked: Ui.modelInstallRequested()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: !cp.speechReady
            role: "meta"
            color: Theme.warning
            wrapMode: Text.WordWrap
            text: cp.whisper.state !== "ready" ? qsTr("Немає whisper-cli: %1").arg(cp.whisper.detail || "") : qsTr("Немає моделі розпізнавання — встановіть її")
        }
        Field {
            Layout.fillWidth: true
            placeholderText: qsTr("Пошук у чаті й мовленні")
            onTextChanged: cp.query = text
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s2
            Check { text: qsTr("Чат"); checked: cp.showChat; onToggled: cp.showChat = checked }
            Check { text: qsTr("Мовлення"); checked: cp.showSpeech; onToggled: cp.showSpeech = checked }
            Check { text: qsTr("Події"); checked: cp.showEvents; onToggled: cp.showEvents = checked }
            Check { text: qsTr("Лише фрагмент"); checked: cp.onlyFragment; onToggled: cp.onlyFragment = checked; enabled: !Project.wholeDemo }
        }
    }
    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: cp.items
        B.ScrollBar.vertical: B.ScrollBar {}
        delegate: B.ItemDelegate {
            id: cd
            required property var modelData
            width: ListView.view.width
            padding: Theme.s2
            leftPadding: Theme.s3
            onClicked: Project.playhead = modelData.time
            background: Rectangle { color: cd.hovered ? Theme.hover : "transparent" }
            contentItem: ColumnLayout {
                spacing: 1
                RowLayout {
                    spacing: Theme.s2
                    Label { text: Project.formatTime(cd.modelData.time); font.family: Theme.monoFamily; role: "meta" }
                    Icon {
                        Layout.preferredWidth: Theme.px(11)
                        Layout.preferredHeight: Theme.px(11)
                        name: cd.modelData.kind === "speech" ? "mic" : cd.modelData.kind === "chat" ? "chat" : "info"
                        color: Theme.textMuted
                    }
                    Label { Layout.fillWidth: true; text: cd.modelData.who; font.weight: Font.Medium; font.pixelSize: Theme.fontSmall }
                }
                Label {
                    Layout.fillWidth: true
                    text: cd.modelData.text
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    font.pixelSize: Theme.fontSmall
                    color: cd.modelData.kind === "chat" ? Theme.text : Theme.textSecondary
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: cp.items.length === 0
            text: qsTr("Нічого немає")
            role: "secondary"
        }
    }
}
