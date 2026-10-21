import QtQuick
import QtQuick.Controls.Basic
import TodoCpp

// A themed on/off switch with its label (heap 2): the stock Basic track was
// 1.09:1 against a popup (DES-20). The label is the accessible name. The
// track and knob are SwitchTrack, the one switch of the app (mockup 5A).
Switch {
    id: sw

    font.family: Theme.fontUi
    font.pixelSize: Theme.fsSm
    spacing: Theme.spMd
    Accessible.name: sw.text

    indicator: SwitchTrack {
        x: sw.leftPadding
        y: (sw.height - height) / 2
        checked: sw.checked
        focusShown: sw.visualFocus
    }
    contentItem: Text {
        leftPadding: sw.indicator.width + sw.spacing
        verticalAlignment: Text.AlignVCenter
        text: sw.text
        font: sw.font
        color: sw.enabled ? Theme.text : Theme.textDim
    }
}
