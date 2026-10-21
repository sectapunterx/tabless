import QtQuick
import QtQuick.Layouts
import TodoCpp

// A group of settings (APP-172, dogfood 2026-10-21): a small dim heading
// and, under it, a card that holds the SettingsRows, separated by hairlines.
// One section page is two to four of these, then the fold.
//
//   SettingsGroup {
//       title: I18n.t(<key>)
//       SettingsRow { label: …; hint: …; <control> }
//       SettingsRow { … }
//   }
//
// - Inside another card (the fold's "Тонкая настройка") it draws no second
//   card: the heading and the rows only.
// - `description` is one dim line under the heading; most groups need none
//   (the "?" of a row explains).
// - `framed` is kept for callers and draws nothing of its own.
// - `danger: true` tints the heading.
ColumnLayout {
    id: group

    property string title: ""
    property string description: ""
    property bool framed: false
    property bool danger: false
    // Set when the group sits in another card (found on creation).
    property bool nested: false
    // What a group or row inside asks to know that it is in a card.
    readonly property bool _settingsCard: true
    default property alias rows: rowsCol.data
    // The card's inner margins.
    readonly property int hpad: Theme.sp2xl
    readonly property int vpad: Theme.spXs

    Layout.fillWidth: true
    spacing: Theme.spSm

    // Untyped, so a marker another type declares can be read off it.
    readonly property var _up: group.parent
    Component.onCompleted: {
        for (let p = group._up; p; p = p.parent) {
            if (p._settingsCard === true) {
                group.nested = true;
                return;
            }
        }
    }

    // The heading, small and dim: the card under it is what reads.
    ColumnLayout {
        visible: group.title.length > 0 || group.description.length > 0
        Layout.fillWidth: true
        Layout.topMargin: group.nested ? Theme.spLg : 0
        Layout.leftMargin: group.nested ? 0 : Theme.spXs
        spacing: Theme.sp2xs
        Text {
            objectName: "settings-group-title"
            visible: group.title.length > 0
            Layout.fillWidth: true
            text: group.title
            color: group.danger ? Theme.danger : Theme.textDim
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
            elide: Text.ElideRight
        }
        Text {
            visible: group.description.length > 0
            Layout.fillWidth: true
            text: group.description
            color: Theme.textDim
            font.pixelSize: Theme.fsSm
            wrapMode: Text.WordWrap
        }
    }

    Rectangle {
        id: card
        objectName: "settings-group-card"
        Layout.fillWidth: true
        implicitHeight: rowsCol.implicitHeight + 2 * group.vpad
        radius: group.nested ? 0 : Theme.radiusXl
        color: group.nested ? "transparent" : Theme.surfaceCard
        border.width: group.nested ? 0 : 1
        border.color: Theme.cardBorder

        ColumnLayout {
            id: rowsCol
            // The last row here draws no hairline (SettingsRow): the card's
            // edge closes it. Nested, the rows of the card go on below.
            readonly property bool _cardRows: !group.nested
            x: group.nested ? 0 : group.hpad
            y: group.vpad
            width: card.width - 2 * x
            spacing: 0
        }
    }
}
