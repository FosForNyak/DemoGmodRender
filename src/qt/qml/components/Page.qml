// Каркас робочого простору: заголовок з поясненням і діями, прокручуваний вміст обмеженої
// ширини (на широкому екрані — не розтягується на весь монітор).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Item {
    id: page
    property string title: ""
    property string subtitle: ""
    property int maxContentWidth: Theme.px(1180)
    property bool scrollable: true
    property alias actions: actionsRow.data
    default property alias content: col.data
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s5
            Layout.bottomMargin: Theme.s4
            spacing: Theme.s3
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: page.title; role: "title" }
                Label {
                    Layout.fillWidth: true
                    visible: page.subtitle !== ""
                    text: page.subtitle
                    role: "secondary"
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
            }
            Row { id: actionsRow; spacing: Theme.s2 }
        }
        B.ScrollView {
            id: sv
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            B.ScrollBar.horizontal.policy: B.ScrollBar.AlwaysOff
            ColumnLayout {
                id: col
                x: Theme.s6
                width: Math.min(sv.availableWidth - Theme.s6 * 2, page.maxContentWidth)
                spacing: Theme.s4
            }
        }
    }
}
