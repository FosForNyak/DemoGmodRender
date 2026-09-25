// Інспектор праворуч від монітора: властивості вибраного (демо / фрагмент / позначка / гравець),
// список позначок, чат і розпізнане мовлення.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    id: insp
    color: Theme.surface
    Rectangle { width: 1; height: parent.height; color: Theme.border }
    property int tab: 0
    function badTime(t) { if (String(t).trim() !== "") Ui.toast(Ui.fmt(qsTr("Не розумію час «{}». Приклади: 95.5 — секунди, 1:35 — хв:с, 1:02:03 — год:хв:с"), t), "warning") }
    Connections {
        target: Ui
        function onSelectionChanged() { if (Ui.selection !== "") insp.tab = 0 }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0
        Segmented {
            Layout.margins: Theme.s3
            options: [{ value: 0, label: qsTr("Властивості") }, { value: 1, label: qsTr("Позначки") }, { value: 2, label: qsTr("Чат") }]
            currentValue: insp.tab
            onChosen: (v) => insp.tab = v
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: insp.tab
            // ---- властивості ----
            B.ScrollView {
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: insp.width - Theme.s3 * 2 - 1
                    x: Theme.s3
                    spacing: Theme.s3
                    // Позначка
                    ColumnLayout {
                        visible: Ui.selection === "marker" && Ui.selectedMarker >= 0 && Ui.selectedMarker < Project.markers.length
                        Layout.fillWidth: true
                        spacing: Theme.s2
                        readonly property var m: visible ? Project.markers[Ui.selectedMarker] : ({})
                        Label { text: qsTr("Позначка %1").arg(Ui.selectedMarker + 1); role: "section" }
                        Label { text: qsTr("Назва"); role: "secondary" }
                        Field {
                            Layout.fillWidth: true
                            text: parent.m.title || ""
                            placeholderText: qsTr("без назви (розділ «Позначка N»)")
                            onEditingFinished: Project.renameMarker(Ui.selectedMarker, text)
                        }
                        Label { text: qsTr("Час"); role: "secondary" }
                        Field {
                            Layout.fillWidth: true
                            font.family: Theme.monoFamily
                            text: Project.formatTimecode(parent.m.time || 0)
                            onEditingFinished: { const t = Project.parseTime(text); if (t >= 0) Project.moveMarker(Ui.selectedMarker, t); else insp.badTime(text) }
                        }
                        RowLayout {
                            spacing: Theme.s2
                            Btn { text: qsTr("Курсор сюди"); onClicked: Project.playhead = parent.parent.m.time }
                            Btn { kind: "danger"; text: qsTr("Видалити"); onClicked: { Project.removeMarker(Ui.selectedMarker); Ui.select("", 0) } }
                        }
                        Label {
                            Layout.fillWidth: true
                            role: "meta"
                            wrapMode: Text.WordWrap
                            text: qsTr("Позначки у фрагменті стають розділами у MP4, MOV і MKV.")
                        }
                    }
                    // Гравець
                    ColumnLayout {
                        id: playerBox
                        readonly property var p: Project.players.find(x => x.key === Ui.selectedPlayer) || null
                        visible: Ui.selection === "player" && p !== null
                        Layout.fillWidth: true
                        spacing: Theme.s2
                        Label { text: playerBox.p ? playerBox.p.name : ""; role: "section" }
                        Label {
                            Layout.fillWidth: true
                            role: "meta"
                            text: playerBox.p ? (playerBox.p.steamid !== "" ? "SteamID64 " + playerBox.p.steamid : playerBox.p.key) + (playerBox.p.local ? " · " + qsTr("той, хто записував") : "") : ""
                        }
                        KeyValue { name: qsTr("Говорив"); value: playerBox.p ? Project.formatTime(playerBox.p.seconds) + " · " + qsTr("фраз: %1").arg(playerBox.p.segments) : "" }
                        KeyValue {
                            visible: playerBox.p && playerBox.p.lost > 0.02
                            name: qsTr("Втрачено кадрів")
                            value: playerBox.p ? Math.round(playerBox.p.lost * 100) + " %" : ""
                            valueColor: Theme.warning
                        }
                        Label { text: qsTr("Гучність"); role: "secondary" }
                        Slide {
                            Layout.fillWidth: true
                            from: 0
                            to: 4
                            stepSize: 0.05
                            decimals: 2
                            value: playerBox.p ? playerBox.p.volume : 1
                            enabled: !Jobs.busy
                            onMoved: (v) => Project.setPlayerVolume(playerBox.p.key, v)
                        }
                        Toggle {
                            text: qsTr("Вимкнути (M)")
                            checked: playerBox.p ? playerBox.p.muted : false
                            enabled: !Jobs.busy
                            onToggled: Project.togglePlayerMute(playerBox.p.key)
                        }
                        Toggle {
                            text: qsTr("Соло — лише цей гравець (S)")
                            checked: playerBox.p ? playerBox.p.solo : false
                            enabled: !Jobs.busy
                            onToggled: Project.togglePlayerSolo(playerBox.p.key)
                        }
                        Toggle {
                            text: qsTr("Шумодав для цього гравця")
                            checked: playerBox.p ? playerBox.p.denoise : false
                            enabled: !Jobs.busy
                            onToggled: Project.togglePlayerDenoise(playerBox.p.key)
                        }
                        Btn {
                            iconName: "headphones"
                            text: Project.listeningKey === (playerBox.p ? playerBox.p.key : "") ? qsTr("Зупинити") : qsTr("Прослухати")
                            enabled: Project.canListen
                            onClicked: Project.listeningKey === playerBox.p.key ? Project.stopListening() : Project.listen(playerBox.p.key)
                        }
                    }
                    // Демо і фрагмент (нічого не вибрано або фрагмент)
                    ColumnLayout {
                        visible: Ui.selection === "" || Ui.selection === "fragment"
                            || (Ui.selection === "marker" && Ui.selectedMarker >= Project.markers.length)
                            || (Ui.selection === "player" && playerBox.p === null)
                        Layout.fillWidth: true
                        spacing: Theme.s2
                        Label { text: qsTr("Фрагмент"); role: "section" }
                        GridLayout {
                            columns: 2
                            columnSpacing: Theme.s2
                            rowSpacing: Theme.s2
                            Layout.fillWidth: true
                            Label { text: qsTr("Початок"); role: "secondary" }
                            Field {
                                Layout.fillWidth: true
                                font.family: Theme.monoFamily
                                enabled: !Jobs.busy
                                text: Project.formatTimecode(Project.fragmentStart)
                                onEditingFinished: { const t = Project.parseTime(text); if (t >= 0) Project.setFragmentStart(t); else insp.badTime(text); text = Qt.binding(() => Project.formatTimecode(Project.fragmentStart)) }
                            }
                            Label { text: qsTr("Кінець"); role: "secondary" }
                            Field {
                                Layout.fillWidth: true
                                font.family: Theme.monoFamily
                                enabled: !Jobs.busy
                                text: Project.formatTimecode(Project.fragmentEnd)
                                onEditingFinished: { const t = Project.parseTime(text); if (t >= 0) Project.setFragmentEnd(t); else insp.badTime(text); text = Qt.binding(() => Project.formatTimecode(Project.fragmentEnd)) }
                            }
                            Label { text: qsTr("Тривалість"); role: "secondary" }
                            Label { text: Project.formatTime(Project.fragmentEnd - Project.fragmentStart) }
                        }
                        Btn { text: qsTr("Увесь запис"); visible: !Project.wholeDemo; enabled: !Jobs.busy; onClicked: Project.wholeDemo = true }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                        Label { text: qsTr("Демо"); role: "section" }
                        KeyValue { name: qsTr("Карта"); value: Project.info.map || "" }
                        KeyValue { name: qsTr("Тривалість"); value: Project.formatTime(Project.duration) }
                        KeyValue { name: qsTr("З голосом"); value: String(Project.players.length) }
                        Label {
                            Layout.fillWidth: true
                            role: "meta"
                            wrapMode: Text.WordWrap
                            text: qsTr("Протягніть по доріжках, щоб вибрати фрагмент; тягніть його краї або позначки; коліщатко — масштаб, Shift+коліщатко — прокрутка.")
                        }
                    }
                }
            }
            // ---- позначки ----
            ColumnLayout {
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.s3
                    spacing: Theme.s2
                    Btn { iconName: "plus"; text: qsTr("У курсорі (M)"); onClicked: Project.addMarkerAtPlayhead() }
                    Item { Layout.fillWidth: true }
                    Btn { kind: "ghost"; text: qsTr("Прибрати всі"); enabled: Project.markers.length > 0; onClicked: Project.clearMarkers() }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Project.markers
                    B.ScrollBar.vertical: B.ScrollBar {}
                    delegate: B.ItemDelegate {
                        id: md
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: Theme.rowHeight
                        highlighted: Ui.selection === "marker" && Ui.selectedMarker === index
                        onClicked: { Ui.select("marker", index); Project.playhead = modelData.time }
                        background: Rectangle { color: md.highlighted ? Theme.accentSoft : md.hovered ? Theme.hover : "transparent" }
                        contentItem: RowLayout {
                            spacing: Theme.s2
                            Icon { Layout.preferredWidth: Theme.px(12); Layout.preferredHeight: Theme.px(12); name: "marker"; color: Theme.warning }
                            Label { text: Project.formatTime(md.modelData.time); font.family: Theme.monoFamily; role: "secondary" }
                            Label { Layout.fillWidth: true; text: md.modelData.title !== "" ? md.modelData.title : qsTr("Позначка %1").arg(md.index + 1) }
                            IconBtn { iconName: "close"; tip: qsTr("Видалити"); implicitWidth: Theme.px(22); implicitHeight: Theme.px(22); onClicked: Project.removeMarker(md.index) }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: Project.markers.length === 0
                        text: qsTr("Позначок немає — M у курсорі")
                        role: "secondary"
                    }
                }
            }
            // ---- чат і мовлення ----
            ChatPanel {}
        }
    }
}
