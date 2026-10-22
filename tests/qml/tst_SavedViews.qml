// Saved views, end to end in the real Main.qml: saving from the filter bar
// through the name dialog (Enter saves, Esc cancels), applying from the
// sidebar, Alt+N and the palette, "modified" with Update view / Save as new,
// the sidebar's keyboard (arrows, Enter, Ctrl+arrows, F2, Delete) and its
// count and problem badges.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SavedViews"
    when: windowShown

    property var win: null
    property var host: null
    property var seeded: []

    function initTestCase() {
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        verify(comp.status === Component.Ready, comp.errorString());
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 939;
        AppController.resetAllShortcuts();
        wait(1200);   // splash
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) {
            const o = cd[i];
            if (o && o.opened !== undefined && o.close !== undefined && o.opened) o.close();
        }
        tc.host = find(tc.win.contentItem, function (it) { return it.objectName === "saved-views-host"; });
        verify(tc.host !== null, "no SavedViewsHost in Main");
        tc.win.requestActivate();
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() !== null; }, 3000);
    }

    function cleanupTestCase() {
        clearViews();
        if (tc.win) tc.win.destroy();
        tc.win = null;
    }

    function init() {
        clearViews();
        resetFilters();
        tc.seeded = [];
        for (let i = 0; i < 3; i++) {
            const d = AppController.newTaskDraft("todo");
            d._isNew = true;
            d.id = "SVQ-" + i;
            d.title = "svprobe card " + i;
            d.priority = i === 0 ? "P0" : "P2";
            AppController.saveTask(d);
            tc.seeded.push(d.id);
        }
        AppController.clearPendingUndo();
        tc.win.focusActiveView();
    }

    function cleanup() {
        const d = tc.host.nameDialog;
        if (d.opened) d.close();
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteTask(tc.seeded[i]);
        tc.seeded = [];
        clearViews();
        resetFilters();
        AppController.clearPendingUndo();
        AppController.currentView = "board";
    }

    function clearViews() {
        // The property is re-read on every access, so delete from the front.
        while (AppController.savedViews.length > 0) AppController.deleteSavedView(AppController.savedViews[0].id);
        if (tc.host) tc.host.leave();
    }
    function resetFilters() {
        tc.win.searchText = "";
        tc.win.prioritiesFilter = ({});
        tc.win.boardSortMode = "manual";
        tc.win.showArchived = false;
        tc.win.showDoneTimeline = false;
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
    function byName(name) {
        return find(tc.win.contentItem, function (it) { return it.objectName === name && it.visible !== false; });
    }
    // A control that just appeared is placed on the next polish; click it
    // after that, not at wherever the layout had it before.
    function clickLaidOut(name) {
        const it = byName(name);
        verify(it !== null, name + " is not shown");
        wait(50);
        mouseClick(it);
    }
    function railRow(i) {
        const list = byName("sidebar-views-list");
        verify(list !== null, "no saved view list in the rail");
        // A row added a moment ago is laid out on the next polish.
        tryVerify(function () {
            const r = list.itemAtIndex(i);
            return r !== null && list.height >= r.y + r.height;
        }, 1000, "saved view row " + i + " is not laid out");
        return list.itemAtIndex(i);
    }
    function mkView(name, query, view) {
        return AppController.saveView(name, { query: query, priorities: [], sort: "manual",
                                              archived: false, showDone: false, view: view || "board" });
    }
    function typeText(s) {
        for (let i = 0; i < s.length; i++) keyClick(s[i]);
    }
    function ids() {
        return AppController.savedViews.map(function (v) { return v.id; });
    }

    // ── Save from the filter bar: the dialog, Enter and Esc ──

    function test_save_from_the_filter_bar_with_enter() {
        tc.win.searchText = "svprobe priority:P0";
        clickLaidOut("query-save-view");
        const dlg = tc.host.nameDialog;
        tryCompare(dlg, "opened", true);
        const field = find(dlg.contentItem, function (it) { return it.objectName === "saved-view-name-field"; });
        tryVerify(function () { return field.activeFocus; }, 1000, "the name field takes the keyboard");
        compare(field.text, "Svprobe · P0", "the query, read as words, is the suggested name");
        field.selectAll();
        typeText("Hot");
        keyClick(Qt.Key_Return);
        tryCompare(dlg, "opened", false);
        compare(AppController.savedViews.length, 1);
        const v = AppController.savedViews[0];
        compare(v.name, "Hot");
        compare(v.query, "svprobe priority:P0");
        compare(tc.host.activeId, v.id, "a view just saved is the active one");
        verify(!tc.host.modified);
        tryVerify(function () { const c = byName("saved-view-chip"); return c !== null && c.text === "Hot"; });
        compare(byName("save-view"), null, "Save view gives way to the active view's chip");
        tryVerify(function () { return AppController.savedViewCounts[v.id] === 1; }, 1000);
    }

    function test_escape_cancels_the_dialog() {
        tc.host.openSave();
        const dlg = tc.host.nameDialog;
        tryCompare(dlg, "opened", true);
        typeText("nope");
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "opened", false);
        compare(AppController.savedViews.length, 0, "Esc saves nothing");
    }

    function test_an_empty_name_does_not_save() {
        tc.host.openSave();
        const dlg = tc.host.nameDialog;
        tryCompare(dlg, "opened", true);
        const field = find(dlg.contentItem, function (it) { return it.objectName === "saved-view-name-field"; });
        field.text = "   ";
        keyClick(Qt.Key_Return);
        verify(dlg.opened, "a blank name keeps the dialog open");
        compare(AppController.savedViews.length, 0);
        keyClick(Qt.Key_Escape);
    }

    // ── Apply, modify, update, save as new ──

    function test_apply_restores_filters_and_view() {
        const id = AppController.saveView("Arch P0", { query: "svprobe", priorities: ["P0"], sort: "priority",
                                                      archived: true, showDone: true, view: "timeline" });
        compare(tc.host.activeId, "", "saving through the API does not activate");
        mouseClick(railRow(0));
        compare(AppController.currentView, "list", "the timeline of 0.8.0 opens as the list (DG-162)");
        compare(tc.win.searchText, "svprobe");
        compare(tc.win.prioritiesFilter["P0"], true);
        compare(tc.win.boardSortMode, "priority");
        compare(tc.win.showArchived, true);
        compare(tc.win.showDoneTimeline, true);
        compare(tc.host.activeId, id);
        verify(railRow(0).active, "the applied view is marked in the sidebar");
        verify(!tc.host.modified);
    }

    function test_changing_filters_marks_modified_then_update() {
        const id = mkView("Probe", "svprobe");
        tc.host.apply(id);
        verify(!tc.host.modified);
        tc.win.prioritiesFilter = ({ P0: true });
        verify(tc.host.modified, "a chip turned on moves off the view");
        verify(railRow(0).modified);
        verify(byName("save-as-new-view") !== null, "Save as new appears");
        clickLaidOut("update-view");
        verify(!tc.host.modified);
        compare(AppController.savedView(id).priorities, ["P0"]);
        compare(AppController.savedViews.length, 1, "update does not add a view");
        // Walking over to Notes is not a change of filters.
        AppController.currentView = "notes";
        verify(!tc.host.modified);
        AppController.currentView = "board";
    }

    function test_save_as_new_keeps_the_original() {
        const id = mkView("Probe", "svprobe");
        tc.host.apply(id);
        tc.win.searchText = "svprobe card 1";
        verify(tc.host.modified);
        clickLaidOut("save-as-new-view");
        const dlg = tc.host.nameDialog;
        tryCompare(dlg, "opened", true);
        keyClick(Qt.Key_Return);   // accept the suggestion
        tryCompare(dlg, "opened", false);
        compare(AppController.savedViews.length, 2);
        compare(AppController.savedView(id).query, "svprobe", "the original is untouched");
        verify(tc.host.activeId !== id, "the new view is the active one");
        verify(!tc.host.modified);
    }

    // The chip's "×" takes the view off with its filters: leaving them on
    // with no view to name them was a filter nobody could see to clear.
    function test_leaving_a_view_clears_the_filters() {
        const id = mkView("Probe", "svprobe");
        tc.host.apply(id);
        clickLaidOut("saved-view-chip");
        compare(tc.host.activeId, "");
        compare(tc.win.searchText, tc.win.defaultQuery("", false));  // the reset keeps is:open (IDIOT-TASKS-5)
    }

    // A second click on the active view in the sidebar turns it off.
    function test_second_click_on_a_view_turns_it_off() {
        const id = mkView("Probe", "svprobe");
        tc.host.toggle(id);
        compare(tc.host.activeId, id);
        compare(tc.win.searchText, "svprobe");
        tc.host.toggle(id);
        compare(tc.host.activeId, "");
        compare(tc.win.searchText, tc.win.defaultQuery("", false));  // the reset keeps is:open (IDIOT-TASKS-5)
        // A different view while one is on switches to it.
        const other = mkView("Other", "svprobe other");
        tc.host.toggle(id);
        tc.host.toggle(other);
        compare(tc.host.activeId, other);
        compare(tc.win.searchText, "svprobe other");
        tc.host.toggle(other);
    }

    // ── Keys: Alt+N and the palette ──

    function test_alt_digits_apply_the_nth_view() {
        mkView("One", "svprobe card 0");
        const two = mkView("Two", "svprobe card 1", "week");
        keyClick(Qt.Key_7, Qt.ControlModifier);   // My view 2 is Ctrl+7 (Ctrl+4 / Ctrl+5 are Calendar and Docs)
        compare(tc.host.activeId, two);
        compare(AppController.currentView, "week");
        compare(tc.win.searchText, "svprobe card 1");
        // A number with no view behind it does nothing.
        keyClick(Qt.Key_9, Qt.ControlModifier);
        compare(tc.host.activeId, two);
        // The same key again turns it off.
        keyClick(Qt.Key_7, Qt.ControlModifier);
        compare(tc.host.activeId, "");
        compare(tc.win.searchText, tc.win.defaultQuery("", false));  // the reset keeps is:open (IDIOT-TASKS-5)
        AppController.currentView = "board";
    }

    function test_palette_offers_each_view_and_save() {
        const id = mkView("Palette probe", "svprobe");
        let found = null;
        let save = null;
        const cd = tc.win.contentData;
        let pal = null;
        for (let i = 0; i < cd.length; i++) if (cd[i] && cd[i]._commands !== undefined) pal = cd[i];
        verify(pal !== null);
        const cmds = pal._commands();
        for (let j = 0; j < cmds.length; j++) {
            if (cmds[j].commandId === "savedview:" + id) found = cmds[j];
            if (cmds[j].commandId === "savedview.save") save = cmds[j];
            verify(cmds[j].commandId.indexOf("savedView.") !== 0, "the numbered catalog entries stay out of the palette");
        }
        verify(found !== null);
        compare(found.label, I18n.t("palette.cmd.savedView").arg("Palette probe"));
        compare(found.sub, AppController.shortcutText("savedView.1"));
        verify(save !== null);
        tc.win.runCommand("savedview:" + id);
        compare(tc.host.activeId, id);
    }

    // ── The sidebar list: arrows, Enter, Ctrl+arrows, F2, Delete ──

    function test_sidebar_keyboard() {
        const a = mkView("Alpha", "svprobe card 0");
        const b = mkView("Beta", "svprobe card 1");
        const c = mkView("Gamma", "svprobe card 2");
        railRow(0).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down);
        verify(railRow(1).activeFocus, "Down moves to the next view");
        keyClick(Qt.Key_Return);
        compare(tc.host.activeId, b, "Enter applies it");
        compare(tc.win.searchText, "svprobe card 1");

        railRow(1).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Up, Qt.ControlModifier);
        compare(ids(), [b, a, c], "Ctrl+Up moves the view up");
        tryVerify(function () { return railRow(0) && railRow(0).activeFocus; }, 1000, "focus follows the moved view");
        AppController.undo();
        compare(ids(), [a, b, c], "the move is one undo step");

        railRow(2).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_F2);
        const dlg = tc.host.nameDialog;
        tryCompare(dlg, "opened", true);
        compare(dlg.mode, "rename");
        compare(dlg.text, "Gamma");
        const field = find(dlg.contentItem, function (it) { return it.objectName === "saved-view-name-field"; });
        tryVerify(function () { return field.activeFocus; });
        field.selectAll();
        typeText("Delta");
        keyClick(Qt.Key_Return);
        tryCompare(dlg, "opened", false);
        compare(AppController.savedView(c).name, "Delta");

        railRow(0).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Delete);
        compare(ids(), [b, c]);
        tryVerify(function () { return railRow(0) && railRow(0).activeFocus; }, 1000, "focus stays in the list");
        AppController.undo();
        compare(ids(), [a, b, c], "undo brings the deleted view back in its place");
        tc.win.focusActiveView();
    }

    // X-Menus-Other (DG-150): no Duplicate row; a copy is "Сохранить как
    // вид" on the open view. The menu still deletes the row it was opened on.
    function test_context_menu_duplicate_and_delete() {
        // The copy's name is localized; the shared test profile may be left in ru.
        const lang = AppController.language;
        AppController.language = "en";
        const a = mkView("Alpha", "svprobe");
        mouseClick(railRow(0), 20, 10, Qt.RightButton);
        const menu = railMenu();
        verify(menu !== null);
        tryCompare(menu, "opened", true);
        verify(findMenuItem(menu, "sidebar-view-duplicate") === null);
        AppController.duplicateSavedView(a);
        tryVerify(function () { return AppController.savedViews.length === 2; });
        compare(AppController.savedViews[1].name, "Alpha copy");
        compare(AppController.savedViews[1].query, "svprobe");
        if (menu.opened) menu.close();
        tryCompare(menu, "opened", false);

        // The Menu key opens it for the focused row; Delete there removes that row.
        railRow(1).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Menu);
        tryCompare(menu, "opened", true);
        compare(menu.targetIndex, 1);
        findMenuItem(menu, "sidebar-view-delete").triggered();
        tryVerify(function () { return AppController.savedViews.length === 1; });
        compare(AppController.savedViews[0].id, a, "the copy went, the original stayed");
        if (menu.opened) menu.close();
        AppController.language = lang;
    }
    function railMenu() {
        let rail = byName("sidebar-views-head");
        while (rail && rail._savedViews === undefined) rail = rail.parent;
        const d = rail.data;
        for (let i = 0; i < d.length; i++) if (d[i] && d[i].objectName === "sidebar-views-menu") return d[i];
        return null;
    }
    function findMenuItem(menu, name) {
        for (let i = 0; i < menu.count; i++) {
            const it = menu.itemAt(i);
            if (it && it.objectName === name) return it;
        }
        return null;
    }

    // ── Badges ──

    function test_badge_counts_and_problem() {
        const ok = mkView("Cards", "svprobe");
        const bad = mkView("Gone", "status:nosuchcolumn svprobe");
        tryVerify(function () { return railRow(0) && railRow(0).countText === "3"; }, 1000, "three probe cards");
        compare(railRow(1).countText, "?", "a query heap cannot read shows the problem, not 0");
        verify(railRow(1).tooltipText.indexOf("status:nosuchcolumn") >= 0);
        // A new matching task moves the badge.
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.id = "SVQ-9";
        d.title = "svprobe late";
        AppController.saveTask(d);
        tc.seeded.push(d.id);
        tryVerify(function () { return railRow(0).countText === "4"; }, 1000);
        // Applying the broken view shows the same problem on the search box.
        tc.host.apply(bad);
        compare(AppController.searchProblems(tc.win.searchText), ["status:nosuchcolumn"]);
        compare(AppController.savedViewCounts[ok], 4);
    }

    // EYE-7: a saved view is a bookmark with its Alt+N digit in it, not a
    // bare digit that its corner count turned into "1⁶" in the collapsed rail.
    function test_rows_are_bookmarks_with_their_digit() {
        mkView("Marked", "svprobe");
        const row = railRow(0);
        // Folded, the row is the bookmark with its Alt+N digit.
        const t = findChild(row, "sidebar-view-digit");
        verify(t !== null && t.text === "1");
    }

    // heap 2 (APP-258): nothing saved is one grey line, not a button.
    // EYES-3: a sentence with a subject, and not on the first run next to
    // the hero.
    function test_empty_list_says_where_views_come_from() {
        const line = find(tc.win.contentItem, function (it) { return it.objectName === "sidebar-views-empty"; });
        verify(line !== null);
        compare(line.visible, AppController.welcomeSeen, "shown only once the first run is over");
        // The "My views" head goes with it: no bare heading on the first run.
        const head = find(tc.win.contentItem, function (it) { return it.objectName === "sidebar-views-head"; });
        verify(head !== null);
        compare(head.visible, AppController.welcomeSeen, "a bare head over nothing on the first run");
        // Past the first run (as tst_TodayView does): the line is there.
        AppController.markWelcomeSeen();
        tryVerify(function () { return line.visible; }, 1000, "no empty line");
        verify(head.visible, "the head is back past the first run");
        compare(line.text, I18n.t("sidebar.myViews.empty"));
        compare(I18n.dict.en["sidebar.myViews.empty"], "Saved views appear here");
    }

    function test_profile_switch_leaves_the_view() {
        const id = mkView("Here", "svprobe");
        tc.host.apply(id);
        const home = AppController.activeProfileId;
        const other = AppController.createProfile("SV probe other");
        compare(tc.host.activeId, "", "a view does not follow into another workspace");
        AppController.activeProfileId = home;
        AppController.deleteProfile(other);
        AppController.clearPendingUndo();
        AppController.activeProfileId = home;
    }

    // IDIOT-TASKS-3: a held Del removes one view; focus stepping to the next
    // row does not hand the repeat on to it.
    function test_a_held_delete_removes_one_view() {
        const a = mkView("Alpha", "svprobe card 0");
        const b = mkView("Beta", "svprobe card 1");
        const c = mkView("Gamma", "svprobe card 2");
        tc.win.requestActivate();
        railRow(0).forceActiveFocus(Qt.TabFocusReason);
        tryVerify(function () { return railRow(0).activeFocus; }, 1000, "the row did not take the keyboard");
        KeyTest.press(tc.win, Qt.Key_Delete, 0, "", false);
        for (let i = 0; i < 4; i++) {
            wait(20);
            KeyTest.press(tc.win, Qt.Key_Delete, 0, "", true);
        }
        wait(50);
        compare(ids(), [b, c], "the repeat deleted more views");
        tc.win.focusActiveView();
    }

    // PERSONA-16: Esc on the board and Ctrl+2 leave a saved view for the
    // plain list with its default query.
    function test_keyboard_ways_out_of_a_view() {
        const id = mkView("Probe", "svprobe");
        tc.host.apply(id);
        compare(tc.host.activeId, id);
        tc.win.focusActiveView();
        const b = tc.win.activeViewItem();
        if (b && b.clearCursor) b.clearCursor();
        keyClick(Qt.Key_Escape);
        compare(tc.host.activeId, "", "Esc kept the view");
        compare(tc.win.searchText, "is:open");
        tc.host.apply(id);
        tc.win.focusActiveView();
        keyClick(Qt.Key_2, Qt.ControlModifier);
        compare(tc.host.activeId, "", "Ctrl+2 kept the view");
    }

    // PERSONA-17: a clause said twice is saved once.
    function test_a_saved_query_keeps_each_clause_once() {
        tc.win.searchText = "is:open priority:p1 priority:p1";
        compare(tc.host.currentState().query, "is:open priority:p1");
    }

    // IDIOT-TASKS-11 (owner: option a): a view whose column was deleted shows
    // nothing — on the board, in the counts and in the sidebar badge alike —
    // while the same clause typed by hand is still dropped as a typo.
    function test_a_view_on_a_deleted_column_shows_nothing() {
        AppController.addStatus("SV Gone", "");
        const gone = AppController.statuses.find(st => st.name === "SV Gone").id;
        const d = AppController.newTaskDraft(gone);
        d._isNew = true;
        d.id = "SVQ-G";
        d.title = "svprobe gone card";
        AppController.saveTask(d);
        tc.seeded.push(d.id);
        const id = mkView("Gone only", "svprobe status:" + gone);
        tryVerify(function () { return AppController.savedViewCounts[id] === 1; }, 1000);
        AppController.deleteStatus(gone, "todo");
        tc.host.apply(id);
        AppController.currentView = "board";
        wait(100);
        compare(AppController.strictQuery, "svprobe status:" + gone);
        tryVerify(function () { return AppController.savedViewCounts[id] === 0; }, 1000, "the badge counts every task");
        compare(AppController.filteredCounts(tc.win.searchText, [], false).total, 0, "the counter shows every task");
        const b = tc.win.activeViewItem();
        let shown = 0;
        const cols = b._visibleByColumn();
        for (let i = 0; i < cols.length; i++) shown += cols[i].ids.length;
        compare(shown, 0, "the board shows every svprobe task");
        // Typed by hand, the unknown column is a typo and is dropped.
        tc.win.searchText = "svprobe card status:" + gone;
        wait(50);
        verify(AppController.filteredCounts(tc.win.searchText, [], false).total > 0, "a typed typo emptied the board");
        tc.host.leave();
        compare(AppController.strictQuery, "");
    }
}
