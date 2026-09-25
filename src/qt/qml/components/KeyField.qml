// API-ключ сервісу: у налаштуваннях — лише зашифрований (ядро), на екрані — крапки.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

SettingRow {
    id: r
    readonly property bool stored: (Config.revision, String(Config.value(r.key) || "") !== "")
    property bool editing: false
    RowLayout {
        spacing: Theme.s2
        Field {
            id: f
            Layout.fillWidth: true
            visible: r.editing || !r.stored
            echoMode: TextInput.Password
            placeholderText: qsTr("вставте ключ")
            onAccepted: save()
            Accessible.name: r.label
            function save() {
                if (text.trim() !== "") Config.setSecret(r.key, text.trim())
                text = ""   // не тримати відкритий ключ у полі довше, ніж треба
                r.editing = false
            }
        }
        Label {
            visible: !f.visible
            Layout.fillWidth: true
            text: qsTr("••••••••  збережено (зашифровано)")
            color: Theme.success
        }
        Btn {
            visible: f.visible
            text: qsTr("Зберегти")
            enabled: f.text.trim() !== ""
            onClicked: f.save()
        }
        Btn {
            visible: f.visible && r.stored
            kind: "ghost"
            text: qsTr("Скасувати")
            onClicked: { f.text = ""; r.editing = false }
        }
        Btn {
            visible: !f.visible
            kind: "ghost"
            text: qsTr("Змінити")
            onClicked: { r.editing = true; f.forceActiveFocus() }
        }
        Btn {
            visible: !f.visible
            kind: "ghost"
            text: qsTr("Прибрати")
            onClicked: Config.setSecret(r.key, "")
        }
    }
}
