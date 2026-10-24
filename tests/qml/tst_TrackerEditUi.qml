// Editing a ticket's content from the task (owner item 16, Checklist
// sheet), on the document alone with a stand-in for the controller's edit
// calls: writes off is today's read-only look and one quiet hint; a
// switched-off tracker says so; writes on, "Править e" opens the
// description in an editor (Ctrl+Enter sends, Esc leaves with nothing
// sent), a checklist chip opens the project's statuses (s, digits), an
// item's text and a new item go out, the title goes on Enter and comes back
// on Esc, the due date and the assignee have chips of their own, and a
// conflict shows both versions with the right buttons.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TrackerEditUi"
    when: windowShown
    visible: true
    width: 1400
    height: 900

    Item { id: host; width: 1400; height: 900 }

    // The controller's edit half, as TaskDocument calls it.
    QtObject {
        id: api
        property var disabledTrackers: []
        property bool can: false
        property bool writeOn: false
        property var calls: []
        signal trackerEditFinished(string taskId, string field, string outcome, string message, var detail)
        signal trackerAssigneesFound(string taskId, string query, var users, string error)
        function canEditTrackerContent(id) { return api.can && api.writeOn && api.disabledTrackers.indexOf("jira") < 0; }
        function trackerCanEditContent(p) { return p === "jira"; }
        function trackerContentWriteEnabled(p) { return api.writeOn; }
        function trackerDescriptionSource(id) { return "Source **text** of the ticket"; }
        function smartChecklistStatuses(id) {
            return [{ name: "To Do", category: "open", builtin: true },
                    { name: "In Progress", category: "in-progress", builtin: true },
                    { name: "IN QA", category: "in-progress", builtin: false },
                    { name: "Done", category: "done", builtin: true }];
        }
        function _call(c) { api.calls = api.calls.concat([c]); }
        function editTrackerDescription(id, text) { api._call({ fn: "description", id: id, text: text }); }
        function editTrackerField(id, field, value) { api._call({ fn: "field", id: id, field: field, value: value }); }
        function setTrackerChecklistItemStatus(id, index, name) { api._call({ fn: "status", id: id, index: index, name: name }); }
        function setTrackerChecklistItemText(id, index, text) { api._call({ fn: "text", id: id, index: index, text: text }); }
        function addTrackerChecklistItem(id, text) { api._call({ fn: "add", id: id, text: text }); }
        function overwriteTrackerEdit(id, field) { api._call({ fn: "overwrite", id: id, field: field }); }
        function discardTrackerEdit(id, field) { api._call({ fn: "discard", id: id, field: field }); }
        function searchTrackerAssignees(id, q) { api._call({ fn: "search", id: id, query: q }); }
    }

    property string homeProfile: ""
    property var imported: []
    property string savedSettings: ""

    function initTestCase() { tc.savedSettings = AppController.appSettingsJson; }
    function init() {
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        delete s.taskBlocksFolded;
        delete s.taskDocColumns;
        AppController.appSettingsJson = JSON.stringify(s);
        api.disabledTrackers = [];
        api.can = false;
        api.writeOn = false;
        api.calls = [];
    }
    function cleanup() {
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
    function shown(name, root) { const it = byName(name, root); return !!it && it.visible; }
    // A Jira ticket with a description and a checklist whose first line is
    // a header (so the items' indices start at 1).
    function ticket(tag) {
        if (tc.imported.length === 0) tc.homeProfile = AppController.activeProfileId;
        const cl = [{ text: "Group", state: "header" },
                    { text: "First item", state: "todo" },
                    { text: "Second item", state: "progress" },
                    { text: "Third item", state: "done" }];
        AppController.importProfileFromJson(JSON.stringify({ profile: {
            id: "edit-" + tag, name: "Edit " + tag, color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }, { id: "done", name: "Done", color: "#888888" }],
            tasks: [{ id: "TE-1", title: "edit " + tag, status: "todo", priority: "P2", desc: "The ticket's text.",
                      externalId: "EDT-1", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/EDT-1",
                      externalMeta: { remoteDetails: { smartChecklist: cl } } }] } }), true);
        tc.imported.push(AppController.activeProfileId);
        const m = AppController.tasks;
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), m.roleOf("title")) === "edit " + tag) return m.data(m.index(i, 0), m.roleOf("id"));
        fail("the ticket was not imported");
        return "";
    }
    function mkDoc(id) {
        const d = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(d !== null);
        d.windowWidth = 1600 * Theme.scale;
        d.trackerApi = api;
        d.open(id);
        tryCompare(d, "opened", true);
        // The panel's slide in, done: a click lands where the button is.
        wait(Theme.durMove + 100);
        return d;
    }
    function callsOf(fn) { return api.calls.filter(c => c.fn === fn); }

    // ── writes off: today's look, one hint ──

    function test_writes_off_has_no_edit_controls() {
        const id = ticket("off");
        const d = mkDoc(id);
        verify(!d._canEdit);
        verify(shown("task-doc-tracker-why", d), "the read-only mark is gone");
        verify(!shown("task-doc-desc-edit", d), "an edit button with writes off");
        verify(!shown("task-doc-tracker-due", d));
        verify(!shown("task-doc-tracker-assignee", d));
        verify(!shown("smart-cl-add", d), "+ item with writes off");
        const chip = byName("smart-cl-state-1", d);
        verify(chip.text.indexOf("▾") < 0, "a chip with a caret: " + chip.text);
        byName("smart-cl-item-1", d).forceActiveFocus();
        verify(!byName("checklist-status-caret", byName("smart-cl-chip-1", d)).visible, "a caret with writes off");
        const note = byName("task-doc-edit-note", d);
        verify(note.visible);
        compare(note.text, I18n.t("taskdoc.edit.hint"));
        // e and a do nothing; the title saves as a local title, as before.
        d.focusDocument();
        keyClick(Qt.Key_E);
        keyClick(Qt.Key_A);
        verify(!d.descEditing);
        verify(!byName("task-doc-desc-editor", d).visible);
        compare(api.calls.length, 0);
        d.destroy();
    }

    function test_switched_off_tracker_says_so() {
        const id = ticket("disabled");
        api.writeOn = true;
        api.can = true;
        api.disabledTrackers = ["jira"];
        const d = mkDoc(id);
        verify(d._trackerOff);
        verify(!d._canEdit);
        verify(shown("task-doc-tracker-off", d), "no word that Jira is off");
        compare(byName("task-doc-tracker-off", d).text, I18n.t("taskdoc.edit.trackerOff").arg("Jira"));
        verify(!shown("task-doc-tracker-why", d));
        verify(!shown("task-doc-desc-edit", d));
        verify(!shown("smart-cl-add", d));
        verify(!shown("task-doc-tracker-due", d));
        // The hint to switch writing on is not this case's.
        verify(!shown("task-doc-edit-note", d));
        d.destroy();
    }

    // ── writes on ──

    function test_description_edit_and_save() {
        const id = ticket("desc");
        api.can = true;
        api.writeOn = true;
        const d = mkDoc(id);
        verify(d._canEdit);
        verify(shown("task-doc-desc-edit", d), "no «Править e»");
        verify(!shown("task-doc-tracker-why", d));
        compare(byName("task-doc-edit-note", d).text, I18n.t("taskdoc.edit.goesTo").arg("Jira"));
        d.focusDocument();
        keyClick(Qt.Key_E);
        tryCompare(d, "descEditing", true);
        verify(byName("task-doc-desc-editor", d).visible);
        const ed = byName("task-doc-desc-editor-body", d);
        compare(ed.source.text, "Source **text** of the ticket");
        verify(shown("task-doc-desc-line", d), "no «Описание · правка» over the editor");
        // Ctrl+Enter sends what is in the editor.
        tryVerify(() => ed.editor.activeFocus, 1000, "the caret is not in the editor");
        ed.editor.cursorPosition = ed.editor.length;
        keyClick(Qt.Key_Exclam);
        keyClick(Qt.Key_Return, Qt.ControlModifier);
        tryCompare(d, "descEditing", false);
        const sent = callsOf("description");
        compare(sent.length, 1);
        compare(sent[0].id, id);
        compare(sent[0].text, "Source **text** of the ticket!");
        compare(byName("task-doc-edit-note", d).text, I18n.t("taskdoc.edit.sending").arg("Jira"));
        // ok: a quiet word, and the draft is gone.
        api.trackerEditFinished(id, "description", "ok", "", {});
        compare(byName("task-doc-edit-note", d).text, I18n.t("taskdoc.edit.saved").arg("Jira"));
        compare(d._descDraft, "");
        d.destroy();
    }

    function test_esc_leaves_the_edit_without_saving() {
        const id = ticket("esc");
        api.can = true;
        api.writeOn = true;
        const d = mkDoc(id);
        verify(d.startDescEdit());
        const ed = byName("task-doc-desc-editor-body", d);
        tryVerify(() => ed.editor.activeFocus, 1000);
        keyClick(Qt.Key_X);
        keyClick(Qt.Key_Escape);
        tryCompare(d, "descEditing", false);
        compare(callsOf("description").length, 0, "Esc sent the description");
        verify(d.opened, "the first Esc closed the task");
        // The keyboard is back in the document: the next Esc closes it.
        tryVerify(() => d.activeFocus || (d.Window.activeFocusItem && find(d, it => it === d.Window.activeFocusItem)), 1000);
        keyClick(Qt.Key_Escape);
        tryCompare(d, "opened", false);
        // Opened again: the tracker's text, not the abandoned one.
        d.open(id);
        verify(d.startDescEdit());
        compare(byName("task-doc-desc-editor-body", d).source.text, "Source **text** of the ticket");
        // Cancel by its button too.
        mouseClick(byName("task-doc-desc-cancel", d));
        tryCompare(d, "descEditing", false);
        compare(callsOf("description").length, 0);
        d.destroy();
    }

    // The Vim keymap reads e as the estimate's key: the document takes it
    // only on a ticket that can be edited; anywhere else the estimate opens.
    function test_e_from_the_keymap() {
        const id = ticket("vim");
        const d = mkDoc(id);
        verify(d.claimedKeys.indexOf("task.estimate") >= 0);
        verify(!d.runClaimed("task.estimate"), "writes off took the estimate's key");
        api.can = true;
        api.writeOn = true;
        const bare = String(AppController.shortcutFor("task.estimate")).toUpperCase() === "E";
        compare(d.runClaimed("task.estimate"), bare);
        compare(d.descEditing, bare);
        d.cancelDescEdit();
        verify(!d.runClaimed("task.done"));
        d.destroy();
    }

    function test_refused_description_comes_back_in_the_editor() {
        const id = ticket("refused");
        api.can = true;
        api.writeOn = true;
        const d = mkDoc(id);
        verify(d.startDescEdit());
        mouseClick(byName("task-doc-desc-save", d));
        tryCompare(d, "descEditing", false);
        compare(callsOf("description").length, 1);
        api.trackerEditFinished(id, "description", "error", "Jira did not take it", { httpStatus: 500 });
        compare(byName("task-doc-edit-note", d).text, "Jira did not take it");
        verify(d.descEditing, "the typed text is gone");
        compare(byName("task-doc-desc-editor-body", d).source.text, "Source **text** of the ticket");
        // Busy and the like: said, nothing else.
        d.cancelDescEdit();
        api.trackerEditFinished(id, "title", "busy", "", {});
        compare(byName("task-doc-edit-note", d).text, I18n.t("taskdoc.edit.notSent").arg("Jira"));
        verify(!d._conflict);
        d.destroy();
    }

    function test_checklist_status_menu_digits_and_index() {
        const id = ticket("status");
        api.can = true;
        api.writeOn = true;
        const d = mkDoc(id);
        const list = byName("task-doc-smart-checklist", d);
        verify(list.editable);
        compare(list.statuses.length, 4);
        // Row 0 is the header; the first item is row 1, index 1 in the list.
        // The status is a soft chip (owner pick «Мягкие чипы»), "IN QA"
        // not shouted; with the keyboard on its row a quiet ▾ says it opens.
        const chip = byName("smart-cl-state-1", d);
        verify(chip.text.length > 0 && chip.text.indexOf("▾") < 0, chip.text);
        const caret = byName("checklist-status-caret", byName("smart-cl-chip-1", d));
        verify(!caret.visible, "a caret before the keyboard is on the row");
        const row = byName("smart-cl-item-1", d);
        row.forceActiveFocus();
        verify(row.activeFocus);
        verify(caret.visible, "no ▾ on the chip of the row the keyboard is on");
        keyClick(Qt.Key_S);
        const menu = list.statusMenu;
        tryCompare(menu, "opened", true);
        // The digits are written in the menu and pick.
        compare(menu.count, 4);
        keyClick(Qt.Key_3);
        tryCompare(menu, "opened", false);
        const sent = callsOf("status");
        compare(sent.length, 1);
        compare(sent[0].index, 1);
        compare(sent[0].name, "IN QA");
        // A click on a chip opens it too; Esc closes it with nothing sent.
        tryVerify(() => !menu.visible, 2000);
        waitForRendering(d);
        mouseClick(byName("smart-cl-chip-button-2", d));
        tryCompare(menu, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "opened", false);
        compare(callsOf("status").length, 1);
        d.destroy();
    }

    function test_checklist_text_and_new_item() {
        const id = ticket("text");
        api.can = true;
        api.writeOn = true;
        const d = mkDoc(id);
        const row = byName("smart-cl-item-2", d);
        row.forceActiveFocus();
        keyClick(Qt.Key_Return);
        const field = byName("smart-cl-edit-2", d);
        tryVerify(() => field.visible && field.activeFocus, 1000);
        compare(field.text, "Second item");
        // Esc: nothing sent, the row keeps the keyboard.
        keyClick(Qt.Key_Z);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !field.visible);
        compare(callsOf("text").length, 0);
        verify(row.activeFocus, "the row lost the keyboard");
        // Enter sends the new text by the item's index in the list.
        keyClick(Qt.Key_F2);
        tryVerify(() => field.activeFocus, 1000);
        field.selectAll();
        keyClick(Qt.Key_N);
        keyClick(Qt.Key_Return);
        tryVerify(() => !field.visible);
        const sent = callsOf("text");
        compare(sent.length, 1);
        compare(sent[0].index, 2);
        compare(sent[0].text, "n");
        // a: a new item at the end.
        keyClick(Qt.Key_A);
        const add = byName("smart-cl-add-field", d);
        tryVerify(() => add.visible && add.activeFocus, 1000);
        keyClick(Qt.Key_Q);
        keyClick(Qt.Key_Return);
        tryVerify(() => !add.visible);
        compare(callsOf("add").length, 1);
        compare(callsOf("add")[0].text, "q");
        // Esc on a new one sends nothing.
        mouseClick(byName("smart-cl-add-button", d));
        tryVerify(() => add.activeFocus, 1000);
        keyClick(Qt.Key_W);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !add.visible);
        compare(callsOf("add").length, 1);
        d.destroy();
    }

    function test_title_goes_on_enter_and_back_on_esc() {
        const id = ticket("title");
        api.can = true;
        api.writeOn = true;
        const d = mkDoc(id);
        const title = byName("task-doc-title", d);
        title.forceActiveFocus();
        title.cursorPosition = title.length;
        keyClick(Qt.Key_X);
        compare(byName("task-doc-edit-note", d).text, I18n.t("taskdoc.edit.titleKeys").arg("Jira"));
        keyClick(Qt.Key_Escape);
        compare(title.text, "edit title");
        verify(d.opened, "Esc on an edited title closed the task");
        compare(callsOf("field").length, 0);
        title.forceActiveFocus();
        title.cursorPosition = title.length;
        keyClick(Qt.Key_Y);
        keyClick(Qt.Key_Return);
        const sent = callsOf("field");
        compare(sent.length, 1);
        compare(sent[0].field, "title");
        compare(sent[0].value, "edit titley");
        // Not saved as a local title meanwhile: the controller shows it.
        wait(800);
        compare(AppController.taskById(id).title, "edit title");
        d.destroy();
    }

    function test_due_and_assignee_chips() {
        const id = ticket("fields");
        api.can = true;
        api.writeOn = true;
        const d = mkDoc(id);
        const due = byName("task-doc-tracker-due", d);
        verify(due.visible);
        const picker = d.trackerDuePicker;
        due.clicked();
        tryCompare(picker, "opened", true);
        picker.picked(new Date(2026, 10, 3));
        compare(callsOf("field")[0].field, "due");
        compare(callsOf("field")[0].value, "2026-11-03");
        picker.close();
        picker.cleared();
        compare(callsOf("field")[1].value, "");
        // The assignee: the tracker is asked, a person picked.
        const chip = byName("task-doc-tracker-assignee", d);
        verify(chip.visible);
        chip.clicked();
        const ap = d.assigneePicker;
        tryCompare(ap, "opened", true);
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_N);
        tryVerify(() => callsOf("search").length === 1, 2000, "nobody was asked");
        compare(callsOf("search")[0].query, "an");
        api.trackerAssigneesFound(id, "an", [{ id: "u1", name: "Anna", email: "anna@example.invalid" }], "");
        compare(ap.rows.length, 1);
        keyClick(Qt.Key_Return);
        tryCompare(ap, "opened", false);
        const set = callsOf("field").filter(c => c.field === "assignee");
        compare(set.length, 1);
        compare(set[0].value.id, "u1");
        compare(set[0].value.name, "Anna");
        d.destroy();
    }

    function test_conflict_panel_buttons() {
        const id = ticket("conflict");
        api.can = true;
        api.writeOn = true;
        const d = mkDoc(id);
        const panel = byName("tracker-edit-conflict", d);
        verify(!panel.visible);
        api.trackerEditFinished(id, "description", "conflict", "changed", { mine: "my text", theirs: "their text" });
        tryVerify(() => panel.visible);
        // Laid out under the chips first: the button is where it is drawn.
        waitForRendering(d);
        compare(byName("tracker-edit-conflict-mine-text", panel).text, "my text");
        compare(byName("tracker-edit-conflict-theirs-text", panel).text, "their text");
        verify(byName("tracker-edit-conflict-keep", panel).visible);
        verify(byName("tracker-edit-conflict-take", panel).visible);
        verify(!byName("tracker-edit-conflict-ok", panel).visible);
        mouseClick(byName("tracker-edit-conflict-keep", panel));
        tryVerify(() => !panel.visible);
        compare(callsOf("overwrite").length, 1);
        compare(callsOf("overwrite")[0].field, "description");
        // Again, and take theirs.
        api.trackerEditFinished(id, "title", "conflict", "changed", { mine: "A", theirs: "B" });
        tryVerify(() => panel.visible);
        waitForRendering(d);
        mouseClick(byName("tracker-edit-conflict-take", panel));
        tryVerify(() => !panel.visible);
        compare(callsOf("discard").length, 1);
        compare(callsOf("discard")[0].field, "title");
        compare(callsOf("overwrite").length, 1);
        // A checklist has no overwrite: shown, then dropped.
        api.trackerEditFinished(id, "checklist", "conflict", "changed",
                                { mine: [{ text: "One", state: "done" }], theirs: [{ text: "One", state: "todo" }] });
        tryVerify(() => panel.visible);
        waitForRendering(d);
        verify(!byName("tracker-edit-conflict-keep", panel).visible);
        verify(!byName("tracker-edit-conflict-take", panel).visible);
        verify(byName("tracker-edit-conflict-ok", panel).visible);
        verify(byName("tracker-edit-conflict-mine-text", panel).text.indexOf("One") >= 0);
        mouseClick(byName("tracker-edit-conflict-ok", panel));
        tryVerify(() => !panel.visible);
        compare(callsOf("overwrite").length, 1);
        compare(callsOf("discard").length, 2);
        // Another task's answer is not this one's.
        api.trackerEditFinished("someone-else", "title", "conflict", "x", { mine: "A", theirs: "B" });
        verify(!panel.visible);
        d.destroy();
    }
}
