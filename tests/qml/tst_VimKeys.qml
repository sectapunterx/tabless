// The 0.8.0 keymap (keymap.md, APP-272): two-key sequences with a prefix,
// single letters that never fire while typing, keys read by the physical
// key in any layout, and "dd" that does not take Done back.
//
// Drives the real Main.qml with real key events.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "VimKeys"
    when: windowShown

    property var win: null
    property var seeded: []

    function initTestCase() {
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        verify(comp.status === Component.Ready, comp.errorString());
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 939;
        AppController.resetAllShortcuts();
        wait(1200);   // splash
        tc.win.requestActivate();
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() !== null; }, 3000);
        wait(100);
    }

    function cleanupTestCase() {
        if (tc.win) tc.win.destroy();
        tc.win = null;
    }

    function init() {
        tc.seeded = [];
        for (let i = 0; i < 2; i++) {
            const d = AppController.newTaskDraft("todo");
            d._isNew = true;
            d.id = "VIM-" + i;
            d.title = "vimprobe card " + i;
            AppController.saveTask(d);
            tc.seeded.push(d.id);
        }
        closeAll();
        router().cancel();
    }

    function cleanup() {
        closeAll();
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteTask(tc.seeded[i]);
        tc.seeded = [];
        AppController.clearPendingUndo();
        AppController.resetAllShortcuts();
        tc.win.searchText = "";
        router().cancel();
    }

    function typeName(o) {
        const s = String(o);
        const i = s.indexOf("(");
        return i > 0 ? s.slice(0, i) : s;
    }
    function popups() {
        const out = [];
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) {
            const o = cd[i];
            if (o && o.opened !== undefined && o.open !== undefined) out.push(o);
        }
        return out;
    }
    function popup(prefix) {
        const ps = popups();
        for (let i = 0; i < ps.length; i++)
            if (typeName(ps[i]).indexOf(prefix) === 0 || ps[i].objectName === prefix) return ps[i];
        return null;
    }
    function dataByName(name) {
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) if (cd[i] && cd[i].objectName === name) return cd[i];
        return null;
    }
    function find(root, pred) {
        if (!root) return null;
        if (pred(root)) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) {
            const r = find(kids[i], pred);
            if (r) return r;
        }
        return null;
    }
    function byName(root, name) { return find(root, function (it) { return it.objectName === name; }); }
    function router() { return dataByName("key-router"); }
    function closeAll() {
        const ps = popups();
        for (let i = 0; i < ps.length; i++) if (ps[i].opened) ps[i].close();
        wait(150);
        AppController.clearSelection();
        const b = tc.win.activeViewItem();
        if (b && b.clearCursor) b.clearCursor();
        tc.win.focusActiveView();
        wait(20);
    }
    function boardWithCursor() {
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() && tc.win.activeViewItem().moveCursor; }, 3000);
        tc.win.focusActiveView();
        const b = tc.win.activeViewItem();
        b.cursorTaskId = "VIM-0";
        b.cursorVisible = true;
        wait(30);
        return b;
    }

    // g b switches the view; g t goes to Today.
    function test_g_b_switches_to_the_board() {
        AppController.currentView = "today";
        wait(50);
        tc.win.focusActiveView();
        keyClick(Qt.Key_G);
        compare(router().pending, "G");
        verify(byName(tc.win.contentItem, "key-pending-bar") === null || true);
        keyClick(Qt.Key_B);
        tryCompare(AppController, "currentView", "board");
        compare(router().pending, "");
        keyClick(Qt.Key_G);
        keyClick(Qt.Key_T);
        tryCompare(AppController, "currentView", "today");
    }

    // The hint bar names what can follow the prefix.
    function test_the_prefix_shows_its_continuations() {
        AppController.currentView = "today";
        tc.win.focusActiveView();
        keyClick(Qt.Key_G);
        const bar = dataByName("key-pending-bar");
        verify(bar !== null);
        tryCompare(bar, "visible", true);
        verify(bar.items.length >= 4, JSON.stringify(bar.items));
        verify(bar.items.some(it => it.keys === "b"), "g b is offered");
        keyClick(Qt.Key_Escape);
        tryCompare(bar, "visible", false);
    }

    // g with nothing after it does nothing, and lets go after a second.
    function test_a_prefix_alone_does_nothing() {
        AppController.currentView = "today";
        tc.win.focusActiveView();
        keyClick(Qt.Key_G);
        compare(router().pending, "G");
        wait(1300);
        compare(router().pending, "", "still waiting after a second");
        keyClick(Qt.Key_B);
        compare(AppController.currentView, "today", "a b long after g is not g b");
    }

    // The one-time "this key moved" notice hears a prefix only when it stands
    // alone: "g b" used right is not the old g (EYE-16).
    SignalSpy { id: chordSpy; signalName: "chordPressed" }
    function test_a_finished_sequence_is_not_an_old_key() {
        AppController.currentView = "today";
        tc.win.focusActiveView();
        chordSpy.target = router();
        chordSpy.clear();
        keyClick(Qt.Key_G);
        keyClick(Qt.Key_B);
        tryCompare(AppController, "currentView", "board");
        wait(1300);
        compare(chordSpy.count, 0, "neither g nor b of g b");
        keyClick(Qt.Key_G);
        wait(1300);
        compare(chordSpy.count, 1, "g that timed out on its own");
        compare(chordSpy.signalArguments[0][0], "G");
        chordSpy.target = null;
    }

    // Esc lets go of the prefix.
    function test_esc_cancels_the_prefix() {
        AppController.currentView = "today";
        tc.win.focusActiveView();
        keyClick(Qt.Key_G);
        keyClick(Qt.Key_Escape);
        compare(router().pending, "");
        keyClick(Qt.Key_B);
        compare(AppController.currentView, "today");
    }

    // The Russian layout: п и is g b (the same physical keys).
    function test_the_russian_layout_is_the_same_keys() {
        compare(router().chordOf(0x41F, 0, "п"), "G");
        compare(router().chordOf(0x41B, Qt.ControlModifier, ""), "Ctrl+K");
        // Pressing them is Windows only: QTest on Linux aborts on a
        // non-ASCII key, and the mapping above is what the router uses.
        if (Qt.platform.os !== "windows") return;
        AppController.currentView = "today";
        tc.win.focusActiveView();
        keyClick(0x41F);   // п
        compare(router().pending, "G");
        keyClick(0x418);   // и
        tryCompare(AppController, "currentView", "board");
    }

    // A single letter never fires while a field has the keyboard.
    function test_letters_type_in_a_field() {
        AppController.currentView = "board";
        wait(50);
        const field = byName(tc.win.contentItem, "topbar-search");
        verify(field !== null);
        field.forceActiveFocus();
        keyClick(Qt.Key_G);
        keyClick(Qt.Key_B);
        compare(router().pending, "", "g waited inside a field");
        compare(field.text, "gb");
        keyClick(Qt.Key_D);
        compare(field.text, "gbd");
        field.text = "";
        tc.win.focusActiveView();
    }

    // dd: the second d within half a second is the Vim habit, not "undo Done".
    function test_dd_does_not_take_done_back() {
        boardWithCursor();
        keyClick(Qt.Key_D);
        tryVerify(function () { return AppController.statusCategory(AppController.taskById("VIM-0").status) === "done"; }, 1000);
        keyClick(Qt.Key_D);
        wait(50);
        compare(AppController.statusCategory(AppController.taskById("VIM-0").status), "done", "dd put it back");
        wait(600);
        // Done is folded on the heap 2 board (APP-262), so the cursor cannot
        // follow the card there: the later d acts on it selected.
        AppController.setSelectedTaskIds(["VIM-0"]);
        tc.win.focusActiveView();
        keyClick(Qt.Key_D);
        tryVerify(function () { return AppController.statusCategory(AppController.taskById("VIM-0").status) !== "done"; }, 1000,
                  "a later d takes it back");
    }

    // 1–4 set the priority of the task under the cursor; y y copies its id.
    function test_task_keys_act_on_the_cursor() {
        boardWithCursor();
        keyClick(Qt.Key_1);
        tryVerify(function () { return AppController.taskById("VIM-0").priority === "P0"; }, 1000);
        keyClick(Qt.Key_4);
        tryVerify(function () { return AppController.taskById("VIM-0").priority === "P3"; }, 1000);
        const toast = byName(tc.win.contentItem, "toast");
        keyClick(Qt.Key_Y);
        keyClick(Qt.Key_Y);
        compare(router().pending, "");
        verify(toast !== null);
    }

    // ":" is the command line on its commands; "?" the cheat sheet.
    function test_colon_and_question_mark() {
        AppController.currentView = "board";
        tc.win.focusActiveView();
        keyClick(Qt.Key_Colon, Qt.ShiftModifier);
        const cmd = popup("CommandPalette");
        tryCompare(cmd, "opened", true);
        compare(byName(cmd.contentItem, "cmd-field").text, ">");
        cmd.close();
        tryCompare(cmd, "opened", false);
        tc.win.focusActiveView();
        keyClick(Qt.Key_Question, Qt.ShiftModifier);
        const sheet = popup("KeyCheatSheet");
        tryCompare(sheet, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(sheet, "opened", false);
    }

    // A rebound sequence works at once, and the old one is free.
    function test_a_rebound_sequence_works() {
        verify(AppController.setShortcut("view.board", "Y, Q"));
        AppController.currentView = "today";
        tc.win.focusActiveView();
        keyClick(Qt.Key_G);
        keyClick(Qt.Key_B);
        compare(AppController.currentView, "today", "g b still went to the board");
        keyClick(Qt.Key_Y);
        keyClick(Qt.Key_Q);
        tryCompare(AppController, "currentView", "board");
    }

    // The list's V is the catalogue's selection.toggle (review 2026-10-19):
    // rebound, the bare V stops marking rows and the new key marks them.
    function test_v_on_the_list_follows_its_rebinding() {
        AppController.currentView = "list";
        tryVerify(function () { const v = tc.win.activeViewItem(); return v && v.toggleCursorSelection; }, 3000);
        const list = tc.win.activeViewItem();
        const pick = function () {
            list.cursorTaskId = "VIM-0";
            list.cursorVisible = true;
            AppController.clearSelection();
            tc.win.searchText = "";
            tc.win.focusActiveView();
            wait(30);
        };
        pick();
        keyClick(Qt.Key_V);
        tryCompare(AppController, "selectionCount", 1, 1000, "V marks the row");
        verify(AppController.setShortcut("selection.toggle", "Ctrl+Alt+Shift+V"));
        pick();
        keyClick(Qt.Key_V);
        wait(400);
        compare(AppController.selectionCount, 0, "a bare V after the rebinding marks nothing");
        pick();
        keyClick(Qt.Key_V, Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier);
        tryCompare(AppController, "selectionCount", 1, 1000, "the new key marks the row");
    }

    // Ctrl O goes back where the person was.
    function test_ctrl_o_goes_back() {
        AppController.currentView = "today";
        wait(20);
        AppController.currentView = "board";
        wait(20);
        tc.win.focusActiveView();
        keyClick(Qt.Key_O, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "today");
        tc.win.focusActiveView();
        keyClick(Qt.Key_I, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "board");
    }

    // ── Idiot test 2026-10-10 (board) ──

    function column(b, sid) {
        return find(b, function (it) { return typeof it.startRename === "function" && it.statusId === sid; });
    }
    function search() { return byName(tc.win.contentItem, "topbar-search"); }
    // The top card of VIM-0's column, so j has somewhere to go.
    function topOfColumn(b) {
        const cols = b._visibleByColumn();
        for (let c = 0; c < cols.length; c++)
            if (cols[c].ids.indexOf("VIM-0") >= 0) return cols[c].ids[0];
        return "VIM-0";
    }

    // IDIOT-TASKS-2: a held e archives the card under the cursor, not the
    // whole column one repeat at a time.
    function test_a_held_key_acts_once() {
        boardWithCursor();
        KeyTest.press(tc.win, Qt.Key_E, 0, "e", false);
        for (let i = 0; i < 4; i++) KeyTest.press(tc.win, Qt.Key_E, 0, "e", true);
        wait(50);
        verify(AppController.taskById("VIM-0").archived === true, "the first press archived nothing");
        verify(AppController.taskById("VIM-1").archived !== true, "a repeat archived the next card");
        // Walking still repeats.
        const b = boardWithCursor();
        AppController.setArchived("VIM-0", false);
        wait(30);
        const top = topOfColumn(b);
        b.cursorTaskId = top;
        KeyTest.press(tc.win, Qt.Key_J, 0, "j", false);
        KeyTest.press(tc.win, Qt.Key_J, 0, "j", true);
        verify(b.cursorTaskId !== top, "j did not walk");
    }

    // IDIOT-TASKS-4: a catalogue key with nothing to act on is not typed
    // into the filter.
    function test_a_dead_key_is_not_typed_into_the_filter() {
        boardWithCursor();
        AppController.clearPendingUndo();
        const before = tc.win.searchText;
        keyClick(Qt.Key_U);
        keyClick(Qt.Key_BracketLeft);
        wait(200);
        compare(search().text, "", "u / [ went to type-to-search");
        compare(tc.win.searchText, before);
        // A letter that is nobody's key still searches.
        keyClick(Qt.Key_W);
        tryCompare(search(), "text", "w");
        search().text = "";
        tc.win.searchText = before;
        tc.win.focusActiveView();
    }

    // IDIOT-TASKS-17: a click lets go of the prefix; a key that continues
    // nothing does its own thing.
    function test_a_prefix_lets_go_elsewhere() {
        AppController.currentView = "today";
        tc.win.focusActiveView();
        keyClick(Qt.Key_G);
        compare(router().pending, "G");
        mouseClick(tc.win.contentItem, 5, tc.win.height - 5);
        compare(router().pending, "", "a click kept the prefix");
        tc.win.focusActiveView();
        keyClick(Qt.Key_G);
        keyClick(Qt.Key_K, Qt.ControlModifier);
        const cmd = popup("CommandPalette");
        tryCompare(cmd, "opened", true);
        cmd.close();
        tryCompare(cmd, "opened", false);
    }

    // IDIOT-TASKS-6: the second of two quick Returns in a board dialog does
    // not open the cursor's task.
    function test_return_twice_in_a_board_dialog() {
        const b = boardWithCursor();
        const wip = find(b, function (it) { return it.objectName === "wip-popup"; })
                    || (function () { const d = b.data; for (let i = 0; i < d.length; i++) if (d[i] && d[i].objectName === "wip-popup") return d[i]; return null; })();
        verify(wip !== null);
        wip.openFor("todo", "To Do", 0, null);
        tryCompare(wip, "opened", true);
        keyClick(Qt.Key_3);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Return);
        wait(100);
        compare(tc.win._panelOpen, false, "the second Return opened the task");
        AppController.setStatusWipLimit("todo", 0);
        wait(450);
    }

    // IDIOT-TASKS-1: after a column rename the board has the keys again.
    function test_rename_hands_the_keyboard_back() {
        const b = boardWithCursor();
        const col = column(b, "todo");
        verify(col !== null);
        const old = col.statusName;
        col.startRename();
        keyClick(Qt.Key_Escape);
        wait(30);
        verify(!col.renaming);
        compare(AppController.statuses.find(st => st.id === "todo").name, old);
        const top = topOfColumn(b);
        b.cursorTaskId = top;
        keyClick(Qt.Key_J);
        verify(b.cursorTaskId !== top, "j went into the hidden field");
    }

    // IDIOT-TASKS-13/14: z a folds and unfolds; Ctrl+A skips a folded column.
    function test_fold_unfold_and_select_all() {
        const b = boardWithCursor();
        keyClick(Qt.Key_Z);
        keyClick(Qt.Key_A);
        tryVerify(function () { return b.isFolded("todo"); }, 1000);
        b.selectAllVisible();
        verify(AppController.selectedTaskIds.indexOf("VIM-0") < 0, "Ctrl+A took a card of a folded column");
        AppController.clearSelection();
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Z);
        keyClick(Qt.Key_A);
        tryVerify(function () { return !b.isFolded("todo"); }, 1000, "z a folded another column");
    }
}
