// Повзунок для налаштування.
import QtQuick
import Gmdr
import Gmdr.Ui

SettingRow {
    id: r
    property real from: 0
    property real to: 1
    property real stepSize: 0.01
    property int decimals: 2
    property string suffix: ""
    Slide {
        from: r.from
        to: r.to
        stepSize: r.stepSize
        decimals: r.decimals
        suffix: r.suffix
        value: (Config.revision, Number(Config.value(r.key)))
        onMoved: (v) => Config.set(r.key, v)
    }
}
