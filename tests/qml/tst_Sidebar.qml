// The heap 2 sidebar (APP-258): the three places and Settings, "New task",
// My views with their counts, folding to icons, and the profile switcher.
// The sidebar is tested on its own here; navigation by keys runs against
// the real Main in tst_ShellLayout.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "Sidebar"
    when: windowShown
    visible: true
    width: 400
    height: 720

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""
    property string savedView: ""
    property var made: []
    function init() {
        tc.savedSettings = AppController.appSettingsJson;
        tc.savedView = AppController.currentView;
        tc.made = [];
    }
    function cleanup() {
        for (const id of tc.made) AppController.deleteSavedView(id);
        AppController.clearPendingUndo();
        AppController.appSettingsJson = tc.savedSettings;
        AppController.currentView = tc.savedView;
    }

    function make(qml) {
        const o = createTemporaryQmlObject(qml, host);
        verify(o !== null);
        return o;
    }
    function bar(expanded, h) {
        return make('import TodoCpp; Sidebar { height: ' + (h || 700) + '; expanded: ' + (expanded ? "true" : "false") + ' }');
    }
    function mkView(name, query) {
        const id = AppController.saveView(name, { query: query, priorities: [], sort: "manual",
                                                  archived: false, showDone: false, view: "board" });
        verify(id !== "");
        tc.made.push(id);
        return id;
    }
    function row(sb, i) {
        const list = findChild(sb, "sidebar-views-list");
        tryVerify(function () { return list.itemAtIndex(i) !== null; }, 1000, "no row " + i);
        return list.itemAtIndex(i);
    }
    function indexOfView(id) {
        return AppController.savedViews.findIndex(v => v.id === id);
    }

    // ── places ──

    function test_places_show_their_keys_as_keys() {
        AppController.appSettingsJson = "{}";
        Style.apply("bold");
        const sb = bar(true);
        tryCompare(sb, "width", sb.expandedWidth, 1000);
        for (const s of ["today", "tasks", "knowledge", "docs", "settings"]) {
            const r = findChild(sb, "sidebar-section-" + s);
            verify(r !== null, s);
            verify(findChild(r, "sidebar-label").visible);
        }
        // "Ctrl 1", written as keymap.md writes keys, not "Ctrl+1".
        if (Qt.platform.os !== "osx") {
            compare(sb.prettyKeys(AppController.shortcutFor("section.today")), "Ctrl 1");
            compare(sb.prettyKeys("Ctrl+,"), "Ctrl ,");
            compare(sb.prettyKeys("Ctrl+Shift+B"), "Ctrl Shift B");
        }
    }

    // a-23 (H2-Today): rows on a 32px pitch, the first one under a 16px top,
    // a 34px "New task…" field and 14px of air (y = 104 at 100 %).
    function test_rows_sit_on_the_sheet_pitch() {
        const sb = bar(true);
        tryCompare(sb, "width", sb.expandedWidth, 1000);
        const today = findChild(sb, "sidebar-section-today");
        const tasks = findChild(sb, "sidebar-section-tasks");
        waitForRendering(sb);
        if (Theme.compact || Theme.spacious || Qt.platform.os === "osx") skip("the sheet is the default density");
        fuzzyCompare(tasks.y - today.y, Theme.px(32), 1, "row pitch");
        fuzzyCompare(today.mapToItem(sb, 0, 0).y, Theme.px(104), 3, "first row");
    }

    function test_exactly_one_row_is_active() {
        const sb = bar(true);
        AppController.currentView = "month";
        const lit = () => ["today", "tasks", "knowledge", "calendar", "docs", "settings"].filter(s => findChild(sb, "sidebar-section-" + s).active);
        compare(lit(), ["calendar"], "month is the Calendar's (dogfood 2026-10-22)");
        AppController.currentView = "docs";
        compare(lit(), ["docs"]);
        AppController.currentView = "notes";
        compare(lit(), ["knowledge"], "the Notes row");
        // A saved view applied: it is lit, Tasks is not.
        const id = mkView("sb-probe", "sbprobe");
        sb.activeSavedViewId = id;
        AppController.currentView = "board";
        compare(lit(), []);
        verify(row(sb, indexOfView(id)).active);
        sb.activeSavedViewId = "";
        compare(lit(), ["tasks"]);
    }

    function test_a_click_opens_the_place_on_its_last_view() {
        const sb = bar(true);
        AppController.currentView = "list";
        AppController.currentView = "month";
        AppController.currentView = "notes";
        mouseClick(findChild(sb, "sidebar-section-tasks"));
        compare(AppController.currentView, "list");
        mouseClick(findChild(sb, "sidebar-section-calendar"));
        compare(AppController.currentView, "month", "the Calendar on the zoom it was left on");
        mouseClick(findChild(sb, "sidebar-section-today"));
        compare(AppController.currentView, "today");
        mouseClick(findChild(sb, "sidebar-section-knowledge"));
        compare(AppController.currentView, "notes");
        // Notes and Docs are a row each: each opens its own screen.
        mouseClick(findChild(sb, "sidebar-section-docs"));
        compare(AppController.currentView, "docs");
        mouseClick(findChild(sb, "sidebar-section-knowledge"));
        compare(AppController.currentView, "notes");
    }

    function test_new_task_asks_main() {
        const sb = bar(true);
        let n = 0;
        sb.newTaskRequested.connect(function () { n++; });
        mouseClick(findChild(sb, "sidebar-new-task"));
        compare(n, 1);
    }

    // ── folded ──

    function test_folded_keeps_an_icon_per_row_and_names_in_tooltips() {
        const sb = bar(false);
        tryCompare(sb, "width", sb.collapsedWidth, 1000);
        const r = findChild(sb, "sidebar-section-tasks");
        verify(!findChild(r, "sidebar-label").visible);
        verify(r.width >= 36, "the icon cell is cut to " + r.width);
        AppController.currentView = "today";
        mouseClick(r);
        compare(AppController.currentView, "board", "a folded row still works");
    }

    function test_the_wordmark_folds_the_sidebar() {
        const sb = bar(true);
        let n = 0;
        sb.toggleRequested.connect(function () { n++; });
        mouseClick(findChild(sb, "sidebar-collapse"));
        compare(n, 1);
    }

    // Folded, the mark on top opens it again: the wordmark that folds it is
    // hidden then, and a key was the only way back.
    function test_the_mark_opens_the_folded_sidebar() {
        const sb = bar(false);
        const mark = findChild(sb, "sidebar-expand");
        verify(mark.visible, "no way back to the sidebar");
        let n = 0;
        sb.toggleRequested.connect(function () { n++; });
        mouseClick(mark);
        compare(n, 1);
        verify(!findChild(bar(true), "sidebar-expand").visible);
    }

    // ── My views ──

    function test_counts_hide_zero_and_cap_at_999() {
        const sb = bar(true);
        compare(sb.countText(0), "");
        compare(sb.countText(undefined), "");
        compare(sb.countText(7), "7");
        compare(sb.countText(999), "999");
        compare(sb.countText(1000), "999+");
    }

    function test_the_quiet_style_shows_no_counts_and_no_keys() {
        AppController.appSettingsJson = "{}";
        // A new install has no tasks (APP-271): give the view one to count.
        if (AppController.tasks.rowCount() === 0)
            AppController.saveTask(AppController.quickTaskDraft("sidebar count", new Date()));
        const id = mkView("sb-all", "");
        const sb = bar(true);
        Style.apply("bold");
        const r = row(sb, indexOfView(id));
        tryVerify(() => r.countText.length > 0, 1000, "bold: a count");
        // The palette hint shows only under the pointer (dogfood 2026-10-23).
        verify(!findChild(sb, "sidebar-palette-hint").visible);
        Style.apply("quiet");
        compare(r.countText, "");
        verify(!findChild(sb, "sidebar-palette-hint").visible);
    }

    function test_a_long_name_elides_and_says_it_in_full() {
        const name = "A saved view whose name goes on and on and does not stop";
        const id = mkView(name, "sbprobe");
        const sb = bar(true);
        const r = row(sb, indexOfView(id));
        const t = findChild(r, "sidebar-view-name");
        tryVerify(() => t.truncated, 1000, "the name was not cut");
        verify(r.tooltipText.indexOf(name) === 0, r.tooltipText);
        verify(r.Accessible.name.indexOf(name) === 0, r.Accessible.name);
        verify(t.width + t.x <= sb.width, "the name ran past the sidebar");
    }

    function test_a_view_on_a_missing_column_lives_and_says_so() {
        const id = mkView("sb-gone", "status:nosuchcolumn");
        const sb = bar(true);
        const r = row(sb, indexOfView(id));
        compare(r.countText, "?");
        verify(r.tooltipText.indexOf("status:nosuchcolumn") >= 0, r.tooltipText);
    }

    function test_twenty_views_scroll_inside_the_block() {
        for (let i = 0; i < 22; i++) mkView("sb-many-" + i, "sbprobe");
        const sb = bar(true, 600);
        const list = findChild(sb, "sidebar-views-list");
        tryVerify(() => list.contentHeight > list.height, 1000, "the list fits after all");
        verify(list.interactive);
        compare(sb.height, 600, "the sidebar did not grow");
        const settings = findChild(sb, "sidebar-section-settings");
        verify(settings.mapToItem(sb, 0, 0).y + settings.height <= sb.height, "Settings was pushed off");
    }

    function test_reorder_from_the_keyboard_and_by_drag() {
        const a = mkView("sb-a", "sbprobe");
        const b = mkView("sb-b", "sbprobe");
        const c = mkView("sb-c", "sbprobe");
        const sb = bar(true);
        const ia = indexOfView(a);
        row(sb, ia).forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down, Qt.ControlModifier);
        tryCompare(tc, "_bIndex", ia, 1000);
        compare(indexOfView(a), ia + 1);
        // Drag c two rows up, onto a's old place.
        const ic = indexOfView(c);
        const from = row(sb, ic);
        const to = row(sb, ic - 2);
        const p = to.mapToItem(from, to.width / 2, to.height / 2);
        mousePress(from, from.width / 2, from.height / 2);
        mouseMove(from, from.width / 2, from.height / 2 - Theme.spLg);
        mouseMove(from, p.x, p.y);
        mouseRelease(from, p.x, p.y);
        tryCompare(tc, "_cIndex", ic - 2, 1000);
        AppController.clearPendingUndo();
    }
    readonly property int _bIndex: AppController.savedViews.findIndex(v => v.name === "sb-b")
    readonly property int _cIndex: AppController.savedViews.findIndex(v => v.name === "sb-c")

    // ── the profile ──

    function test_a_long_profile_name_elides_with_the_full_name_spoken() {
        const pid = AppController.activeProfileId;
        const old = AppController.profileById(pid).name;
        AppController.renameProfile(pid, "Payments platform · checkout · a very long profile name");
        try {
            const sb = bar(true);
            const name = findChild(sb, "sidebar-profile-name");
            tryVerify(() => name.truncated, 1000);
            // After the row is laid out (a polish later).
            tryVerify(() => name.mapToItem(sb, name.width, 0).x <= sb.width, 1000,
                      "the name ends past the sidebar");
            const area = findChild(sb, "sidebar-profile");
            verify(area.Accessible.name.indexOf("a very long profile name") > 0, area.Accessible.name);
        } finally {
            AppController.renameProfile(pid, old);
        }
    }

    function test_eight_profiles_get_a_picker_with_search() {
        const home = AppController.activeProfileId;
        const made = [];
        while (AppController.profiles.length < 8) made.push(AppController.createProfile("sb-prof-" + made.length));
        AppController.activeProfileId = home;
        try {
            const sb = bar(true);
            sb.profileSwitcher.openMenu();
            const picker = findChild(sb.profileSwitcher, "sidebar-profile-picker");
            tryCompare(picker, "opened", true);
            keyClick(Qt.Key_S); keyClick(Qt.Key_B); keyClick(Qt.Key_Minus);
            tryVerify(() => picker.matches.length === made.length, 1000, "the search did not narrow the list");
            keyClick(Qt.Key_Return);
            tryCompare(picker, "opened", false);
            verify(AppController.activeProfileId !== home);
        } finally {
            AppController.activeProfileId = home;
            for (const id of made) AppController.deleteProfile(id);
            AppController.clearPendingUndo();
            AppController.activeProfileId = home;
        }
    }

    // The sync dot (APP-186), next to the profile now.
    function test_sync_dot_waits_400ms_and_goes_with_the_sync() {
        const ps = make('import TodoCpp; ProfileSwitcher { width: 180; height: 28; syncing: false }');
        compare(ps.syncDotDelay, 400);
        verify(!ps.syncDotDue(true, 1000, 1399));
        verify(ps.syncDotDue(true, 1000, 1400));
        verify(!ps.syncDotDue(false, 1000, 5000));
        ps.syncing = true;
        wait(200);
        verify(!ps.syncDotShown, "a quick pull flashed the dot");
        tryVerify(() => ps.syncDotShown, 1000, "a slow sync never said so");
        let asked = 0;
        ps.syncStatusRequested.connect(function () { asked++; });
        mouseClick(findChild(ps, "sidebar-sync-dot"));
        compare(asked, 1);
        ps.syncing = false;
        verify(!ps.syncDotShown, "the dot outlived the sync");
    }

    function test_shortcuts_are_in_the_catalog() {
        for (const id of ["section.today", "section.tasks", "section.knowledge", "view.settings", "rail.toggle", "task.new"])
            verify(AppController.shortcutFor(id).length > 0, id);
    }
}
