// Токени оформлення: кольори (темна / світла тема, акцент), відступи, радіуси, шрифти,
// висоти елементів, тривалості анімацій. Усе масштабується разом з інтерфейсом (80–200 %).
// Інші файли не тримають власних «магічних» чисел — лише ці токени.
pragma Singleton
import QtQuick
import Gmdr

QtObject {
    id: t

    // ---- налаштування вигляду ----
    readonly property int rev: Config.revision
    readonly property string themeId: (rev, Config.value("ui_theme") || "dark")
    readonly property bool light: themeId === "light" || (themeId === "system" && Shell.systemLight)
    readonly property bool dark: !light
    readonly property real scale: (rev, Math.max(0.8, Math.min(2.0, Number(Config.value("ui_scale")) || 1.0)))
    readonly property bool compact: (rev, Config.value("ui_compact") === true)
    function px(v) { return Math.round(v * scale) }

    // ---- акцент ----
    readonly property var accents: [
        { id: "sky", label: qsTr("Блакитний"), color: "#4CC2FF" },
        { id: "violet", label: qsTr("Фіолетовий"), color: "#7B61FF" },
        { id: "blue", label: qsTr("Синій"), color: "#3B82F6" },
        { id: "teal", label: qsTr("Бірюзовий"), color: "#14B8A6" },
        { id: "green", label: qsTr("Зелений"), color: "#22B35E" },
        { id: "orange", label: qsTr("Помаранчевий"), color: "#F97316" },
        { id: "pink", label: qsTr("Рожевий"), color: "#EC4899" },
        { id: "red", label: qsTr("Червоний"), color: "#EF4444" }
    ]
    readonly property string accentId: (rev, Config.value("ui_accent") || "sky")
    readonly property color accent: {
        for (let i = 0; i < accents.length; ++i)
            if (accents[i].id === accentId) return accents[i].color
        return accents[0].color
    }
    readonly property color accentHover: light ? Qt.darker(accent, 1.12) : Qt.lighter(accent, 1.12)
    readonly property color accentPressed: light ? Qt.darker(accent, 1.25) : Qt.darker(accent, 1.1)
    // Текст на акцентній кнопці: темний на світлих акцентах, білий на темних
    readonly property color onAccent: (accent.r * 0.299 + accent.g * 0.587 + accent.b * 0.114) > 0.6 ? "#0B0D10" : "#FFFFFF"
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, light ? 0.14 : 0.18)
    readonly property color accentText: light ? Qt.darker(accent, 1.35) : Qt.lighter(accent, 1.15)

    // ---- поверхні й текст ----
    readonly property color bg: light ? "#F4F5F7" : "#111214"
    readonly property color surface: light ? "#FFFFFF" : "#181A1D"
    readonly property color surface2: light ? "#EEF0F2" : "#1E2023"
    readonly property color surface3: light ? "#E7E9EC" : "#24272B"
    readonly property color hover: light ? "#E7E9EC" : "#2A2D32"
    readonly property color pressed: light ? "#DDE0E4" : "#31353B"
    readonly property color border: light ? "#D4D8DD" : "#30343A"
    readonly property color borderStrong: light ? "#B9BFC7" : "#3E434A"
    readonly property color field: light ? "#FFFFFF" : "#141517"
    readonly property color text: light ? "#1B1E22" : "#F1F3F5"
    readonly property color textSecondary: light ? "#5F6670" : "#A6ACB4"
    readonly property color textMuted: light ? "#8A9099" : "#747B84"
    readonly property color overlay: light ? Qt.rgba(0.1, 0.12, 0.15, 0.35) : Qt.rgba(0, 0, 0, 0.55)
    readonly property color focus: accent

    // ---- значення станів (завжди разом зі значком чи текстом, не лише кольором) ----
    readonly property color success: light ? "#1E8E4E" : "#3CCB7F"
    readonly property color warning: light ? "#B26A00" : "#F0B429"
    readonly property color error: light ? "#C62828" : "#FF6B6B"
    readonly property color info: light ? "#2F6FCF" : "#6CB6FF"
    function severityColor(s) { return s === "error" ? error : s === "warning" ? warning : s === "info" ? info : textSecondary }
    function severityIcon(s) { return s === "error" ? "error" : s === "warning" ? "warning" : "info" }
    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    // ---- відступи, радіуси ----
    readonly property int s1: px(compact ? 2 : 4)
    readonly property int s2: px(compact ? 4 : 6)
    readonly property int s3: px(compact ? 6 : 8)
    readonly property int s4: px(compact ? 8 : 12)
    readonly property int s5: px(compact ? 12 : 16)
    readonly property int s6: px(compact ? 16 : 24)
    readonly property int r1: px(3)
    readonly property int r2: px(5)
    readonly property int r3: px(8)

    // ---- шрифти ----
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI Variable Text" : "Inter"
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Cascadia Mono" : "monospace"
    readonly property int fontTitle: px(19)
    readonly property int fontSection: px(14)
    readonly property int fontBody: px(13)
    readonly property int fontSmall: px(12)
    readonly property int fontMeta: px(11)

    // ---- розміри ----
    readonly property int controlHeight: px(compact ? 24 : 28)
    readonly property int rowHeight: px(compact ? 26 : 32)
    readonly property int iconSize: px(16)
    readonly property int sidebarWidth: px(208)
    readonly property int sidebarCollapsed: px(52)
    readonly property int topBarHeight: px(44)
    readonly property int statusBarHeight: px(26)
    readonly property int labelWidth: px(180)
    readonly property int borderWidth: 1

    // ---- анімації ----
    readonly property int animFast: 110
    readonly property int animNormal: 180
}
