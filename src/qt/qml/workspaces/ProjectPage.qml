// «Проєкт»: над яким демо працюємо, що в ньому знайдено, що буде відрендерено і що
// відбувається зараз. Не загальна панель — лише це демо і цей рендер.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Page {
    id: page
    title: qsTr("Проєкт")
    subtitle: Project.loaded ? Project.demoPath : qsTr("Демо, що з нього вийде, і стан рендеру")
    actions: [
        Btn { text: qsTr("Відкрити демо…"); iconName: "folder"; enabled: !Jobs.busy; onClicked: Ui.openDemoRequested() },
        Btn { text: qsTr("До черги"); iconName: "queue"; visible: Project.loaded; enabled: Config.errorCount === 0; onClicked: { if (Queue.addCurrent()) Ui.toast(qsTr("Додано до черги"), "success") } }
    ]

    // ---- урваний рендер ----
    Rectangle {
        visible: Object.keys(Jobs.resumeOffer).length > 0 && !Jobs.busy
        Layout.fillWidth: true
        implicitHeight: resumeRow.implicitHeight + Theme.s3 * 2
        radius: Theme.r2
        color: Theme.tint(Theme.info, 0.1)
        border.color: Theme.tint(Theme.info, 0.4)
        RowLayout {
            id: resumeRow
            anchors.fill: parent
            anchors.margins: Theme.s3
            spacing: Theme.s3
            Icon { Layout.preferredWidth: Theme.iconSize; Layout.preferredHeight: Theme.iconSize; name: "refresh"; color: Theme.info }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("Знайдено урваний рендер — %1 % готово").arg(Math.round((Jobs.resumeOffer.fraction || 0) * 100)); font.weight: Font.Medium }
                Label { Layout.fillWidth: true; text: String(Jobs.resumeOffer.output || ""); role: "secondary"; elide: Text.ElideMiddle }
            }
            Btn { kind: "primary"; text: qsTr("Дописати"); onClicked: Jobs.resume() }
            Btn { kind: "ghost"; text: qsTr("Забути"); onClicked: Jobs.forgetResume() }
        }
    }

    // ---- немає демо ----
    EmptyState {
        visible: !Project.loaded && !Project.loading
        Layout.topMargin: Theme.s6 * 2
        iconName: "file"
        title: qsTr("Демо не відкрито")
        text: qsTr("Перетягніть файл .dem у вікно або відкрийте його з бібліотеки.")
        actionText: qsTr("Відкрити демо")
        actionIcon: "folder"
        onAction: Ui.openDemoRequested()
    }
    // Нещодавні демо з теки гри і ваших тек (огляд бібліотеки — коли сторінка порожня)
    Section {
        id: recentBox
        visible: !Project.loaded && !Project.loading
        Layout.fillWidth: true
        Layout.maximumWidth: Theme.px(720)
        Layout.alignment: Qt.AlignHCenter
        title: qsTr("Нещодавні демо")
        subtitle: qsTr("З теки гри і ваших тек — клацніть, щоб відкрити")
        iconName: "clock"
        collapsible: false
        readonly property var items: Library.scanned ? Library.recent(8) : []
        onVisibleChanged: if (visible && !Library.scanned && !Library.scanning) Library.rescan()
        Component.onCompleted: if (visible && !Library.scanned && !Library.scanning) Library.rescan()
        Label {
            visible: recentBox.items.length === 0
            role: "secondary"
            text: Library.scanning ? qsTr("Шукаю демо...") : qsTr("Демо не знайдено. Записати демо в грі: консоль → record назва, зупинити → stop.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Repeater {
            model: recentBox.items
            B.ItemDelegate {
                id: rd
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: Theme.rowHeight + Theme.px(6)
                hoverEnabled: true
                onClicked: Project.open(modelData.path)
                Accessible.name: modelData.name
                background: Rectangle { radius: Theme.r1; color: rd.hovered ? Theme.hover : "transparent" }
                contentItem: RowLayout {
                    spacing: Theme.s3
                    Icon { Layout.preferredWidth: Theme.px(14); Layout.preferredHeight: Theme.px(14); name: "file"; color: Theme.textSecondary }
                    Label { Layout.fillWidth: true; text: rd.modelData.name }
                    Label { text: rd.modelData.map; role: "secondary"; Layout.preferredWidth: Theme.px(150) }
                    Label { text: rd.modelData.seconds > 0 ? Project.formatTime(rd.modelData.seconds) : ""; role: "secondary"; font.family: Theme.monoFamily; Layout.preferredWidth: Theme.px(70) }
                    Label { text: Shell.formatDate(rd.modelData.modified); role: "meta"; Layout.preferredWidth: Theme.px(130) }
                }
            }
        }
        Btn {
            kind: "ghost"
            text: qsTr("Уся бібліотека")
            iconName: "library"
            onClicked: Ui.goTo("library")
        }
    }
    ColumnLayout {
        visible: Project.loading
        Layout.fillWidth: true
        Layout.topMargin: Theme.s6
        spacing: Theme.s3
        Label { text: qsTr("Аналіз демо…"); role: "section" }
        Bar { Layout.fillWidth: true; Layout.maximumWidth: Theme.px(480); value: Project.loadProgress }
    }

    // ---- що в демо ----
    GridLayout {
        visible: Project.loaded
        Layout.fillWidth: true
        columns: page.width > Theme.px(1100) ? 2 : 1
        columnSpacing: Theme.s6
        rowSpacing: Theme.s4
        Section {
            title: qsTr("Демо")
            iconName: "file"
            collapsible: false
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 1   // стовпці порівну, незалежно від вмісту
            KeyValue { name: qsTr("Карта"); value: Project.info.map || "—" }
            KeyValue { name: qsTr("Сервер"); value: Project.info.server || "—" }
            KeyValue { name: qsTr("Записав"); value: Project.info.recordedBy || "—" }
            KeyValue { name: qsTr("Режим гри"); value: Project.info.gamemode || "—"; visible: (Project.info.gamemode || "") !== "" }
            KeyValue { name: qsTr("Тривалість"); value: Project.formatTime(Project.duration) + " · " + qsTr("%1 тіків, %2 тік/с").arg(Project.info.ticks || 0).arg(Math.round(Project.info.tickrate || 0)) }
            KeyValue { name: qsTr("Гравці"); value: String(Project.info.players || 0) }
            KeyValue {
                name: qsTr("Голосовий чат")
                value: (Project.info.speakers || 0) > 0 ? qsTr("гравців з голосом: %1 (%2)").arg(Project.info.speakers).arg(Project.info.voiceCodec || "") : qsTr("немає")
            }
            KeyValue { name: qsTr("Чат"); value: qsTr("повідомлень: %1").arg(Project.info.chatMessages || 0) }
            KeyValue { name: qsTr("Позначки"); value: String(Project.markers.length) }
            KeyValue { name: qsTr("Протокол"); value: (Project.info.protocol || 0) + " / " + (Project.info.networkProtocol || 0); visible: Config.advanced }
            KeyValue { name: qsTr("Файл"); value: Shell.formatBytes(Project.info.size || 0); visible: Config.advanced }
            Repeater {
                model: Project.info.warnings || []
                IssueItem {
                    required property string modelData
                    Layout.fillWidth: true
                    issue: ({ severity: "warning", message: modelData })
                }
            }
        }
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: Theme.s4
            Section {
                title: qsTr("Фрагмент")
                iconName: "scissors"
                collapsible: false
                KeyValue {
                    name: qsTr("Відрізок")
                    value: Project.wholeDemo ? qsTr("увесь запис") : Project.formatTime(Project.fragmentStart) + " – " + Project.formatTime(Project.fragmentEnd)
                }
                KeyValue { name: qsTr("Тривалість відео"); value: Project.formatTime((Config.revision, Config.derived.videoSeconds)) }
                RowLayout {
                    spacing: Theme.s2
                    Btn { text: qsTr("Змінити на шкалі"); iconName: "edit"; onClicked: Ui.goTo("edit") }
                    Btn { kind: "ghost"; text: qsTr("Увесь запис"); visible: !Project.wholeDemo; onClicked: Project.wholeDemo = true }
                }
            }
            Section {
                title: qsTr("Що вийде")
                iconName: "render"
                collapsible: false
                RenderSummary { Layout.fillWidth: true }
                IssueList {
                    Layout.fillWidth: true
                    issues: (Config.revision, Config.issues)
                    showInfo: false
                    compactView: true
                }
                Btn { text: qsTr("Налаштування рендеру"); iconName: "chevronRight"; onClicked: Ui.goTo("render") }
            }
            Section {
                title: qsTr("Зараз")
                iconName: "clock"
                collapsible: false
                visible: Jobs.busy || Object.keys(Jobs.lastResult).length > 0
                JobProgress { Layout.fillWidth: true }
            }
        }
    }
}
