// Смуга прогресу; indeterminate — невідомо скільки (рухома ділянка).
import QtQuick
import Gmdr.Ui

Rectangle {
    id: bar
    property real value: 0
    property bool indeterminate: false
    property color tone: Theme.accent
    implicitHeight: Theme.px(4)
    radius: height / 2
    color: Theme.surface3
    clip: true
    Rectangle {
        visible: !bar.indeterminate
        width: Math.max(0, Math.min(1, bar.value)) * parent.width
        height: parent.height
        radius: parent.radius
        color: bar.tone
        Behavior on width { NumberAnimation { duration: 200 } }
    }
    Rectangle {
        id: runner
        visible: bar.indeterminate
        width: parent.width * 0.3
        height: parent.height
        radius: parent.radius
        color: bar.tone
        NumberAnimation on x {
            running: bar.indeterminate && bar.visible
            from: -runner.width
            to: bar.width
            duration: 1200
            loops: Animation.Infinite
        }
    }
}
