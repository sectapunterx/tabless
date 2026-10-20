// Dogfood 2026-10-20, through Main: going to Notes opens a new, empty note
// with the caret in it (and an empty one left behind goes again);
// Ctrl+Tab / Ctrl+Shift+Tab walk the notes opened last, as a browser's
// tabs do — a tap toggles two, Ctrl held walks further, Esc keeps the note;
// Ctrl+N is a note in Notes, Ctrl+W closes the note.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "NotesRecent"
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
        // The rich view, whatever an earlier run left.
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}"); } catch (e) { s = {}; }
        s.notes = Object.assign({}, s.notes || {}, { view: "rich" });
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function cleanup() {
        const sw = tc.switcher();
        if (sw && sw.opened) sw.cancel();
        keyRelease(Qt.Key_Shift);
        keyRelease(Qt.Key_Control);
        AppController.currentView = "board";
        for (let i = 0; i < tc.made.length; i++) AppController.deleteNote(tc.made[i]);
        tc.made = [];
        AppController.clearPendingUndo();
    }

    function editor() { return findChild(tc.win.contentItem, "notesEditor"); }
    function switcher() {
        const v = tc.win.activeViewItem();
        return v && v.switcher ? v.switcher : null;
    }
    function titleOf(id) {
        const list = AppController.recentNotes(0);
        for (let i = 0; i < list.length; i++) if (list[i].id === id) return list[i].title;
        return "";
    }
    // Three notes opened A, B, C in that order, in Notes, C open.
    function threeNotes() {
        AppController.currentView = "notes";
        const tag = String(Date.now()).slice(-6);
        const a = AppController.newNote("Recent A " + tag);
        const b = AppController.newNote("Recent B " + tag);
        const c = AppController.newNote("Recent C " + tag);
        tc.made.push(a, b, c);
        tc.win.requestActivate();
        const ed = tc.editor();
        ed.forceActiveFocus();
        tryVerify(function () { return ed.activeFocus; });
        return { a: a, b: b, c: c };
    }
    function ctrlTab(times, shift) {
        keyPress(Qt.Key_Control);
        for (let i = 0; i < times; i++)
            // A press only: QTest's click lets go of the modifiers after the key,
            // and letting go of Ctrl is what opens the marked note.
            keyPress(shift ? Qt.Key_Backtab : Qt.Key_Tab, Qt.ControlModifier | (shift ? Qt.ShiftModifier : 0));
            keyRelease(shift ? Qt.Key_Backtab : Qt.Key_Tab, Qt.NoModifier);
            if (shift) keyRelease(Qt.Key_Shift, Qt.ControlModifier);
    }

    function test_going_to_notes_opens_an_empty_note_with_the_caret() {
        AppController.currentView = "board";
        wait(100);
        const before = AppController.notes.rowCount();
        tc.win.requestActivate();
        tc.win.focusActiveView();
        keyClick(Qt.Key_3, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "notes");
        const fresh = AppController.activeNoteId;
        verify(AppController.isFreshNote(fresh), "Notes did not open on a new note");
        compare(AppController.notesState, "");
        tryVerify(function () { return tc.editor().activeFocus; }, 2000, "the caret is not in the new note");

        // Typed, then straight to another screen: the note and its name stay.
        keyClick(Qt.Key_P);
        keyClick(Qt.Key_L);
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_N);
        AppController.currentView = "board";
        verify(AppController.notes.indexOfId(fresh) >= 0, "the typed note was dropped");
        tc.made.push(fresh);
        compare(tc.titleOf(fresh).toLowerCase(), "plan");
        compare(AppController.notes.rowCount(), before + 1);
    }

    function test_an_empty_note_left_goes_and_two_visits_make_one() {
        AppController.currentView = "board";
        const before = AppController.notes.rowCount();
        AppController.currentView = "notes";
        compare(AppController.notes.rowCount(), before + 1);
        AppController.currentView = "today";
        compare(AppController.notes.rowCount(), before, "an empty note piled up");
        AppController.currentView = "notes";
        AppController.currentView = "notes";
        compare(AppController.notes.rowCount(), before + 1);
    }

    function test_ctrl_tab_tap_toggles_two_notes() {
        const n = tc.threeNotes();
        tc.ctrlTab(1, false);
        keyRelease(Qt.Key_Control);
        compare(AppController.activeNoteId, n.b);
        tryVerify(function () { return tc.editor().activeFocus; }, 2000, "the caret did not come back to the text");
        tc.ctrlTab(1, false);
        keyRelease(Qt.Key_Control);
        compare(AppController.activeNoteId, n.c, "a second tap goes back");
    }

    function test_ctrl_held_walks_further_and_shift_walks_back() {
        const n = tc.threeNotes();
        tc.ctrlTab(2, false);
        verify(tc.switcher().opened, "no list while Ctrl is held");
        compare(AppController.activeNoteId, n.c, "switched before Ctrl was let go");
        keyRelease(Qt.Key_Control);
        compare(AppController.activeNoteId, n.a);
        verify(!tc.switcher().opened);
        // From A the list is A, C, B …: Ctrl+Shift+Tab starts at the far end.
        tc.ctrlTab(1, true);
        const sw = tc.switcher();
        compare(sw.entries[sw.index].id, sw.entries[sw.entries.length - 1].id);
        keyRelease(Qt.Key_Control);
        verify(AppController.activeNoteId !== n.a);
    }

    function test_esc_keeps_the_note() {
        const n = tc.threeNotes();
        tc.ctrlTab(2, false);
        keyClick(Qt.Key_Escape, Qt.ControlModifier);
        verify(!tc.switcher().opened);
        keyRelease(Qt.Key_Control);
        compare(AppController.activeNoteId, n.c);
        tryVerify(function () { return tc.editor().activeFocus; }, 2000, "Esc lost the caret");
    }

    function test_ctrl_tab_inside_a_table_cell() {
        const n = tc.threeNotes();
        const table = "| a | b |\n|---|---|\n| 1 | 2 |\n";
        AppController.setNoteBody(n.c, table);
        const ed = tc.editor();
        tryCompare(ed, "text", table);
        ed.forceActiveFocus();
        ed.cursorPosition = 2;   // in the first cell
        tryVerify(function () { return ed.activeFocus; });
        wait(50);
        const recent = AppController.recentNoteIds();
        compare(recent[0], n.c);
        tc.ctrlTab(1, false);
        keyRelease(Qt.Key_Control);
        compare(AppController.activeNoteId, recent[1], "Ctrl+Tab was eaten by the table (c=" + n.c + " b=" + n.b
                + " focus=" + (tc.win.activeFocusItem ? tc.win.activeFocusItem.objectName : "") + ")");
        compare(AppController.noteBody(n.c), table, "Ctrl+Tab also moved or changed the table");
    }

    function test_ctrl_tab_does_nothing_behind_the_slash_menu() {
        const n = tc.threeNotes();
        const ed = tc.editor();
        ed.cursorPosition = ed.length;
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Slash, Qt.NoModifier);
        const menu = findChild(tc.win.activeViewItem(), "notes-slash-menu");
        tryVerify(function () { return menu.opened; }, 2000, "no slash menu");
        tc.ctrlTab(1, false);
        keyRelease(Qt.Key_Control);
        compare(AppController.activeNoteId, n.c, "switched the note behind the open menu");
        keyClick(Qt.Key_Escape);
    }

    function test_ctrl_n_in_notes_is_a_note() {
        tc.threeNotes();
        AppController.notesState = "something written";
        const before = AppController.notes.rowCount();
        keyClick(Qt.Key_N, Qt.ControlModifier);
        tryVerify(function () { return AppController.notes.rowCount() === before + 1; }, 2000, "Ctrl+N made no note");
        verify(AppController.isFreshNote(AppController.activeNoteId));
        compare(AppController.currentView, "notes");
    }

    // The keys every text editor has, in the note's text: nothing of the
    // app's may take them there.
    function test_universal_editing_keys() {
        const n = tc.threeNotes();
        AppController.setNoteBody(n.c, "alpha beta gamma");
        const ed = tc.editor();
        tryCompare(ed, "text", "alpha beta gamma");
        ed.forceActiveFocus();
        keyClick(Qt.Key_End, Qt.ControlModifier);
        compare(ed.cursorPosition, ed.length);
        keyClick(Qt.Key_Backspace, Qt.ControlModifier);
        compare(ed.text, "alpha beta ");
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        compare(ed.text, "alpha beta gamma");
        keyClick(Qt.Key_Y, Qt.ControlModifier);
        compare(ed.text, "alpha beta ");
        keyClick(Qt.Key_Home, Qt.ControlModifier);
        compare(ed.cursorPosition, 0);
        keyClick(Qt.Key_A, Qt.ControlModifier);
        compare(ed.selectedText, "alpha beta ");
        compare(AppController.currentView, "notes");
    }

    function test_ctrl_w_closes_to_the_note_before() {
        const n = tc.threeNotes();
        keyClick(Qt.Key_W, Qt.ControlModifier);
        compare(AppController.activeNoteId, n.b);
        verify(AppController.notes.indexOfId(n.c) >= 0, "closing deleted the note");
    }
}
