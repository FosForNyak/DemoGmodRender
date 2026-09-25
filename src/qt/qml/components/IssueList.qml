// Перелік зауважень (помилки, попередження, за бажанням — інформація).
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

ColumnLayout {
    id: il
    property var issues: []
    property bool showInfo: true
    property bool compactView: false
    property string emptyText: ""
    spacing: Theme.s2
    Repeater {
        model: il.issues.filter(i => il.showInfo || i.severity !== "info")
        IssueItem {
            required property var modelData
            Layout.fillWidth: true
            issue: modelData
            compactView: il.compactView
        }
    }
    Label {
        visible: il.emptyText !== "" && il.issues.filter(i => il.showInfo || i.severity !== "info").length === 0
        Layout.fillWidth: true
        text: il.emptyText
        role: "secondary"
    }
}
