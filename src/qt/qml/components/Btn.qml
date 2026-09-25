// Кнопка: kind — primary (акцент), secondary, ghost, danger. icon — назва значка (Icon).
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr
import Gmdr.Ui

B.Button {
    id: b
    property string kind: "secondary"
    property string iconName: ""
    property string tip: ""
    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(Theme.controlHeight, row.implicitWidth + Theme.s4 * 2)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    font.weight: kind === "primary" ? Font.DemiBold : Font.Normal
    readonly property color fg: !enabled ? Theme.textMuted
                                : kind === "primary" ? Theme.onAccent
                                : kind === "danger" ? Theme.error
                                : b.checked ? Theme.accentText : Theme.text
    background: Rectangle {
        radius: Theme.r2
        color: !b.enabled ? (b.kind === "primary" ? Theme.surface3 : "transparent")
               : b.kind === "primary" ? (b.down ? Theme.accentPressed : b.hovered ? Theme.accentHover : Theme.accent)
               : b.checked ? Theme.accentSoft
               : b.down ? Theme.pressed : b.hovered ? Theme.hover
               : b.kind === "ghost" ? "transparent" : Theme.surface2
        border.width: b.kind === "secondary" || b.kind === "danger" ? 1 : 0
        border.color: b.kind === "danger" ? Theme.tint(Theme.error, 0.5) : Theme.border
        Rectangle {   // рамка фокуса з клавіатури
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.width: 2
            border.color: Theme.focus
            visible: b.visualFocus
        }
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.s2
            Icon {
                visible: b.iconName !== ""
                name: b.iconName
                color: b.fg
                width: Theme.iconSize
                height: Theme.iconSize
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: b.text !== ""
                text: b.text
                color: b.fg
                font: b.font
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    B.ToolTip.visible: tip !== "" && hovered
    B.ToolTip.text: tip
    B.ToolTip.delay: 500
}
