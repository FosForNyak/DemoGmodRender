// Рядок стану: що відбувається (єдина модель стану рендеру), прогрес, огляд системи,
// графічний API вікна і версія.
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

Rectangle {
    implicitHeight: Theme.statusBarHeight
    color: Theme.surface
    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
    readonly property var p: Jobs.progress
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s3
        anchors.rightMargin: Theme.s3
        spacing: Theme.s3
        Icon {
            Layout.preferredWidth: Theme.px(12)
            Layout.preferredHeight: Theme.px(12)
            name: Jobs.busy ? "record" : "check"
            color: Jobs.busy ? Theme.accent : Theme.textMuted
        }
        Label {
            role: "meta"
            color: Jobs.busy ? Theme.text : Theme.textMuted
            text: Jobs.busy ? Jobs.title + " · " + Jobs.phaseLabel : (Log.lastWarning !== "" ? Log.lastWarning : qsTr("Готово"))
            Layout.maximumWidth: parent.width * 0.45
        }
        Bar {
            visible: Jobs.busy
            Layout.preferredWidth: Theme.px(160)
            value: parent.parent.p.fraction || 0
            indeterminate: (parent.parent.p.fraction || 0) <= 0
        }
        Label {
            visible: Jobs.busy
            role: "meta"
            text: {
                const q = parent.parent.p
                let t = Math.round(Math.max(0, q.fraction || 0) * 100) + " %"
                if (q.frames > 0) t += " · " + qsTr("кадрів %1").arg(q.frames)
                if (q.speed > 0) t += " · " + q.speed.toFixed(1) + " " + qsTr("к/с")
                if (q.eta >= 0) t += " · " + qsTr("залишилось %1").arg(Project.formatTime(q.eta))
                return t
            }
        }
        Item { Layout.fillWidth: true }
        Label {
            visible: Env.scanning || Env.probingGpu
            role: "meta"
            text: Env.scanning ? qsTr("Перевірка системи…") : qsTr("Перевірка GPU-кодеків…")
        }
        Label {
            role: "meta"
            visible: Env.activeGraphicsApi !== ""
            text: qsTr("Вікно: %1").arg(Env.graphicsApiLabel(Env.activeGraphicsApi))
            HoverHandler { id: gh }
            B.ToolTip.visible: gh.hovered
            B.ToolTip.text: qsTr("Графічний API самого вікна програми (не рендерер гри). Змінити — «Налаштування → Графіка».")
        }
        Label { role: "meta"; text: "v" + Env.system.app }
    }
}
