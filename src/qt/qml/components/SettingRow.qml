// Рядок налаштування: підпис, редактор, значок стану і пояснення під редактором
// (чому вимкнено, примітка «Обмежено: GMod RTX», зауваження перевірки).
// Видимість, доступність і зауваження — з перевірки налаштувань (Config.state).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as B
import Gmdr
import Gmdr.Ui

GridLayout {
    id: row
    property string key: ""
    property string label: key !== "" ? Config.label(key) : ""
    property string hint: ""
    property bool showAlways: false        // показувати й у стандартному режимі
    property bool forceVisible: false      // показувати, навіть якщо перевірка ховає (напр., папки обох рендерерів у налаштуваннях)
    property bool showMessages: true
    readonly property var st: (Config.revision, key !== "" ? Config.state(key) : ({}))
    readonly property bool advancedOnly: key !== "" && Config.isAdvancedSetting(key)
    readonly property bool settingEnabled: st.enabled !== false
    default property alias editor: holder.data
    visible: (forceVisible || st.visible !== false) && (!advancedOnly || Config.advanced || showAlways)
    Layout.fillWidth: true
    // Вузьке вікно (чи великий масштаб) — підпис над редактором. Рішення — від ширини вікна (Ui.narrow),
    // а не від ширини рядка: та залежить від кількості стовпців, і розкладка перераховувалась би
    // рекурсивно (Qt 6.8 зависав, Qt 6.4 мовчки лишав старі розміри)
    readonly property bool stacked: Ui.narrow
    columns: stacked ? 1 : 2
    columnSpacing: Theme.s3
    rowSpacing: Theme.s1
    RowLayout {
        visible: !row.stacked || row.label !== "" || (row.st.severity || "") !== ""
        Layout.preferredWidth: row.stacked ? -1 : Theme.labelWidth
        Layout.maximumWidth: row.stacked ? Number.POSITIVE_INFINITY : Theme.labelWidth
        Layout.fillWidth: row.stacked
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: row.stacked ? 0 : Math.max(0, (Theme.controlHeight - lbl.implicitHeight) / 2)
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
        // Редактор — у розкладці, що сама дає йому ширину (без прив'язок ширини вручну: висота
        // тоді залежала б від ширини в обхід розкладки, і Qt 6.8 бачить рекурсивне перерозташування)
        ColumnLayout {
            id: holder
            Layout.fillWidth: true
            Layout.minimumWidth: 0   // найменша ширина редактора не розширює сторінку — він стискається
            Layout.minimumHeight: Theme.controlHeight
            spacing: 0
            enabled: row.settingEnabled
            onChildrenChanged: {
                for (let i = 0; i < children.length; ++i) children[i].Layout.fillWidth = true
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
        Each {
            items: row.showMessages && row.st.severity === "error" ? (row.st.ownMessages || []) : []
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
