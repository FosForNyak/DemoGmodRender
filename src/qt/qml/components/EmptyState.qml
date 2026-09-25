// Порожній стан: що відсутнє і як далі (значок, заголовок, пояснення, дія).
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

ColumnLayout {
    id: es
    property string iconName: "info"
    property string title: ""
    property string text: ""
    property string actionText: ""
    property string actionIcon: ""
    signal action()
    spacing: Theme.s3
    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        width: Theme.px(52)
        height: width
        radius: Theme.r3
        color: Theme.accentSoft
        Icon {
            anchors.centerIn: parent
            width: Theme.px(26)
            height: width
            name: es.iconName
            color: Theme.accentText
        }
    }
    Label {
        Layout.alignment: Qt.AlignHCenter
        text: es.title
        role: "section"
    }
    Label {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: Theme.px(420)
        horizontalAlignment: Text.AlignHCenter
        text: es.text
        role: "secondary"
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        visible: text !== ""
    }
    Btn {
        Layout.alignment: Qt.AlignHCenter
        visible: es.actionText !== ""
        kind: "primary"
        text: es.actionText
        iconName: es.actionIcon
        onClicked: es.action()
    }
}
