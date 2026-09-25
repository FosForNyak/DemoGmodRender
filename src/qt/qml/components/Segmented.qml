// Сегментований перемикач: кілька взаємовиключних варіантів поруч. options — [{value, label, available, reason}]
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr.Ui

Item {
    id: seg
    property var options: []
    property var currentValue
    signal chosen(var value)
    implicitHeight: Theme.controlHeight
    implicitWidth: row.implicitWidth + 4
    Rectangle {
        width: row.implicitWidth + 4
        height: parent.height
        radius: Theme.r2
        color: Theme.surface2
        border.color: Theme.border
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: seg.options
                B.AbstractButton {
                    id: sb
                    required property var modelData
                    readonly property bool on: String(modelData.value) === String(seg.currentValue)
                    enabled: seg.enabled && modelData.available !== false
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    implicitHeight: seg.height - 4
                    implicitWidth: lbl.implicitWidth + Theme.s4 * 2
                    onClicked: seg.chosen(modelData.value)
                    Accessible.name: modelData.label
                    background: Rectangle {
                        radius: Theme.r2 - 1
                        color: sb.on ? Theme.surface : sb.hovered ? Theme.hover : "transparent"
                        border.width: sb.on || sb.visualFocus ? 1 : 0
                        border.color: sb.visualFocus ? Theme.focus : Theme.border
                    }
                    contentItem: Text {
                        id: lbl
                        text: sb.modelData.label
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.weight: sb.on ? Font.DemiBold : Font.Normal
                        color: !sb.enabled ? Theme.textMuted : sb.on ? Theme.text : Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    B.ToolTip.visible: hovered && (modelData.reason || "") !== ""
                    B.ToolTip.text: modelData.reason || ""
                }
            }
        }
    }
}
