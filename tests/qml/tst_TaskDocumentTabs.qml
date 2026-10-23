// A tracker ticket on a narrow window (dogfood 2026-10-23, TaskNarrow
// sheet): the text column is two tabs, «Описание и чек-лист» and
// «Комментарии · N», switched by the mouse and by ← / →, the keyboard going
// with them; the properties column beside them stays one column of blocks
// (the 0.9.5 Код · Время · История tabs are gone). A wide window has no tabs.
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

    property string homeProfile: ""
    property string imported: ""
    property string savedSettings: ""
    function initTestCase() { tc.savedSettings = AppController.appSettingsJson; }
    function cleanupTestCase() { AppController.appSettingsJson = tc.savedSettings; }
    function init() {
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        delete s.taskBlocksFolded;
        delete s.taskDocColumns;
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function cleanup() {
        if (tc.imported.length > 0) {
            AppController.activeProfileId = tc.homeProfile;
            AppController.deleteProfile(tc.imported);
            tc.imported = "";
        }
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
    function ticket() {
        tc.homeProfile = AppController.activeProfileId;
        AppController.importProfileFromJson(JSON.stringify({ profile: {
            id: "tabs-narrow", name: "Tabs narrow", color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }],
            tasks: [{ id: "TL-3", title: "narrow ticket", status: "todo", priority: "P2", desc: "The ticket's text.",
                      externalId: "NAR-3", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/NAR-3",
                      externalMeta: { remoteDetails: { smartChecklist: [{ text: "One", state: "todo" }] } } }] } }), true);
        tc.imported = AppController.activeProfileId;
        const m = AppController.tasks;
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), m.roleOf("title")) === "narrow ticket") return m.data(m.index(i, 0), m.roleOf("id"));
        fail("not imported");
        return "";
    }
    function mkDoc(windowWidth) {
        const d = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(d !== null);
        d.windowWidth = windowWidth * Theme.scale;
        return d;
    }

    function test_tabs_switch_with_the_mouse() {
        const id = ticket();
        const d = mkDoc(1280);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(d.contentTabs);
        verify(byName("task-doc-content-tabs", d).visible);
        compare(d.contentTab, "desc");
        verify(byName("task-doc-desc", d).visible, "the description is not on its tab");
        verify(!byName("task-doc-comments-section", d).visible, "the comments show on the description's tab");
        wait(Theme.durMove + 100);   // the panel slides in
        mouseClick(byName("task-doc-ctab-comments", d));
        compare(d.contentTab, "comments");
        verify(!byName("task-doc-desc", d).visible);
        verify(!byName("task-doc-check", d).visible);
        verify(byName("task-doc-comments-section", d).visible, "the comments are not on their tab");
        verify(byName("task-doc-ctab-comments-area", d).Accessible.checked);
        wait(Qt.styleHints.mouseDoubleClickInterval + 50);
        mouseClick(byName("task-doc-ctab-desc", d));
        compare(d.contentTab, "desc");
        // The properties column is blocks, not tabs.
        verify(byName("task-doc-side", d).visible);
        verify(byName("task-doc-code", d).visible && byName("task-doc-time", d).visible);
        d.close();
    }

    function test_tabs_switch_with_the_keyboard() {
        const id = ticket();
        const d = mkDoc(1280);
        d.open(id);
        tryCompare(d, "opened", true);
        const descArea = byName("task-doc-ctab-desc-area", d);
        descArea.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Right);
        compare(d.contentTab, "comments");
        verify(byName("task-doc-ctab-comments-area", d).activeFocus, "the keyboard stayed on the first tab");
        keyClick(Qt.Key_Left);
        compare(d.contentTab, "desc");
        verify(descArea.activeFocus);
        d.close();
    }

    function test_reopened_on_the_first_tab() {
        const id = ticket();
        const d = mkDoc(1280);
        d.open(id);
        d.pickContentTab("comments");
        d.close();
        d.open(id);
        compare(d.contentTab, "desc");
        d.close();
    }

    function test_wide_mode_has_no_tabs() {
        const id = ticket();
        const d = mkDoc(1600);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(!d.contentTabs);
        verify(!byName("task-doc-content-tabs", d).visible);
        verify(byName("task-doc-desc", d).visible);
        d.close();
    }
}
