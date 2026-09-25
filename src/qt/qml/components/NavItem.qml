// Пункт бічної навігації: значок, назва, лічильник (зауваження, черга), гаряча клавіша.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

B.AbstractButton {
    id: n
    property string iconName: ""
    property string label: ""
    property bool current: false
    property bool collapsed: false
    property string badge: ""
    property string badgeSeverity: ""
    property string shortcut: ""
    implicitHeight: Theme.rowHeight + Theme.s1
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: label
    background: Rectangle {
        radius: Theme.r2
        color: n.current ? Theme.accentSoft : n.down ? Theme.pressed : n.hovered ? Theme.hover : "transparent"
        border.width: n.visualFocus ? 2 : 0
        border.color: Theme.focus
        Rectangle {   // позначка поточного пункту
            visible: n.current
            width: Theme.px(3)
            height: parent.height * 0.55
            radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            x: 1
            color: Theme.accent
        }
    }
    contentItem: RowLayout {
        spacing: Theme.s3
        Item { Layout.preferredWidth: Theme.s1 }
        Icon {
            Layout.preferredWidth: Theme.iconSize + 2
            Layout.preferredHeight: Theme.iconSize + 2
            name: n.iconName
            color: n.current ? Theme.accentText : n.hovered ? Theme.text : Theme.textSecondary
        }
        Label {
            visible: !n.collapsed
            Layout.fillWidth: true
            text: n.label
            color: n.current ? Theme.text : Theme.textSecondary
            font.weight: n.current ? Font.DemiBold : Font.Normal
        }
        Rectangle {
            visible: n.badge !== "" && !n.collapsed
            Layout.preferredHeight: Theme.px(16)
            Layout.preferredWidth: Math.max(height, bt.implicitWidth + Theme.s2 * 2)
            radius: height / 2
            color: n.badgeSeverity !== "" ? Theme.tint(Theme.severityColor(n.badgeSeverity), 0.2) : Theme.surface3
            Label {
                id: bt
                anchors.centerIn: parent
                text: n.badge
                role: "meta"
                color: n.badgeSeverity !== "" ? Theme.severityColor(n.badgeSeverity) : Theme.textSecondary
            }
        }
        Item { Layout.preferredWidth: Theme.s1 }
    }
    B.ToolTip.visible: collapsed && hovered
    B.ToolTip.text: label + (shortcut !== "" ? "  (" + shortcut + ")" : "")
}
