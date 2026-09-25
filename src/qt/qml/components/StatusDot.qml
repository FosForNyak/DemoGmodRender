// Стан складника: значок і текст (готово / немає / не налаштовано / не працює / невідомо).
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

RowLayout {
    property string state: "unknown"
    property string text: ""
    readonly property color tone: state === "ready" || state === "available" ? Theme.success
                                  : state === "unknown" || state === "checking" || state === "notApplicable" ? Theme.textMuted
                                  : state === "notConfigured" ? Theme.warning : Theme.error
    spacing: Theme.s2
    Icon {
        Layout.preferredWidth: Theme.px(14)
        Layout.preferredHeight: Theme.px(14)
        name: parent.state === "ready" || parent.state === "available" ? "check"
              : parent.state === "unknown" || parent.state === "checking" ? "clock"
              : parent.state === "notConfigured" ? "warning" : "close"
        color: parent.tone
    }
    Label {
        Layout.fillWidth: true
        text: parent.text
        color: parent.tone
    }
}
