// The task document where its meta column is folded away (0.9 acceptance,
// APP-221 B4): the tracker card is named by its key and says the tracker is
// not told; the notes that mention it still show; Tab reaches the body; a
// monthly rule keeps its day; a toast clears the bottom Done.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TaskDocumentNarrow"
    when: windowShown
    visible: true
    width: 1100
    height: 760

    // Narrower than the 820 px the meta column needs.
    Item { id: host; width: 700; height: 760 }
    property var made: []
    property string importedProfile: ""
    property string homeProfile: ""
    function cleanup() {
        for (const id of tc.made) AppController.deleteTask(id);
        tc.made = [];
        if (tc.importedProfile.length > 0) {
            AppController.activeProfileId = tc.homeProfile;
            AppController.deleteProfile(tc.importedProfile);
            tc.importedProfile = "";
        }
        AppController.clearPendingUndo();
    }
    function mkTask(title, extra) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        for (const k in (extra || {})) d[k] = extra[k];
        AppController.saveTask(d);
        tc.made.push(d.id);
        return d.id;
    }
    function make() {
        const doc = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(doc !== null);
        return doc;
    }
    // A Jira card, as a sync leaves it: id TL-…, key PLAT-101, writes off.
    function importTicket() {
        tc.homeProfile = AppController.activeProfileId;
        const json = JSON.stringify({ profile: {
            id: "b4-narrow", name: "B4 narrow", color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }, { id: "done", name: "Done", color: "#888888" }],
            tasks: [{ id: "TL-2", title: "b4 narrow ticket", status: "todo", priority: "P2",
                      externalId: "PLAT-101", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/PLAT-101" }] } });
        AppController.importProfileFromJson(json, true);
        tc.importedProfile = AppController.activeProfileId;
        verify(tc.importedProfile !== tc.homeProfile, "the import did not open its profile");
        const m = AppController.tasks;
        const rId = m.roleOf("id"), rTitle = m.roleOf("title");
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), rTitle) === "b4 narrow ticket") return m.data(m.index(i, 0), rId);
        fail("the ticket was not imported");
        return "";
    }

    // A1-29, A3-10: the crumb reads PLAT-101, and the folded document still
    // says it is a Jira card whose changes stay here.
    function test_a_ticket_is_named_by_its_key_and_says_it_stays_local() {
        const id = importTicket();
        const doc = make();
        doc.open(id);
        verify(!findChild(doc, "task-doc-side").visible, "the meta column should be folded at 700 px");
        compare(findChild(doc, "task-doc-crumb-id").text, "PLAT-101");
        const inline = findChild(doc, "task-doc-tracker-inline");
        verify(inline.visible, "no tracker line while the meta column is folded");
        verify(inline.text.indexOf("PLAT-101") >= 0, inline.text);
        verify(findChild(doc, "task-doc-local-only").visible, "nothing says the tracker is not told");
        doc.close();
    }

    // A4-14: Tab goes title → chips → body, not round the chips for ever.
    function test_tab_reaches_the_body() {
        const id = mkTask("b4 tab probe");
        const doc = make();
        doc.open(id);
        doc.focusTitle();
        const body = findChild(doc, "task-doc-body");
        let reached = false;
        for (let i = 0; i < 20 && !reached; i++) {
            keyClick(Qt.Key_Tab);
            wait(10);
            reached = body.editing;
        }
        verify(reached, "Tab never reached the body");
        doc.close();
    }

    // A2-22: every:month:1 reads "…1…", and the menu marks it.
    function test_a_monthly_rule_keeps_its_day() {
        const id = mkTask("b4 recur probe", { recurrence: "every:month:1" });
        const doc = make();
        doc.open(id);
        const chip = findChild(doc, "task-doc-recurrence");
        verify(chip.value.indexOf("1") >= 0, chip.value);
        verify(doc._recurOptions.indexOf("every:month:1") >= 0, JSON.stringify(doc._recurOptions));
        verify(doc._recurOptions.some(r => /^every:(mon|tue|wed|thu|fri|sat|sun)$/.test(r)), "no weekday rule offered");
        doc.close();
    }

    // A2-34 / A1-30: with the bottom Done showing, a toast rises above it.
    function test_the_toast_clears_the_bottom_done() {
        const id = mkTask("b4 toast probe");
        const doc = make();
        doc.open(id);
        const done = findChild(doc, "task-doc-done-bottom");
        verify(done.visible);
        verify(doc.toastLift >= done.height, "lift " + doc.toastLift + " < " + done.height);
        doc.close();
        compare(doc.toastLift, 0);
    }

    // 0.9.4: a long ticket on a short panel (a 720p screen at 125 %). The
    // tracker text took its preferred height and the rest ran off the panel:
    // my notes, the comments and Done could not be reached. Since dogfood
    // 2026-10-23 a long text folds to its first lines in the one scroll, so
    // my notes and Done come right under it.
    function test_a_long_ticket_leaves_my_notes_and_done_on_a_short_panel_data() {
        return [ { tag: "480", h: 480 }, { tag: "560", h: 560 }, { tag: "760", h: 760 } ];
    }
    function test_a_long_ticket_leaves_my_notes_and_done_on_a_short_panel(d) {
        const home = AppController.activeProfileId;
        let desc = "";
        for (let i = 1; i <= 40; i++) desc += i + ". A long tracker line that wraps on a narrow panel, number " + i + "

";
        AppController.importProfileFromJson(JSON.stringify({ profile: {
            id: "b4-long-" + d.tag, name: "B4 long " + d.tag, color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }, { id: "done", name: "Done", color: "#888888" }],
            tasks: [{ id: "TL-7", title: "long ticket " + d.tag, status: "todo", priority: "P2", desc: desc,
                      externalId: "PLAT-707", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/PLAT-707" }] } }), true);
        const imported = AppController.activeProfileId;
        verify(imported !== home, "the import did not open its profile");
        const m = AppController.tasks;
        let id = "";
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), m.roleOf("title")) === "long ticket " + d.tag) id = m.data(m.index(i, 0), m.roleOf("id"));
        verify(id.length > 0, "the ticket was not imported");

        host.height = d.h;
        const doc = make();
        doc.open(id);
        const tracker = findChild(doc, "task-doc-tracker-body");
        const body = findChild(doc, "task-doc-body");
        const done = findChild(doc, "task-doc-done-bottom");
        tryVerify(() => tracker.height > 0 && body.height > 0, 2000);
        const bottom = (item) => item.mapToItem(doc, 0, item.height).y;
        verify(done.visible, "Done is folded away");
        verify(bottom(done) <= doc.height + 0.5, "Done ran off a " + d.h + " px panel: " + bottom(done));
        verify(body.height >= Theme.px(90), "my notes got " + body.height + " px at " + d.h);
        const shown = tracker.parent.height;   // the window it is folded into
        verify(shown <= doc.height * 0.62 + 0.5, "the tracker text took " + shown + " of " + doc.height);

        doc.close();
        host.height = 760;
        AppController.activeProfileId = home;
        AppController.deleteProfile(imported);
    }

    // 0.9.4 owner: a long tracker text scrolled to its end shrank its box
    // and reopening kept it so. Now the text has no scroll of its own: the
    // task's one scroll reopens at its top, the text folded again.
    function test_a_scrolled_ticket_reopens_at_its_top() {
        const home = AppController.activeProfileId;
        let desc = "";
        for (let i = 1; i <= 400; i++) desc += i + ". A long tracker paragraph that wraps on the panel and goes on, number " + i + "

";
        AppController.importProfileFromJson(JSON.stringify({ profile: {
            id: "b4-scroll", name: "B4 scroll", color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }],
            tasks: [{ id: "TL-8", title: "scrolled ticket", status: "todo", priority: "P2", desc: desc,
                      externalId: "PLAT-808", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/PLAT-808" }] } }), true);
        const imported = AppController.activeProfileId;
        const m = AppController.tasks;
        let id = "";
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), m.roleOf("title")) === "scrolled ticket") id = m.data(m.index(i, 0), m.roleOf("id"));
        verify(id.length > 0);
        host.height = 760;
        const doc = make();
        doc.open(id);
        const tracker = findChild(doc, "task-doc-tracker-body");
        const scroll = findChild(doc, "task-doc-scroll");
        tryVerify(() => tracker.height > Theme.px(100), 2000, "box " + tracker.height);
        verify(!findChild(tracker, "md-block-view").interactive, "the text scrolls inside itself");
        doc.descExpanded = true;
        tryVerify(() => scroll.contentHeight > scroll.height * 3, 2000);
        scroll.contentY = scroll.contentHeight - scroll.height;
        doc.close();
        doc.open(id);
        compare(scroll.contentY, 0, "reopened away from its top");
        verify(!doc.descExpanded, "reopened unfolded");
        doc.close();
        AppController.activeProfileId = home;
        AppController.deleteProfile(imported);
    }
}
