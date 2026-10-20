// Knowledge from the keyboard, through the real Main (APP-221 acceptance,
// batch B5):
// A4-4  arriving puts the keyboard on the drawn note: g n, g t goes back to
//       Today and writes nothing into the note.
// A4-6  Esc steps out (text → note → list) and stays on the list.
// A4-9  a note deleted from the list's menu: the list keeps the keyboard and
//       u brings the note back.
// IDIOT-KNOW-5  u in Knowledge never undoes a change made on another screen.
// A4-27 F6 walks the list, the note and its links before it leaves.
// A4-7  a [[link]] in the note is a Tab stop; Return follows it.
// A4-12 below 1000 px the links sit under the note instead of vanishing.
// A4-10/11 the rename dialog labels its fields and warns of the same name
//       in another folder.
// A4-17 (the board, same batch) Esc lets go of words typed with "/" while
//       they still find cards.
// VB5-R1 arriving (or a link followed) after a Tab stop had the keyboard
//       lands on the drawn note, not that stop; a letter on a stop is not
//       text; a bare u with nothing of the note's own types nothing.
// VB5-R2 the F6 frame goes once the keyboard leaves the links column.
// VB5-R3 Return on a "linked from" row: the keyboard goes on to that note.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "KnowledgeKeys"
    when: windowShown

    property var win: null
    property var seeded: []

    function initTestCase() {
        // These cases drive the in-place block editor ("live"); notes open
        // split since 0.9.2, so ask for it.
        AppController.appSettingsJson = JSON.stringify(Object.assign({}, JSON.parse(AppController.appSettingsJson || "{}"),
                                                                     { notes: { view: "live" } }));
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 900;
        wait(1200);
        // Today's first-run hero holds a field: the keys here are the view's.
        AppController.markWelcomeSeen();
        tc.win.requestActivate();
    }
    function cleanupTestCase() {
        AppController.currentView = "board";
        if (tc.win) tc.win.destroy();
    }
    function init() {
        AppController.clearPendingUndo();
        tc.win.width = 1456;
    }
    function cleanup() {
        for (let i = 0; i < tc.seeded.length; i++)
            if (AppController.notes.indexOfId(tc.seeded[i]) >= 0) AppController.deleteNote(tc.seeded[i]);
        tc.seeded = [];
        AppController.clearPendingUndo();
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
    function byName(name, root) {
        return find(root || tc.win.contentItem, function (it) { return it.objectName === name; });
    }
    function focusName() {
        const f = tc.win.activeFocusItem;
        return f ? String(f.objectName) : "";
    }
    function inside(name) {
        const host = byName(name);
        for (let p = tc.win.activeFocusItem; p; p = p.parent) if (p === host) return true;
        return false;
    }
    function note(title, body, folder) {
        const id = AppController.newNote(title);
        AppController.setNoteBody(id, body);
        if (folder) AppController.moveNoteToFolder(id, folder);
        tc.seeded.push(id);
        return id;
    }
    function chord(a, b) {
        keyClick(a);
        wait(30);
        keyClick(b);
        wait(150);
    }
    function openNote(id) {
        AppController.currentView = "notes";
        AppController.activeNoteId = id;
        tryVerify(function () { return byName("notes-live") !== null && byName("notes-live").visible; });
        wait(300);
        tc.win.focusActiveView();
        wait(100);
    }

    function test_a_chord_after_arriving_is_a_key_not_text() {
        const id = note("Chord probe", "# Chord probe\n\nbody line");
        AppController.activeNoteId = id;
        AppController.currentView = "board";
        wait(300);
        tc.win.requestActivate();
        tc.win.focusActiveView();
        wait(100);
        chord(Qt.Key_G, Qt.Key_N);
        tryCompare(AppController, "currentView", "notes");
        wait(300);
        // Dogfood 2026-10-20: arriving opens a new, empty note to write in;
        // Esc steps out of its text onto the drawn note, where chords are keys.
        verify(AppController.isFreshNote(AppController.activeNoteId), "Notes did not open on a new note");
        const fresh = AppController.activeNoteId;
        if (tc.win._typing) { keyClick(Qt.Key_Escape); wait(100); }
        verify(!tc.win._typing, "Esc left the caret in the text: " + focusName());
        verify(inside("notes-live"), "the drawn note holds the keyboard");
        chord(Qt.Key_G, Qt.Key_T);
        tryCompare(AppController, "currentView", "today");
        verify(AppController.notes.indexOfId(fresh) < 0, "the empty note stayed behind");
        // The note itself, asked for on the way, is the drawn note.
        AppController.activeNoteId = id;
        AppController.currentView = "notes";
        wait(300);
        tc.win.focusActiveView();
        wait(100);
        compare(AppController.activeNoteId, id);
        verify(!tc.win._typing, "arrived in the text: " + focusName());
        verify(byName("notes-meta").text.indexOf(I18n.t("know.hint.write")) >= 0, "the line says how to write");
        chord(Qt.Key_G, Qt.Key_T);
        tryCompare(AppController, "currentView", "today");
        compare(AppController.noteBody(id), "# Chord probe\n\nbody line", "nothing typed into the note");
    }

    function test_a_letter_that_is_no_key_still_writes() {
        const id = note("Type probe", "# Type probe\n\nfirst");
        openNote(id);
        keyClick(Qt.Key_Q);
        wait(100);
        verify(tc.win._typing, "the caret went into the text");
        AppController.flushSave();
        tc.win.focusActiveView();
        wait(500);
        verify(AppController.noteBody(id).indexOf("firstq") >= 0, AppController.noteBody(id));
    }

    function test_esc_steps_out_and_stays_out() {
        const id = note("Esc probe", "# Esc probe\n\npara");
        openNote(id);
        keyClick(Qt.Key_Return);
        wait(100);
        verify(tc.win._typing, "Return opens the text");
        keyClick(Qt.Key_Escape); wait(100);
        verify(!tc.win._typing && inside("notes-live"), "Esc 1: the drawn note, " + focusName());
        keyClick(Qt.Key_Escape); wait(100);
        verify(inside("note-list"), "Esc 2: the list, " + focusName());
        keyClick(Qt.Key_Escape); wait(100);
        verify(inside("note-list") && !tc.win._typing, "Esc 3 stays on the list, " + focusName());
        keyClick(Qt.Key_Escape); wait(100);
        verify(inside("note-list") && !tc.win._typing, "Esc 4 stays on the list, " + focusName());
    }

    function test_u_brings_back_a_note_deleted_from_the_list() {
        const keep = note("Keep probe", "# Keep probe\n\nkeep");
        const gone = note("Gone probe", "# Gone probe\n\ngone");
        openNote(gone);
        byName("notes-list-pane").focusList();
        wait(100);
        // The row menu (Menu key) → Delete → Return.
        keyClick(Qt.Key_Menu);
        tryVerify(function () { return tc.win._focusInPopup; }, 2000, "the row menu opened, " + focusName());
        for (let i = 0; i < 12 && focusName() !== "note-menu-delete"; i++) { keyClick(Qt.Key_Down); wait(30); }
        compare(focusName(), "note-menu-delete");
        keyClick(Qt.Key_Return);
        tryVerify(function () { return AppController.notes.indexOfId(gone) < 0; }, 2000, "the menu deleted the note");
        wait(300);
        verify(inside("note-list"), "the list keeps the keyboard, " + focusName());
        const other = AppController.activeNoteId;
        const before = AppController.noteBody(other);
        keyClick(Qt.Key_U);
        tryVerify(function () { return AppController.notes.indexOfId(gone) >= 0; }, 2000, "u restored the note");
        compare(AppController.noteBody(other), before, "no u typed into the next note");
        verify(keep.length > 0);
    }

    function test_u_in_knowledge_leaves_other_screens_alone() {
        const id = note("Undo scope probe", "# Undo scope probe\n\ntext");
        // A change on the board, then Knowledge.
        const d = AppController.newTaskDraft("todo");
        d.title = "B5 undo scope task";
        verify(AppController.saveTask(d));
        const t = String(d.id);
        AppController.clearPendingUndo();
        const now = "P0";
        AppController.setTaskPriority(t, now);
        verify(AppController.hasPendingUndo);
        verify(!AppController.undoIsKnowledgeOnly());
        openNote(id);
        keyClick(Qt.Key_U);
        wait(200);
        compare(String(AppController.taskById(t).priority), now, "u did not undo the board's change");
        compare(AppController.noteBody(id), "# Undo scope probe

text", "u was not typed into the note");
        verify(!tc.win._typing, "u left the caret out of the text, " + focusName());
        AppController.deleteTask(t);
    }

    function test_arriving_after_a_tab_stop_lands_on_the_note() {
        const body = "# Stale stop probe

```
let x = 1;
```
";
        const id = note("Stale stop probe", body);
        openNote(id);
        let hit = false;
        for (let i = 0; i < 40 && !hit; i++) {
            keyClick(Qt.Key_Tab);
            hit = focusName() === "mdCodeCopyButton";
        }
        verify(hit, "Tab never reached the code block's Copy");
        // A letter on the button is not text for the note.
        chord(Qt.Key_G, Qt.Key_T);
        compare(AppController.noteBody(id), body, "g t on Copy wrote into the note");
        // Esc on the button: back on the drawn note.
        keyClick(Qt.Key_Escape); wait(100);
        verify(inside("notes-live") && focusName() !== "mdCodeCopyButton" && !tc.win._typing,
               "Esc on Copy: the drawn note, " + focusName());
        // Away and back: the drawn note, not the button it had last.
        for (let i = 0; i < 40 && focusName() !== "mdCodeCopyButton"; i++) keyClick(Qt.Key_Tab);
        compare(focusName(), "mdCodeCopyButton");
        AppController.currentView = "today";
        wait(300);
        tc.win.focusActiveView();
        wait(100);
        chord(Qt.Key_G, Qt.Key_N);
        tryCompare(AppController, "currentView", "notes");
        wait(300);
        // Dogfood 2026-10-20: arriving opens a new, empty note — not the
        // stop the old one had last. Esc leaves its text for the drawn note.
        verify(AppController.isFreshNote(AppController.activeNoteId), "Notes did not open on a new note");
        verify(inside("notes-live") && focusName() !== "mdCodeCopyButton", "arrived on the stop it had last: " + focusName());
        if (tc.win._typing) { keyClick(Qt.Key_Escape); wait(100); }
        verify(!tc.win._typing, "Esc left the caret in the text: " + focusName());
        chord(Qt.Key_G, Qt.Key_T);
        tryCompare(AppController, "currentView", "today");
        compare(AppController.noteBody(id), body, "nothing typed into the note");
    }

    function test_a_link_followed_then_g_t_writes_nothing() {
        const target = note("Follow target B5", "# Follow target B5

back to [[Follow source B5]]");
        const id = note("Follow source B5", "# Follow source B5

see [[Follow target B5]] now");
        openNote(id);
        let hit = false;
        for (let i = 0; i < 40 && !hit; i++) {
            keyClick(Qt.Key_Tab);
            hit = focusName() === "mdLinkStop";
        }
        verify(hit, "Tab never reached the [[link]]");
        keyClick(Qt.Key_Return);
        tryCompare(AppController, "activeNoteId", target);
        tryVerify(function () { return inside("notes-live") && focusName() !== "mdLinkStop" && !tc.win._typing; }, 2000,
                  "the keyboard stayed on a link: " + focusName());
        chord(Qt.Key_G, Qt.Key_T);
        tryCompare(AppController, "currentView", "today");
        compare(AppController.noteBody(target), "# Follow target B5

back to [[Follow source B5]]", "g t wrote into the note");
    }

    function test_a_linked_from_row_hands_the_keyboard_to_its_note() {
        const target = note("Incoming target B5", "# Incoming target B5

x");
        const id = note("Incoming source B5", "# Incoming source B5

see [[Incoming target B5]]");
        openNote(target);
        keyClick(Qt.Key_F6); wait(100);
        verify(inside("notes-links-pane"), "document → links, " + focusName());
        verify(tc.win.regionShown.length > 0, "F6 framed the links");
        for (let i = 0; i < 10 && !inside("incoming-" + id); i++) keyClick(Qt.Key_Tab);
        verify(inside("incoming-" + id), "the row is a stop, " + focusName());
        keyClick(Qt.Key_Return);
        tryCompare(AppController, "activeNoteId", id);
        tryVerify(function () { return inside("notes-live") && !tc.win._typing; }, 2000,
                  "the keyboard did not go on to the note: " + focusName());
        compare(tc.win.regionShown, "", "the F6 frame stayed on the links column");
    }

    function test_the_f6_frame_goes_when_the_keyboard_leaves_the_links() {
        const target = note("Frame target B5", "# Frame target B5

x");
        const id = note("Frame source B5", "# Frame source B5

see [[Frame target B5]]");
        verify(id.length > 0);
        openNote(target);
        keyClick(Qt.Key_F6); wait(100);
        verify(inside("notes-links-pane"));
        verify(tc.win.regionShown.length > 0, "F6 framed the links");
        keyClick(Qt.Key_Escape); wait(100);
        verify(!inside("notes-links-pane"), "Esc left the links, " + focusName());
        compare(tc.win.regionShown, "", "the frame stayed after Esc");
        keyClick(Qt.Key_F6); wait(100);
        verify(tc.win.regionShown.length > 0);
        tc.win.width = 1000;
        wait(200);
        compare(tc.win.regionShown, "", "the frame stayed after a resize hid the column");
    }

    function test_f6_walks_the_parts_of_knowledge() {
        const target = note("F6 target", "# F6 target\n\nx");
        const id = note("F6 probe", "# F6 probe\n\nsee [[F6 target]]");
        verify(id.length > 0);
        // The target: a note links here, so its links column has a row.
        openNote(target);
        verify(inside("notes-live"));
        keyClick(Qt.Key_F6); wait(100);
        verify(inside("notes-links-pane"), "document → links, " + focusName());
        keyClick(Qt.Key_F6); wait(100);
        verify(!inside("notes-document") && !inside("notes-list-pane"), "links → out of the view");
        let back = 0;
        while (!inside("notes-list-pane") && back < 6) { keyClick(Qt.Key_F6); wait(100); back++; }
        verify(inside("notes-list-pane"), "the list is a stop");
        keyClick(Qt.Key_F6); wait(100);
        verify(inside("notes-document") && !tc.win._typing, "list → the drawn note, not its text");
    }

    function test_a_wiki_link_is_a_tab_stop_and_return_follows_it() {
        const target = note("Link target B5", "# Link target B5\n\nthere");
        const id = note("Link source B5", "# Link source B5\n\nsee [[Link target B5]] now");
        openNote(id);
        let hit = false;
        for (let i = 0; i < 40 && !hit; i++) {
            keyClick(Qt.Key_Tab);
            const f = tc.win.activeFocusItem;
            hit = !!f && f.objectName === "mdLinkStop" && f.label === "Link target B5";
        }
        verify(hit, "Tab never reached the [[link]]");
        keyClick(Qt.Key_Return);
        tryCompare(AppController, "activeNoteId", target);
        tryVerify(function () { return inside("notes-live") && !tc.win._typing; }, 2000,
                  "the keyboard went on to the note opened: " + focusName());
    }

    function test_links_stay_under_the_note_when_narrow() {
        const target = note("Narrow target", "# Narrow target\n\nx");
        const id = note("Narrow probe", "# Narrow probe\n\nsee [[Narrow target]]");
        tc.win.width = 1000;
        openNote(target);
        tryVerify(function () { return !byName("notes-links-pane").visible; });
        tryVerify(function () { return byName("notes-links-inline") !== null; }, 2000, "the links vanished");
        verify(find(byName("notes-links-inline"), function (it) { return it.objectName === "incoming-" + id; }) !== null,
               "the note linking here is listed under the text");
    }

    function test_rename_labels_and_the_same_name_elsewhere() {
        const a = note("Same name B5", "# Same name B5\n\n", "meetings");
        const b = note("Other B5", "# Other B5\n\n", "ref");
        verify(a.length > 0);
        openNote(b);
        const pane = byName("notes-list-pane");
        pane.renameActive();
        const dlg = findChild(pane, "note-rename");
        tryVerify(function () { return dlg.opened; });
        verify(dlg.title.indexOf("…") < 0, "no ellipsis in the dialog title");
        const field = findChild(dlg.contentItem, "note-rename-title");
        const warn = findChild(dlg.contentItem, "note-rename-elsewhere");
        verify(!warn.visible);
        field.text = "Same name B5";
        tryVerify(function () { return warn.visible; }, 1000, "no word about the other folder");
        verify(warn.text.indexOf("meetings") >= 0, warn.text);
        dlg.close();
    }

    function test_esc_on_the_board_clears_a_typed_filter_with_results() {
        const d = AppController.newTaskDraft("todo");
        d.title = "B5 esc filter probe";
        verify(AppController.saveTask(d));
        AppController.currentView = "board";
        wait(300);
        tc.win.searchText = tc.win.defaultQuery("B5 esc filter", false);
        wait(300);
        verify(!tc.win._taskFilterEmpty, "the words find the card");
        tc.win.requestActivate();
        tc.win.focusActiveView();
        wait(100);
        // The cursor (if any) takes the first Esc.
        keyClick(Qt.Key_Escape); wait(100);
        keyClick(Qt.Key_Escape); wait(100);
        compare(tc.win.searchText, tc.win.defaultQuery("", false), "Esc left the typed words on the board");
        AppController.deleteTask(String(d.id));
    }
}
