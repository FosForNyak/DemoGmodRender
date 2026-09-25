// Згода гравців на клонування голосу (і на бібліотеку голосів). Без неї — ні клонування,
// ні нових зразків: голос — особисті дані.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Dialog {
    id: cd
    property bool forLibrary: false
    title: qsTr("Клонування голосів гравців")
    iconName: "user"
    onOpened: agree.checked = false
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s3
        Label {
            Layout.fillWidth: true
            text: qsTr("Голос — особисті дані. Клон звучить як сама людина, тому озвучувати ним переклад можна лише з її дозволу, і глядач має знати, що голос синтезований (доріжки підписуються «озвучення ШІ»).")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Зразки голосів зберігаються лише на цьому ПК; з ElevenLabs вони надсилаються в їхній сервіс для створення клону. Бібліотеку можна будь-коли очистити.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Check {
            id: agree
            Layout.fillWidth: true
            text: qsTr("Гравці в моїх демо погодились на клонування своїх голосів")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.s2
            Btn {
                kind: "primary"
                enabled: agree.checked
                text: qsTr("Підтвердити")
                onClicked: {
                    Config.set("tts_clone_ack", true)
                    Config.set(cd.forLibrary ? "voice_library_auto" : "tts_clone", true)
                    cd.close()
                }
            }
            Btn { text: qsTr("Скасувати"); onClicked: cd.close() }
        }
    }
}
