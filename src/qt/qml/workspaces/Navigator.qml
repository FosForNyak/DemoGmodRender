// Огляд усього демо під шкалою: видима ділянка — рамка, яку можна тягнути; фрагмент і позначки.
import QtQuick
import Gmdr
import Gmdr.Ui

Rectangle {
    id: nav
    property var timeline
    implicitHeight: Theme.px(16)
    color: Theme.surface
    readonly property real dur: Math.max(0.001, Project.duration)
    function xOf(t) { return t / dur * width }
    Rectangle {   // фрагмент
        visible: !Project.wholeDemo
        x: nav.xOf(Project.fragmentStart)
        width: Math.max(2, nav.xOf(Project.fragmentEnd) - x)
        height: parent.height
        color: Theme.tint(Theme.accent, 0.25)
    }
    Repeater {
        model: Project.markers
        Rectangle {
            required property var modelData
            x: nav.xOf(modelData.time)
            width: 1
            height: parent.height
            color: Theme.warning
        }
    }
    Rectangle {   // курсор
        x: nav.xOf(Project.playhead)
        width: 1
        height: parent.height
        color: Theme.accent
    }
    Rectangle {   // видима ділянка
        id: win
        x: nav.xOf(nav.timeline ? nav.timeline.viewStart : 0)
        width: Math.max(Theme.px(8), nav.xOf(nav.timeline ? nav.timeline.viewEnd : nav.dur) - x)
        height: parent.height
        color: "transparent"
        radius: Theme.r1
        border.color: drag.containsMouse || drag.pressed ? Theme.accent : Theme.textSecondary
        border.width: 1
    }
    MouseArea {
        id: drag
        anchors.fill: parent
        hoverEnabled: true
        property real grab: 0
        onPressed: (m) => {
            const t = m.x / nav.width * nav.dur
            const span = nav.timeline.viewEnd - nav.timeline.viewStart
            if (m.x < win.x || m.x > win.x + win.width) nav.timeline.viewStart = t - span / 2
            grab = m.x / nav.width * nav.dur - nav.timeline.viewStart
        }
        onPositionChanged: (m) => { if (pressed) nav.timeline.viewStart = m.x / nav.width * nav.dur - grab }
        onDoubleClicked: nav.timeline.showAll()
    }
}
