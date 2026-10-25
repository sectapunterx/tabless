// Two keymaps (dogfood 2026-10-19): Classic (arrows, Enter, Del, F2,
// Ctrl / Alt chords) is a new install's, Vim the 0.8 one. Through the real
// Main.qml with real key events:
// - Classic walks the board with the arrows and leaves j to type-to-search;
//   a scheme switch with a prefix waiting lets go of it.
// - Notes and docs: Del (Classic) or d d (Vim) deletes the note the
//   Knowledge list is on, and Undo brings it back; d alone there never marks
//   a task done behind the screen.
// - A document's block: Ctrl+Shift+K while typing (Classic), d d on the drawn
//   document (Vim), undone by Ctrl+Z.
// - Settings search finds a hotkey row by its name and by its key, and the
//   Keys filter by the English name.
// - Private list keys follow the scheme: j / k walk the notes list only in Vim.
import QtQuick
import QtTest
import TodoCpp
import "../../qml/KeyRules.js" as KeyRules
import "../../qml/SettingsIndex.js" as Idx

TestCase {
    id: tc
    name: "KeymapSchemes"
    when: windowShown

    property var win: null
    property var notesMade: []
    property var tasksMade: []

    function initTestCase() {
        KeyTest.armCrashTrace();   // TEMP
        AppController.appSettingsJson = JSON.stringify(Object.assign({}, JSON.parse(AppController.appSettingsJson || "{}"),
                                                                     { notes: { view: "live" } }));
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        verify(comp.status === Component.Ready, comp.errorString());
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 939;
        wait(1200);
        AppController.markWelcomeSeen();
        tc.win.requestActivate();
    }
    function cleanupTestCase() {
        AppController.keymapScheme = "vim";
        AppController.currentView = "board";
        if (tc.win) tc.win.destroy();
    }
    function init() {
        AppController.keymapScheme = "vim";
        AppController.resetAllShortcuts();
        AppController.clearPendingUndo();
        router().cancel();
    }
    function cleanup() {
        for (let i = 0; i < tc.notesMade.length; i++)
            if (AppController.notes.indexOfId(tc.notesMade[i]) >= 0) AppController.deleteNote(tc.notesMade[i]);
        for (let j = 0; j < tc.tasksMade.length; j++) AppController.deleteTask(tc.tasksMade[j]);
        tc.notesMade = [];
        tc.tasksMade = [];
        AppController.clearSelection();
        AppController.clearPendingUndo();
        AppController.keymapScheme = "vim";
        AppController.resetAllShortcuts();
        router().cancel();
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
    function byName(name, root) { return find(root || tc.win.contentItem, function (it) { return it.objectName === name; }); }
    function router() {
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) if (cd[i] && cd[i].objectName === "key-router") return cd[i];
        return null;
    }
    function focusName() { const f = tc.win.activeFocusItem; return f ? String(f.objectName) : ""; }
    function inside(name) {
        const host = byName(name);
        for (let p = tc.win.activeFocusItem; p; p = p.parent) if (p === host) return true;
        return false;
    }
    function note(title, body) {
        const id = AppController.newNote(title);
        AppController.setNoteBody(id, body);
        tc.notesMade.push(id);
        return id;
    }
    function task(id, title) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.id = id;
        d.title = title;
        AppController.saveTask(d);
        tc.tasksMade.push(id);
    }
    function onList(id) {
        AppController.currentView = "notes";
        AppController.activeNoteId = id;
        tryVerify(function () { return byName("notes-list-pane") !== null && byName("notes-live") && byName("notes-live").visible; });
        wait(300);
        byName("notes-list-pane").focusList();
        wait(100);
        verify(inside("note-list"), "the list holds the keyboard, " + focusName());
    }

    // ── Pure rules ────────────────────────────────────────────────────
    function test_a_key_typed_into_a_search_reads_as_keys() {
        verify(KeyRules.keyQueryMatches("ctrl+k", ["Ctrl+K"]), "ctrl+k");
        verify(KeyRules.keyQueryMatches("Ctrl K", ["Ctrl+K"]), "Ctrl K");
        verify(KeyRules.keyQueryMatches("shift ctrl k", ["Ctrl+Shift+K"]), "shift ctrl k");
        verify(KeyRules.keyQueryMatches("g b", ["G, B"]), "g b");
        verify(KeyRules.keyQueryMatches("gb", ["G, B"]), "gb");
        verify(KeyRules.keyQueryMatches("п и", ["G, B"]), "п и");
        verify(KeyRules.keyQueryMatches("ctrl+л", ["Ctrl+K"]), "ctrl+л");
        verify(KeyRules.keyQueryMatches("F2", ["F2"]), "F2");
        verify(KeyRules.keyQueryMatches("del", ["Del"]), "del");
        verify(KeyRules.keyQueryMatches("delete", ["Del"]), "delete");
        verify(KeyRules.keyQueryMatches("pgdn", ["PgDown"]), "pgdn");
        verify(KeyRules.keyQueryMatches("enter", ["Return"]), "enter");
        verify(KeyRules.keyQueryMatches("ctrl+,", ["Ctrl+,"]), "ctrl+,");
        verify(KeyRules.keyQueryMatches("ctrl++", ["Ctrl++"]), "ctrl++");
        verify(KeyRules.keyQueryMatches("?", ["?"]), "?");
        verify(!KeyRules.keyQueryMatches("ctrl", ["Ctrl+K"]), "ctrl");
        verify(!KeyRules.keyQueryMatches("branch", ["Ctrl+Shift+G"]), "branch");
        verify(!KeyRules.keyQueryMatches("k", ["Ctrl+K"]), "k");
        verify(!KeyRules.keyQueryMatches("g", ["G, B"]), "g");
    }

    // ── Settings search ────────────────────────────────────────────────
    function test_the_settings_index_finds_a_hotkey_both_ways() {
        const items = Idx.build(I18n, I18n.lang, [], AppController.shortcuts);
        const ids = (q) => Idx.search(items, q).map(it => it.id);
        verify(ids("ctrl+k").indexOf("shortcuts:key:palette.open") >= 0, JSON.stringify(ids("ctrl+k")));
        verify(ids("g b").indexOf("shortcuts:key:view.board") >= 0, "a Vim sequence");
        // The name in either language.
        const label = AppController.shortcutLabel("palette.open");
        verify(ids(label).indexOf("shortcuts:key:palette.open") >= 0, label);
        AppController.language = "ru";
        const ru = Idx.build(I18n, I18n.lang, [], AppController.shortcuts);
        const enName = ru.filter(it => it.id === "shortcuts:key:palette.open")[0].titleEn;
        verify(enName.length > 0);
        verify(Idx.search(ru, enName).some(it => it.id === "shortcuts:key:palette.open"), "the English name in a Russian UI");
        AppController.language = "en";
        // The key in the scheme that is on: Del finds the note's delete in Classic.
        AppController.keymapScheme = "classic";
        const cl = Idx.build(I18n, I18n.lang, [], AppController.shortcuts);
        const del = Idx.search(cl, "del").map(it => it.id);
        verify(del.indexOf("shortcuts:key:notes.delete") >= 0, JSON.stringify(del));
        verify(Idx.search(cl, "f2").some(it => it.id === "shortcuts:key:task.rename"));
        verify(!Idx.search(cl, "g b").some(it => it.id === "shortcuts:key:view.board"), "no Vim key in Classic");
    }

    function test_a_settings_hit_opens_the_keys_section_on_its_row() {
        AppController.currentView = "settings";
        tryVerify(function () { return tc.win.activeViewItem() && tc.win.activeViewItem().revealItem; }, 3000);
        const sv = tc.win.activeViewItem();
        sv.searchText = "ctrl+k";
        wait(50);
        const hit = sv.searchMatches.filter(it => it.id === "shortcuts:key:palette.open")[0];
        verify(hit !== undefined, JSON.stringify(sv.searchMatches.map(it => it.id)));
        verify(sv.revealItem(hit));
        tryVerify(function () { return inside("settings-key-palette.open"); }, 4000, "focus on the row, " + focusName());
        const row = byName("settings-key-palette.open");
        compare(row.searchMark, 1, "the row is marked");
        sv.searchText = "";
        AppController.currentView = "board";
    }

    function test_the_keys_filter_matches_the_english_name_and_the_key() {
        AppController.language = "ru";
        AppController.currentView = "settings";
        tryVerify(function () { return byName("settings-keys-search") !== null; }, 3000);
        const field = byName("settings-keys-search");
        field.text = "Command line";
        wait(50);
        verify(byName("settings-key-palette.open") !== null, "found by the English name");
        field.text = "ctrl k";
        wait(50);
        verify(byName("settings-key-palette.open") !== null, "found by the key");
        verify(byName("settings-key-theme.toggle") === null, "and nothing else");
        field.text = "";
        AppController.language = "en";
        AppController.currentView = "board";
    }

    // ── The board in each scheme ───────────────────────────────────────
    function test_classic_walks_with_arrows_and_j_is_not_a_key() {
        task("KMS-0", "keymap probe 0");
        task("KMS-1", "keymap probe 1");
        AppController.keymapScheme = "classic";
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() && tc.win.activeViewItem().moveCursor; }, 3000);
        tc.win.focusActiveView();
        const b = tc.win.activeViewItem();
        b.cursorTaskId = "KMS-0";
        b.cursorVisible = true;
        wait(30);
        compare(AppController.shortcutFor("board.cursorDown"), "Down");
        keyClick(Qt.Key_D);   // d is no key in Classic: nothing is marked done
        wait(700);
        verify(AppController.statusCategory(AppController.taskById("KMS-0").status) !== "done", "a bare d did nothing");
        // It went to type-to-search, as any letter that is no key does.
        tc.win.searchText = "";
        tc.win.focusActiveView();
        b.cursorTaskId = "KMS-0";
        b.cursorVisible = true;
        wait(50);
        keyClick(Qt.Key_D, Qt.ControlModifier);
        tryVerify(function () { return AppController.statusCategory(AppController.taskById("KMS-0").status) === "done"; }, 1500,
                  "Ctrl+D is Done in Classic");
        tc.win.searchText = "";
    }

    function test_switching_the_scheme_lets_go_of_a_waiting_prefix() {
        AppController.currentView = "today";
        wait(50);
        tc.win.focusActiveView();
        keyClick(Qt.Key_G);
        compare(router().pending, "G");
        AppController.keymapScheme = "classic";
        compare(router().pending, "", "the waiting g went with the Vim keymap");
        keyClick(Qt.Key_B);
        compare(AppController.currentView, "today", "b after the switch is not g b");
    }

    // ── Deleting notes from the keyboard ───────────────────────────────
    function test_classic_del_deletes_the_note_and_undo_brings_it_back() {
        AppController.keymapScheme = "classic";
        note("Keep classic", "# Keep classic\n\nkeep");
        const gone = note("Gone classic", "# Gone classic\n\ngone");
        onList(gone);
        keyClick(Qt.Key_Delete);
        tryVerify(function () { return AppController.notes.indexOfId(gone) < 0; }, 2000, "Del deleted the note");
        wait(200);
        verify(inside("note-list"), "the list keeps the keyboard, " + focusName());
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () { return AppController.notes.indexOfId(gone) >= 0; }, 2000, "Ctrl+Z restored it");
    }

    function test_vim_dd_deletes_the_note_and_d_alone_does_nothing() {
        task("KMS-V", "vim note probe task");
        AppController.setSelectedTaskIds(["KMS-V"]);   // a selection made elsewhere
        const gone = note("Gone vim", "# Gone vim\n\ngone");
        note("Keep vim", "# Keep vim\n\nkeep");
        onList(gone);
        keyClick(Qt.Key_D);
        compare(router().pending, "D", "d waits for its second d on the list");
        wait(1300);
        verify(AppController.notes.indexOfId(gone) >= 0, "d alone deleted nothing");
        verify(AppController.statusCategory(AppController.taskById("KMS-V").status) !== "done",
               "nor marked the selected task done behind Knowledge");
        keyClick(Qt.Key_D);
        keyClick(Qt.Key_D);
        tryVerify(function () { return AppController.notes.indexOfId(gone) < 0; }, 2000, "d d deleted the note");
        keyClick(Qt.Key_U);
        tryVerify(function () { return AppController.notes.indexOfId(gone) >= 0; }, 2000, "u restored it");
        // Del works in Vim too, and never takes the selected task.
        onList(gone);
        keyClick(Qt.Key_Delete);
        tryVerify(function () { return AppController.notes.indexOfId(gone) < 0; }, 2000);
        verify(AppController.taskById("KMS-V").id === "KMS-V", "the selected task is still there");
    }

    function test_j_and_k_walk_the_notes_list_only_in_vim() {
        const a = note("Walk a", "# Walk a

a");
        note("Walk b", "# Walk b

b");
        const pane = () => byName("notes-list-pane");
        onList(a);
        const ids = pane().visibleIds();
        const at = ids.indexOf(a);
        verify(at >= 0 && ids.length > 1);
        // Toward the neighbour that exists.
        const down = at < ids.length - 1;
        const next = ids[down ? at + 1 : at - 1];
        AppController.keymapScheme = "classic";
        onList(a);
        keyClick(down ? Qt.Key_J : Qt.Key_K);
        wait(100);
        compare(AppController.activeNoteId, a, "j / k are no keys in Classic");
        keyClick(down ? Qt.Key_Down : Qt.Key_Up);
        tryCompare(AppController, "activeNoteId", next, 1000, "the arrow walks in Classic");
        AppController.keymapScheme = "vim";
        onList(a);
        keyClick(down ? Qt.Key_J : Qt.Key_K);
        tryCompare(AppController, "activeNoteId", next, 1000, "j / k walk in Vim");
    }

    // ── Deleting a block ───────────────────────────────────────────────
    function test_classic_ctrl_shift_k_deletes_the_block_being_typed() {
        AppController.keymapScheme = "classic";
        const id = note("Blocks classic", "# Blocks classic\n\nfirst para\n\nsecond para");
        AppController.currentView = "notes";
        AppController.activeNoteId = id;
        tryVerify(function () { return byName("notes-live") && byName("notes-live").visible; });
        wait(300);
        const ed = byName("notes-live");
        ed.editLine(2, false);
        wait(100);
        verify(tc.win._typing, "the block is open");
        keyClick(Qt.Key_K, Qt.ControlModifier | Qt.ShiftModifier);
        wait(300);
        AppController.flushSave();
        tryVerify(function () { return AppController.noteBody(id).indexOf("first para") < 0; }, 2000, AppController.noteBody(id));
        verify(AppController.noteBody(id).indexOf("second para") >= 0, AppController.noteBody(id));
        verify(tc.win._typing, "the keyboard stays in the text");
    }

    function test_vim_dd_on_the_drawn_document_deletes_the_last_block() {
        const id = note("Blocks vim", "# Blocks vim\n\nfirst para\n\nsecond para");
        AppController.currentView = "notes";
        AppController.activeNoteId = id;
        tryVerify(function () { return byName("notes-live") && byName("notes-live").visible; });
        wait(300);
        const ed = byName("notes-live");
        ed.editLine(4, false);     // "second para" was the last block edited
        wait(100);
        keyClick(Qt.Key_Escape);   // out of the text, onto the drawn note
        wait(100);
        verify(!tc.win._typing, "on the drawn note, " + focusName());
        keyClick(Qt.Key_D);
        keyClick(Qt.Key_D);
        wait(300);
        tryVerify(function () { return ed.text.indexOf("second para") < 0 || AppController.noteBody(id).indexOf("second para") < 0; },
                  2000, AppController.noteBody(id));
        verify(AppController.notes.indexOfId(id) >= 0, "the block went, not the note");
    }
}
