// heap. — month / custom-range calendar.
//
// A companion to WeekView: instead of an hour grid it lays out whole days as a
// 7-column calendar. Two modes:
//   • "month"  — the 6-week grid of the month containing selectedDate.
//   • "weeks"  — a custom span of N weeks starting at selectedDate's week.
// Each day cell shows its task deadlines + events as compact chips. Clicking a
// day selects it; clicking a chip opens the task / event editor.

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp
import "Segments.js" as Seg
import "Search.js" as Search
import "QueryWords.js" as QueryWords
import "Reschedule.js" as Resched
import "TaskDates.js" as TaskDates

Item {
    id: root

    // Wired by Main.qml (mirrors WeekView).
    property string searchText: ""
    property var prioritiesFilter: ({})
    property bool showArchived: false
    // The Calendar's layers (dogfood 2026-10-22), as WeekView reads them.
    property var layers: ({ meetings: true, timed: true, reminders: true, deadlines: true })
    signal taskClicked(string id)
    // The task menu (APP-268), one for the view, refilled per task.
    TaskMenuHost {
        id: viewTaskMenu
        anchorItem: root
        onOpenRequested: root.taskClicked(viewTaskMenu.taskId)
    }
    function openTaskMenu(id) {
        viewTaskMenu.releaseMenu();
        viewTaskMenu.taskId = id;
        viewTaskMenu.popup();
    }
    // The occurrence, not just its id: a repeating event is stored once, so
    // every occurrence of a series carries the master's id and only the
    // occurrence map says which date was clicked.
    signal eventClicked(string id, var occurrence)
    // Inside the calendar lens (APP-264): the header carries the range, the
    // tray the tasks without a date, and "+N more" zooms into the day.
    property bool chrome: true
    property string armedTaskId: ""
    signal armedUsed()
    // "сбросить фильтр · Esc" under a search that finds nothing (R4-100).
    signal resetFilterRequested()
    signal dayRequested(date day)
    // Heap 2 month: at most three things a day, then "+N more" (APP-264).
    readonly property int maxPerDay: root.chrome ? 99 : 3

    // View state.
    property string mode: "month"   // "month" | "weeks"
    property int weeksCount: 2       // used in "weeks" mode (1..8)

    // ── Helpers ──────────────────────────────────────────────────────
    function isSameDay(a, b) {
        if (!a || !b || !a.getFullYear || !b.getFullYear) return false;
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }
    function startOfWeek(d) {
        const dow = d.getDay();
        const sundayFirst = Theme.weekStart === "sun";
        const offset = sundayFirst ? -dow : (dow === 0 ? -6 : 1 - dow);
        return new Date(d.getFullYear(), d.getMonth(), d.getDate() + offset);
    }
    function priColor(p) {
        return p === "P0" ? Theme.p0 : p === "P1" ? Theme.p1 : p === "P2" ? Theme.p2 : Theme.p3;
    }
    function passesFilter(t) {
        // Any column of the Done kind, not only "done" (IDIOT-CAL-4).
        if (AppController.statusCategory(t.status) === "done") return false;
        // Clauses filter structurally, leftover words stay a substring test.
        if (!Search.accepts(AppController, root.searchText, root.taskRev + ":" + AppController.today, t)) return false;
        let any = false;
        for (const k in root.prioritiesFilter) if (root.prioritiesFilter[k]) { any = true; break; }
        if (any && !root.prioritiesFilter[t.priority]) return false;
        return true;
    }

    // A burst of model changes (a tracker sync) rebuilds once, not per row (APP-203).
    ChangeTick { id: taskTick }
    ChangeTick { id: eventTick }
    readonly property int taskRev: taskTick.rev
    readonly property int eventRev: eventTick.rev
    Connections {
        target: AppController.tasks
        function onDataChanged()  { taskTick.bump() }
        function onRowsInserted() { taskTick.bump() }
        function onRowsRemoved()  { taskTick.bump() }
        function onModelReset()   { taskTick.bump() }
    }
    Connections {
        target: AppController.events
        function onDataChanged()  { eventTick.bump() }
        function onRowsInserted() { eventTick.bump() }
        function onRowsRemoved()  { eventTick.bump() }
        function onModelReset()   { eventTick.bump() }
    }

    // Anchor month + visible range. Declarative: recompute on selectedDate /
    // mode / weeksCount / Theme.weekStart changes.
    readonly property date anchorDate: AppController.selectedDate
    // As many weeks as the month touches: five for October 2026 (DG-050),
    // never trailing a week of next month's days.
    readonly property int rows: {
        if (mode !== "month") return Math.max(1, Math.min(8, weeksCount));
        const first = new Date(anchorDate.getFullYear(), anchorDate.getMonth(), 1);
        const lead = Math.round((first - startOfWeek(first)) / 86400000);
        const len = new Date(anchorDate.getFullYear(), anchorDate.getMonth() + 1, 0).getDate();
        return Math.ceil((lead + len) / 7);
    }
    readonly property date gridStart: {
        if (mode === "month")
            return startOfWeek(new Date(anchorDate.getFullYear(), anchorDate.getMonth(), 1));
        return startOfWeek(anchorDate);
    }
    readonly property int anchorMonth: anchorDate.getMonth()

    function buildCells() {
        const _t = root.taskRev; const _e = root.eventRev;   // track for reactivity
        const start = gridStart;
        const n = rows * 7;
        const cells = [];
        for (let i = 0; i < n; i++) {
            const d = new Date(start.getFullYear(), start.getMonth(), start.getDate() + i);
            cells.push({ date: d, tasks: [], events: [] });
        }
        // C++ hands over only the tasks due or scheduled inside the grid, with
        // their cell already worked out: reading nine roles of every task and
        // scanning 42 cells per task is what made opening Month take seconds
        // on a large profile. A task planned for a day (scheduledAt) shows on
        // it too, not only one with a deadline there.
        const last = cells[n - 1].date;
        const list = AppController.calendarTasks(start, last, root.showArchived);
        const showTimed = root.layers.timed !== false;
        const showDue = root.layers.deadlines !== false;
        const showRemind = root.layers.reminders !== false;
        for (let i = 0; i < list.length; i++) {
            const t = list[i];
            if (!root.passesFilter(t)) continue;
            if (showDue && t.dueDay >= 0 && t.dueDay < n) cells[t.dueDay].tasks.push(t);
            if (showTimed && t.schedDay >= 0 && t.schedDay < n && (t.schedDay !== t.dueDay || !showDue))
                cells[t.schedDay].tasks.push(Object.assign({ scheduled: true }, t));
            // A reminder to self on its day, with a bell (dogfood 2026-10-22).
            if (showRemind && t.reminder && t.remindDay >= 0 && t.remindDay < n)
                cells[t.remindDay].tasks.push(Object.assign({ remindChip: true }, t));
        }
        // The expansion, not the rows: a repeating event is stored once, and
        // a month is the view most likely to be showing a whole series at
        // once. A multi-day event is listed on every day it covers, which is
        // what a month grid is for — found by arithmetic, not by testing every
        // occurrence against every cell.
        const occ = root.layers.meetings === false ? [] : AppController.eventOccurrences(cells[0].date, last);
        const first = Date.UTC(start.getFullYear(), start.getMonth(), start.getDate());
        const dayOf = (d) => Math.round((Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()) - first) / 86400000);
        for (let i = 0; i < occ.length; i++) {
            const e = occ[i];
            if (!e.date || !e.date.getFullYear) continue;
            const a = Math.max(0, dayOf(e.date));
            const endD = (e.endDate && e.endDate.getFullYear) ? e.endDate : e.date;
            const b = Math.min(n - 1, dayOf(endD));
            for (let k = a; k <= b; k++) cells[k].events.push(e);
        }
        const priRank = { P0: 0, P1: 1, P2: 2, P3: 3 };
        // Events in time order, all-day first: the 16:00 meeting used to be
        // the one shown and the 09:00 and 12:00 ones hidden behind "+2".
        const evKey = (e) => e.allDay ? -1 : e.start;
        for (let k = 0; k < cells.length; k++) {
            cells[k].tasks.sort((a, b) => (priRank[a.priority] ?? 9) - (priRank[b.priority] ?? 9));
            cells[k].events.sort((a, b) => evKey(a) - evKey(b));
        }
        // A2-25: the occurrences ahead of a repeating task, from its rule,
        // faint and read-only after the day's real tasks. Nothing is created.
        const ghosts = showTimed ? AppController.calendarRepeats(start, last) : [];
        for (let i = 0; i < ghosts.length; i++) {
            const g = ghosts[i];
            if (g.day >= 0 && g.day < n && root.passesFilter(g)) cells[g.day].tasks.push(g);
        }
        return cells;
    }
    readonly property var cells: buildCells()
    // What a cell reads for the moment between the grid shrinking (month to
    // weeks) and the Repeater dropping the cells past the new end.
    readonly property var _noCell: ({ date: new Date(0), tasks: [], events: [] })
    // Nothing dated in the whole grid: the empty state says what lands here.
    readonly property bool monthEmpty: cells.every(c => c.tasks.length === 0 && c.events.length === 0)

    // Day-of-month held across a month step, clamped to the target month's own
    // length. A flat clamp to 28 was safe against JS Date overflow (Feb 31
    // rolls into March) but lossy in one direction only: stepping off the 31st
    // landed on the 28th and every step after that stayed there, so a month of
    // paging left the selection three days adrift.
    function _sameDayNextMonth(d, dir, day) {
        const y = d.getFullYear();
        const m = d.getMonth() + dir;
        const lastDay = new Date(y, m + 1, 0).getDate();   // day 0 = last of month m
        return new Date(y, m, Math.min(day, lastDay));
    }

    // The day of the month paging keeps to. Stepping from the 31st lands on
    // Feb 28 and then must go back to the 31st in March, not stay on the 28th
    // — it is remembered until the selection is moved some other way.
    property int _stickyDay: 0
    property var _steppedTo: null
    function step(dir) {
        const d = AppController.selectedDate;
        if (mode === "month") {
            const kept = root._steppedTo && root.isSameDay(root._steppedTo, d) && root._stickyDay > 0;
            const day = kept ? root._stickyDay : d.getDate();
            root._stickyDay = day;
            const next = _sameDayNextMonth(d, dir, day);
            root._steppedTo = next;
            AppController.selectedDate = next;
        } else {
            AppController.selectedDate = new Date(d.getFullYear(), d.getMonth(), d.getDate() + dir * rows * 7);
        }
    }
    // What ‹ › say they do: a month, or the run of weeks on screen.
    function navLabel(dir) {
        if (root.mode === "month") return I18n.t(dir < 0 ? "month.prev" : "month.next");
        return I18n.t(dir < 0 ? "month.prevWeeks" : "month.nextWeeks");
    }
    function rangeTitle() {
        if (mode === "month")
            return I18n.monthName(anchorDate.getMonth()) + " " + anchorDate.getFullYear();
        const end = new Date(gridStart.getFullYear(), gridStart.getMonth(), gridStart.getDate() + rows * 7 - 1);
        return I18n.fmtDate(gridStart, "dayMonth") + " – " + I18n.fmtDate(end, "dayMonthYear");
    }

    // A day as a screen reader hears it: "Wednesday 30 September: 3 tasks,
    // 7 events".
    function dayLabel(d) {
        if (!d || !d.getFullYear) return "";
        let tasks = 0, events = 0;
        for (let i = 0; i < root.cells.length; i++) {
            if (!root.isSameDay(root.cells[i].date, d)) continue;
            tasks = root.cells[i].tasks.filter(t => !t.ghost).length;
            events = root.cells[i].events.length;
            break;
        }
        return I18n.t("month.dayA11y").arg(I18n.fmtDate(d, "longWeekday")).arg(tasks).arg(events);
    }
    // Return on the grid: the first chip of the selected day takes the
    // keyboard, if the day has any.
    function enterSelectedDay() {
        for (let i = 0; i < cellRep.count; i++) {
            const cell = cellRep.itemAt(i);
            if (!cell || !root.isSameDay(cell.cell.date, AppController.selectedDate)) continue;
            const stop = root._firstTabStop(cell);
            if (stop) stop.forceActiveFocus(Qt.TabFocusReason);
            return;
        }
    }
    function _firstTabStop(it) {
        const kids = it.children || [];
        for (let i = 0; i < kids.length; i++) {
            const k = kids[i];
            if (!k.visible) continue;
            if (k.activeFocusOnTab === true) return k;
            const r = root._firstTabStop(k);
            if (r) return r;
        }
        return null;
    }

    // ── Moving a task to another day (APP-249) ───────────────────────
    // A task chip dropped on a day plans the task for that day (scheduledAt),
    // at the time it was planned for if it had one. Its deadline stays where
    // it is. The hint at the pointer says what a drop would set; Esc cancels.
    // { id, scheduledAt, timed } of the chip being carried.
    property var drag: null
    property int dropIndex: -1

    function cellIndexAt(x, y) {
        // `x`, `y` in this view's coordinates.
        for (let i = 0; i < cellRep.count; i++) {
            const c = cellRep.itemAt(i);
            if (!c) continue;
            const p = c.mapFromItem(root, x, y);
            if (p.x >= 0 && p.y >= 0 && p.x < c.width && p.y < c.height) return i;
        }
        return -1;
    }
    function landing(index) {
        if (!root.drag || index < 0 || index >= root.cells.length) return null;
        return Resched.dropOnDay(root.cells[index].date, root.drag.scheduledAt, root.drag.timed);
    }
    function beginDrag(t) {
        const full = AppController.taskById(t.id);
        root.drag = { id: t.id, scheduledAt: full.scheduledAt, timed: !!full.scheduledHasTime };
        root.dropIndex = -1;
        dragLayer.begin(t.id, t.title, true);
    }
    function moveDrag(x, y) {
        if (!root.drag) return;
        root.dropIndex = root.cellIndexAt(x, y);
        const land = root.landing(root.dropIndex);
        dragLayer.update(x, y, land ? dragLayer.describe("scheduled", land.when, land.timed) : I18n.t("drag.notHere"), !!land);
    }
    function endDrag() {
        const d = root.drag;
        const land = root.landing(root.dropIndex);
        root.drag = null;
        root.dropIndex = -1;
        dragLayer.finish();
        if (!d || !land) return false;
        return AppController.rescheduleTask(d.id, "scheduled", land.when, land.timed);
    }

    // The task the move keys act on (Ctrl+←/→ a day, with Shift a week): the
    // chip that has the keyboard, else the one under the pointer. Kept after
    // a move rebuilds the grid, so the next press moves it again.
    property string keyTaskId: ""
    property var keyTaskDay: null
    property string hoverTaskId: ""
    property var hoverTaskDay: null
    // What a repeat ahead says on hover and to a reader (A2-25).
    function ghostTip(g) {
        return I18n.t("cal.ghost.tip").arg(g.title).arg(TaskDates.repeatText(g.recurrence));
    }
    function moveKeyTaskByDays(days) {
        const it = root._cursorItem();
        if (it && it.kind === "event") return root._moveCursorEvent(days);
        if (it && it.kind === "ghost") return false;   // a repeat ahead is not a task
        const id = it ? it.id : (root.keyTaskId || root.hoverTaskId);
        if (!id) return false;
        const t = AppController.taskById(id);
        if (!t || !t.id) return false;
        const day = it ? AppController.selectedDate : root.keyTaskId ? root.keyTaskDay : root.hoverTaskDay;
        const r = Resched.shiftByDays(t.scheduledAt, t.scheduledHasTime, days, day, AppController.today);
        const ok = AppController.rescheduleTask(id, "scheduled", r.when, r.timed);
        if (ok && it) root._shiftDay(days);
        return ok;
    }
    function moveKeyTaskByTime(steps) {
        const it = root._cursorItem();
        if (it && (it.kind === "event" || it.kind === "ghost")) return false;
        const id = it ? it.id : (root.keyTaskId || root.hoverTaskId);
        const t = id ? AppController.taskById(id) : null;
        const r = t ? Resched.shiftByTime(t.scheduledAt, t.scheduledHasTime, steps, Theme.snapMinutes) : null;
        return r ? AppController.rescheduleTask(id, "scheduled", r.when, true) : false;
    }

    // ── The keyboard cursor (APP-276) ────────────────────────────────
    // The day it is on is the selected date; j / k walk that day's tasks and
    // meetings and go on to the week below / above past the last / first;
    // h / l are the day beside it. Held by key, so a move or a sync leaves
    // it on the same thing, or on its neighbour when that is gone.
    property bool cursorVisible: false
    property string cursorKey: ""
    property int _cursorIdx: 0
    readonly property bool cardMenuOpen: false
    readonly property int _selCell: {
        const c = root.cells;
        for (let i = 0; i < c.length; i++)
            if (root.isSameDay(c[i].date, AppController.selectedDate)) return i;
        return -1;
    }
    function _cellItems(ci) {
        if (ci < 0 || ci >= root.cells.length) return [];
        const cell = root.cells[ci];
        const out = [];
        for (let i = 0; i < cell.tasks.length; i++) {
            // A repeat ahead (A2-25): Return opens the task; nothing moves it.
            const ghost = !!cell.tasks[i].ghost;
            out.push({ kind: ghost ? "ghost" : "task", id: cell.tasks[i].id,
                       key: (ghost ? "ghost:" : "task:") + cell.tasks[i].id, index: i });
        }
        for (let i = 0; i < cell.events.length; i++) {
            const e = cell.events[i];
            out.push({ kind: "event", id: e.id, key: "event:" + e.id + (e.masterId ? "@" + Number(e.occurrenceDate || e.date) : ""),
                       ev: e, index: i });
        }
        return out;
    }
    function _cursorIndex() {
        const items = root._cellItems(root._selCell);
        for (let i = 0; i < items.length; i++)
            if (items[i].key === root.cursorKey) return i;
        return -1;
    }
    function _cursorItem() {
        if (!root.cursorVisible || root.cursorKey === "") return null;
        const items = root._cellItems(root._selCell);
        for (let i = 0; i < items.length; i++)
            if (items[i].key === root.cursorKey) return items[i];
        return null;
    }
    readonly property string cursorTaskId: {
        const it = root._cursorItem();
        return it && it.kind === "task" ? it.id : "";
    }
    function clearCursor() {
        root.cursorVisible = false;
        root.cursorKey = "";
    }
    function _placeIdx(i) {
        root.cursorVisible = true;
        const items = root._cellItems(root._selCell);
        if (items.length === 0) {
            root.cursorKey = "";
            root._cursorIdx = 0;
            return;
        }
        const at = Math.max(0, Math.min(items.length - 1, i));
        root.cursorKey = items[at].key;
        root._cursorIdx = at;
        if (items[at].kind === "task") AppController.markTaskSeen(items[at].id);
    }
    function _shiftDay(n) {
        const d = AppController.selectedDate;
        AppController.selectedDate = new Date(d.getFullYear(), d.getMonth(), d.getDate() + n);
    }
    function moveCursor(dx, dy) {
        const items = root._cellItems(root._selCell);
        if (!root.cursorVisible && dy !== 0 && items.length > 0) {
            root._placeIdx(dy > 0 ? 0 : items.length - 1);
            return;
        }
        root.cursorVisible = true;
        if (dx !== 0) {
            root._shiftDay(dx);
            Qt.callLater(root._placeIdx, 0);
            return;
        }
        if (dy === 0) { root._placeIdx(Math.max(0, root._cursorIndex())); return; }
        const at = root._cursorIndex();
        const next = at < 0 ? (dy > 0 ? 0 : items.length - 1) : at + dy;
        if (Math.abs(dy) === 1 && (items.length === 0 || (at >= 0 && (next < 0 || next >= items.length)))) {
            root._shiftDay(dy * 7);
            Qt.callLater(root._placeIdx, dy > 0 ? 0 : 100000);
            return;
        }
        root._placeIdx(next);
    }
    function _reconcile() {
        if (!root.cursorVisible || root._cursorIndex() >= 0) return;
        root._placeIdx(root._cursorIdx);
    }
    onCellsChanged: if (root.cursorVisible) Qt.callLater(root._reconcile)
    Connections {
        target: AppController
        function onSelectedDateChanged() { if (root.cursorVisible) Qt.callLater(root._reconcile); }
    }
    function _actionCardId() {
        const it = root._cursorItem();
        if (it) return it.kind === "task" ? it.id : "";
        if (AppController.selectionCount === 1) return AppController.selectedTaskIds[0];
        return root.hoverTaskId || "";
    }
    function openCursor() {
        const it = root._cursorItem();
        if (!it) { root.enterSelectedDay(); return; }
        if (it.kind === "task" || it.kind === "ghost") root.taskClicked(it.id);
        else root.eventClicked(it.id, it.ev);
    }
    function toggleCursorSelection() {
        const it = root._cursorItem();
        if (it && it.kind === "task") AppController.toggleTaskSelection(it.id);
        else if (!root.cursorVisible) root.moveCursor(0, 1);
    }
    function openCursorMenu() {
        const id = root._actionCardId();
        if (id) root.openTaskMenu(id);
    }
    function archiveCursor() {
        if (AppController.selectionCount > 0) { AppController.setSelectedTasksArchived(true); return; }
        const id = root._actionCardId();
        if (id) AppController.setArchived(id, true);
    }
    // Shift H / L a day, Shift J / K a week: the grid's own directions.
    function moveSelectionOrCard(dx) { root.moveKeyTaskByDays(dx); }
    function moveCursorCard(dx, dy) { if (dy !== 0) root.moveKeyTaskByDays(dy * 7); }
    SeriesScopeDialog { id: scopeAsk }
    function _moveCursorEvent(days) {
        const it = root._cursorItem();
        if (!it || it.kind !== "event" || days === 0) return false;
        const occ = it.ev;
        // The day changes once the scope is answered; Esc leaves the cursor
        // where it was (IDIOT-CAL-14).
        if (String(occ.masterId || "").length > 0) {
            scopeAsk.ask("move", (scope) => {
                AppController.moveOccurrence(occ, days * 24, scope);
                root._shiftDay(days);
            }, null);
        } else {
            AppController.moveOccurrence(occ, days * 24, "this");
            root._shiftDay(days);
        }
        return true;
    }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp2xl
        spacing: Theme.spLg

        // ── Header ────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            visible: root.chrome
            spacing: Theme.spMd

            // prev / next
            Repeater {
                model: [{ g: "‹", d: -1, k: "cal.prev" }, { g: "›", d: 1, k: "cal.next" }]
                delegate: Rectangle {
                    id: navBtn
                    required property var modelData
                    objectName: navBtn.modelData.d < 0 ? "month-prev" : "month-next"
                    width: 28; height: 28; radius: Theme.radiusMd
                    color: navMA.hovered ? Theme.panel3 : Theme.panel2
                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    border.color: Theme.border; border.width: 1
                    Text { anchors.centerIn: parent; text: navBtn.modelData.g; color: Theme.text; font.pixelSize: Theme.fsLg }
                    ClickArea {
                        id: navMA
                        label: root.navLabel(navBtn.modelData.d)
                        shortcutId: navBtn.modelData.k
                        onActivated: root.step(navBtn.modelData.d)
                    }
                }
            }

            Text {
                text: root.rangeTitle()
                color: Theme.text; font.pixelSize: Theme.fsLg; font.weight: Theme.fwHeading
                Layout.preferredWidth: 240
            }

            Item { Layout.fillWidth: true }

            // Mode toggle: Month | Weeks
            Row {
                spacing: 0
                Repeater {
                    model: [{ id: "month", label: I18n.t("cal.month") }, { id: "weeks", label: I18n.t("cal.weeks") }]
                    delegate: Rectangle {
                        id: modeBtn
                        required property var modelData
                        objectName: "month-mode-" + modelData.id
                        readonly property bool sel: root.mode === modelData.id
                        width: 76; height: 28
                        radius: Theme.radiusMd
                        // A selected segment is tinted, not filled: a filled
                        // accent is the screen's one action ("+ Task").
                        color: sel ? Theme.accentSoft : (modeMA.hovered ? Theme.panel3 : Theme.panel2)
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        border.color: sel ? Theme.withAlpha(Theme.accent, 0.5) : Theme.border; border.width: 1
                        Text { anchors.centerIn: parent; text: parent.modelData.label; color: parent.sel ? Theme.accentStrong : Theme.text; font.pixelSize: Theme.fsSm; font.weight: parent.sel ? Theme.fwTitle : Theme.fwBody }
                        ClickArea {
                            id: modeMA
                            label: modeBtn.modelData.label
                            showTip: false
                            role: Accessible.RadioButton
                            checkable: true
                            checked: modeBtn.sel
                            onActivated: root.mode = modeBtn.modelData.id
                        }
                    }
                }
            }

            // Weeks stepper (custom range) — only in "weeks" mode.
            Row {
                spacing: Theme.spXs
                visible: root.mode === "weeks"
                Rectangle {
                    width: 24; height: 28; radius: Theme.radiusMd; color: decMA.hovered ? Theme.panel3 : Theme.panel2; border.color: Theme.border; border.width: 1
                    Text { anchors.centerIn: parent; text: "−"; color: Theme.text; font.pixelSize: Theme.fsLg }
                    ClickArea { id: decMA; objectName: "month-fewer-weeks"; label: I18n.t("month.fewerWeeks"); onActivated: root.weeksCount = Math.max(1, root.weeksCount - 1) }
                }
                Rectangle {
                    width: 52; height: 28; radius: Theme.radiusMd; color: Theme.panel2; border.color: Theme.border; border.width: 1
                    Text { anchors.centerIn: parent; text: root.weeksCount + " " + I18n.t("cal.wk"); color: Theme.text; font.pixelSize: Theme.fsSm; font.family: Theme.fontUi; font.features: Theme.tabularNums }
                }
                Rectangle {
                    width: 24; height: 28; radius: Theme.radiusMd; color: incMA.hovered ? Theme.panel3 : Theme.panel2; border.color: Theme.border; border.width: 1
                    Text { anchors.centerIn: parent; text: "+"; color: Theme.text; font.pixelSize: Theme.fsLg }
                    ClickArea { id: incMA; objectName: "month-more-weeks"; label: I18n.t("month.moreWeeks"); onActivated: root.weeksCount = Math.min(8, root.weeksCount + 1) }
                }
            }

            Rectangle {
                width: 60; height: 28; radius: Theme.radiusMd
                color: todayMA.hovered ? Theme.panel3 : Theme.panel2
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                border.color: Theme.border; border.width: 1
                Text { anchors.centerIn: parent; text: I18n.t("common.today"); color: Theme.text; font.pixelSize: Theme.fsSm }
                // AppController.today, like every other "today" in the app —
                // a raw new Date() carries a time-of-day with it.
                ClickArea {
                    id: todayMA
                    objectName: "month-today"
                    label: I18n.t("common.today")
                    shortcutId: "cal.today"
                    onActivated: AppController.selectedDate = AppController.today
                }
            }
        }

        // ── Weekday header ────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: -Theme.spLg
            spacing: 0
            Repeater {
                model: 7
                delegate: Text {
                    required property int index
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    leftPadding: Theme.spMd
                    topPadding: Theme.spSm
                    bottomPadding: Theme.spSm
                    horizontalAlignment: Text.AlignLeft
                    // App language, not the system locale: MonthView used to say
                    // "Mon" while the MiniWeek right next to it said "ПН".
                    text: I18n.dayName(new Date(root.gridStart.getFullYear(),
                                                root.gridStart.getMonth(),
                                                root.gridStart.getDate() + index).getDay())
                    color: Theme.textDim; font.pixelSize: Theme.fsXs; font.weight: Theme.fwBody
                    // The grid's hairlines run through the header too.
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
                    Rectangle { visible: parent.index === 0; anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border }
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
                }
            }
        }

        // ── Day grid ──────────────────────────────────────────────
        // One Tab stop for the whole grid (SHELL-17): the arrows move the
        // selected day (a day / a week), Return takes the keyboard to that
        // day's first chip, from where Tab walks its chips and "+N". The
        // arrows are claimed before the window's Left/Right month paging sees
        // them; T, G and Alt+arrows still work here (viewSurface).
        GridLayout {
            id: grid
            objectName: "month-grid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 7
            activeFocusOnTab: true
            readonly property bool viewSurface: true
            Accessible.role: Accessible.Table
            Accessible.name: root.rangeTitle()
            Accessible.description: root.dayLabel(AppController.selectedDate)
            Keys.onShortcutOverride: (event) => {
                if (event.modifiers === Qt.NoModifier || event.modifiers === Qt.KeypadModifier) {
                    const k = event.key;
                    if (k === Qt.Key_Left || k === Qt.Key_Right || k === Qt.Key_Up || k === Qt.Key_Down
                        || k === Qt.Key_Return || k === Qt.Key_Enter)
                        event.accepted = true;
                }
            }
            Keys.onPressed: (event) => {
                if (event.modifiers !== Qt.NoModifier && event.modifiers !== Qt.KeypadModifier) return;
                const k = event.key;
                // The arrows are h j k l here too (APP-276).
                const dx = k === Qt.Key_Left ? -1 : k === Qt.Key_Right ? 1 : 0;
                const dy = k === Qt.Key_Up ? -1 : k === Qt.Key_Down ? 1 : 0;
                if (dx !== 0 || dy !== 0) {
                    root.moveCursor(dx, dy);
                    event.accepted = true;
                } else if (k === Qt.Key_Return || k === Qt.Key_Enter) {
                    root.openCursor();
                    event.accepted = true;
                }
            }
            // A flat hairline grid (DG-050, X-Oth-DayMonth): no gaps, no cards.
            rowSpacing: 0
            columnSpacing: 0
            Repeater {
                id: cellRep
                // By position, not by the cells array: a new array on every
                // task change re-created all 42 cells and their chips, most of
                // a 200 ms rebuild on a 2k-task profile (APP-203). The cells
                // stay; what they show is rebound.
                model: root.cells.length
                delegate: Rectangle {
                    id: dayCell
                    required property int index
                    objectName: "month-cell-" + dayCell.index
                    // Named so the nested chip Repeaters (whose own `index` is
                    // their row) can still read the cell.
                    readonly property var cell: root.cells[dayCell.index] || root._noCell
                    readonly property bool _inMonth: root.mode !== "month" || dayCell.cell.date.getMonth() === root.anchorMonth
                    readonly property bool _today: root.isSameDay(dayCell.cell.date, AppController.today)
                    readonly property bool _sel: root.isSameDay(dayCell.cell.date, AppController.selectedDate)
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 1
                    // Today is tinted; bold adds the orange line on top.
                    color: _today ? Theme.withAlpha(Theme.text, 0.025) : "transparent"
                    readonly property bool _dropHere: root.drag !== null && root.dropIndex === dayCell.index
                    // Hairlines: right and bottom per cell, left and top on
                    // the first column and row.
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                    Rectangle { visible: dayCell.index % 7 === 0; width: 1; height: parent.height; color: Theme.border }
                    Rectangle {
                        objectName: "month-today-bar"
                        visible: dayCell._today && Style.fills
                        width: parent.width; height: 2
                        color: Theme.signalNow
                    }
                    Rectangle {
                        visible: dayCell._dropHere
                        anchors.fill: parent
                        color: "transparent"
                        border.color: Theme.accent
                        border.width: 2
                    }
                    Accessible.role: Accessible.Cell
                    Accessible.name: root.dayLabel(dayCell.cell.date)
                    Accessible.selected: _sel
                    // The keyboard's day, while the grid has the keyboard.
                    FocusRing {
                        objectName: "month-cell-cursor"
                        anchors.margins: 0
                        visible: dayCell._sel && (grid.activeFocus || (root.cursorVisible && (root.cursorKey === ""
                                 || root._cursorIdx >= dayCell._tasksShown && root._cursorIdx < dayCell.cell.tasks.length
                                 || root._cursorIdx >= dayCell.cell.tasks.length + dayCell._eventsShown)))
                    }
                    // A busy day is clipped to its cell instead of drawing
                    // over the row below.
                    clip: true

                    // How many chips fit, worked out from the cell's own
                    // height. A fixed 3 tasks + 2 events overflowed a 112px
                    // cell, and the clip took the "+N" - the busiest day was
                    // the one that said nothing about what it hid (VISU-12).
                    // When not everything fits, a line is kept for "+N".
                    readonly property int _rowH: Theme.px(18) + Theme.sp2xs
                    readonly property int _avail: height - 2 * Theme.spXs - dayNum.implicitHeight - Theme.sp2xs
                    readonly property int _total: cell.tasks.length + cell.events.length
                    readonly property int _slots: Math.min(root.maxPerDay < _total ? root.maxPerDay : _total,
                        _total * _rowH <= _avail
                        ? _total
                        : Math.max(0, Math.floor((_avail - moreText.implicitHeight - Theme.sp2xs) / _rowH)))
                    // Events keep up to half the slots, the tasks take the rest
                    // (3 + 2 on a cell with room for five, as before).
                    readonly property int _eventsShown: Math.min(cell.events.length,
                                                                 Math.max(_slots - cell.tasks.length, Math.floor(_slots / 2)))
                    // Never below zero: a kept cell rebinding to new data can
                    // read _slots and _eventsShown a step apart for a moment.
                    readonly property int _tasksShown: Math.max(0, Math.min(cell.tasks.length, _slots - _eventsShown))

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // A task picked in the tray is planned for this day.
                            if (root.armedTaskId) {
                                AppController.rescheduleTask(root.armedTaskId, "scheduled", dayCell.cell.date, false);
                                root.armedUsed();
                                return;
                            }
                            AppController.selectedDate = dayCell.cell.date;
                        }
                    }
                    // A task dragged from the tray lands on the day, as a date.
                    DropArea {
                        objectName: "month-drop-" + dayCell.index
                        anchors.fill: parent
                        onDropped: (drop) => {
                            const src = drop.source;
                            if (!src || !src.taskId) return;
                            AppController.rescheduleTask(String(src.taskId), "scheduled", dayCell.cell.date, false);
                            drop.accept(Qt.MoveAction);
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spMd
                        anchors.rightMargin: Theme.spMd
                        anchors.topMargin: Theme.spSm
                        anchors.bottomMargin: Theme.spXs
                        spacing: Theme.sp2xs

                        // Day number: grey, today bright (orange in bold),
                        // the neighbouring months faint.
                        Text {
                            id: dayNum
                            objectName: "month-day-number"
                            text: cell.date.getDate()
                            color: _today ? (Style.urgency ? Theme.signalNow : Theme.text)
                                 : _inMonth ? Theme.textDim : Theme.withAlpha(Theme.textDim, 0.4)
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            font.pixelSize: Theme.fsSm
                            font.weight: _today ? Theme.fwHeading : Theme.fwBody
                        }

                        // Chips — first few tasks, then events, then overflow.
                        // 20px, like the week's chips: at 15px a row of them
                        // was the hardest target in the app to hit (design
                        // audit DES-17).
                        Repeater {
                            model: dayCell._tasksShown
                            delegate: Rectangle {
                                id: taskChip
                                required property int index
                                FocusRing {
                                    objectName: "month-cursor"
                                    visible: root.cursorVisible && dayCell._sel
                                             && root.cursorKey === (taskChip.ghost ? "ghost:" : "task:") + dayCell.cell.tasks[taskChip.index].id
                                }
                                // A repeat ahead (A2-25): faint, read-only.
                                readonly property bool ghost: !!dayCell.cell.tasks[taskChip.index].ghost
                                Layout.fillWidth: true
                                implicitHeight: Theme.px(18)
                                radius: Theme.radiusXs
                                // A line with its sign, not a coloured box
                                // (DG-050): a flag for a deadline, the status
                                // ring for a planned day.
                                color: chipMA.containsMouse ? Theme.panel2 : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                Row {
                                    anchors.fill: parent; spacing: Theme.spXs
                                    Item {
                                        id: taskSign
                                        width: Theme.px(10); height: parent.height
                                        Icon {
                                            anchors.centerIn: parent
                                            visible: !dayCell.cell.tasks[taskChip.index].scheduled && !taskChip.ghost
                                                     && !dayCell.cell.tasks[taskChip.index].remindChip
                                            name: "flag"
                                            size: Theme.px(10)
                                            color: Theme.textDim
                                        }
                                        Icon {
                                            objectName: "month-reminder-bell"
                                            anchors.centerIn: parent
                                            visible: !!dayCell.cell.tasks[taskChip.index].remindChip
                                            name: "bell"
                                            size: Theme.px(10)
                                            color: Theme.warning
                                        }
                                        StatusRing {
                                            anchors.centerIn: parent
                                            visible: !!dayCell.cell.tasks[taskChip.index].scheduled && !taskChip.ghost
                                            category: AppController.statusCategory(dayCell.cell.tasks[taskChip.index].status || "")
                                            size: Theme.px(9)
                                        }
                                        // The repeat mark the card and Today use.
                                        Text {
                                            anchors.centerIn: parent
                                            visible: taskChip.ghost
                                            text: "↻"
                                            color: Theme.textDim
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsXs
                                        }
                                    }
                                    Text {
                                        objectName: taskChip.ghost ? "month-ghost-title" : "month-task-title"
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - taskSign.width - Theme.spXs
                                        elide: Text.ElideRight
                                        text: {
                                            const t = dayCell.cell.tasks[taskChip.index];
                                            return t.remindChip && t.remindHour >= 0 ? Theme.fmtHour(t.remindHour) + " " + t.title : t.title;
                                        }
                                        color: taskChip.ghost ? Theme.textDim : Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsXs
                                    }
                                }
                                readonly property var task: dayCell.cell.tasks[taskChip.index]
                                objectName: (taskChip.ghost ? "month-ghost-" : "month-task-") + taskChip.task.id
                                // The keyboard's way in; deaf to the pointer, which
                                // the drag area below takes.
                                ClickArea {
                                    label: taskChip.ghost ? root.ghostTip(taskChip.task) : taskChip.task.title
                                    showTip: false
                                    acceptedButtons: Qt.NoButton
                                    onActivated: root.taskClicked(taskChip.task.id)
                                    onActiveFocusChanged: if (activeFocus && !taskChip.ghost) {
                                        root.keyTaskId = taskChip.task.id;
                                        root.keyTaskDay = dayCell.cell.date;
                                    }
                                }
                                // A click opens it; a drag takes it to another day
                                // (APP-249).
                                // The task's menu, the same in every view (APP-268).
                                TapHandler {
                                    enabled: !taskChip.ghost
                                    acceptedButtons: Qt.RightButton
                                    onTapped: root.openTaskMenu(taskChip.task.id)
                                }
                                MouseArea {
                                    id: chipMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    preventStealing: true
                                    cursorShape: root.drag ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                    property real pressX: 0
                                    property real pressY: 0
                                    // Esc ended this press's drag.
                                    property bool inert: false
                                    onPressed: (mouse) => { pressX = mouse.x; pressY = mouse.y; inert = false; }
                                    onPositionChanged: (mouse) => {
                                        // A reminder's day is its own moment, not a plan: not dragged.
                                        if (!pressed || inert || taskChip.ghost || !!taskChip.task.remindChip) return;
                                        if (!root.drag) {
                                            if (Math.abs(mouse.x - pressX) < 6 && Math.abs(mouse.y - pressY) < 6) return;
                                            root.beginDrag(taskChip.task);
                                        }
                                        const p = chipMA.mapToItem(root, mouse.x, mouse.y);
                                        root.moveDrag(p.x, p.y);
                                    }
                                    onReleased: if (root.drag) { inert = true; root.endDrag(); }
                                    onCanceled: if (root.drag) { root.drag = null; root.dropIndex = -1; dragLayer.finish(); }
                                    onClicked: {
                                        if (inert) { inert = false; return; }
                                        root.taskClicked(taskChip.task.id);
                                    }
                                    onContainsMouseChanged: {
                                        if (taskChip.ghost) return;
                                        if (containsMouse) { root.hoverTaskId = taskChip.task.id; root.hoverTaskDay = dayCell.cell.date; }
                                        else if (root.hoverTaskId === taskChip.task.id) root.hoverTaskId = "";
                                    }
                                    ToolTip.visible: taskChip.ghost && containsMouse
                                    ToolTip.delay: 400
                                    ToolTip.text: taskChip.ghost ? root.ghostTip(taskChip.task) : ""
                                    Connections {
                                        target: dragLayer
                                        function onCanceled() { if (chipMA.pressed) chipMA.inert = true; }
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: dayCell._eventsShown
                            delegate: Rectangle {
                                id: eventChip
                                required property int index
                                FocusRing {
                                    objectName: "month-cursor"
                                    visible: root.cursorVisible && dayCell._sel && root._cursorIdx === dayCell.cell.tasks.length + eventChip.index
                                             && root.cursorKey.indexOf("event:" + dayCell.cell.events[eventChip.index].id) === 0
                                    // The meeting's time also for the keyboard
                                    // cursor, not only on hover (DG-050).
                                    ToolTip.visible: visible && evCA.tip.length > 0
                                    ToolTip.delay: 300
                                    ToolTip.text: evCA.tip
                                }
                                Layout.fillWidth: true
                                implicitHeight: Theme.px(18)
                                radius: Theme.radiusXs
                                color: evCA.hovered ? Theme.panel2 : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                // A meeting is the calendar glyph and its
                                // title (DG-050); the time is in the tooltip.
                                Row {
                                    anchors.fill: parent; spacing: Theme.spXs
                                    Item {
                                        id: evSign
                                        width: Theme.px(10); height: parent.height
                                        MeetingIcon { anchors.centerIn: parent; size: Theme.px(10); ink: Theme.textDim }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter; width: parent.width - evSign.width - Theme.spXs; elide: Text.ElideRight
                                        text: dayCell.cell.events[eventChip.index].title
                                        color: Theme.textMuted; font.family: Theme.fontUi; font.pixelSize: Theme.fsXs
                                    }
                                }
                                ClickArea {
                                    id: evCA
                                    label: dayCell.cell.events[eventChip.index].title
                                    tip: dayCell.cell.events[eventChip.index].allDay ? ""
                                         : Theme.fmtHour(dayCell.cell.events[eventChip.index].start) + "–" + Theme.fmtHour(dayCell.cell.events[eventChip.index].end)
                                    showTip: tip.length > 0
                                    onActivated: root.eventClicked(dayCell.cell.events[eventChip.index].id, dayCell.cell.events[eventChip.index])
                                }
                            }
                        }
                        // The overflow count was dead text — the only way to
                        // reach what a cell hid was to guess. It selects the
                        // day, same as WeekView's.
                        Text {
                            id: moreText
                            objectName: "month-more"
                            readonly property int _extra: dayCell._total - dayCell._tasksShown - dayCell._eventsShown
                            visible: _extra > 0
                            // Says when a meeting is among them (A6-22): at 150%
                            // the evening sync sat silently inside "+2".
                            readonly property int _evHidden: dayCell.cell.events.length - dayCell._eventsShown
                            readonly property string _short: root.chrome ? "+" + _extra : I18n.t("cal.moreN").arg(_extra)
                            readonly property string _long: _short
                                  + (_evHidden > 0 && !root.chrome ? " · " + I18n.count(_evHidden, "today.n.meetings") : "")
                            // The meetings part goes when the cell is too
                            // narrow for it, and what is left still elides:
                            // at 1280px it was cut at the edge (F3-EYES-9).
                            // The column's width, not its own: that follows the text.
                            text: moreFm.advanceWidth(_long) <= parent.width ? _long : _short
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            FontMetrics { id: moreFm; font: moreText.font }
                            color: moreMA.hovered ? Theme.text : Theme.textDim
                            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                            ClickArea {
                                id: moreMA
                                label: I18n.t("cal.moreOnDay").arg(moreText._extra)
                                onActivated: {
                                    AppController.selectedDate = dayCell.cell.date;
                                    if (!root.chrome) root.dayRequested(dayCell.cell.date);
                                }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    // A grid with nothing dated in it (APP-191): one line and its key
    // (R3-133). Backed by the page colour, no frame, so the cell borders do
    // not cut the text. Non-interactive: a click beside it selects the day.
    Rectangle {
        objectName: "month-empty"
        visible: root.monthEmpty
        anchors.centerIn: parent
        width: monthEmptyState.width + 2 * Theme.sp3xl
        height: monthEmptyState.implicitHeight + 2 * Theme.sp2xl
        radius: Theme.radiusXl
        color: Theme.bg
        EmptyState {
            id: monthEmptyState
            objectName: "month-empty-state"
            // The default "не готово" (is:open) is not a search (DG-020).
            readonly property bool searching: root.searchText.replace(/(^|\s)is:open(?=\s|$)/gi, " ").trim().length > 0
            anchors.centerIn: parent
            width: Math.min(root.width - 2 * Theme.sp3xl - 96, 360)
            // A search that finds nothing reads as on the board (N/X-Err-Empty, R4-100).
            title: searching ? I18n.t("view.empty.noMatchFor").arg(QueryWords.label(root.searchText, []))
                             : I18n.t("month.empty.title")
            line: searching ? I18n.t("view.empty.resetFilter")
                            : I18n.t("calendar.empty.hint").arg(AppController.shortcutText("task.schedule"))
            lineLink: searching
            onLineActivated: root.resetFilterRequested()
        }
    }

    // What a drop would set, at the pointer; Esc cancels (APP-249).
    RescheduleDrag {
        id: dragLayer
        objectName: "month-drag"
        onCanceled: { root.drag = null; root.dropIndex = -1; }
    }
}
