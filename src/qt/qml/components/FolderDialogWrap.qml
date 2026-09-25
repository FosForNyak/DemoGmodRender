// Вибір теки (стандартний діалог системи) з результатом-шляхом.
import QtQuick
import QtQuick.Dialogs
import Gmdr

Item {
    id: w
    property string title: ""
    property string startFolder: ""
    signal chosen(string path)
    function open() { dlg.open() }
    FolderDialog {
        id: dlg
        title: w.title
        currentFolder: w.startFolder !== "" ? Shell.fileUrl(w.startFolder) : ""
        onAccepted: w.chosen(Shell.localPath(selectedFolder))
    }
}
