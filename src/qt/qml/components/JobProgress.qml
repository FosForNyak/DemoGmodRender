// Стан поточного завдання: етап (єдина модель стану), прогрес, кадри, швидкість, час.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

ColumnLayout {
    readonly property var p: Jobs.progress
    spacing: Theme.s2
    RowLayout {
        Layout.fillWidth: true
        Label { text: Jobs.busy ? Jobs.title : (Jobs.lastResult.title || ""); font.weight: Font.Medium }
        Label { Layout.fillWidth: true; text: Jobs.busy ? "· " + Jobs.phaseLabel : ""; role: "secondary" }
        Btn { visible: Jobs.busy; kind: "ghost"; text: qsTr("Зупинити"); onClicked: Jobs.cancel() }
        Btn { visible: Jobs.canShowGame; kind: "ghost"; checkable: true; checked: Jobs.showGame; text: qsTr("Показати гру"); onToggled: Jobs.showGame = checked }
    }
    Bar {
        visible: Jobs.busy
        Layout.fillWidth: true
        value: parent.p.fraction || 0
        indeterminate: (parent.p.fraction || 0) <= 0
    }
    Label {
        visible: Jobs.busy
        Layout.fillWidth: true
        role: "meta"
        text: {
            const q = parent.p
            const parts = [q.stage]
            if (q.frames > 0) parts.push(qsTr("кадрів %1").arg(q.frames))
            if (q.videoSeconds > 0) parts.push(qsTr("відео %1").arg(Project.formatTime(q.videoSeconds)))
            if (q.speed > 0) parts.push(q.speed.toFixed(1) + " " + qsTr("к/с"))
            if (q.eta >= 0) parts.push(qsTr("залишилось %1").arg(Project.formatTime(q.eta)))
            if (q.gamePaused) parts.push(q.diskLow ? qsTr("гру призупинено: мало місця на диску") : qsTr("гра на паузі"))
            if (q.gameRestarts > 0) parts.push(qsTr("перезапусків гри: %1").arg(q.gameRestarts))
            return parts.filter(x => x).join(" · ")
        }
        wrapMode: Text.WordWrap
    }
    Label {
        visible: !Jobs.busy && (Jobs.lastResult.text || "") !== ""
        Layout.fillWidth: true
        role: "secondary"
        text: (Jobs.lastResult.text || "").split("\n\n")[0]
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        maximumLineCount: 4
    }
}
