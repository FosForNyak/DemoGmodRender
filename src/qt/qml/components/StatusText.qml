// Результат перевірки поруч із кнопкою: зелений — вдалося, червоний — ні.
import QtQuick
import QtQuick.Layouts
import Gmdr.Ui

Label {
    property var status: null            // { ok, text } або null
    visible: status !== null && text !== ""
    Layout.fillWidth: true
    text: status ? status.text : ""
    color: status && status.ok ? Theme.success : Theme.error
    wrapMode: Text.WordWrap
    elide: Text.ElideNone
}
