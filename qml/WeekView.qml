pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import TodoCpp
import "Overlap.js" as Overlap
import "Segments.js" as Seg
import "Search.js" as Search
import "QueryWords.js" as QueryWords
import "Reschedule.js" as Resched
import "EventRule.js" as EventRule
import "TaskDates.js" as TaskDates

Item {
    id: root

    property string searchText: ""
    property var prioritiesFilter: ({})
    property bool showArchived: false
    // The Calendar's layers (dogfood 2026-10-22): meetings, tasks with a
    // time (and the ones planned for a day), reminders to self, deadlines.
    property var layers: ({ meetings: true, timed: true, reminders: true, deadlines: true })
    // The calendar lens (APP-264): "week" or "day", one grid at two zooms.
    // The day is the selected date alone, weekend or not.
    property string zoom: "week"
    readonly property bool dayZoom: root.zoom === "day"
    readonly property int spanDays: root.dayZoom ? 1 : 7
    // Off inside the calendar lens, where the header carries the range and
    // the tray the tasks without a date: no toolbar, no rail of its own.
    property bool chrome: true
    // A task picked in the "Without a date" tray: a click on an empty slot
    // plans it there instead of starting a meeting (APP-264).
    property string armedTaskId: ""
    signal armedUsed()
    // "сбросить фильтр · Esc" under a search that finds nothing (R4-100).
    signal resetFilterRequested()

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
    // "Перенести…" on a block: the schedule field, as s does (DG-027).
    signal scheduleRequested(string id)

    // ── The short menu on a task block (N/X-Menus-Column, R3-048) ──
    // { id, title, start, end, date } of the block it was opened on.
    property var menuBlock: null
    function openBlockMenu(b, day) {
        root.menuBlock = { id: b.id, title: b.title || "", start: b.start, end: b.end, date: day };
        blockMenu.popup();
    }
    function setBlockLength(hours) {
        const b = root.menuBlock;
        if (!b) return;
        AppController.resizeTaskBlock(b.id, b.date, b.start, Math.min(24, b.start + hours));
    }
    AppMenu {
        id: blockMenu
        objectName: "week-block-menu"
        AppMenuHeader {
            text: root.menuBlock ? root.menuBlock.title + " · " + Theme.fmtHour(root.menuBlock.start) + "–" + Theme.fmtHour(root.menuBlock.end) : ""
        }
        AppMenuItem {
            objectName: "week-block-menu-open"
            text: I18n.t("calmenu.open")
            keyText: "↵"
            onTriggered: if (root.menuBlock) root.taskClicked(root.menuBlock.id)
        }
        AppMenuItem {
            objectName: "week-block-menu-done"
            text: I18n.t("taskmenu.done")
            shortcutId: "task.done"
            onTriggered: if (root.menuBlock) { const id = root.menuBlock.id; Qt.callLater(() => AppController.toggleDone([id])); }
        }
        AppMenuItem {
            objectName: "week-block-menu-move"
            text: I18n.t("calmenu.move")
            shortcutId: "task.schedule"
            onTriggered: if (root.menuBlock) { const id = root.menuBlock.id; Qt.callLater(() => root.scheduleRequested(id)); }
        }
        AppMenuItem {
            objectName: "week-block-menu-length"
            text: I18n.t("calmenu.length")
            keyText: "Ctrl Shift J / K"
            opensList: true
            onTriggered: Qt.callLater(() => blockLengthMenu.popup())
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "week-block-menu-unplan"
            text: I18n.t("calmenu.unplan")
            note: I18n.t("calmenu.unplan.note")
            onTriggered: if (root.menuBlock) AppController.clearTaskDate(root.menuBlock.id, "scheduled")
        }
    }
    AppMenu {
        id: blockLengthMenu
        objectName: "week-block-menu-lengths"
        Instantiator {
            model: [0.25, 0.5, 0.75, 1, 1.5, 2]
            delegate: AppMenuItem {
                required property real modelData
                text: I18n.fmtMinutes(modelData * 60)
                note: !!root.menuBlock && Math.abs(root.menuBlock.end - root.menuBlock.start - modelData) < 1e-6 ? I18n.t("taskmenu.now") : ""
                onTriggered: root.setBlockLength(modelData)
            }
            onObjectAdded: (index, object) => blockLengthMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => blockLengthMenu.removeItem(object)
        }
    }
    // The occurrence, not just its id: a repeating event is stored once, so
    // every occurrence of a series carries the master's id and only the
    // occurrence map says which date was clicked.
    signal eventClicked(string id, var occurrence)
    signal dayClicked(date d)
    // An empty slot was clicked: the shell opens the event editor there.
    signal createRequested(real hour, date day)
    // A stretch was dragged on an empty part of a day: a meeting that long.
    signal createRangeRequested(real startHour, real endHour, date day)
    // A new task for a slot (DG-045): the capture opens with the time
    // already typed, the name still the person's to write.
    signal taskCaptureRequested(string text)
    // "Go to this day": the day zoom on it.
    signal dayRequested(date day)

    // ── Menus on a meeting and on an empty slot (DG-045, X-Menus-Column) ──
    // The meeting the menu was opened on: the flat event map.
    property var menuEvent: null
    readonly property var _menuOcc: root.menuEvent ? (root.menuEvent.occ || {}) : ({})
    readonly property bool _menuSeries: String(root._menuOcc.masterId || "").length > 0
    readonly property bool _menuReadOnly: !!root.menuEvent && AppController.isSubscriptionEvent(root.menuEvent.id)
    function openEventMenu(ev) {
        root.menuEvent = ev;
        eventMenu.popup();
    }
    // The menu of the meeting under the cursor (Del on a series or a
    // calendar's read-only meeting, review 2026-10-19); false when the
    // cursor is on no meeting.
    function openCursorEventMenu(): bool {
        if (!root.cursorVisible || !root.cursorKey.startsWith("event:")) return false;
        const fe = root.flatEvents;
        for (let i = 0; i < fe.length; i++) {
            if (fe[i].dayIndex === root.cursorDay && root._evKey(fe[i]) === root.cursorKey) {
                root.openEventMenu(fe[i]);
                return true;
            }
        }
        return false;
    }
    // A meeting's duration from the menu: the same as stretching its edge.
    function setEventLength(hours) {
        const e = root.menuEvent;
        if (!e) return;
        root._commitResize(e.occ, e.start, Math.min(24, e.start + hours), null);
    }
    // A single copy on the same day and time; a series is copied as one.
    function duplicateEvent() {
        const e = root.menuEvent;
        if (!e) return;
        const src = AppController.eventById(String(root._menuOcc.masterId || "") || e.id);
        const d = AppController.newEventDraft(e.start, e.date);
        for (const k of ["title", "type", "attendees", "taskId", "profileId", "context", "location", "notes", "url",
                         "reminderMinutes", "tz"])
            if (src[k] !== undefined) d[k] = src[k];
        d.end = e.end;
        AppController.saveEvent(d);
    }
    function deleteEventHere(series) {
        const e = root.menuEvent;
        if (!e) return;
        const o = root._menuOcc;
        if (root._menuSeries) {
            if (series) scopeAsk.ask("delete", (scope) => AppController.deleteOccurrence(o.masterId, o.originalDate || e.date, scope), null);
            else AppController.deleteOccurrence(o.masterId, o.originalDate || e.date, "this");
        } else {
            AppController.deleteEvent(e.id);   // undoable, with its toast
        }
    }

    AppMenu {
        id: eventMenu
        objectName: "week-event-menu"
        AppMenuItem {
            enabled: false
            contentItem: Text {
                text: root.menuEvent ? root.menuEvent.title + " · "
                      + root.dowLabelsByJsDow[root.menuEvent.date.getDay()].toLowerCase() + " "
                      + Theme.fmtHour(root.menuEvent.start) + "–" + Theme.fmtHour(root.menuEvent.end) : ""
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                leftPadding: Theme.spXl
                rightPadding: Theme.spXl
                elide: Text.ElideRight
            }
        }
        AppMenuItem {
            objectName: "week-event-menu-open"
            text: I18n.t("calmenu.open")
            keyText: "↵"
            onTriggered: if (root.menuEvent) root.eventClicked(root.menuEvent.id, root.menuEvent.occ)
        }
        AppMenuItem {
            objectName: "week-event-menu-join"
            visible: String(root._menuOcc.url || "").length > 0
            height: visible ? implicitHeight : 0
            text: I18n.t("calmenu.join")
            onTriggered: {
                const u = String(root._menuOcc.url || "").trim();
                Qt.openUrlExternally(/^[a-z]+:\/\//i.test(u) ? u : "https://" + u);
            }
        }
        AppMenuSeparator {}
        // The panel's "Когда" is where a meeting is moved by words.
        AppMenuItem {
            objectName: "week-event-menu-move"
            text: I18n.t("calmenu.move")
            enabled: !root._menuReadOnly
            keyText: "s"
            onTriggered: if (root.menuEvent) root.eventClicked(root.menuEvent.id, root.menuEvent.occ)
        }
        AppMenuItem {
            objectName: "week-event-menu-length"
            text: I18n.t("calmenu.length")
            enabled: !root._menuReadOnly
            keyText: "Ctrl Shift J / K"
            opensList: true
            onTriggered: Qt.callLater(() => lengthMenu.popup())
        }
        AppMenuItem {
            objectName: "week-event-menu-duplicate"
            text: I18n.t("calmenu.duplicate")
            enabled: !root._menuReadOnly
            onTriggered: Qt.callLater(root.duplicateEvent)
        }
        AppMenuItem {
            objectName: "week-event-menu-link"
            text: I18n.t("calmenu.linkTask")
            enabled: !root._menuReadOnly
            onTriggered: if (root.menuEvent) root.eventClicked(root.menuEvent.id, root.menuEvent.occ)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "week-event-menu-delete"
            text: I18n.t(root._menuSeries ? "calmenu.deleteOne" : "calmenu.delete")
            danger: true
            enabled: !root._menuReadOnly
            keyText: "Del"
            onTriggered: Qt.callLater(() => root.deleteEventHere(false))
        }
        AppMenuItem {
            objectName: "week-event-menu-delete-series"
            visible: root._menuSeries
            height: visible ? implicitHeight : 0
            text: I18n.t("calmenu.deleteSeries")
            // Both delete rows are red, as the sheet has them (b-07).
            danger: true
            enabled: !root._menuReadOnly
            onTriggered: Qt.callLater(() => root.deleteEventHere(true))
        }
    }
    AppMenu {
        id: lengthMenu
        objectName: "week-event-menu-lengths"
        Instantiator {
            model: [0.25, 0.5, 0.75, 1, 1.5, 2]
            delegate: AppMenuItem {
                required property real modelData
                text: I18n.fmtMinutes(modelData * 60)
                checkable: true
                checked: !!root.menuEvent && Math.abs(root.menuEvent.end - root.menuEvent.start - modelData) < 1e-6
                onTriggered: root.setEventLength(modelData)
            }
            onObjectAdded: (index, object) => lengthMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => lengthMenu.removeItem(object)
        }
    }

    // The empty slot the menu was opened on: { date, hour }.
    property var menuSlot: null
    function openSlotMenu(day, hour) {
        root.menuSlot = { date: day, hour: hour };
        root._undated = AppController.undatedTasks("").slice(0, 12);
        slotMenu.popup();
    }
    // What "Поставить из «Без даты»" lists, read when the menu opens.
    property var _undated: []
    AppMenu {
        id: slotMenu
        objectName: "week-slot-menu"
        AppMenuItem {
            enabled: false
            contentItem: Text {
                text: root.menuSlot ? I18n.fmtDate(root.menuSlot.date, "weekdayDay") + " · " + Theme.fmtHour(root.menuSlot.hour) : ""
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                leftPadding: Theme.spXl
                rightPadding: Theme.spXl
            }
        }
        AppMenuItem {
            objectName: "week-slot-menu-meeting"
            text: I18n.t("calmenu.newMeeting")
            shortcutId: "cal.newEvent"
            onTriggered: if (root.menuSlot) root.createRequested(root.menuSlot.hour, root.menuSlot.date)
        }
        AppMenuItem {
            objectName: "week-slot-menu-task"
            text: I18n.t("calmenu.newTask")
            keyText: "o"
            onTriggered: if (root.menuSlot)
                root.taskCaptureRequested(I18n.fmtDate(root.menuSlot.date, "longDay") + " " + Theme.fmtHour(root.menuSlot.hour) + " ")
        }
        AppMenuItem {
            objectName: "week-slot-menu-undated"
            text: I18n.t("calmenu.fromUndated")
            enabled: root._undated.length > 0
            opensList: true
            onTriggered: Qt.callLater(() => undatedMenu.popup())
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "week-slot-menu-day"
            text: I18n.t("calmenu.goToDay")
            keyText: "↵"
            onTriggered: if (root.menuSlot) root.dayRequested(root.menuSlot.date)
        }
    }
    AppMenu {
        id: undatedMenu
        objectName: "week-slot-menu-undated-list"
        Instantiator {
            model: root._undated
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.title || modelData.id
                onTriggered: if (root.menuSlot)
                    AppController.rescheduleTask(String(modelData.id), "scheduled",
                                                 Resched.atHour(root.menuSlot.date, Math.min(root.menuSlot.hour, 24 - Theme.minEventHours)), true)
            }
            onObjectAdded: (index, object) => undatedMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => undatedMenu.removeItem(object)
        }
    }


    // Shift-click anchor + day for range select. Shift across days falls back
    // to single-toggle since the visible-chip order isn't a single flat list.
    property string shiftAnchorId: ""
    property int    shiftAnchorDay: -1
    Connections {
        target: AppController

        function onSelectedTaskIdsChanged() {
            if (AppController.selectionCount === 0) {
                root.shiftAnchorId = "";
                root.shiftAnchorDay = -1;
            }
        }
    }

    function _renderedIdsForDay(dayIdx) {
        if (dayIdx < 0 || dayIdx >= days.length) return [];
        const list = days[dayIdx].tasks.slice(0, root.dayZoom ? 3 : gridHost.dueMaxLines);
        const ids = [];
        for (let i = 0; i < list.length; i++) if (!list[i].ghost) ids.push(list[i].id);
        return ids;
    }

    function _dayIndexOfTask(taskId) {
        for (let i = 0; i < days.length; i++) {
            const ids = _renderedIdsForDay(i);
            if (ids.indexOf(taskId) >= 0) return i;
        }
        return -1;
    }

    function selectAllVisible() {
        const ids = [];
        for (let i = 0; i < days.length; i++) {
            const part = _renderedIdsForDay(i);
            for (let j = 0; j < part.length; j++) ids.push(part[j]);
        }
        AppController.setSelectedTaskIds(ids);
    }

    function _rangeSelect(targetId) {
        const targetDay = _dayIndexOfTask(targetId);
        if (targetDay < 0) return;
        if (root.shiftAnchorId === "" || root.shiftAnchorDay !== targetDay
            || _renderedIdsForDay(targetDay).indexOf(root.shiftAnchorId) < 0) {
            root.shiftAnchorId = targetId;
            root.shiftAnchorDay = targetDay;
            AppController.toggleTaskSelection(targetId);
            return;
        }
        const ordered = _renderedIdsForDay(targetDay);
        const ai = ordered.indexOf(root.shiftAnchorId);
        const ti = ordered.indexOf(targetId);
        const lo = Math.min(ai, ti);
        const hi = Math.max(ai, ti);
        const merged = AppController.selectedTaskIds.slice();
        for (let i = lo; i <= hi; i++) {
            if (merged.indexOf(ordered[i]) < 0) merged.push(ordered[i]);
        }
        AppController.setSelectedTaskIds(merged);
    }

    // The whole day, like the day grid. Clipping to workdayStart..workdayEnd
    // did not merely de-emphasise the rest — an 07:00 standup or a 21:00 call
    // was laid out off-grid and could not be reached at all.
    readonly property int hoursStart: 0
    readonly property int hoursEnd:   24
    readonly property int workStart:  AppController.workdayStart
    readonly property int workEnd:    AppController.workdayEnd

    property date now: new Date()

    // Returns false while the grid has no height to scroll yet.
    // The hour the grid opens on (rows from hoursStart): the working day,
    // or the hour before now when that is earlier. Pure, for the tests.
    function workdayFirstHour(nowHour) {
        return Math.max(0, Math.min(root.workStart, nowHour - 1) - root.hoursStart);
    }
    function scrollToWorkday() {
        const flick = hourScroll.contentItem;
        if (!flick || flick.height <= 0 || flick.contentHeight <= flick.height) return false;
        const first = root.workdayFirstHour(new Date().getHours());
        const maxY = Math.max(0, flick.contentHeight - flick.height);
        flick.contentY = Math.min(maxY, first * root.hourH);
        return true;
    }
    // Component.onCompleted runs before the ScrollView has been laid out, so
    // the scroll used to land on a zero-height grid and the week opened on
    // 00:00–09:00, empty. Try again each frame until there is something to
    // scroll, for at most a second.
    Timer {
        id: workdayScroll
        interval: 16
        repeat: true
        property int tries: 0
        onTriggered: if (root.scrollToWorkday() || ++tries > 60) stop()
    }

    // The "Needs a slot" rail, when the window has room for it. The header
    // button hides it for good on a window that has room but no need.
    property bool railWanted: true
    Timer { interval: 60000; repeat: true; running: true; onTriggered: root.now = new Date() }
    readonly property int hourH: Theme.px(50)
    // A block's gap to its column edge: the day stands its blocks 8px
    // in from both sides (N-Oth-DayMonth, R4-048), the week 2px.
    readonly property int laneInset: root.dayZoom ? Theme.spMd : 2
    readonly property int blockRadius: root.dayZoom ? Theme.radius : Theme.radiusMd
    // An event block's floor heights (APP-199): one line of small text, or
    // the time on top of a title.
    readonly property int eventOneLineH: Math.ceil(Theme.fsXs * 1.4) + 2 * Theme.sp2xs
    readonly property int eventTwoLineH: Math.ceil((Theme.fsXs + Theme.fsSm) * 1.4) + 2 * Theme.sp2xs
    // The top of a block its time and a title of up to three lines take:
    // a "+N" chip stays below it.
    readonly property int blockTitleH: Math.ceil((Theme.fsXs + 3 * Theme.fsSm) * 1.4) + 2 * Theme.sp2xs
    function eventBlock(start, end) {
        return Overlap.block(start, end, root.hourH, root.eventOneLineH, root.eventTwoLineH);
    }
    // Indexed by JS day-of-week (0=Sun..6=Sat) so the label tracks the actual
    // date regardless of which day the week starts on. Names come from the app
    // language — they used to be hardcoded English next to a localised date
    // range in the same header.
    readonly property var dowLabelsByJsDow: {
        const out = [];
        for (let i = 0; i < 7; i++) out.push(I18n.dayName(i));
        return out;
    }

    function _dow(d) {
        const n = root.dowLabelsByJsDow[d.getDay()];
        return I18n.lang === "ru" ? n.toLowerCase() : n;
    }
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
    // Weekend shading must track the real day-of-week, not the column position:
    // under weekStart="sun" column 5 is Friday and column 0 is Sunday, so the
    // old `index >= 5` tinted Friday and missed Sunday.
    function isWeekendDate(d) {
        if (!d || !d.getDay) return false;
        const w = d.getDay();
        return w === 0 || w === 6;
    }
    function fmtShort(d) {
        return AppController.shortDate(d);
    }
    function snapHour(h)  {
        const step = Math.max(1, Theme.snapMinutes) / 60.0;
        return Math.round(h / step) * step;
    }
    function yToHour(y)   { return root.hoursStart + y / root.hourH; }
    function clampHour(h) { return Math.max(root.hoursStart, Math.min(root.hoursEnd, h)); }
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

    // Declarative binding: re-evaluates on AppController.selectedDate and
    // Theme.weekStart changes — no manual Connections needed.
    property var weekStart: root.dayZoom
        ? new Date(AppController.selectedDate.getFullYear(), AppController.selectedDate.getMonth(), AppController.selectedDate.getDate())
        : startOfWeek(AppController.selectedDate)

    // Every event in the model, in the shape Segments.js reads. Its own
    // binding rather than a local inside buildDays(), because the all-day strip
    // needs the same list and buildDays() must not write a property it is
    // itself bound to.
    // One week in either direction. Named to match MonthView.step(), so the
    // keyboard can move the date without knowing which calendar is on screen.
    function step(dir) {
        const w = root.weekStart;
        if (!w || !w.getFullYear) return;
        AppController.selectedDate = new Date(w.getFullYear(), w.getMonth(), w.getDate() + (root.spanDays * dir));
    }

    // The rebuild is one chain, each stage reading only the one before it:
    // weekStart / eventRev -> spans -> eventDays -> days -> overlaps, strip.
    // When a stage also read weekStart itself, a week step re-ran it twice —
    // once with the old week's input and again when that caught up — and
    // every extra run of eventDays rebuilt all the event delegates (TIME-18).
    function buildSpans() {
        const _e = root.eventRev;
        const start = root.weekStart;
        if (!start || !start.getFullYear) return [];
        // A day either side of the week: a timed event that crosses midnight
        // reaches in from the Sunday before, and out into the Monday after.
        const from = new Date(start.getFullYear(), start.getMonth(), start.getDate() - 1);
        const to = new Date(start.getFullYear(), start.getMonth(), start.getDate() + root.spanDays);
        // Copied out into a plain array once. What comes back is a C++ list
        // the engine converts element by element on every index — and
        // buildEventDays() reads each occurrence once per day column, so a
        // week of 420 occurrences cost ~3000 full conversions, 2.4 s per
        // step or save (TIME-18).
        // The meetings layer off: none on the grid nor in the all-day strip.
        const raw = root.layers.meetings === false ? [] : AppController.eventOccurrences(from, to);
        const n = raw.length;
        const out = new Array(n);
        for (let i = 0; i < n; i++) out[i] = raw[i];
        out.weekStart = start;
        return out;
    }
    readonly property var spans: buildSpans()

    // The event half of the week, on its own binding: it depends on the
    // events and the week only, so a task that changes mid-drag (a running
    // timer ticks every second) does not rebuild the event delegates and
    // drop the block out from under the pointer.
    function buildEventDays() {
        const spans = root.spans;
        const start = spans.weekStart || root.weekStart;
        const days = [];
        const showWeekends = Theme.showWeekends || root.dayZoom;
        const span = root.dayZoom ? 1 : 7;
        // Column of each day of the week, by its offset from weekStart; -1
        // for a hidden weekend.
        const colOf = [];
        for (let i = 0; i < span; i++) {
            const d = new Date(start.getFullYear(), start.getMonth(), start.getDate() + i);
            if (!showWeekends && (d.getDay() === 0 || d.getDay() === 6)) { colOf.push(-1); continue; }
            colOf.push(days.length);
            days.push({ date: d, events: [] });
        }
        // An event is no longer pinned to one day: it may be all-day, or run
        // past midnight. Each day takes the piece that lands on it, so a
        // 22:00-02:00 call draws on both days instead of only the one its
        // `date` happens to name.
        const linked = [];   // per column: task ids a linked event stands in for
        for (let k = 0; k < days.length; k++) linked.push({});
        // Which days of the week each occurrence covers, found once by
        // arithmetic (as MonthView does) rather than by asking Segments.js
        // about every occurrence on every day: that was seven rounds of Date
        // construction per occurrence and most of a week step's time with a
        // few hundred occurrences on screen (TIME-18). The pieces are the
        // ones Seg.segmentOn() cuts: the first day keeps the start, the last
        // day the end, a day in between runs 0-24.
        const first = Date.UTC(start.getFullYear(), start.getMonth(), start.getDate());
        const dayOf = (d) => Math.round((Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()) - first) / 86400000);
        for (let i = 0; i < spans.length; i++) {
            const e = spans[i];
            if (!e.date || !e.date.getFullYear) continue;
            const a = dayOf(e.date);
            const b = (e.endDate && e.endDate.getFullYear) ? Math.max(a, dayOf(e.endDate)) : a;
            const strip = Seg.isStrip(e);   // all-day events live in the strip
            for (let off = Math.max(0, a); off <= Math.min(span - 1, b); off++) {
                const k = colOf[off];
                if (k < 0) continue;
                if (e.taskId) linked[k][String(e.taskId)] = true;
                if (strip) continue;
                const segFirst = off === a;
                const segLast = off === b;
                days[k].events.push({
                    id: e.id, title: e.title, type: e.type,
                    start: segFirst ? e.start : 0, end: segLast ? e.end : 24,
                    attendees: e.attendees, date: e.date, context: e.context,
                    masterId: e.masterId || "", occurrenceDate: e.occurrenceDate,
                    occ: e,
                    // Unique per piece: the same event can appear on several
                    // days, and the overlap map is keyed by this. Keying it by
                    // event id would let Tuesday's piece overwrite Monday's.
                    key: e.id + "@" + k,
                    segFirst: segFirst, segLast: segLast
                });
            }
        }
        return { days: days, colOf: colOf, linked: linked, spans: spans, weekStart: start };
    }
    readonly property var eventDays: buildEventDays()

    function buildDays() {
        const _t = root.taskRev;
        const ev = root.eventDays;
        const start = ev.weekStart;
        const colOf = ev.colOf;
        const linked = ev.linked;
        const days = [];
        for (let k = 0; k < ev.days.length; k++)
            days.push({ date: ev.days[k].date, tasks: [], events: ev.days[k].events, blocks: [], reminders: [] });
        const showTimed = root.layers.timed !== false;
        const showDue = root.layers.deadlines !== false;
        const showRemind = root.layers.reminders !== false;
        // Tasks: C++ hands over only those due or scheduled this week, with
        // the day already worked out — reading ten roles of every task in the
        // profile to find the few that are this week took seconds at 10k.
        // A deadline is a chip in the header; so is a date-only schedule. A
        // task scheduled at a clock time is a block on the grid, next to the
        // events, unless a linked event already stands in for it.
        const end = new Date(start.getFullYear(), start.getMonth(), start.getDate() + colOf.length - 1);
        const list = AppController.calendarTasks(start, end, root.showArchived);
        for (let i = 0; i < list.length; i++) {
            const t = list[i];
            if (!root.passesFilter(t)) continue;
            const dueCol = showDue && t.dueDay >= 0 ? colOf[t.dueDay] : -1;
            const schedCol = showTimed && t.schedDay >= 0 ? colOf[t.schedDay] : -1;
            // A reminder to self at its moment: a bell and the time on the grid.
            const remindCol = showRemind && t.reminder && t.remindDay >= 0 ? colOf[t.remindDay] : -1;
            if (remindCol >= 0 && t.remindHour >= 0)
                days[remindCol].reminders.push({ id: t.id, title: t.title, hour: t.remindHour,
                                                 key: "remind:" + t.id + "@" + remindCol });
            // Each thing once (APP-264): a task with a time that day is its
            // block, not also a flag in the deadline row above it.
            const timedThere = schedCol >= 0 && schedCol === dueCol && t.schedHour >= 0;
            if (dueCol >= 0 && !timedThere) days[dueCol].tasks.push(t);
            if (schedCol >= 0) {
                if (t.schedHour >= 0) {
                    if (!linked[schedCol][t.id]) {
                        // As long as Today and the day's load count it.
                        const len = (t.blockMinutes > 0 ? t.blockMinutes
                                   : t.estimateMinutes > 0 ? t.estimateMinutes : 60) / 60;
                        days[schedCol].blocks.push({
                            id: t.id, title: t.title, priority: t.priority, status: t.status,
                            due: dueCol === schedCol,
                            start: t.schedHour, end: Math.min(24, t.schedHour + Math.max(Theme.minEventHours, len)),
                            key: "task:" + t.id + "@" + schedCol
                        });
                    }
                } else if (schedCol !== dueCol) {
                    days[schedCol].tasks.push(Object.assign({ scheduled: true }, t));
                }
            }
        }
        const priRank = { P0: 0, P1: 1, P2: 2, P3: 3 };
        for (let k = 0; k < days.length; k++) {
            days[k].tasks.sort((a, b) => (priRank[a.priority] ?? 9) - (priRank[b.priority] ?? 9));
        }
        // A2-25: the occurrences ahead of a repeating task, worked out from
        // its rule, faint and read-only after the day's real chips. Shown,
        // never created; Return on one opens the task itself.
        // They go with the tasks layer.
        const ghosts = showTimed ? AppController.calendarRepeats(start, end) : [];
        for (let i = 0; i < ghosts.length; i++) {
            const g = ghosts[i];
            const col = g.day >= 0 && g.day < colOf.length ? colOf[g.day] : -1;
            if (col >= 0 && root.passesFilter(g)) days[col].tasks.push(g);
        }
        return days;
    }

    // The load of each day on screen (APP-247): meetings and task blocks,
    // each minute once, against the working day. A fact, never advice.
    // The revisions are read in the expression: an unused local reading them
    // would be dropped by the compiler, and the load would never refresh.
    readonly property var loads: (root.taskRev >= 0 && root.eventRev >= 0 && root.weekStart && root.weekStart.getFullYear)
        ? AppController.dayLoads(root.weekStart, root.spanDays) : []
    // By its offset from the first day: a QDate crossing into JS is not a
    // safe key across time zones.
    function loadOf(d) {
        if (!d || !d.getFullYear || !root.weekStart || !root.weekStart.getFullYear) return null;
        const i = root._daysBetween(root.weekStart, d);
        return i >= 0 && i < root.loads.length ? root.loads[i] : null;
    }
    // "3 h of 10" in a week column; the whole sentence on a day.
    function loadShort(l) {
        if (!l) return "";
        const busy = (l.meetings || 0) + (l.tasks || 0);
        if (!l.workday) return busy > 0 ? I18n.fmtMinutes(busy) : I18n.t("today.dayOff");
        // An empty working day says nothing; the empty scale under it is the fact.
        if (busy === 0) return "";
        return I18n.t("load.ofWork").arg(I18n.fmtMinutes(busy)).arg(I18n.fmtMinutes(l.work));
    }
    function loadLong(l) {
        if (!l) return "";
        const parts = [];
        if (l.meetings > 0) parts.push(I18n.t("load.meetings").arg(I18n.fmtMinutes(l.meetings)));
        if (l.tasks > 0) parts.push(I18n.t("load.tasks").arg(I18n.fmtMinutes(l.tasks)));
        if (l.workday) parts.push(I18n.t("load.free").arg(I18n.fmtMinutes(l.free || 0)));
        else parts.push(I18n.t("today.dayOff"));
        if (l.overWork > 0) parts.push(I18n.t("load.over").arg(I18n.fmtMinutes(l.overWork)));
        return parts.join(" · ");
    }

    // A repeat ahead (A2-25): its clock time when the task has one, then the
    // title — "09:00 Проверить бэкапы". What the tip and a reader say.
    function ghostTitle(g) {
        if (!g || !g.hasTime || !(g.hour >= 0)) return g ? g.title : "";
        const h = Math.floor(g.hour);
        return I18n.fmtTime(new Date(2000, 0, 1, h, Math.round((g.hour - h) * 60))) + " " + g.title;
    }
    function ghostTip(g) {
        return I18n.t("cal.ghost.tip").arg(g.title).arg(TaskDates.repeatText(g.recurrence));
    }

    // The day zoom's header line (DG-051, X-Oth-DayMonth): "2 встречи ·
    // 1 задача · срок:", the deadlines following as links. Facts only.
    function dayFacts(day) {
        if (!day) return "";
        const parts = [];
        const meetings = day.events ? day.events.length : 0;
        let planned = day.blocks ? day.blocks.length : 0;
        let dues = 0;
        for (let i = 0; i < day.tasks.length; i++) {
            if (day.tasks[i].ghost) continue;   // a repeat ahead is not a fact of the day
            if (day.tasks[i].scheduled) planned++;
            else dues++;
        }
        if (meetings > 0) parts.push(I18n.count(meetings, "cal.n.meetings"));
        if (planned > 0) parts.push(I18n.count(planned, "query.n.tasks"));
        if (dues > 0) parts.push(I18n.t("cal.dueColon"));
        return parts.join(" · ");
    }
    // An hour on the week's gutter: "10" (DG-043); the day keeps "10:00".
    function hourLabel(h) {
        if (root.dayZoom || Theme.twelveHour) return Theme.fmtHour(h);
        return String(h).padStart(2, "0");
    }

    // Declarative: buildDays() reads weekStart, taskRev, eventRev, showArchived,
    // searchText, prioritiesFilter, and Theme.showWeekends — QML auto-tracks
    // those reads and re-evaluates this binding when any of them changes.
    readonly property var days: buildDays()

    // True when the visible week has neither task deadlines nor events, so the
    // grid would otherwise read as blank/broken.
    readonly property bool weekEmpty: {
        for (let i = 0; i < days.length; i++)
            if (days[i].tasks.length > 0 || days[i].events.length > 0 || days[i].blocks.length > 0
                    || days[i].reminders.length > 0) return false;
        return true;
    }

    // Flattened events with per-week day index so interactive drag/resize
    // can position them absolutely (and move across day columns).
    function buildFlatEvents() {
        const out = [];
        const days = root.eventDays.days;
        for (let i = 0; i < days.length; i++) {
            for (let j = 0; j < days[i].events.length; j++) {
                const e = days[i].events[j];
                out.push({
                    id: e.id, title: e.title, type: e.type,
                    start: e.start, end: e.end, attendees: e.attendees,
                    date: e.date, dayIndex: i, context: e.context || "",
                    key: e.key, segFirst: e.segFirst, segLast: e.segLast,
                    masterId: e.masterId || "", occurrenceDate: e.occurrenceDate,
                    // The occurrence as it came from the expansion: a click
                    // opens the editor on this, not on a copy missing half
                    // its fields.
                    occ: e.occ
                });
            }
        }
        return out;
    }
    readonly property var flatEvents: buildFlatEvents()

    function buildFlatBlocks() {
        const out = [];
        for (let i = 0; i < days.length; i++)
            for (let j = 0; j < days[i].blocks.length; j++)
                out.push(Object.assign({ dayIndex: i }, days[i].blocks[j]));
        return out;
    }
    readonly property var flatBlocks: buildFlatBlocks()
    // Reminders to self on the grid (dogfood 2026-10-22): a bell at the time.
    function buildFlatReminders() {
        const out = [];
        for (let i = 0; i < days.length; i++)
            for (let j = 0; j < days[i].reminders.length; j++)
                out.push(Object.assign({ dayIndex: i }, days[i].reminders[j]));
        return out;
    }
    readonly property var flatReminders: buildFlatReminders()

    // A drag or resize on one occurrence of a series asks which ones it is
    // for, like the editor does; anything else applies at once.
    SeriesScopeDialog { id: scopeAsk }
    readonly property alias scopePrompt: scopeAsk
    // `done` runs once the move is made, after the scope answer for a series.
    function _commitMove(occ, deltaHours, cancel, done) {
        if (!occ || Math.abs(deltaHours) < 1e-9) { if (cancel) cancel(); return; }
        if (String(occ.masterId || "").length > 0) {
            scopeAsk.ask("move", (scope) => {
                AppController.moveOccurrence(occ, deltaHours, scope);
                if (done) done();
            }, cancel);
        } else {
            AppController.moveOccurrence(occ, deltaHours, "this");
            if (done) done();
        }
    }
    function _commitResize(occ, start, end, cancel) {
        if (!occ) { if (cancel) cancel(); return; }
        if (String(occ.masterId || "").length > 0)
            scopeAsk.ask("move", (scope) => AppController.resizeOccurrence(occ, start, end, scope), cancel);
        else
            AppController.resizeOccurrence(occ, start, end, "this");
    }
    function _daysBetween(a, b) {
        return Math.round((Date.UTC(b.getFullYear(), b.getMonth(), b.getDate())
                         - Date.UTC(a.getFullYear(), a.getMonth(), a.getDate())) / 86400000);
    }

    // ── Moving a task to another day or time (APP-249) ───────────────
    // A task's own block moves and stretches like an event; a chip in the
    // day headers goes to another day. Both change when the task is planned
    // (scheduledAt), never its deadline: a chip dropped on a day header keeps
    // the time it was planned for, one dropped on the hour grid takes that
    // hour. The hint at the pointer says what a drop would set; Esc cancels.
    // { id, scheduledAt, timed } of the chip being carried.
    property var chipDrag: null
    // { day: index, hour: -1 for a header (date only) } under the pointer.
    property var chipTarget: null

    function _chipTargetAt(x, y) {
        // `x`, `y` in gridHost's coordinates.
        const col = Math.floor((x - gridHost.gutterW) / gridHost.dayW);
        if (x < gridHost.gutterW || col < 0 || col >= root.days.length) return null;
        if (y >= 0 && y < headerBand.height) return { day: col, hour: -1 };
        const p = gridContent.mapFromItem(gridHost, x, y);
        const top = hourScroll.mapToItem(gridHost, 0, 0).y;
        if (y < top || y >= top + hourScroll.height) return null;
        return { day: col, hour: root.clampHour(root.snapHour(root.yToHour(p.y))) };
    }
    function _chipLanding(tgt) {
        if (!root.chipDrag || !tgt) return null;
        const day = root.days[tgt.day].date;
        if (tgt.hour < 0) return Resched.dropOnDay(day, root.chipDrag.scheduledAt, root.chipDrag.timed);
        return { when: Resched.atHour(day, Math.min(tgt.hour, 24 - Theme.minEventHours)), timed: true };
    }
    function beginChipDrag(t) {
        const full = AppController.taskById(t.id);
        root.chipDrag = { id: t.id, scheduledAt: full.scheduledAt, timed: !!full.scheduledHasTime };
        root.chipTarget = null;
        dragLayer.begin(t.id, t.title, true);
    }
    // `x`, `y` in gridHost's coordinates.
    function moveChipDrag(x, y) {
        if (!root.chipDrag) return;
        root.chipTarget = root._chipTargetAt(x, y);
        const land = root._chipLanding(root.chipTarget);
        const p = dragLayer.mapFromItem(gridHost, x, y);
        dragLayer.update(p.x, p.y, land ? dragLayer.describe("scheduled", land.when, land.timed) : I18n.t("drag.notHere"), !!land);
    }
    function endChipDrag() {
        const d = root.chipDrag;
        const land = root._chipLanding(root.chipTarget);
        root.chipDrag = null;
        root.chipTarget = null;
        dragLayer.finish();
        if (!d || !land) return false;
        return AppController.rescheduleTask(d.id, "scheduled", land.when, land.timed);
    }

    // The task the move keys act on: the chip or block that has the
    // keyboard, else the one under the pointer. Kept after a move rebuilds
    // the week, so the next press moves the same task again.
    property string keyTaskId: ""
    property var keyTaskDay: null
    property string hoverTaskId: ""
    property var hoverTaskDay: null
    function _keyTask() {
        // The keyboard cursor first (APP-276); on a meeting it is the meeting.
        const it = root._cursorItem();
        if (it) return it.kind === "task" ? { id: it.id, day: root.days[root.cursorDay].date } : null;
        if (root.keyTaskId) return { id: root.keyTaskId, day: root.keyTaskDay };
        if (root.hoverTaskId) return { id: root.hoverTaskId, day: root.hoverTaskDay };
        return null;
    }
    function moveKeyTaskByDays(days) {
        const it = root._cursorItem();
        if (it && it.kind === "event") return root._moveCursorEvent(days * 24);
        const k = root._keyTask();
        if (!k) return false;
        const t = AppController.taskById(k.id);
        if (!t || !t.id) return false;
        const r = Resched.shiftByDays(t.scheduledAt, t.scheduledHasTime, days, k.day, AppController.today);
        const ok = AppController.rescheduleTask(k.id, "scheduled", r.when, r.timed);
        if (ok && it) root._follow(days);
        return ok;
    }
    function moveKeyTaskByTime(steps) {
        const it = root._cursorItem();
        if (it && it.kind === "event") return root._moveCursorEvent(steps * Theme.snapMinutes / 60);
        const k = root._keyTask();
        if (!k) return false;
        const t = AppController.taskById(k.id);
        const r = t ? Resched.shiftByTime(t.scheduledAt, t.scheduledHasTime, steps, Theme.snapMinutes) : null;
        return r ? AppController.rescheduleTask(k.id, "scheduled", r.when, true) : false;
    }

    // ── The keyboard cursor (APP-276) ────────────────────────────────
    // One cursor for tasks and meetings: j / k walk a day top to bottom (its
    // deadlines, then the grid by time), h / l go to the day beside it and
    // turn the week over at its edge. It holds on to what it is on by key,
    // so a move, a sync or a filter leaves it there — or on the neighbour
    // when the thing is gone. The day it is on is the selected date.
    property bool cursorVisible: false
    property string cursorKey: ""
    property int cursorDay: -1
    property int _cursorIdx: 0
    readonly property string cursorTaskId: {
        const it = root._cursorItem();
        return it && it.kind === "task" ? it.id : "";
    }
    readonly property bool cardMenuOpen: false
    function _evKey(e) {
        return "event:" + e.id + (e.masterId ? "@" + Number(e.occurrenceDate || e.date) : "");
    }
    function _dayItems(d) {
        if (d < 0 || d >= root.days.length) return [];
        const day = root.days[d];
        const out = [];
        const chips = day.tasks.slice(0, 4);
        for (let i = 0; i < chips.length; i++) {
            // A repeat ahead (A2-25) is its own stop: Return opens the task,
            // nothing moves, selects or archives it.
            if (chips[i].ghost)
                out.push({ kind: "ghost", id: chips[i].id, key: "ghost:" + chips[i].id + "@" + d, start: -1 });
            else
                out.push({ kind: "task", id: chips[i].id, key: "task:" + chips[i].id, start: -1 });
        }
        const timed = [];
        for (let i = 0; i < day.events.length; i++) {
            const e = day.events[i];
            timed.push({ kind: "event", id: e.id, key: root._evKey(e), start: e.start, end: e.end, ev: e });
        }
        for (let i = 0; i < day.blocks.length; i++) {
            const b = day.blocks[i];
            timed.push({ kind: "task", id: b.id, key: "task:" + b.id, start: b.start, end: b.end, block: b });
        }
        timed.sort((a, b) => a.start - b.start);
        return out.concat(timed);
    }
    function _cursorIndex() {
        const items = root._dayItems(root.cursorDay);
        for (let i = 0; i < items.length; i++)
            if (items[i].key === root.cursorKey) return i;
        return -1;
    }
    function _cursorItem() {
        if (!root.cursorVisible || root.cursorKey === "") return null;
        const items = root._dayItems(root.cursorDay);
        for (let i = 0; i < items.length; i++)
            if (items[i].key === root.cursorKey) return items[i];
        return null;
    }
    function clearCursor() {
        root.cursorVisible = false;
        root.cursorKey = "";
    }
    function _place(d, i) {
        if (root.days.length === 0) return;
        d = Math.max(0, Math.min(root.days.length - 1, d));
        const items = root._dayItems(d);
        root.cursorDay = d;
        root.cursorVisible = true;
        const date = root.days[d].date;
        if (!root.isSameDay(date, AppController.selectedDate)) AppController.selectedDate = date;
        if (items.length === 0) {
            root.cursorKey = "";
            root._cursorIdx = 0;
            return;
        }
        const at = Math.max(0, Math.min(items.length - 1, i));
        root.cursorKey = items[at].key;
        root._cursorIdx = at;
        root._revealItem(items[at]);
        if (items[at].kind === "task") AppController.markTaskSeen(items[at].id);
    }
    function _revealItem(it) {
        if (!it || it.start < 0) return;
        const f = hourScroll.contentItem;
        if (!f || f.height <= 0) return;
        const top = (it.start - root.hoursStart) * root.hourH;
        const bottom = (it.end - root.hoursStart) * root.hourH;
        if (top < f.contentY) f.contentY = Math.max(0, top - root.hourH / 2);
        else if (bottom > f.contentY + f.height)
            f.contentY = Math.min(Math.max(0, f.contentHeight - f.height), Math.min(top, bottom - f.height + root.hourH / 2));
    }
    function _startDay() {
        for (let i = 0; i < root.days.length; i++)
            if (root.isSameDay(root.days[i].date, AppController.selectedDate)) return i;
        return 0;
    }
    function moveCursor(dx, dy) {
        if (root.days.length === 0) return;
        if (!root.cursorVisible || root.cursorDay < 0 || root.cursorDay >= root.days.length) {
            root._place(root._startDay(), 0);
            return;
        }
        const at = Math.max(0, root._cursorIndex());
        if (dx !== 0) {
            const nd = root.cursorDay + dx;
            if (nd < 0 || nd >= root.days.length) {
                root.step(dx);
                Qt.callLater(root._place, dx < 0 ? root.spanDays : 0, at);
                return;
            }
            root._place(nd, at);
            return;
        }
        if (root.cursorKey === "") return;
        root._place(root.cursorDay, at + dy);
    }
    // The cursor goes with what it moved, into the next week if need be.
    function _follow(days) {
        const d = root.days[root.cursorDay] ? root.days[root.cursorDay].date : AppController.selectedDate;
        const target = new Date(d.getFullYear(), d.getMonth(), d.getDate() + days);
        AppController.selectedDate = target;
        for (let i = 0; i < root.days.length; i++) {
            if (root.isSameDay(root.days[i].date, target)) { root.cursorDay = i; return; }
        }
        Qt.callLater(root._reconcile);
    }
    function _reconcile() {
        if (!root.cursorVisible || root.cursorKey === "" || root._cursorIndex() >= 0) return;
        for (let d = 0; d < root.days.length; d++) {
            const items = root._dayItems(d);
            for (let i = 0; i < items.length; i++) {
                if (items[i].key === root.cursorKey) {
                    root.cursorDay = d;
                    root._cursorIdx = i;
                    return;
                }
            }
        }
        root._place(root.cursorDay, root._cursorIdx);
    }
    onDaysChanged: if (root.cursorVisible) Qt.callLater(root._reconcile)
    function _actionCardId() {
        const it = root._cursorItem();
        if (it) return it.kind === "task" ? it.id : "";
        if (AppController.selectionCount === 1) return AppController.selectedTaskIds[0];
        return root.hoverTaskId || "";
    }
    function openCursor() {
        const it = root._cursorItem();
        if (!it) { if (!root.cursorVisible) root.moveCursor(0, 0); return; }
        if (it.kind === "task" || it.kind === "ghost") root.taskClicked(it.id);
        else root.eventClicked(it.id, it.ev.occ);
    }
    function toggleCursorSelection() {
        const it = root._cursorItem();
        if (!it) { if (!root.cursorVisible) root.moveCursor(0, 0); return; }
        if (it.kind === "task") AppController.toggleTaskSelection(it.id);
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
    // Shift J / K: a grid step later / earlier; Shift H / L: a day.
    function moveCursorCard(dx, dy) { if (dy !== 0) root.moveKeyTaskByTime(dy); }
    function moveSelectionOrCard(dx) {
        if (AppController.selectionCount > 0 && !root._cursorItem()) {
            const ids = AppController.selectedTaskIds;
            for (let i = 0; i < ids.length; i++) {
                const t = AppController.taskById(ids[i]);
                if (!t || !t.id) continue;
                const r = Resched.shiftByDays(t.scheduledAt, t.scheduledHasTime, dx, null, AppController.today);
                AppController.rescheduleTask(t.id, "scheduled", r.when, r.timed);
            }
            return;
        }
        root.moveKeyTaskByDays(dx);
    }
    function _moveCursorEvent(deltaHours) {
        const it = root._cursorItem();
        if (!it || it.kind !== "event" || Math.abs(deltaHours) < 1e-9) return false;
        // The cursor follows once the scope is answered, not before
        // (IDIOT-CAL-14): Esc left it a day off, on the series' next
        // occurrence, and the next Return opened another meeting.
        const occ = it.ev.occ;
        const days = Math.abs(deltaHours) >= 24 ? Math.round(deltaHours / 24) : 0;
        root._commitMove(occ, deltaHours, null, () => {
            if (days !== 0) root._follow(days);
            Qt.callLater(root._findMoved, occ);
        });
        return true;
    }
    // After a move the occurrence may have a new id (an override): the cursor
    // goes on it in its day by its series and name.
    function _findMoved(occ) {
        const items = root._dayItems(root.cursorDay);
        const series = String(occ.masterId || occ.id);
        for (let i = 0; i < items.length; i++) {
            const e = items[i].ev;
            if (!e || !e.occ) continue;
            if ((String(e.occ.masterId || e.occ.id) === series || e.occ.id === occ.id) && e.occ.title === occ.title) {
                root.cursorKey = items[i].key;
                root._cursorIdx = i;
                return;
            }
        }
    }
    // Ctrl Shift J / K (APP-278): the block under the cursor a grid step
    // longer or shorter — what stretching its lower edge does.
    function resizeCursor(steps) {
        const it = root._cursorItem();
        if (!it || it.start < 0) return false;
        const step = Theme.snapMinutes / 60;
        const end = Math.min(24, it.end + steps * step);
        if (end - it.start < step - 1e-9) return false;
        if (it.kind === "task")
            return AppController.resizeTaskBlock(it.id, root.days[root.cursorDay].date, it.start, end);
        if (!it.ev.segFirst || !it.ev.segLast) return false;
        root._commitResize(it.ev.occ, it.start, end, null);
        return true;
    }

    // All-day events, packed into rows so bars stack instead of overlapping.
    // Computed for the whole week at once: a bar's row has to be the same in
    // every column it crosses, or a trip would jump up and down across the
    // week.
    function buildStrip() {
        const ev = root.eventDays;
        const spans = ev.spans;
        const dates = [];
        for (let i = 0; i < ev.days.length; i++) dates.push(ev.days[i].date);
        const laid = Seg.stripRows(spans, dates);
        const bars = [];
        for (let i = 0; i < spans.length; i++) {
            const e = spans[i];
            if (!Seg.isStrip(e)) continue;
            const ext = Seg.barExtent(e, dates);
            if (!ext) continue;
            bars.push({
                id: e.id, title: e.title, type: e.type,
                from: ext.from, span: ext.span,
                clippedStart: ext.clippedStart, clippedEnd: ext.clippedEnd,
                row: laid.rows[e.id] || 0,
                occ: e
            });
        }
        return { bars: bars, rows: laid.count };
    }
    readonly property var strip: buildStrip()

    // Side-by-side layout for events that share a time window. Two meetings at
    // 10:00 were drawn exactly on top of each other here: the second hid the
    // first, and only the top one could be clicked. The day grid had solved
    // this; this one had never been taught. Both call the same module now.
    function buildOverlapItems() {
        const perDay = [];
        for (let i = 0; i < days.length; i++) {
            const list = [];
            for (let j = 0; j < days[i].events.length; j++) {
                const e = days[i].events[j];
                // As tall as it is drawn: a half-hour block grown to two
                // lines sits beside the meeting after it, not on top of it.
                list.push({ id: e.key, start: e.start, end: root.eventBlock(e.start, e.end).visualEnd });
            }
            for (let j = 0; j < days[i].blocks.length; j++) {
                const b = days[i].blocks[j];
                list.push({ id: b.key, start: b.start, end: b.end });
            }
            perDay.push(list);
        }
        return perDay;
    }
    readonly property var overlapItems: buildOverlapItems()
    readonly property var overlaps: Overlap.computeByDay(root.overlapItems)
    // The narrowest lane drawn; past it the rest fold onto "+N" (A1-7).
    // Measured in the title's own font, so it grows with the scale: six
    // letters and the ellipsis in a slim block's small title ("o" is a
    // letter of average width: "Design…", "Синхро…" fit), inside its
    // padding and the gap between lanes. At a fixed 44px a lane showed
    // "Sta…" or one letter at 150% (F2-REG-2..4).
    TextMetrics {
        id: laneSample
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBlock
        font.weight: Theme.fwHeading
        text: "oooooo…"
    }
    // The same six letters in a block's usual title: narrower than that
    // with its usual padding, a block goes slim.
    TextMetrics {
        id: laneSampleSm
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        font.weight: Theme.fwHeading
        text: laneSample.text
    }
    // A block narrower than its usual padding allows keeps to this one.
    readonly property int slimPadL: Theme.spXs
    readonly property int slimPadR: Theme.sp2xs
    // + the bar, the padding and the gap between lanes.
    readonly property int minLaneW: Math.ceil(laneSample.advanceWidth) + 3 + root.slimPadL + root.slimPadR + 2
    // A day holds two such lanes side by side: the tray and the rail give
    // way to the grid before a day gets narrower than that.
    readonly property int minDayW: Math.max(Theme.px(96), 2 * root.minLaneW + 2 * root.laneInset)
    readonly property int minGridW: gridHost.gutterW + gridHost.dayCount * root.minDayW
    // Whether a block `w` wide, with `chrome` px of its usual padding and
    // icon, would leave its title less than the sample: then it drops the
    // icon and keeps to the slim padding.
    function slimBlock(w: real, chrome: int): bool {
        return !root.dayZoom && w < Math.ceil(laneSampleSm.advanceWidth) + chrome + 2;
    }
    // Whether every word of `text` fits `w` px in `fm`: else a wrapped
    // title would cut a long word at the block's edge, so it keeps to one
    // line and ends in an ellipsis instead (F3-EYES-8).
    function wordsFit(text: string, fm: var, w: real): bool {
        const words = String(text || "").split(/\s+/);
        for (let i = 0; i < words.length; i++)
            if (words[i].length > 0 && fm.advanceWidth(words[i]) > w + 0.5) return false;
        return true;
    }
    readonly property int laneAvail: gridHost.dayW - 2 * root.laneInset
    readonly property int laneFit: Overlap.capacity(root.laneAvail, root.minLaneW, 1)
    // The "+N" chips: what is folded, per day, from what hour.
    readonly property var foldChips: {
        const out = [];
        for (let i = 0; i < root.overlapItems.length; i++) {
            const f = Overlap.folds(root.overlapItems[i], root.overlaps, root.laneFit);
            for (let j = 0; j < f.length; j++) out.push(Object.assign({ dayIndex: i }, f[j]));
        }
        return out;
    }

    function totalTasks() {
        let n = 0;
        for (let i = 0; i < days.length; i++)
            for (let j = 0; j < days[i].tasks.length; j++) if (!days[i].tasks[j].scheduled && !days[i].tasks[j].ghost) n++;
        return n;
    }
    function totalEvents() {
        let n = 0;
        for (let i = 0; i < days.length; i++) n += days[i].events.length;
        return n;
    }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.px(50)
            visible: root.chrome
            color: Theme.panel
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1; color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.sp2xl; anchors.rightMargin: Theme.sp2xl
                spacing: Theme.spMd

                // An arrow says nothing to a screen reader, and the tooltip
                // names the key as bound now (design audit DES-22).
                PillButton {
                    id: prevWeekBtn
                    objectName: "week-prev"
                    text: "←"
                    Accessible.name: I18n.t("miniweek.prevWeek")
                    ToolTip.visible: prevWeekBtn.hovered || prevWeekBtn.visualFocus
                    ToolTip.delay: 500
                    ToolTip.text: I18n.t("miniweek.prevWeek") + "  " + AppController.shortcutFor("cal.prev")
                    onClicked: root.step(-1)
                }
                ColumnLayout {
                    spacing: 1
                    Layout.alignment: Qt.AlignVCenter
                    RowLayout {
                        spacing: Theme.spLg
                        Text {
                            text: I18n.t("week.number").arg(AppController.isoWeekNumber(weekStart))
                            color: Theme.textDim
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            font.pixelSize: Theme.fsSm
                        }
                        Text {
                            text: I18n.relang(AppController.shortDate(weekStart)) + " — " + AppController.shortDate(new Date(weekStart.getFullYear(), weekStart.getMonth(), weekStart.getDate() + 6))
                            color: Theme.text
                            font.pixelSize: Theme.fsLg
                            font.weight: Theme.fwHeading
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: I18n.t("week.summary").arg(I18n.deadlines(root.totalTasks())).arg(I18n.events(root.totalEvents()))
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.features: Theme.tabularNums
                    font.pixelSize: Theme.fsSm
                }
                PillButton {
                    objectName: "week-rail-toggle"
                    visible: gridHost.railFits
                    text: I18n.t("week.rail.toggle")
                    selected: root.railWanted
                    onClicked: root.railWanted = !root.railWanted
                }
                PillButton {
                    id: todayBtn
                    text: I18n.t("common.today")
                    ToolTip.visible: todayBtn.hovered || todayBtn.visualFocus
                    ToolTip.delay: 500
                    ToolTip.text: I18n.t("common.today") + "  " + AppController.shortcutFor("cal.today")
                    onClicked: AppController.selectedDate = AppController.today
                }
                PillButton {
                    id: nextWeekBtn
                    objectName: "week-next"
                    text: "→"
                    Accessible.name: I18n.t("miniweek.nextWeek")
                    ToolTip.visible: nextWeekBtn.hovered || nextWeekBtn.visualFocus
                    ToolTip.delay: 500
                    ToolTip.text: I18n.t("miniweek.nextWeek") + "  " + AppController.shortcutFor("cal.next")
                    onClicked: root.step(1)
                }
            }
        }

        // 7-column grid
        Item {
            id: gridHost
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Header, gutter and chip rows grow with the interface scale
            // (APP-183): at 150 % the fixed 60px header put the date on top
            // of the weekday and the gutter cut "10:00" to "0:00".
            readonly property int gutterW: root.dayZoom || Style.fills ? Theme.px(48) : Theme.px(40)
            readonly property int dayCount: Math.max(1, root.days.length)
            // Narrowest a day column may get before the rail has to give way.
            // Scaled (A6-11): at 150% the rail left 105 px days, every
            // title cut.
            readonly property int minDayW: root.minDayW
            // The rail takes its width off the grid rather than overlapping it,
            // and folds away whenever keeping it would squeeze the days below
            // minDayW. The old test was a fixed 900px, while dayW refused to go
            // under 120 — so at 1400px the week overflowed its own view and the
            // weekend was cut off behind the rail.
            readonly property bool railFits: root.chrome && (width - gutterW - 240) / dayCount >= minDayW
            readonly property bool railVisible: root.chrome && root.railWanted && railFits
            readonly property int railW: railVisible ? 240 : 0
            // Never wider than the space there is: every day stays on screen.
            readonly property int dayW: Math.max(40, Math.floor((width - gutterW - railW) / dayCount))
            // The deadline row (DG-041): a flag and a title per line, as tall
            // as the busiest day needs. The day zoom says them in its header
            // line instead (DG-051), so it has no row of its own.
            readonly property int dueLineH: Theme.px(18)
            // Deadline lines per day before "+N": four, fewer when they would
            // take more than a fifth of the height (A6-11): at 150% the
            // header ate half the window and the grid showed four hours.
            readonly property int dueMaxLines: Math.max(1, Math.min(4, Math.floor((height * 0.2 - 2 * Theme.spXs - dueLineH) / (dueLineH + Theme.sp2xs))))
            readonly property int dueRowH: {
                if (root.dayZoom) return 0;
                const floor = Style.fills ? Theme.px(30) : Theme.px(22);
                let most = 0;
                for (let i = 0; i < root.days.length; i++) {
                    const n = root.days[i].tasks.length;
                    if (n > 0) most = Math.max(most, 2 * Theme.spXs + Math.min(dueMaxLines, n) * (dueLineH + Theme.sp2xs) + (n > dueMaxLines ? dueLineH : 0));
                }
                return Math.max(floor, most);
            }
            // The day header (DG-042): one compact line, "пн 5".
            readonly property int headH: root.dayZoom ? Theme.px(36) : (Style.fills ? Theme.px(38) : Theme.px(26))

            // Sticky header band for the day-header + due-chips area
            Rectangle {
                id: headerBand
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                anchors.rightMargin: gridHost.railW
                height: gridHost.headH + gridHost.dueRowH
                color: Theme.bg
                z: 2

                // The hairline under the day names (bold, H2-Calendar).
                Rectangle {
                    visible: Style.fills && !root.dayZoom
                    x: gridHost.gutterW
                    y: gridHost.headH - 1
                    width: parent.width - gridHost.gutterW
                    height: 1
                    color: Theme.border
                }

                Repeater {
                    model: root.days
                    delegate: Item {
                        id: headCol
                        objectName: "wvHeadCol"
                        required property var modelData
                        required property int index
                        x: root.dayZoom ? 0 : gridHost.gutterW + index * gridHost.dayW
                        y: 0
                        width: root.dayZoom ? headerBand.width : gridHost.dayW
                        height: headerBand.height
                        readonly property bool isToday: root.isSameDay(modelData.date, AppController.today)
                        // The cursor on a day with nothing on it (APP-276).
                        FocusRing {
                            objectName: "week-cursor-day"
                            anchors.margins: 1
                            haloInside: true
                            visible: root.cursorVisible && root.cursorKey === "" && root.cursorDay === headCol.index
                        }

                        // Today (DG-043): the bold style draws an orange line
                        // on top of its column and tints it; the quiet one
                        // only brightens the name.
                        Rectangle {
                            objectName: "week-today-bar"
                            visible: headCol.isToday && Style.fills && !root.dayZoom
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            height: 2
                            color: Theme.signalNow
                        }
                        // "пн 5" (DG-041); the day zoom reads "пт, 9 октября"
                        // and its facts (DG-051). The day's load (APP-247) is
                        // the tooltip of the line: the sheets draw no bar.
                        readonly property var load: root.loadOf(headCol.modelData.date)
                        Item {
                            id: headInfo
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            height: gridHost.headH
                            clip: true
                            MouseArea {
                                id: headMA
                                objectName: "week-load-" + headCol.index
                                // The day's load (APP-247), said on hover and
                                // while the keyboard cursor is in the day
                                // (DG-041: a hover-only fact fails the keyboard).
                                readonly property string loadText: root.loadLong(headCol.load)
                                readonly property bool cursorHere: root.cursorVisible && root.cursorDay === headCol.index
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: AppController.selectedDate = headCol.modelData.date
                                // Below the day header, inside the grid: above
                                // it, it covered the День/Неделя/Месяц switch
                                // (EYES-7).
                                ToolTip {
                                    objectName: "week-load-tip"
                                    visible: (headMA.containsMouse || headMA.cursorHere) && headMA.loadText.length > 0
                                    delay: 400
                                    text: headMA.loadText
                                    y: headMA.height + Theme.spXs
                                    // Kept over the grid (A1-22): centred on a narrow
                                    // first column it ran over the sidebar.
                                    x: {
                                        const centred = (headMA.width - implicitWidth) / 2;
                                        if (!visible || !gridHost) return centred;
                                        const colX = headMA.mapToItem(gridHost, 0, 0).x;
                                        return Math.max(gridHost.gutterW - colX, Math.min(centred, gridHost.width - colX - implicitWidth));
                                    }
                                }
                            }
                            Row {
                                id: headLine
                                objectName: "week-head-" + headCol.index
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: root.dayZoom ? gridHost.gutterW : Theme.spSm
                                spacing: Theme.spXs
                                // Bold: a small grey day name and a large
                                // number; quiet: one small line.
                                Text {
                                    objectName: "week-head-dow"
                                    anchors.baseline: dayNumber.baseline
                                    // "пт, 9 октября" (X-Oth-DayMonth).
                                    // Lower case is Russian usage; English keeps "Mon" (A2-16).
                                    text: root.dayZoom ? root._dow(headCol.modelData.date) + ", " + I18n.fmtDate(headCol.modelData.date, "longDay")
                                        : root.dowLabelsByJsDow[headCol.modelData.date.getDay()].toLowerCase()
                                          + (Style.fills ? "" : " " + headCol.modelData.date.getDate())
                                    color: root.dayZoom || (headCol.isToday && !Style.fills) ? Theme.text : Theme.textDim
                                    font.family: Theme.fontUi
                                    font.pixelSize: root.dayZoom ? Theme.fsLg : (Style.fills ? Theme.fsXs : Theme.fsSm)
                                    font.weight: root.dayZoom || (headCol.isToday && !Style.fills) ? Theme.fwTitle : Theme.fwBody
                                }
                                Text {
                                    id: dayNumber
                                    visible: !root.dayZoom && Style.fills
                                    text: headCol.modelData.date.getDate()
                                    color: Theme.text
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsLg
                                    font.weight: Theme.fwHeading
                                }
                                // "2 встречи · 1 задача · срок:" — the deadlines
                                // follow as links (DG-051).
                                Text {
                                    objectName: "week-day-facts"
                                    visible: root.dayZoom && text.length > 0
                                    anchors.baseline: dayNumber.baseline
                                    leftPadding: Theme.spMd
                                    text: root.dayFacts(headCol.modelData)
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsMd
                                }
                            }
                        }

                        // The deadlines: under the day names in a week, after
                        // the facts in a day.
                        Column {
                            x: root.dayZoom ? headLine.x + headLine.width + Theme.spXs : 0
                            y: root.dayZoom ? Math.round((gridHost.headH - gridHost.dueLineH) / 2) : gridHost.headH + Theme.spXs
                            width: headCol.width - x
                            spacing: Theme.sp2xs
                            clip: true

                            Flow {
                                id: dueFlow
                                width: parent.width
                                spacing: root.dayZoom ? Theme.spMd : Theme.sp2xs
                                Repeater {
                                    model: headCol.modelData.tasks.slice(0, root.dayZoom ? 3 : gridHost.dueMaxLines)
                                    delegate: Rectangle {
                                        id: dueChip
                                        required property var modelData
                                        // A repeat ahead (A2-25): faint, read-only.
                                        readonly property bool ghost: !!modelData.ghost
                                        readonly property bool _selected: !ghost && AppController.selectionCount >= 0
                                            && AppController.isTaskSelected(modelData.id)
                                        width: root.dayZoom ? dueLine.implicitWidth + 2 * Theme.spXs : dueFlow.width
                                        height: gridHost.dueLineH
                                        radius: Theme.radiusSm
                                        // A deadline is a flag in the row, not
                                        // a box (APP-264): fill only to say it
                                        // is selected or under the pointer.
                                        color: _selected ? Theme.withAlpha(Theme.accent, 0.18)
                                            : chipMA.containsMouse ? Theme.panel2 : "transparent"
                                        border.color: _selected ? Theme.accent : "transparent"
                                        border.width: _selected ? 2 : 0
                                        // A deadline is orange in bold and grey
                                        // in quiet; a planned day is a ring.
                                        readonly property color ink: ghost ? Theme.textDim
                                            : modelData.scheduled ? Theme.textMuted
                                            : Style.urgency ? Theme.signalNow : Theme.textMuted

                                        Row {
                                            id: dueLine
                                            anchors.left: parent.left; anchors.leftMargin: Theme.spXs
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: root.dayZoom ? implicitWidth : parent.width - 2 * Theme.spXs
                                            spacing: Theme.spXs
                                            Icon {
                                                anchors.verticalCenter: parent.verticalCenter
                                                visible: !dueChip.modelData.scheduled && !dueChip.ghost
                                                name: "flag"
                                                size: Theme.px(10)
                                                color: dueChip.ink
                                            }
                                            StatusRing {
                                                anchors.verticalCenter: parent.verticalCenter
                                                visible: !!dueChip.modelData.scheduled && !dueChip.ghost
                                                category: AppController.statusCategory(dueChip.modelData.status || "")
                                                size: Theme.px(10)
                                            }
                                            // The repeat mark the card and Today use.
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                visible: dueChip.ghost
                                                width: Theme.px(10)
                                                horizontalAlignment: Text.AlignHCenter
                                                text: "↻"
                                                color: dueChip.ink
                                                font.family: Theme.fontUi
                                                font.pixelSize: root.dayZoom ? Theme.fsSm : Theme.fsXs
                                            }
                                            // The title's natural width, read off a metric and not
                                            // the Text's own implicitWidth: an elided Text redoes
                                            // its implicitWidth when its width changes, a binding
                                            // loop on every day view (b-01).
                                            TextMetrics {
                                                id: dueTitleMetrics
                                                font: dueTitle.font
                                                text: dueTitle.text
                                            }
                                            Text {
                                                id: dueTitle
                                                // A day's chips share the line and elide (A2-16): a
                                                // long title ran past the edge cut mid-word.
                                                width: root.dayZoom ? Math.min(Math.ceil(dueTitleMetrics.advanceWidth), Math.max(Theme.px(60), dueFlow.width / Math.max(1, Math.min(3, headCol.modelData.tasks.length)) - Theme.px(10) - 3 * Theme.spXs - Theme.spMd))
                                                                    : dueLine.width - Theme.px(10) - Theme.spXs
                                                objectName: dueChip.ghost ? "week-ghost-title" : "week-due-title"
                                                text: dueChip.ghost ? root.ghostTitle(dueChip.modelData) : dueChip.modelData.title
                                                textFormat: Text.PlainText
                                                color: dueChip.ink
                                                font.family: Theme.fontUi
                                                font.pixelSize: root.dayZoom ? Theme.fsMd : Theme.fsXs
                                                elide: Text.ElideRight
                                            }
                                        }
                                        FocusRing {
                                            objectName: "week-cursor"
                                            visible: root.cursorVisible && root.cursorDay === headCol.index
                                                     && root.cursorKey === (dueChip.ghost ? "ghost:" + dueChip.modelData.id + "@" + headCol.index
                                                                                          : "task:" + dueChip.modelData.id)
                                        }
                                        // The keyboard's way in (design audit
                                        // DES-19). Under the MouseArea and deaf
                                        // to the pointer: a click still goes
                                        // there, with its Ctrl / Shift.
                                        ClickArea {
                                            id: dueKey
                                            objectName: (dueChip.ghost ? "week-ghost-" : "week-due-") + dueChip.modelData.id
                                            label: dueChip.ghost ? root.ghostTip(dueChip.modelData) : dueChip.modelData.title
                                            showTip: false
                                            acceptedButtons: Qt.NoButton
                                            onActivated: chipMA.open()
                                            // The move keys act on it (APP-249); never
                                            // on a repeat ahead, which is not a task.
                                            onActiveFocusChanged: if (activeFocus && !dueChip.ghost) {
                                                root.keyTaskId = dueChip.modelData.id;
                                                root.keyTaskDay = headCol.modelData.date;
                                            }
                                        }
                                        // The task's menu, the same in every view (APP-268).
                                        TapHandler {
                                            enabled: !dueChip.ghost
                                            acceptedButtons: Qt.RightButton
                                            onTapped: root.openTaskMenu(dueChip.modelData.id)
                                        }
                                        MouseArea {
                                            id: chipMA
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: root.chipDrag ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                            acceptedButtons: Qt.LeftButton
                                            // Dragged to another day or onto the
                                            // hour grid (APP-249).
                                            preventStealing: true
                                            property real pressX: 0
                                            property real pressY: 0
                                            // Esc ended this press's drag.
                                            property bool inert: false
                                            onPressed: (mouse) => { pressX = mouse.x; pressY = mouse.y; inert = false; }
                                            onPositionChanged: (mouse) => {
                                                if (!pressed || inert || dueChip.ghost) return;
                                                if (!root.chipDrag) {
                                                    if (Math.abs(mouse.x - pressX) < 6 && Math.abs(mouse.y - pressY) < 6) return;
                                                    root.beginChipDrag(dueChip.modelData);
                                                }
                                                const p = chipMA.mapToItem(gridHost, mouse.x, mouse.y);
                                                root.moveChipDrag(p.x, p.y);
                                            }
                                            onReleased: {
                                                if (root.chipDrag) { inert = true; root.endChipDrag(); }
                                            }
                                            onCanceled: if (root.chipDrag) { root.chipDrag = null; dragLayer.finish(); }
                                            onContainsMouseChanged: {
                                                if (dueChip.ghost) return;
                                                if (containsMouse) { root.hoverTaskId = dueChip.modelData.id; root.hoverTaskDay = headCol.modelData.date; }
                                                else if (root.hoverTaskId === dueChip.modelData.id) root.hoverTaskId = "";
                                            }
                                            Connections {
                                                target: dragLayer
                                                function onCanceled() { if (chipMA.pressed) chipMA.inert = true; }
                                            }
                                            // A plain click, and Return on the
                                            // ClickArea above.
                                            function open() {
                                                if (AppController.selectionCount > 0) AppController.clearSelection();
                                                root.taskClicked(modelData.id);
                                            }
                                            onClicked: (mouse) => {
                                                if (inert) { inert = false; return; }
                                                const ctrl = (mouse.modifiers & Qt.ControlModifier) !== 0;
                                                const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0;
                                                if (dueChip.ghost) {
                                                    chipMA.open();
                                                } else if (ctrl) {
                                                    AppController.toggleTaskSelection(modelData.id);
                                                } else if (shift) {
                                                    root._rangeSelect(modelData.id);
                                                } else {
                                                    chipMA.open();
                                                }
                                            }
                                            // The full title on Tab too (APP-184).
                                            ToolTip.visible: containsMouse || dueKey.activeFocus
                                            ToolTip.delay: 400
                                            ToolTip.text: dueChip.ghost ? root.ghostTip(modelData) : modelData.title
                                        }
                                    }
                                }
                            }
                            // The overflow count selects the day.
                            Text {
                                readonly property int _shown: root.dayZoom ? 3 : gridHost.dueMaxLines
                                visible: headCol.modelData.tasks.length > _shown
                                leftPadding: Theme.spXs
                                text: I18n.t("cal.moreN").arg(headCol.modelData.tasks.length - _shown)
                                color: moreMA.hovered ? Theme.text : Theme.textDim
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.family: Theme.fontUi
                                font.features: Theme.tabularNums
                                font.pixelSize: Theme.fsXs
                                ClickArea {
                                    id: moreMA
                                    label: I18n.t("cal.moreOnDay").arg(headCol.modelData.tasks.length - parent._shown)
                                    onActivated: AppController.selectedDate = headCol.modelData.date
                                }
                            }
                        }
                    }
                }
            }

            // All-day events. No hours, so no place on the grid; a strip under
            // the header is where every calendar puts them, and it keeps them
            // visible however far the grid is scrolled.
            Rectangle {
                id: weekStrip
                objectName: "allday-strip"
                anchors.top: headerBand.bottom
                anchors.left: parent.left; anchors.right: parent.right
                anchors.rightMargin: gridHost.railW
                height: visible ? (root.strip.rows * Theme.px(24) + 8) : 0
                visible: root.strip.rows > 0
                color: Theme.panel
                z: 2

                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 1; color: Theme.border
                }

                Repeater {
                    model: root.strip.bars
                    Rectangle {
                        id: weekBar
                        required property var modelData
                        objectName: "allday-" + weekBar.modelData.id
                        x: gridHost.gutterW + weekBar.modelData.from * gridHost.dayW + 2
                        y: 4 + weekBar.modelData.row * Theme.px(24)
                        width: weekBar.modelData.span * gridHost.dayW - 4
                        height: Theme.px(22)
                        // Square off the clipped end so a bar that runs past
                        // the week reads as continuing rather than ending here.
                        radius: Theme.radiusSm
                        color: Theme.withAlpha(Theme.eventColor(weekBar.modelData.type || "sync"), 0.16)

                        Rectangle {
                            visible: !weekBar.modelData.clippedStart
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: 3; radius: 1
                            color: Theme.eventColor(weekBar.modelData.type || "sync")
                        }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: weekBar.modelData.clippedStart ? 16 : 10
                            anchors.rightMargin: Theme.spMd
                            verticalAlignment: Text.AlignVCenter
                            // A bar continued from last week says so, so the
                            // title is not read as starting on Monday.
                            text: (weekBar.modelData.clippedStart ? "‹ " : "")
                                  + (weekBar.modelData.title || "")
                                  + (weekBar.modelData.clippedEnd ? " ›" : "")
                            color: Theme.text
                            font.pixelSize: Theme.fsMd
                            elide: Text.ElideRight
                        }

                        ClickArea {
                            label: weekBar.modelData.title || ""
                            showTip: false
                            onActivated: root.eventClicked(weekBar.modelData.id, weekBar.modelData.occ)
                        }
                    }
                }
            }

            // What still needs a slot, beside the grid that has the slots.
            UnscheduledRail {
                id: unscheduledRail
                objectName: "unscheduled-rail"
                visible: gridHost.railVisible
                anchors.top: parent.top; anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: gridHost.railW
                // From the week's dates alone: read off `root.days`, every
                // task change handed the rail a new array and it rebuilt
                // twice, once for the change and once for the array (APP-203).
                days: {
                    const out = [];
                    const ev = root.eventDays.days;
                    for (let i = 0; i < ev.length; i++) out.push(ev[i].date);
                    return out;
                }
                searchText: root.searchText
                taskRev: root.taskRev
                eventRev: root.eventRev
                onTaskClicked: (id) => root.taskClicked(id)
                z: 3
            }

            // Scrollable hour grid
            ScrollView {
                id: hourScroll
                objectName: "week-hour-scroll"
                // Open on the working day (or the current hour, if that is
                // earlier), not on midnight.
                Component.onCompleted: workdayScroll.start()
                anchors.left: parent.left; anchors.right: parent.right
                anchors.rightMargin: gridHost.railW
                anchors.top: weekStrip.bottom; anchors.bottom: parent.bottom
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                // Said outright: the grid has a height but no implicit one,
                // so the ScrollView's own guess was 0 and the first scroll to
                // the working day had nothing to scroll.
                contentWidth: gridContent.width
                contentHeight: gridContent.height

                Item {
                    id: gridContent
                    width: gridHost.width - gridHost.railW
                    height: (root.hoursEnd - root.hoursStart) * root.hourH + 4

                    // Hour-label gutter
                    Item {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: gridHost.gutterW

                        Repeater {
                            model: root.hoursEnd - root.hoursStart
                            Text {
                                required property int index
                                x: 0
                                // Centred on its hour line, but never above the
                                // top of the scroll area — the first label used
                                // to be cut in half by the sticky header. The
                                // same for the hour the grid is scrolled to:
                                // its label stays whole under the day header
                                // (Q-Calendar, R4-044/R4-050).
                                readonly property real _line: index * root.hourH
                                readonly property real _top: hourScroll.contentItem ? hourScroll.contentItem.contentY : 0
                                y: _line >= _top ? Math.max(_top, _line - 6) : Math.max(0, _line - 6)
                                width: gridHost.gutterW - 8
                                horizontalAlignment: Text.AlignRight
                                text: root.hourLabel(root.hoursStart + index)
                                color: Theme.textDim
                                // The UI font: the mono zero read as 8 (A2-27).
                                font.family: Theme.fontUi
                                font.features: Theme.tabularNums
                                font.pixelSize: Theme.fsXs
                            }
                        }
                    }

                    // 7 day columns
                    Repeater {
                        model: root.days
                        delegate: Item {
                            id: dayCol
                            required property var modelData
                            required property int index
                            x: gridHost.gutterW + index * gridHost.dayW
                            y: 0
                            width: gridHost.dayW
                            height: (root.hoursEnd - root.hoursStart) * root.hourH
                            readonly property bool isToday: root.isSameDay(modelData.date, AppController.today)

                            // Hairline columns (H2/Q-Calendar); the bold
                            // style tints today's column, the quiet one not.
                            Rectangle {
                                anchors.fill: parent
                                color: dayCol.isToday && Style.fills && !root.dayZoom ? Theme.withAlpha(Theme.text, 0.025) : "transparent"
                                Rectangle {
                                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                    width: 1
                                    color: Theme.border
                                    opacity: 0.7
                                }
                            }

                            // Outside the working day. Dimmed, not missing.
                            Repeater {
                                model: root.hoursEnd - root.hoursStart
                                Rectangle {
                                    required property int index
                                    visible: index < root.workStart || index >= root.workEnd
                                    // The column by id: under Bound a Repeater's
                                    // row is built before it has a parent.
                                    width: dayCol.width
                                    y: index * root.hourH
                                    height: root.hourH
                                    color: Theme.bg2
                                    opacity: 0.55
                                }
                            }

                            // Lunch (APP-319), on a working day: not free
                            // time, so it reads like the hours outside work,
                            // with its name.
                            Rectangle {
                                objectName: "week-lunch-band"
                                readonly property var span: AppController.lunch
                                readonly property var load: root.loadOf(dayCol.modelData.date)
                                readonly property bool shown: span.start !== undefined && !!load && !!load.workday
                                visible: shown
                                y: shown ? (Number(span.start) - root.hoursStart) * root.hourH : 0
                                width: dayCol.width
                                height: shown ? (Number(span.end) - Number(span.start)) * root.hourH : 0
                                color: Theme.withAlpha(Theme.bg2, 0.55)
                                Text {
                                    visible: parent.height >= font.pixelSize + 2 * Theme.spXs
                                    anchors.left: parent.left; anchors.top: parent.top
                                    anchors.leftMargin: Theme.spSm; anchors.topMargin: Theme.spXs
                                    text: I18n.t("cal.lunch")
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fsXs
                                }
                            }

                            // Hour separators
                            Repeater {
                                model: root.hoursEnd - root.hoursStart
                                Rectangle {
                                    required property int index
                                    width: dayCol.width
                                    y: index * root.hourH
                                    height: 1
                                    color: Theme.border
                                    opacity: 0.5
                                }
                            }

                            // Drop a card from the board onto an hour to
                            // time-block it. The day grid had this; the week
                            // is where a week's worth of planning happens.
                            DropArea {
                                objectName: "week-drop-" + dayCol.index
                                anchors.fill: parent
                                property real hoverY: -1
                                // The dragged task's block as it would land:
                                // the snapped hour and the estimate's length
                                // (X/N-Oth-Select-Drag, R3-054).
                                property string dragTitle: ""
                                property int dragMinutes: 0
                                readonly property real ghostStart: hoverY < 0 ? 0
                                    : Math.min(root.clampHour(root.snapHour(root.yToHour(hoverY))), 24 - Theme.minEventHours)
                                readonly property real ghostLen: (dragMinutes > 0 ? dragMinutes : 60) / 60
                                onEntered: (drag) => {
                                    const src = drag.source;
                                    const t = src && src.taskId ? AppController.taskById(String(src.taskId)) : null;
                                    dragTitle = t ? String(t.title || "") : "";
                                    dragMinutes = t ? Number(t.estimateMinutes || 0) : 0;
                                    hoverY = drag.y;
                                }
                                onPositionChanged: (drag) => hoverY = drag.y
                                onExited: hoverY = -1
                                onDropped: (drop) => {
                                    hoverY = -1;
                                    const src = drop.source;
                                    if (!src || !src.taskId) return;
                                    const h = root.clampHour(root.snapHour(root.yToHour(drop.y)));
                                    // From the "Without a date" tray: the task
                                    // is planned for that hour, no extra event.
                                    if (src.plainSchedule === true)
                                        AppController.rescheduleTask(String(src.taskId), "scheduled",
                                                                     Resched.atHour(dayCol.modelData.date, Math.min(h, 24 - Theme.minEventHours)), true);
                                    else
                                        AppController.scheduleTask(String(src.taskId), h, dayCol.modelData.date);
                                    drop.accept(Qt.MoveAction);
                                }
                                DashedRect {
                                    id: dropGhost
                                    objectName: "week-drop-ghost"
                                    visible: parent.hoverY >= 0
                                    z: 6
                                    x: 2; width: parent.width - 4
                                    y: (parent.ghostStart - root.hoursStart) * root.hourH
                                    height: Math.max(Theme.px(22), parent.ghostLen * root.hourH - 2)
                                    radius: Theme.radiusSm
                                    strokeColor: Style.urgency ? Theme.info : Theme.borderStrong
                                    fillColor: Theme.withAlpha(Theme.surfaceCard, 0.6)
                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: Theme.spSm
                                        spacing: Theme.sp2xs
                                        clip: true
                                        Text {
                                            width: parent.width
                                            text: dropGhost.parent.dragTitle
                                            textFormat: Text.PlainText
                                            color: Theme.text
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsSm
                                            font.weight: Theme.fwTitle
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            width: parent.width
                                            readonly property var dz: dropGhost.parent
                                            text: Theme.fmtHour(dz.ghostStart) + "–" + Theme.fmtHour(Math.min(24, dz.ghostStart + dz.ghostLen))
                                                  + (dz.dragMinutes > 0 ? " · " + I18n.t("week.drop.byEstimate").arg(I18n.fmtMinutes(dz.dragMinutes)) : "")
                                            color: Theme.textMuted
                                            font.family: Theme.fontUi
                                            font.features: Theme.tabularNums
                                            font.pixelSize: Theme.fsXs
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            // Click an empty slot to add something there. It
                            // opens the editor rather than saving an untitled
                            // event, so the user names it before it exists.
                            //
                            // A drag on an empty stretch makes a meeting that
                            // long (as the day panel always could); a task
                            // picked in the tray is planned at the slot
                            // instead. Under the task blocks (4) and the
                            // events (5), like the day grid's z-order.
                            MouseArea {
                                id: createArea
                                objectName: "week-create-" + dayCol.index
                                anchors.fill: parent
                                z: 1
                                // Right: the empty slot's menu (DG-045).
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                preventStealing: true
                                property real pressY: -1
                                property real currentY: -1
                                property bool dragging: false
                                onPressed: (mouse) => {
                                    if (mouse.button === Qt.RightButton) {
                                        root.openSlotMenu(dayCol.modelData.date,
                                                          root.clampHour(root.snapHour(root.yToHour(mouse.y))));
                                        return;
                                    }
                                    pressY = mouse.y; currentY = mouse.y; dragging = false;
                                }
                                onPositionChanged: (mouse) => {
                                    if (pressY < 0) return;
                                    currentY = mouse.y;
                                    if (!dragging && !root.armedTaskId && Math.abs(currentY - pressY) >= 5) {
                                        dragging = true;
                                        root._activeDrag = createArea;
                                    }
                                }
                                function cancel() { pressY = -1; currentY = -1; dragging = false; }
                                onReleased: {
                                    if (pressY < 0) return;
                                    const h = root.clampHour(root.snapHour(root.yToHour(pressY)));
                                    const startH = root.snapHour(root.yToHour(Math.min(pressY, currentY)));
                                    const endH = root.snapHour(root.yToHour(Math.max(pressY, currentY)));
                                    if (root.armedTaskId) {
                                        AppController.rescheduleTask(root.armedTaskId, "scheduled",
                                                                     Resched.atHour(dayCol.modelData.date, Math.min(h, 24 - Theme.minEventHours)), true);
                                        root.armedUsed();
                                    } else if (dragging && endH - startH >= Theme.minEventHours) {
                                        root.createRangeRequested(startH, Math.min(endH, 24), dayCol.modelData.date);
                                    } else {
                                        root.createRequested(h, dayCol.modelData.date);
                                    }
                                    pressY = -1; currentY = -1; dragging = false;
                                    if (root._activeDrag === createArea) root._activeDrag = null;
                                }
                                onCanceled: { pressY = -1; currentY = -1; dragging = false; if (root._activeDrag === createArea) root._activeDrag = null; }
                            }
                            Rectangle {
                                objectName: "week-create-ghost"
                                visible: createArea.dragging
                                y: Math.min(createArea.pressY, createArea.currentY)
                                x: 2; width: parent.width - 4
                                height: Math.abs(createArea.currentY - createArea.pressY)
                                color: Theme.withAlpha(Theme.accent, 0.18)
                                border.color: Theme.accent
                                border.width: 1
                                radius: Theme.radiusSm
                                z: 10
                                Text {
                                    anchors.centerIn: parent
                                    text: Theme.fmtHour(root.snapHour(root.yToHour(Math.min(createArea.pressY, createArea.currentY))))
                                          + " – " + Theme.fmtHour(root.snapHour(root.yToHour(Math.max(createArea.pressY, createArea.currentY))))
                                    color: Theme.text
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsSm
                                }
                            }

                            // Where the day is now. Only on today, and only
                            // the column it belongs to.
                            Rectangle {
                                visible: dayCol.isToday
                                anchors.left: parent.left; anchors.right: parent.right
                                // An orange 2 px line in bold, a grey hairline
                                // in quiet (H2 / Q-Calendar).
                                objectName: "week-now-line"
                                y: (root.now.getHours() + root.now.getMinutes() / 60 - root.hoursStart) * root.hourH
                                height: Style.urgency ? 2 : 1
                                color: Style.urgency ? Theme.signalNow : Theme.borderStrong
                                z: 9
                            }
                        }
                    }

                    // Flat interactive events layer — declared after the day
                    // columns so it sits on top, and uses absolute coords
                    // (dayIndex × dayW) to allow drag between columns.
                    Repeater {
                        model: root.flatEvents
                        delegate: Rectangle {
                            id: weEv
                            required property var modelData

                            property real dragDx: 0
                            property real dragDy: 0
                            property real pendingStartH: NaN
                            property real pendingEndH:   NaN

                            readonly property int effDayIndex: {
                                const raw = modelData.dayIndex + Math.round(dragDx / gridHost.dayW);
                                return Math.max(0, Math.min(root.days.length - 1, raw));
                            }
                            readonly property real effStart: !isNaN(pendingStartH) ? pendingStartH : modelData.start
                            readonly property real effEnd:   !isNaN(pendingEndH)   ? pendingEndH   : modelData.end

                            // Its slot within the day's overlap cluster. A
                            // dragged event goes full width: it is following
                            // the pointer, not sitting in a cluster any more.
                            readonly property var _slot: root.overlaps[modelData.key] || ({ col: 0, cols: 1 })
                            readonly property int _cols: (dragDx !== 0 || dragDy !== 0) ? 1 : Math.max(1, _slot.cols)
                            readonly property int _col:  (dragDx !== 0 || dragDy !== 0) ? 0 : _slot.col
                            // Tiled, or cascaded once lanes would get
                            // narrower than a readable title (VISU-15).
                            readonly property var _lane: Overlap.lane(weEv._col, weEv._cols, root.laneAvail, root.minLaneW, 1)
                            // Folded onto "+N", drawn only under the cursor.
                            readonly property bool _cursorOn: root.cursorVisible && root.cursorKey === root._evKey(weEv.modelData)
                            visible: !weEv._lane.folded || weEv._cursorOn

                            x: gridHost.gutterW + weEv.effDayIndex * gridHost.dayW + root.laneInset + weEv._lane.x
                            y: (weEv.effStart - root.hoursStart) * root.hourH + weEv.dragDy
                            width: weEv._lane.w - (weEv._cols > 1 ? 2 : 0)
                            // Half an hour or more is at least two lines high,
                            // the time small on top of the title; anything
                            // shorter is one line (APP-199). A half-hour
                            // meeting used to be 17px, its title clipped away.
                            readonly property var _block: root.eventBlock(effStart, effEnd)
                            height: weEv._block.height
                            readonly property bool compact: !weEv._block.twoLine
                            // A meeting is filled (DG-044): bold adds the
                            // blue bar, quiet keeps a faint ground and the
                            // calendar glyph; the type is not a colour.
                            radius: root.blockRadius
                            color: Theme.meetingFill
                            // Quiet fills a meeting with the card colour: on the
                            // light ground it needs the edge to show its length (EYES-4).
                            border.width: Style.chipFill ? 0 : 1
                            border.color: Theme.cardBorder
                            // A later lane is drawn over an earlier one where
                            // they cascade; a dragged event above them all.
                            z: (weEv.dragDx !== 0 || weEv.dragDy !== 0) || weEv._lane.folded ? 7 : 5 + weEv._col / Math.max(1, weEv._cols)
                            readonly property string _range: Theme.fmtHour(weEv.effStart) + "–" + Theme.fmtHour(weEv.effEnd)
                            // The day's second line: where, else how it repeats.
                            readonly property string _sub: {
                                const o = weEv.modelData.occ || {};
                                // Kept in another zone (A6-4): its hour there.
                                if (String(o.srcTz || "").length > 0)
                                    return Theme.fmtHour(Number(o.srcStart) || 0) + " " + String(o.srcTz).split("/").pop().replace(/_/g, " ");
                                if (String(o.location || "").length > 0) return o.location;
                                if ((weEv.modelData.context || "").length > 0) return weEv.modelData.context;
                                const rule = String(o.rrule || "");
                                return rule.length > 0 ? EventRule.describe(rule, weEv.modelData.date, I18n) : "";
                            }

                            Rectangle {
                                visible: Style.fills
                                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                width: 3
                                color: Theme.meeting
                                radius: 1
                            }
                            // Narrow: no icon, slim padding (F2-REG-3).
                            readonly property bool slim: root.slimBlock(weEv.width,
                                (Style.fills ? Theme.spMd : Theme.spSm + Theme.px(11) + Theme.spXs) + Theme.spSm)
                            Column {
                                anchors.fill: parent
                                anchors.leftMargin: weEv.slim ? (Style.fills ? 3 : 0) + root.slimPadL : (Style.fills ? Theme.spMd : Theme.spSm)
                                anchors.rightMargin: weEv.slim ? root.slimPadR : Theme.spSm
                                anchors.topMargin: weEv.compact ? 0 : (root.dayZoom ? Theme.spXs : Theme.sp2xs)
                                spacing: root.dayZoom ? Theme.sp2xs : 0
                                clip: true
                                // Bold: the start small on top of the title.
                                Text {
                                    visible: Style.fills && !weEv.compact && !root.dayZoom
                                    text: Theme.fmtHour(weEv.effStart)
                                    color: Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsXs
                                }
                                RowLayout {
                                    id: evLine
                                    width: parent.width
                                    height: weEv.compact ? weEv.height : implicitHeight
                                    spacing: Theme.spXs
                                    // On one line the time gives way before
                                    // the title does (SCALE-7).
                                    readonly property bool roomy: evLine.width - evTime.implicitWidth - Theme.spXs >= Theme.px(64)
                                    MeetingIcon {
                                        visible: (!Style.fills || root.dayZoom) && !weEv.slim
                                        size: Theme.px(11)
                                        ink: Style.fills ? Theme.meeting : Theme.textMuted
                                    }
                                    Text {
                                        id: evTime
                                        objectName: "week-event-time"
                                        visible: weEv.compact && evLine.roomy && Style.fills && !root.dayZoom
                                        text: Theme.fmtHour(weEv.effStart)
                                        color: Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.features: Theme.tabularNums
                                        font.pixelSize: Theme.fsXs
                                    }
                                    Text {
                                        objectName: "week-event-title"
                                        Layout.fillWidth: true
                                        text: weEv.modelData.title
                                        color: Theme.text
                                        font.family: Theme.fontUi
                                        font.pixelSize: root.dayZoom ? Theme.fsMd : (weEv.slim ? Theme.fsBlock : Theme.fsSm)
                                        font.weight: Style.fills && !root.dayZoom ? Theme.fwHeading : Theme.fwBody
                                        elide: Text.ElideRight
                                    }
                                    // The day: the range at the right (DG-051).
                                    Text {
                                        objectName: "week-event-range"
                                        visible: root.dayZoom
                                        text: weEv._range
                                        color: Theme.textDim
                                        font.family: Theme.fontUi
                                        font.features: Theme.tabularNums
                                        font.pixelSize: Theme.fsXs
                                    }
                                }
                                // Quiet week: the range under the title. A narrow
                                // lane ends it with "…", not cut by the block's
                                // edge mid-digit (b-18).
                                Text {
                                    objectName: "week-event-range-under"
                                    visible: !Style.fills && !root.dayZoom && !weEv.compact
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: weEv._range
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsXs
                                }
                                Text {
                                    visible: root.dayZoom && !weEv.compact && weEv._sub.length > 0
                                    width: parent.width
                                    text: weEv._sub
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                    elide: Text.ElideRight
                                }
                            }

                            // Opening from the keyboard (design audit DES-19).
                            // Under the drag areas and deaf to the pointer, so
                            // a press still starts a move or a resize.
                            FocusRing {
                                objectName: "week-cursor"
                                visible: root.cursorVisible && root.cursorDay === weEv.modelData.dayIndex
                                         && root.cursorKey === root._evKey(weEv.modelData)
                            }
                            // The meeting's menu (DG-045). A MouseArea, so
                            // the slot under it does not get the click too.
                            MouseArea {
                                objectName: "week-event-menu-area"
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onPressed: (mouse) => { mouse.accepted = true; root.openEventMenu(weEv.modelData); }
                            }
                            ClickArea {
                                objectName: "week-event-open"
                                label: weEv.modelData.title
                                showTip: false
                                acceptedButtons: Qt.NoButton
                                cursorShape: Qt.ArrowCursor
                                onActivated: weMove.open()
                            }

                            // Move-drag — vertical = time, horizontal = day.
                            MouseArea {
                                id: weMove
                                anchors.fill: parent
                                anchors.topMargin: Theme.spSm
                                anchors.bottomMargin: Theme.spSm
                                cursorShape: didDrag ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                // The ScrollView's Flickable took the vertical
                                // drag: a move in time or any resize snapped back.
                                preventStealing: true
                                property real grabX: 0
                                property real grabY: 0
                                property real baseX: 0
                                property real baseY: 0
                                property bool didDrag: false
                                property bool cancelled: false
                                function cancel() { cancelled = true; didDrag = false; weEv.dragDx = 0; weEv.dragDy = 0; }
                                // A click that did not drag, and Return on the
                                // ClickArea below.
                                function open() { root.eventClicked(weEv.modelData.id, weEv.modelData.occ); }

                                onPressed: (mouse) => {
                                    grabX = mouse.x; grabY = mouse.y;
                                    baseX = weEv.x; baseY = weEv.y;
                                    didDrag = false;
                                    cancelled = false;
                                    weEv.dragDx = 0; weEv.dragDy = 0;
                                }
                                onPositionChanged: (mouse) => {
                                    const pt = weMove.mapToItem(gridContent, mouse.x, mouse.y);
                                    const wantX = pt.x - grabX;
                                    // grabY is weMove-local; its origin sits topMargin below the event
                                    // top while pt/baseY are gridContent-absolute. Subtract the inset so
                                    // a still pointer yields dy == 0 (no constant +6px bias). No left
                                    // margin, so wantX needs no such correction.
                                    const wantY = pt.y - grabY - weMove.anchors.topMargin;
                                    const dx = wantX - baseX;
                                    const dy = wantY - baseY;
                                    if (cancelled) return;
                                    if (!didDrag && (Math.abs(dx) > 5 || Math.abs(dy) > 5)) {
                                        didDrag = true;
                                        root._activeDrag = weMove;
                                    }
                                    if (didDrag) {
                                        weEv.dragDx = dx;
                                        weEv.dragDy = dy;
                                    }
                                }
                                onReleased: {
                                    if (root._activeDrag === weMove) root._activeDrag = null;
                                    if (cancelled) { cancelled = false; return; }
                                    if (didDrag) {
                                        // A shift of the whole event by what
                                        // this piece moved: hours, plus whole
                                        // days by date (hidden weekends make
                                        // columns and days differ).
                                        const dur = weEv.modelData.end - weEv.modelData.start;
                                        const newY = (weEv.modelData.start - root.hoursStart) * root.hourH + weEv.dragDy;
                                        let ns = root.snapHour(root.yToHour(newY));
                                        ns = Math.max(root.hoursStart, Math.min(ns, root.hoursEnd - dur));
                                        const dayShift = root._daysBetween(root.days[weEv.modelData.dayIndex].date,
                                                                           root.days[weEv.effDayIndex].date);
                                        didDrag = false;
                                        root._commitMove(weEv.modelData.occ, (ns - weEv.modelData.start) + dayShift * 24,
                                                         () => { if (weEv) { weEv.dragDx = 0; weEv.dragDy = 0; } });
                                        return;
                                    }
                                    weMove.open();
                                    weEv.dragDx = 0; weEv.dragDy = 0;
                                    didDrag = false;
                                }
                                onCanceled: { weEv.dragDx = 0; weEv.dragDy = 0; didDrag = false; if (root._activeDrag === weMove) root._activeDrag = null; }
                            }

                            // Top resize handle.
                            MouseArea {
                                id: weTop
                                // A piece of an overnight event has no edge of
                                // its own on this day, as in the day panel.
                                enabled: weEv.modelData.segFirst && weEv.modelData.segLast
                                visible: enabled
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                height: 6
                                cursorShape: Qt.SizeVerCursor
                                preventStealing: true
                                property bool resizing: false
                                onPressed: { resizing = true; weEv.pendingStartH = weEv.modelData.start; }
                                onPositionChanged: (mouse) => {
                                    if (!resizing) return;
                                    const pt = weTop.mapToItem(gridContent, mouse.x, mouse.y);
                                    const h = root.snapHour(root.yToHour(pt.y));
                                    const clamped = Math.min(h, weEv.modelData.end - Theme.minEventHours);
                                    weEv.pendingStartH = Math.max(root.hoursStart, clamped);
                                }
                                onReleased: {
                                    if (!resizing) return;
                                    resizing = false;
                                    const ns = weEv.pendingStartH;
                                    if (Math.abs(ns - weEv.modelData.start) < 1e-9) { weEv.pendingStartH = NaN; return; }
                                    root._commitResize(weEv.modelData.occ, ns, weEv.modelData.end, () => { if (weEv) weEv.pendingStartH = NaN; });
                                }
                                onCanceled: { resizing = false; weEv.pendingStartH = NaN; }
                            }

                            // Bottom resize handle.
                            MouseArea {
                                id: weBot
                                enabled: weEv.modelData.segFirst && weEv.modelData.segLast
                                visible: enabled
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                height: 6
                                cursorShape: Qt.SizeVerCursor
                                preventStealing: true
                                property bool resizing: false
                                onPressed: { resizing = true; weEv.pendingEndH = weEv.modelData.end; }
                                onPositionChanged: (mouse) => {
                                    if (!resizing) return;
                                    const pt = weBot.mapToItem(gridContent, mouse.x, mouse.y);
                                    const h = root.snapHour(root.yToHour(pt.y));
                                    const clamped = Math.max(h, weEv.modelData.start + Theme.minEventHours);
                                    weEv.pendingEndH = Math.min(root.hoursEnd, clamped);
                                }
                                onReleased: {
                                    if (!resizing) return;
                                    resizing = false;
                                    const ne = weEv.pendingEndH;
                                    if (Math.abs(ne - weEv.modelData.end) < 1e-9) { weEv.pendingEndH = NaN; return; }
                                    root._commitResize(weEv.modelData.occ, weEv.modelData.start, ne, () => { if (weEv) weEv.pendingEndH = NaN; });
                                }
                                onCanceled: { resizing = false; weEv.pendingEndH = NaN; }
                            }
                        }
                    }

                    // Reminders to self (dogfood 2026-10-22): a bell and
                    // "15:00 позвонить в банк" at their moment, no block —
                    // a reminder takes no time of the day. A click opens it.
                    Repeater {
                        model: root.flatReminders
                        delegate: Item {
                            id: wkRemind
                            required property var modelData
                            objectName: "week-reminder-" + wkRemind.modelData.id
                            x: gridHost.gutterW + wkRemind.modelData.dayIndex * gridHost.dayW + root.laneInset
                            y: (wkRemind.modelData.hour - root.hoursStart) * root.hourH
                            width: gridHost.dayW - 2 * root.laneInset
                            height: Theme.px(18)
                            z: 6
                            Row {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.spXs
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.spXs
                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "bell"
                                    size: Theme.px(12)
                                    color: Theme.warning
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - Theme.px(12) - Theme.spXs
                                    elide: Text.ElideRight
                                    text: Theme.fmtHour(wkRemind.modelData.hour) + " " + wkRemind.modelData.title
                                    color: wkRemindCA.hovered ? Theme.text : Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                }
                            }
                            ClickArea {
                                id: wkRemindCA
                                label: I18n.t("today.reminder") + " · " + wkRemind.modelData.title
                                onActivated: root.taskClicked(wkRemind.modelData.id)
                            }
                        }
                    }

                    // Tasks scheduled at a clock time. The week used to show
                    // only deadlines, so a task planned for Thursday 14:00
                    // without one appeared nowhere but the day panel.
                    Repeater {
                        model: root.flatBlocks
                        delegate: Rectangle {
                            id: wkBlock
                            required property var modelData
                            objectName: "week-taskblock-" + wkBlock.modelData.id
                            // Moved and stretched like an event (APP-249): the
                            // drag follows the pointer, the drop sets when the
                            // task is planned, a stretch sets its estimate.
                            property real dragDx: 0
                            property real dragDy: 0
                            property real pendingStartH: NaN
                            property real pendingEndH: NaN
                            readonly property bool moving: dragDx !== 0 || dragDy !== 0
                            readonly property int effDayIndex: Math.max(0, Math.min(root.days.length - 1,
                                wkBlock.modelData.dayIndex + Math.round(wkBlock.dragDx / gridHost.dayW)))
                            readonly property real effStart: !isNaN(pendingStartH) ? pendingStartH : modelData.start
                            readonly property real effEnd: !isNaN(pendingEndH) ? pendingEndH : modelData.end
                            readonly property var _slot: root.overlaps[wkBlock.modelData.key] || ({ col: 0, cols: 1 })
                            readonly property int _cols: wkBlock.moving ? 1 : Math.max(1, wkBlock._slot.cols)
                            readonly property var _lane: Overlap.lane(wkBlock.moving ? 0 : wkBlock._slot.col, wkBlock._cols, root.laneAvail, root.minLaneW, 1)
                            readonly property bool _cursorOn: root.cursorVisible && root.cursorKey === "task:" + wkBlock.modelData.id
                            visible: !wkBlock._lane.folded || wkBlock._cursorOn
                            x: gridHost.gutterW + wkBlock.effDayIndex * gridHost.dayW + root.laneInset + wkBlock._lane.x
                            y: (wkBlock.effStart - root.hoursStart) * root.hourH + wkBlock.dragDy
                            width: wkBlock._lane.w - (wkBlock._cols > 1 ? 2 : 0)
                            height: Math.max(18, (wkBlock.effEnd - wkBlock.effStart) * root.hourH - 2)
                            radius: root.blockRadius
                            // A task with a time is an outline; a meeting is
                            // filled (APP-264). The fill only answers the pointer.
                            // Bold draws the outline bright in the week, quiet
                            // and the day a hairline (N-Oth-DayMonth, R4-047).
                            color: wkBlockMA.hovered || wkMove.containsMouse ? Theme.panel2 : Theme.bg
                            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                            border.color: Style.fills && !root.dayZoom ? Theme.textMuted : Theme.border
                            border.width: Style.fills && !root.dayZoom ? 1.5 : 1
                            readonly property bool oneLine: height < Theme.px(36)
                            readonly property string _range: Theme.fmtHour(wkBlock.effStart) + "–" + Theme.fmtHour(wkBlock.effEnd)
                            // Task blocks sit under events (4 < 5), as in the
                            // day grid; a carried one above everything.
                            // One ladder with the meetings (A2-12): lanes no
                            // longer overlap, so a task beside two meetings is
                            // never under them.
                            z: wkBlock.moving || wkBlock._lane.folded ? 7 : 5 + wkBlock._slot.col / Math.max(1, wkBlock._slot.cols)

                            // Where the block would land, for the hint and the drop.
                            function landing() {
                                const dur = wkBlock.modelData.end - wkBlock.modelData.start;
                                const newY = (wkBlock.modelData.start - root.hoursStart) * root.hourH + wkBlock.dragDy;
                                let ns = root.snapHour(root.yToHour(newY));
                                ns = Math.max(root.hoursStart, Math.min(ns, root.hoursEnd - dur));
                                return { day: root.days[wkBlock.effDayIndex].date, start: ns };
                            }
                            function resetDrag() {
                                wkBlock.dragDx = 0; wkBlock.dragDy = 0;
                                wkBlock.pendingStartH = NaN; wkBlock.pendingEndH = NaN;
                            }
                            function hintAt(mouseItem, mx, my, text) {
                                const p = dragLayer.mapFromItem(mouseItem, mx, my);
                                dragLayer.update(p.x, p.y, text, true);
                            }
                            function rangeText(day, s, e) {
                                return dragLayer.describe("scheduled", Resched.atHour(day, s), true) + "–" + Theme.fmtHour(e);
                            }
                            Connections {
                                target: dragLayer
                                function onCanceled() {
                                    if (!wkMove.pressed && !wkTop.pressed && !wkBot.pressed) return;
                                    wkBlock.resetDrag();
                                    wkMove.inert = true; wkTop.inert = true; wkBot.inert = true;
                                }
                            }

                            // Bold: the start on top of a heavy title;
                            // quiet: the status ring, the title and the range
                            // under it; the day: one row with the range at the
                            // right and the key under it (DG-044, DG-051).
                            // Narrow: no ring, slim padding (F2-REG-2).
                            readonly property bool slim: root.slimBlock(wkBlock.width,
                                2 * Theme.spSm + (Style.fills ? 0 : Theme.px(10) + Theme.spXs))
                            Column {
                                anchors.fill: parent
                                anchors.leftMargin: root.dayZoom ? Theme.spLg : (wkBlock.slim ? root.slimPadL : Theme.spSm)
                                anchors.rightMargin: root.dayZoom ? Theme.spLg : (wkBlock.slim ? root.slimPadR : Theme.spSm)
                                anchors.topMargin: wkBlock.oneLine ? 0 : (root.dayZoom ? Theme.spXs : Theme.sp2xs)
                                spacing: root.dayZoom ? Theme.sp2xs : 0
                                clip: true
                                Text {
                                    id: blkStart
                                    visible: !wkBlock.oneLine && Style.fills && !root.dayZoom
                                    text: Theme.fmtHour(wkBlock.effStart)
                                    color: Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsXs
                                }
                                RowLayout {
                                    width: parent.width
                                    height: wkBlock.oneLine ? wkBlock.height : implicitHeight
                                    spacing: Theme.spXs
                                    StatusRing {
                                        id: blkRing
                                        visible: (!Style.fills || root.dayZoom) && !wkBlock.slim
                                        Layout.alignment: wkBlock.oneLine || root.dayZoom ? Qt.AlignVCenter : Qt.AlignTop
                                        Layout.topMargin: wkBlock.oneLine || root.dayZoom ? 0 : Theme.sp2xs
                                        category: AppController.statusCategory(wkBlock.modelData.status || "")
                                        size: Theme.px(10)
                                    }
                                    // No deadline flag on the block: the
                                    // deadline sits in the flag row above
                                    // the grid (H2-Calendar, R4-043).
                                    Text {
                                        id: blkTitle
                                        objectName: "week-taskblock-title"
                                        Layout.fillWidth: true
                                        // One line when short: the time first
                                        // in bold, never a title cut through.
                                        text: (wkBlock.oneLine && Style.fills && !root.dayZoom ? Theme.fmtHour(wkBlock.effStart) + " " : "")
                                              + (wkBlock.modelData.title || "")
                                        color: Theme.text
                                        font.family: Theme.fontUi
                                        font.pixelSize: root.dayZoom ? Theme.fsMd : (wkBlock.slim ? Theme.fsBlock : Theme.fsSm)
                                        font.weight: Style.fills && !root.dayZoom ? Theme.fwHeading : Theme.fwBody
                                        // Wrapped at words only while every word fits;
                                        // a word wider than the lane was cut through
                                        // with no ellipsis (F3-EYES-8, F2-REG-2).
                                        readonly property bool oneLine: wkBlock.oneLine || root.dayZoom || !Style.fills
                                            || !root.wordsFit(blkTitle.text, blkTitleFm, blkTitle.width)
                                        wrapMode: blkTitle.oneLine ? Text.NoWrap : Text.WordWrap
                                        // As many lines as the block holds whole (up to
                                        // three): a narrow lane wrapped a fourth one
                                        // cut in half by the block's bottom.
                                        maximumLineCount: blkTitle.oneLine ? 1
                                            : Math.max(1, Math.min(3, Math.floor((wkBlock.height - Theme.sp2xs * 2
                                                - (blkStart.visible ? blkStart.height : 0)) / blkTitleFm.lineSpacing)))
                                        elide: Text.ElideRight
                                        FontMetrics { id: blkTitleFm; font: blkTitle.font }
                                    }
                                    Text {
                                        visible: root.dayZoom
                                        text: wkBlock._range
                                        color: Theme.textDim
                                        font.family: Theme.fontUi
                                        font.features: Theme.tabularNums
                                        font.pixelSize: Theme.fsXs
                                    }
                                }
                                // Elided in a narrow lane, as an event's (b-18).
                                Text {
                                    objectName: "week-taskblock-range-under"
                                    visible: !wkBlock.oneLine && !Style.fills && !root.dayZoom
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: wkBlock._range
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsXs
                                }
                                Text {
                                    visible: !wkBlock.oneLine && root.dayZoom
                                    text: wkBlock.modelData.id
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                }
                            }
                            // Opening from the keyboard; under the drag areas
                            // and deaf to the pointer, like an event's.
                            FocusRing {
                                objectName: "week-cursor"
                                visible: root.cursorVisible && root.cursorDay === wkBlock.modelData.dayIndex
                                         && root.cursorKey === "task:" + wkBlock.modelData.id
                            }
                            ClickArea {
                                id: wkBlockMA
                                label: wkBlock.modelData.title || ""
                                showTip: false
                                acceptedButtons: Qt.NoButton
                                cursorShape: Qt.ArrowCursor
                                onActivated: root.taskClicked(wkBlock.modelData.id)
                                onActiveFocusChanged: if (activeFocus) {
                                    root.keyTaskId = wkBlock.modelData.id;
                                    root.keyTaskDay = root.days[wkBlock.modelData.dayIndex].date;
                                }
                            }
                            // Move — vertical = time, horizontal = day.
                            // The task's menu, the same in every view (APP-268).
                            // A MouseArea, so the slot under it stays quiet.
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onPressed: (mouse) => { mouse.accepted = true; root.openBlockMenu(wkBlock.modelData, root.days[wkBlock.modelData.dayIndex].date); }
                            }
                            MouseArea {
                                id: wkMove
                                objectName: "week-taskblock-move"
                                anchors.fill: parent
                                anchors.topMargin: Theme.spSm
                                anchors.bottomMargin: Theme.spSm
                                hoverEnabled: true
                                cursorShape: didDrag ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                preventStealing: true
                                property real grabX: 0
                                property real grabY: 0
                                property real baseX: 0
                                property real baseY: 0
                                property bool didDrag: false
                                property bool inert: false
                                onContainsMouseChanged: {
                                    if (containsMouse) { root.hoverTaskId = wkBlock.modelData.id; root.hoverTaskDay = root.days[wkBlock.modelData.dayIndex].date; }
                                    else if (root.hoverTaskId === wkBlock.modelData.id) root.hoverTaskId = "";
                                }
                                onPressed: (mouse) => {
                                    grabX = mouse.x; grabY = mouse.y;
                                    baseX = wkBlock.x; baseY = wkBlock.y;
                                    didDrag = false; inert = false;
                                    wkBlock.resetDrag();
                                }
                                onPositionChanged: (mouse) => {
                                    if (!pressed || inert) return;
                                    const pt = wkMove.mapToItem(gridContent, mouse.x, mouse.y);
                                    const dx = pt.x - grabX - baseX;
                                    const dy = pt.y - grabY - wkMove.anchors.topMargin - baseY;
                                    if (!didDrag && (Math.abs(dx) > 5 || Math.abs(dy) > 5)) {
                                        didDrag = true;
                                        dragLayer.begin(wkBlock.modelData.id, wkBlock.modelData.title, false);
                                    }
                                    if (!didDrag) return;
                                    wkBlock.dragDx = dx;
                                    wkBlock.dragDy = dy;
                                    const l = wkBlock.landing();
                                    wkBlock.hintAt(wkMove, mouse.x, mouse.y, dragLayer.describe("scheduled", Resched.atHour(l.day, l.start), true));
                                }
                                onReleased: {
                                    if (inert) return;
                                    if (didDrag) {
                                        const l = wkBlock.landing();
                                        didDrag = false;
                                        dragLayer.finish();
                                        const changed = AppController.rescheduleTask(wkBlock.modelData.id, "scheduled", Resched.atHour(l.day, l.start), true);
                                        if (!changed && wkBlock) wkBlock.resetDrag();
                                        return;
                                    }
                                    root.taskClicked(wkBlock.modelData.id);
                                }
                                onCanceled: { wkBlock.resetDrag(); didDrag = false; dragLayer.finish(); }
                            }
                            // Top edge: the start moves, the end stays.
                            MouseArea {
                                id: wkTop
                                objectName: "week-taskblock-top"
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                height: Theme.spSm
                                cursorShape: Qt.SizeVerCursor
                                preventStealing: true
                                property bool inert: false
                                // By how far the pointer moved, not where it is:
                                // the handle sits a few pixels inside the edge.
                                property real pressY: 0
                                onPressed: (mouse) => {
                                    inert = false;
                                    pressY = wkTop.mapToItem(gridContent, mouse.x, mouse.y).y;
                                    wkBlock.pendingStartH = wkBlock.modelData.start;
                                    dragLayer.begin(wkBlock.modelData.id, wkBlock.modelData.title, false);
                                }
                                onPositionChanged: (mouse) => {
                                    if (!pressed || inert) return;
                                    const pt = wkTop.mapToItem(gridContent, mouse.x, mouse.y);
                                    const h = Math.min(root.snapHour(wkBlock.modelData.start + (pt.y - wkTop.pressY) / root.hourH),
                                                       wkBlock.modelData.end - Theme.minEventHours);
                                    wkBlock.pendingStartH = Math.max(root.hoursStart, h);
                                    wkBlock.hintAt(wkTop, mouse.x, mouse.y,
                                                   wkBlock.rangeText(root.days[wkBlock.modelData.dayIndex].date, wkBlock.pendingStartH, wkBlock.modelData.end));
                                }
                                onReleased: {
                                    dragLayer.finish();
                                    if (inert) return;
                                    const ns = wkBlock.pendingStartH;
                                    if (isNaN(ns) || Math.abs(ns - wkBlock.modelData.start) < 1e-9) { wkBlock.resetDrag(); return; }
                                    AppController.resizeTaskBlock(wkBlock.modelData.id, root.days[wkBlock.modelData.dayIndex].date, ns, wkBlock.modelData.end);
                                }
                                onCanceled: { wkBlock.resetDrag(); dragLayer.finish(); }
                            }
                            // Bottom edge: the length, which becomes the estimate.
                            MouseArea {
                                id: wkBot
                                objectName: "week-taskblock-bottom"
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                height: Theme.spSm
                                cursorShape: Qt.SizeVerCursor
                                preventStealing: true
                                property bool inert: false
                                // By how far the pointer moved, not where it is:
                                // the handle sits a few pixels inside the edge.
                                property real pressY: 0
                                onPressed: (mouse) => {
                                    inert = false;
                                    pressY = wkBot.mapToItem(gridContent, mouse.x, mouse.y).y;
                                    wkBlock.pendingEndH = wkBlock.modelData.end;
                                    dragLayer.begin(wkBlock.modelData.id, wkBlock.modelData.title, false);
                                }
                                onPositionChanged: (mouse) => {
                                    if (!pressed || inert) return;
                                    const pt = wkBot.mapToItem(gridContent, mouse.x, mouse.y);
                                    const h = Math.max(root.snapHour(wkBlock.modelData.end + (pt.y - wkBot.pressY) / root.hourH),
                                                       wkBlock.modelData.start + Theme.minEventHours);
                                    wkBlock.pendingEndH = Math.min(root.hoursEnd, h);
                                    wkBlock.hintAt(wkBot, mouse.x, mouse.y,
                                                   wkBlock.rangeText(root.days[wkBlock.modelData.dayIndex].date, wkBlock.modelData.start, wkBlock.pendingEndH));
                                }
                                onReleased: {
                                    dragLayer.finish();
                                    if (inert) return;
                                    const ne = wkBlock.pendingEndH;
                                    if (isNaN(ne) || Math.abs(ne - wkBlock.modelData.end) < 1e-9) { wkBlock.resetDrag(); return; }
                                    AppController.resizeTaskBlock(wkBlock.modelData.id, root.days[wkBlock.modelData.dayIndex].date, wkBlock.modelData.start, ne);
                                }
                                onCanceled: { wkBlock.resetDrag(); dragLayer.finish(); }
                            }
                        }
                    }

                    // "+N": the lanes that do not fit the day's width (A1-7,
                    // A6-9): how many, at the first of them; a click opens
                    // the day, where they stand side by side.
                    Repeater {
                        model: root.dayZoom ? [] : root.foldChips
                        delegate: Rectangle {
                            id: foldChip
                            required property var modelData
                            objectName: "week-fold-" + foldChip.modelData.dayIndex
                            readonly property var _last: Overlap.lane(root.laneFit - 1, root.laneFit, root.laneAvail, root.minLaneW, 1)
                            x: gridHost.gutterW + foldChip.modelData.dayIndex * gridHost.dayW + root.laneInset + foldChip._last.x + foldChip._last.w - width - Theme.sp2xs
                            // Off the title of what the last lane draws there.
                            y: (Overlap.chipStart(foldChip.modelData, (height + Theme.sp2xs) / root.hourH, root.blockTitleH / root.hourH) - root.hoursStart) * root.hourH + Theme.sp2xs
                            z: 6
                            width: foldT.implicitWidth + 2 * Theme.spXs
                            height: foldT.implicitHeight + Theme.sp2xs
                            radius: Theme.radiusMd
                            color: Theme.panel2
                            border.color: Theme.borderStrong
                            border.width: 1
                            Text {
                                id: foldT
                                anchors.centerIn: parent
                                text: "+" + foldChip.modelData.n
                                color: Theme.text
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                                font.weight: Theme.fwHeading
                            }
                            ClickArea {
                                label: I18n.t("week.foldOpen").arg(foldChip.modelData.n)
                                onActivated: root.dayRequested(root.days[foldChip.modelData.dayIndex].date)
                            }
                        }
                    }
                }
            }

            // What the hours on screen do not show (A2-13, A6-8): an 07:00
            // sync or the after-midnight half of a release sat above the
            // grid while the header counted it. A chip at the edge says how
            // many and the nearest; a click scrolls to it.
            readonly property var offView: {
                const f = hourScroll.contentItem;
                const out = { above: [], below: [] };
                if (!f || f.height <= 0) return out;
                const top = f.contentY, bot = f.contentY + f.height;
                const all = root.flatEvents.concat(root.flatBlocks);
                for (let i = 0; i < all.length; i++) {
                    const it = all[i];
                    const y0 = (it.start - root.hoursStart) * root.hourH;
                    const y1 = Math.max(y0 + 18, (it.end - root.hoursStart) * root.hourH);
                    if (y1 <= top + 2) out.above.push(it);
                    else if (y0 >= bot - 2) out.below.push(it);
                }
                out.above.sort((a, b) => a.start - b.start);
                out.below.sort((a, b) => a.start - b.start);
                return out;
            }
            function _scrollTo(it) {
                const f = hourScroll.contentItem;
                if (!f || !it) return;
                const maxY = Math.max(0, f.contentHeight - f.height);
                f.contentY = Math.max(0, Math.min(maxY, (it.start - root.hoursStart) * root.hourH - root.hourH / 2));
            }
            EdgeChip {
                objectName: "week-more-earlier"
                anchors.top: hourScroll.top
                // Between the first two hour labels, not on one.
                anchors.topMargin: Math.round((root.hourH - height) / 2)
                items: parent.offView.above
                up: true
                countKey: "week.moreEarlier"
                onActivated: (it) => parent._scrollTo(it)
            }
            EdgeChip {
                objectName: "week-more-later"
                anchors.bottom: hourScroll.bottom
                anchors.bottomMargin: Math.round((root.hourH - height) / 2)
                items: parent.offView.below
                up: false
                countKey: "week.moreLater"
                onActivated: (it) => parent._scrollTo(it)
            }
        }
    }

    // An edge chip of the hour grid, in the hour gutter so it never covers a
    // block: "↑2"; the tooltip and the label say "2 earlier · 07:00 Early sync".
    component EdgeChip: Rectangle {
        id: ec
        property var items: []
        property bool up: true
        property string countKey: ""
        signal activated(var it)
        readonly property var first: ec.items.length > 0 ? (ec.up ? ec.items[ec.items.length - 1] : ec.items[0]) : null
        readonly property string fullText: ec.first ? I18n.t(ec.countKey).arg(ec.items.length) + " · " + Theme.fmtHour(ec.first.start) + " " + (ec.first.title || "") : ""
        visible: ec.items.length > 0
        x: hourScroll.x + Theme.sp2xs
        z: 9
        width: gridHost.gutterW - 2 * Theme.sp2xs
        height: ecText.implicitHeight + 2 * Theme.sp2xs
        radius: height / 2
        color: Theme.panel2
        border.color: Theme.borderStrong
        border.width: 1
        Text {
            id: ecText
            anchors.centerIn: parent
            text: (ec.up ? "↑" : "↓") + ec.items.length
            color: Theme.text
            font.family: Theme.fontUi
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsXs
            font.weight: Theme.fwHeading
        }
        ClickArea { label: ec.fullText; onActivated: ec.activated(ec.first) }
    }

    // Empty-week hint — shown only when the week has no tasks and no events, so
    // the grid does not read as blank. Non-interactive: a click goes through
    // to the slot under it, which is what the hint says to do.
    EmptyState {
        objectName: "week-empty"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.sp3xl, 420)
        visible: root.weekEmpty
        // The default "не готово" (is:open) is not a search (DG-020).
        readonly property bool searching: root.searchText.replace(/(^|\s)is:open(?=\s|$)/gi, " ").trim().length > 0
        // A search that finds nothing reads as on the board (N/X-Err-Empty, R4-100).
        title: searching ? I18n.t("view.empty.noMatchFor").arg(QueryWords.label(root.searchText, []))
                         : I18n.t(root.dayZoom ? "day.empty.title" : "week.empty.title")
        line: searching ? I18n.t("view.empty.resetFilter")
                        : I18n.t("calendar.empty.hint").arg(AppController.shortcutText("task.schedule"))
        lineLink: searching
        onLineActivated: root.resetFilterRequested()
    }

    // A sweep to create or a meeting being moved: Esc puts everything back,
    // as on the board, and Main calls cancelDrag() when a popup takes the
    // keyboard mid-drag (IDIOT-TASKS-8).
    property var _activeDrag: null
    function cancelDrag() {
        if (!root._activeDrag) return false;
        root._activeDrag.cancel();
        root._activeDrag = null;
        return true;
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.visible && root._activeDrag !== null
        onActivated: root.cancelDrag()
    }

    // What a drag would set, at the pointer; Esc cancels it (APP-249).
    RescheduleDrag {
        id: dragLayer
        objectName: "week-drag"
        onCanceled: { root.chipDrag = null; root.chipTarget = null; }
    }
}
