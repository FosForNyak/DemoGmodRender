// «Фішка»: пункт, що вмикається клацанням (мови перекладу, фільтри).
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr
import Gmdr.Ui

B.AbstractButton {
    id: c
    property bool on: false
    property bool dim: false          // доступна, але з застереженням
    property string tip: ""
    checkable: false
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: Theme.controlHeight - Theme.px(4)
    implicitWidth: lbl.implicitWidth + Theme.s4 * 2
    Accessible.role: Accessible.CheckBox
    Accessible.checked: on
    Accessible.name: text
    background: Rectangle {
        radius: height / 2
        color: c.on ? Theme.accentSoft : c.hovered ? Theme.hover : "transparent"
        border.width: c.visualFocus ? 2 : 1
        border.color: c.visualFocus ? Theme.focus : c.on ? Theme.accent : Theme.border
    }
    contentItem: Text {
        id: lbl
        text: c.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        color: !c.enabled ? Theme.textMuted : c.on ? Theme.text : c.dim ? Theme.textMuted : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    B.ToolTip.visible: hovered && tip !== ""
    B.ToolTip.text: tip
    B.ToolTip.delay: 300
}
