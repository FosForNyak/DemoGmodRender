// «Черга»: кілька рендерів підряд — гра запускається один раз. Кожен пункт перевіряється
// тими самими правилами, що й рендер (помилки видно до запуску черги).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Item {
    id: qp

    function statusText(s) {
        return s === "running" ? qsTr("рендер...") : s === "succeeded" ? qsTr("✓ готово") : s === "failed" ? qsTr("✗ помилка")
             : s === "cancelled" ? qsTr("скасовано") : qsTr("чекає")
    }
    function statusColor(s) {
        return s === "running" ? Theme.accentText : s === "succeeded" ? Theme.success : s === "failed" ? Theme.error : Theme.textMuted
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // ---- заголовок і дії ----
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s5
            spacing: Theme.s3
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("Черга"); role: "title" }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Кілька рендерів підряд — гра запускається один раз.")
                    role: "secondary"
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    HoverHandler { id: qh }
                    B.ToolTip.visible: qh.hovered
                    B.ToolTip.text: qsTr("Якщо пункт не вдасться (чи гра впаде), черга піде далі — наступний пункт запустить гру заново. Розмір вікна гри, RTX і параметри запуску беруться з пункту; якщо вони інші, ніж у попереднього, гра перезапуститься. Готові пункти після черги зникають зі списку, невдалі лишаються.")
                }
            }
            Btn {
                iconName: "plus"
                text: qsTr("Додати поточний рендер")
                enabled: Project.loaded && !Queue.running
                tip: Project.loaded ? "" : qsTr("Спершу відкрийте демо")
                onClicked: if (Queue.addCurrent()) Ui.toast(qsTr("Додано до черги"), "success")
            }
            Btn {
                kind: "ghost"
                text: qsTr("Очистити")
                enabled: Queue.count > 0 && !Queue.running
                onClicked: clearDialog.open()
            }
        }
        // ---- запуск ----
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s4
            visible: Queue.count > 0
            spacing: Theme.s3
            Btn {
                kind: "primary"
                iconName: Queue.running ? "stop" : "play"
                text: Queue.running ? qsTr("Зупинити") : Ui.fmt(qsTr("Почати чергу ({})"), Queue.count)
                enabled: Queue.running || (!Jobs.busy && Queue.errorItems === 0)
                onClicked: Queue.running ? Jobs.cancel() : Jobs.startQueue()
            }
            Label { text: qsTr("Потім:"); role: "secondary" }
            Combo {
                Layout.preferredWidth: Theme.px(190)
                options: [{ value: "none", label: qsTr("нічого не робити") }, { value: "shutdown", label: qsTr("вимкнути ПК") }, { value: "sleep", label: qsTr("сон") }]
                selected: Jobs.afterDone
                onChosen: (v) => Jobs.afterDone = v
            }
            Label {
                Layout.fillWidth: true
                visible: Queue.errorItems > 0
                color: Theme.error
                wrapMode: Text.WordWrap
                text: Ui.fmt(qsTr("Пунктів з помилками: {} — виправте їх («Змінити») або приберіть з черги."), Queue.errorItems)
            }
            Item { Layout.fillWidth: Queue.errorItems === 0 }
        }
        JobProgress {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s3
            visible: Jobs.busy && Jobs.kind === "queue"
        }
        Banner {
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s3
            visible: Queue.editingIndex >= 0
            tone: "info"
            text: Ui.fmt(qsTr("Пункт {} відкрито для змін у робочому просторі «Рендер». Збережіть його там або скасуйте."), Queue.editingIndex + 1)
            actionText: qsTr("До «Рендеру»")
            onAction: Ui.goTo("render")
        }
        // ---- пункти ----
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.s6
            Layout.topMargin: Theme.s4
            visible: Queue.count > 0
            clip: true
            spacing: Theme.s2
            model: Queue
            boundsBehavior: Flickable.StopAtBounds
            B.ScrollBar.vertical: B.ScrollBar {}
            Accessible.name: qsTr("Черга")
            delegate: Rectangle {
                id: item
                required property int index
                required property string demoName
                required property string demo
                required property string output
                required property string renderer
                required property string resolution
                required property string fps
                required property string codec
                required property int parallel
                required property string fragment
                required property string status
                required property string error
                required property double seconds
                required property int errors
                required property int warnings
                required property var issues
                required property string summary
                property bool expanded: false
                width: ListView.view.width - Theme.s2
                implicitHeight: col.implicitHeight + Theme.s3 * 2
                radius: Theme.r2
                color: Queue.editingIndex === index ? Theme.accentSoft : Theme.surface
                border.color: errors > 0 ? Theme.tint(Theme.error, 0.5) : status === "running" ? Theme.accent : Theme.border
                ColumnLayout {
                    id: col
                    x: Theme.s3
                    y: Theme.s3
                    width: item.width - Theme.s3 * 2
                    spacing: Theme.s1
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s3
                        Label { text: String(item.index + 1); role: "section"; color: Theme.textMuted; Layout.preferredWidth: Theme.px(22) }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { Layout.fillWidth: true; text: item.demoName + " · " + item.fragment; font.weight: Font.DemiBold }
                            Label {
                                Layout.fillWidth: true
                                role: "meta"
                                text: [item.renderer, item.resolution, item.fps + " fps", item.codec, item.parallel > 1 ? Ui.fmt(qsTr("{} копії гри"), item.parallel) : ""].filter(x => x !== "").join(" · ")
                            }
                            Label {
                                Layout.fillWidth: true
                                role: "meta"
                                text: "→ " + item.output
                                elide: Text.ElideMiddle
                            }
                        }
                        ColumnLayout {
                            spacing: 0
                            Label { Layout.alignment: Qt.AlignRight; text: qp.statusText(item.status); color: qp.statusColor(item.status); font.weight: Font.Medium }
                            Label {
                                Layout.alignment: Qt.AlignRight
                                visible: item.seconds > 0
                                role: "meta"
                                text: Project.formatTime(item.seconds)
                            }
                        }
                        IconBtn { iconName: "chevronUp"; tip: qsTr("Вище"); enabled: item.index > 0 && !Queue.running; onClicked: Queue.move(item.index, item.index - 1) }
                        IconBtn { iconName: "chevronDown"; tip: qsTr("Нижче"); enabled: item.index < Queue.count - 1 && !Queue.running; onClicked: Queue.move(item.index, item.index + 1) }
                        IconBtn {
                            iconName: "edit"
                            tip: qsTr("Змінити: налаштування пункту — у робочий простір «Рендер»")
                            enabled: !Queue.running && !Jobs.busy
                            onClicked: { Queue.edit(item.index); Ui.goTo("render") }
                        }
                        IconBtn { iconName: "close"; tip: qsTr("Прибрати з черги"); enabled: !Queue.running; onClicked: Queue.remove(item.index) }
                    }
                    Label {
                        visible: item.error !== ""
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.px(22) + Theme.s3
                        color: Theme.error
                        wrapMode: Text.WordWrap
                        text: item.error
                    }
                    // Перевірка пункту
                    B.AbstractButton {
                        visible: item.errors > 0 || item.warnings > 0
                        Layout.leftMargin: Theme.px(22) + Theme.s3
                        hoverEnabled: true
                        onClicked: item.expanded = !item.expanded
                        Accessible.name: item.summary
                        contentItem: RowLayout {
                            spacing: Theme.s1
                            Icon {
                                Layout.preferredWidth: Theme.px(12)
                                Layout.preferredHeight: Theme.px(12)
                                name: item.errors > 0 ? "error" : "warning"
                                color: item.errors > 0 ? Theme.error : Theme.warning
                            }
                            Label { text: item.summary; role: "meta"; color: item.errors > 0 ? Theme.error : Theme.warning }
                            Icon {
                                Layout.preferredWidth: Theme.px(10)
                                Layout.preferredHeight: Theme.px(10)
                                name: item.expanded ? "chevronDown" : "chevronRight"
                                color: Theme.textMuted
                            }
                        }
                        background: Item {}
                    }
                    IssueList {
                        visible: item.expanded
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.px(22) + Theme.s3
                        showInfo: false
                        compactView: true
                        issues: item.expanded ? item.issues : []
                    }
                }
            }
        }
        // ---- порожньо ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: Queue.count === 0
            EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.s6 * 2, Theme.px(560))
                iconName: "queue"
                title: qsTr("Черга порожня")
                text: qsTr("Кілька фрагментів чи демо підряд — наприклад, на ніч. Гра запускається один раз: після кожного пункту вона не закривається, а одразу вмикає наступне демо.")
                      + "\n\n" + qsTr("Додати: налаштуйте демо, фрагмент і файл, як для звичайного рендеру, і натисніть «Додати до черги» в робочому просторі «Рендер». Ціле демо — правим кліком у «Бібліотеці».")
                actionText: Project.loaded ? qsTr("Додати поточний рендер") : ""
                actionIcon: "plus"
                onAction: if (Queue.addCurrent()) Ui.toast(qsTr("Додано до черги"), "success")
            }
        }
    }

    Dialog {
        id: clearDialog
        title: qsTr("Очистити чергу?")
        iconName: "warning"
        iconColor: Theme.warning
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.s4
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: Ui.fmt(qsTr("Буде прибрано всі пункти черги ({})."), Queue.count) }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.s2
                Btn { kind: "danger"; text: qsTr("Очистити"); onClicked: { Queue.clear(); clearDialog.close() } }
                Btn { text: qsTr("Скасувати"); onClicked: clearDialog.close() }
            }
        }
    }
}
