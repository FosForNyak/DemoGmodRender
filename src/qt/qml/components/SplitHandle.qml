// Межа між областями, яку можна тягнути (тонка лінія, ширшає під курсором).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Templates as T
import Gmdr.Ui

Rectangle {
    property bool vertical: false
    implicitWidth: vertical ? 0 : Theme.px(5)
    implicitHeight: vertical ? Theme.px(5) : 0
    color: T.SplitHandle.pressed ? Theme.accent : T.SplitHandle.hovered ? Theme.borderStrong : Theme.bg
    Rectangle {
        anchors.centerIn: parent
        width: parent.vertical ? parent.width : 1
        height: parent.vertical ? 1 : parent.height
        color: Theme.border
    }
}
