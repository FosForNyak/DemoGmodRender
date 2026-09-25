// Перелік зауважень (помилки, попередження, за бажанням — інформація).
// Усередині — Column, а не ColumnLayout: делегати Repeater перестворюються з кожною перевіркою
// налаштувань, а розкладка Qt 6.4 падає, якщо вікно змінює розмір, поки старі ще не видалені.
// Зовні — Item без власної «природної» ширини: ширину задає батьківська розкладка (інакше
// ширина списку залежала б від самої себе — цикл розкладки). Список оновлюється лише тоді,
// коли зауваження справді змінились.
import QtQuick
import Gmdr
import Gmdr.Ui

Item {
    id: il
    property var issues: []
    property bool showInfo: true
    property bool compactView: false
    property string emptyText: ""
    property var shown: []
    property string shownJson: "[]"
    implicitHeight: col.implicitHeight
    function refresh() {
        const json = JSON.stringify((il.issues || []).filter(i => il.showInfo || i.severity !== "info"))
        if (json === il.shownJson) return
        il.shownJson = json
        il.shown = JSON.parse(json)   // копія: список із C++ «живий»
    }
    onIssuesChanged: refresh()
    onShowInfoChanged: refresh()
    Component.onCompleted: refresh()
    Column {
        id: col
        width: il.width
        spacing: Theme.s2
        Repeater {
            model: il.shown
            IssueItem {
                required property var modelData
                width: col.width
                issue: modelData
                compactView: il.compactView
            }
        }
        Label {
            visible: il.emptyText !== "" && il.shown.length === 0
            width: col.width
            text: il.emptyText
            role: "secondary"
            wrapMode: Text.WordWrap
        }
    }
}
