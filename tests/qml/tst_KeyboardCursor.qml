// One keyboard cursor in every view (APP-276), regions on F6 (APP-277) and
// a keyboard path for every drag (APP-278): the week, the month and Today
// walk tasks and meetings with j / k / h / l, move and stretch them, and
// every change takes one Ctrl+Z. Drives the real Main.qml with real keys.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "KeyboardCursor"
    when: windowShown

    property var win: null
    property var tasks: []
    property var events: []
    property date day: new Date()
    property date prevDate: new Date()

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
        tc.prevDate = AppController.selectedDate;
    }

    function cleanupTestCase() {
        AppController.selectedDate = tc.prevDate;
        if (tc.win) tc.win.destroy();
        tc.win = null;
    }

    // A far day with one task at 10:00 and one meeting at 12:00, nothing else.
    function init() {
        const d = new Date();
        d.setDate(d.getDate() + 777);
        d.setHours(0, 0, 0, 0);
        // A Wednesday, so a day either side stays in the same week.
        while (d.getDay() !== 3) d.setDate(d.getDate() + 1);
        tc.day = d;
        const t = AppController.newTaskDraft("todo");
        t._isNew = true;
        t.id = "KCUR-1";
        t.title = "kcur probe task";
        t.scheduledAt = new Date(d.getFullYear(), d.getMonth(), d.getDate(), 10, 0);
        t.scheduledHasTime = true;
        AppController.saveTask(t);
        tc.tasks = ["KCUR-1"];
        const ev = AppController.newEventDraft(12, d);
        ev.title = "kcur probe meeting";
        ev.date = d;
        ev.start = 12;
        ev.end = 13;
        AppController.saveEvent(ev);
        tc.events = [ev.id];
        AppController.selectedDate = d;
        closeAll();
    }

    function cleanup() {
        closeAll();
        for (let i = 0; i < tc.tasks.length; i++) AppController.deleteTask(tc.tasks[i]);
        for (let i = 0; i < tc.events.length; i++) AppController.deleteEvent(tc.events[i]);
        tc.tasks = [];
        tc.events = [];
        AppController.clearPendingUndo();
        AppController.resetAllShortcuts();
    }

    function typeName(o) {
        const s = String(o);
        const i = s.indexOf("(");
        return i > 0 ? s.slice(0, i) : s;
    }
    function popup(prefix) {
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) {
            const o = cd[i];
            if (!o || o.opened === undefined || o.open === undefined) continue;
            if (typeName(o).indexOf(prefix) === 0 || o.objectName === prefix) return o;
        }
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
    function byName(name, root) {
        return find(root || tc.win.contentItem, function (it) { return it.objectName === name; });
    }
    function closeAll() {
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) {
            const o = cd[i];
            if (o && o.opened === true && typeof o.close === "function") o.close();
        }
        const doc = byName("task-doc");
        if (doc && doc.opened && typeof doc.close === "function") doc.close();
        wait(120);
        AppController.clearSelection();
        const v = tc.win.activeViewItem();
        if (v && v.clearCursor) v.clearCursor();
        tc.win.focusActiveView();
        wait(20);
    }
    function openView(name) {
        AppController.currentView = name;
        tryVerify(function () { const v = tc.win.activeViewItem(); return v && typeof v.moveCursor === "function"; }, 3000);
        AppController.selectedDate = tc.day;
        wait(60);
        tc.win.focusActiveView();
        wait(20);
        return tc.win.activeViewItem();
    }
    function task() { return AppController.taskById("KCUR-1"); }
    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }

    // Week: j reaches the task, then the meeting (in the Russian layout too),
    // Enter opens what it is on, Shift L moves the task a day and the cursor
    // follows it, Ctrl Shift J stretches, Del deletes — each one Ctrl+Z.
    function test_week_cursor_walks_tasks_and_meetings() {
        const v = openView("week");
        keyClick(Qt.Key_J);
        verify(v.cursorVisible, "j shows the cursor in the week");
        compare(v.cursorTaskId, "KCUR-1");
        verify(find(tc.win.contentItem, function (it) { return it.objectName === "week-cursor" && it.visible; }) !== null,
               "the cursor is drawn");
        // о — j in the Russian layout. Windows only: the router reads the
        // native key there, and QTest on Linux aborts on a non-ASCII key.
        if (Qt.platform.os === "windows") keyClick(0x43E);
        else keyClick(Qt.Key_J);
        verify(v.cursorKey.indexOf("event:") === 0, "j goes on to the meeting");
        keyClick(Qt.Key_Return);
        // The meeting opens in the panel on the right (DG-120).
        const panel = find(tc.win.contentItem, function (it) { return it.objectName === "event-panel"; });
        tryVerify(function () { return panel && panel.opened; }, 2000);
        panel.close();
        closeAll();
        v.moveCursor(0, 0);
        v.moveCursor(0, 1);
        verify(v.cursorKey.indexOf("event:") === 0);

        // The meeting a grid step longer, and back.
        const before = AppController.eventById(tc.events[0]).end;
        keyClick(Qt.Key_J, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(function () { return AppController.eventById(tc.events[0]).end > before; }, 2000);
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () { return Math.abs(AppController.eventById(tc.events[0]).end - before) < 1e-6; }, 2000);

        // The task a day later; the cursor stays on it.
        v.moveCursor(0, -1);
        compare(v.cursorTaskId, "KCUR-1");
        keyClick(Qt.Key_L, Qt.ShiftModifier);
        tryVerify(function () { return task().scheduledAt.getDate() !== tc.day.getDate(); }, 2000);
        verify(sameDay(task().scheduledAt, new Date(tc.day.getFullYear(), tc.day.getMonth(), tc.day.getDate() + 1)));
        tryCompare(v, "cursorTaskId", "KCUR-1");
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () { return sameDay(task().scheduledAt, tc.day); }, 2000);

        // Its block longer, and back.
        AppController.selectedDate = tc.day;
        wait(50);
        v.moveCursor(0, 0);
        v.moveCursor(0, -100000);
        compare(v.cursorTaskId, "KCUR-1");
        const len = AppController.taskBlockMinutes("KCUR-1");
        keyClick(Qt.Key_J, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(function () { return AppController.taskBlockMinutes("KCUR-1") > len; }, 2000);
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () { return AppController.taskBlockMinutes("KCUR-1") === len; }, 2000);

        // Del on the cursor, no selection needed; Ctrl+Z brings it back.
        keyClick(Qt.Key_Delete);
        tryVerify(function () { return !task() || !task().id; }, 2000);
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () { return task() && task().id === "KCUR-1"; }, 2000);
    }

    // h / l go to the day beside it; Esc lets go of the cursor.
    function test_week_h_l_and_esc() {
        const v = openView("week");
        keyClick(Qt.Key_J);
        compare(v.cursorTaskId, "KCUR-1");
        keyClick(Qt.Key_L);
        verify(sameDay(AppController.selectedDate, new Date(tc.day.getFullYear(), tc.day.getMonth(), tc.day.getDate() + 1)));
        compare(v.cursorKey, "", "the next day has nothing on it");
        keyClick(Qt.Key_H);
        verify(sameDay(AppController.selectedDate, tc.day));
        keyClick(Qt.Key_Escape);
        verify(!v.cursorVisible, "Esc lets go of the cursor");
    }

    // Month: the cursor on the day's items, the task moved a day with Shift L.
    function test_month_cursor() {
        const v = openView("month");
        keyClick(Qt.Key_J);
        verify(v.cursorVisible);
        compare(v.cursorTaskId, "KCUR-1");
        keyClick(Qt.Key_J);
        verify(v.cursorKey.indexOf("event:") === 0, "j goes on to the meeting");
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_L, Qt.ShiftModifier);
        tryVerify(function () { return !sameDay(task().scheduledAt, tc.day); }, 2000);
        tryCompare(v, "cursorTaskId", "KCUR-1");
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () { return sameDay(task().scheduledAt, tc.day); }, 2000);
    }

    // Today: j reaches the task, Enter opens its document, 1 sets priority.
    function test_today_cursor() {
        const v = openView("today");
        keyClick(Qt.Key_J);
        verify(v.cursorVisible);
        compare(v.cursorTaskId, "KCUR-1");
        verify(find(v, function (it) { return it.objectName === "today-cursor" && it.visible; }) !== null);
        keyClick(Qt.Key_2);
        tryVerify(function () { return task().priority === "P1"; }, 2000);
        keyClick(Qt.Key_J);
        verify(v.cursorKey.indexOf("event:") === 0);
        keyClick(Qt.Key_K);
        keyClick(Qt.Key_Return);
        tryVerify(function () { const d = byName("task-doc"); return d && d.opened; }, 2000);
    }

    // s: the small field reads a date like the quick input; Enter sets it,
    // Ctrl+Z takes it back; Shift S "no" takes a deadline off.
    function test_s_schedules_from_a_typed_date() {
        const v = openView("week");
        keyClick(Qt.Key_J);
        compare(v.cursorTaskId, "KCUR-1");
        keyClick(Qt.Key_S);
        tryVerify(function () { const p = popup("SchedulePopup"); return p && p.opened; }, 2000);
        const pop = popup("SchedulePopup");
        verify(pop.read("tomorrow 15:00").state === "ok");
        compare(pop.read("после релиза xyz").state, "unknown");
        compare(pop.read("нет").state, "clear");
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return !pop.opened; }, 2000);
        verify(sameDay(task().scheduledAt, tc.day), "Esc changes nothing");

        pop.openFor(["KCUR-1"], "scheduled");
        const input = byName("schedule-input", pop.contentItem);
        input.text = "tomorrow 15:00";
        verify(pop.parsed.state === "ok");
        const want = pop.parsed.when;
        keyClick(Qt.Key_Return);
        tryVerify(function () { return !pop.opened; }, 2000);
        tryVerify(function () { return task().scheduledAt.getHours() === 15 && sameDay(task().scheduledAt, want); }, 2000);
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () { return sameDay(task().scheduledAt, tc.day) && task().scheduledAt.getHours() === 10; }, 2000);

        // The deadline, then off again.
        pop.openFor(["KCUR-1"], "due");
        byName("schedule-input", pop.contentItem).text = "tomorrow";
        verify(pop.apply());
        tryVerify(function () { const t = task(); return t.dueAt && !isNaN(t.dueAt.getTime()); }, 2000);
        pop.openFor(["KCUR-1"], "due");
        byName("schedule-input", pop.contentItem).text = "no";
        verify(pop.apply());
        tryVerify(function () { const t = task(); return !t.dueAt || isNaN(t.dueAt.getTime()); }, 2000);
    }

    // Column order from the keyboard: Ctrl Shift L / H on a column header.
    function test_column_order_from_the_keyboard() {
        const v = openView("board");
        const ids = function () { return AppController.statuses.map(function (s) { return s.id; }).join(","); };
        const before = ids();
        const add = byName("column-add", v);
        verify(add !== null);
        add.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_L, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(function () { return ids() !== before; }, 2000);
        verify(!popup("EventLogDialog") || !popup("EventLogDialog").opened, "the key moved the column, not the log");
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () { return ids() === before; }, 2000);
    }

    // F6 goes round the regions and comes back; the region is framed.
    function test_f6_walks_the_regions() {
        openView("board");
        compare(tc.win._regionOf(tc.win.activeFocusItem), "content");
        const seen = [];
        for (let i = 0; i < 6; i++) {
            keyClick(Qt.Key_F6);
            const r = tc.win._regionOf(tc.win.activeFocusItem);
            seen.push(r);
            verify(byName("region-frame").visible, "the region is framed");
            if (r === "content") break;
        }
        verify(seen.indexOf("sidebar") >= 0, "F6 reaches the sidebar: " + seen);
        verify(seen.indexOf("header") >= 0, "F6 reaches the header: " + seen);
        compare(seen[seen.length - 1], "content", "F6 comes back to the content");
        keyClick(Qt.Key_F6, Qt.ShiftModifier);
        verify(tc.win._regionOf(tc.win.activeFocusItem) !== "content", "Shift F6 goes the other way");
        tc.win.focusActiveView();
        verify(!byName("region-frame").visible, "the frame goes with the focus");
    }

    // In the sidebar ↓ walks the rows; Enter on a section puts the keyboard
    // in the content, not in the header.
    function test_sidebar_arrows_and_section_focus() {
        openView("board");
        keyClick(Qt.Key_F6, Qt.ShiftModifier);
        compare(tc.win._regionOf(tc.win.activeFocusItem), "sidebar");
        const tasksRow = byName("sidebar-section-tasks");
        verify(find(tasksRow, function (it) { return it.activeFocus; }) !== null, "F6 lands on the open section");
        keyClick(Qt.Key_Up);
        const todayRow = byName("sidebar-section-today");
        verify(find(todayRow, function (it) { return it.activeFocus; }) !== null, "↑ walks the sidebar");
        keyClick(Qt.Key_Return);
        tryCompare(AppController, "currentSection", "today");
        tryVerify(function () { return tc.win._regionOf(tc.win.activeFocusItem) === "content"; }, 2000);
    }

    // From the start, the first card is at most two keys away.
    function test_first_card_is_near() {
        openView("board");
        const b = tc.win.activeViewItem();
        b.clearCursor();
        keyClick(Qt.Key_J);
        verify(b.cursorVisible && b.cursorTaskId.length > 0, "j puts the cursor on a card");
    }

    // IDIOT-CAL-3: z d zooms the week to the day, as z w and z m switch.
    function test_z_d_zooms_to_the_day() {
        openView("week");
        keyClick(Qt.Key_Z);
        keyClick(Qt.Key_D);
        tryCompare(AppController, "currentView", "day");
    }

    // IDIOT-TASKS-3: 1 on a selection is one bulk edit, one Ctrl+Z.
    function test_priority_on_a_selection_is_one_undo() {
        for (const id of ["KCUR-2", "KCUR-3"]) {
            const t = AppController.newTaskDraft("todo");
            t._isNew = true;
            t.id = id;
            t.title = "kcur bulk " + id;
            t.priority = "P3";
            AppController.saveTask(t);
            tc.tasks.push(id);
        }
        openView("board");
        AppController.clearSelection();
        AppController.toggleTaskSelection("KCUR-2");
        AppController.toggleTaskSelection("KCUR-3");
        keyClick(Qt.Key_1);
        tryVerify(function () {
            return AppController.taskById("KCUR-2").priority === "P0" && AppController.taskById("KCUR-3").priority === "P0";
        }, 2000);
        AppController.clearSelection();
        keyClick(Qt.Key_Z, Qt.ControlModifier);
        tryVerify(function () {
            return AppController.taskById("KCUR-2").priority === "P3" && AppController.taskById("KCUR-3").priority === "P3";
        }, 2000);
    }

    // IDIOT-CAL-14: Shift L on a series occurrence moves the cursor only once
    // the scope is answered; Esc leaves it where it was.
    function test_moving_a_series_occurrence_waits_for_the_scope() {
        const ev = AppController.newEventDraft(15, tc.day);
        ev.title = "kcur series";
        ev.date = tc.day;
        ev.start = 15;
        ev.end = 16;
        ev.rrule = "FREQ=DAILY;COUNT=4";
        AppController.saveEvent(ev);
        tc.events.push(ev.id);
        const v = openView("week");
        keyClick(Qt.Key_J);
        const items = v._dayItems(v.cursorDay);
        const at = items.findIndex(it => it.ev && it.ev.occ && it.ev.occ.title === "kcur series");
        verify(at >= 0);
        v.cursorKey = items[at].key;
        v._cursorIdx = at;
        const key = v.cursorKey;
        keyClick(Qt.Key_L, Qt.ShiftModifier);
        tryVerify(function () { return v.scopePrompt.opened; }, 2000);
        verify(sameDay(AppController.selectedDate, tc.day), "the day waits for the answer");
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return !v.scopePrompt.opened; }, 2000);
        verify(sameDay(AppController.selectedDate, tc.day));
        compare(v.cursorKey, key, "the cursor stays on the occurrence");
    }

    // Del on a repeating meeting (review 2026-10-19): Today had no menu for
    // a meeting, so Del did nothing at all. Today says why and what to press;
    // the week opens the meeting's own menu. The meeting stays either way.
    function test_del_on_a_repeating_meeting_says_why() {
        const ev = AppController.newEventDraft(15, tc.day);
        ev.title = "kcur del series";
        ev.date = tc.day;
        ev.start = 15;
        ev.end = 16;
        ev.rrule = "FREQ=DAILY;COUNT=4";
        AppController.saveEvent(ev);
        tc.events.push(ev.id);
        const onSeries = function () {
            const id = tc.win._cursorEventId();
            const e = id ? AppController.eventById(id) : null;
            return !!e && e.title === "kcur del series";
        };
        for (const view of ["today", "week"]) {
            const v = openView(view);
            for (let i = 0; i < 8 && !onSeries(); i++) keyClick(Qt.Key_J);
            verify(onSeries(), view + ": the cursor reached the series");
            const toast = byName("toast");
            keyClick(Qt.Key_Delete);
            if (view === "today") {
                tryVerify(function () { return toast.message === I18n.t("event.delSeries"); }, 2000, toast.message);
            } else {
                const menu = find(v, function (it) { return it.objectName === "week-event-menu"; })
                             || (function () { const d = v.data; for (let i = 0; i < d.length; i++) if (d[i] && d[i].objectName === "week-event-menu") return d[i]; return null; })();
                verify(menu !== null);
                tryVerify(function () { return menu.opened; }, 2000, "the week opens the meeting's menu");
                menu.close();
                tryVerify(function () { return !menu.visible; });
            }
            verify(AppController.eventById(ev.id).id === ev.id, view + ": the series is still there");
            closeAll();
        }
    }
}
