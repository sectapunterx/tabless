// The task's layout «C · тише» (dogfood 2026-10-23: Redo-Sheet-Quiet,
// Pick-Narrow, Pick-Full, Pick-Expanded, Pick-SideOpen), on the document
// alone: from 1500 logical px the text and the properties side by side,
// below 1500 the text alone with a line of what the column holds under the
// chips, `f` for the whole area and back; no column switch. Every section of
// the properties column folds by its heading and stays folded for every task;
// the text's sections never fold (quiet "ещё N" buttons instead); the text
// column is one scroll with nothing scrolling inside it and the section being
// read named on top; a long description folds to its first lines; the
// properties column scrolls with Готово always under it. Main's side (the
// sheet over the dimmed board) is tst_SmallScreenLayout.
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
        // The folds of an earlier run are not this one's.
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
    function mkDoc(windowWidth) {
        const d = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(d !== null);
        d.windowWidth = windowWidth * Theme.scale;
        return d;
    }
    function settled(d) { wait(Theme.durMove + 100); }

    // ── the thresholds ──

    function test_thresholds_by_logical_width() {
        const s = Theme.scale;
        verify(Theme.taskNarrowAt(1499 * s));
        verify(!Theme.taskNarrowAt(1500 * s));
        verify(!Theme.taskNarrowAt(1920 * s));
        verify(!Theme.taskNarrowAt(0));
        // The sheet: the text at most 760, the properties 300, side by side.
        compare(Theme.taskTextMax, Theme.px(760));
        compare(Theme.taskSideColumn, Theme.px(300));
        verify(Theme.taskSheetWidth >= Theme.taskTextMax + Theme.taskSideColumn);
    }

    function test_layout_by_width() {
        const id = ticket("cols", 3, 4);
        // Under 1500 (Pick-Narrow): the text alone, the column's line under
        // the chips, which opens what the column holds in place.
        let d = mkDoc(1440);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(!byName("task-doc-side", d).visible, "a properties column on a narrow window");
        verify(byName("task-doc-meta-line", d).visible, "no line of the column under the chips");
        verify(!byName("task-doc-meta-inline", d).visible);
        settled(d);   // the sheet slides in
        mouseClick(byName("task-doc-meta-more-area", d));
        verify(d.metaOpen);
        verify(byName("task-doc-meta-inline", d).visible, "the line did not open the rest");
        compare(byName("task-doc-side-content", d).parent, byName("task-doc-meta-inline", d));
        verify(byName("task-doc-close", d).visible, "no Close button on a narrow window");
        verify(!byName("task-doc-full", d).visible, "the sheet is the area here: no full-screen button");
        verify(byName("task-doc-done-bottom", d).visible, "no Done under the text");
        d.close(); d.destroy();
        // From 1500 (Redo-Sheet-Quiet): the text and the properties.
        d = mkDoc(1600);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(byName("task-doc-side", d).visible, "no properties column");
        verify(!byName("task-doc-meta-line", d).visible, "the narrow line beside the column");
        verify(byName("task-doc-text", d).width <= Theme.taskTextMax + 0.5, "the text is wider than 760");
        verify(byName("task-doc-close", d).visible, "no Close button on the sheet");
        verify(byName("task-doc-full", d).visible, "no full-screen button on the sheet");
        verify(d.beside);
        d.focusDocument();
        keyClick(Qt.Key_F);
        verify(d.full, "f did not give the whole area");
        verify(!d.beside);
        // Закрыть Esc in every layout, the whole area too (owner review 2026-10-23).
        verify(byName("task-doc-close", d).visible, "no Close button over the whole area");
        verify(byName("task-doc-full", d).visible, "no back-to-sheet button");
        verify(byName("task-doc-text", d).width <= Theme.taskTextMax + 0.5, "over the whole area the text is wider than 760");
        keyClick(Qt.Key_F);
        verify(!d.full && d.beside, "f did not go back to the sheet");
        d.close(); d.destroy();
    }

    // The 1 / 2 / 3 columns switch is gone (not in «C · тише»): no buttons,
    // and Ctrl+Backslash changes nothing.
    function test_there_is_no_column_switch() {
        const id = ticket("choice", 2, 0);
        const d = mkDoc(1600);
        d.open(id);
        tryCompare(d, "opened", true);
        verify(byName("task-doc-columns", d) === null, "the columns switch is back");
        d.focusDocument();
        const side = byName("task-doc-side", d).visible;
        keyClick(Qt.Key_Backslash, Qt.ControlModifier);
        compare(byName("task-doc-side", d).visible, side, "Ctrl+Backslash changed the layout");
        verify(JSON.parse(AppController.appSettingsJson).taskDocColumns === undefined, "a column choice was kept");
        d.close();
    }

    // ── folding ──

    function test_a_folded_section_stays_folded_for_every_task() {
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
        // Folded, the heading stays: one line.
        verify(byName("task-block-head-code", d).visible);
        // Another task, another document: folded.
        const d2 = mkDoc(1600);
        d2.open(b);
        tryCompare(d2, "opened", true);
        verify(!byName("task-block-body-code", d2).visible, "the fold did not carry to the next task");
        d2.close(); d2.destroy();
        // Space on the heading unfolds it.
        toggle.forceActiveFocus();
        keyClick(Qt.Key_Space);
        verify(code.visible, "Space did not unfold Код");
        compare(JSON.parse(AppController.appSettingsJson).taskBlocksFolded.code, false);
        // The column itself never folds.
        verify(byName("task-doc-side", d).visible);
        d.close();
    }

    function test_rare_sections_start_folded() {
        const d = mkDoc(1600);
        verify(!d.isOpen("history") && !d.isOpen("mentions") && !d.isOpen("owners"));
        verify(d.isOpen("code") && d.isOpen("threads") && d.isOpen("time"));
        // The same on a narrow window: one density everywhere.
        d.windowWidth = 1400 * Theme.scale;
        verify(d.isOpen("code") && d.isOpen("threads") && d.isOpen("time"));
        d.destroy();
    }

    // The text's sections have no fold: no toggle on their headings; a long
    // checklist shows its first open items and "ещё N · готовые скрыты".
    function test_text_sections_do_not_fold() {
        const id = ticket("sections", 30, 20);
        host.width = 1100;
        host.height = 800;
        const d = mkDoc(1920);
        d.open(id);
        tryCompare(d, "opened", true);
        settled(d);
        verify(byName("task-block-toggle-desc", d) === null, "the description folds by a heading");
        verify(byName("task-block-toggle-check", d) === null, "the checklist folds by a heading");
        verify(byName("task-doc-tracker-body", d).visible);
        const list = byName("task-doc-smart-checklist", d);
        verify(list.visible);
        const more = byName("smart-cl-more", list);
        verify(more.visible, "no «ещё N» on a long checklist");
        verify(more.text.indexOf(I18n.t("taskdoc.smart.closedHidden")) > 0, "the finished ones are not said to be hidden");
        tryVerify(() => {
            d._keepInView(more, 0, more.height);
            const q = more.mapToItem(d, 0, 0);
            return q.y > 0 && q.y + more.height < d.height;
        }, 2000, "«ещё N» never came into view");
        mouseClick(byName("smart-cl-more-toggle", list));
        verify(list.expanded, "«ещё N» did not show the rest");
        d.close();
    }

    // ── one scroll ──

    function test_the_text_column_is_one_scroll() {
        const id = ticket("scroll", 30, 20);
        host.width = 1100;
        host.height = 700;
        const d = mkDoc(1920);
        d.open(id);
        tryCompare(d, "opened", true);
        settled(d);
        const tracker = byName("task-doc-tracker-body", d);
        verify(!byName("md-block-view", tracker).interactive, "the description scrolls inside itself");
        verify(!byName("md-block-view", byName("task-doc-body", d)).interactive, "my notes scroll inside themselves");
        const scroll = byName("task-doc-scroll", d);
        verify(scroll.interactive && scroll.contentHeight > scroll.height, "nothing to scroll");
        // Whole pixels, whatever the wheel did (owner review 2026-10-23).
        verify(scroll.pixelAligned);
        compare(d.activeSection, "desc");
        // 2 jumps to the checklist.
        d.focusDocument();
        keyClick(Qt.Key_2);
        tryVerify(() => d.activeSection === "check", 1000, "2 did not go to the checklist: " + d.activeSection);
        const check = byName("task-doc-check", d);
        tryVerify(() => Math.abs(scroll.contentY - Math.min(check.y, scroll.contentHeight - scroll.height)) < 2, 1000,
                  "the checklist is not at the top: " + scroll.contentY + " / " + check.y);
        // Back at the top, then scrolled into the description: its name
        // stays on top in a quiet line (Pick-Expanded), on a whole pixel.
        scroll.contentY = byName("task-doc-desc", d).y + Theme.px(40);
        const sticky = byName("task-doc-sticky", d);
        tryVerify(() => sticky.visible, 1000, "the description's heading did not stick");
        compare(byName("task-doc-sticky-head", d).kind, "desc");
        compare(sticky.y, Math.round(sticky.y));
        d.close();
    }

    function test_a_long_description_folds_to_its_first_lines() {
        const id = ticket("long", 40, 0);
        host.width = 1100;
        host.height = 700;
        const d = mkDoc(1920);
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
        verify(byName("task-doc-desc-end", d).visible, "no «Конец описания · свернуть»");
        verify(byName("task-doc-desc-fold", d).visible, "no «Свернуть» over the description read whole");
        mouseClick(byName("task-doc-desc-fold", d));
        verify(!d.descExpanded);
        verify(readAll.visible, "folded again, no «Читать целиком»");
        // And from its end.
        mouseClick(readAll);
        const end = byName("task-doc-desc-end", d);
        tryVerify(() => end.visible && tracker.parent.height > d._foldH * 2, 2000);
        // Scrolled to its end, as a reader gets there.
        tryVerify(() => {
            d._keepInView(end, 0, end.height);
            const q = end.mapToItem(d, 0, 0);
            return q.y > 0 && q.y + end.height < d.height;
        }, 2000, "the end of the description never came into view");
        mouseClick(byName("task-doc-desc-end-fold", d));
        verify(!d.descExpanded, "«Конец описания · свернуть» did not fold it");
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
