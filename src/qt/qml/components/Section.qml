// Розділ налаштувань: заголовок (можна згорнути) і вміст стовпчиком. Без великих карток —
// тонка лінія зверху і відступ.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as B
import Gmdr
import Gmdr.Ui

ColumnLayout {
    id: sec
    property string title: ""
    property string subtitle: ""
    property string iconName: ""
    property bool collapsible: true
    property bool expanded: true
    property alias headerExtra: extra.data
    default property alias content: body.data
    spacing: 0
    Layout.fillWidth: true
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.border
    }
    B.AbstractButton {
        id: head
        Layout.fillWidth: true
        implicitHeight: Theme.rowHeight + Theme.s2
        enabled: sec.collapsible
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        onClicked: sec.expanded = !sec.expanded
        Accessible.name: sec.title
        background: Rectangle { color: head.hovered ? Theme.tint(Theme.hover, 0.5) : "transparent" }
        contentItem: RowLayout {
            spacing: Theme.s2
            Icon {
                visible: sec.collapsible
                Layout.preferredWidth: Theme.px(12)
                Layout.preferredHeight: Theme.px(12)
                name: sec.expanded ? "chevronDown" : "chevronRight"
                color: Theme.textSecondary
            }
            Icon {
                visible: sec.iconName !== ""
                Layout.preferredWidth: Theme.iconSize
                Layout.preferredHeight: Theme.iconSize
                name: sec.iconName
                color: Theme.textSecondary
            }
            Label {
                text: sec.title
                role: "section"
            }
            Label {
                Layout.fillWidth: true
                text: sec.subtitle
                role: "muted"
            }
            Row {
                id: extra
                spacing: Theme.s2
            }
        }
    }
    ColumnLayout {
        id: body
        visible: sec.expanded
        Layout.fillWidth: true
        Layout.leftMargin: Theme.s3
        Layout.bottomMargin: Theme.s4
        spacing: Theme.s2
    }
}
