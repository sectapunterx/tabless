// Settings and back (dogfood 2026-10-22 #4) in the real Main.qml: Esc in a
// page goes to the section list, Esc on the list leaves Settings for exactly
// where the person was — the view and the task open in its document — and
// Settings keep their own place for a minute (SettingsPlace.keepsPlace, a
// pure rule driven here by an injected clock, never a 60 s wait). The idiot
// test: Esc pressed many times fast never dead-ends and never closes or
// opens anything unrelated.
import QtQuick
import QtTest
import TodoCpp
import "../../qml/SettingsPlace.js" as Place

TestCase {
    id: tc
    name: "SettingsReturn"
    when: windowShown

    property var win: null
    property var seeded: []
    property real fakeNow: 1700000000000

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
        tc.seeded = [];
        for (let i = 0; i < 2; i++) {
            const d = AppController.newTaskDraft("todo");
            d._isNew = true;
            d.id = "SRET-" + i;
            d.title = "settings return probe " + i;
            AppController.saveTask(d);
            tc.seeded.push(d.id);
        }
        closeAll();
        AppController.currentView = "board";
        tc.win._settingsReturn = null;
        // Each case starts long after the last one left Settings: fresh.
        tc.fakeNow += 120000;
        tc.win.focusActiveView();
        wait(30);
    }
    function cleanup() {
        closeAll();
        AppController.currentView = "board";
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteTask(tc.seeded[i]);
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
    function byName(root, name) { return find(root, function (it) { return it.objectName === name; }); }
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
            if (o && o.opened !== undefined && o.open !== undefined && o.close !== undefined) out.push(o);
        }
        return out;
    }
    function anyPopupOpen() {
        const ps = popups();
        for (let i = 0; i < ps.length; i++) if (ps[i].opened && ps[i].objectName !== "task-doc") return true;
        return false;
    }
    function closeAll() {
        const ps = popups();
        for (let i = 0; i < ps.length; i++) if (ps[i].opened) ps[i].close();
        const d = doc();
        if (d && d.opened) d.close();
    }
    function doc() { return byName(tc.win.contentItem, "task-doc"); }
    function openDoc(id) {
        tc.win.openTask(id);
        const d = doc();
        tryCompare(d, "opened", true);
        tryVerify(function () { return tc.win._focusInPanel; }, 1000, "the keyboard is not in the document");
        return d;
    }
    function openSettingsByKey() {
        keySequence(tc.win._kbd("view.settings"));
        tryCompare(AppController, "currentView", "settings");
        const sv = tc.win.activeViewItem();
        verify(sv !== null);
        sv.clock = function () { return tc.fakeNow; };
        return sv;
    }
    function focusInside(item) {
        for (let p = tc.win.activeFocusItem; p; p = p.parent) if (p === item) return true;
        return false;
    }

    // ── the rule itself, with fixed times ──
    function test_keepsPlace_rule() {
        const t0 = 1700000000000;
        verify(Place.keepsPlace(t0, t0));
        verify(Place.keepsPlace(t0, t0 + 1));
        verify(Place.keepsPlace(t0, t0 + 59999));
        verify(Place.keepsPlace(t0, t0 + 60000), "exactly a minute still keeps the place");
        verify(!Place.keepsPlace(t0, t0 + 60001), "past a minute: fresh");
        verify(!Place.keepsPlace(t0, t0 + 3600000));
        verify(!Place.keepsPlace(0, t0), "never left: fresh");
        verify(!Place.keepsPlace(null, t0));
        verify(!Place.keepsPlace(undefined, t0));
        verify(!Place.keepsPlace(t0, t0 - 1), "a clock that went back: fresh");
        verify(!Place.keepsPlace(t0, NaN));
        compare(Place.KEEP_MS, 60000);
    }

    // ── open task → Ctrl+, → into a page → Esc Esc → the same view, the same task ──
    function test_esc_chain_returns_to_the_view_and_the_task() {
        AppController.currentView = "list";
        wait(50);
        const d = openDoc("SRET-1");
        d.full = true;
        openSettingsByKey();
        compare(d.opened, false, "Settings close the document while they are up");
        // Into a page from the list.
        tryVerify(function () { return focusInside(tc.win.activeViewItem()); }, 1000);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Right);
        wait(50);
        const sv = tc.win.activeViewItem();
        const nav = byName(sv, "settings-nav-" + sv.activeSection);
        verify(!nav.activeFocus, "the keyboard went into the page");
        // Esc: page → list.
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return nav.activeFocus; }, 1000, "Esc in a page lands on the section list");
        compare(AppController.currentView, "settings");
        // Esc: list → where the person was.
        keyClick(Qt.Key_Escape);
        tryCompare(AppController, "currentView", "list");
        tryCompare(d, "taskId", "SRET-1");
        compare(d.full, true, "the document comes back as wide as it was");
        tryVerify(function () { return tc.win._focusInPanel; }, 1000, "the keyboard is back in the document");
        // Esc once more: the document's own Esc closes it; the view stays.
        keyClick(Qt.Key_Escape);
        tryCompare(d, "opened", false);
        compare(AppController.currentView, "list");
    }

    // An empty search's Esc leaves too; a search with words clears first.
    function test_search_esc_clears_then_leaves() {
        openSettingsByKey();
        const sv = tc.win.activeViewItem();
        tryVerify(function () { return focusInside(sv); }, 1000);
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_B);
        compare(sv.searchText, "ab");
        keyClick(Qt.Key_Escape);
        compare(sv.searchText, "");
        compare(AppController.currentView, "settings");
        keyClick(Qt.Key_Escape);
        tryCompare(AppController, "currentView", "board");
    }

    // Keyboard on nothing of Settings (the window itself): Esc still leaves.
    function test_esc_with_focus_on_the_window_still_leaves() {
        openSettingsByKey();
        wait(50);
        tc.win.contentItem.forceActiveFocus();
        verify(!tc.win._focusInSettings);
        keyClick(Qt.Key_Escape);
        tryCompare(AppController, "currentView", "board");
    }

    // The idiot test: Esc hammered from a page with a task open behind.
    function test_esc_hammered_never_breaks_anything() {
        const d = openDoc("SRET-0");
        openSettingsByKey();
        tryVerify(function () { return focusInside(tc.win.activeViewItem()); }, 1000);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Right);
        for (let i = 0; i < 12; i++) keyClick(Qt.Key_Escape);
        wait(100);
        compare(AppController.currentView, "board", "never back in Settings, never somewhere else");
        verify(AppController.taskById("SRET-0").id === "SRET-0", "the task is untouched");
        verify(AppController.taskById("SRET-1").id === "SRET-1");
        verify(!anyPopupOpen(), "nothing opened on the way");
        compare(d.opened, false, "the extra Esc closed the document, as its own Esc does");
        // And the app still answers: Settings open and close again.
        openSettingsByKey();
        keyClick(Qt.Key_Escape);
        tryCompare(AppController, "currentView", "board");
        // Esc held down in Settings (auto-repeat) is the same.
        openSettingsByKey();
        for (let i = 0; i < 8; i++) keyPress(Qt.Key_Escape, Qt.NoModifier, -1);
        keyRelease(Qt.Key_Escape);
        wait(50);
        compare(AppController.currentView, "board");
        verify(!anyPopupOpen());
    }

    // A task deleted while Settings were up: back to the view, no document.
    function test_a_task_gone_meanwhile_is_not_reopened() {
        openDoc("SRET-0");
        openSettingsByKey();
        AppController.deleteTask("SRET-0");
        tc.win.leaveSettings();
        tryCompare(AppController, "currentView", "board");
        compare(doc().opened, false);
    }

    // ── Settings' own place, for a minute ──
    function test_place_is_kept_within_a_minute_and_dropped_after() {
        let sv = openSettingsByKey();
        sv.searchText = "";
        const row = byName(sv, "settings-nav-calendar");
        row.forceActiveFocus();
        compare(sv.activeSection, "calendar");
        // A scroll of the page, where there is room for one.
        const body = byName(sv, "settings-body-scroll");
        wait(100);
        const room = Math.max(0, body.contentHeight - body.height);
        const y = Math.min(room, 120);
        body.contentY = y;
        keyClick(Qt.Key_Escape);   // the list → back to the board
        tryCompare(AppController, "currentView", "board");

        // 30 s later: the same section, row and scroll.
        tc.fakeNow += 30000;
        sv = openSettingsByKey();
        compare(sv.activeSection, "calendar");
        tryVerify(function () { return row.activeFocus; }, 1000, "the row that had the keyboard has it again");
        tryCompare(body, "contentY", y);

        // A search kept too.
        sv.searchText = "week";
        keyClick(Qt.Key_Escape);   // the row: clears the search first
        compare(sv.searchText, "");
        sv.searchText = "week";
        tc.win.leaveSettings();
        tryCompare(AppController, "currentView", "board");
        tc.fakeNow += 1000;
        sv = openSettingsByKey();
        compare(sv.searchText, "week");
        sv.searchText = "";
        row.forceActiveFocus();
        tc.win.leaveSettings();
        tryCompare(AppController, "currentView", "board");

        // A minute and a second later: fresh, on the first section, the
        // caret in the search.
        tc.fakeNow += 61000;
        sv = openSettingsByKey();
        compare(sv.activeSection, "general");
        compare(sv.searchText, "");
        compare(body.contentY, 0);
        tryVerify(function () { return byName(sv, "settings-search").activeFocus; }, 1000);
        tc.win.leaveSettings();
        tryCompare(AppController, "currentView", "board");
    }

    // The row that had the keyboard is rebuilt while Settings are away (the
    // keys list filtered again, a language switch): back within the minute,
    // the caret goes to the search. The place kept the row as a bare pointer
    // and coming back read the destroyed row - a crash on Qt 6.9.1, the Linux
    // and macOS builds.
    function test_a_kept_row_rebuilt_meanwhile_is_not_reused() {
        let sv = openSettingsByKey();
        sv.searchText = "";
        sv.activeSection = "shortcuts";
        let chip = null;
        tryVerify(function () { chip = byName(sv, "hotkey-chip-palette.open"); return chip !== null && chip.visible; }, 3000);
        chip.forceActiveFocus();
        verify(chip.activeFocus);
        tc.win.leaveSettings();
        tryCompare(AppController, "currentView", "board");
        // While away: the list is filtered and cleared, every row made anew.
        const field = byName(sv, "settings-keys-search");
        field.text = "zzz";
        wait(20);
        field.text = "";
        wait(20);
        gc();
        verify(byName(sv, "hotkey-chip-palette.open") !== chip, "the row was not rebuilt");
        compare(sv._placeFocus, null, "the kept place still points at the destroyed row");
        tc.fakeNow += 1000;
        sv = openSettingsByKey();
        compare(sv.activeSection, "shortcuts");
        tryVerify(function () { return byName(sv, "settings-search").activeFocus; }, 1000, "the caret is not in the search");
        tc.win.leaveSettings();
        tryCompare(AppController, "currentView", "board");
    }

    // A deep link opens where it points even within the minute.
    function test_a_deep_link_wins_over_the_kept_place() {
        let sv = openSettingsByKey();
        byName(sv, "settings-nav-calendar").forceActiveFocus();
        tc.win.leaveSettings();
        tryCompare(AppController, "currentView", "board");
        tc.fakeNow += 5000;
        tc.win.runCommand("settings:git");
        tryCompare(AppController, "currentView", "settings");
        sv = tc.win.activeViewItem();
        tryCompare(sv, "activeSection", "git");
        tc.win.leaveSettings();
        tryCompare(AppController, "currentView", "board");
    }
}
