// Кнопка-літера (M / S на доріжках): увімкнена — кольоровий квадрат із літерою.
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr.Ui

B.AbstractButton {
    id: lt
    property string letter: "M"
    property bool on: false
    property color onColor: Theme.accent
    property string tip: ""
    implicitWidth: Theme.px(20)
    implicitHeight: Theme.px(20)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: tip
    Accessible.checkable: true
    Accessible.checked: on
    background: Rectangle {
        radius: Theme.r1
        color: lt.on ? lt.onColor : "transparent"
        border.width: lt.on ? 0 : 1
        border.color: lt.visualFocus ? Theme.focus : lt.hovered ? Theme.textSecondary : Theme.border
    }
    contentItem: Text {
        text: lt.letter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMeta
        font.weight: Font.Bold
        color: lt.on ? "#121216" : lt.hovered ? Theme.text : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    B.ToolTip.visible: tip !== "" && hovered
    B.ToolTip.text: tip
}
