// Число для налаштування: межі — з перевірки (st.min / st.max), неправильне значення не записується.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

SettingRow {
    id: r
    property int decimals: 0
    property string suffix: ""
    property real scaleFactor: 1       // показ: значення * scaleFactor (напр. гучність у %)
    RowLayout {
        spacing: Theme.s2
        Field {
            id: f
            Layout.preferredWidth: Theme.px(96)
            severity: r.st.severity || ""
            horizontalAlignment: TextInput.AlignRight
            readonly property real v: (Config.revision, Number(Config.value(r.key)))
            text: (v * r.scaleFactor).toFixed(r.decimals)
            onEditingFinished: {
                const n = parseFloat(text.replace(",", "."))
                if (!isNaN(n)) Config.set(r.key, n / r.scaleFactor)
                text = Qt.binding(() => (v * r.scaleFactor).toFixed(r.decimals))
            }
            Accessible.name: r.label
        }
        Label {
            text: r.suffix
            role: "secondary"
            visible: r.suffix !== ""
        }
        Label {
            Layout.fillWidth: true
            role: "meta"
            visible: r.st.min !== undefined && r.st.max !== undefined
            text: visible ? qsTr("%1…%2").arg(r.st.min * r.scaleFactor).arg(r.st.max * r.scaleFactor) : ""
        }
    }
}
