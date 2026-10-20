import QtQuick
import TodoCpp

// Where a coach mark (CoachMark.qml) is made: inside the item it is about,
// only while AppController.coachMark is this one, and opened only when the
// window is quiet — nobody typing, no focus mode, no modal up
// (Main.coachMarksQuiet). Put one in the anchor:
//
//   CoachMarkHost {
//       markId: "trackerMove"
//       when: card.task.id === AppController.coachMark.taskId
//       sourceComponent: TrackerMoveCoachmark { anchor: card }
//   }
//
// An anchor that goes away before its coach mark could show hands it back
// unread (dropCoachMark), so it can come another time.
Loader {
    id: host
    required property string markId
    // The anchor's own say: this card, and it is on screen. (The host's own
    // `visible` is not asked: it may sit hidden in a Flow.)
    property bool when: true
    // Shows while the anchor's own field has the caret (quick capture's
    // date chips): the card takes no keys then.
    property bool overTyping: false
    // Lives in the task document or a meeting panel, which the view's
    // coach marks wait behind.
    property bool inPanel: false
    // Goes, read, when its anchor goes (quick capture closed with Esc under
    // it); otherwise one on screen stays until read.
    property bool closesWithAnchor: false
    // The item the card stands by, handed to it when it is made (a
    // component cannot always name it itself).
    property Item anchorItem: null

    readonly property bool _mine: String(AppController.coachMark.id || "") === host.markId
    // Main.qml's window (its coachMarksQuiet…), or none in a test.
    readonly property var _win: host.Window.window
    // The CoachMark made, untyped: the component is the caller's.
    readonly property var _card: host.item
    readonly property bool _quiet: {
        const w = host._win;
        if (host.overTyping || !w || w.coachMarksQuiet === undefined) return true;
        return host.inPanel ? w.coachMarksQuietInPanel : w.coachMarksQuiet;
    }

    // Once on screen it stays until read, even if `when` goes (the toast it
    // stood by timed out): a card that vanished unread would come back.
    property bool _held: false
    on_MineChanged: if (!host._mine) host._held = false
    onWhenChanged: if (!host.when && host.closesWithAnchor && host._card && host._card.opened) host._card.close()
    active: host._mine && (host.when || host._held)
    onLoaded: {
        if (host.anchorItem && host._card) host._card.anchor = host.anchorItem;
        Qt.callLater(host._tryOpen);
    }
    // Later, not inside the quiet binding's own evaluation: opening moves
    // the focus, which that binding reads (a binding loop otherwise).
    on_QuietChanged: Qt.callLater(host._tryOpen)

    function _tryOpen() {
        const p = host._card;
        if (p && !p.opened && !p.visible && host.when && host._quiet && host._mine && !AppController.coachMark.shown) {
            host._held = true;
            p.open();
        }
    }

    // Still on offer but the anchor is gone (the card filtered out, the
    // document closed) and it never showed: back on the shelf, unread.
    onActiveChanged: {
        if (!host.active && host._mine && !AppController.coachMark.shown)
            Qt.callLater(function () {
                if (!host.active && String(AppController.coachMark.id || "") === host.markId)
                    AppController.dropCoachMark(host.markId);
            });
    }
}
