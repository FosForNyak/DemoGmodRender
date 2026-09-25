// Рядок «назва — значення» для відомостей (демо, система).
import QtQuick
import QtQuick.Layouts
import Gmdr.Ui

RowLayout {
    property string name: ""
    property string value: ""
    property string valueRole: "body"
    property color valueColor: Theme.text
    Layout.fillWidth: true
    spacing: Theme.s3
    Label {
        Layout.preferredWidth: Theme.px(150)
        text: parent.name
        role: "secondary"
    }
    Label {
        Layout.fillWidth: true
        text: parent.value
        role: parent.valueRole
        color: parent.valueColor
        wrapMode: Text.WrapAnywhere
        elide: Text.ElideNone
        maximumLineCount: 3
    }
}
