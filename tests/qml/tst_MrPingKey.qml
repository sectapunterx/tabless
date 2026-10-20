// The key that puts the people a merge request waits on onto "Кому
// написать" (dogfood 2026-10-20 item 9), through the real Main.qml and
// real key events, and the idiot test: nobody to wait on, no request,
// the key typed into a field, the other keymap.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "MrPingKey"
    when: windowShown

    property var win: null
    property var seeded: []
    // The suite's keymap (HEAP_TEST_KEYMAP), put back after: the profile
    // persists and the next files rely on it.
    property string schemeBefore: ""
    readonly property var probes: ["mrprobe-ira", "mrprobe-max"]

    function initTestCase() {
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        verify(comp.status === Component.Ready, comp.errorString());
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 939;
        tc.schemeBefore = AppController.keymapScheme;
        AppController.keymapScheme = "classic";
        AppController.resetAllShortcuts();
        wait(1200);   // splash
        tc.win.requestActivate();
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() !== null; }, 3000);
    }

    function cleanupTestCase() {
        AppController.keymapScheme = tc.schemeBefore;
        AppController.resetAllShortcuts();
        if (tc.win) tc.win.destroy();
        tc.win = null;
    }

    function seed(id, title, git) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.id = id;
        d.title = title;
        AppController.saveTask(d);
        tc.seeded.push(id);
        if (git) AppController.setGitInfoForTest(id, git);
    }

    function dropProbes() {
        for (let i = 0; i < tc.probes.length; i++) {
            const pid = AppController.personIdForHandle(tc.probes[i]);
            if (pid) AppController.deletePerson(pid);
        }
    }
    function probesOnList() {
        return AppController.openPings().filter(p => tc.probes.indexOf(String(p.name)) >= 0).map(p => p.name);
    }

    function init() {
        tc.seeded = [];
        dropProbes();
        seed("MRK-1", "mrkprobe waits", { prState: "open", prNumber: 481, prChecks: "pending",
                                          prReviewDecision: "REVIEW_REQUIRED", prReviewRequests: tc.probes });
        seed("MRK-2", "mrkprobe nobody", { prState: "open", prNumber: 482, prChecks: "passing",
                                           prReviewDecision: "APPROVED", prMergeable: "MERGEABLE", prReviewRequests: [] });
        seed("MRK-3", "mrkprobe plain", null);
        closeAll();
    }

    function cleanup() {
        closeAll();
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteTask(tc.seeded[i]);
        tc.seeded = [];
        dropProbes();
        AppController.clearPendingUndo();
        const field = byName(tc.win.contentItem, "topbar-search");
        if (field) field.text = "";
        tc.win.searchText = "";
        AppController.keymapScheme = "classic";
        AppController.resetAllShortcuts();
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
        AppController.clearSelection();
        const b = tc.win.activeViewItem();
        if (b && b.clearCursor) b.clearCursor();
        tc.win.focusActiveView();
        wait(20);
    }
    function boardWithCursor(id) {
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() && tc.win.activeViewItem().moveCursor; }, 3000);
        tc.win.focusActiveView();
        const b = tc.win.activeViewItem();
        b.cursorTaskId = id;
        b.cursorVisible = true;
        wait(30);
        return b;
    }
    // Ctrl+Alt+P, the Classic key.
    function pressClassic() { keyClick(Qt.Key_P, Qt.ControlModifier | Qt.AltModifier); }

    // P was asked for, but Classic has no bare letters and Vim's P is the
    // previous day / week: Ctrl+Alt+P and Shift+P.
    function test_the_key_is_ctrl_alt_p_in_classic_and_shift_p_in_vim() {
        compare(AppController.shortcutFor("task.pingReviewers"), "Ctrl+Alt+P");
        AppController.keymapScheme = "vim";
        AppController.resetAllShortcuts();
        compare(AppController.shortcutFor("task.pingReviewers"), "Shift+P", "Vim's P is the previous day / week");
    }

    function test_mrof_reads_the_watchers_facts() {
        const m = AppController.mrOf(AppController.taskById("MRK-1"));
        compare(m.ci, "running");
        compare(m.merge, "waiting");
        compare(m.ref, "#481");
        compare(m.waitingHandles, tc.probes);
        const ready = AppController.mrOf(AppController.taskById("MRK-2"));
        compare(ready.ci, "passed");
        compare(ready.merge, "ready");
        compare(AppController.mrOf(AppController.taskById("MRK-3")).ci, undefined, "dots on a task without a request");
    }

    function test_the_key_puts_whom_the_request_waits_on_onto_the_list() {
        boardWithCursor("MRK-1");
        pressClassic();
        tryVerify(() => tc.probesOnList().length === 2, 3000, "P did not add the reviewers: " + JSON.stringify(tc.probesOnList()));
        const ira = AppController.personById(AppController.personIdForHandle("mrprobe-ira"));
        compare(ira.state, "todo");
        verify(String(ira.question).indexOf("#481") >= 0, "the ask does not name the request: " + ira.question);
        // Again: still one each, no duplicates.
        pressClassic();
        wait(600);
        compare(tc.probesOnList().length, 2);
        compare(AppController.openPings().filter(p => p.name === "mrprobe-ira").length, 1, "a second Ира");
    }

    function test_shift_p_in_vim() {
        AppController.keymapScheme = "vim";
        AppController.resetAllShortcuts();
        boardWithCursor("MRK-1");
        keyClick(Qt.Key_P, Qt.ShiftModifier);
        tryVerify(() => tc.probesOnList().length === 2, 3000, "Shift+P did not add the reviewers in Vim");
    }

    // Idiot test: the key where it has nothing to do does nothing.
    function test_the_key_with_nobody_to_wait_on_adds_nobody() {
        const before = AppController.openPings().length;
        boardWithCursor("MRK-2");
        pressClassic();
        wait(600);
        compare(AppController.openPings().length, before, "people added for an approved request");
        boardWithCursor("MRK-3");
        pressClassic();
        wait(600);
        compare(AppController.openPings().length, before, "people added for a task without a request");
        compare(AppController.pingMrReviewers("MRK-3"), 0);
        compare(AppController.pingMrReviewers("MRK-2"), 0);
    }

    // Vim's Shift+P typed into the search is a letter of the search.
    function test_shift_p_typed_into_a_field_is_a_letter() {
        AppController.keymapScheme = "vim";
        AppController.resetAllShortcuts();
        boardWithCursor("MRK-1");
        const field = byName(tc.win.contentItem, "topbar-search");
        verify(field !== null);
        field.forceActiveFocus();
        tryVerify(() => field.activeFocus, 1000);
        keyClick(Qt.Key_P, Qt.ShiftModifier);
        wait(600);
        verify(String(field.text).toLowerCase().indexOf("p") >= 0, "the letter did not reach the field: " + field.text);
        compare(tc.probesOnList().length, 0, "Shift+P typed into the search added people");
        // Vim's P on the board is not this key.
        field.text = "";
        boardWithCursor("MRK-1");
        keyClick(Qt.Key_P);
        wait(600);
        compare(tc.probesOnList().length, 0, "P in Vim added people");
    }

    function test_one_undo_takes_them_back() {
        compare(AppController.pingMrReviewers("MRK-1"), 2);
        compare(tc.probesOnList().length, 2);
        AppController.undo();
        tryVerify(() => tc.probesOnList().length === 0, 2000, "undo left people on the list");
    }
}
