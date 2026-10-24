// The task document's meta column as tabs (dogfood 2026-10-22, Small
// sheet): on a small screen Код · Время · История one at a time, switched by
// the mouse and by ← / →; a tab with nothing in it is not offered; a wide
// window keeps the stacked column. The window side is tst_SmallScreenLayout.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TaskDocumentTabs"
    when: windowShown
    visible: true
    width: 1100
    height: 760

    Item { id: host; anchors.fill: parent }

    property var made: []
    function cleanup() {
        for (const id of tc.made) AppController.deleteTask(id);
        tc.made = [];
        AppController.clearPendingUndo();
    }
    function find(root, pred) {
        if (!root) return null;
        if (pred(root)) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) { const r = find(kids[i], pred); if (r) return r; }
        return null;
    }
    function byName(name, root) { return find(root, function (it) { return it.objectName === name; }); }
    function mkTask(title, extra) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        for (const k in (extra || {})) d[k] = extra[k];
        AppController.saveTask(d);
        tc.made.push(d.id);
        return d.id;
    }

    function mkDoc() {
        const d = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent; tabbed: true }', host);
        verify(d !== null);
        return d;
    }

    function test_tabs_switch_with_the_mouse() {
        const id = mkTask("tabs by mouse", { branch: "feat/tabs" });
        const d = mkDoc();
        d.open(id);
        tryCompare(d, "opened", true);
        const tabs = byName("task-doc-side-tabs", d);
        verify(tabs.visible);
        const code = byName("task-doc-tab-code", d);
        const time = byName("task-doc-tab-time", d);
        verify(code !== null && time !== null);
        verify(code.current);
        verify(byName("task-doc-code", d).visible);
        wait(Theme.durMove + 100);   // the panel slides in
        mouseClick(time);
        compare(d._tab, "time");
        verify(byName("task-doc-tab-time", d).current);
        compare(byName("task-doc-tab-time", d), time, "the tabs were built again under the pointer");
        verify(!byName("task-doc-code", d).visible);
        verify(byName("task-doc-time", d).visible);
        compare(byName("task-doc-tab-time-area", d).Accessible.checked, true);
        // QtTest's second click right after the first never reaches the
        // other tab's area; a person's does.
        wait(Qt.styleHints.mouseDoubleClickInterval + 50);
        mouseClick(code);
        compare(d._tab, "code");
        verify(byName("task-doc-code", d).visible);
        verify(!byName("task-doc-time", d).visible);
        d.close();
    }

    function test_tabs_switch_with_the_keyboard() {
        const id = mkTask("tabs by keys", { branch: "feat/tabs-keys" });
        const d = mkDoc();
        d.open(id);
        tryCompare(d, "opened", true);
        const codeArea = byName("task-doc-tab-code-area", d);
        codeArea.forceActiveFocus(Qt.TabFocusReason);
        verify(codeArea.activeFocus);
        keyClick(Qt.Key_Right);
        compare(d._tab, "time");
        verify(byName("task-doc-tab-time-area", d).activeFocus, "the keyboard stayed on Код");
        verify(byName("task-doc-time", d).visible);
        keyClick(Qt.Key_Left);
        compare(d._tab, "code");
        keyClick(Qt.Key_Left);
        compare(d._tab, d._sideTabs[d._sideTabs.length - 1], "← on the first wraps round");
        // Space / Return on a tab pick it.
        byName("task-doc-tab-code-area", d).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        compare(d._tab, "code");
        d.close();
    }

    function test_an_empty_tab_is_not_offered() {
        const id = mkTask("no code here");
        const d = mkDoc();
        d.open(id);
        tryCompare(d, "opened", true);
        verify(byName("task-doc-tab-code", d) === null, "Код offered with no code");
        compare(d._tab, "time", "the first tab there is");
        verify(byName("task-doc-time", d).visible);
        d.close();
    }

    function test_wide_mode_has_no_tabs() {
        const id = mkTask("wide", { branch: "feat/wide" });
        const d = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(!byName("task-doc-side-tabs", d).visible);
        verify(byName("task-doc-code", d).visible && byName("task-doc-time", d).visible);
        d.close();
    }
}
