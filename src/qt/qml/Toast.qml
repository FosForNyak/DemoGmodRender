// Коротке повідомлення внизу праворуч (без модальних вікон для дрібниць).
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    id: toast
    property string severity: "info"
    function show(text, sev) {
        msg.text = text
        severity = sev || "info"
        opacity = 1
        timer.restart()
    }
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.margins: Theme.s5
    z: 1000
    width: Math.min(row.implicitWidth + Theme.s4 * 2, Theme.px(460))
    height: row.implicitHeight + Theme.s3 * 2
    radius: Theme.r2
    color: Theme.surface3
    border.color: Theme.tint(Theme.severityColor(severity === "success" ? "info" : severity), 0.5)
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }
    Timer { id: timer; interval: 3500; onTriggered: toast.opacity = 0 }
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.s3
        spacing: Theme.s2
        Icon {
            Layout.preferredWidth: Theme.iconSize
            Layout.preferredHeight: Theme.iconSize
            name: toast.severity === "success" ? "check" : Theme.severityIcon(toast.severity)
            color: toast.severity === "success" ? Theme.success : Theme.severityColor(toast.severity)
        }
        Label {
            id: msg
            Layout.fillWidth: true
            Layout.maximumWidth: Theme.px(400)
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
    }
}
