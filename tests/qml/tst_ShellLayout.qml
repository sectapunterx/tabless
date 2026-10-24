// The window's own furniture at small sizes (audit UX-7, 17, 18, 19, 23,
// 29, 31). Drives the real Main.qml.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "ShellLayout"
    when: windowShown

    property var win: null
    property string savedProject: ""
    property string savedUser: ""

    function initTestCase() {
        tc.savedProject = AppController.crumbProject;
        tc.savedUser = AppController.crumbUser;
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        wait(1200);
        const ps = popups();
        for (let i = 0; i < ps.length; i++) if (ps[i].opened) ps[i].close();
        AppController.currentView = "board";
    }
    function cleanupTestCase() {
        AppController.crumbProject = tc.savedProject;
        AppController.crumbUser = tc.savedUser;
        if (tc.win) tc.win.destroy();
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
            if (String(ps[i]).indexOf(prefix) === 0 || ps[i].objectName === prefix) return ps[i];
        return null;
    }
    function find(root, pred) {
        if (!root) return null;
        if (pred(root)) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) { const r = find(kids[i], pred); if (r) return r; }
        return null;
    }
    function byName(name) { return find(tc.win.contentItem, function (it) { return it.objectName === name; }); }

    // UX-31: the title ends with the display name, so Windows does not add it
    // a second time.
    function test_window_title_names_the_app_once() {
        // Tabless since APP-283 (lowkey in 0.8.0): the name alone, as the taskbar shows it.
        compare(tc.win.title, "Tabless");
    }

    // heap 2 (APP-258): every place by its key, from the real window.
    // Ctrl+1…5 are Today / Tasks / Notes / Calendar / Docs (the Calendar a
    // place of its own since dogfood 2026-10-22), Ctrl+, Settings; Tasks and
    // the Calendar come back on the lens / zoom they were left on.
    function test_every_place_by_its_key() {
        tc.win.width = 1440;
        tc.win.height = 900;
        tc.win.requestActivate();
        AppController.currentView = "board";
        wait(50);
        keyClick(Qt.Key_1, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "today");
        keyClick(Qt.Key_2, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "board");
        tc.win.openLens("list");
        compare(AppController.currentView, "list");
        tc.win.openLens("calendar");
        compare(AppController.currentView, "week");
        tryCompare(AppController, "currentSection", "calendar");
        keyClick(Qt.Key_3, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "notes");
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "settings");
        keyClick(Qt.Key_2, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "list", 1000, "Tasks reopens on its lens, not the calendar");
        keyClick(Qt.Key_4, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "week", 1000, "the Calendar on its zoom");
        keyClick(Qt.Key_5, Qt.ControlModifier);
        tryCompare(AppController, "currentView", "docs");
        tc.win.openLens("board");
        compare(AppController.currentView, "board");
        tryCompare(AppController, "currentSection", "tasks");
    }

    // The top bar is gone (APP-258): no "+ Task", no panel toggle up there;
    // the sidebar's field and the header's lenses instead.
    function test_the_shell_has_no_top_bar() {
        compare(byName("topbar-new-task"), null);
        compare(byName("topbar-right-panel"), null);
        verify(byName("sidebar-new-task") !== null);
        AppController.currentView = "board";
        const header = byName("view-header");
        verify(header !== null && header.visible);
        AppController.currentView = "today";
        verify(!header.visible, "Today carries its own title");
        compare(byName("right-panel"), null, "no right panel anywhere (DG-002)");
        AppController.currentView = "board";
    }

    // Narrower than ~1100px the sidebar folds to its icons on its own.
    function test_a_narrow_window_folds_the_sidebar() {
        tc.win.width = 1440;
        const rail = byName("sidebar");
        tryCompare(rail, "expanded", true);
        tc.win.width = 1000;
        tryCompare(rail, "expanded", false);
        tc.win.width = 1440;
        tryCompare(rail, "expanded", true);
    }

    // UX-18: Go to date opens over the middle of the window, not its corner.
    function test_go_to_date_is_centred() {
        tc.win.width = 1400;
        tc.win.height = 900;
        const g = popup("go-to-date");
        g.openAt(AppController.selectedDate, tc.win.contentItem);
        tryCompare(g, "opened", true);
        wait(50);
        const x = g.x, w = g.width;
        g.close();
        verify(Math.abs(x + w / 2 - tc.win.width / 2) < 4, "centre at " + (x + w / 2));
    }

    // UX-19: the close-to-tray dialog title is drawn in the theme.
    function test_close_dialog_is_themed() {
        const d = popup("close-to-tray-ask");
        verify(d !== null);
        verify(d.header && Qt.colorEqual(d.header.color, Theme.text), "unstyled dialog header");
    }

}
