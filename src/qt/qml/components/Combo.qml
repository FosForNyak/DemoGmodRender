// Вибір зі списку. Модель — масив {value, label, available, reason, group, warning, recommended}.
// Недоступні варіанти видно (з причиною), але вибрати їх не можна; поточне значення — selected.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

B.ComboBox {
    id: c
    property var options: []
    // Варіанти приходять новими з кожною перевіркою; модель змінюється лише зі змістом
    // (інакше відкритий список закривається і скидається)
    property var shown: []
    property string shownJson: "[]"
    function syncOptions() {
        const json = JSON.stringify(c.options || [])
        if (json === c.shownJson) return
        c.shownJson = json
        c.shown = JSON.parse(json)   // копія: список із C++ «живий»
    }
    onOptionsChanged: syncOptions()
    Component.onCompleted: syncOptions()
    property var selected
    property string severity: ""
    property string placeholder: ""
    signal chosen(var value)
    model: shown
    textRole: "label"
    valueRole: "value"
    implicitHeight: Theme.controlHeight
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    hoverEnabled: true
    readonly property int valueIndex: {
        const o = shown
        for (let i = 0; i < o.length; ++i)
            if (String(o[i].value) === String(selected)) return i
        return -1
    }
    currentIndex: valueIndex
    displayText: valueIndex >= 0 ? shown[valueIndex].label : (selected !== undefined && selected !== "" ? String(selected) : placeholder)
    onActivated: (i) => {
        if (i >= 0 && i < shown.length && shown[i].available !== false) chosen(shown[i].value)
        currentIndex = Qt.binding(() => valueIndex)
    }
    background: Rectangle {
        radius: Theme.r2
        color: c.enabled ? (c.hovered ? Theme.hover : Theme.field) : Theme.surface2
        border.width: c.visualFocus || c.popup.visible ? 2 : 1
        border.color: c.visualFocus || c.popup.visible ? Theme.focus
                      : c.severity === "error" ? Theme.error : c.severity === "warning" ? Theme.warning : Theme.border
    }
    contentItem: Text {
        leftPadding: Theme.s3
        rightPadding: c.indicator.width + Theme.s2
        text: c.displayText
        font: c.font
        color: c.enabled ? (c.valueIndex >= 0 ? Theme.text : Theme.textSecondary) : Theme.textMuted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: c.width - width - Theme.s3
        y: (c.height - height) / 2
        width: Theme.px(12)
        height: Theme.px(12)
        name: "chevronDown"
        color: Theme.textSecondary
    }
    delegate: B.ItemDelegate {
        id: d
        required property var modelData
        required property int index
        readonly property bool header: index === 0 ? modelData.group !== undefined && modelData.group !== ""
                                                   : (modelData.group || "") !== (c.shown[index - 1].group || "")
        width: ListView.view ? ListView.view.width : c.width
        enabled: modelData.available !== false
        hoverEnabled: true
        padding: 0
        implicitHeight: Theme.rowHeight + (header ? Theme.rowHeight * 0.8 : 0) + (modelData.reason && !enabled ? Theme.fontMeta + 2 : 0)
        highlighted: c.highlightedIndex === index
        background: Rectangle {
            color: d.highlighted && d.enabled ? Theme.hover : "transparent"
        }
        contentItem: ColumnLayout {
            spacing: 0
            Label {
                visible: d.header
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.rowHeight * 0.8
                leftPadding: Theme.s3
                text: d.modelData.group || ""
                role: "meta"
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.rowHeight
                spacing: Theme.s2
                Item { Layout.preferredWidth: Theme.s1 }
                Icon {
                    Layout.preferredWidth: Theme.px(12)
                    Layout.preferredHeight: Theme.px(12)
                    name: String(d.modelData.value) === String(c.selected) ? "check" : d.modelData.warning ? "warning" : ""
                    color: d.modelData.warning ? Theme.warning : Theme.accentText
                }
                Label {
                    Layout.fillWidth: true
                    text: d.modelData.label + (d.modelData.recommended ? "  · " + qsTr("рекомендовано") : "")
                    color: d.enabled ? Theme.text : Theme.textMuted
                }
            }
            Label {
                visible: !d.enabled && (d.modelData.reason || "") !== ""
                Layout.fillWidth: true
                leftPadding: Theme.s3 + Theme.px(12) + Theme.s2
                bottomPadding: 2
                text: d.modelData.reason || ""
                role: "meta"
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
        }
        B.ToolTip.visible: hovered && d.enabled && d.modelData.warning && (d.modelData.reason || "") !== ""
        B.ToolTip.text: d.modelData.reason || ""
    }
    popup: B.Popup {
        y: c.height + 2
        width: Math.max(c.width, Theme.px(260))
        implicitHeight: Math.min(contentItem.implicitHeight + 4, Theme.px(420))
        padding: 2
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: c.popup.visible ? c.delegateModel : null
            currentIndex: c.highlightedIndex
            B.ScrollIndicator.vertical: B.ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.surface
            radius: Theme.r2
            border.color: Theme.border
        }
    }
}
