// Notes, dogfood 2026-10-30: a numbered list counts again after an edit, the
// renumbering and the edit undo together (MdList), Alt ↑ / Alt ↓ to the
// neighbouring notes (NotesNav), Ctrl+F in a note's text (Find), and a
// picture on the clipboard pasted as a picture (Paste).
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "NotesDogfood1030"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Item { id: host; anchors.fill: parent }

    function make() {
        const nv = Qt.createQmlObject('import TodoCpp; NotesView { anchors.fill: parent }', host, "tst_NotesDogfood1030.make");
        verify(nv !== null);
        tryVerify(function () { return nv._loadedOnce; });
        return nv;
    }

    function editorOf(nv) {
        const ed = findChild(nv, "notesEditor");
        verify(ed !== null, "notesEditor not found");
        return ed;
    }

    function init() {
        AppController.notesState = "";
    }

    // ── MdList ───────────────────────────────────────────────────

    function test_deleting_an_item_renumbers_and_undoes_in_one_step() {
        const nv = make();
        nv.viewMode = "edit";
        const ed = editorOf(nv);
        const before = "4. Logs\n5. Restart\n6. Compare\n7. Write";
        ed.text = before;
        ed.forceActiveFocus();
        const from = before.indexOf("5.");
        ed.select(from, before.indexOf("6."));
        keyClick(Qt.Key_Backspace);
        tryCompare(ed, "text", "4. Logs\n5. Compare\n6. Write");
        ed.undo();
        compare(ed.text, before);
        nv.destroy();
    }

    function test_enter_in_the_middle_renumbers_and_undoes_in_one_step() {
        const nv = make();
        nv.viewMode = "edit";
        const ed = editorOf(nv);
        const before = "1. a\n2. b\n3. c";
        ed.text = before;
        ed.forceActiveFocus();
        ed.cursorPosition = before.indexOf("\n");
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_X);
        tryCompare(ed, "text", "1. a\n2. x\n3. b\n4. c");
        ed.undo();
        // The typed "x" goes first, then the new item with its renumbering.
        if (ed.text !== before) ed.undo();
        compare(ed.text, before);
        nv.destroy();
    }

    function test_emptying_an_item_and_joining_lines_keeps_the_count() {
        const nv = make();
        nv.viewMode = "rich";
        const ed = editorOf(nv);
        const before = "1. a\n2. x\n3. b";
        ed.text = before;
        ed.forceActiveFocus();
        ed.select(before.indexOf("2."), before.indexOf("\n3."));
        keyClick(Qt.Key_Backspace);
        tryCompare(ed, "text", "1. a\n\n2. b");
        keyClick(Qt.Key_Backspace);
        tryCompare(ed, "text", "1. a\n2. b");
        nv.destroy();
    }

    function test_undo_does_not_renumber_again() {
        const nv = make();
        nv.viewMode = "edit";
        const ed = editorOf(nv);
        const before = "1. a\n2. b\n3. c";
        ed.text = before;
        ed.forceActiveFocus();
        ed.select(before.indexOf("2."), before.indexOf("3."));
        keyClick(Qt.Key_Delete);
        tryCompare(ed, "text", "1. a\n2. c");
        ed.undo();
        compare(ed.text, before);
        wait(50);
        compare(ed.text, before);
        ed.redo();
        compare(ed.text, "1. a\n2. c");
        nv.destroy();
    }

    // ── Find ─────────────────────────────────────────────────────

    function findBarOf(nv) {
        const bar = findChild(nv, "note-find-bar");
        verify(bar !== null, "note-find-bar not found");
        return bar;
    }

    function test_ctrl_f_in_the_text_finds_in_the_note() {
        const nv = make();
        nv.viewMode = "rich";
        const ed = editorOf(nv);
        const bar = findBarOf(nv);
        const text = "Sim one\nsim two sim\nSIM three";
        ed.text = text;
        ed.forceActiveFocus();
        // The selection is the query, and its match is the current one.
        ed.select(8, 11);
        nv.focusSearch();
        verify(bar.visible);
        const field = findChild(bar, "note-find-field");
        verify(field.activeFocus, "the query field has no keyboard");
        compare(field.text, "sim");
        compare(bar.matches, [0, 8, 16, 20]);
        compare(bar.current, 1);
        tryVerify(function () { return findChild(nv, "note-find-current") !== null; }, 1000, "the current match is not tinted");
        keyClick(Qt.Key_Return);
        compare(bar.current, 2);
        keyClick(Qt.Key_Down);
        compare(bar.current, 3);
        keyClick(Qt.Key_Down);
        compare(bar.current, 0, "the last match goes round to the first");
        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        compare(bar.current, 3);
        keyClick(Qt.Key_Up);
        compare(bar.current, 2);
        // Аа: the case counts.
        bar.caseSensitive = true;
        compare(bar.matches, [8, 16]);
        bar.caseSensitive = false;
        // Typing narrows.
        keyClick(Qt.Key_End);
        keyClick(" ");
        compare(findChild(bar, "note-find-field").text, "sim ");
        compare(bar.matches, [0, 8, 20]);
        keyClick(Qt.Key_Backspace);
        bar.step(-1);
        const at = bar.matches[bar.current];
        // Esc: closed, the text has the keyboard and the match selected.
        keyClick(Qt.Key_Escape);
        verify(!bar.visible);
        verify(ed.activeFocus);
        compare(ed.selectionStart, at);
        compare(ed.selectionEnd, at + 3);
        nv.destroy();
    }

    function test_ctrl_f_from_the_list_is_still_the_filter() {
        const nv = make();
        const bar = findBarOf(nv);
        const ed = editorOf(nv);
        ed.text = "Sim one";
        const filter = findChild(nv, "note-filter");
        verify(filter !== null);
        filter.forceActiveFocus();
        nv.focusSearch();
        verify(!bar.visible);
        verify(filter.activeFocus);
        nv.destroy();
    }

    function test_no_matches_says_so() {
        const nv = make();
        nv.viewMode = "edit";
        const ed = editorOf(nv);
        const bar = findBarOf(nv);
        ed.text = "alpha beta";
        ed.forceActiveFocus();
        nv.focusSearch();
        keyClick(Qt.Key_Z);
        keyClick(Qt.Key_Q);
        compare(bar.matches.length, 0);
        compare(findChild(bar, "note-find-count").text, I18n.t("notes.find.none"));
        keyClick(Qt.Key_Escape);
        verify(ed.activeFocus);
        nv.destroy();
    }
}
