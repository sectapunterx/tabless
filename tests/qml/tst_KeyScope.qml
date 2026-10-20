// Who gets a key: the innermost thing that holds the keyboard.
//
// Board and calendar keys are application shortcuts, so Qt offered them before
// the focused item. Return in the header search opened the card under the board
// cursor, Down in a card's context menu moved the cursor behind it, Enter in
// "New column" opened the task editor, and the first Esc with a selection
// cleared the selection while the editor stayed open. Global shortcuts fired
// behind modals (Ctrl+3 under the task editor, Ctrl+K over the welcome tour),
// and a new task's editor opened with no focus, so what was typed went nowhere.
//
// Drives the real Main.qml with real key events.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "KeyScope"
    when: windowShown

    property var win: null
    property var seeded: []
    readonly property string probe: "scopeprobe"

    function initTestCase() {
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        verify(comp.status === Component.Ready, comp.errorString());
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 939;
        AppController.resetAllShortcuts();
        tryVerify(function () { return !tc.win.findChild || true; });
        wait(1200);   // splash
        tc.win.requestActivate();
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() !== null; }, 3000);
        wait(100);
    }

    function cleanupTestCase() {
        if (tc.win) tc.win.destroy();
        tc.win = null;
    }

    function init() {
        seed();
        closeAll();
    }

    function cleanup() {
        closeAll();
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteTask(tc.seeded[i]);
        tc.seeded = [];
        AppController.clearPendingUndo();
        tc.win.searchText = "";
    }

    function seed() {
        tc.seeded = [];
        for (let i = 0; i < 3; i++) {
            const d = AppController.newTaskDraft("todo");
            d._isNew = true;
            d.id = "SCOPE-" + i;
            d.title = tc.probe + " card " + i;
            AppController.saveTask(d);
            tc.seeded.push(d.id);
        }
    }

    function typeName(o) {
        const s = String(o);
        const i = s.indexOf("(");
        return i > 0 ? s.slice(0, i) : s;
    }
    function popups() {
        const out = [];
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) {
            const o = cd[i];
            if (o && o.opened !== undefined && o.open !== undefined) out.push(o);
        }
        return out;
    }
    function popup(prefix) {
        const ps = popups();
        for (let i = 0; i < ps.length; i++)
            if (typeName(ps[i]).indexOf(prefix) === 0 || ps[i].objectName === prefix) return ps[i];
        return null;
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
    function byName(root, name) { return find(root, function (it) { return it.objectName === name; }); }
    function dataByName(root, name) {
        const d = root.data || [];
        for (let i = 0; i < d.length; i++) if (d[i] && d[i].objectName === name) return d[i];
        return null;
    }
    function closeAll() {
        const ps = popups();
        for (let i = 0; i < ps.length; i++) if (ps[i].opened) ps[i].close();
        const b = tc.win.activeViewItem();
        wait(150);
        AppController.clearSelection();
        if (b && b.clearCursor) b.clearCursor();
        tc.win.focusActiveView();
        wait(20);
    }
    function board() { return tc.win.activeViewItem(); }

    // UX-1 / TASKS-10: Ctrl+N and type — the title gets every character.
    // APP-286: in the app Ctrl+N opens the ticket editor, not quick capture.
    function test_new_task_editor_focuses_title() {
        const qc = popup("QuickCapturePopup");
        const te = popup("TaskEditor");
        keyClick(Qt.Key_N, Qt.ControlModifier);
        tryCompare(te, "opened", true);
        compare(qc.opened, false, "Ctrl+N opened quick capture");
        const title = byName(te.contentItem, "te-title");
        tryVerify(function () { return title.activeFocus; }, 1000, "the title does not have the keyboard");
        keyClick(Qt.Key_A); keyClick(Qt.Key_B); keyClick(Qt.Key_C);
        compare(title.text, "abc");
        te.close();
        tryCompare(te, "opened", false);
        tc.win.focusActiveView();
        AppController.clearPendingUndo();
    }

    // IDIOT-SHELL-1: a hurried second Enter after the capture saved its task
    // opened the card under the board cursor with the caret in its title,
    // and what was typed next went into that task.
    function test_second_enter_after_capture_stays_put() {
        const qc = popup("QuickCapturePopup");
        const doc = byName(tc.win.contentItem, "task-doc");
        const b = board();
        b.searchText = tc.probe;
        b.moveCursor(0, 1);
        const cursor = b.cursorTaskId;
        verify(cursor !== "");
        const title = AppController.taskById(cursor).title;
        tc.win.focusActiveView();
        const before = AppController.tasks.rowCount();
        // Quick capture's own key (APP-286: Ctrl+N is the editor now).
        keyClick(Qt.Key_Space, Qt.ControlModifier | Qt.ShiftModifier);
        tryCompare(qc, "opened", true);
        keyClick(Qt.Key_X); keyClick(Qt.Key_Y);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Return);
        tryCompare(qc, "opened", false);
        wait(100);
        compare(doc.opened, false, "the second Enter opened the card under the cursor");
        compare(AppController.taskById(cursor).title, title);
        tryVerify(function () { return AppController.tasks.rowCount() === before + 1; }, 1000, "the capture made no task");
        // …and Return opens the card again once the moment has passed.
        wait(450);
        keyClick(Qt.Key_Return);
        tryCompare(doc, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(doc, "opened", false);
        const m = AppController.tasks, rid = m.roleOf("id"), rti = m.roleOf("title");
        for (let i = m.rowCount() - 1; i >= 0; i--)
            if (m.data(m.index(i, 0), rti) === "xy") AppController.deleteTask(m.data(m.index(i, 0), rid));
        AppController.clearPendingUndo();
    }

    // IDIOT-SHELL-7: Ctrl+[ is Vim's Esc — in a field it leaves the field and
    // does not switch the profile under the caret.
    function test_ctrl_bracket_in_a_field_leaves_it() {
        const home = AppController.activeProfileId;
        const other = AppController.createProfile("scope-bracket-probe");
        AppController.activeProfileId = home;
        wait(50);
        const search = byName(tc.win.contentItem, "topbar-search");
        verify(search !== null);
        search.forceActiveFocus();
        keyClick(Qt.Key_A); keyClick(Qt.Key_B);
        verify(search.activeFocus);
        keyClick(Qt.Key_BracketLeft, Qt.ControlModifier);
        compare(AppController.activeProfileId, home, "Ctrl+[ switched the profile while typing");
        verify(!search.activeFocus, "Ctrl+[ did not leave the field");
        keyClick(Qt.Key_BracketRight, Qt.ControlModifier);  // out of the field: the profile key works
        compare(AppController.activeProfileId === home, false, "Ctrl+] out of a field did nothing");
        AppController.activeProfileId = home;
        AppController.deleteProfile(other);
        AppController.clearPendingUndo();
        tc.win.searchText = "";
    }

    // UX-9: Tab leaves the description instead of typing a tab, and a list
    // line indents instead.
    function test_tab_leaves_description() {
        const te = popup("TaskEditor");
        te.showFor(AppController.newTaskDraft("todo"));
        tryCompare(te, "opened", true);
        const desc = byName(te.contentItem, "te-desc");
        desc.forceActiveFocus();
        keyClick(Qt.Key_Tab);
        compare(desc.text, "");
        verify(!desc.activeFocus, "Tab must move focus out of the description");
        desc.forceActiveFocus();
        desc.text = "- item";
        desc.cursorPosition = 6;
        keyClick(Qt.Key_Tab);
        compare(desc.text, "  - item");
        verify(desc.activeFocus);
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
        compare(desc.text, "- item");
        keyClick(Qt.Key_Escape);
        // The prompt answers Ctrl+D, not a bare d, and not at once (IDIOT-DOC-19).
        wait(450);
        keyClick(Qt.Key_D, Qt.ControlModifier);
        tryCompare(te, "opened", false);
    }

    // UX-2: the editor closes first, the selection is let go of second.
    function test_escape_closes_editor_before_selection() {
        AppController.setSelectedTaskIds([tc.seeded[0], tc.seeded[1]]);
        compare(AppController.selectionCount, 2);
        const te = popup("TaskEditor");
        te.showFor(Object.assign({}, AppController.taskById(tc.seeded[0])));
        tryCompare(te, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(te, "opened", false);
        compare(AppController.selectionCount, 2, "the first Esc belongs to the editor");
        tc.win.focusActiveView();
        keyClick(Qt.Key_Escape);
        compare(AppController.selectionCount, 0, "the second Esc lets go of the selection");
    }

    // UX-3 / TASKS-5 / UX-21: the header search keeps Return, Esc and arrows.
    function test_search_field_owns_its_keys() {
        const b = board();
        b.searchText = tc.probe;
        b.moveCursor(0, 1);
        const cursor = b.cursorTaskId;
        verify(cursor !== "");
        const te = popup("TaskEditor");
        const search = byName(tc.win.contentItem, "topbar-search");
        search.forceActiveFocus();
        keyClick(Qt.Key_S);
        keyClick(Qt.Key_Down);
        compare(b.cursorTaskId, cursor, "Down in the search moved the board cursor");
        keyClick(Qt.Key_Return);
        wait(100);
        compare(te.opened, false, "Return in the search opened the card under the cursor");
        verify(!search.activeFocus, "Return hands the keyboard back to the view");
        search.forceActiveFocus();
        keyClick(Qt.Key_Escape);
        compare(search.text, "", "Esc clears the search");
        verify(search.activeFocus);
        keyClick(Qt.Key_Escape);
        verify(!search.activeFocus, "a second Esc leaves the search");
    }

    // APP-166 / APP-272: `?` opens the cheat sheet from the view, and types a
    // question mark in a text field.
    function test_question_mark_opens_cheat_sheet() {
        const hk = popup("KeyCheatSheet");
        verify(hk !== null);
        // The task search is in the Tasks header (APP-258).
        AppController.currentView = "board";
        const search = byName(tc.win.contentItem, "topbar-search");
        search.forceActiveFocus();
        keyClick(Qt.Key_Question, Qt.ShiftModifier);
        wait(50);
        compare(hk.opened, false, "? in the search opened the cheat-sheet");
        search.text = "";
        tc.win.focusActiveView();
        wait(20);
        keyClick(Qt.Key_Question, Qt.ShiftModifier);
        tryCompare(hk, "opened", true);
        hk.close();
        tryCompare(hk, "opened", false);
    }

    // APP-268: D on the card under the cursor is Done, D again puts it back;
    // D in a text field types a d.
    function test_d_is_done_on_the_cursor_card() {
        const b = board();
        b.searchText = tc.probe;
        b.moveCursor(0, 1);
        const id = b.cursorTaskId;
        verify(id !== "");
        const was = AppController.taskById(id).status;
        tc.win.focusActiveView();
        keyClick(Qt.Key_D);
        tryVerify(function () { return AppController.taskById(id).status === AppController.doneColumn(); }, 1000, "D did not make it done");
        // Done is folded on the heap 2 board (APP-262), so the cursor cannot
        // follow the card there: the second D acts on it selected.
        AppController.setSelectedTaskIds([id]);
        // Within half a second a second d is Vim's "dd", not "put it back".
        wait(600);
        keyClick(Qt.Key_D);
        tryVerify(function () { return AppController.taskById(id).status === was; }, 1000, "a second D did not put it back");
        AppController.clearSelection();
        AppController.clearPendingUndo();
    }

    // UX-3: a card's context menu keeps Down and Esc.
    function test_card_menu_owns_its_keys() {
        const b = board();
        b.searchText = tc.probe;
        b.moveCursor(0, 1);
        const cursor = b.cursorTaskId;
        const card = find(b, function (it) { return it.objectName === "tc-card" && it.visible && it.taskId === cursor; })
                  || find(b, function (it) { return it.objectName === "tc-card" && it.visible; });
        verify(card !== null);
        const menu = card.contextMenu();  // built on first use
        verify(menu !== null);
        menu.popup(card, 10, 10);
        tryCompare(menu, "opened", true);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(b.cursorTaskId, cursor, "Down in the menu moved the board cursor");
        verify(menu.currentIndex >= 0, "Down must move through the menu");
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "opened", false);
        verify(b.cursorVisible, "Esc closed the menu and nothing else");
    }

    // PERA-1: M → Down → Down → Enter on "Set priority ›" opened the list while
    // the card menu's close was handing focus back to the board. The list sat
    // open without the keyboard; G's date popup closing gave it focus back,
    // unseen, and the next Enter set P0.
    function test_priority_list_from_the_keyboard_never_holds_focus_unseen() {
        const b = board();
        b.searchText = tc.probe;
        b.moveCursor(0, 1);
        const id = b.cursorTaskId;
        verify(id !== "");
        AppController.setTaskPriority(id, "P3");
        const card = b._cardItem(id);
        verify(card !== null);
        tc.win.focusActiveView();

        keyClick(Qt.Key_M);
        tryVerify(function () { return card._menu && card._menu.opened; });
        // Down to Priority, wherever the menu has it (APP-268 reordered it).
        for (let i = 0; i < 12 && card._menu.itemAt(card._menu.currentIndex).objectName !== "tc-menu-priority"; i++)
            keyClick(Qt.Key_Down);
        compare(card._menu.itemAt(card._menu.currentIndex).objectName, "tc-menu-priority");
        keyClick(Qt.Key_Return);
        // The list is up, has the keyboard, and starts on the current value.
        tryVerify(function () { return card._priorityMenu && card._priorityMenu.opened; }, 2000);
        tryVerify(function () { return card._priorityMenu.activeFocus; }, 2000, "the list opened without the keyboard");
        compare(tc.win.activeFocusItem.text, "P3");

        // G is a view key: it waits while a menu holds the keyboard (SHELL-3).
        keyClick(Qt.Key_G);
        const go = popup("go-to-date");
        wait(50);
        compare(go.opened, false, "G opened go-to-date over a menu");
        verify(card._priorityMenu.opened);
        // Something else opening over the list still takes it down with it.
        go.openAt(AppController.selectedDate, tc.win.contentItem);
        tryCompare(go, "opened", true);
        tryCompare(card._priorityMenu, "visible", false, 2000, "a menu the keyboard left stays open");
        keyClick(Qt.Key_Escape);
        tryCompare(go, "opened", false);
        // Past the Return guard a closing popup leaves (IDIOT-SHELL-1).
        wait(450);
        verify(typeName(tc.win.activeFocusItem).indexOf("AppMenuItem") !== 0,
               "a closed menu's row holds the keyboard: " + typeName(tc.win.activeFocusItem));

        // A Return right after a popup closed is still that popup's
        // (IDIOT-TASKS-6): the card opens once the guard is over.
        wait(400);
        keyClick(Qt.Key_Return);
        wait(100);
        compare(AppController.taskById(id).priority, "P3", "Enter changed the priority unseen");
        // Return on the card opens it — as a document now (APP-265).
        const doc = byName(tc.win.contentItem, "task-doc");
        tryCompare(doc, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(doc, "opened", false);
    }

    // TASKS-5: Enter in the WIP dialog saves the limit.
    function test_wip_dialog_enter_saves() {
        const b = board();
        const wip = dataByName(b, "wip-popup");
        verify(wip !== null);
        b.moveCursor(0, 1);
        const st = AppController.statuses[0];
        wip.openFor(st.id, st.name, 0, null);
        tryCompare(wip, "opened", true);
        keyClick(Qt.Key_7);
        keyClick(Qt.Key_Return);
        tryCompare(wip, "opened", false);
        compare(popup("TaskEditor").opened, false);
        compare(AppController.statuses[0].wip, 7);
        AppController.setStatusWipLimit(st.id, 0);
    }

    // TASKS-5 / UX-22: a view's own modal dialog blocks the board keys and
    // the global shortcuts behind it — even one that never took focus.
    function test_modal_dialog_blocks_keys_behind_it() {
        const b = board();
        b.searchText = tc.probe;
        b.moveCursor(0, 1);
        tc.win.focusActiveView();
        const dlg = dataByName(b, "confirm-delete-column");
        verify(dlg !== null);
        dlg.focus = false;         // the worst case: a dialog that leaves focus behind
        dlg.open();
        tryCompare(dlg, "opened", true);
        tryVerify(function () { return tc.win._dimmerShown; }, 1000, "no dimmer seen");
        keyClick(Qt.Key_3, Qt.ControlModifier);
        compare(AppController.currentView, "board", "Ctrl+3 switched the view behind the dialog");
        // Return is the dialog's own main button (X-Dlg-Small): it answers
        // the dialog (no column named here, so nothing is deleted), never
        // the card behind it.
        const before = AppController.statuses.length;
        keyClick(Qt.Key_Return);
        wait(50);
        compare(popup("TaskEditor").opened, false, "Return opened a card behind the dialog");
        compare(AppController.statuses.length, before);
        dlg.close();
        tryCompare(dlg, "opened", false);
        dlg.focus = true;
        tryVerify(function () { return !tc.win._dimmerShown; }, 2000);
    }

    // UX-20: the palette runs commands through Main.
    function test_palette_runs_a_command() {
        const pal = popup("CommandPalette");
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryCompare(pal, "opened", true);
        for (const ch of "go to week") keyClick(ch === " " ? Qt.Key_Space : ch.toUpperCase().charCodeAt(0));
        tryVerify(function () { return pal._matches.length > 0 && pal._matches[0].commandId === "view.week"; }, 1000,
                  "top hit is " + JSON.stringify(pal._matches[0]));
        keyClick(Qt.Key_Return);
        tryCompare(AppController, "currentView", "week");
        tryCompare(pal, "opened", false);
        AppController.currentView = "board";
        wait(50);
    }

    // UX-22: global shortcuts stand down behind a modal.
    function test_global_shortcuts_wait_behind_modals() {
        const te = popup("TaskEditor");
        te.showFor(AppController.newTaskDraft("todo"));
        tryCompare(te, "opened", true);
        keyClick(Qt.Key_3, Qt.ControlModifier);
        compare(AppController.currentView, "board", "Ctrl+3 switched the view under the editor");
        keyClick(Qt.Key_Escape);
        tryCompare(te, "opened", false);

        const welcome = popup("KeyCheatSheet");
        welcome.open();
        tryCompare(welcome, "opened", true);
        keyClick(Qt.Key_K, Qt.ControlModifier);
        compare(popup("CommandPalette").opened, false, "Ctrl+K opened the palette over the cheat sheet");
        welcome.close();
        tryCompare(welcome, "opened", false);

        // …and work again once it is gone.
        tc.win.focusActiveView();
        keyClick(Qt.Key_K, Qt.ControlModifier);
        tryCompare(popup("CommandPalette"), "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(popup("CommandPalette"), "opened", false);
    }

    // Item 19: e in the open task's document is its estimate, in both
    // keymaps (Vim e, Classic Alt+E), never the board's archive behind it;
    // Esc takes the popover away and leaves the document open.
    function test_e_in_the_document_is_the_estimate() {
        const scheme = AppController.keymapScheme;
        const doc = byName(tc.win.contentItem, "task-doc");
        const id = tc.seeded[0];
        const runs = [["vim", Qt.Key_E, Qt.NoModifier], ["classic", Qt.Key_E, Qt.AltModifier]];
        for (let i = 0; i < runs.length; i++) {
            AppController.keymapScheme = runs[i][0];
            tc.win.openTask(id);
            tryCompare(doc, "opened", true);
            doc.focusDocument();
            wait(50);
            keyClick(runs[i][1], runs[i][2]);
            tryVerify(function () { return doc.estimatePopup.opened; }, 1000, runs[i][0] + ": no estimate popover");
            verify(!AppController.taskById(id).archived, runs[i][0] + ": e archived the open task");
            keyClick(Qt.Key_Escape);
            wait(100);
            verify(doc.opened, runs[i][0] + ": Esc in the popover closed the document");
            keyClick(Qt.Key_Escape);
            tryCompare(doc, "opened", false);
        }
        AppController.keymapScheme = scheme;
    }

    // Acceptance 2026-10-20 (idiot test), in the real window: with the caret
    // in the task's text, the estimate popover takes the keyboard on every
    // way in. From "+ свойство" the typed "1.5h" and Enter went into the
    // text behind it and the popover stayed open.
    function estimateRun(how) {
        const doc = byName(tc.win.contentItem, "task-doc");
        const id = tc.seeded[1];
        const t = AppController.taskById(id);
        t._originalId = id;
        t.desc = "text stays";
        t.estimateMinutes = how === "menu" ? 0 : 30;
        AppController.saveTask(t);
        tc.win.openTask(id);
        tryCompare(doc, "opened", true);
        const body = findChild(doc, "task-doc-body");
        body.focusEditor();
        tryVerify(function () { return tc.within(tc.win.activeFocusItem, body); }, 1000, how + ": the caret is not in the text");
        const before = body.source.text;
        waitForRendering(tc.win.contentItem);
        const chip = findChild(doc, "task-doc-estimate");
        if (how === "menu") {
            mouseClick(findChild(doc, "task-doc-add"));
            const menu = findChild(doc, "task-doc-add-menu");
            tryVerify(function () { return menu.opened; }, 1000, "+ property did not open");
            let row = null;
            for (let i = 0; i < menu.count; i++)
                if (menu.itemAt(i) && menu.itemAt(i).text === I18n.t("taskdoc.add.estimate")) row = menu.itemAt(i);
            verify(row !== null, "no Estimate row");
            waitForRendering(tc.win.contentItem);
            mouseClick(row);
        } else if (how === "click") {
            mouseClick(chip);
        } else if (how === "enter") {
            chip.focusChip();
            keyClick(Qt.Key_Return);
        } else {
            doc.focusDocument();
            wait(50);
            keyClick(Qt.Key_E);
        }
        const p = doc.estimatePopup;
        tryVerify(function () { return p.opened; }, 1000, how + ": no popover");
        const input = findChild(p.contentItem, "estimate-input");
        // Settled: whatever hands focus about after a popup closes has run.
        wait(100);
        verify(input.activeFocus, how + ": the field has no keyboard, " + tc.win.activeFocusItem);
        compare(input.selectedText, input.text, how + ": the text is not selected");
        keyClick(Qt.Key_1); keyClick(Qt.Key_Period); keyClick(Qt.Key_5); keyClick(Qt.Key_H);
        compare(input.text, "1.5h", how);
        keyClick(Qt.Key_Return);
        tryVerify(function () { return !p.opened; }, 1000, how + ": Enter did not close it");
        compare(AppController.taskById(id).estimateMinutes, 90, how);
        body.flush();
        compare(body.source.text, before, how + ": keys went into the text");
        compare(AppController.taskById(id).desc, "text stays", how);
        tryVerify(function () { return tc.within(tc.win.activeFocusItem, chip); }, 1000, how + ": the keyboard is not back on the chip");
        // Esc from a reopened popover gives it back to the chip too.
        keyClick(Qt.Key_Return);
        tryVerify(function () { return p.opened && input.activeFocus; }, 1000, how + ": Enter on the chip");
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return !p.opened; }, 1000);
        wait(100);
        verify(tc.within(tc.win.activeFocusItem, chip), how + ": Esc did not give the keyboard to the chip, " + tc.win.activeFocusItem);
        keyClick(Qt.Key_Escape);
        tryCompare(doc, "opened", false);
    }
    function within(item, anc) {
        for (let p = item; p; p = p.parent) if (p === anc) return true;
        return false;
    }
    function test_estimate_from_the_property_menu_has_the_keyboard() { estimateRun("menu"); }
    function test_estimate_from_a_click_on_the_chip_has_the_keyboard() { estimateRun("click"); }
    function test_estimate_from_enter_on_the_chip_has_the_keyboard() { estimateRun("enter"); }
    function test_estimate_from_e_has_the_keyboard() { estimateRun("e"); }
}
