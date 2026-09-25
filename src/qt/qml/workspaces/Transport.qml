// Транспорт шкали: курсор (таймкод, можна ввести), до входу/виходу, вхід/вихід/позначка в
// курсорі, увесь запис, масштаб.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    id: tr
    property var timeline
    implicitHeight: Theme.px(40)
    color: Theme.surface
    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s3
        anchors.rightMargin: Theme.s3
        spacing: Theme.s1
        Field {
            id: tc
            Layout.preferredWidth: Theme.px(110)
            font.family: Theme.monoFamily
            horizontalAlignment: TextInput.AlignHCenter
            text: Project.formatTimecode(Project.playhead)
            onEditingFinished: {
                const t = Project.parseTime(text)
                if (t >= 0) Project.playhead = t
                text = Qt.binding(() => Project.formatTimecode(Project.playhead))
            }
            Accessible.name: qsTr("Курсор")
        }
        Item { Layout.preferredWidth: Theme.s2 }
        IconBtn { iconName: "goToIn"; tip: qsTr("До початку фрагмента (Shift+I)"); onClicked: Project.playhead = Project.fragmentStart }
        IconBtn { iconName: "goToOut"; tip: qsTr("До кінця фрагмента (Shift+O)"); onClicked: Project.playhead = Project.fragmentEnd }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: Theme.px(18); color: Theme.border }
        IconBtn { iconName: "markIn"; tip: qsTr("Початок фрагмента в курсорі (I)"); enabled: !Jobs.busy; onClicked: Project.markInAtPlayhead() }
        IconBtn { iconName: "markOut"; tip: qsTr("Кінець фрагмента в курсорі (O)"); enabled: !Jobs.busy; onClicked: Project.markOutAtPlayhead() }
        IconBtn { iconName: "marker"; tip: qsTr("Позначка в курсорі (M)"); onClicked: Project.addMarkerAtPlayhead() }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: Theme.px(18); color: Theme.border }
        Toggle {
            text: qsTr("Увесь запис")
            checked: Project.wholeDemo
            enabled: !Jobs.busy
            onToggled: {
                if (checked) Project.wholeDemo = true
                else Project.setFragment(Project.playhead, Project.duration)
            }
        }
        Label {
            Layout.leftMargin: Theme.s3
            visible: !Project.wholeDemo
            role: "secondary"
            text: Project.formatTime(Project.fragmentStart) + " – " + Project.formatTime(Project.fragmentEnd)
                  + " (" + Project.formatTime(Project.fragmentEnd - Project.fragmentStart) + ")"
        }
        Item { Layout.fillWidth: true }
        IconBtn { iconName: "eye"; tip: qsTr("Переглянути в грі з курсора"); enabled: !Jobs.busy; onClicked: Jobs.watchInGame(Project.playhead) }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: Theme.px(18); color: Theme.border }
        IconBtn { iconName: "minus"; tip: qsTr("Зменшити (−)"); onClicked: tr.timeline.zoomAt(Project.playhead, 1.5) }
        IconBtn { iconName: "plus"; tip: qsTr("Збільшити (+)"); onClicked: tr.timeline.zoomAt(Project.playhead, 0.66) }
        IconBtn { iconName: "scissors"; tip: qsTr("Показати фрагмент"); enabled: !Project.wholeDemo; onClicked: tr.timeline.showRange(Project.fragmentStart, Project.fragmentEnd) }
        IconBtn { iconName: "maximize"; tip: qsTr("Показати все"); onClicked: tr.timeline.showAll() }
    }
}
