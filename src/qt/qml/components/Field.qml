// Поле введення. severity — рамка стану (error / warning).
import QtQuick
import QtQuick.Controls.Basic as B
import Gmdr.Ui

B.TextField {
    id: f
    property string severity: ""
    implicitHeight: Theme.controlHeight
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    color: enabled ? Theme.text : Theme.textMuted
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.text
    leftPadding: Theme.s3
    rightPadding: Theme.s3
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    background: Rectangle {
        radius: Theme.r2
        color: f.enabled ? Theme.field : Theme.surface2
        border.width: f.activeFocus ? 2 : 1
        border.color: f.activeFocus ? Theme.focus
                      : f.severity === "error" ? Theme.error
                      : f.severity === "warning" ? Theme.warning
                      : f.hovered ? Theme.borderStrong : Theme.border
    }
}
