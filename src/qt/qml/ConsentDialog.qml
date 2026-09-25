// Згода гравців на клонування голосу (і на бібліотеку голосів). Без неї — ні клонування,
// ні нових зразків: голос — особисті дані.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Dialog {
    id: cd
    property bool forLibrary: false
    title: qsTr("Згода гравців на клонування голосу")
    iconName: "user"
    onOpened: agree.checked = false
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s4
        Label {
            Layout.fillWidth: true
            text: qsTr("Клон голосу гравця можна використовувати лише з його дозволу. Підтвердьте, що всі гравці, чиї голоси будуть клоновані (або збережені в бібліотеку голосів), на це згодні. Зразки голосів лишаються на цьому комп'ютері, бібліотеку можна будь-коли очистити.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Check {
            id: agree
            text: qsTr("Я підтверджую згоду гравців")
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
                    if (cd.forLibrary) Config.set("voice_library_auto", true)
                    cd.close()
                }
            }
            Btn { text: qsTr("Скасувати"); onClicked: cd.close() }
        }
    }
}
