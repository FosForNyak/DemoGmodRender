// Текст з токенами теми. role: title / section / body / secondary / muted / meta / mono
import QtQuick
import Gmdr.Ui

Text {
    property string role: "body"
    color: role === "secondary" ? Theme.textSecondary : role === "muted" || role === "meta" ? Theme.textMuted : Theme.text
    font.family: role === "mono" ? Theme.monoFamily : Theme.fontFamily
    font.pixelSize: role === "title" ? Theme.fontTitle : role === "section" ? Theme.fontSection
                    : role === "meta" ? Theme.fontMeta : role === "secondary" || role === "muted" ? Theme.fontSmall : Theme.fontBody
    font.weight: role === "title" || role === "section" ? Font.DemiBold : Font.Normal
    elide: Text.ElideRight
    textFormat: Text.PlainText
    verticalAlignment: wrapMode === Text.NoWrap ? Text.AlignVCenter : Text.AlignTop
}
