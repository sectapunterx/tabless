// A tracker ticket on a narrow window («C · тише», dogfood 2026-10-23,
// Pick-Narrow): no tabs any more (TaskNarrow's «Описание и чек-лист» /
// «Комментарии» are gone) — the description, the checklist and the comments
// read in one scroll, 1 2 3 jumping between them; what the properties column
// holds is a line under the chips, "Код, треды, история ▾" opening it in
// place by the mouse and by the keyboard, folded again for the next task.
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
                      externalId: "NAR-3", externalProvider: "jira", branch: "NAR-3-narrow",
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

    function test_narrow_reads_in_one_scroll_without_tabs() {
        const id = ticket();
        const d = mkDoc(1280);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(byName("task-doc-content-tabs", d) === null, "the tabs are back");
        verify(d.contentTabs === undefined);
        verify(byName("task-doc-desc", d).visible, "no description");
        verify(byName("task-doc-check", d).visible, "the checklist is not with the description");
        verify(byName("task-doc-comments-section", d).visible, "the comments are not in the same scroll");
        const check = byName("task-doc-check", d);
        const desc = byName("task-doc-desc", d);
        tryVerify(() => check.y > desc.y, 1000, "the checklist is not under the description");
        verify(!byName("task-doc-side", d).visible, "a properties column on a narrow window");
        d.close();
    }

    function test_the_line_opens_with_the_mouse() {
        const id = ticket();
        const d = mkDoc(1280);
        d.open(id);
        tryCompare(d, "opened", true);
        wait(Theme.durMove + 100);   // the sheet slides in
        const more = byName("task-doc-meta-more", d);
        verify(more.visible);
        verify(more.text.indexOf(I18n.t("taskdoc.meta.more")) === 0);
        verify(!byName("task-doc-code", d).visible);
        mouseClick(byName("task-doc-meta-more-area", d));
        verify(d.metaOpen);
        tryVerify(() => byName("task-doc-code", d).visible, 1000, "Код did not open under the line");
        verify(byName("task-doc-meta-more-area", d).Accessible.checked);
        wait(Qt.styleHints.mouseDoubleClickInterval + 50);
        mouseClick(byName("task-doc-meta-more-area", d));
        verify(!d.metaOpen);
        verify(!byName("task-doc-code", d).visible);
        d.close();
    }

    function test_the_line_opens_with_the_keyboard() {
        const id = ticket();
        const d = mkDoc(1280);
        d.open(id);
        tryCompare(d, "opened", true);
        const area = byName("task-doc-meta-more-area", d);
        area.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        verify(d.metaOpen, "Space did not open the rest");
        keyClick(Qt.Key_Return);
        verify(!d.metaOpen, "Return did not fold it");
        d.close();
    }

    function test_reopened_folded() {
        const id = ticket();
        const d = mkDoc(1280);
        d.open(id);
        d.toggleMeta();
        verify(d.metaOpen);
        d.close();
        d.open(id);
        verify(!d.metaOpen, "the line's rest stayed open for the next opening");
        d.close();
    }

    // Acceptance 2026-10-30: a Jira sub-task says what it is part of in the
    // head, "· часть TEL-401300 …", on a 1440 window too (it was hidden
    // under 1500), the key in view.
    function test_a_sub_task_names_its_parent_in_the_head_data() {
        return [ { tag: "1440", w: 1440 }, { tag: "1280", w: 1280 }, { tag: "1600", w: 1600 } ];
    }
    function test_a_sub_task_names_its_parent_in_the_head(row) {
        tc.homeProfile = AppController.activeProfileId;
        AppController.importProfileFromJson(JSON.stringify({ profile: {
            id: "tabs-subtask", name: "Tabs subtask", color: "#5cc2dd",
            statuses: [{ id: "prog", name: "In Progress", color: "#888888" }],
            tasks: [{ id: "TL-9", title: "sub ticket", status: "prog", priority: "P2", desc: "",
                      externalId: "TEL-401377", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/TEL-401377",
                      externalMeta: { issueType: "Sub-task", remoteDetails: { parent: {
                          key: "TEL-401300", summary: "Пересчёт при смене пакета", status: "In Progress",
                          statusCategory: "indeterminate", issueType: "Task",
                          url: "https://jira.example.invalid/browse/TEL-401300" } } } }] } }), true);
        tc.imported = AppController.activeProfileId;
        const m = AppController.tasks;
        let id = "";
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), m.roleOf("title")) === "sub ticket") id = m.data(m.index(i, 0), m.roleOf("id"));
        verify(id.length > 0, "not imported");
        const d = mkDoc(row.w);
        d.open(id);
        tryCompare(d, "opened", true);
        const part = byName("task-doc-part-of", d);
        verify(part !== null);
        tryVerify(() => part.visible, 1000, "no part-of line at " + row.w);
        compare(part.text, "·  " + I18n.t("relations.partOf.inline") + " TEL-401300 Пересчёт при смене пакета");
        const crumb = byName("task-doc-crumb-id", d);
        compare(crumb.text, "TEL-401377");
        tryVerify(() => part.width > 0 && part.x >= crumb.x + crumb.width, 1000, "the line is not after the key");
        // The key is never elided away.
        const tm = Qt.createQmlObject('import QtQuick; TextMetrics {}', part);
        tm.font = part.font;
        tm.text = "·  " + I18n.t("relations.partOf.inline") + " TEL-401300";
        verify(part.width >= tm.advanceWidth, part.width + " < " + tm.advanceWidth);
        tm.destroy();
        d.close();
    }

    function test_wide_has_the_column_and_no_line() {
        const id = ticket();
        const d = mkDoc(1600);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(!byName("task-doc-meta-line", d).visible);
        verify(byName("task-doc-side", d).visible);
        verify(byName("task-doc-desc", d).visible);
        d.close();
    }
}
