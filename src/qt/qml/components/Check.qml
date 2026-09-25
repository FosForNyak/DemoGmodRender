// Прапорець з підписом.
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr
import Gmdr.Ui

B.CheckBox {
    id: c
    implicitHeight: Theme.controlHeight
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    hoverEnabled: true
    spacing: Theme.s2
    indicator: Rectangle {
        implicitWidth: Theme.px(16)
        implicitHeight: Theme.px(16)
        x: c.leftPadding
        y: (c.height - height) / 2
        radius: Theme.r1
        color: c.checked ? Theme.accent : Theme.field
        border.width: c.checked ? 0 : 1
        border.color: c.visualFocus ? Theme.focus : c.hovered ? Theme.textSecondary : Theme.borderStrong
        Icon {
            anchors.centerIn: parent
            width: parent.width - 3
            height: width
            name: c.checked ? "check" : ""
            color: Theme.onAccent
        }
    }
    contentItem: Text {
        leftPadding: c.indicator.width + c.spacing
        text: c.text
        font: c.font
        color: c.enabled ? Theme.text : Theme.textMuted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
