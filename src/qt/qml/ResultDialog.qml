// Підсумок завдання: готово (відкрити файл / теку), тестовий прогін (звіт і «Почати рендер»),
// помилка налаштувань (зауваження з виправленнями) чи помилка під час роботи (кроки перевірки).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Dialog {
    id: rd
    property var r: ({})
    signal startRender()
    function showResult(res) {
        r = res
        title = res.title
        iconName = res.state === "succeeded" ? "check" : res.state === "cancelled" ? "stop" : "error"
        iconColor = res.state === "succeeded" ? Theme.success : res.state === "cancelled" ? Theme.textSecondary : Theme.error
        open()
    }
    width: Math.min(Theme.px(640), (parent ? parent.width : 800) - Theme.s6 * 2)
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.s4
        Label {
            visible: rd.r.settingsError === true
            Layout.fillWidth: true
            text: qsTr("Гру не запускали: це помилка налаштувань, а не збій під час рендеру.")
            role: "secondary"
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        B.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(content.implicitHeight, Theme.px(420))
            clip: true
            ColumnLayout {
                id: content
                width: rd.availableWidth
                spacing: Theme.s3
                Label {
                    Layout.fillWidth: true
                    visible: rd.r.settingsError !== true
                    text: rd.r.text || ""
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    font.family: rd.r.kind === "test" ? Theme.monoFamily : Theme.fontFamily
                }
                Repeater {
                    model: rd.r.settingsError === true ? (rd.r.issues || []) : []
                    IssueItem {
                        required property var modelData
                        Layout.fillWidth: true
                        // Виправлення — на сторінці «Рендер» (там актуальні номери зауважень)
                        issue: ({ severity: modelData.severity, message: modelData.message, explanation: modelData.explanation, fixes: [], rule: modelData.rule })
                    }
                }
                Repeater {
                    model: rd.r.checks || []
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.s2
                        Icon {
                            Layout.preferredWidth: Theme.px(14)
                            Layout.preferredHeight: Theme.px(14)
                            name: modelData.state === "ok" ? "check" : modelData.state === "failed" ? "close" : modelData.state === "skipped" ? "minus" : "clock"
                            color: modelData.state === "ok" ? Theme.success : modelData.state === "failed" ? Theme.error : Theme.textMuted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.name + (modelData.detail ? " — " + modelData.detail : "")
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.s2
            Btn {
                visible: rd.r.kind === "test" && rd.r.testOk === true
                kind: "primary"
                iconName: "render"
                text: qsTr("Почати рендер")
                onClicked: { rd.close(); rd.startRender() }
            }
            Btn {
                visible: rd.r.settingsError === true
                kind: "primary"
                text: qsTr("До налаштувань")
                onClicked: { rd.close(); Ui.goTo("render") }
            }
            Btn {
                visible: rd.r.state === "succeeded" && (rd.r.result || "") !== "" && rd.r.kind !== "test"
                kind: "primary"
                text: rd.r.isFolder ? qsTr("Відкрити теку") : qsTr("Відкрити")
                onClicked: { Shell.openPath(rd.r.result); rd.close() }
            }
            Btn {
                visible: rd.r.state === "succeeded" && (rd.r.result || "") !== "" && !rd.r.isFolder && rd.r.kind !== "test"
                text: qsTr("Показати в теці")
                onClicked: { Shell.showInFolder(rd.r.result); rd.close() }
            }
            Btn {
                visible: (rd.r.text || "") !== ""
                kind: "ghost"
                iconName: "copy"
                text: qsTr("Копіювати")
                onClicked: Shell.copyText(rd.r.title + "\n\n" + rd.r.text)
            }
            Btn { text: qsTr("Закрити"); onClicked: rd.close() }
        }
    }
}
