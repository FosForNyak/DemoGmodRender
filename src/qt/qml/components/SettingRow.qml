// Рядок налаштування: підпис, редактор, значок стану і пояснення під редактором
// (чому вимкнено, примітка «Обмежено: GMod RTX», зауваження перевірки).
// Видимість, доступність і зауваження — з перевірки налаштувань (Config.state).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as B
import Gmdr
import Gmdr.Ui

RowLayout {
    id: row
    property string key: ""
    property string label: key !== "" ? Config.label(key) : ""
    property string hint: ""
    property bool showAlways: false        // показувати й у стандартному режимі
    property bool showMessages: true
    readonly property var st: (Config.revision, key !== "" ? Config.state(key) : ({}))
    readonly property bool advancedOnly: key !== "" && Config.isAdvancedSetting(key)
    readonly property bool settingEnabled: st.enabled !== false
    default property alias editor: holder.data
    visible: st.visible !== false && (!advancedOnly || Config.advanced || showAlways)
    Layout.fillWidth: true
    spacing: Theme.s3
    RowLayout {
        Layout.preferredWidth: Theme.labelWidth
        Layout.maximumWidth: Theme.labelWidth
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: Math.max(0, (Theme.controlHeight - lbl.implicitHeight) / 2)
        spacing: Theme.s1
        Label {
            id: lbl
            Layout.fillWidth: true
            text: row.label
            color: row.settingEnabled ? Theme.text : Theme.textMuted
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            HoverHandler { id: lh }
            B.ToolTip.visible: lh.hovered && row.hint !== ""
            B.ToolTip.text: row.hint
            B.ToolTip.delay: 400
        }
        ValidationBadge {
            Layout.alignment: Qt.AlignTop
            severity: row.st.severity || ""
            messages: row.st.messages || []
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Item {
            id: holder
            Layout.fillWidth: true
            implicitHeight: children.length > 0 ? children[0].implicitHeight : Theme.controlHeight
            enabled: row.settingEnabled
            onChildrenChanged: {
                for (let i = 0; i < children.length; ++i) children[i].width = Qt.binding(() => holder.width)
            }
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            role: "meta"
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            color: !row.settingEnabled ? Theme.textMuted : Theme.textSecondary
            text: !row.settingEnabled && (row.st.reason || "") !== "" ? qsTr("Недоступно: %1").arg(row.st.reason)
                  : (row.st.note || "")
        }
        Repeater {
            model: row.showMessages && row.st.severity === "error" ? (row.st.ownMessages || []) : []
            Label {
                required property string modelData
                Layout.fillWidth: true
                role: "meta"
                color: Theme.error
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                text: modelData
            }
        }
    }
}
