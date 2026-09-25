// Встановлення локального рушія озвучення OmniVoice (~2–5 ГБ: Python, PyTorch, модель).
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Dialog {
    id: ed
    title: qsTr("Встановити локальний рушій озвучення")
    iconName: "download"
    readonly property bool nvidia: Env.gpus.some(g => g.vendor === "nvidia")
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s3
        Label {
            Layout.fillWidth: true
            text: qsTr("OmniVoice озвучує переклад на цьому комп'ютері, без онлайн-сервісів. Програма завантажить Python, PyTorch і модель (близько 2–5 ГБ) у свою теку. Це можна будь-коли видалити.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Check {
            id: cuda
            text: qsTr("Для відеокарти NVIDIA (CUDA) — швидше, але більше завантаження")
            checked: ed.nvidia
            enabled: ed.nvidia
        }
        Label {
            visible: !ed.nvidia
            Layout.fillWidth: true
            role: "secondary"
            text: qsTr("GPU NVIDIA не знайдено — рушій працюватиме на процесорі.")
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.s2
            Btn { kind: "primary"; text: qsTr("Встановити"); enabled: !Jobs.busy; onClicked: { Jobs.voiceEngine(true, cuda.checked); ed.close() } }
            Btn { text: qsTr("Скасувати"); onClicked: ed.close() }
        }
    }
}
