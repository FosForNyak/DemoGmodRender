// Встановлення локального рушія озвучення OmniVoice: великий обсяг — лише з підтвердженням.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Dialog {
    id: ed
    title: qsTr("Встановити локальний рушій озвучення?")
    iconName: "download"
    readonly property bool nvidia: Env.gpus.some(g => g.vendor === "nvidia")
    onOpened: cuda.checked = nvidia
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s3
        Label {
            Layout.fillWidth: true
            text: Ui.fmt(qsTr("Буде завантажено близько {} з інтернету: менеджер пакетів uv, Python 3.12, PyTorch{}, OmniVoice і його модель. Усе — в одну теку, в системі нічого не змінюється; видалити можна тут же."),
                         cuda.checked ? qsTr("5 ГБ") : qsTr("2 ГБ"), cuda.checked ? qsTr(" з CUDA") : "")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Label {
            Layout.fillWidth: true
            role: "meta"
            text: Voices.engineDir
            wrapMode: Text.WrapAnywhere
            elide: Text.ElideNone
        }
        Check {
            id: cuda
            visible: ed.nvidia
            text: qsTr("Для GPU NVIDIA (CUDA, значно швидше)")
        }
        Label {
            visible: !ed.nvidia
            Layout.fillWidth: true
            color: Theme.warning
            text: qsTr("GPU NVIDIA не знайдено — версія для процесора.")
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.s2
            Btn {
                kind: "primary"
                iconName: "download"
                text: qsTr("Встановити")
                enabled: !Jobs.busy
                onClicked: { Jobs.voiceEngine(true, cuda.checked && ed.nvidia); ed.close() }
            }
            Btn { text: qsTr("Скасувати"); onClicked: ed.close() }
        }
    }
}
