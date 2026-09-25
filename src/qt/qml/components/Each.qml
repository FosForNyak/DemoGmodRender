// Repeater, що перестворює делегати лише тоді, коли перелік справді змінився.
// Масиви з C++ (варіанти, зауваження) приходять новими з кожною перевіркою налаштувань;
// звичайний Repeater тоді щоразу будує делегати заново: губиться наведення й фокус, а
// розкладка Qt 6.4 може впасти, якщо вікно змінює розмір, поки старі ще не видалені.
import QtQuick

Repeater {
    id: e
    property var items: []
    property var current: []
    property string currentJson: "[]"
    model: current
    // Копія, а не посилання: список із C++ у Qt 6.4 — «живий» і змінюється разом із властивістю
    function sync() {
        const json = JSON.stringify(e.items || [])
        if (json === e.currentJson) return
        e.currentJson = json
        e.current = JSON.parse(json)
    }
    onItemsChanged: sync()
    Component.onCompleted: sync()
}
