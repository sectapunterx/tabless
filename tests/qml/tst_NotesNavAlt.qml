// Dogfood 2026-10-30, through Main: Alt ↓ / Alt ↑ open the next / previous
// note in the list's order from anywhere in Notes, the note's text included;
// the draft is kept, the caret goes back to where it was in that note, and
// the keys are drawn beside the neighbouring notes while Alt is held.
// Ctrl+PgDn / PgUp still do the same.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "NotesNavAlt"
    when: windowShown

    property var win: null
    property var made: []

    function initTestCase() {
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 939;
        wait(1200);
    }
    function cleanupTestCase() {
        AppController.currentView = "board";
        if (tc.win) tc.win.destroy();
    }
    function init() {
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}"); } catch (e) { s = {}; }
        s.notes = Object.assign({}, s.notes || {}, { view: "rich" });
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function cleanup() {
        keyRelease(Qt.Key_Alt);
        AppController.currentView = "board";
        for (let i = 0; i < tc.made.length; i++) AppController.deleteNote(tc.made[i]);
        tc.made = [];
        AppController.clearPendingUndo();
    }

    function editor() { return findChild(tc.win.contentItem, "notesEditor"); }
    function listPane() { return findChild(tc.win.contentItem, "notes-list-pane"); }

    // Three notes with text, in Notes, the keyboard in the middle one's text.
    function threeNotes() {
        AppController.currentView = "notes";
        const tag = String(Date.now()).slice(-6);
        const a = AppController.newNote("Nav A " + tag);
        const b = AppController.newNote("Nav B " + tag);
        const c = AppController.newNote("Nav C " + tag);
        tc.made.push(a, b, c);
        tryVerify(function () { return tc.listPane() !== null; });
        const ids = tc.listPane().visibleIds();
        const ia = ids.indexOf(a), ib = ids.indexOf(b), ic = ids.indexOf(c);
        verify(ia >= 0 && ib >= 0 && ic >= 0);
        // The list's own order decides which is "next".
        const order = [{ id: a, at: ia }, { id: b, at: ib }, { id: c, at: ic }].sort((x, y) => x.at - y.at);
        AppController.activeNoteId = order[1].id;
        tc.win.requestActivate();
        const ed = tc.editor();
        tryVerify(function () { return ed.visible; });
        ed.forceActiveFocus();
        tryVerify(function () { return ed.activeFocus; });
        return { first: order[0].id, middle: order[1].id, last: order[2].id, ids: ids };
    }

    function test_alt_arrows_step_through_the_list_from_the_text() {
        const n = threeNotes();
        const ed = tc.editor();
        const nextInList = n.ids[n.ids.indexOf(n.middle) + 1];
        const prevInList = n.ids[n.ids.indexOf(n.middle) - 1];
        // A draft typed and not yet saved goes with the note.
        ed.cursorPosition = ed.length;
        keyClick(Qt.Key_X);
        keyClick(Qt.Key_Y);
        const middleText = ed.text;
        const caret = 3;
        ed.cursorPosition = caret;

        keyClick(Qt.Key_Down, Qt.AltModifier);
        tryCompare(AppController, "activeNoteId", nextInList);
        verify(ed.activeFocus, "the keyboard left the text");
        keyClick(Qt.Key_Up, Qt.AltModifier);
        tryCompare(AppController, "activeNoteId", n.middle);
        compare(ed.text, middleText, "the draft was lost");
        compare(ed.cursorPosition, caret, "the caret did not come back");
        verify(ed.activeFocus);
        keyClick(Qt.Key_Up, Qt.AltModifier);
        tryCompare(AppController, "activeNoteId", prevInList);
        // Ctrl+PgDn still does the same.
        keyClick(Qt.Key_PageDown, Qt.ControlModifier);
        tryCompare(AppController, "activeNoteId", n.middle);
    }

    function test_alt_arrows_from_the_list() {
        const n = threeNotes();
        const pane = tc.listPane();
        pane.focusList();
        tryVerify(function () { return !tc.editor().activeFocus; });
        const nextInList = n.ids[n.ids.indexOf(n.middle) + 1];
        keyClick(Qt.Key_Down, Qt.AltModifier);
        tryCompare(AppController, "activeNoteId", nextInList);
    }

    function test_neighbours_show_their_keys_while_alt_is_held() {
        const n = threeNotes();
        const pane = tc.listPane();
        compare(pane.stepHints, false);
        keyPress(Qt.Key_Alt, Qt.AltModifier);
        tryCompare(pane, "stepHints", true);
        const prevInList = n.ids[n.ids.indexOf(n.middle) - 1];
        const nextInList = n.ids[n.ids.indexOf(n.middle) + 1];
        compare(pane._stepNeighbours.prev, prevInList);
        compare(pane._stepNeighbours.next, nextInList);
        keyRelease(Qt.Key_Alt, Qt.NoModifier);
        tryCompare(pane, "stepHints", false);
    }
}
