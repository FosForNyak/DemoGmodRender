// Перемикач (так / ні) з підписом праворуч.
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr.Ui

B.Switch {
    id: s
    implicitHeight: Theme.controlHeight
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    hoverEnabled: true
    spacing: Theme.s3
    indicator: Rectangle {
        implicitWidth: Theme.px(32)
        implicitHeight: Theme.px(18)
        x: s.leftPadding
        y: (s.height - height) / 2
        radius: height / 2
        color: s.checked ? (s.enabled ? Theme.accent : Theme.tint(Theme.accent, 0.4)) : (s.hovered ? Theme.hover : Theme.surface3)
        border.width: s.checked ? 0 : 1
        border.color: s.visualFocus ? Theme.focus : Theme.borderStrong
        Rectangle {
            width: parent.height - Theme.px(6)
            height: width
            radius: width / 2
            y: Theme.px(3)
            x: s.checked ? parent.width - width - Theme.px(3) : Theme.px(3)
            color: s.checked ? Theme.onAccent : (s.enabled ? Theme.textSecondary : Theme.textMuted)
            Behavior on x { NumberAnimation { duration: Theme.animFast } }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.focus
            visible: s.visualFocus
        }
    }
    contentItem: Text {
        leftPadding: s.indicator.width + s.spacing
        text: s.text
        font: s.font
        color: s.enabled ? Theme.text : Theme.textMuted
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
