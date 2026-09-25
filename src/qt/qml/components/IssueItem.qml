// Одне зауваження перевірки: що не так, чому, і що зробити (виправлення — кнопками).
// У режимі розробника — ще ідентифікатор правила і пов'язані налаштування.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    id: it
    property var issue: ({})
    property bool compactView: false
    readonly property color tone: Theme.severityColor(issue.severity)
    implicitHeight: row.implicitHeight + Theme.s3 * 2
    radius: Theme.r2
    color: Theme.tint(tone, Theme.light ? 0.07 : 0.09)
    border.color: Theme.tint(tone, 0.35)
    Accessible.role: Accessible.StaticText
    Accessible.name: (issue.message || "") + " " + (issue.explanation || "")
    // Ширина — від рамки, висота — від вмісту (без anchors.fill: висота рамки сама залежить від
    // переносу тексту, і розкладка Qt 6.4 падає на такому циклі)
    RowLayout {
        id: row
        x: Theme.s3
        y: Theme.s3
        width: it.width - Theme.s3 * 2
        spacing: Theme.s3
        Icon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
            Layout.preferredWidth: Theme.iconSize
            Layout.preferredHeight: Theme.iconSize
            name: Theme.severityIcon(it.issue.severity)
            color: it.tone
        }
        ColumnLayout {
            id: col
            Layout.fillWidth: true
            spacing: Theme.s1
            Label {
                Layout.fillWidth: true
                text: it.issue.message || ""
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                font.weight: Font.Medium
            }
            Label {
                Layout.fillWidth: true
                visible: !it.compactView && (it.issue.explanation || "") !== ""
                text: it.issue.explanation || ""
                role: "secondary"
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.s2
                visible: (it.issue.fixes && it.issue.fixes.length > 0) || (it.issue.action || "") !== ""
                Repeater {
                    model: it.issue.fixes || []
                    Btn {
                        required property string modelData
                        required property int index
                        kind: index === 0 ? "secondary" : "ghost"
                        iconName: index === 0 ? "check" : ""
                        text: modelData
                        tip: qsTr("Застосувати виправлення")
                        onClicked: Config.applyFix(it.issue.index, index)
                    }
                }
                Btn {
                    visible: (it.issue.action || "") !== "" && Ui.actionLabel(it.issue.action) !== ""
                    kind: "secondary"
                    text: Ui.actionLabel(it.issue.action || "")
                    onClicked: Ui.runAction(it.issue.action)
                }
            }
            Label {
                visible: Ui.developer
                Layout.fillWidth: true
                role: "mono"
                font.pixelSize: Theme.fontMeta
                color: Theme.textMuted
                text: (it.issue.rule || "") + " · " + (it.issue.kind || "") + (it.issue.setting ? " · " + it.issue.setting : "")
                      + (it.issue.related && it.issue.related.length ? " → " + it.issue.related.join(", ") : "")
            }
        }
    }
}
