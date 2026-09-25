// Кнопка-значок без рамки; checked — увімкнена (акцентна). tip — підказка (і назва для читачів екрана).
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr
import Gmdr.Ui

B.AbstractButton {
    id: b
    property string iconName: ""
    property string tip: ""
    property color iconColor: !enabled ? Theme.textMuted : checked ? Theme.accentText : hovered ? Theme.text : Theme.textSecondary
    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: tip
    background: Rectangle {
        radius: Theme.r2
        color: b.checked ? Theme.accentSoft : b.down ? Theme.pressed : b.hovered ? Theme.hover : "transparent"
        border.width: b.visualFocus ? 2 : 0
        border.color: Theme.focus
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            width: Theme.iconSize
            height: Theme.iconSize
            name: b.iconName
            color: b.iconColor
        }
    }
    B.ToolTip.visible: tip !== "" && hovered
    B.ToolTip.text: tip
    B.ToolTip.delay: 500
}
