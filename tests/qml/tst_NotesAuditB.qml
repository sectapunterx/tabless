// Notes, B-tier audit findings (2026-09-30): the editor's keyboard exits
// (SHELL-18), the notes.* commands the palette offers (SHELL-1) and remote
// image consent (KNOW-18).
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "NotesAuditB"
    when: windowShown
    visible: true
    width: 900
    height: 500

    Item { id: host; anchors.fill: parent }

    property var made: []

    function note(title) {
        const id = AppController.newNote(title);
        tc.made.push(id);
        return id;
    }

    function cleanup() {
        for (let i = 0; i < tc.made.length; i++) AppController.deleteNote(tc.made[i]);
        tc.made = [];
        AppController.clearPendingUndo();
    }

    function make() {
        const nv = createTemporaryQmlObject('import TodoCpp; NotesView { anchors.fill: parent }', host);
        verify(nv !== null);
        tryVerify(function () { return nv._loadedOnce; });
        return nv;
    }

    function within(item, root) {
        for (let p = item; p; p = p.parent) if (p === root) return true;
        return false;
    }

    // SHELL-18: Tab indents in the notes editor, and nothing else let the
    // keyboard out of it.
    function test_escape_leaves_the_editor_for_the_list() {
        note("Esc target");
        const nv = make();
        verify(nv._listShown, "setup: the list is beside the editor");
        const ed = findChild(nv, "notesEditor");
        ed.forceActiveFocus();
        verify(ed.activeFocus);

        keyClick(Qt.Key_Escape);

        verify(!ed.activeFocus, "Esc kept the focus in the editor");
        verify(findChild(nv, "note-list").activeFocus, "Esc did not land on the list of notes");
    }

    // F6 / Shift+F6 leave the editor. Ctrl+Tab used to as well; since
    // dogfood 2026-10-20 it is the recent notes (tst_NotesRecent) — and
    // still types nothing into the note.
    function test_ctrl_tab_and_f6_leave_the_editor() {
        note("Ctrl+Tab target");
        const nv = make();
        const ed = findChild(nv, "notesEditor");
        const shown = AppController.activeNoteId;
        const before = AppController.noteBody(shown);

        ed.forceActiveFocus();
        keyClick(Qt.Key_F6);
        verify(!ed.activeFocus, "F6 kept the focus in the editor");

        ed.forceActiveFocus();
        keyClick(Qt.Key_F6, Qt.ShiftModifier);
        verify(!ed.activeFocus, "Shift+F6 kept the focus in the editor");

        ed.forceActiveFocus();
        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        if (nv.switcher.opened) nv.switcher.cancel();
        compare(AppController.noteBody(shown), before, "Ctrl+Tab must not type into the note");
    }

    // Tab still indents: the way out is a different key, not a lost feature.
    function test_tab_still_indents() {
        note("Indent");
        const nv = make();
        const ed = findChild(nv, "notesEditor");
        ed.forceActiveFocus();
        ed.cursorPosition = ed.length;
        keyClick(Qt.Key_Minus);
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_X);
        const before = ed.text;
        keyClick(Qt.Key_Tab);
        verify(ed.activeFocus);
        verify(ed.text !== before, "Tab no longer indents");
    }

    // SHELL-1: the palette lists the notes.* actions; each one runs.
    function test_notes_commands_run() {
        note("Cmd A");
        const nv = make();
        const count = AppController.notes.rowCount();

        nv.runNotesCommand("new");
        const fresh = AppController.activeNoteId;
        tc.made.push(fresh);
        compare(AppController.notes.rowCount(), count + 1, "notes.new did not make a note");
        // Written in, so leaving it keeps it (an untouched one goes, IDIOT-KNOW-14).
        AppController.notesState = AppController.notesState + "typed";

        const shown = nv._listShown;
        nv.runNotesCommand("toggleList");
        compare(nv._listShown, !shown, "notes.toggleList did not toggle");
        nv.runNotesCommand("toggleList");

        const ids = findChild(nv, "notes-list-pane").visibleIds();
        verify(ids.length >= 2);
        AppController.activeNoteId = ids[0];
        nv.runNotesCommand("next");
        compare(AppController.activeNoteId, ids[1], "notes.next did not move");
        nv.runNotesCommand("prev");
        compare(AppController.activeNoteId, ids[0], "notes.prev did not move");
    }

    // KNOW-18: "Load images" was a switch on the one MdDocument every note
    // shares, so a note opened after it fetched its remote images unasked.
    function test_remote_images_are_allowed_for_one_note_only() {
        const a = note("Trusted");
        const b = note("Imported");
        AppController.activeNoteId = a;
        const nv = make();
        const doc = findChild(nv, "notesPreview").document;
        doc.allowRemoteImages = true;

        AppController.activeNoteId = b;

        verify(!doc.allowRemoteImages, "consent carried over to another note");
    }
}
