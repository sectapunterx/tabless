pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import TodoCpp

// Ctrl+Tab in Notes (dogfood 2026-10-20): the notes opened last, as a
// browser's tabs or an editor's files. A tap of Ctrl+Tab goes to the note
// before and a second tap comes back; with Ctrl held, every Tab steps one
// further (Shift+Tab one back) and letting go of Ctrl opens the marked
// note. Esc keeps the note that was open. The list shows itself only after
// a moment, so a quick tap does not flash it.
Popup {
    id: root
    objectName: "note-switcher"

    // [{ id, title, folder, fresh }], the open note first.
    property var entries: []
    property int index: 0
    // The modifiers of the key that opened it: letting go of all of them
    // opens the marked note. None (a bare key bound): it opens at once.
    property int holdModifiers: Qt.ControlModifier
    property bool _drawn: false
    property Item _returnFocus: null

    signal chosen(string noteId)

    // False when there is nothing to switch to (no other note).
    function start(direction: int, holdMods: int): bool {
        const list = AppController.recentNotes(12);
        if (list.length < 2) return false;
        root.entries = list;
        root.holdModifiers = holdMods;
        root.index = direction < 0 ? list.length - 1 : 1;
        if (holdMods === 0) {
            root.chosen(list[root.index].id);
            return true;
        }
        root._returnFocus = root.parent && root.parent.Window ? root.parent.Window.activeFocusItem : null;
        root._drawn = false;
        drawTimer.restart();
        root.open();
        return true;
    }
    function step(delta: int) {
        if (root.entries.length === 0) return;
        root.index = (root.index + delta + root.entries.length) % root.entries.length;
    }
    function commit() {
        if (!root.opened) return;
        const e = root.entries[root.index];
        root._returnFocus = null;
        root.close();
        if (e) root.chosen(e.id);
    }
    function cancel() {
        if (!root.opened) return;
        const back = root._returnFocus;
        root._returnFocus = null;
        root.close();
        if (back && back.visible) back.forceActiveFocus();
    }
    function _holdKey(key: int): bool {
        return (key === Qt.Key_Control && (root.holdModifiers & Qt.ControlModifier))
            || (key === Qt.Key_Alt && (root.holdModifiers & Qt.AltModifier))
            || (key === Qt.Key_Meta && (root.holdModifiers & Qt.MetaModifier));
    }

    Timer {
        id: drawTimer
        interval: 150
        onTriggered: root._drawn = true
    }

    modal: false
    focus: true
    // Esc is the list's own (cancel); a click elsewhere puts it away too.
    closePolicy: Popup.CloseOnPressOutside
    onClosed: drawTimer.stop()
    opacity: root._drawn ? 1 : 0
    padding: Theme.spXs
    width: Math.min(Theme.px(420), (root.parent ? root.parent.width : Theme.px(420)) - 2 * Theme.spLg)
    x: root.parent ? Math.round((root.parent.width - root.width) / 2) : 0
    y: Theme.px(72)

    background: PopupSurface {}

    contentItem: Column {
        id: content
        focus: true
        spacing: 0

        // Every chord is the list's while it is up: Ctrl+K or Ctrl+W behind
        // it would act on a note nobody can see.
        Keys.onShortcutOverride: (event) => { event.accepted = true; }
        Keys.onPressed: (event) => {
            event.accepted = true;
            const back = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) !== 0;
            switch (event.key) {
            case Qt.Key_Tab:
            case Qt.Key_Backtab:
                root.step(back ? -1 : 1);
                return;
            case Qt.Key_Down:
                root.step(1);
                return;
            case Qt.Key_Up:
                root.step(-1);
                return;
            case Qt.Key_Return:
            case Qt.Key_Enter:
                root.commit();
                return;
            case Qt.Key_Escape:
                root.cancel();
                return;
            case Qt.Key_Control:
            case Qt.Key_Shift:
            case Qt.Key_Alt:
            case Qt.Key_Meta:
                return;
            }
            // Any other key with the hold let go of: the marked note, as if
            // the release had come (it went to another window).
            if ((event.modifiers & root.holdModifiers) === 0) root.commit();
        }
        // Letting go of the held key (Ctrl) opens the marked note. Only that
        // release: a Tab's own release says nothing about Ctrl.
        Keys.onReleased: (event) => {
            event.accepted = true;
            if (root._holdKey(event.key)) root.commit();
        }

        Text {
            width: content.width
            leftPadding: Theme.spSm
            topPadding: Theme.spXs
            bottomPadding: Theme.spXs
            text: I18n.t("notes.switcher.title")
            color: Theme.textDim
            font.pixelSize: Theme.fsXs
        }
        Repeater {
            model: root.entries
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool marked: row.index === root.index
                objectName: "note-switcher-row"
                width: content.width
                height: Theme.px(30)
                radius: Theme.radiusSm
                color: row.marked ? Theme.rowHighlight : "transparent"
                Rectangle {
                    visible: row.marked && Style.fills
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(3)
                    height: parent.height - 2 * Theme.spXs
                    radius: Theme.radiusXs
                    color: Theme.focusRing
                }
                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spSm
                    anchors.right: folder.left
                    anchors.rightMargin: Theme.spSm
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.title
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: Theme.fsMd
                    font.weight: row.marked ? Theme.fwTitle : Theme.fwBody
                }
                Text {
                    id: folder
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spSm
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.folder
                    color: Theme.textDim
                    font.pixelSize: Theme.fsXs
                }
                TapHandler {
                    onTapped: {
                        root.index = row.index;
                        root.commit();
                    }
                }
            }
        }
    }
}
