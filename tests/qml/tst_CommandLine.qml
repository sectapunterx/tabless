// The command line (APP-267): "block p0" → the one blocked P0 task → its
// "Mark done" closes it; nothing found offers a task; ">" is commands only;
// Ctrl+K again closes it; Ctrl+Enter shows everything as a list.
//
// Drives the real Main.qml with real key events.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "CommandLine"
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

    function add(id, status, priority, title) {
        const d = AppController.newTaskDraft(status);
        d._isNew = true;
        d.id = id;
        d.title = title;
        d.status = status;
        d.priority = priority;
        AppController.saveTask(d);
        tc.seeded.push(id);
    }

    function init() {
        tc.seeded = [];
        add("CMDL-1", "blocked", "P0", "cmdlprobe checkout times out");
        add("CMDL-2", "todo", "P0", "cmdlprobe history");
        add("CMDL-3", "blocked", "P2", "cmdlprobe export");
        closeAll();
    }

    function cleanup() {
        closeAll();
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteTask(tc.seeded[i]);
        tc.seeded = [];
        AppController.clearPendingUndo();
        tc.win.searchText = "";
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
    function type(text) {
        for (const ch of text) {
            if (ch === " ") keyClick(Qt.Key_Space);
            else if (ch >= "0" && ch <= "9") keyClick(ch.charCodeAt(0));
            else keyClick(ch.toUpperCase().charCodeAt(0));
        }
    }
    function openLine() {
        const cmd = popup("CommandPalette");
        tc.win.focusActiveView();
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryCompare(cmd, "opened", true);
        wait(50);
        return cmd;
    }
    function indexOfAction(cmd, action) {
        for (let i = 0; i < cmd._matches.length; i++) if (cmd._matches[i].action === action) return i;
        return -1;
    }

    // The ticket's check: "block p0" → one task → "Mark done" closes it.
    function test_block_p0_finds_one_task_and_d_closes_it() {
        const cmd = openLine();
        type("block p0 cmdlprobe");
        tryVerify(function () { return cmd.chips.length === 2; }, 1000, "chips: " + JSON.stringify(cmd.chips));
        compare(cmd.chips[0].clause, "status:blocked");
        compare(cmd.chips[1].clause, "priority:P0");
        tryVerify(function () { return cmd._parse.total === 1; }, 1000, "found " + cmd._parse.total);
        compare(cmd._matches[0].kind, "task");
        compare(cmd._matches[0].taskId, "CMDL-1");
        const done = indexOfAction(cmd, "doneFound");
        verify(done > 0, "no Mark done row");
        compare(cmd._matches[done].keys, AppController.shortcutText("task.done"));
        while (cmd._selectedIdx < done) keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        tryCompare(cmd, "opened", false);
        tryVerify(function () { return AppController.statusCategory(AppController.taskById("CMDL-1").status) === "done"; }, 1000);
        compare(AppController.statusCategory(AppController.taskById("CMDL-3").status), "blocked", "only what was found");
    }

    // Backspace on an empty field takes the last chip back; Tab makes a chip.
    function test_chips_by_tab_and_backspace() {
        const cmd = openLine();
        type("p0");
        compare(cmd.chips.length, 0);
        keyClick(Qt.Key_Tab);
        compare(cmd.chips.length, 1);
        compare(byName(cmd.contentItem, "cmd-field").text, "");
        keyClick(Qt.Key_Backspace);
        compare(cmd.chips.length, 0);
        cmd.close();
    }

    // Nothing found → "create a task" with the words; Enter opens the input.
    function test_nothing_found_offers_a_task() {
        const cmd = openLine();
        type("zzqxw vvkk");
        tryVerify(function () { return cmd._matches.length === 1 && cmd._matches[0].kind === "create"; }, 1000,
                  JSON.stringify(cmd._matches.map(m => m.kind)));
        keyClick(Qt.Key_Return);
        const qc = popup("QuickCapturePopup");
        tryCompare(qc, "opened", true);
        qc.close();
        tryCompare(qc, "opened", false);
    }

    // ">" leaves only the commands.
    function test_greater_than_is_commands_only() {
        const cmd = openLine();
        keyClick(Qt.Key_Greater, Qt.ShiftModifier);
        type("board");
        tryVerify(function () { return cmd._matches.length > 0; }, 1000);
        for (const m of cmd._matches) verify(m.kind === "command" || m.kind === "setting" || m.kind === "action", m.kind);
        verify(cmd._matches.some(m => m.commandId === "view.board"));
        cmd.close();
    }

    // Ctrl+K and letters at once (acceptance 2026-10-20, idiot test): the
    // keys come in before the event loop turns, so the field must hold the
    // keyboard as the palette opens, not a tick later. Nothing reaches the
    // board as Vim keys behind it.
    function test_letters_right_after_ctrl_k_go_to_the_field() {
        AppController.currentView = "board";
        const b = tc.win.activeViewItem();
        b.cursorTaskId = "CMDL-2";
        b.cursorVisible = true;
        const cmd = popup("CommandPalette");
        tc.win.focusActiveView();
        wait(20);
        // No wait between them: one tick.
        keyClick(Qt.Key_K, Qt.ControlModifier);
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_X);
        keyClick(Qt.Key_K);
        verify(cmd.opened, "Ctrl+K did not open the palette");
        compare(byName(cmd.contentItem, "cmd-field").text, "jxk", "the letters missed the field");
        compare(b.cursorTaskId, "CMDL-2", "j / k moved the board's cursor behind the palette");
        cmd.close();
        tryCompare(cmd, "opened", false);
        // QtTest's keyClick turns the event loop, which hid a field focused
        // a tick late (Qt.callLater); key presses already queued behind Ctrl+K (a slow
        // open, a Debug build) are not so patient. The field must hold the
        // keyboard by the time open() returns, before any tick.
        tc.win.focusActiveView();
        wait(20);
        // As on the first Ctrl+K of a session: the palette's scope does not
        // yet remember the field (later ones did, which hid this).
        byName(cmd.contentItem, "cmd-field").focus = false;
        cmd.open();
        verify(byName(cmd.contentItem, "cmd-field").activeFocus,
               "the field had no keyboard as the palette opened: " + tc.win.activeFocusItem);
        const t = AppController.taskById("CMDL-2");
        verify(t && t.id === "CMDL-2", "x deleted the task behind the palette");
        compare(AppController.statusCategory(t.status), "todo", "x acted on the board behind the palette");
        verify(!AppController.isTaskSelected("CMDL-2"), "x selected the task behind the palette");
        cmd.close();
    }

    // Ctrl+K again closes the line.
    function test_ctrl_k_again_closes() {
        const cmd = openLine();
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryCompare(cmd, "opened", false);
    }

    // Ctrl+Enter: everything found, as a list in Tasks.
    function test_ctrl_enter_shows_all_as_a_list() {
        const cmd = openLine();
        type("block ");
        tryVerify(function () { return cmd._parse.total >= 2; }, 1000);
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(cmd, "opened", false);
        tryCompare(AppController, "currentView", "list");
        verify(tc.win.searchText.indexOf("status:blocked") >= 0, tc.win.searchText);
        tc.win.searchText = "";
        AppController.currentView = "board";
    }

    // The task the cursor was on has its own group, on an empty line.
    function test_the_selected_task_has_its_actions() {
        AppController.currentView = "board";
        const b = tc.win.activeViewItem();
        b.cursorTaskId = "CMDL-2";
        b.cursorVisible = true;
        const cmd = openLine();
        verify(cmd._matches.length > 0);
        compare(cmd._matches[0].kind, "action");
        compare(cmd._matches[0].ids[0], "CMDL-2");
        cmd.close();
    }
}
