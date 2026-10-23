// The task's layout of dogfood 2026-10-23 (Main, TaskNarrow, Wide, Heavy
// sheets), on the document alone: one, two or three columns by the window's
// width or by hand (Ctrl+\, kept); every block folds by its header and stays
// folded for every task; the sections of a long ticket fold apart from each
// other, never as an accordion; the text column is one scroll with nothing
// scrolling inside it; a long description folds to its first lines; the
// properties column scrolls with Готово always under it. Main's side (the
// panel beside a live board) is tst_SmallScreenLayout.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TaskLayout"
    when: windowShown
    visible: true
    width: 1400
    height: 900

    Item { id: host; width: 1400; height: 900 }

    property var made: []
    property string savedSettings: ""
    property string homeProfile: ""
    property var imported: []

    function initTestCase() { tc.savedSettings = AppController.appSettingsJson; }
    function init() {
        // The folds and the column choice of an earlier run are not this one's.
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        delete s.taskBlocksFolded;
        delete s.taskDocColumns;
        AppController.appSettingsJson = JSON.stringify(s);
        host.width = 1400;
        host.height = 900;
    }
    function cleanup() {
        for (const id of tc.made) AppController.deleteTask(id);
        tc.made = [];
        if (tc.imported.length > 0) {
            AppController.activeProfileId = tc.homeProfile;
            for (const p of tc.imported) AppController.deleteProfile(p);
            tc.imported = [];
        }
        AppController.clearPendingUndo();
    }
    function cleanupTestCase() { AppController.appSettingsJson = tc.savedSettings; }

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
    // A Jira ticket as a sync leaves it, in a profile of its own: `paras`
    // paragraphs of text and `items` Smart Checklist items.
    function ticket(tag, paras, items) {
        if (tc.imported.length === 0) tc.homeProfile = AppController.activeProfileId;
        let desc = "";
        for (let i = 1; i <= paras; i++) desc += "Paragraph " + i + " of the ticket, long enough to wrap once on a panel of this width, more or less.\n\n";
        const cl = [];
        for (let i = 0; i < items; i++)
            cl.push({ text: "Item " + i, state: i < 3 ? "done" : i === 3 ? "progress" : "todo" });
        AppController.importProfileFromJson(JSON.stringify({ profile: {
            id: "layout-" + tag, name: "Layout " + tag, color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }, { id: "done", name: "Done", color: "#888888" }],
            tasks: [{ id: "TL-1", title: "layout " + tag, status: "todo", priority: "P2", desc: desc,
                      externalId: "LAY-1", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/LAY-1",
                      externalMeta: { remoteDetails: { smartChecklist: cl } } }] } }), true);
        tc.imported.push(AppController.activeProfileId);
        const m = AppController.tasks;
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), m.roleOf("title")) === "layout " + tag) return m.data(m.index(i, 0), m.roleOf("id"));
        fail("the ticket was not imported");
        return "";
    }
    function mkDoc(windowWidth, wide) {
        const d = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(d !== null);
        d.windowWidth = windowWidth * Theme.scale;
        d.wide = !!wide;
        return d;
    }
    function settled(d) { wait(Theme.durMove + 100); }

    // ── the thresholds ──

    function test_thresholds_by_logical_width() {
        const s = Theme.scale;
        verify(Theme.taskNarrowAt(1499 * s));
        verify(!Theme.taskNarrowAt(1500 * s));
        verify(!Theme.taskPanelAt(1799 * s));
        verify(Theme.taskPanelAt(1800 * s));
        verify(!Theme.taskNarrowAt(0) && !Theme.taskPanelAt(0));
    }

    function test_columns_by_width() {
        const id = ticket("cols", 3, 4);
        // Under 1500: two columns, the text as two tabs.
        let d = mkDoc(1440);
        d.open(id);
        tryCompare(d, "opened", true);
        compare(d.columns, 2);
        verify(d.contentTabs, "no tabs on a narrow window");
        verify(byName("task-doc-content-tabs", d).visible);
        verify(byName("task-doc-side", d).visible, "the properties column is gone");
        verify(!byName("task-doc-comments-col", d).visible);
        d.close(); d.destroy();
        // 1500–1800: three, the comments in the middle.
        d = mkDoc(1600);
        d.open(id);
        tryCompare(d, "opened", true);
        compare(d.columns, 3);
        verify(!d.contentTabs);
        tryVerify(() => byName("task-doc-comments-col", d).visible, 1000, "no comments column");
        const comments = byName("task-doc-comments-section", d);
        compare(comments.parent, byName("task-doc-comments-content", d),
                "the comments are not in the middle column");
        d.close(); d.destroy();
        // From 1800, beside the board: two, one scroll; `f` gives three.
        d = mkDoc(1920, true);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(d.beside);
        compare(d.columns, 2);
        verify(!d.contentTabs, "tabs beside a board");
        verify(byName("task-doc-close", d).visible, "no Close button on the panel");
        verify(byName("task-doc-full", d).visible, "no full-screen button on the panel");
        d.focusDocument();
        keyClick(Qt.Key_F);
        verify(d.full, "f did not give the whole area");
        verify(!d.beside);
        compare(d.columns, 3);
        verify(!byName("task-doc-close", d).visible);
        keyClick(Qt.Key_F);
        verify(!d.full && d.beside, "f did not go back beside the board");
        d.close(); d.destroy();
    }

    function test_a_task_of_my_own_has_no_comments_column() {
        const id = mkTask("no comments here", { branch: "feat/own" });
        const d = mkDoc(1600);
        d.open(id);
        tryCompare(d, "opened", true);
        compare(d.columns, 2, "three columns without comments");
        verify(!byName("task-doc-comments-col", d).visible);
        d.close();
    }

    function test_the_column_choice_cycles_and_is_kept() {
        const id = ticket("choice", 2, 0);
        const d = mkDoc(1600);
        d.open(id);
        tryCompare(d, "opened", true);
        compare(d.columns, 3);
        d.focusDocument();
        keyClick(Qt.Key_Backslash, Qt.ControlModifier);
        compare(d.columns, 1, "Ctrl+\\ did not walk 3 → 1");
        compare(JSON.parse(AppController.appSettingsJson).taskDocColumns, 1, "the choice is not kept");
        verify(!byName("task-doc-side", d).visible, "one column kept the properties column");
        verify(byName("task-doc-done-bottom", d).visible, "no Done in one column");
        keyClick(Qt.Key_Backslash, Qt.ControlModifier);
        compare(d.columns, 2);
        // Another document reads it.
        const d2 = mkDoc(1600);
        d2.open(id);
        compare(d2.columns, 2, "the choice did not carry to the next task");
        d2.close();
        d2.visible = false;
        d2.destroy();
        wait(0);
        // The one in use picked again: back to the window's width.
        mouseClick(byName("task-doc-columns-2", d));
        compare(d.columnsChoice, 0);
        compare(d.columns, 3);
        verify(JSON.parse(AppController.appSettingsJson).taskDocColumns === undefined);
        d.close();
    }

    // ── folding ──

    function test_a_folded_block_stays_folded_for_every_task() {
        const a = mkTask("fold a", { branch: "feat/fold-a" });
        const b = mkTask("fold b", { branch: "feat/fold-b" });
        const d = mkDoc(1600);
        d.open(a);
        tryCompare(d, "opened", true);
        settled(d);
        const code = byName("task-block-body-code", d);
        verify(code.visible, "Код starts folded");
        const toggle = byName("task-block-toggle-code", d);
        mouseClick(toggle);
        verify(!code.visible, "a click did not fold Код");
        compare(JSON.parse(AppController.appSettingsJson).taskBlocksFolded.code, true);
        // Another task, another document: folded.
        const d2 = mkDoc(1600);
        d2.open(b);
        tryCompare(d2, "opened", true);
        verify(!byName("task-block-body-code", d2).visible, "the fold did not carry to the next task");
        d2.close(); d2.destroy();
        // Space on the header unfolds it.
        toggle.forceActiveFocus();
        keyClick(Qt.Key_Space);
        verify(code.visible, "Space did not unfold Код");
        compare(JSON.parse(AppController.appSettingsJson).taskBlocksFolded.code, false);
        d.close();
    }

    function test_rare_blocks_start_folded() {
        const d = mkDoc(1600);
        verify(!d.isOpen("history") && !d.isOpen("mentions") && !d.isOpen("owners"));
        verify(d.isOpen("code") && d.isOpen("threads") && d.isOpen("time"));
        d.windowWidth = 1400 * Theme.scale;
        verify(!d.isOpen("time") && !d.isOpen("check"), "a narrow window keeps time and the checklist open");
        d.destroy();
    }

    function test_sections_fold_apart_never_as_an_accordion() {
        const id = ticket("sections", 30, 20);
        host.width = 1100;
        host.height = 800;
        const d = mkDoc(1920, true);
        d.open(id);
        tryCompare(d, "opened", true);
        settled(d);
        verify(d.isOpen("desc") && d.isOpen("check"), "the description and the checklist are not open together");
        verify(byName("task-doc-tracker-body", d).visible);
        verify(byName("task-doc-smart-checklist", d).visible);
        mouseClick(byName("task-block-toggle-check", d));
        verify(!d.isOpen("check"));
        verify(d.isOpen("desc"), "folding the checklist folded the description");
        verify(byName("task-doc-tracker-body", d).visible);
        mouseClick(byName("task-block-toggle-desc", d));
        verify(!d.isOpen("desc"));
        verify(!d.isOpen("check"), "folding the description opened the checklist");
        mouseClick(byName("task-block-toggle-check", d));
        verify(d.isOpen("check") && !d.isOpen("desc"), "opening the checklist touched the description");
        d.close();
    }

    // ── one scroll ──

    function test_the_text_column_is_one_scroll() {
        const id = ticket("scroll", 30, 20);
        host.width = 1100;
        host.height = 700;
        const d = mkDoc(1920, true);
        d.open(id);
        tryCompare(d, "opened", true);
        settled(d);
        const tracker = byName("task-doc-tracker-body", d);
        verify(!byName("md-block-view", tracker).interactive, "the description scrolls inside itself");
        verify(!byName("md-block-view", byName("task-doc-body", d)).interactive, "my notes scroll inside themselves");
        const scroll = byName("task-doc-scroll", d);
        verify(scroll.interactive && scroll.contentHeight > scroll.height, "nothing to scroll");
        // The section bar: two sections and more than the column shows.
        const bar = byName("task-doc-section-bar", d);
        tryVerify(() => bar.visible, 1000, "no section bar on a long ticket");
        compare(d.activeSection, "desc");
        // 2 jumps to the checklist and lights it in the bar.
        d.focusDocument();
        keyClick(Qt.Key_2);
        tryVerify(() => d.activeSection === "check", 1000, "2 did not go to the checklist: " + d.activeSection);
        const check = byName("task-doc-check", d);
        tryVerify(() => Math.abs(scroll.contentY - Math.min(check.y, scroll.contentHeight - scroll.height)) < 2, 1000,
                  "the checklist is not at the top: " + scroll.contentY + " / " + check.y);
        verify(byName("task-doc-section-check", d).current);
        // Back at the top, then scrolled into the description: its header
        // stays on top with its fold.
        scroll.contentY = byName("task-doc-desc", d).y + Theme.px(40);
        const sticky = byName("task-doc-sticky", d);
        tryVerify(() => sticky.visible, 1000, "the description's header did not stick");
        compare(byName("task-doc-sticky-head", d).kind, "desc");
        d.close();
    }

    function test_a_long_description_folds_to_its_first_lines() {
        const id = ticket("long", 40, 0);
        host.width = 1100;
        host.height = 700;
        const d = mkDoc(1920, true);
        d.open(id);
        tryCompare(d, "opened", true);
        settled(d);
        const tracker = byName("task-doc-tracker-body", d);
        tryVerify(() => d._descLong, 2000, "a 40-paragraph text is not long");
        verify(d._descFolded);
        verify(tracker.parent.height <= d._foldH + 0.5, "folded, the text shows " + tracker.parent.height);
        const readAll = byName("task-doc-desc-read-all", d);
        verify(readAll.visible, "no «Читать целиком»");
        mouseClick(readAll);
        verify(d.descExpanded);
        tryVerify(() => tracker.parent.height > d._foldH * 2, 2000, "opened, the text shows " + tracker.parent.height);
        verify(byName("task-doc-desc-end", d).visible, "no end of the description");
        verify(byName("task-doc-desc-fold", d).visible, "no «Свернуть описание» in its header");
        mouseClick(byName("task-doc-desc-fold", d));
        verify(!d.descExpanded);
        verify(d.isOpen("desc"), "folding the long text closed the block");
        // Another ticket opens folded again.
        d.close();
        d.open(id);
        verify(!d.descExpanded);
        d.close();
    }

    // ── the properties column ──

    function test_the_properties_column_scrolls_with_done_on_screen() {
        const id = mkTask("short window", { branch: "feat/short-window", prNumber: 42 });
        host.height = 240;
        const d = mkDoc(1600);
        d.open(id);
        tryCompare(d, "opened", true);
        settled(d);
        const side = byName("task-doc-side", d);
        const scroll = byName("task-doc-side-scroll", d);
        const done = byName("task-doc-done-bold", d);
        verify(side.visible && done.visible);
        const bottom = done.mapToItem(d, 0, done.height).y;
        verify(bottom <= d.height + 0.5, "Done ran off a 240 px window: " + bottom);
        verify(scroll.contentHeight > scroll.height, "the column fits; the test needs it not to");
        verify(scroll.interactive);
        scroll.contentY = scroll.contentHeight - scroll.height;
        compare(done.mapToItem(d, 0, done.height).y, bottom, "Done moved with the scroll");
        d.close();
    }
}
