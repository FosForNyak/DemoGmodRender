// Спільний стан інтерфейсу (не налаштування рендеру): поточний робочий простір, що вибрано
// в редакторі, повідомлення і дії, які потребують головного вікна (діалоги).
pragma Singleton
import QtQuick
import Gmdr

QtObject {
    id: ui
    property string page: "project"
    property string settingsCategory: "appearance"   // вибрана категорія в «Налаштуваннях»
    property string selection: ""          // "", "marker", "player", "fragment"
    property int selectedMarker: -1
    property string selectedPlayer: ""
    property bool developer: false         // режим розробника: ідентифікатори правил і похідні значення

    signal openDemoRequested()
    signal addFolderRequested()
    signal toast(string text, string severity)
    signal consentRequested(bool forLibrary)
    signal modelInstallRequested()
    signal voiceEngineInstallRequested()
    signal reportRequested()
    signal resetRequested()
    signal aboutRequested()

    function goTo(p) { page = p }
    // Підстановка в перекладений рядок з «{}» (як trf у ядрі): Ui.fmt(qsTr("Вибрано: {}"), names)
    function fmt(s) {
        let i = 1
        const a = arguments
        return s.replace(/\{\}/g, () => i < a.length ? String(a[i++]) : "")
    }
    function select(kind, value) {
        selection = kind
        if (kind === "marker") selectedMarker = value
        if (kind === "player") selectedPlayer = value
    }
    // Дії з зауважень перевірки (ActionId у ядрі)
    function runAction(action) {
        switch (action) {
        case "openDemo": openDemoRequested(); break
        case "detectGame": Env.rescan(); settingsCategory = "game"; goTo("settings"); break
        case "installWhisperModel": modelInstallRequested(); break
        case "installWhisperCli": goTo("ai"); break
        case "installVoiceEngine": voiceEngineInstallRequested(); break
        case "configureTranslator": goTo("ai"); break
        case "configureElevenLabs": goTo("ai"); break
        case "confirmVoiceConsent": consentRequested(false); break
        case "closeGame": Env.checkGameRunning(); break
        }
    }
    function actionLabel(action) {
        switch (action) {
        case "openDemo": return qsTr("Відкрити демо")
        case "detectGame": return qsTr("Знайти гру")
        case "installWhisperModel": return qsTr("Встановити модель")
        case "installWhisperCli": return qsTr("Докладніше")
        case "installVoiceEngine": return qsTr("Встановити")
        case "configureTranslator": return qsTr("Налаштувати")
        case "configureElevenLabs": return qsTr("Налаштувати")
        case "confirmVoiceConsent": return qsTr("Підтвердити згоду")
        case "closeGame": return qsTr("Перевірити ще раз")
        }
        return ""
    }
}
