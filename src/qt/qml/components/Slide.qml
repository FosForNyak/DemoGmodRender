// Повзунок з числом праворуч (число можна ввести).
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr.Ui

RowLayout {
    id: r
    property real from: 0
    property real to: 1
    property real value: 0
    property real stepSize: 0.01
    property int decimals: 2
    property string suffix: ""
    signal moved(real value)
    spacing: Theme.s3
    B.Slider {
        id: s
        Layout.fillWidth: true
        from: r.from
        to: r.to
        stepSize: r.stepSize
        value: r.value
        enabled: r.enabled
        hoverEnabled: true
        onMoved: r.moved(value)
        background: Rectangle {
            x: s.leftPadding
            y: s.topPadding + s.availableHeight / 2 - height / 2
            width: s.availableWidth
            height: Theme.px(4)
            radius: height / 2
            color: Theme.surface3
            Rectangle {
                width: s.visualPosition * parent.width
                height: parent.height
                radius: height / 2
                color: s.enabled ? Theme.accent : Theme.textMuted
            }
        }
        handle: Rectangle {
            x: s.leftPadding + s.visualPosition * (s.availableWidth - width)
            y: s.topPadding + s.availableHeight / 2 - height / 2
            width: Theme.px(14)
            height: width
            radius: width / 2
            color: "#FFFFFF"
            border.width: s.visualFocus ? 3 : 2
            border.color: s.enabled ? (s.visualFocus ? Theme.focus : Theme.accent) : Theme.textMuted
        }
    }
    Field {
        Layout.preferredWidth: Theme.px(64)
        text: Number(r.value).toFixed(r.decimals) + r.suffix
        horizontalAlignment: TextInput.AlignRight
        enabled: r.enabled
        onEditingFinished: {
            const v = parseFloat(text.replace(",", "."))
            if (!isNaN(v)) r.moved(Math.max(r.from, Math.min(r.to, v)))
            text = Qt.binding(() => Number(r.value).toFixed(r.decimals) + r.suffix)
        }
    }
}
