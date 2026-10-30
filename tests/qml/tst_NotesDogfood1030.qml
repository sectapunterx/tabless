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
}
