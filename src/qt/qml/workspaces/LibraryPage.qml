// «Бібліотека»: усі демо з теки гри і ваших тек — пошук, сортування за стовпцями,
// подвійний клік — відкрити, правий клік — до черги, показати в папці.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Item {
    id: lib
    property bool requested: false
    // Перший огляд — коли сторінку вперше відкрили (не під час запуску програми)
    onVisibleChanged: if (visible && !requested) { requested = true; Library.rescan() }
    Component.onCompleted: if (visible) { requested = true; Library.rescan() }

    readonly property real colMap: Theme.px(150)
    readonly property real colDur: Theme.px(90)
    readonly property real colSize: Theme.px(90)
    readonly property real colDate: Theme.px(140)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // ---- заголовок ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s5
            spacing: 2
            Label { text: qsTr("Бібліотека"); role: "title" }
            Label {
                Layout.fillWidth: true
                text: qsTr("Усі демо з теки гри і ваших тек. Подвійний клік — відкрити, правий клік — до черги.")
                role: "secondary"
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
        }
        // ---- панель ----
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s4
            spacing: Theme.s2
            Field {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.px(420)
                placeholderText: qsTr("пошук: назва, карта, сервер, гравець")
                text: Library.query
                onTextChanged: Library.query = text
                Accessible.name: qsTr("Пошук демо")
            }
            Check {
                text: qsTr("Сховати пошкоджені")
                checked: Library.hideBroken
                onToggled: Library.hideBroken = checked
            }
            Item { Layout.fillWidth: true }
            Label {
                role: "secondary"
                text: Library.scanning ? qsTr("Шукаю демо...")
                      : Library.count === Library.total ? Ui.fmt(qsTr("Демо: {}"), Library.total)
                      : Ui.fmt(qsTr("Демо: {} з {}"), Library.count, Library.total)
            }
            Btn { iconName: "plus"; text: qsTr("Додати теку..."); onClicked: folderDialog.open() }
            Btn { kind: "ghost"; iconName: "refresh"; text: qsTr("Оновити"); enabled: !Library.scanning; onClicked: Library.rescan() }
        }
        // ---- теки ----
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s2
            spacing: Theme.s2
            Label {
                height: Theme.controlHeight - Theme.px(4)
                verticalAlignment: Text.AlignVCenter
                role: "meta"
                text: Library.folders.length === 0 ? qsTr("(гру не знайдено — додайте теку з демо)") : qsTr("Теки:")
                HoverHandler { id: fh }
                B.ToolTip.visible: fh.hovered
                B.ToolTip.text: qsTr("Тека гри — верхній рівень garrysmod (туди пише консольна команда record) і garrysmod/demos з підтеками. Ваші теки переглядаються з підтеками. Подвійний клік — відкрити демо, правий клік — додати ціле демо до черги з поточними налаштуваннями.")
            }
            Each {
                items: Library.folders
                Chip {
                    required property string modelData
                    readonly property bool own: (Config.revision, String(Config.value("library_dirs") || "")).split(";").map(x => x.trim()).indexOf(modelData) >= 0
                    text: modelData + (own ? "  ×" : "")
                    on: false
                    tip: own ? qsTr("Прибрати теку з бібліотеки") : qsTr("Тека гри")
                    onClicked: own ? Library.removeFolder(modelData) : Shell.openPath(modelData)
                }
            }
        }
        // ---- таблиця ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.s6
            Layout.topMargin: Theme.s3
            radius: Theme.r2
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                // Заголовки стовпців — клік сортує
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.rowHeight
                    Layout.leftMargin: Theme.s3
                    Layout.rightMargin: Theme.s3
                    spacing: Theme.s3
                    SortHeader { Layout.fillWidth: true; text: qsTr("Демо"); key: "name" }
                    SortHeader { Layout.preferredWidth: lib.colMap; text: qsTr("Карта"); key: "map" }
                    SortHeader { Layout.preferredWidth: lib.colDur; text: qsTr("Тривалість"); key: "duration" }
                    SortHeader { Layout.preferredWidth: lib.colSize; text: qsTr("Розмір"); key: "size" }
                    SortHeader { Layout.preferredWidth: lib.colDate; text: qsTr("Дата"); key: "modified" }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Library
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    currentIndex: -1
                    B.ScrollBar.vertical: B.ScrollBar {}
                    Accessible.role: Accessible.Table
                    Accessible.name: qsTr("Демо")
                    Keys.onReturnPressed: if (currentIndex >= 0) Project.open(Library.pathAt(currentIndex))
                    Keys.onMenuPressed: if (currentItem) ctx.openFor(currentIndex, currentItem.path)
                    delegate: B.ItemDelegate {
                        id: row
                        required property int index
                        required property string name
                        required property string path
                        required property double size
                        required property double modified
                        required property string map
                        required property string server
                        required property string recordedBy
                        required property double seconds
                        required property string error
                        required property bool current
                        width: ListView.view.width
                        height: Theme.rowHeight + Theme.px(14)
                        highlighted: ListView.isCurrentItem
                        padding: 0
                        onClicked: list.currentIndex = index
                        onDoubleClicked: Project.open(path)
                        Accessible.name: name + ", " + map
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: { list.currentIndex = row.index; ctx.openFor(row.index, row.path) }
                        }
                        background: Rectangle {
                            color: row.highlighted ? Theme.accentSoft : row.hovered ? Theme.hover
                                   : row.index % 2 ? "transparent" : Theme.tint(Theme.surface2, 0.5)
                            Rectangle { visible: row.current; width: 3; height: parent.height; color: Theme.accent }
                        }
                        contentItem: RowLayout {
                            spacing: Theme.s3
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.s3
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: row.name
                                    font.weight: row.current ? Font.DemiBold : Font.Normal
                                    color: row.error !== "" ? Theme.error : Theme.text
                                }
                                Label {
                                    Layout.fillWidth: true
                                    role: "meta"
                                    color: row.error !== "" ? Theme.error : Theme.textMuted
                                    text: row.error !== "" ? row.error
                                          : [row.server, row.recordedBy !== "" ? Ui.fmt(qsTr("записав {}"), row.recordedBy) : ""].filter(x => x !== "").join(" · ")
                                }
                            }
                            Label { Layout.preferredWidth: lib.colMap; text: row.map; role: "secondary" }
                            Label { Layout.preferredWidth: lib.colDur; text: row.seconds > 0 ? Project.formatTime(row.seconds) : "—"; role: "secondary"; font.family: Theme.monoFamily }
                            Label { Layout.preferredWidth: lib.colSize; text: Shell.formatBytes(row.size); role: "secondary" }
                            Label { Layout.preferredWidth: lib.colDate; Layout.rightMargin: Theme.s3; text: Shell.formatDate(row.modified); role: "secondary" }
                        }
                    }
                    EmptyState {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        iconName: "library"
                        title: Library.scanning ? qsTr("Шукаю демо...")
                               : Library.total > 0 ? qsTr("Нічого не знайдено")
                               : qsTr("Демо не знайдено")
                        text: Library.scanning || Library.total > 0 ? ""
                              : qsTr("Додайте теку з демо або вкажіть теку гри в налаштуваннях.")
                        actionText: Library.scanning ? "" : Library.total > 0 ? qsTr("Скинути пошук") : qsTr("Додати теку...")
                        actionIcon: Library.total > 0 ? "" : "plus"
                        onAction: {
                            if (Library.total > 0) {
                                search.text = ""
                                Library.hideBroken = false
                            } else {
                                folderDialog.open()
                            }
                        }
                    }
                }
            }
        }
    }

    component SortHeader: B.AbstractButton {
        id: sh
        property string key: ""
        readonly property bool active: Library.sortKey === key
        implicitHeight: Theme.rowHeight
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        onClicked: {
            if (active) Library.sortDescending = !Library.sortDescending
            else Library.sortKey = key
        }
        Accessible.name: text
        background: Rectangle { color: sh.visualFocus ? Theme.hover : "transparent"; radius: Theme.r1 }
        contentItem: RowLayout {
            spacing: Theme.s1
            Label {
                text: sh.text
                role: "meta"
                font.weight: Font.DemiBold
                color: sh.active ? Theme.text : sh.hovered ? Theme.textSecondary : Theme.textMuted
            }
            Icon {
                visible: sh.active
                Layout.preferredWidth: Theme.px(10)
                Layout.preferredHeight: Theme.px(10)
                name: Library.sortDescending ? "chevronDown" : "chevronUp"
                color: Theme.textSecondary
            }
            Item { Layout.fillWidth: true }
        }
    }

    B.Menu {
        id: ctx
        property int row: -1
        property string path: ""
        function openFor(r, p) { row = r; path = p; popup() }
        B.MenuItem { text: qsTr("Відкрити"); onTriggered: Project.open(ctx.path) }
        B.MenuItem { text: qsTr("Додати до черги (усе демо)"); enabled: !Queue.running; onTriggered: if (Queue.addDemo(ctx.path)) Ui.toast(qsTr("Додано до черги"), "success") }
        B.MenuSeparator {}
        B.MenuItem { text: qsTr("Показати в папці"); onTriggered: Shell.showInFolder(ctx.path) }
        B.MenuItem { text: qsTr("Копіювати шлях"); onTriggered: Shell.copyText(ctx.path) }
    }

    FolderDialogWrap { id: folderDialog; title: qsTr("Тека з демо (разом із підтеками)"); onChosen: (p) => Library.addFolder(p) }
}
