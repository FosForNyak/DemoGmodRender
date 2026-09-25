// Верхня панель: логотип і меню, поточне демо, режим (стандартний / розширений), стан
// перевірки налаштувань, «Тест 3 с» і головна кнопка рендеру (з її станами).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    id: bar
    signal openDemo()
    signal startRender()
    signal startTest()
    signal showMenu(var item)
    implicitHeight: Theme.topBarHeight
    color: Theme.surface
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s3
        anchors.rightMargin: Theme.s3
        spacing: Theme.s3
        IconBtn {
            id: menuBtn
            iconName: "menu"
            tip: qsTr("Меню")
            onClicked: bar.showMenu(menuBtn)
        }
        Logo {
            Layout.preferredWidth: Theme.px(24)
            Layout.preferredHeight: Theme.px(24)
            accent: Theme.accent
        }
        Label {
            text: "GMod Demo Render"
            font.weight: Font.DemiBold
            visible: bar.width > Theme.px(1100)
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: Theme.px(20); color: Theme.border }
        // ---- поточне демо ----
        B.AbstractButton {
            id: demoBtn
            Layout.maximumWidth: Theme.px(520)
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight + Theme.px(4)
            hoverEnabled: true
            enabled: !Jobs.busy
            onClicked: bar.openDemo()
            Accessible.name: qsTr("Відкрити демо")
            background: Rectangle {
                radius: Theme.r2
                color: demoBtn.hovered ? Theme.hover : "transparent"
                border.width: demoBtn.visualFocus ? 2 : 0
                border.color: Theme.focus
            }
            contentItem: RowLayout {
                spacing: Theme.s2
                Icon {
                    Layout.preferredWidth: Theme.iconSize
                    Layout.preferredHeight: Theme.iconSize
                    name: "file"
                    color: Theme.textSecondary
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: Project.demoName !== "" ? Project.demoName : qsTr("Відкрити демо…")
                        font.weight: Project.demoName !== "" ? Font.DemiBold : Font.Normal
                        color: Project.demoName !== "" ? Theme.text : Theme.textSecondary
                    }
                    Label {
                        visible: Project.loaded || Project.loading
                        Layout.fillWidth: true
                        role: "meta"
                        text: Project.loading ? qsTr("Аналіз демо… %1 %").arg(Math.round(Project.loadProgress * 100))
                              : (Project.info.map || "") + " · " + Project.formatTime(Project.duration)
                                + (Project.wholeDemo ? "" : " · " + qsTr("фрагмент %1–%2").arg(Project.formatTime(Project.fragmentStart)).arg(Project.formatTime(Project.fragmentEnd)))
                    }
                }
                Label { text: "Ctrl+O"; role: "meta"; visible: demoBtn.hovered }
            }
        }
        Item { Layout.fillWidth: true }
        Segmented {
            options: [{ value: false, label: qsTr("Стандартний") }, { value: true, label: qsTr("Розширений") }]
            currentValue: Config.advanced
            onChosen: (v) => Config.advanced = v
            Accessible.name: qsTr("Режим інтерфейсу")
        }
        // ---- стан перевірки ----
        B.AbstractButton {
            id: checkBtn
            implicitHeight: Theme.controlHeight
            implicitWidth: checkRow.implicitWidth + Theme.s3 * 2
            hoverEnabled: true
            visible: Project.loaded
            onClicked: Ui.goTo("render")
            readonly property string sev: Config.errorCount > 0 ? "error" : Config.warningCount > 0 ? "warning" : "ok"
            Accessible.name: Config.summary
            background: Rectangle {
                radius: Theme.r2
                color: checkBtn.hovered ? Theme.hover : "transparent"
            }
            contentItem: RowLayout {
                id: checkRow
                spacing: Theme.s1
                Icon {
                    Layout.preferredWidth: Theme.iconSize
                    Layout.preferredHeight: Theme.iconSize
                    name: checkBtn.sev === "ok" ? "check" : Theme.severityIcon(checkBtn.sev)
                    color: checkBtn.sev === "ok" ? Theme.success : Theme.severityColor(checkBtn.sev)
                }
                Label {
                    text: checkBtn.sev === "ok" ? qsTr("Готово до рендеру")
                          : Config.errorCount > 0 ? qsTr("Помилок: %1").arg(Config.errorCount) : qsTr("Попереджень: %1").arg(Config.warningCount)
                    color: checkBtn.sev === "ok" ? Theme.textSecondary : Theme.severityColor(checkBtn.sev)
                }
            }
            B.ToolTip.visible: hovered
            B.ToolTip.text: Config.issues.filter(i => i.severity !== "info").map(i => "• " + i.message).join("\n") || qsTr("Налаштування перевірено — помилок немає")
        }
        Btn {
            // Гра відповідає, але зупинка «з збереженням» чекає її — тут закрити одразу
            visible: Jobs.busy && Jobs.kind !== "watch" && Jobs.kind !== "download" && Jobs.kind !== "serviceCheck"
            kind: "ghost"
            text: qsTr("Перервати")
            tip: qsTr("Негайно закрити гру і перервати рендер")
            onClicked: Jobs.kill()
        }
        Btn {
            visible: !Jobs.busy
            text: qsTr("Тест 3 с")
            iconName: "clock"
            enabled: Project.loaded && !Jobs.busy && Config.errorCount === 0
            tip: Config.errorCount > 0 ? qsTr("Спершу виправте помилки налаштувань")
                                       : qsTr("Тестовий прогін: 3 секунди з початку фрагмента, звіт по кроках і прогноз часу")
            onClicked: bar.startTest()
        }
        Btn {
            id: renderBtn
            kind: Jobs.busy ? "danger" : "primary"
            iconName: Jobs.busy ? "stop" : "render"
            enabled: Jobs.busy ? true : Project.loaded && Config.errorCount === 0
            text: Jobs.busy ? (Jobs.kind === "watch" ? qsTr("Закрити гру") : qsTr("Зупинити"))
                  : !Project.loaded ? qsTr("Почати рендер")
                  : Config.errorCount > 0 ? qsTr("Неможливо: помилок %1").arg(Config.errorCount)
                  : qsTr("Почати рендер")
            tip: Jobs.busy ? (Jobs.kind === "watch" ? qsTr("Закрити гру (позначки, зроблені в грі, вже збережено)") : qsTr("Зупинити і зберегти вже записане"))
                 : Config.errorCount > 0 ? qsTr("Налаштування не дозволяють рендер — див. «Рендер»") : ""
            onClicked: Jobs.busy ? Jobs.cancel() : bar.startRender()
        }
    }
}
