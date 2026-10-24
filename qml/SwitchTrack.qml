import QtQuick
import TodoCpp

// The one on/off switch of the app (mockup 5A, dogfood 2026-10-22): a 36×20
// pill. On: the accent track, a 16px knob in the text-on-accent colour 2px
// in from the right. Off: no fill, a hairline track, a dim knob 2px in from
// the left. Only the drawing: AppSwitch, a settings row or a card's corner
// owns the click and the keyboard.
Rectangle {
    id: track

    property bool checked: false
    // A focus ring drawn on the track itself (AppSwitch); a settings row
    // draws its own FocusRing around the track instead.
    property bool focusShown: false

    implicitWidth: Theme.px(36)
    implicitHeight: Theme.px(20)
    radius: height / 2
    color: track.checked ? Theme.accent : "transparent"
    border.width: track.focusShown ? 2 : (track.checked ? 0 : 1)
    border.color: track.focusShown ? Theme.focusRing : Theme.switchOffLine

    Rectangle {
        id: knob
        objectName: "switch-knob"
        readonly property int inset: Theme.px(2)
        width: track.height - 2 * knob.inset
        height: width
        radius: width / 2
        y: knob.inset
        x: track.checked ? track.width - width - knob.inset : knob.inset
        color: track.checked ? Theme.textOnAccent : Theme.switchKnobOff
        Behavior on x { NumberAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
    }
}
