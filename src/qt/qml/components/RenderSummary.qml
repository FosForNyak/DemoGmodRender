// Коротко про майбутній рендер: рендерер, кадр, FPS, кодек, бітність, копії гри, розмиття,
// файл і стан перевірки. Той самий підсумок — на всіх сторінках, де є кнопка рендеру.
import QtQuick
import QtQuick.Layouts
import Gmdr
import Gmdr.Ui

ColumnLayout {
    id: rs
    readonly property var d: (Config.revision, Config.derived)
    readonly property var codecOpt: {
        const st = (Config.revision, Config.state("video_codec"))
        const v = Config.value("video_codec")
        for (const o of st.options || []) if (o.value === v) return o
        return { label: v, group: "" }
    }
    spacing: Theme.s1
    Flow {
        Layout.fillWidth: true
        // Ширина Flow залежить від переносу рядків — не віддаємо її розкладці (інакше цикл розкладки)
        Layout.preferredWidth: Theme.px(120)
        spacing: Theme.s2
        Repeater {
            model: [
                (Env.games.find(g => g.renderer === rs.d.renderer) || { label: rs.d.renderer }).label,
                rs.d.width + " × " + rs.d.height,
                Math.round(rs.d.fps * 100) / 100 + " " + qsTr("кадрів/с"),
                String(rs.codecOpt.label).split(" — ")[0],
                rs.d.imageSequence ? "" : rs.d.bitDepth + " " + qsTr("біт") + " · " + (rs.d.chroma === 444 ? "4:4:4" : rs.d.chroma === 422 ? "4:2:2" : "4:2:0"),
                qsTr("Копій гри: %1×").arg(rs.d.parallel),
                rs.d.subframes > 1 ? qsTr("Розмиття: %1 під-кадрів").arg(rs.d.subframes) : "",
                Math.abs(rs.d.speed - 1) > 0.001 ? "×" + rs.d.speed : ""
            ].filter(x => x !== "")
            Rectangle {
                required property string modelData
                height: Theme.px(22)
                width: chip.implicitWidth + Theme.s3 * 2
                radius: Theme.r1
                color: Theme.surface2
                border.color: Theme.border
                Label {
                    id: chip
                    anchors.centerIn: parent
                    text: parent.modelData
                    role: "meta"
                    color: Theme.text
                }
            }
        }
    }
    Label {
        Layout.fillWidth: true
        role: "meta"
        text: rs.d.parallelReason !== "" && rs.d.parallel < Number(Config.value("parallel_games"))
              ? qsTr("Одна копія гри: %1").arg(rs.d.parallelReason) : ""
        visible: text !== ""
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        Icon {
            Layout.preferredWidth: Theme.px(14)
            Layout.preferredHeight: Theme.px(14)
            name: "file"
            color: Theme.textSecondary
        }
        Label {
            Layout.fillWidth: true
            text: (Config.revision, String(Config.value("output_path") || qsTr("(поруч із демо)")))
            role: "secondary"
            elide: Text.ElideMiddle
        }
    }
    RowLayout {
        spacing: Theme.s2
        Label {
            role: "meta"
            visible: rs.d.videoSeconds > 0
            text: qsTr("%1 відео · %2 кадрів").arg(Project.formatTime(rs.d.videoSeconds)).arg(rs.d.videoFrames)
                  + (rs.d.estimatedBytes > 0 ? " · ≈ " + Shell.formatBytes(rs.d.estimatedBytes) : "")
        }
    }
}
