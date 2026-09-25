// Шлях до файлу чи теки: поле і кнопка «Вибрати» (стандартний діалог системи).
// mode: open (файл), save (новий файл), folder (тека).
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import Gmdr
import Gmdr.Ui

SettingRow {
    id: r
    property string mode: "open"
    property var nameFilters: []
    property string placeholder: ""
    property string dialogTitle: label
    RowLayout {
        spacing: Theme.s2
        Field {
            id: f
            Layout.fillWidth: true
            text: (Config.revision, String(Config.value(r.key) || ""))
            placeholderText: r.placeholder
            severity: r.st.severity || ""
            onEditingFinished: Config.set(r.key, text)
            Accessible.name: r.label
        }
        IconBtn {
            iconName: "folder"
            tip: qsTr("Вибрати…")
            onClicked: r.mode === "folder" ? folderDialog.open() : fileDialog.open()
        }
        IconBtn {
            iconName: "close"
            tip: qsTr("Очистити")
            visible: f.text !== ""
            onClicked: Config.set(r.key, "")
        }
    }
    FileDialog {
        id: fileDialog
        title: r.dialogTitle
        fileMode: r.mode === "save" ? FileDialog.SaveFile : FileDialog.OpenFile
        nameFilters: r.nameFilters.length > 0 ? r.nameFilters : [qsTr("Усі файли (*)")]
        currentFolder: Shell.folderUrl(f.text)
        onAccepted: Config.set(r.key, Shell.localPath(selectedFile))
    }
    FolderDialog {
        id: folderDialog
        title: r.dialogTitle
        currentFolder: Shell.folderUrl(f.text)
        onAccepted: Config.set(r.key, Shell.localPath(selectedFolder))
    }
}
