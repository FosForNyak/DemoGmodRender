// Текст для налаштування (записується, коли поле втрачає фокус або Enter).
import QtQuick
import Gmdr
import Gmdr.Ui

SettingRow {
    id: r
    property string placeholder: ""
    property bool password: false
    Field {
        text: (Config.revision, String(Config.value(r.key) || ""))
        placeholderText: r.placeholder
        severity: r.st.severity || ""
        echoMode: r.password ? TextInput.Password : TextInput.Normal
        onEditingFinished: Config.set(r.key, text)
        Accessible.name: r.label
    }
}
