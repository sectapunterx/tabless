// The local layer in the task document and on the card (APP-236…241, 251):
// the checklist from the keyboard, its two tick marks, links by search, the
// comment draft, my tags with completion, and the card's small marks.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TaskLocal"
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
    function mkTask(title) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        AppController.saveTask(d);
        tc.made.push(d.id);
        return d.id;
    }
    function make() {
        const doc = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(doc !== null);
        return doc;
    }
    function typeText(s) { for (let i = 0; i < s.length; i++) keyClick(s[i]); }
    function itemRow(doc, i) { return findChild(doc, "cl-item-" + i); }

    function test_checklist_from_the_keyboard() {
        const id = mkTask("plan probe");
        const doc = make();
        doc.open(id);
        // The plan is hidden while empty; "+ свойство → План" starts it (DG-062).
        tryVerify(() => findChild(doc, "cl-add") !== null, 1000, "no plan in the body's tail");
        const add = findChild(doc, "cl-add");
        verify(!findChild(doc, "task-doc-checklist").visible, "an empty plan is drawn at rest");
        doc.startPlan();
        tryVerify(() => add.visible && add.activeFocus, 1000, "the plan did not start");
        typeText("repro");
        keyClick(Qt.Key_Return);
        tryCompare(AppController.taskChecklist(id), "length", 1);
        typeText("fix");
        keyClick(Qt.Key_Return);
        tryVerify(() => AppController.taskChecklist(id).length === 2, 1000);
        // Up from the empty field lands on the last item; Alt+→ nests it
        // (Tab moves on from a row, A1-11).
        keyClick(Qt.Key_Up);
        tryVerify(() => itemRow(doc, 1) && itemRow(doc, 1).activeFocus, 1000, "the last item did not get the keyboard");
        keyClick(Qt.Key_Right, Qt.AltModifier);
        tryVerify(() => AppController.taskChecklist(id)[1].level === 2, 1000, "Alt+Right did not nest the item");
        // Space ticks it: the only child done closes the parent, automatically.
        tryVerify(() => itemRow(doc, 1) && itemRow(doc, 1).activeFocus, 1000, "the keyboard was lost after Tab");
        keyClick(Qt.Key_Space);
        tryVerify(() => AppController.taskChecklist(id)[1].done, 1000, "Space did not tick");
        const items = AppController.taskChecklist(id);
        verify(items[0].done && items[0].autoDone, "the parent was not closed automatically");
        tryVerify(() => findChild(itemRow(doc, 0), "cl-check-auto") !== null, 1000, "no auto mark on the parent");
        verify(findChild(itemRow(doc, 1), "cl-check-manual") !== null, "no manual mark on the child");
        compare(AppController.taskChecklistText(id), "- [x] repro\n-- [x] fix");
        // Alt+Up on the parent cannot move it (first), Delete removes it all.
        itemRow(doc, 0).forceActiveFocus();
        keyClick(Qt.Key_Delete);
        tryCompare(AppController.taskChecklist(id), "length", 0);
    }

    function test_a_pasted_list_keeps_levels() {
        const id = mkTask("paste probe");
        const doc = make();
        doc.open(id);
        tryVerify(() => findChild(doc, "cl-add") !== null, 1000);
        const add = findChild(doc, "cl-add");
        doc.startPlan();
        tryVerify(() => add.activeFocus, 1000);
        add.text = "- a\n-- [x] b\n--- c";
        keyClick(Qt.Key_Return);
        tryVerify(() => AppController.taskChecklist(id).length === 3, 1000);
        const items = AppController.taskChecklist(id);
        compare(items.map(i => i.level), [1, 2, 3]);
    }

    function test_link_by_search() {
        const a = mkTask("link probe alpha");
        const b = mkTask("zebrafish target");
        const doc = make();
        doc.open(a);
        // No links: the section is not drawn; "+ свойство → Связь" starts one.
        verify(!findChild(doc, "task-doc-links").visible, "an empty links section is drawn");
        doc.startLink("related");
        const field = findChild(doc, "task-doc-link-field");
        tryVerify(() => field.visible, 1000, "the field did not show");
        tryVerify(() => field.activeFocus, 1000, "the field has no keyboard");
        typeText("zebrafish");
        tryVerify(() => findChild(doc, "task-doc-link-match-0") !== null, 1000, "no search match shown");
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        tryVerify(() => AppController.taskRelations(a).length === 1, 1000, "the link was not added");
        compare(AppController.taskRelations(a)[0].target, b);
        compare(AppController.taskRelations(b).length, 1, "a related link is two-sided");
    }

    function test_link_by_url() {
        const a = mkTask("url probe");
        const doc = make();
        doc.open(a);
        doc.startLink("related");
        const field = findChild(doc, "task-doc-link-field");
        tryVerify(() => field.activeFocus, 1000);
        typeText("https://gitlab.example/a/-/merge_requests/17");
        keyClick(Qt.Key_Return);
        tryVerify(() => AppController.taskRelations(a).length === 1, 1000);
        verify(!AppController.taskRelations(a)[0].isTask);
    }

    function test_tags_with_completion() {
        const a = mkTask("tag source");
        AppController.setTaskLocalTags(a, ["after-release"]);
        const b = mkTask("tag probe");
        const doc = make();
        doc.open(b);
        const editor = findChild(doc, "task-doc-tags-editor");
        editor.visible = true;
        editor.open();
        const field = findChild(doc, "task-doc-tags-field");
        tryVerify(() => field.activeFocus, 1000);
        typeText("#aft");
        tryVerify(() => findChild(doc, "task-doc-tags-match-0") !== null, 1000, "no completion");
        keyClick(Qt.Key_Tab);
        compare(field.text.trim(), "#after-release");
        keyClick(Qt.Key_Return);
        tryVerify(() => AppController.taskById(b).localTags.length === 1, 1000);
        compare(AppController.taskById(b).localTags[0].id, "after-release");
        compare(AppController.compileSearch("#after-release").ids.length, 2);
    }

    function test_session_added_by_hand() {
        const id = mkTask("session probe");
        verify(AppController.addTaskSession(id, new Date(2026, 9, 8, 9, 0), new Date(2026, 9, 8, 10, 30)));
        const doc = make();
        doc.open(id);
        // Время may be folded (dogfood 2026-10-23): open it, and let the
        // panel slide in.
        doc.openBlock("time");
        wait(Theme.durMove + 100);
        const toggle = findChild(doc, "task-doc-sessions-toggle");
        tryVerify(() => toggle.visible, 1000);
        mouseClick(toggle);
        tryVerify(() => findChild(doc, "task-doc-session-0") !== null, 1000);
        compare(AppController.taskById(id).trackedSeconds, 5400);
    }

    function test_card_marks() {
        const card = createTemporaryQmlObject('import TodoCpp; TaskCardLocal { width: 300 }', host);
        verify(!card.hasContent, "an empty layer drew something");
        card.task = { checklist: { next: "write header", done: 1, total: 3 },
                      local: { notes: true, draft: true, tags: [{ id: "quick", color: "" }], trackerChanged: false },
                      estimateMinutes: 120, trackedSeconds: 12000 };
        verify(card.hasContent);
        compare(findChild(card, "tc-next-step").text, "→ write header");
        verify(findChild(card, "tc-has-notes").visible);
        verify(findChild(card, "tc-has-draft").visible);
        verify(!findChild(card, "tc-tracker-changed").visible);
        verify(card.tip.length > 0, "estimate vs spent missing from the tip");
        card.task = { checklist: { next: "", done: 3, total: 3 }, local: {} };
        verify(!card.hasContent, "all done and nothing local still drew");
    }
}
