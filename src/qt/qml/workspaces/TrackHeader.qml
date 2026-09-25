// Заголовок доріжки гравця: ім'я, прослухати, M (вимкнути), S (соло). Клік — гравець в інспекторі.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    id: th
    property var player: ({})
    readonly property bool selected: Ui.selection === "player" && Ui.selectedPlayer === player.key
    color: selected ? Theme.accentSoft : ma.containsMouse ? Theme.hover : "transparent"
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: Ui.select("player", th.player.key)
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s3
        anchors.rightMargin: Theme.s1
        spacing: 2
        Rectangle {
            Layout.preferredWidth: Theme.px(4)
            Layout.preferredHeight: parent.height * 0.6
            radius: 2
            color: th.player.muted ? Theme.textMuted : ["#6287D1", "#23AA96", "#BE82D2", "#D69646", "#5AAA5A", "#D26478", "#78A0DC", "#B4B45A"][th.player.index % 8]
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s2
            text: th.player.name + (th.player.local ? " · " + qsTr("ви") : "")
            color: th.player.muted ? Theme.textMuted : Theme.text
            font.pixelSize: Theme.fontSmall
        }
        IconBtn {
            implicitWidth: Theme.px(22)
            implicitHeight: Theme.px(22)
            iconName: "headphones"
            checked: Project.listeningKey === th.player.key
            enabled: Project.canListen && (Project.preparingKey === "" || checked)
            tip: checked ? qsTr("Зупинити прослуховування") : qsTr("Прослухати (15 с з початку фрагмента)")
            onClicked: checked ? Project.stopListening() : Project.listen(th.player.key)
        }
        LetterToggle {
            letter: "M"
            on: th.player.muted
            onColor: Theme.success
            tip: qsTr("Вимкнути цього гравця")
            enabled: !Jobs.busy
            onClicked: Project.togglePlayerMute(th.player.key)
        }
        LetterToggle {
            letter: "S"
            on: th.player.solo
            onColor: Theme.warning
            tip: qsTr("Лише цей гравець (соло)")
            enabled: !Jobs.busy
            onClicked: Project.togglePlayerSolo(th.player.key)
        }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.tint(Theme.border, 0.6) }
}
