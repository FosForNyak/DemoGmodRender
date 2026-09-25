// Так / ні для налаштування; text — підпис праворуч (label рядка можна лишити порожнім).
import QtQuick
import Gmdr
import Gmdr.Ui

SettingRow {
    id: r
    property string text: ""
    label: ""
    Toggle {
        text: r.text !== "" ? r.text : Config.label(r.key)
        checked: (Config.revision, Config.value(r.key) === true)
        onToggled: Config.set(r.key, checked)
    }
}
