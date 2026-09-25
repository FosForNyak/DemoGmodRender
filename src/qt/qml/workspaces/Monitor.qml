// Монітор: під час рендеру — кадри, що йдуть у кодер; інакше — курсор великим таймкодом,
// фрагмент і що буде в кадрі.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    id: mon
    color: "#000000"
    radius: Theme.r2
    readonly property bool live: Jobs.busy && (Jobs.kind === "render" || Jobs.kind === "test" || Jobs.kind === "queue" || Jobs.kind === "resume")
    readonly property var d: (Config.revision, Config.derived)
    // Кадр потрібних пропорцій усередині
    Rectangle {
        id: frame
        readonly property real aspect: mon.d.width > 0 && mon.d.height > 0 ? mon.d.width / mon.d.height : 16 / 9
        width: Math.min(parent.width - Theme.s4 * 2, (parent.height - Theme.s4 * 2) * aspect)
        height: width / aspect
        anchors.centerIn: parent
        color: "#0B0C0E"
        border.color: "#2A2D32"
        Preview {
            anchors.fill: parent
            anchors.margins: 1
            visible: mon.live || hasFrame
            background: "#000000"
        }
        ColumnLayout {
            anchors.centerIn: parent
            visible: !mon.live
            spacing: Theme.s2
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: Project.formatTimecode(Project.playhead)
                color: "#F1F3F5"
                font.pixelSize: Theme.px(34)
                font.family: Theme.monoFamily
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                color: "#A6ACB4"
                text: Project.wholeDemo ? qsTr("Увесь запис · %1").arg(Project.formatTime(Project.duration))
                                        : qsTr("Фрагмент %1 – %2").arg(Project.formatTime(Project.fragmentStart)).arg(Project.formatTime(Project.fragmentEnd))
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: frame.width - Theme.s6
                color: "#747B84"
                role: "meta"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                text: qsTr("Кадри з'являться тут під час рендеру або тестового прогону. Подивитись демо з курсора можна в самій грі.")
            }
            Btn {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Переглянути в грі з курсора")
                iconName: "eye"
                enabled: !Jobs.busy
                onClicked: Jobs.watchInGame(Project.playhead)
            }
        }
    }
    // Рядок стану рендеру поверх кадру
    Rectangle {
        visible: mon.live
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.px(26)
        color: Qt.rgba(0, 0, 0, 0.6)
        radius: mon.radius
        Label {
            anchors.fill: parent
            anchors.leftMargin: Theme.s3
            color: "#F1F3F5"
            role: "meta"
            text: Jobs.phaseLabel + " · " + Math.round((Jobs.progress.fraction || 0) * 100) + " % · " + qsTr("кадрів %1").arg(Jobs.progress.frames || 0)
        }
    }
}
