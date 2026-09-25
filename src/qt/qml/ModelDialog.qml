// Модель розпізнавання мовлення (whisper.cpp): один файл, завантажується лише на прохання.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Dialog {
    id: md
    title: qsTr("Модель розпізнавання мовлення")
    iconName: "download"
    property var models: []
    property int choice: 0
    onOpened: models = Jobs.whisperModels()
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s3
        Label {
            Layout.fillWidth: true
            text: qsTr("Розпізнавання працює локально (whisper.cpp). Потрібна модель — один файл, який завантажується один раз з Hugging Face (ggerganov/whisper.cpp) у теку програми:")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Label { Layout.fillWidth: true; text: Jobs.whisperModelsFolder(); role: "mono"; font.pixelSize: Theme.fontMeta; color: Theme.textSecondary; elide: Text.ElideMiddle }
        Repeater {
            model: md.models
            B.RadioButton {
                id: rb
                required property var modelData
                checked: md.choice === modelData.index
                onToggled: md.choice = modelData.index
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                text: modelData.label + " — " + modelData.sizeMb + " " + qsTr("МБ") + (modelData.installed ? "  ✓ " + qsTr("є") : "")
                contentItem: Label { leftPadding: rb.indicator.width + rb.spacing; text: rb.text }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.s2
            readonly property var m: md.models.length > md.choice ? md.models[md.choice] : null
            Btn {
                kind: "primary"
                enabled: parent.m !== null && !Jobs.busy
                text: parent.m && parent.m.installed ? qsTr("Використовувати цю") : qsTr("Завантажити (%1 МБ)").arg(parent.m ? parent.m.sizeMb : 0)
                onClicked: {
                    const m = parent.m
                    if (m.installed) Config.set("whisper_model", m.path)
                    else Jobs.downloadWhisperModel(m.index)
                    md.close()
                }
            }
            Btn { text: qsTr("Закрити"); onClicked: md.close() }
        }
    }
}
