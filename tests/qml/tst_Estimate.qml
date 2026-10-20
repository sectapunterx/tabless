// The estimate in the ticket's card (dogfood 2026-10-20, item 19): a chip in
// the property row ("7ч 30м / 10ч", story points apart), a popover under it
// on click, Enter on the chip or E, Jira's units, nothing sent anywhere; and a
// quiet line on the board card, in the warning colour over the estimate.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "Estimate"
    when: windowShown
    visible: true
    width: 1400
    height: 860

    Item { id: host; width: 1400; height: 860 }
    Component {
        id: cardComp
        TaskCard { width: 300 }
    }
    property string importedProfile: ""
    property string homeProfile: ""

    function cleanup() {
        if (tc.importedProfile.length > 0) {
            AppController.activeProfileId = tc.homeProfile;
            AppController.deleteProfile(tc.importedProfile);
            tc.importedProfile = "";
        }
        AppController.clearPendingUndo();
    }
    function importTicket(estimate, estimateMinutes) {
        tc.homeProfile = AppController.activeProfileId;
        const meta = { status: "To Do", column: "todo" };
        if (estimate) meta.remoteDetails = { estimate: estimate };
        AppController.importProfileFromJson(JSON.stringify({ profile: {
            id: "est-" + Date.now(), name: "Estimate", color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }],
            tasks: [{ id: "ES-1", title: "estimate ticket", status: "todo", priority: "P2",
                      estimateMinutes: estimateMinutes,
                      externalId: "PAY-412", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/PAY-412",
                      externalMeta: meta }] } }), true);
        tc.importedProfile = AppController.activeProfileId;
        verify(tc.importedProfile !== tc.homeProfile, "the import did not open its profile");
        const m = AppController.tasks;
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), m.roleOf("title")) === "estimate ticket") return m.data(m.index(i, 0), m.roleOf("id"));
        fail("the ticket was not imported");
        return "";
    }
    function make() {
        const doc = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(doc !== null);
        return doc;
    }
    function within(item, anc) {
        for (let p = item; p; p = p.parent) if (p === anc) return true;
        return false;
    }
    function fmt(m) { return AppController.formatDuration(m, "jira"); }
    function popupOf(doc) { return findChild(doc, "estimate-popup"); }
    function openedPopup(doc) {
        const p = popupOf(doc);
        verify(p !== null, "no estimate popover");
        tryVerify(() => p.opened, 1000, "the popover did not open");
        const input = findChild(p.contentItem, "estimate-input");
        tryVerify(() => input.activeFocus, 1000, "the field has no keyboard");
        return p;
    }
    // Latin keys are typed; QtTest has no key for a Cyrillic letter, so a
    // Russian duration goes into the field as text.
    function typeIn(p, text) {
        const input = findChild(p.contentItem, "estimate-input");
        input.selectAll();
        keyClick(Qt.Key_Delete);
        if (/[^ -~]/.test(text)) { input.insert(0, text); return; }
        for (let i = 0; i < text.length; i++) keyClick(text[i]);
    }

    function test_the_chip_reads_from_the_trackers_estimate() {
        const id = importTicket({ original: 36000, remaining: 9000, spent: 27000, points: 3 }, 0);
        const doc = make();
        doc.open(id);
        const chip = findChild(doc, "task-doc-estimate");
        tryVerify(() => chip.visible, 1000, "no estimate chip on an estimated ticket");
        compare(chip.value, fmt(450) + " / " + fmt(600));
        compare(chip.icon, "timer");
        const sp = findChild(doc, "task-doc-points");
        verify(sp.visible, "no story points chip");
        compare(sp.value, "3 sp");
        // Read only: the tracker's estimate is not made mine.
        compare(AppController.taskById(id).estimateMinutes, 0);
        doc.close();
    }

    function test_mine_wins_and_over_the_estimate_warns() {
        const id = importTicket({ original: 36000, spent: 27000 }, 360);
        const doc = make();
        doc.open(id);
        const chip = findChild(doc, "task-doc-estimate");
        tryVerify(() => chip.visible, 1000);
        compare(chip.value, fmt(450) + " / " + fmt(360) + " · +" + fmt(90));
        compare(chip.valueColor, Theme.warning);
        doc.close();
    }

    function test_nothing_to_say_shows_nothing() {
        const id = importTicket(null, 0);
        const doc = make();
        doc.open(id);
        wait(50);
        verify(!findChild(doc, "task-doc-estimate").visible, "an empty estimate chip");
        verify(!findChild(doc, "task-doc-points").visible);
        doc.close();
    }

    // E (openEstimate, as Main runs it): the popover with the field filled,
    // spent as "Jira … ", the units of the site; Enter saves mine and the
    // keyboard goes back to the chip.
    function test_e_opens_it_and_enter_saves_mine() {
        const id = importTicket({ original: 36000, spent: 18000 }, 0);
        const doc = make();
        doc.open(id);
        doc.focusDocument();
        doc.openEstimate();
        const p = openedPopup(doc);
        const input = findChild(p.contentItem, "estimate-input");
        compare(input.text, fmt(600));
        compare(findChild(p.contentItem, "estimate-spent").text, fmt(300));
        compare(findChild(p.contentItem, "estimate-left").text, fmt(300));
        verify(findChild(p.contentItem, "estimate-units").text.indexOf("Jira") >= 0);
        verify(findChild(p.contentItem, "estimate-local-only").text.indexOf("Jira") >= 0, "it does not say the estimate stays here");
        typeIn(p, "1d 3h");
        keyClick(Qt.Key_Return);
        tryVerify(() => !p.opened, 1000, "Enter did not close it");
        compare(AppController.taskById(id).estimateMinutes, 660);
        const chip = findChild(doc, "task-doc-estimate");
        tryVerify(() => tc.within(doc.Window.activeFocusItem, chip), 1000, "the keyboard did not come back to the chip");
        verify(!AppController.trackerWriteEnabled("jira"), "tracker writes are off by default");
        doc.close();
    }

    // Enter as it came: the tracker's estimate is not copied into mine.
    function test_enter_on_the_prefill_keeps_it_the_trackers() {
        const id = importTicket({ original: 36000 }, 0);
        const doc = make();
        doc.open(id);
        doc.openEstimate();
        const p = openedPopup(doc);
        keyClick(Qt.Key_Return);
        tryVerify(() => !p.opened, 1000);
        compare(AppController.taskById(id).estimateMinutes, 0);
        doc.close();
    }

    function test_esc_cancels_and_junk_is_not_saved() {
        const id = importTicket({ original: 36000 }, 0);
        const doc = make();
        doc.open(id);
        doc.openEstimate();
        let p = openedPopup(doc);
        typeIn(p, "soon");
        keyClick(Qt.Key_Return);
        verify(p.opened, "a duration it could not read closed the popover");
        verify(findChild(p.contentItem, "estimate-error").visible, "no word on what was wrong");
        compare(AppController.taskById(id).estimateMinutes, 0);
        typeIn(p, "3h");
        verify(!findChild(p.contentItem, "estimate-error").visible, "the error outlived the edit");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !p.opened, 1000, "Esc did not close it");
        compare(AppController.taskById(id).estimateMinutes, 0);
        verify(doc.opened, "Esc went through to the document and closed it");
        doc.close();
    }

    // Enter on the chip opens it too; a local task gets it from + property.
    function test_enter_on_the_chip_and_a_local_task() {
        const id = importTicket({ original: 3600 }, 0);
        const doc = make();
        doc.open(id);
        const chip = findChild(doc, "task-doc-estimate");
        tryVerify(() => chip.visible, 1000);
        chip.focusChip();
        keyClick(Qt.Key_Return);
        const p = openedPopup(doc);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !p.opened, 1000);
        doc.close();

        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = "estimate local probe";
        AppController.saveTask(d);
        const local = d.id;
        const doc2 = make();
        doc2.open(local);
        verify(!findChild(doc2, "task-doc-estimate").visible);
        doc2.openEstimate();
        const p2 = openedPopup(doc2);
        verify(findChild(doc2, "task-doc-estimate").visible, "the chip did not come out for the popover");
        typeIn(p2, "1н 2д");
        keyClick(Qt.Key_Return);
        tryVerify(() => !p2.opened, 1000);
        compare(AppController.taskById(local).estimateMinutes, 7 * 8 * 60);
        AppController.deleteTask(local);
        doc2.close();
    }

    function test_the_board_card_line() {
        const plain = cardComp.createObject(host, { task: { id: "EC-0", title: "no estimate", status: "todo", priority: "P2",
                                                             labels: [], ticket: ({}), checklist: ({}) } });
        verify(!findChild(plain, "tc-effort").visible, "a line on a card with no estimate");
        plain.destroy();

        const card = cardComp.createObject(host, { task: { id: "EC-1", title: "estimated", status: "todo", priority: "P2",
                                                            labels: [], checklist: ({}), estimateMinutes: 0, trackedSeconds: 1800,
                                                            ticket: ({ provider: "jira", key: "PAY-412", originalMinutes: 600, spentMinutes: 420 }) } });
        const line = findChild(card, "tc-effort");
        verify(line.visible);
        compare(line.text, fmt(450) + " / " + fmt(600));
        compare(line.color, Theme.textMuted);
        card.destroy();

        const over = cardComp.createObject(host, { task: { id: "EC-2", title: "over", status: "todo", priority: "P2",
                                                            labels: [], checklist: ({}), estimateMinutes: 480, trackedSeconds: 0,
                                                            ticket: ({ provider: "jira", key: "PAY-398", spentMinutes: 720 }) } });
        const overLine = findChild(over, "tc-effort");
        compare(overLine.text, fmt(720) + " / " + fmt(480) + " · +" + fmt(240));
        compare(overLine.color, Theme.warning);
        over.destroy();
    }
}
