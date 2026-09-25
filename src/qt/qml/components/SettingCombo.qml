// Вибір значення налаштування: варіанти (доступні й ні, з причинами) — з перевірки.
import QtQuick
import Gmdr
import Gmdr.Ui

SettingRow {
    id: r
    property var extraOptions: []          // варіанти, яких немає в перевірці (власні підписи)
    property string placeholder: ""
    Combo {
        options: (r.st.options && r.st.options.length > 0 ? r.st.options : []).concat(r.extraOptions)
        selected: (Config.revision, Config.value(r.key))
        severity: r.st.severity || ""
        placeholder: r.placeholder
        onChosen: (v) => Config.set(r.key, v)
        Accessible.name: r.label
    }
}
