import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Dialog {
    id: ad
    title: "GMod Demo Render " + Env.system.app
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s3
        Logo { Layout.preferredWidth: Theme.px(48); Layout.preferredHeight: Theme.px(48); accent: Theme.accent }
        Label {
            Layout.fillWidth: true
            text: qsTr("Рендер демо Garry's Mod у відео: гра рендерить кадри, програма кодує відео, міксує звук і голоси гравців.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Label { text: "Qt " + Env.system.qt + " · " + Env.system.ffmpeg; role: "secondary" }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.s2
            Btn { text: "GitHub"; iconName: "link"; onClicked: Qt.openUrlExternally("https://github.com/FosForNyak/DemoGmodRender") }
            Btn { text: qsTr("Закрити"); onClicked: ad.close() }
        }
    }
}
