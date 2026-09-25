// «Журнал» (розширений режим): усе, що програма пише в gmdr_log.txt, — з фільтром за рівнем,
// пошуком і копіюванням для звіту про проблему.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Item {
    id: lp
    property string query: ""
    property bool onlyProblems: false

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s5
            spacing: Theme.s3
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("Журнал"); role: "title" }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Що робить програма: аналіз демо, запуск гри, кодування. Той самий текст — у файлі gmdr_log.txt.")
                    role: "secondary"
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
            }
            Btn { iconName: "copy"; text: qsTr("Копіювати журнал"); onClicked: { Shell.copyText(Log.allText()); Ui.toast(qsTr("Журнал скопійовано"), "success") } }
            Btn { kind: "ghost"; iconName: "external"; text: qsTr("Відкрити файл журналу"); onClicked: Shell.openLogFile() }
            Btn { kind: "ghost"; iconName: "trash"; text: qsTr("Очистити"); enabled: Log.count > 0; onClicked: Log.clear() }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s4
            spacing: Theme.s3
            Field {
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.px(420)
                placeholderText: qsTr("Пошук у журналі")
                onTextChanged: lp.query = text.toLowerCase()
                Accessible.name: placeholderText
            }
            Check { text: qsTr("Лише попередження й помилки"); checked: lp.onlyProblems; onToggled: lp.onlyProblems = checked }
            Check { text: qsTr("Налагоджувальні повідомлення"); checked: Log.showDebug; onToggled: Log.showDebug = checked }
            Item { Layout.fillWidth: true }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.s6
            Layout.topMargin: Theme.s3
            radius: Theme.r2
            color: Theme.surface
            border.color: Theme.border
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: Log
                boundsBehavior: Flickable.StopAtBounds
                B.ScrollBar.vertical: B.ScrollBar {}
                Accessible.name: qsTr("Журнал")
                // Нові рядки — у кінці; прокручуємо, лише якщо користувач і так унизу
                property bool follow: true
                onMovementEnded: follow = atYEnd
                onCountChanged: if (follow) Qt.callLater(positionViewAtEnd)
                Component.onCompleted: positionViewAtEnd()
                delegate: Item {
                    id: line
                    required property string time
                    required property string level
                    required property string text
                    readonly property bool shown: (!lp.onlyProblems || level === "warning" || level === "error")
                                                  && (lp.query === "" || text.toLowerCase().indexOf(lp.query) >= 0)
                    width: ListView.view.width
                    height: shown ? txt.implicitHeight + Theme.px(4) : 0
                    visible: shown
                    Rectangle {
                        anchors.fill: parent
                        visible: line.level === "error" || line.level === "warning"
                        color: Theme.tint(line.level === "error" ? Theme.error : Theme.warning, 0.07)
                    }
                    Row {
                        x: Theme.s3
                        y: Theme.px(2)
                        spacing: Theme.s3
                        Label { id: tm; text: line.time; role: "mono"; font.pixelSize: Theme.fontMeta; color: Theme.textMuted }
                        Label {
                            id: txt
                            width: line.width - tm.width - Theme.s3 * 3
                            text: line.text
                            role: "mono"
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WrapAnywhere
                            elide: Text.ElideNone
                            color: line.level === "error" ? Theme.error : line.level === "warning" ? Theme.warning
                                   : line.level === "debug" ? Theme.textMuted : Theme.text
                        }
                    }
                }
                EmptyState {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    iconName: "info"
                    title: qsTr("Журнал порожній")
                }
            }
        }
    }
}
