// Значок стану біля поля: помилка / попередження / інформація, з підказкою (не лише колір).
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr
import Gmdr.Ui

Item {
    id: vb
    property string severity: ""
    property var messages: []
    visible: severity !== ""
    implicitWidth: Theme.iconSize + 4
    implicitHeight: Theme.iconSize + 4
    Accessible.name: messages.join("; ")
    Icon {
        anchors.centerIn: parent
        width: Theme.iconSize
        height: Theme.iconSize
        name: Theme.severityIcon(vb.severity)
        color: Theme.severityColor(vb.severity)
    }
    HoverHandler { id: hh }
    B.ToolTip.visible: hh.hovered && messages.length > 0
    B.ToolTip.text: messages.join("\n")
}
