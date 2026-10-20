// The notes and doc page editors, driven with real keys (audit 2026-09-30).
//
// KNOW-5: formatting keys wrap the selection; Tab and heading cycling act on
// every selected line. KNOW-14: the doc page editor has the same keyboard.
// KNOW-15: the page head follows a rename. KNOW-7: the list is reachable at
// any width and the header names the open note. KNOW-8: the links pane shows
// who links here, and a link to an existing note is not "broken".
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "KnowEditors"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Item { id: host; anchors.fill: parent }

    property var seeded: []
    property var pages: []

    // Ctrl+Z past a block's history is the app's undo: with the deletes of
    // an earlier cleanup still on it, it brought those notes back.
    function initTestCase() {
        // These cases drive the in-place block editor ("live"); notes open
        // split since 0.9.2, so ask for it.
        AppController.appSettingsJson = JSON.stringify(Object.assign({}, JSON.parse(AppController.appSettingsJson || "{}"),
                                                                     { notes: { view: "live" } }));
    }
    function init() { AppController.clearPendingUndo(); }
    function cleanup() {
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteNote(tc.seeded[i]);
        tc.seeded = [];
        for (let i = 0; i < tc.pages.length; i++) AppController.deleteDocPage(tc.pages[i]);
        tc.pages = [];
        AppController.clearPendingUndo();
    }

    function makeNotes(w) {
        const nv = createTemporaryQmlObject('import TodoCpp; NotesView { width: ' + (w || 900) + '; height: 600 }', host);
        tryVerify(function () { return nv._loadedOnce; });
        return nv;
    }
    function note(title, body) {
        const id = AppController.newNote(title);
        if (body !== undefined) AppController.setNoteBody(id, body);
        tc.seeded.push(id);
        return id;
    }

    function test_ctrl_b_wraps_the_selection_in_notes() {
        const id = note("sel probe", "one word here");
        AppController.activeNoteId = id;
        const nv = makeNotes();
        const ed = findChild(nv, "notesEditor");
        ed.forceActiveFocus();
        ed.select(4, 8);
        keyClick(Qt.Key_B, Qt.ControlModifier);
        compare(ed.text, "one **word** here");
        ed.select(0, 3);
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(ed.text, "[one]() **word** here");
    }

    function test_tab_indents_every_selected_line() {
        const id = note("tab probe", "- a\n- b\n- c");
        AppController.activeNoteId = id;
        const nv = makeNotes();
        const ed = findChild(nv, "notesEditor");
        ed.forceActiveFocus();
        ed.select(0, ed.length);
        keyClick(Qt.Key_Tab);
        compare(ed.text, "  - a\n  - b\n  - c");
    }

    function test_slash_date_leaves_urls_alone() {
        const id = note("slash probe", "");
        AppController.activeNoteId = id;
        const nv = makeNotes();
        const ed = findChild(nv, "notesEditor");
        ed.forceActiveFocus();
        ed.text = "see http://x.com/now";
        ed.cursorPosition = ed.length;
        keyClick(Qt.Key_Return);
        compare(ed.text, "see http://x.com/now\n", "the URL is kept and Enter still ends the line");
        // A slash word of its own still expands.
        ed.text = "due /today";
        ed.cursorPosition = ed.length;
        keyClick(Qt.Key_Tab);
        verify(/^due \d{4}-\d{2}-\d{2}$/.test(ed.text), ed.text);
    }

    // DG-071: the line above the note says what it is and how fresh; there
    // is no toolbar.
    function test_the_meta_line_says_note_and_when() {
        const id = note("Meta probe");
        AppController.activeNoteId = id;
        const nv = makeNotes();
        const meta = findChild(nv, "notes-meta");
        verify(meta !== null);
        const edited = I18n.t("know.edited").arg(I18n.t("know.ago.now"));
        tryCompare(meta, "text", Style.quiet ? edited : I18n.t("know.kind.note") + " · " + edited);
        compare(findChild(nv, "notes-mode-toggle"), null, "no Source button");
        compare(findChild(nv, "notes-attach"), null, "no attach button");
    }

    function test_ago_reads_minutes_hours_and_days() {
        const nv = makeNotes();
        const now = new Date(2026, 9, 9, 12, 0, 0);
        compare(nv.agoText(new Date(2026, 9, 9, 11, 59, 40), now), I18n.t("know.ago.now"));
        compare(nv.agoText(new Date(2026, 9, 9, 11, 55, 0), now), I18n.t("know.ago.min").arg(5));
        compare(nv.agoText(new Date(2026, 9, 9, 9, 0, 0), now), I18n.t("know.ago.hours").arg(3));
        compare(nv.agoText(new Date(2026, 9, 8, 9, 0, 0), now), I18n.t("know.ago.yesterday"));
        compare(nv.agoText(null, now), "");
    }

    // DG-070: a page of the former Docs catalogue opens in the document.
    function test_a_doc_page_opens_in_the_document() {
        const id = AppController.newDocPage("Page probe");
        tc.pages.push(id);
        const nv = makeNotes(1200);
        nv.openPage(id);
        const page = findChild(nv, "knowledge-page");
        verify(page.visible);
        compare(page.pageId, id);
        verify(!findChild(nv, "notes-live").visible, "the note gives way to the page");
        verify(!findChild(nv, "notes-links-pane").visible);
        const n = note("Back probe");
        AppController.activeNoteId = n;
        compare(nv.pageId, "", "opening a note closes the page");
    }

    function test_the_list_is_reachable_when_narrow() {
        const nv = makeNotes(700);
        const list = findChild(nv, "notes-list-pane");
        verify(list.visible, "700 px still has room for the list");
        nv.width = 480;
        verify(!list.visible, "folds away when there is no room");
        nv.toggleList();
        verify(list.visible, "and the toggle brings it back");
    }

    // DG-072: the column beside the note — backlinks, external links.
    function test_links_column_shows_backlinks_and_external_links() {
        const target = note("Link target probe", "# Link target probe\n\n[RFC 6585](https://www.rfc-editor.org/rfc/rfc6585 \"429 Too Many Requests\")");
        const other = note("Link source probe", "see [[Link target probe]]");
        AppController.activeNoteId = target;
        const nv = makeNotes(1200);
        verify(nv.showBacklinks, "shown when there is room");
        tryVerify(function () { return nv._incoming.length === 1; });
        compare(nv._incoming[0].noteId, other);
        tryVerify(function () { return nv._external.length === 1; });
        compare(nv._external[0].label, "RFC 6585");
        compare(nv._external[0].title, "429 Too Many Requests");
        verify(findChild(nv, "incoming-" + other) !== null);
    }

    function test_doc_page_editor_has_the_markdown_keyboard() {
        const id = AppController.newDocPage("Keys probe");
        tc.pages.push(id);
        AppController.setDocPageBody(id, "- first");
        const pane = createTemporaryQmlObject('import TodoCpp; MdEditorPane { width: 800; height: 400 }', host);
        pane.pageId = id;
        const area = findChild(pane, "docpage-text");
        area.forceActiveFocus();
        area.cursorPosition = area.length;
        keyClick(Qt.Key_Return);
        compare(area.text, "- first\n- ");
        area.text = "bold me";
        area.select(0, 4);
        keyClick(Qt.Key_B, Qt.ControlModifier);
        compare(area.text, "**bold** me");
    }

    // Ctrl+Shift+Z within the debounce: the page's last keystrokes go into it
    // before the redo deletes it again, so the next undo brings them back.
    function test_undo_flushes_the_doc_page_editor() {
        const id = AppController.newDocPage("Flush probe");
        tc.pages.push(id);
        const pane = createTemporaryQmlObject('import TodoCpp; MdEditorPane { width: 800; height: 400 }', host);
        pane.pageId = id;
        AppController.deleteDocPage(id);
        AppController.undo();
        compare(AppController.docPageBody(id), "# Flush probe\n\n");
        const area = findChild(pane, "docpage-text");
        area.forceActiveFocus();
        area.cursorPosition = area.length;
        keyClick(Qt.Key_Z);
        verify(pane._dirty, "the keystroke is still only in the editor");
        AppController.redo();
        wait(400);
        AppController.undo();
        compare(AppController.docPageBody(id), "# Flush probe\n\nz");
    }

    function test_doc_page_head_follows_a_rename() {
        const id = AppController.newDocPage("Before probe");
        tc.pages.push(id);
        const pane = createTemporaryQmlObject('import TodoCpp; MdEditorPane { width: 800; height: 400 }', host);
        pane.pageId = id;
        const head = findChild(pane, "docpage-title");
        compare(head.text, "Before probe");
        AppController.renameDocPage(id, "After probe");
        compare(head.text, "After probe");
    }

    // ── 0.8.4 idiot test + functional audit (Knowledge) ──
    function typeStr(t) {
        for (let i = 0; i < t.length; i++) {
            if (t[i] === " ") keyClick(Qt.Key_Space);
            else keyClick(t[i]);
        }
    }
    function liveNotes(id, w) {
        AppController.activeNoteId = id;
        const nv = makeNotes(w || 900);
        nv.viewMode = "live";
        return nv;
    }
    function focusedField() {
        const w = tc.Window.window;
        const f = w ? w.activeFocusItem : null;
        return f && f.objectName === "md-block-field" ? f : null;
    }

    // IDIOT-KNOW-1: words typed in a page block reach the page when another
    // page opens within the 400 ms the block waits.
    function test_page_text_survives_a_quick_switch() {
        const p1 = AppController.newDocPage("Quick one"); tc.pages.push(p1);
        const p2 = AppController.newDocPage("Quick two"); tc.pages.push(p2);
        const nv = makeNotes(1200);
        nv.openPage(p1);
        const live = findChild(findChild(nv, "knowledge-page"), "docpage-live");
        live.editLine(0, false);
        typeStr(" FAST");
        nv.openPage(p2);
        verify(AppController.docPageBody(p1).indexOf("Quick one FAST") >= 0, AppController.docPageBody(p1));
        // A profile switch announces itself the same way.
        live.editLine(0, false);
        typeStr(" PROF");
        AppController.aboutToChangeActiveNote();
        verify(AppController.docPageBody(p2).indexOf("Quick two PROF") >= 0, AppController.docPageBody(p2));
    }

    // IDIOT-KNOW-2: the keyboard goes with the page; nothing typed reaches the
    // hidden note.
    function test_typing_after_opening_a_page_goes_to_the_page() {
        const n = note("Ghost probe", "# Ghost probe\n\n");
        const p = AppController.newDocPage("Ghost page"); tc.pages.push(p);
        const nv = liveNotes(n, 1200);
        nv.openPage(p);
        tryVerify(function () { return focusedField() !== null; });
        typeStr("x");
        nv._flushPending();
        compare(AppController.noteBody(n), "# Ghost probe\n\n");
        verify(AppController.docPageBody(p).indexOf("x") >= 0, AppController.docPageBody(p));
        // Return routed to the view opens the page, not the hidden note.
        keyClick(Qt.Key_Escape);
        nv.openCursor();
        tryVerify(function () { return focusedField() !== null; });
        compare(findChild(nv, "notes-live").editing, false);
    }

    // IDIOT-KNOW-4: no notes keys behind a panel; F2 on a page renames the page.
    function test_notes_keys_stand_down_and_f2_renames_the_page() {
        const n = note("Keys probe", "# Keys probe\n\n");
        const p = AppController.newDocPage("Keys page"); tc.pages.push(p);
        const nv = liveNotes(n, 1200);
        nv.keysLive = false;
        keyClick(Qt.Key_F2);
        verify(!findChild(nv, "note-rename").opened, "F2 behind a panel");
        nv.keysLive = true;
        nv.openPage(p);
        keyClick(Qt.Key_F2);
        const pr = findChild(nv, "knowledge-page-rename");
        tryVerify(function () { return pr.opened; });
        verify(!findChild(nv, "note-rename").opened);
        pr.close();
    }

    // A4-4: arriving puts the keyboard on the drawn note (the keymap's
    // chords are keys there); IDIOT-KNOW-6: a letter typed on it lands in
    // the text, at the end, not in the title.
    function test_take_focus_and_typing_land_in_the_note() {
        const n = note("Caret probe", "# Caret probe\n\nfirst para");
        const nv = liveNotes(n);
        nv.takeFocus();
        verify(focusedField() === null, "no block opened on arrival");
        verify(findChild(nv, "notes-live").documentFocused, "the drawn note holds the keyboard");
        typeStr("Z");
        verify(focusedField() !== null);
        nv._flushPending();
        compare(AppController.noteBody(n), "# Caret probe\n\nfirst paraZ");
    }

    // Dogfood 2026-10-20 (was IDIOT-KNOW-9 / PERSONA-20): a new note opens
    // empty with the caret in it, and its first line names it; "/" there is
    // the insert menu, and Esc takes the "/" back (IDIOT-KNOW-16).
    function test_new_note_opens_empty_and_its_first_line_names_it() {
        const nv = liveNotes(note("Before new", "# Before new\n\n"));
        nv.newNoteAndEdit();
        const id = AppController.activeNoteId;
        tc.seeded.push(id);
        tryVerify(function () { return focusedField() !== null; });
        compare(AppController.noteBody(id), "");
        typeStr("Standup");
        nv._flushPending();
        compare(AppController.noteBody(id), "Standup");
        tryCompare(nv, "_activeTitle", "Standup");
        // The next new note is empty again, the caret in it.
        nv.newNoteAndEdit();
        tc.seeded.push(AppController.activeNoteId);
        verify(AppController.activeNoteId !== id);
        compare(AppController.notesState, "");
        tryVerify(function () { return focusedField() !== null; });
    }

    // IDIOT-KNOW-10: Ctrl+Z in a block past its own history is the app's: a
    // deleted note comes back.
    function test_ctrl_z_in_a_block_restores_a_deleted_note() {
        const keep = note("Keeper", "# Keeper\n\nbody");
        const gone = note("Goner", "# Goner\n\n");
        const nv = liveNotes(gone);
        AppController.deleteNote(gone);
        // In a block: arriving lands on the drawn note now (A4-4).
        findChild(nv, "notes-live").focusEnd();
        verify(focusedField() !== null);
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        verify(AppController.notes.indexOfId(gone) >= 0, "the note is back");
        verify(keep.length > 0);
    }

    // KNOW-1: Ctrl+Z past the block never saves it blank.
    function test_undo_never_blanks_the_block() {
        const n = note("Undo probe", "# Undo probe\n\npara");
        const nv = liveNotes(n);
        const live = findChild(nv, "notes-live");
        live.editLine(2, false);
        typeStr(" abc");
        wait(600);
        keyClick(Qt.Key_Escape);
        live.editLine(2, false);
        for (let i = 0; i < 4; i++) keyClick(Qt.Key_Z, Qt.ControlModifier);
        nv._flushPending();
        compare(AppController.noteBody(n), "# Undo probe\n\npara");
    }

    // KNOW-2: Ctrl+Z after a tick undoes the tick, not something unseen.
    function test_ctrl_z_on_the_drawn_note_undoes_a_tick() {
        const n = note("Tick probe", "- [ ] alpha");
        const nv = liveNotes(n);
        const live = findChild(nv, "notes-live");
        verify(live.document.toggleTask(live.source.textDocument, 0));
        live.forceActiveFocus();
        nv._flushPending();
        compare(AppController.noteBody(n), "- [x] alpha");
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        nv._flushPending();
        compare(AppController.noteBody(n), "- [ ] alpha");
    }

    // KNOW-3: a heading link opens that block; nothing goes to the hidden
    // source field.
    function test_heading_jump_opens_the_block() {
        const n = note("Jump probe", "# Jump probe\n\nsee\n\n## Bottom\n\nend");
        const nv = liveNotes(n);
        nv._followLink("note", "Jump probe#Bottom");
        const f = focusedField();
        verify(f !== null, "the block of the heading is open");
        compare(f.text, "## Bottom");
        verify(!findChild(nv, "notesEditor").activeFocus);
    }

    // KNOW-5: a one-letter edit keeps CRLF and no-break spaces elsewhere.
    function test_a_page_edit_keeps_line_endings() {
        const p = AppController.newDocPage("Raw page"); tc.pages.push(p);
        AppController.setDocPageBody(p, "# Raw page\r\n\r\nkeep this\r\n\r\nline two");
        const nv = makeNotes(1200);
        nv.openPage(p);
        const live = findChild(findChild(nv, "knowledge-page"), "docpage-live");
        live.editLine(4, false);
        typeStr("Q");
        nv._flushPending();
        compare(AppController.docPageBody(p), "# Raw page\r\n\r\nkeep this\r\n\r\nline twoQ");
    }

    // KNOW-6: the table is a block of its own, the caret in its first cell.
    function test_slash_table_is_its_own_block() {
        const n = note("Table probe", "# Table probe\n\npara");
        const nv = liveNotes(n);
        const live = findChild(nv, "notes-live");
        live.editLine(2, false);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Slash);
        const menu = findChild(live, "md-slash-menu");
        tryVerify(function () { return menu.opened; });
        findChild(live, "md-slash-table").triggered();
        typeStr("cell");
        nv._flushPending();
        compare(AppController.noteBody(n), "# Table probe\n\npara\n\n| cell |  |\n|---|---|\n|  |  |");
    }

    // KNOW-8 / PERSONA-21: [[ in a block offers notes; Enter takes the hit.
    // Enter on a typed task title links the task's id.
    function test_wiki_autocomplete_in_a_block() {
        note("Autocomplete target", "# Autocomplete target\n\n");
        const n = note("Autocomplete probe", "# Autocomplete probe\n\npara");
        const nv = liveNotes(n);
        findChild(nv, "notes-live").editLine(2, false);
        typeStr(" [[Autocomplete ta");
        tryVerify(function () { return nv.acMatches.length > 0; });
        keyClick(Qt.Key_Return);
        nv._flushPending();
        compare(AppController.noteBody(n), "# Autocomplete probe\n\npara [[Autocomplete target]] ");
    }

    // IDIOT-KNOW-3/13: the rename dialog says why and stays open.
    function test_rename_dialog_refuses_taken_and_empty_names() {
        note("Taken name", "# Taken name\n\n");
        const n = note("Renamed one", "# Renamed one\n\n");
        const nv = liveNotes(n);
        findChild(nv, "notes-list-pane").renameActive();
        const dlg = findChild(nv, "note-rename");
        tryVerify(function () { return dlg.opened; });
        const field = findChild(nv, "note-rename-title");
        field.text = "taken NAME";
        dlg.commit();
        verify(dlg.opened, "a taken name keeps the dialog open");
        verify(findChild(nv, "note-rename-error").visible);
        field.text = "  ";
        dlg.commit();
        verify(dlg.opened, "an empty name keeps it open");
        field.text = "Fresh name";
        dlg.commit();
        verify(!dlg.opened);
    }

    // IDIOT-KNOW-7/8: Esc clears the filter then leaves it; Return opens the
    // first hit; Return on the list opens the note to write in.
    function test_filter_and_list_keys() {
        const a = note("Filter alpha", "# Filter alpha\n\n");
        note("Filter beta", "# Filter beta\n\n");
        const nv = liveNotes(a);
        nv.focusSearch();
        typeStr("Filter beta");
        keyClick(Qt.Key_Return);
        tryVerify(function () { return focusedField() !== null; });
        compare(nv._activeTitle, "Filter beta");
        nv.focusSearch();
        keyClick(Qt.Key_Escape);
        compare(findChild(nv, "note-filter").text, "");
        keyClick(Qt.Key_Escape);
        // Back to the note, drawn: Esc never puts the caret in the text (A4-6).
        tryVerify(function () { return findChild(nv, "notes-live").documentFocused; });
        verify(focusedField() === null);
        findChild(nv, "notes-list-pane").focusList();
        keyClick(Qt.Key_Return);
        tryVerify(function () { return focusedField() !== null; });
    }

    // PERSONA-25: the "+" names its key in the tooltip.
    function test_plus_tooltip_names_ctrl_alt_n() {
        const nv = makeNotes();
        const plus = findChild(nv, "notes-new");
        verify(plus._tipText.indexOf(AppController.shortcutText("notes.new")) > 0, plus._tipText);
    }
}
