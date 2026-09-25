// API-ключ сервісу: зберігається лише зашифрованим (ядро), тут видно лише «збережено».
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
            placeholderText: qsTr("Вставте ключ API")
            onAccepted: save()
            function save() {
                if (text !== "") Config.setSecret(r.key, text)
                text = ""
                r.editing = false
            }
        }
        Label {
            visible: !f.visible
            Layout.fillWidth: true
            text: qsTr("Ключ збережено (зашифровано)")
            color: Theme.success
        }
        Btn {
            visible: f.visible
            text: qsTr("Зберегти")
            enabled: f.text !== ""
            onClicked: f.save()
        }
        Btn {
            visible: !f.visible
            kind: "ghost"
            text: qsTr("Змінити")
            onClicked: { r.editing = true; f.forceActiveFocus() }
        }
        Btn {
            visible: r.stored
            kind: "ghost"
            text: qsTr("Видалити")
            onClicked: Config.setSecret(r.key, "")
        }
    }
}
