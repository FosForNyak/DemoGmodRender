// «Монтаж»: монітор і контекстний інспектор угорі, транспорт, шкала часу з доріжками гравців і
// чату. Розміри областей змінюються перетягуванням меж. Клавіші: I / O — вхід і вихід фрагмента,
// M — позначка, Shift+I / Shift+O — до входу / виходу, Home / End, ← / →.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Item {
    id: page

    EmptyState {
        anchors.centerIn: parent
        visible: !Project.loaded
        iconName: "edit"
        title: Project.loading ? qsTr("Аналіз демо…") : qsTr("Демо не відкрито")
        text: qsTr("Шкала часу покаже голоси гравців, чат, фрагмент і позначки.")
        actionText: Project.loading ? "" : qsTr("Відкрити демо")
        actionIcon: "folder"
        onAction: Ui.openDemoRequested()
    }

    B.SplitView {
        anchors.fill: parent
        visible: Project.loaded
        orientation: Qt.Vertical
        handle: SplitHandle { vertical: true }

        // ---- монітор і інспектор ----
        B.SplitView {
            B.SplitView.preferredHeight: page.height * 0.52
            B.SplitView.minimumHeight: Theme.px(160)
            orientation: Qt.Horizontal
            handle: SplitHandle {}
            Rectangle {
                B.SplitView.fillWidth: true
                B.SplitView.minimumWidth: Theme.px(320)
                color: Theme.bg
                Monitor { anchors.fill: parent; anchors.margins: Theme.s3 }
            }
            Inspector {
                B.SplitView.preferredWidth: Theme.px(340)
                B.SplitView.minimumWidth: Theme.px(260)
            }
        }

        // ---- транспорт і шкала ----
        ColumnLayout {
            B.SplitView.fillHeight: true
            B.SplitView.minimumHeight: Theme.px(170)
            spacing: 0
            Transport {
                Layout.fillWidth: true
                timeline: tl
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                // ---- заголовки доріжок ----
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: Theme.px(210)
                    color: Theme.surface
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
                    Label {
                        x: Theme.s3
                        height: tl.rulerHeight
                        text: qsTr("Гравці · %1").arg(Project.players.length)
                        role: "meta"
                        font.weight: Font.DemiBold
                    }
                    ListView {
                        id: heads
                        anchors.fill: parent
                        anchors.topMargin: tl.rulerHeight
                        anchors.bottomMargin: tl.laneHeight * 0.6
                        clip: true
                        interactive: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: Project.players
                        contentY: tl.laneOffset
                        onContentYChanged: tl.laneOffset = contentY
                        delegate: TrackHeader {
                            required property var modelData
                            width: heads.width - 1
                            height: tl.laneHeight
                            player: modelData
                        }
                    }
                    Label {
                        anchors.bottom: parent.bottom
                        x: Theme.s3
                        height: tl.laneHeight * 0.6
                        text: qsTr("Чат")
                        role: "meta"
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    Timeline {
                        id: tl
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        laneHeight: Theme.px(26)
                        rulerHeight: Theme.px(26)
                        fontPixelSize: Theme.fontMeta
                        backgroundColor: Theme.bg
                        laneColor: Theme.surface
                        gridColor: Theme.border
                        textColor: Theme.text
                        dimTextColor: Theme.textSecondary
                        accentColor: Theme.accent
                        markerColor: Theme.warning
                        mutedColor: Theme.textMuted
                        interactive: !Jobs.busy
                        Accessible.name: qsTr("Шкала часу")
                        onRangeSelected: (a, b) => { Project.setFragment(a, b); Ui.select("fragment", 0) }
                        onMarkerActivated: (i) => Ui.select("marker", i)
                        onContextRequested: (t, x, y, marker) => { ctx.time = t; ctx.marker = marker; ctx.popup() }
                        Keys.onPressed: (e) => {
                            if (e.key === Qt.Key_Plus || e.key === Qt.Key_Equal) { zoomAt(Project.playhead, 0.7); e.accepted = true }
                            else if (e.key === Qt.Key_Minus) { zoomAt(Project.playhead, 1.4); e.accepted = true }
                        }
                        Connections {
                            target: Project
                            function onPlayheadChanged() { tl.ensureVisible(Project.playhead) }
                        }
                    }
                    Navigator {
                        Layout.fillWidth: true
                        timeline: tl
                    }
                }
            }
        }
    }

    B.Menu {
        id: ctx
        property real time: 0
        property int marker: -1
        B.MenuItem { text: qsTr("Курсор сюди"); onTriggered: Project.playhead = ctx.time }
        B.MenuItem { text: qsTr("Початок фрагмента тут") + "\tI"; enabled: !Jobs.busy; onTriggered: Project.setFragmentStart(ctx.time) }
        B.MenuItem { text: qsTr("Кінець фрагмента тут") + "\tO"; enabled: !Jobs.busy; onTriggered: Project.setFragmentEnd(ctx.time) }
        B.MenuItem { text: qsTr("Позначка тут") + "\tM"; onTriggered: Project.addMarker(ctx.time, "") }
        B.MenuSeparator {}
        B.MenuItem { text: qsTr("Перейменувати позначку…"); visible: ctx.marker >= 0; height: visible ? implicitHeight : 0; onTriggered: Ui.select("marker", ctx.marker) }
        B.MenuItem { text: qsTr("Видалити позначку"); visible: ctx.marker >= 0; height: visible ? implicitHeight : 0; onTriggered: Project.removeMarker(ctx.marker) }
        B.MenuItem { text: qsTr("Увесь запис"); enabled: !Project.wholeDemo && !Jobs.busy; onTriggered: Project.wholeDemo = true }
        B.MenuItem { text: qsTr("Показати фрагмент"); enabled: !Project.wholeDemo; onTriggered: tl.showRange(Project.fragmentStart, Project.fragmentEnd) }
        B.MenuItem { text: qsTr("Показати все"); onTriggered: tl.showAll() }
        B.MenuSeparator {}
        B.MenuItem { text: qsTr("Переглянути в грі звідси"); enabled: !Jobs.busy; onTriggered: Jobs.watchInGame(ctx.time) }
    }
}
