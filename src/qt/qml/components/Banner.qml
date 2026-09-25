// Смужка-повідомлення на сторінці: попередження, помилка чи довідка з необов'язковою дією.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    id: b
    property string tone: "warning"      // warning / error / info / success
    property string text: ""
    property string actionText: ""
    property string actionIcon: ""
    property string secondaryText: ""
    signal action()
    signal secondaryAction()
    readonly property color toneColor: tone === "error" ? Theme.error : tone === "info" ? Theme.info
                                       : tone === "success" ? Theme.success : Theme.warning
    Layout.fillWidth: true
    implicitHeight: row.implicitHeight + Theme.s3 * 2
    radius: Theme.r2
    color: Theme.tint(toneColor, 0.1)
    border.color: Theme.tint(toneColor, 0.45)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.s3
        spacing: Theme.s3
        Icon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
            Layout.preferredWidth: Theme.iconSize
            Layout.preferredHeight: Theme.iconSize
            name: b.tone === "error" ? "error" : b.tone === "info" ? "info" : b.tone === "success" ? "check" : "warning"
            color: b.toneColor
        }
        Label {
            Layout.fillWidth: true
            text: b.text
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Btn {
            visible: b.actionText !== ""
            text: b.actionText
            iconName: b.actionIcon
            onClicked: b.action()
        }
        Btn {
            visible: b.secondaryText !== ""
            kind: "ghost"
            text: b.secondaryText
            onClicked: b.secondaryAction()
        }
    }
}
