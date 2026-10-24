// The Calendar as a place of its own (dogfood 2026-10-22, sheet
// "Календарь"): Ctrl+4 opens it and Ctrl+5 Docs; its header is the range,
// ‹ Сегодня › and Day / Week / Month; p / t / n step the period in both
// keymaps; the keys on those buttons show only with the keyboard (or the
// pointer) on them; the Layers in the sidebar choose what the grid shows.
// Drives the real Main.qml.
//
// The test profile persists between runs: every task and event here has its
// own id, is deleted before it is made and again at the end, the dates are
// far from today, and the layers and the keymap are put back.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "CalendarPlace"
    when: windowShown

    property var win: null
    property string savedScheme: ""
    property var savedDate: null
    property var made: []
    property var events: []
    // A Wednesday far from today.
    readonly property var wed: new Date(2031, 2, 12)

    function initTestCase() {
        tc.savedScheme = AppController.keymapScheme;
        tc.savedDate = AppController.selectedDate;
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        wait(1200);
        const ps = popups();
        for (let i = 0; i < ps.length; i++) if (ps[i].opened) ps[i].close();
        tc.win.width = 1440;
        tc.win.height = 900;
        AppController.currentView = "board";
    }
    function cleanupTestCase() {
        for (const k of ["meetings", "timed", "reminders", "deadlines"]) Theme.setCalendarLayer(k, true);
        AppController.keymapScheme = tc.savedScheme;
        AppController.selectedDate = tc.savedDate;
        if (tc.win) tc.win.destroy();
    }
    function cleanup() {
        for (let i = 0; i < tc.made.length; i++) AppController.deleteTask(tc.made[i]);
        for (let i = 0; i < tc.events.length; i++) AppController.deleteEvent(tc.events[i]);
        tc.made = [];
        tc.events = [];
        AppController.clearPendingUndo();
        for (const k of ["meetings", "timed", "reminders", "deadlines"]) Theme.setCalendarLayer(k, true);
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
    function find(root, pred) {
        if (!root) return null;
        if (pred(root)) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) { const r = find(kids[i], pred); if (r) return r; }
        return null;
    }
    function byName(name) { return find(tc.win.contentItem, function (it) { return it.objectName === name; }); }
    // The ClickArea inside a drawn button: the item that takes the keyboard.
    function clickAreaOf(item) {
        return find(item, function (it) { return it !== item && it.keyboardFocused !== undefined && it.activated !== undefined; });
    }
    function sameDay(a, b) {
        return !!a && !!b && a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }
    function seed(id, fields) {
        AppController.deleteTask(id);
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.id = id;
        d.title = id + " probe";
        for (const k in fields) d[k] = fields[k];
        verify(AppController.saveTask(d), "seeding " + id);
        tc.made.push(id);
    }
    function openCalendar() {
        tc.win.requestActivate();
        AppController.currentView = "board";
        wait(50);
        keyClick(Qt.Key_4, Qt.ControlModifier);
        tryCompare(AppController, "currentSection", "calendar");
        tryVerify(() => !!tc.win.activeViewItem() && tc.win.activeViewItem().step !== undefined, 3000, "the grid is up");
    }

    // Ctrl+4 is the Calendar in both keymaps, a sidebar row after Notes;
    // Ctrl+5 is Docs; g c (the old lens key) still opens the Calendar.
    function test_ctrl4_opens_the_calendar_and_ctrl5_docs() {
        for (const scheme of ["vim", "classic"]) {
            AppController.keymapScheme = scheme;
            compare(AppController.shortcutFor("section.calendar"), "Ctrl+4", scheme);
            compare(AppController.shortcutFor("view.docs"), "Ctrl+5", scheme);
            openCalendar();
            verify(["day", "week", "month"].indexOf(AppController.currentView) >= 0, AppController.currentView);
            const row = byName("sidebar-section-calendar");
            verify(row !== null, "a Calendar row in the sidebar");
            verify(row.active, "and it is the lit one");
            const notes = byName("sidebar-section-knowledge");
            const docs = byName("sidebar-section-docs");
            verify(notes.y < row.y && row.y < docs.y, "after Notes, before Docs");
            compare(byName("view-header-title").text, byName("calendar-nav").title, "the title is the range");
            keyClick(Qt.Key_5, Qt.ControlModifier);
            tryCompare(AppController, "currentView", "docs");
            tc.win.runCommand("view.calendar");
            tryCompare(AppController, "currentSection", "calendar");
        }
        // Tasks has no calendar tab any more: board and list.
        AppController.currentView = "board";
        const lenses = byName("view-header-lenses");
        verify(lenses !== null);
        compare(byName("view-header").lenses.map(l => l.id).join(","), "board,list");
    }

    // p / t / n: a period back, today, a period on — a day, a week or a
    // month as the zoom shows — in both keymaps.
    function test_p_t_n_step_the_period() {
        for (const scheme of ["vim", "classic"]) {
            AppController.keymapScheme = scheme;
            openCalendar();
            AppController.currentView = "week";
            AppController.selectedDate = tc.wed;
            tc.win.focusActiveView();
            wait(50);
            // The week grid pages from its first day: n lands on the next
            // week's Monday, p on this week's.
            const weekOf = () => tc.win.activeViewItem().weekStart;
            tryVerify(() => sameDay(weekOf(), new Date(2031, 2, 10)), 2000);
            keyClick(Qt.Key_N);
            tryVerify(() => sameDay(weekOf(), new Date(2031, 2, 17)), 2000, scheme + ": n is a week on");
            keyClick(Qt.Key_P);
            tryVerify(() => sameDay(weekOf(), new Date(2031, 2, 10)), 2000, scheme + ": p is a week back");
            AppController.currentView = "day";
            AppController.selectedDate = tc.wed;
            tryVerify(() => !!tc.win.activeViewItem() && tc.win.activeViewItem().dayZoom === true, 2000);
            tc.win.focusActiveView();
            wait(50);
            keyClick(Qt.Key_N);
            tryVerify(() => sameDay(AppController.selectedDate, new Date(2031, 2, 13)), 2000, scheme + ": n is a day on the day zoom");
            AppController.currentView = "month";
            tryVerify(() => !!tc.win.activeViewItem() && tc.win.activeViewItem().cells !== undefined, 2000);
            tc.win.focusActiveView();
            wait(50);
            keyClick(Qt.Key_P);
            tryVerify(() => AppController.selectedDate.getMonth() === 1 && AppController.selectedDate.getFullYear() === 2031,
                      2000, scheme + ": p is a month back on the month zoom");
            keyClick(Qt.Key_T);
            tryVerify(() => sameDay(AppController.selectedDate, AppController.today), 2000, scheme + ": t is today");
            AppController.currentView = "week";
        }
    }

    // In Vim t is the timer of the task under the cursor: on the Calendar's
    // grid the Calendar's t goes first, anywhere else the timer keeps it.
    function test_t_is_today_on_the_grid_and_the_timer_elsewhere() {
        AppController.keymapScheme = "vim";
        openCalendar();
        verify(tc.win._keyLive("cal.today.alt"), "t is live on the grid");
        AppController.currentView = "board";
        wait(50);
        verify(!tc.win._keyLive("cal.today.alt"), "and nowhere else");
        verify(!tc.win._calendarKeyTaken("task.timer"), "the timer keeps t off the grid");
        AppController.currentView = "week";
        wait(50);
        verify(tc.win._calendarKeyTaken("task.timer"), "on the grid the timer stands down");
    }

    // The keys on ‹ Сегодня › and on Day / Week / Month are hidden until the
    // keyboard is on the button (the pointer too; offscreen has no hover).
    function test_key_letters_show_only_with_the_keyboard_on_the_button() {
        AppController.keymapScheme = "vim";
        openCalendar();
        AppController.currentView = "week";
        wait(50);
        const checks = [["calendar-prev", "calendar-prev-key-after", "p"],
                        ["calendar-today", "calendar-today-key-after", "t"],
                        ["calendar-next", "calendar-next-key", "n"],
                        ["zoom-day", "zoom-day-key", "z d"],
                        ["zoom-week", "zoom-week-key", "z w"],
                        ["zoom-month", "zoom-month-key", "z m"]];
        for (const c of checks) {
            const btn = byName(c[0]);
            verify(btn !== null, c[0]);
            const hint = byName(c[1]);
            verify(hint !== null, c[1]);
            compare(hint.keys, c[2], c[0] + " says its key");
            verify(!hint.visible, c[0] + ": the key is hidden at rest");
            const ca = clickAreaOf(btn);
            verify(ca !== null, c[0] + " has a click area");
            ca.forceActiveFocus(Qt.TabFocusReason);
            tryVerify(() => hint.visible, 1000, c[0] + ": the key shows with the keyboard on it");
            tc.win.focusActiveView();
            tryVerify(() => !hint.visible, 1000, c[0] + ": and hides again when it leaves");
        }
        // The buttons do what their keys do.
        AppController.selectedDate = tc.wed;
        mouseClick(byName("calendar-next"));
        tryVerify(() => sameDay(AppController.selectedDate, new Date(2031, 2, 19)), 1000, "› is a week on");
        mouseClick(byName("calendar-today"));
        tryVerify(() => sameDay(AppController.selectedDate, AppController.today), 1000, "Сегодня is today");
        mouseClick(byName("zoom-month"));
        tryCompare(AppController, "currentView", "month");
        mouseClick(byName("zoom-week"));
        tryCompare(AppController, "currentView", "week");
    }

    // The Layers: meetings, tasks with a time, reminders, deadlines — each
    // a box in the sidebar, kept in the settings, and the grid follows.
    function test_layers_choose_what_the_grid_shows() {
        AppController.keymapScheme = "vim";
        seed("CALP-TIMED", { scheduledAt: new Date(2031, 2, 12, 14, 0), scheduledHasTime: true });
        seed("CALP-DUE", { dueAt: new Date(2031, 2, 13), dueHasTime: false });
        seed("CALP-REM", { reminder: true, remindAt: new Date(2031, 2, 11, 15, 0) });
        const ev = AppController.newEventDraft(10, tc.wed);
        ev.title = "CALP meeting";
        AppController.saveEvent(ev);
        tc.events.push(ev.id);
        openCalendar();
        AppController.currentView = "week";
        AppController.selectedDate = tc.wed;
        const grid = () => tc.win.activeViewItem();
        const has = (list, id) => list.some(x => x.id === id);
        const dues = () => grid().days.reduce((a, d) => a.concat(d.tasks), []);
        tryVerify(() => has(grid().flatEvents, ev.id), 2000, "the meeting is on the grid");
        verify(has(grid().flatBlocks, "CALP-TIMED"), "the timed task is a block");
        verify(has(grid().flatReminders, "CALP-REM"), "the reminder is on its day");
        verify(has(dues(), "CALP-DUE"), "the deadline is a flag");
        verify(byName("week-reminder-CALP-REM") !== null, "the reminder is drawn with its bell");

        const layers = byName("sidebar-calendar-layers");
        verify(layers !== null && layers.visible, "the Layers are in the sidebar while the Calendar is open");
        const rows = [["meetings", () => has(grid().flatEvents, ev.id)],
                      ["timed", () => has(grid().flatBlocks, "CALP-TIMED")],
                      ["reminders", () => has(grid().flatReminders, "CALP-REM")],
                      ["deadlines", () => has(dues(), "CALP-DUE")]];
        for (const r of rows) {
            const row = byName("sidebar-layer-" + r[0]);
            verify(row !== null, r[0]);
            verify(row.on, r[0] + " is on by default");
            mouseClick(row);
            tryVerify(() => !Theme.calendarLayers[r[0]], 1000, r[0] + " is switched off");
            tryVerify(() => !r[1](), 2000, r[0] + " leaves the grid");
            verify(JSON.parse(AppController.appSettingsJson).calendar.layers[r[0]] === false, r[0] + " is kept");
            // The others stay.
            for (const o of rows) if (o !== r) verify(o[1](), o[0] + " stays while " + r[0] + " is off");
            mouseClick(row);
            tryVerify(() => r[1](), 2000, r[0] + " is back");
        }
        // The month follows the same layers.
        AppController.currentView = "month";
        tryVerify(() => grid().cells !== undefined, 2000);
        const monthHas = (id) => grid().cells.some(c => c.tasks.some(t => t.id === id));
        tryVerify(() => monthHas("CALP-REM"), 2000, "the reminder is in its month day");
        Theme.setCalendarLayer("reminders", false);
        tryVerify(() => !monthHas("CALP-REM"), 2000, "and leaves it with its layer");
        AppController.currentView = "board";
        wait(50);
        verify(!byName("sidebar-calendar-layers").visible, "the Layers go with the Calendar");
    }
}
