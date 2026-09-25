// Діалог у стилі програми (лише коли потрібна дія користувача).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Templates as T
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

B.Dialog {
    id: dlg
    property string iconName: ""
    property color iconColor: Theme.accentText
    modal: true
    anchors.centerIn: T.Overlay.overlay
    width: Math.min(Theme.px(560), (parent ? parent.width : 800) - Theme.s6 * 2)
    padding: Theme.s5
    topPadding: Theme.s4
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    T.Overlay.modal: Rectangle { color: Theme.overlay }
    background: Rectangle {
        color: Theme.surface
        radius: Theme.r3
        border.color: Theme.border
    }
    header: RowLayout {
        spacing: Theme.s3
        Item { Layout.preferredWidth: Theme.s5 - Theme.s3 }
        Icon {
            visible: dlg.iconName !== ""
            Layout.topMargin: Theme.s5
            Layout.preferredWidth: Theme.px(20)
            Layout.preferredHeight: Theme.px(20)
            name: dlg.iconName
            color: dlg.iconColor
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s5
            text: dlg.title
            role: "title"
            font.pixelSize: Theme.fontSection + 2
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Item { Layout.preferredWidth: Theme.s5 - Theme.s3 }
    }
    footer: Item { implicitHeight: 0 }
}
