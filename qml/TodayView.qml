pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Controls.Basic as QQC
import TodoCpp
import "Reschedule.js" as Resched
import "TaskDates.js" as TaskDates

// Today (heap 2, APP-260): the start screen. The date and one line of facts;
// the day by the hours — meetings and tasks with their status, the line of
// "now"; on the side what is in progress, what is due, whom to write, and how
// many tasks have no date. It shows; Tabless places nothing by itself.
//
// Two drawings of the same day (DG-010…015):
//   bold  (H2-Today)       the eyebrow, "День", meeting cards with a bar,
//                          outlined task cards, the free windows as facts,
//                          the side as cards and lists with a footer link;
//   quiet (H2-Today-Calm)  plain rows with an icon, a thin grey now-line,
//                          the side as small text blocks, people folded.
// A small window (X-Oth-Small, DG-008) draws one-line rows and puts the
// side under the day in three columns, in both styles.
//
// The day shown is AppController.selectedDate, so Alt+←/→ and the arrows in
// the header move it, and cal.today (0) brings it back.
FocusScope {
    id: root
    objectName: "today-view"

    signal eventClicked(string id, var occurrence)
    signal createRequested(real startHour, real endHour, var day)
    signal taskClicked(string id)
    // "N tasks without a date": Tasks · List with that condition.
    signal undatedRequested()
    // "N overdue" under Deadlines: Tasks · List with overdue deadlines (APP-287).
    signal overdueRequested()
    // "ещё N" under the deadlines: Tasks · List with the deadlines it holds.
    signal deadlinesRequested(string query)
    signal recapRequested()
    // The first-run screen (APP-271): a task made from its line, and where
    // tasks from elsewhere come in.
    signal firstTaskCreated(string id)
    signal connectRequested()
    signal importRequested()
    signal exampleRequested()
    // "Кому написать" in full, on this person (DG-002).
    signal peopleRequested(string id)
    // The person menu's "Изменить вопрос…" (R2-063).
    signal personEditRequested(string id)
    // "+ Добавить человека" under the list: a new person to write to.
    signal personAddRequested()

    // Every profile's tasks (A1-17, a setting): another profile's rows are
    // labelled with its name and colour and read-only here; Enter opens one
    // in its own profile.
    property bool allProfiles: Theme.todayAllProfiles
    signal otherProfileTaskClicked(string id, string profileId)
    // A task key pressed on another profile's row: it says where it lives.
    signal otherProfileKeys(string id, string profileName)
    function _isOther(t) { return !!(t && t.profileId && String(t.profileId).length > 0); }
    function _openTask(t) {
        if (!t || !t.id) return;
        if (root._isOther(t)) root.otherProfileTaskClicked(t.id, t.profileId);
        else root.taskClicked(t.id);
    }
    function focusView() {
        if (root.firstRun) firstRunHero.focusInput();
        else root.forceActiveFocus();
    }
    // Main hands the keyboard to a view through takeFocus(): on the first
    // run that is the input, which looked focused and was not (PERSONA-2).
    function takeFocus() { root.focusView(); }

    readonly property date day: AppController.selectedDate
    readonly property bool isToday: root._sameDay(root.day, AppController.today)
    function _sameDay(a: date, b: date): bool {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }

    // A small window (X-Oth-Small, DG-008): the side goes under the day, its
    // blocks side by side in three columns, the rows one line each.
    readonly property bool stacked: Theme.compactAt(Window.width)
    // The quiet drawing of the day and of the side (DG-012, DG-013).
    readonly property bool plain: Style.plainRows || root.stacked
    readonly property bool cards: Style.sideCards && !root.stacked

    // Rebuilt when tasks, meetings or people change; the minute ticks too.
    property int _rev: 0
    Connections { target: AppController.tasks; function onDataChanged() { root._rev++; } function onRowsInserted() { root._rev++; } function onRowsRemoved() { root._rev++; } function onModelReset() { root._rev++; } }
    Connections { target: AppController.events; function onDataChanged() { root._rev++; } function onRowsInserted() { root._rev++; } function onRowsRemoved() { root._rev++; } function onModelReset() { root._rev++; } }
    Connections { target: AppController.people; function onDataChanged() { root._rev++; } function onRowsInserted() { root._rev++; } function onRowsRemoved() { root._rev++; } function onModelReset() { root._rev++; } }
    Connections { target: AppController; function onTodayChanged() { root._rev++; } function onFocusedGitChanged() { root._rev++; }
        // The horizon changed in Settings or in the menu beside the tabs.
        function onAppSettingsJsonChanged() { if (AppController.deadlineHorizon() !== root.dlHorizon) root._rev++; } }
    Timer { interval: 60000; repeat: true; running: root.visible; onTriggered: root._rev++ }

    readonly property var dayData: root._rev >= 0 ? AppController.todayData(root.day, root.allProfiles) : ({})
    // Until the first task (APP-271): the input line, three keys, and the
    // way in for tasks that live elsewhere — instead of a tour. Closed
    // before a task was made, it is here again on the next start.
    // With "Today shows: all profiles" another profile's day is still a day:
    // the first-run page hid it while this profile was empty (A1-17).
    readonly property bool firstRun: !AppController.welcomeSeen && root._rev >= 0 && AppController.tasks.rowCount() === 0
                                     && !(root.allProfiles && (!root.dayEmpty || root.inProgress.length > 0))
    readonly property real nowHour: {
        const n = root._rev >= 0 ? new Date() : new Date();
        return n.getHours() + n.getMinutes() / 60;
    }

    // ── text ──
    // One line of facts, only the parts that are not zero (DG-010).
    function _facts() {
        const f = root.dayData.facts || {};
        const parts = [];
        // Meetings with others (A2-33, A3-20): focus time is not one, and an
        // all-day block (a conference, an on-call duty) has its own row.
        const meetings = (root.dayData.blocks || []).filter(b => b.kind === "meeting" && b.eventType !== "focus").length;
        if (meetings > 0) parts.push(I18n.count(meetings, "today.n.meetings"));
        if (f.planned > 0) parts.push(I18n.count(f.planned, "today.n.planned"));
        // "Due today" only on today (A2-23): browsing another day it is that
        // day's deadlines, said as tomorrow's or as that day's.
        if (f.dueToday > 0) {
            const off = root._daysFromToday(root.day);
            parts.push(I18n.count(f.dueToday, off === 0 ? (root.plain || !Style.fills ? "today.n.dueShort" : "today.n.due")
                                              : off === 1 ? "today.n.dueTomorrow" : "today.n.dueThatDay"));
        }
        // A plan emptied by Done keeps what was done (A2-24).
        if (root.isToday && parts.length === 0) {
            const closed = (AppController.endOfDaySummary().closed || []).length;
            if (closed > 0) parts.push(I18n.count(closed, "today.n.closed"));
        }
        return parts.join(" · ");
    }
    // Whole days from the real today to `d` (negative before it).
    function _daysFromToday(d) {
        const t = AppController.today;
        const a = Date.UTC(d.getFullYear(), d.getMonth(), d.getDate());
        const b = Date.UTC(t.getFullYear(), t.getMonth(), t.getDate());
        return Math.round((a - b) / 86400000);
    }
    // A deadline's words on the side (A2-23, A5-18): relative to the real
    // today, whichever day is shown; a date beyond tomorrow, "overdue" past.
    function _dueWord(due) {
        const dd = new Date(due);
        const off = root._daysFromToday(dd);
        if (off === 0) return I18n.t("quick.day.today");
        if (off === 1) return I18n.t("quick.day.tomorrow");
        if (off < 0) return I18n.t("today.dueWasOn").arg(I18n.fmtDate(dd, "dayMonth"));
        return I18n.fmtDate(dd, "weekdayDay");
    }
    function _hm(h) {
        const hh = Math.floor(h + 1e-6), mm = Math.round((h - hh) * 60);
        return Theme.fmtHour(hh + mm / 60);
    }
    function _len(a, b) { return I18n.fmtMinutes(Math.round((b - a) * 60)); }
    // How long a row lasts: the whole meeting for one across midnight, not
    // the part of it on this day (A3-3, A6-3).
    function _rowLen(b) {
        if ((b.toNextDay || b.fromPrevDay) && b.totalMinutes > 0) return I18n.fmtMinutes(b.totalMinutes);
        return root._len(b.start || 0, b.end || 0);
    }
    // A meeting's type in the words of its editor (A2-33, A3-20): "Синк",
    // "Фокус-время"; a meeting with no type is a meeting.
    function _typeName(b, lower) {
        const ty = String(b.eventType || "");
        const s = ty.length > 0 && ty !== "none" ? I18n.t("event.type." + ty) : I18n.t("event.kind.meeting");
        return lower && s.length > 0 ? s.charAt(0).toLowerCase() + s.slice(1) : s;
    }
    // The type, unless the title already is that word ("Sync · sync").
    function _typeWord(b, lower) {
        const s = root._typeName(b, lower);
        return s.toLowerCase() === String(b.title || "").trim().toLowerCase() ? "" : s;
    }
    // m:ss, h:mm:ss past an hour, as the sidebar and the focus mode (A1-13).
    function _timer(id, tick) {
        const s = tick >= 0 ? AppController.elapsedSecondsFor(id) : 0;
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
        const p2 = (n) => (n < 10 ? "0" : "") + n;
        return h > 0 ? h + ":" + p2(m) + ":" + p2(sec) : m + ":" + p2(sec);
    }
    // The caption under a row of the day.
    function _sub(kind: string, b: var): string {
        const parts = [];
        const meeting = kind !== "task";
        // An all-day block over several days says which days (A6-17); one
        // with no type is not called a meeting (an on-call duty is not one).
        const typed = String(b.eventType || "").length > 0 && b.eventType !== "none";
        if (kind === "allday" && b.dayCount > 1) {
            if (typed && root._typeWord(b, root.plain)) parts.push(root._typeWord(b, root.plain));
            parts.push(I18n.fmtDate(new Date(b.firstDay), "dayMonth") + "–" + I18n.fmtDate(new Date(b.lastDay), "dayMonth"));
            parts.push(I18n.t("today.dayOfSpan").arg(b.dayNo).arg(b.dayCount));
        } else if (kind === "allday" && !typed) {
            // the title says it
        } else if (root.plain) {
            // "встреча · 30 мин · Zoom", "APP-105 · 1 ч"
            // A meeting titled as its type says neither its type twice
            // nor its storage id (F3-EYES-1).
            if (meeting) { if (root._typeWord(b, true)) parts.push(root._typeWord(b, true)); }
            else parts.push(b.id);
            if (kind !== "allday" && !b.dayOnly) parts.push(root._rowLen(b));
            if (!meeting && b.waiting) parts.push(I18n.t("today.waitingOn").arg(b.waiting));
        } else {
            if (meeting) { if (root._typeWord(b, false)) parts.push(root._typeWord(b, false)); }
            else parts.push(b.id, I18n.t("today.plannedByYou"));
        }
        // A repeating task is marked as one (A2-25).
        if (kind === "task") {
            const rec = String(AppController.taskById(b.id).recurrence || "");
            if (rec.length > 0) parts.push("↻ " + TaskDates.repeatText(rec));
        }
        // A focus block is "with yourself"; its people field is a note.
        if (meeting && b.attendees && b.eventType !== "focus") parts.push(b.attendees);
        if (b.profileName) parts.push(b.profileName);
        // Kept in another zone (A6-4): its own hour there, read-only.
        if (b.srcTz) parts.push(root._hm(b.srcStart) + " " + String(b.srcTz).split("/").pop().replace(/_/g, " "));
        // Where a meeting across midnight really ends (A3-3, A6-3).
        if (b.toNextDay) parts.push(I18n.t("today.untilNextDay").arg(root._hm(b.realEnd)));
        if (b.fromPrevDay) parts.push(I18n.t("today.untilNext").arg(root._hm(b.end)));
        if ((b.overlapsWith || []).length > 0) parts.push(I18n.t("today.overlaps").arg(b.overlapsWith.join(", ")));
        return parts.join(" · ");
    }

    // The day as rows: all-day above, then by time with the free windows,
    // "now" and the end of the working day put in their places.
    readonly property var rows: {
        const d = root.dayData;
        const out = [];
        if (!d.blocks) return out;
        for (const b of d.allDay || []) out.push({ kind: "allday", start: -1, block: b });
        // Planned for the day with no time, under the all-day row (IDIOT-CAL-10).
        for (const b of d.dayOnly || []) out.push({ kind: "task", start: -0.5, block: b });
        for (const b of d.blocks) out.push({ kind: b.kind, start: b.start, block: b });
        // Today, a window counts from now: "Free 10 h" at 16:56 counted the
        // morning already gone (EYE-21). It sorts just under the "now" line.
        for (const g of d.free || []) {
            if (root.isToday && g.end <= root.nowHour) continue;
            const clipped = root.isToday && g.start < root.nowHour;
            out.push({ kind: "free", start: clipped ? root.nowHour + 0.0001 : g.start, end: g.end });
        }
        if (root.isToday && root.nowHour >= d.fromHour && root.nowHour <= Math.max(d.toHour, d.workEnd))
            out.push({ kind: "now", start: root.nowHour });
        if (d.workday) out.push({ kind: "end", start: d.workEnd });
        const order = { allday: 0, free: 1, meeting: 2, task: 2, now: 3, end: 4 };
        out.sort((a, b) => a.start - b.start || order[a.kind] - order[b.kind]);
        return out;
    }
    readonly property bool dayEmpty: !root.dayData.blocks || (root.dayData.blocks.length === 0 && (root.dayData.allDay || []).length === 0
                                                               && (root.dayData.dayOnly || []).length === 0)

    // The one in-progress task the bold card shows (DG-014): the one with
    // the timer, else the one whose branch is checked out, else the first.
    readonly property var inProgress: root.dayData.inProgress || []
    readonly property int _leadIdx: {
        const ip = root.inProgress;
        for (let i = 0; i < ip.length; i++) if (ip[i].isTiming) return i;
        for (let i = 0; i < ip.length; i++) if (ip[i].repo) return i;
        return ip.length > 0 ? 0 : -1;
    }
    readonly property var lead: root._leadIdx >= 0 ? root.inProgress[root._leadIdx] : null
    readonly property var otherInProgress: root.inProgress.filter((t, i) => i !== root._leadIdx)
    readonly property var _ipLines: root.cards ? root.otherInProgress : root.inProgress
    readonly property int _ipCap: 5
    property bool _ipAll: false
    readonly property int _dlCap: 8
    // Two tabs on the deadlines (dogfood 2026-10-19): "Срок сегодня" — the
    // shown day's (H2-Today-Calm) — and "Ближайшие": that day and the
    // horizon's days after it, by day (Q-List's day groups). The tab and
    // the horizon live in settings.calendar, as Settings → Calendar sets them.
    readonly property string dlTab: Theme.deadlinesTab
    readonly property int dlHorizon: root.dayData.deadlineHorizon || 7
    readonly property var deadlines: (root.dayData.deadlines || []).filter(t => root.dlTab === "upcoming" || (t.dayOffset || 0) === 0)
    // What the section draws: in "Ближайшие" a day's heading and then its
    // tasks; at most _dlCap tasks, the rest behind "ещё N" in Tasks · List.
    readonly property var dlRows: {
        const out = [];
        const list = root.dayData.deadlines || [];
        const upcoming = root.dlTab === "upcoming";
        let n = 0;
        for (const d of root.dayData.deadlineDays || []) {
            if (n >= root._dlCap) break;
            if (d.count <= 0 || d.first >= list.length) continue;
            if (!upcoming && (list[d.first].dayOffset || 0) !== 0) continue;
            if (upcoming) out.push({ kind: "day", key: "day:" + d.kind + ":" + d.offset, day: d });
            for (let i = d.first; i < d.first + d.count && i < list.length && n < root._dlCap; i++, n++)
                out.push({ kind: "task", key: "task:" + list[i].id + ":" + (list[i].profileId || ""), task: list[i] });
        }
        return out;
    }
    readonly property int dlHidden: Math.max(0, root.deadlines.length - root.dlRows.filter(r => r.kind === "task").length)
    // A day's heading, as Q-List draws one: "Завтра  пт, 23 окт",
    // "Пятница  23 окт", "2 нояб  пн".
    function dueDayTitle(d) {
        const day = new Date(d.date);
        if (d.kind === "today") return I18n.t("common.today");
        if (d.kind === "tomorrow") return I18n.t("common.tomorrow");
        if (d.kind === "weekday") {
            const w = I18n.locale.standaloneDayName(day.getDay(), Locale.LongFormat);
            return w.length > 0 ? w.charAt(0).toUpperCase() + w.slice(1) : w;
        }
        return I18n.fmtDate(day, "dayMonth");
    }
    function dueDaySub(d) {
        const day = new Date(d.date);
        if (d.kind === "today" || d.kind === "tomorrow") return I18n.fmtDate(day, "weekdayDay");
        if (d.kind === "weekday") return I18n.fmtDate(day, "dayMonth");
        return I18n.dayName(day.getDay());
    }
    // The horizon's name: "до завтра", "7 дней".
    function horizonLabel(n) {
        return n <= 1 ? I18n.t("today.horizon.tomorrow") : I18n.count(n, "today.horizon.days");
    }
    // The tab and the horizon are kept with the other calendar settings.
    function _setCalendar(key, value) {
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        const c = Object.assign({}, s.calendar || {});
        c[key] = value;
        s.calendar = c;
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function setDeadlinesTab(tab) { root._setCalendar("deadlinesTab", tab === "today" ? "today" : "upcoming"); }
    function setDeadlineHorizon(n) {
        root._setCalendar("deadlineHorizon", Number(n));
        root.setDeadlinesTab("upcoming");
    }
    // "ещё N": every one of them in Tasks · List, overdue on top there too.
    function deadlinesQuery() {
        const d = root.day;
        const iso = (x) => x.getFullYear() + "-" + (x.getMonth() < 9 ? "0" : "") + (x.getMonth() + 1) + "-" + (x.getDate() < 10 ? "0" : "") + x.getDate();
        if (root.dlTab !== "upcoming") return "is:open due:" + iso(d);
        const last = new Date(d.getFullYear(), d.getMonth(), d.getDate() + root.dlHorizon);
        return "is:open due:<=" + iso(last);
    }

    readonly property var people: Style.todayExtras === "hidden" ? [] : (root.dayData.people || [])
    // The widest word the time gutter holds, in the gutter's font.
    readonly property int timeLabelW: Math.ceil(Math.max(noTimeTM.advanceWidth, allDayTM.advanceWidth, fromPrevTM.advanceWidth)) + 1
    TextMetrics { id: noTimeTM; text: I18n.t("today.noTime"); font.family: Theme.fontUi; font.pixelSize: Theme.fsXs }
    TextMetrics { id: allDayTM; text: I18n.t("today.allDay"); font.family: Theme.fontUi; font.pixelSize: Theme.fsXs }
    TextMetrics { id: fromPrevTM; text: I18n.t("today.fromPrev"); font.family: Theme.fontUi; font.pixelSize: Theme.fsXs }

    Keys.onPressed: (e) => {
        if (e.modifiers & Qt.AltModifier && (e.key === Qt.Key_Left || e.key === Qt.Key_Right)) return;
    }

    // ── The keyboard cursor (APP-276) ────────────────────────────────
    // j / k walk the day's meetings and tasks, h / l the day before / after
    // (as [ ] do); the task keys act on the task under it. Held by key, so
    // a move or a sync leaves it on the same thing, or on its neighbour.
    property bool cursorVisible: false
    property string cursorKey: ""
    property int _cursorIdx: 0
    readonly property bool cardMenuOpen: menuHost.menuOpen
    function _rowKey(r) {
        if (!r || !r.block) return "";
        // One key per row (IDIOT-CAL-7): the two parts of a meeting across
        // midnight share an id, and j looped back to the first of them.
        const b = r.block;
        return (r.kind === "task" ? "task:" : "event:") + b.id + (b.fromPrevDay ? ":prev" : "") + (b.toNextDay ? ":next" : "");
    }
    function _items() {
        const out = [];
        for (let i = 0; i < root.rows.length; i++) {
            const r = root.rows[i];
            if (r.kind !== "meeting" && r.kind !== "task" && r.kind !== "allday") continue;
            out.push({ kind: r.kind === "task" ? "task" : "event", id: r.block.id, key: root._rowKey(r), row: i, block: r.block });
        }
        // Past the day's last row, the "N without a date" link (A1-2): it
        // was 29 Tab presses away.
        if ((root.dayData.undated || 0) > 0 && !root.firstRun)
            out.push({ kind: "undated", id: "", key: "undated", row: -1, block: null });
        return out;
    }
    function _cursorIndex() {
        const items = root._items();
        for (let i = 0; i < items.length; i++)
            if (items[i].key === root.cursorKey) return i;
        return -1;
    }
    function _cursorItem() {
        if (!root.cursorVisible || root.cursorKey === "") return null;
        const items = root._items();
        for (let i = 0; i < items.length; i++)
            if (items[i].key === root.cursorKey) return items[i];
        return null;
    }
    readonly property string cursorTaskId: {
        const it = root._cursorItem();
        return it && it.kind === "task" && !root._isOther(it.block) ? it.id : "";
    }
    function clearCursor() {
        root.cursorVisible = false;
        root.cursorKey = "";
    }
    function _placeIdx(i) {
        root.cursorVisible = true;
        const items = root._items();
        if (items.length === 0) {
            root.cursorKey = "";
            root._cursorIdx = 0;
            return;
        }
        const at = Math.max(0, Math.min(items.length - 1, i));
        root.cursorKey = items[at].key;
        root._cursorIdx = at;
        if (items[at].row >= 0) dayList.positionViewAtIndex(items[at].row, ListView.Contain);
        if (items[at].kind === "task") AppController.markTaskSeen(items[at].id);
    }
    function moveCursor(dx, dy) {
        if (dx !== 0) {
            root._step(dx);
            Qt.callLater(root._placeIdx, 0);
            return;
        }
        const at = root._cursorIndex();
        if (!root.cursorVisible || at < 0) { root._placeIdx(at < 0 && root.cursorVisible ? root._cursorIdx : 0); return; }
        root._placeIdx(at + dy);
    }
    function _reconcile() {
        if (root.cursorVisible && root.cursorKey !== "" && root._cursorIndex() < 0) root._placeIdx(root._cursorIdx);
    }
    onRowsChanged: if (root.cursorVisible) Qt.callLater(root._reconcile)
    function _actionCardId() {
        const it = root._cursorItem();
        if (it && it.kind === "task" && root._isOther(it.block)) {
            root.otherProfileKeys(it.id, it.block.profileName || "");
            return "";
        }
        if (it) return it.kind === "task" ? it.id : "";
        if (AppController.selectionCount === 1) return AppController.selectedTaskIds[0];
        return "";
    }
    function openCursor() {
        const it = root._cursorItem();
        if (!it) { root.moveCursor(0, 0); return; }
        if (it.kind === "undated") { root.undatedRequested(); return; }
        if (it.kind === "task") root._openTask(it.block);
        // The day's occurrence, not the series (IDIOT-CAL-1): Main finds it
        // by the ISO date the block carries.
        else root.eventClicked(it.id, it.block.occurrence || null);
    }
    function toggleCursorSelection() {
        const it = root._cursorItem();
        if (it && it.kind === "task" && !root._isOther(it.block)) AppController.toggleTaskSelection(it.id);
        else if (!it) root.moveCursor(0, 0);
    }
    function openCursorMenu() {
        const id = root._actionCardId();
        if (!id) return;
        menuHost.taskId = id;
        menuHost.releaseMenu();
        menuHost.popup();
    }
    // Shift H / L: the task a day earlier / later (the cursor follows);
    // Shift J / K: a grid step; Ctrl Shift J / K: its block longer / shorter.
    function moveSelectionOrCard(dx) {
        const it = root._cursorItem();
        if (!it || it.kind !== "task" || root._isOther(it.block)) return;
        const t = AppController.taskById(it.id);
        if (!t || !t.id) return;
        const r = Resched.shiftByDays(t.scheduledAt, t.scheduledHasTime, dx, root.day, AppController.today);
        if (AppController.rescheduleTask(it.id, "scheduled", r.when, r.timed)) root._step(dx);
    }
    function moveCursorCard(dx, dy) {
        const it = root._cursorItem();
        if (!it || it.kind !== "task" || dy === 0 || root._isOther(it.block)) return;
        const t = AppController.taskById(it.id);
        const r = t ? Resched.shiftByTime(t.scheduledAt, t.scheduledHasTime, dy, Theme.snapMinutes) : null;
        if (r) AppController.rescheduleTask(it.id, "scheduled", r.when, true);
    }
    function resizeCursor(steps) {
        const it = root._cursorItem();
        if (!it || it.kind !== "task" || it.block.dayOnly || it.block.fromPrevDay || it.block.toNextDay || root._isOther(it.block)) return false;
        const step = Theme.snapMinutes / 60;
        const end = Math.min(24, it.block.end + steps * step);
        if (end - it.block.start < step - 1e-9) return false;
        return AppController.resizeTaskBlock(it.id, root.day, it.block.start, end);
    }
    PersonMenu {
        id: personMenu
        onEditRequested: (id) => root.personEditRequested(id)
        onLinksRequested: (id) => root.peopleRequested(id)
    }

    TaskMenuHost {
        id: menuHost
        anchorItem: root
        onOpenRequested: root.taskClicked(menuHost.taskId)
    }

    ColumnLayout {
        anchors.fill: parent
        // The sheets' main padding (R4-001): bold 28/36 (H2-Today, H2-First),
        // quiet 40/48 (H2-Today-Calm, Q-First), a small window 26/32
        // (N/X-Oth-Small).
        readonly property int _side: root.stacked ? Theme.sp3xl + Theme.spMd
            : root.plain ? 2 * Theme.sp3xl : Theme.sp3xl + Theme.spXl
        anchors.leftMargin: _side
        anchors.rightMargin: _side
        anchors.topMargin: root.stacked ? Theme.sp2xl + Theme.spLg
            : root.plain ? Theme.sp3xl + Theme.sp2xl : Theme.sp2xl + Theme.spXl
        spacing: 0

        // ── the header: the date, the facts, the day's arrows on the right ──
        RowLayout {
            objectName: "today-header"
            Layout.fillWidth: true
            // Quiet: 36px under the header, the day starts lower (a-24,
            // H2-Today-Calm); bold keeps 16 over its "День" head.
            Layout.bottomMargin: root.plain && !root.stacked ? Theme.sp3xl + Theme.spXl : Theme.sp2xl
            spacing: Theme.spMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spXs
                // The eyebrow: "Сегодня" in bold; quiet draws none (R2-003,
                // H2-Today-Calm): the date alone heads the day.
                RowLayout {
                    visible: !Style.plainRows && (!root.stacked || !root.isToday || root.dayData.workday === false)
                    // One space's width before the dot, as after it: spMd
                    // plus the "· " read as a double gap (EYES-9).
                    spacing: Theme.spXs
                    Text {
                        objectName: "today-label"
                        // Another day said its date twice, small over big (EYE-21):
                        // the small line says how far it is, or nothing.
                        readonly property int _offset: Math.round((new Date(root.day.getFullYear(), root.day.getMonth(), root.day.getDate())
                            - new Date(AppController.today.getFullYear(), AppController.today.getMonth(), AppController.today.getDate())) / 86400000)
                        visible: text.length > 0
                        text: root.isToday ? I18n.t("sidebar.today")
                             : root.dayData.workday === false ? I18n.t("today.dayOff")
                             : _offset === 1 ? I18n.t("common.tomorrow") : _offset === -1 ? I18n.t("common.yesterday") : ""
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                    }
                    Text {
                        objectName: "today-dayoff"
                        visible: root.isToday && root.dayData.workday === false
                        text: "· " + I18n.t("today.dayOff")
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                    }
                }
                Text {
                    objectName: "today-date"
                    text: {
                        const s = I18n.fmtDate(root.day, "longWeekday");
                        return s.charAt(0).toUpperCase() + s.slice(1);
                    }
                    color: Theme.text
                    font.family: Theme.fontDisplay
                    font.pixelSize: root.stacked ? Theme.fsDayTitleSmall : Theme.fsDayTitle
                    font.weight: root.stacked ? Theme.fwHeading : Theme.fwScreenTitle
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                // One line of facts (DG-010); on the first run, quiet says
                // "пока пусто" and bold says nothing (Q-First / H2-First).
                Text {
                    objectName: "today-facts"
                    visible: !root.firstRun || root.plain
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spXs
                    text: root.firstRun ? I18n.t("first.emptySub")
                        : root._facts().length > 0 ? root._facts() : I18n.t("today.nothing")
                    color: root.plain || !Style.factsLine ? Theme.textDim : Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    wrapMode: Text.WordWrap
                }
            }
            Text {
                objectName: "today-back"
                Layout.alignment: Qt.AlignBottom
                visible: !root.isToday
                // The key as bound now (A1-6): a literal "(T)" outlived the
                // move of cal.today to 0, and T is the timer.
                readonly property string keys: AppController.shortcuts.length >= 0 ? AppController.shortcutText("cal.today") : ""
                // The key in the mono face, as every hint writes keys: "(0)"
                // read as a count of nothing (EYE-21).
                textFormat: Text.StyledText
                text: I18n.t("today.backToToday") + (keys.length > 0 ? "&nbsp;&nbsp;<font face=\"" + Theme.fontMono + "\">" + keys + "</font>" : "")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.underline: backCA.hovered
                ClickArea { id: backCA; label: I18n.t("today.backToToday"); shortcutId: "cal.today"; onActivated: AppController.selectedDate = AppController.today }
            }
            // ‹ › at the far right (DG-011): boxed in bold, bare in quiet;
            // not on the first run and not in a small window, where the
            // keys still move the day.
            Row {
                Layout.alignment: root.plain ? Qt.AlignTop : Qt.AlignBottom
                visible: !root.firstRun && !root.stacked
                spacing: root.plain ? Theme.spXs : Theme.spSm
                NavBtn { objectName: "today-prev"; icon: "chevron-left"; label: I18n.t("today.prevDay"); shortcutId: "cal.prevDay"; onActivated: root._step(-1) }
                NavBtn { objectName: "today-next"; icon: "chevron-right"; label: I18n.t("today.nextDay"); shortcutId: "cal.nextDay"; onActivated: root._step(1) }
            }
        }

        FirstRunHero {
            id: firstRunHero
            visible: root.firstRun
            Layout.fillWidth: true
            Layout.topMargin: root.plain ? Theme.px(150) : Theme.px(90)
            onCreated: (id) => root.firstTaskCreated(id)
            onConnectRequested: root.connectRequested()
            onImportRequested: root.importRequested()
            onExampleRequested: root.exampleRequested()
        }
        Item { visible: root.firstRun; Layout.fillHeight: true }

        GridLayout {
            visible: !root.firstRun
            Layout.fillWidth: true
            Layout.fillHeight: !root.stacked
            columns: root.stacked ? 1 : 3
            rowSpacing: Theme.sp2xl
            columnSpacing: root.plain ? Theme.sp3xl + Theme.spXl : Theme.sp3xl

            // ── the day ──
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: !root.stacked
                Layout.maximumWidth: root.plain && !root.stacked ? Theme.px(680) : -1
                Layout.preferredWidth: root.plain && !root.stacked ? Theme.px(680) : -1
                Layout.preferredHeight: root.stacked ? dayList.contentHeight + Theme.spSm : -1
                spacing: 0

                SectionHeader {
                    visible: !root.plain
                    title: I18n.t("today.day")
                    titleColor: Theme.textDim
                }

                // A free day (X-Err-Empty): the facts line already says
                // "Ничего не запланировано"; here only where the undated
                // tasks are. Its own line above the rows, so the bold
                // "Свободно 10 ч" row never runs under it (R2-067).
                Text {
                    objectName: "today-empty"
                    visible: root.dayEmpty && (root.dayData.undated || 0) > 0
                    Layout.topMargin: root.plain ? 0 : Theme.spMd
                    Layout.bottomMargin: root.plain ? Theme.spSm : 0
                    text: I18n.t("today.emptyUndated").arg(root.dayData.undated || 0)
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.underline: emptyUndCA.hovered
                    ClickArea { id: emptyUndCA; label: parent.text; role: Accessible.Link; onActivated: root.undatedRequested() }
                }

                // "N more above" (A2-10): a band of its own over the rows,
                // never over the first one's title.
                Rectangle {
                    objectName: "today-more-above"
                    visible: root.hiddenAbove > 0 && !root.stacked
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: root.plain ? 0 : Theme.spSm
                    Layout.bottomMargin: Theme.spXs
                    implicitWidth: aboveT.implicitWidth + 2 * Theme.spMd
                    implicitHeight: aboveT.implicitHeight + Theme.spXs * 2
                    radius: height / 2
                    color: Theme.panel2
                    border.color: Theme.border
                    border.width: 1
                    Text {
                        id: aboveT
                        anchors.centerIn: parent
                        text: "↑ " + I18n.t("today.moreAbove").arg(root.hiddenAbove)
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                    ClickArea { label: aboveT.text; onActivated: dayList.positionViewAtBeginning() }
                }

                ListView {
                    id: dayList
                    objectName: "today-day"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: root.plain ? 0 : Theme.spMd
                    clip: true
                    spacing: root.plain ? 0 : Theme.spSm
                    interactive: !root.stacked
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.rows
                    QQC.ScrollBar.vertical: ThinScrollBar {}
                    delegate: DayRow {}
                    // Opened, it shows "now", not 09:00.
                    onCountChanged: Qt.callLater(root._revealNow)
                }
            }

            // A hairline between the day and the side once it is under it.
            Rectangle {
                visible: root.stacked
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }

            // ── the side ──
            Flickable {
                objectName: "today-side"
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: root.stacked ? -1
                                     : root.plain ? Math.min(Theme.px(320), root.width * 0.3)
                                     : Math.min(Theme.px(470), root.width * 0.38)
                Layout.fillWidth: root.stacked
                Layout.fillHeight: !root.stacked
                Layout.preferredHeight: root.stacked ? side.implicitHeight : -1
                Layout.topMargin: root.plain && !root.stacked ? Theme.spSm : 0
                contentHeight: side.implicitHeight
                clip: true
                interactive: !root.stacked
                boundsBehavior: Flickable.StopAtBounds
                GridLayout {
                    id: side
                    // Stacked, the columns keep their own width (R3-014).
                    width: root.stacked ? Math.min(implicitWidth, parent.width) : parent.width
                    columns: root.stacked ? 3 : 1
                    rowSpacing: root.plain ? Theme.sp2xl : Theme.sp2xl + Theme.spSm
                    columnSpacing: Theme.sp3xl

                    // In progress; hidden when nothing is (DG-013, DG-014).
                    ColumnLayout {
                        objectName: "today-inprogress"
                        Layout.alignment: Qt.AlignTop
                        // Stacked: packed at natural width, like flex-wrap (R3-014).
                        Layout.preferredWidth: root.stacked ? Theme.px(220) : -1
                        Layout.fillWidth: !root.stacked
                        visible: root.inProgress.length > 0
                        spacing: root.cards ? Theme.spMd : Theme.spSm
                        SideHead { text: I18n.t(root.cards ? "today.inProgress" : "today.q.inProgress") }

                        // Bold: one card, the task with its key, PR / CI and
                        // the timer.
                        Rectangle {
                            id: leadCard
                            objectName: "today-lead"
                            visible: root.cards && !!root.lead
                            Layout.fillWidth: true
                            readonly property var t: root.lead || ({})
                            readonly property var pr: leadCard.t.repo && leadCard.t.repo.pr ? leadCard.t.repo.pr : null
                            implicitHeight: leadCol.implicitHeight + 2 * Theme.spLg
                            radius: Theme.radiusXl
                            color: Theme.surfaceCard
                            ColumnLayout {
                                id: leadCol
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spXl
                                anchors.rightMargin: Theme.spXl
                                anchors.topMargin: Theme.spLg
                                anchors.bottomMargin: Theme.spLg
                                spacing: Theme.spMd
                                RowLayout {
                                    spacing: Theme.spMd
                                    StatusRing { category: leadCard.t.category || "prog" }
                                    ProfileDot { block: leadCard.t }
                                    Text {
                                        Layout.fillWidth: true
                                        text: leadCard.t.title || ""
                                        elide: Text.ElideRight
                                        color: Theme.text
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsLg
                                        font.weight: Theme.fwHeading
                                    }
                                }
                                RowLayout {
                                    Layout.leftMargin: Theme.statusRingSize + Theme.spMd
                                    spacing: Theme.spLg
                                    Text { text: leadCard.t.id || ""; color: Theme.textDim; font.family: Theme.fontMono; font.pixelSize: Theme.fsXs }
                                    // Another profile's task says whose it is (A1-17).
                                    Text {
                                        objectName: "today-lead-profile"
                                        visible: root._isOther(leadCard.t)
                                        text: leadCard.t.profileName || ""
                                        color: Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsXs
                                    }
                                    // ID · PR · CI · timer, never the branch
                                    // (a-18, H2-Today; no branch on cards).
                                    Row {
                                        visible: !!leadCard.pr && leadCard.pr.number > 0
                                        spacing: Theme.spXs
                                        Text {
                                            text: leadCard.pr ? "PR #" + leadCard.pr.number + (leadCard.pr.checks ? " · CI" : "") : ""
                                            color: Theme.textMuted
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsSm
                                        }
                                        Icon {
                                            visible: !!leadCard.pr && (leadCard.pr.checks === "passing" || leadCard.pr.checks === "failing")
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: leadCard.pr && leadCard.pr.checks === "failing" ? "close" : "check"
                                            size: Theme.fsSm
                                            color: leadCard.pr && leadCard.pr.checks === "failing" ? Theme.signalUrgent : Theme.textMuted
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                    TimerMark { visible: !!leadCard.t.isTiming; taskId: leadCard.t.id || "" }
                                }
                            }
                            ClickArea { label: leadCard.t.title || ""; onActivated: root._openTask(leadCard.t) }
                        }
                        // The others in progress, as lines; quiet draws all
                        // of them this way.
                        Repeater {
                            model: root._ipAll ? root._ipLines : root._ipLines.slice(0, root._ipCap)
                            delegate: RowLayout {
                                id: ipRow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.spMd
                                StatusRing {
                                    visible: !root.stacked
                                    size: Theme.statusRingSize - 2
                                    category: ipRow.modelData.category || "prog"
                                }
                                ProfileDot { block: ipRow.modelData }
                                Text {
                                    Layout.fillWidth: true
                                    text: ipRow.modelData.title
                                    elide: Text.ElideRight
                                    color: Theme.text
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsMd
                                    ClickArea { label: parent.text; onActivated: root._openTask(ipRow.modelData) }
                                }
                                // Whose it is, when it is another profile's (A1-17).
                                Text {
                                    objectName: "today-inprogress-profile"
                                    visible: root._isOther(ipRow.modelData)
                                    Layout.maximumWidth: Theme.px(120)
                                    text: ipRow.modelData.profileName || ""
                                    elide: Text.ElideRight
                                    color: Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                }
                                TimerMark { visible: !!ipRow.modelData.isTiming; taskId: ipRow.modelData.id }
                            }
                        }
                        // The rest on request: eighty lines pushed the deadlines
                        // off the screen (EYES-3).
                        Text {
                            objectName: "today-inprogress-more"
                            visible: !root._ipAll && root._ipLines.length > root._ipCap
                            text: I18n.t("week.more").arg(root._ipLines.length - root._ipCap)
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                            ClickArea { label: parent.text; onActivated: root._ipAll = true }
                        }
                    }

                    // Deadlines; overdue apart, never in red (DG-013).
                    ColumnLayout {
                        objectName: "today-deadlines"
                        Layout.alignment: Qt.AlignTop
                        // Stacked: packed at natural width, like flex-wrap (R3-014).
                        Layout.preferredWidth: root.stacked ? Theme.px(220) : -1
                        Layout.fillWidth: !root.stacked
                        // Shown while either tab has something, so an empty
                        // tab is never a dead end: the other one is a Tab away.
                        visible: (root.dayData.deadlines || []).length > 0 || (root.dayData.overdue || []).length > 0
                        spacing: root.cards ? 0 : Theme.spSm
                        Accessible.role: Accessible.Grouping
                        Accessible.name: I18n.t("today.deadlines")
                        // The heading is the two tabs; in "Ближайшие" the
                        // horizon beside them opens its choices. Wraps in a
                        // narrow column instead of clipping.
                        Flow {
                            Layout.fillWidth: true
                            Layout.bottomMargin: root.cards ? Theme.spXs : 0
                            spacing: Theme.spMd
                            Repeater {
                                model: [
                                    { id: "today", label: I18n.t("today.q.dueToday") },
                                    { id: "upcoming", label: I18n.t("today.dl.upcoming") }
                                ]
                                delegate: Item {
                                    id: dlTabItem
                                    required property var modelData
                                    readonly property bool on: root.dlTab === dlTabItem.modelData.id
                                    objectName: "today-deadlines-tab-" + dlTabItem.modelData.id
                                    implicitWidth: dlTabText.implicitWidth
                                    implicitHeight: dlTabText.implicitHeight + Theme.spXs
                                    SideHead {
                                        id: dlTabText
                                        text: dlTabItem.modelData.label
                                        color: dlTabItem.on ? Theme.text : Theme.textDim
                                    }
                                    CursorBar {
                                        anchors.left: dlTabText.left
                                        anchors.bottom: parent.bottom
                                        full: true
                                        target: dlTabText
                                        shown: dlTabItem.on
                                    }
                                    ClickArea {
                                        label: dlTabItem.modelData.label
                                        role: Accessible.PageTab
                                        checkable: true
                                        checked: dlTabItem.on
                                        onActivated: root.setDeadlinesTab(dlTabItem.modelData.id)
                                    }
                                }
                            }
                            Item {
                                id: horizonBtn
                                objectName: "today-deadlines-horizon"
                                visible: root.dlTab === "upcoming"
                                implicitWidth: horizonRow.implicitWidth
                                implicitHeight: horizonRow.implicitHeight + Theme.spXs
                                Row {
                                    id: horizonRow
                                    spacing: Theme.sp2xs
                                    Text {
                                        text: root.horizonLabel(root.dlHorizon)
                                        color: Theme.textDim
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsSm
                                        font.underline: horizonCA.hovered
                                    }
                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "chevron-down"
                                        size: Theme.fsSm
                                        color: Theme.textDim
                                    }
                                }
                                ClickArea {
                                    id: horizonCA
                                    label: I18n.t("settings.cal.deadlineHorizon") + ": " + root.horizonLabel(root.dlHorizon)
                                    onActivated: horizonMenu.popup(horizonBtn, 0, horizonBtn.height)
                                }
                                AppMenu {
                                    id: horizonMenu
                                    objectName: "today-deadlines-horizon-menu"
                                    Instantiator {
                                        model: AppController.deadlineHorizonChoices()
                                        delegate: AppMenuItem {
                                            id: horizonItem
                                            required property var modelData
                                            objectName: "today-horizon-" + horizonItem.modelData
                                            text: root.horizonLabel(horizonItem.modelData)
                                            marked: Number(horizonItem.modelData) === root.dlHorizon
                                            onTriggered: root.setDeadlineHorizon(horizonItem.modelData)
                                        }
                                        onObjectAdded: (index, object) => horizonMenu.insertItem(index, object)
                                        onObjectRemoved: (index, object) => horizonMenu.removeItem(object)
                                    }
                                }
                            }
                        }
                        // Overdue on top, as a count that opens them; never in
                        // red (DG-013).
                        Text {
                            objectName: "today-overdue"
                            Layout.bottomMargin: root.cards ? Theme.spXs : 0
                            visible: (root.dayData.overdue || []).length > 0
                            text: Style.urgency ? I18n.count((root.dayData.overdue || []).length, "today.n.overdue")
                                                : I18n.t("today.overdueQuiet").arg((root.dayData.overdue || []).length)
                            color: Theme.textDim
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                            font.underline: odCA.hovered
                            ClickArea { id: odCA; label: parent.text; onActivated: root.overdueRequested() }
                        }
                        Text {
                            objectName: "today-deadlines-empty"
                            visible: root.deadlines.length === 0
                            text: I18n.t(root.dlTab === "upcoming" ? "today.dl.noneUpcoming" : "today.dl.noneToday")
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                        }
                        Repeater {
                            // Capped like the work in progress: thousands of rows
                            // froze the day on every change (DATA-11, EYES-3).
                            model: root.dlRows
                            delegate: ColumnLayout {
                                id: dlRow
                                required property var modelData
                                readonly property bool isDay: dlRow.modelData.kind === "day"
                                readonly property var t: dlRow.modelData.task || ({})
                                Layout.fillWidth: true
                                spacing: 0
                                // A day's heading (Q-List): the name, then the date dim.
                                Row {
                                    objectName: "today-deadlines-day"
                                    visible: dlRow.isDay
                                    topPadding: Theme.spSm
                                    spacing: Theme.spSm
                                    Accessible.role: Accessible.Heading
                                    Accessible.name: dayTitle.text + " " + daySub.text
                                    Text {
                                        id: dayTitle
                                        text: dlRow.isDay ? root.dueDayTitle(dlRow.modelData.day) : ""
                                        color: Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsSm
                                        font.weight: Theme.fwTitle
                                    }
                                    Text {
                                        id: daySub
                                        text: dlRow.isDay ? root.dueDaySub(dlRow.modelData.day) : ""
                                        color: Theme.textDim
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsXs
                                    }
                                }
                                TaskLine {
                                    objectName: "today-deadlines-task"
                                    visible: !dlRow.isDay
                                    task: dlRow.t
                                    sub: {
                                        const t = dlRow.t;
                                        const p = [];
                                        if (root.cards) p.push(t.id);
                                        // "заблокировано — ждёт ответа: Олег" (R3-002)
                                        const w = t.waiting ? I18n.t("today.waitingOn").arg(t.waiting) : "";
                                        const blocked = t.category === "blocked";
                                        if (blocked && root.cards) p.push(I18n.t("today.blocked") + (w ? " — " + w : ""));
                                        else if (w) p.push(w);
                                        else if (blocked) p.push(I18n.t("today.blocked"));
                                        if (root.cards && String(t.priority || "").length > 0) p.push(String(t.priority).toUpperCase());
                                        if (t.profileName) p.push(t.profileName);
                                        return root.stacked ? "" : p.join(" · ");
                                    }
                                    // Under a day's heading only the hour is new;
                                    // "Срок сегодня" in bold keeps its word.
                                    when: dlRow.isDay ? "" : root.dlTab === "upcoming" ? (dlRow.t.dueHasTime ? I18n.fmtTime(new Date(dlRow.t.due)) : "")
                                                                                        : (!root.cards ? "" : root._dueWord(dlRow.t.due))
                                    whenSignal: !dlRow.isDay && root._daysFromToday(new Date(dlRow.t.due)) <= 0
                                }
                            }
                        }
                        Text {
                            objectName: "today-deadlines-more"
                            visible: root.dlHidden > 0
                            text: I18n.t("week.more").arg(root.dlHidden)
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                            font.underline: dlMoreCA.hovered
                            ClickArea {
                                id: dlMoreCA
                                label: parent.text
                                role: Accessible.Link
                                onActivated: root.deadlinesRequested(root.deadlinesQuery())
                            }
                        }
                    }

                    // Whom to write. Folded in the quiet style (DG-013,
                    // DG-015) while anyone is on it; with nobody, the heading
                    // and the add link stay, so there is a way in from Today.
                    ColumnLayout {
                        id: peopleBox
                        objectName: "today-people"
                        Layout.alignment: Qt.AlignTop
                        // Stacked: packed at natural width, like flex-wrap (R3-014).
                        Layout.preferredWidth: root.stacked ? Theme.px(220) : -1
                        Layout.fillWidth: !root.stacked
                        visible: Style.todayExtras !== "hidden"
                        spacing: root.cards ? 0 : Theme.spSm
                        property bool open: Style.todayExtras === "open" && !root.stacked
                        readonly property bool folding: !root.cards && root.people.length > 0
                        readonly property bool shown: peopleBox.open || !peopleBox.folding
                        // Bold: a heading. Quiet: "▸ Кому написать · 2".
                        SideHead {
                            visible: !peopleBox.folding
                            Layout.bottomMargin: Theme.spXs
                            text: I18n.t("today.people")
                        }
                        Item {
                            visible: peopleBox.folding
                            Layout.fillWidth: true
                            implicitHeight: foldRow.implicitHeight
                            Row {
                                id: foldRow
                                spacing: Theme.spSm
                                Icon {
                                    visible: !root.stacked
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: peopleBox.open ? "chevron-down" : "chevron-right"
                                    size: Theme.fsSm
                                    color: Theme.textDim
                                }
                                Text {
                                    text: I18n.t("today.people") + " · " + root.people.length
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                }
                            }
                            ClickArea {
                                objectName: "today-people-fold"
                                label: I18n.t("today.people")
                                checkable: true
                                checked: peopleBox.open
                                onActivated: peopleBox.open = !peopleBox.open
                            }
                        }
                        Text {
                            visible: root.stacked && !peopleBox.open && root.people.length > 0
                            text: I18n.t("today.collapsed")
                            color: Theme.textDim
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsMd
                        }
                        Repeater {
                            model: peopleBox.shown ? root.people : []
                            delegate: RowLayout {
                                id: pr
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.topMargin: root.cards ? Theme.spSm : 0
                                Layout.bottomMargin: root.cards ? Theme.spSm : 0
                                spacing: Theme.spMd
                                TapHandler {
                                    objectName: "today-person-menu-tap"
                                    acceptedButtons: Qt.RightButton
                                    onTapped: personMenu.openFor(pr.modelData)
                                }
                                Rectangle {
                                    visible: root.cards
                                    implicitWidth: Theme.px(28); implicitHeight: Theme.px(28)
                                    radius: width / 2
                                    color: Theme.panel3
                                    Text {
                                        anchors.centerIn: parent
                                        text: String(pr.modelData.name).split(/\s+/).map(w => w.charAt(0)).join("").slice(0, 2).toUpperCase()
                                        color: Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsXs
                                        font.weight: Theme.fwHeading
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.cards
                                          ? "<b>" + pr.modelData.name + "</b>" + (pr.modelData.question ? " <font color=\"" + Theme.textDim + "\">— " + pr.modelData.question + "</font>" : "")
                                          : pr.modelData.name + (pr.modelData.question ? " — " + pr.modelData.question : "")
                                    textFormat: root.cards ? Text.StyledText : Text.PlainText
                                    elide: Text.ElideRight
                                    color: root.cards ? Theme.text : Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsMd
                                    ClickArea {
                                        objectName: "today-person-open"
                                        label: pr.modelData.name
                                        onActivated: root.peopleRequested(pr.modelData.id)
                                    }
                                }
                                // "Написал": an outlined button in bold, a
                                // text link in quiet.
                                PillButton {
                                    objectName: "today-wrote"
                                    visible: root.cards
                                    text: I18n.t("today.wrote")
                                    onClicked: AppController.setPersonState(pr.modelData.id, "pinged")
                                }
                                Text {
                                    objectName: "today-wrote-link"
                                    visible: !root.cards
                                    text: I18n.t("today.q.wrote")
                                    color: wroteCA.hovered ? Theme.text : Theme.textDim
                                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                    font.underline: true
                                    ClickArea { id: wroteCA; label: I18n.t("today.wrote"); onActivated: AppController.setPersonState(pr.modelData.id, "pinged") }
                                }
                            }
                        }
                        Text {
                            objectName: "today-person-add"
                            visible: peopleBox.shown
                            Layout.topMargin: root.cards && root.people.length > 0 ? Theme.spSm : 0
                            text: I18n.t("people.add")
                            color: addCA.hovered ? Theme.text : Theme.textDim
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                            ClickArea { id: addCA; label: I18n.t("people.add"); onActivated: root.personAddRequested() }
                        }
                    }

                    // The footer (DG-015): tasks without a date (bold), and
                    // the week's recap on its last working day (APP-211,
                    // owner: both styles).
                    FooterLink {
                        objectName: "today-undated"
                        // Quiet shows it too while the cursor is on it (A1-2).
                        visible: (root.cards || root.cursorKey === "undated" && root.cursorVisible) && (root.dayData.undated || 0) > 0
                        FocusRing {
                            objectName: "today-undated-cursor"
                            visible: root.cursorVisible && root.cursorKey === "undated"
                        }
                        text: I18n.t("today.undatedLook").arg(I18n.count(root.dayData.undated || 0, "today.n.undated"))
                        onActivated: root.undatedRequested()
                    }
                    FooterLink {
                        objectName: "today-recap"
                        visible: root.dayData.recapDay === true
                        divider: root.cards && !((root.dayData.undated || 0) > 0)
                        text: I18n.t("today.recap")
                        onActivated: root.recapRequested()
                    }
                    Item { visible: !root.stacked; Layout.fillHeight: true }
                }
            }
            // Quiet: the side stands right after the day (max 680 px), the
            // rest of the width stays empty (H2-Today-Calm).
            Item {
                visible: !root.stacked && root.plain
                Layout.fillWidth: true
                Layout.preferredWidth: 0
            }
        }
        // Small: the day and the side keep to the top.
        Item { visible: root.stacked && !root.firstRun; Layout.fillHeight: true }
    }

    function _step(n) {
        const d = root.day;
        AppController.selectedDate = new Date(d.getFullYear(), d.getMonth(), d.getDate() + n);
    }
    // Opened, the day shows "now" (A2-10): from the top when the rows above
    // it fit on the screen with it, so the untimed ones planned for the day
    // are not scrolled away; else centred on it, a whole row at the top
    // (EYES-5), and "N more above" says what is up there.
    function _revealNow() {
        for (let i = 0; i < root.rows.length; i++) {
            if (root.rows[i].kind !== "now") continue;
            dayList.positionViewAtBeginning();
            dayList.forceLayout();
            const it = dayList.itemAtIndex(i);
            if (it && it.y + it.height - dayList.originY <= dayList.height) return;
            dayList.positionViewAtIndex(i, ListView.Center);
            root._snapTopRow();
            return;
        }
    }
    function _snapTopRow() {
        const top = dayList.itemAt(dayList.width / 2, dayList.contentY + 1);
        if (top && top.y < dayList.contentY - 0.5) dayList.contentY = Math.max(dayList.originY, top.y);
    }
    // The meetings and tasks above the visible part of the day.
    readonly property int hiddenAbove: {
        const y = dayList.contentY;
        if (y <= dayList.originY + 0.5 || dayList.count === 0) return 0;
        let n = 0;
        for (let i = 0; i < root.rows.length; i++) {
            const r = root.rows[i];
            if (r.kind !== "meeting" && r.kind !== "task" && r.kind !== "allday") continue;
            const it = dayList.itemAtIndex(i);
            if (!it || it.y < y - 0.5) n++;
            else break;
        }
        return n;
    }
    // ‹ ›: a box in bold, a bare chevron in quiet.
    component NavBtn: Rectangle {
        id: nb
        property string icon: ""
        property string label: ""
        property string shortcutId: ""
        signal activated()
        implicitWidth: Theme.chipH; implicitHeight: Theme.chipH
        radius: Theme.radiusMd
        color: nbCA.hovered && !root.plain ? Theme.panel2 : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        border.color: Theme.border
        border.width: root.plain ? 0 : 1
        Icon {
            anchors.centerIn: parent
            name: nb.icon
            size: Theme.fsMd
            color: root.plain && !nbCA.hovered ? Theme.textDim : Theme.textMuted
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        }
        ClickArea { id: nbCA; label: nb.label; shortcutId: nb.shortcutId; onActivated: nb.activated() }
    }

    // Another profile's row (A1-17): a dot in that profile's colour before
    // the title; its name is in the caption.
    component ProfileDot: Rectangle {
        property var block: ({})
        visible: root._isOther(block)
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: Theme.spSm
        implicitHeight: Theme.spSm
        radius: width / 2
        color: block && block.profileColor ? block.profileColor : Theme.textDim
    }

    // A heading on the side: the bold section title, or quiet's small grey
    // words.
    component SideHead: Text {
        color: Theme.textDim
        font.family: Theme.fontUi
        font.pixelSize: root.cards ? Theme.fsMd : Theme.fsSm
        font.weight: root.cards ? Theme.fwHeading : Theme.fwBody
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    // A running timer: an amber dot in bold, the time alone in quiet.
    component TimerMark: Row {
        id: tm
        property string taskId: ""
        property int _tick: 0
        spacing: Theme.spXs
        Timer { interval: 1000; repeat: true; running: tm.visible && root.visible; onTriggered: tm._tick++ }
        Rectangle {
            visible: Style.urgency
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.spSm; height: Theme.spSm; radius: width / 2
            color: Theme.signalNow
        }
        Text {
            objectName: "today-timer"
            text: root._timer(tm.taskId, tm._tick + root._rev)
            color: Style.urgency ? Theme.signalNow : Theme.textDim
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsXs
        }
    }

    // A line at the foot of the side: a hairline above it in bold, the
    // text, → on the right.
    component FooterLink: Item {
        id: fl
        property string text: ""
        property bool divider: root.cards
        signal activated()
        Layout.columnSpan: root.stacked ? 3 : 1
        Layout.fillWidth: true
        implicitHeight: flRow.implicitHeight + (root.cards ? 2 * Theme.spLg : 0)
        Rectangle {
            visible: fl.divider
            width: parent.width
            height: 1
            color: Theme.border
        }
        RowLayout {
            id: flRow
            anchors.left: parent.left
            anchors.right: root.cards ? parent.right : undefined
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spSm
            Text {
                Layout.fillWidth: root.cards
                text: fl.text
                color: flCA.hovered ? Theme.text : (root.cards ? Theme.textMuted : Theme.textDim)
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                font.family: Theme.fontUi
                font.pixelSize: root.cards ? Theme.fsMd : Theme.fsSm
                font.underline: !root.cards
            }
            Icon {
                name: "arrow-right"
                size: root.cards ? Theme.fsLg : Theme.fsSm
                color: root.cards ? Theme.textMuted : Theme.textDim
            }
        }
        ClickArea { id: flCA; label: fl.text; role: Accessible.Link; onActivated: fl.activated() }
    }

    // A task line on the side: its status mark (a click = Done), title,
    // the facts under it, when on the right; a hairline under it in bold.
    component TaskLine: Item {
        id: tl
        property var task: ({})
        property string sub: ""
        property string when: ""
        property bool whenSignal: false
        Layout.fillWidth: true
        implicitHeight: tlRow.implicitHeight + (root.cards ? 2 * Theme.spMd : 0)
        RowLayout {
            id: tlRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spMd
            Item {
                visible: !root.stacked
                implicitWidth: Theme.statusRingSize; implicitHeight: Theme.statusRingSize
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Theme.spXs / 2
                StatusRing { size: root.cards ? Theme.statusRingSize : Theme.statusRingSize - 2; category: tl.task.category || "todo" }
                ClickArea { enabled: !root._isOther(tl.task); label: I18n.t("taskmenu.done"); shortcutId: "task.done"; onActivated: AppController.toggleDone([tl.task.id]) }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spXs / 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spSm
                    ProfileDot { block: tl.task }
                    Text {
                        Layout.fillWidth: true
                        text: tl.task.title || ""
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                        font.weight: root.cards ? Theme.fwTitle : Theme.fwBody
                        ClickArea { label: parent.text; onActivated: root._openTask(tl.task) }
                    }
                }
                Text {
                    visible: tl.sub.length > 0
                    Layout.fillWidth: true
                    text: tl.sub
                    elide: Text.ElideRight
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
            }
            Text {
                visible: tl.when.length > 0
                Layout.alignment: Qt.AlignTop
                text: tl.when
                color: tl.whenSignal ? Theme.signalUrgent : Theme.signalNow
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                font.weight: tl.whenSignal ? Theme.fwHeading : Theme.fwBody
            }
        }
        Rectangle {
            visible: root.cards
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    // One row of the day.
    component DayRow: Item {
        id: dr
        required property var modelData
        required property int index
        width: ListView.view.width
        readonly property var b: dr.modelData.block || ({})
        readonly property bool item: dr.modelData.kind === "meeting" || dr.modelData.kind === "task" || dr.modelData.kind === "allday"
        readonly property bool meeting: dr.modelData.kind !== "task"
        // As wide as its longest word (A2-26, A6-14): "без времени" was cut
        // to "без врем…" in a 52 px gutter.
        readonly property int timeW: Math.max(root.plain ? Theme.px(44) : Theme.px(52), root.timeLabelW)
        // Quiet: a free window is a gap, the end of the day is not drawn;
        // a small window keeps no gaps at all (DG-012, X-Oth-Small).
        implicitHeight: {
            const k = dr.modelData.kind;
            if (dr.item) {
                if (root.stacked) return Theme.px(30);
                return root.plain ? plainBody.implicitHeight + 2 * Theme.spMd
                                  : Math.max(Theme.px(48), body.implicitHeight + 2 * Theme.spMd);
            }
            if (k === "now") return root.plain ? Theme.spLg : Theme.px(20);
            if (root.plain) return k === "free" && !root.stacked && (dr.modelData.end - dr.modelData.start) >= 1 ? Theme.px(22) : 0;
            return Theme.px(30);
        }
        visible: implicitHeight > 0
        // Past: the title steps down to muted; the time and the line under it
        // stay at textDim, which holds AA (a 0.55 opacity read 2.2:1, EYES-6).

        Text {
            id: timeT
            visible: dr.item || !root.plain
            width: dr.timeW
            horizontalAlignment: Text.AlignRight
            anchors.top: dr.item && !root.stacked ? parent.top : undefined
            anchors.topMargin: dr.item ? (root.stacked ? 0 : root.plain ? Theme.spMd + Theme.spXs / 2 : Theme.spMd + Theme.spXs) : 0
            anchors.verticalCenter: dr.item && !root.stacked ? undefined : parent.verticalCenter
            text: dr.modelData.kind === "allday" ? I18n.t("today.allDay")
                : dr.b.dayOnly ? I18n.t("today.noTime")
                : dr.b.fromPrevDay ? I18n.t("today.fromPrev")
                : root._hm(dr.modelData.start)
            elide: Text.ElideRight
            color: dr.modelData.kind === "now" ? Theme.signalNow : dr.item && !root.plain ? Theme.textMuted : Theme.textDim
            // The UI font with even figures, as the week's blocks: the mono
            // font's dotted zero ran into an 8 at 11 px (A2-27).
            font.family: Theme.fontUi
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsXs
        }

        // ── bold: a meeting card with a bar, a task card with its ring ──
        Rectangle {
            id: blockBox
            visible: dr.item && !root.plain
            anchors.left: timeT.right
            anchors.leftMargin: Theme.spLg
            anchors.right: parent.right
            height: parent.height
            radius: Theme.radiusLg
            color: dr.meeting && Style.fills ? Theme.meetingFill : "transparent"
            border.color: dr.meeting && Style.fills ? "transparent" : Theme.border
            border.width: 1
            Rectangle {
                visible: dr.meeting
                anchors.left: parent.left; anchors.leftMargin: Theme.spLg
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.cursorBarH; height: parent.height - 2 * Theme.spMd
                radius: width / 2
                color: Theme.meeting
            }
            RowLayout {
                id: body
                anchors.left: parent.left
                anchors.leftMargin: dr.meeting ? Theme.spXl + Theme.spMd : Theme.spLg
                anchors.right: parent.right
                anchors.rightMargin: Theme.spLg
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spMd
                Item {
                    visible: !dr.meeting
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Theme.spXs / 2
                    implicitWidth: Theme.statusRingSize; implicitHeight: Theme.statusRingSize
                    StatusRing { category: dr.b.category || "todo" }
                    ClickArea { enabled: !root._isOther(dr.b); label: I18n.t("taskmenu.done"); onActivated: AppController.toggleDone([dr.b.id]) }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spXs / 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spSm
                        ProfileDot { block: dr.b }
                        Text {
                            Layout.fillWidth: true
                            text: dr.b.title || ""
                            elide: Text.ElideRight
                            color: dr.b.past ? Theme.textMuted : Theme.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsMd
                            // The sheets set day rows at 500 (R4-003).
                            font.weight: Theme.fwTitle
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root._sub(dr.modelData.kind, dr.b)
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignTop
                    visible: dr.modelData.kind !== "allday" && !dr.b.dayOnly
                    text: root._rowLen(dr.b)
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
            }
        }

        // ── quiet: an icon, the title, the caption; one line when small ──
        RowLayout {
            id: plainBody
            visible: dr.item && root.plain
            anchors.left: timeT.right
            anchors.leftMargin: Theme.spLg
            anchors.right: parent.right
            anchors.top: root.stacked ? undefined : parent.top
            anchors.topMargin: Theme.spMd
            anchors.verticalCenter: root.stacked ? parent.verticalCenter : undefined
            spacing: Theme.spLg
            Item {
                visible: !root.stacked
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Theme.spXs / 2
                implicitWidth: Theme.statusRingSize; implicitHeight: Theme.statusRingSize
                MeetingIcon { visible: dr.meeting; anchors.centerIn: parent; ink: Theme.textMuted }
                StatusRing { visible: !dr.meeting; anchors.centerIn: parent; size: Theme.statusRingSize - 1; category: dr.b.category || "todo" }
                ClickArea { enabled: !dr.meeting && !root._isOther(dr.b); label: I18n.t("taskmenu.done"); onActivated: AppController.toggleDone([dr.b.id]) }
            }
            ColumnLayout {
                visible: !root.stacked
                Layout.fillWidth: true
                spacing: Theme.spXs / 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spSm
                    ProfileDot { block: dr.b }
                    Text {
                        Layout.fillWidth: true
                        text: dr.b.title || ""
                        elide: Text.ElideRight
                        color: dr.b.past ? Theme.textMuted : Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsLg
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root._sub(dr.modelData.kind, dr.b)
                    elide: Text.ElideRight
                    color: Theme.textDim
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
            }
            Text {
                visible: root.stacked
                Layout.fillWidth: true
                text: (dr.b.title || "") + "<font color=\"" + Theme.textDim + "\"> · " + root._sub(dr.modelData.kind, dr.b) + "</font>"
                textFormat: Text.StyledText
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
                font.weight: Theme.fwTitle
            }
        }

        FocusRing {
            objectName: "today-cursor"
            anchors.fill: undefined
            x: root.plain ? 0 : blockBox.x
            width: root.plain ? parent.width : blockBox.width
            height: parent.height
            visible: dr.item && root.cursorVisible && root.cursorKey.length > 0 && root.cursorKey === root._rowKey(dr.modelData)
        }
        ClickArea {
            visible: dr.item
            anchors.fill: undefined
            x: root.plain ? (root.stacked ? 0 : timeT.width + Theme.spLg + Theme.statusRingSize + Theme.spLg)
                          : blockBox.x + (dr.meeting ? 0 : Theme.spLg + Theme.statusRingSize + Theme.spMd)
            width: parent.width - x
            height: parent.height
            label: dr.b.title || ""
            onActivated: dr.meeting ? root.eventClicked(dr.b.id, dr.b.occurrence || null) : root._openTask(dr.b)
        }

        // Bold: a free window and the end of the day, facts beside a dashed
        // line (a-19, H2-Today: 1px dashed, not solid).
        Shape {
            objectName: "today-free-rule"
            visible: !root.plain && (dr.modelData.kind === "free" || dr.modelData.kind === "end")
            anchors.left: timeT.right
            anchors.leftMargin: Theme.spLg
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            ShapePath {
                strokeColor: Theme.borderStrong
                strokeWidth: 1
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 3]
                fillColor: "transparent"
                startX: 0.5; startY: 0
                PathLine { x: 0.5; y: dr.height }
            }
        }
        Text {
            visible: !root.plain && (dr.modelData.kind === "free" || dr.modelData.kind === "end")
            anchors.left: timeT.right
            anchors.leftMargin: Theme.spLg + Theme.spLg
            anchors.verticalCenter: parent.verticalCenter
            text: dr.modelData.kind === "free"
                  ? I18n.t("today.free").arg(root._len(dr.modelData.start, dr.modelData.end || dr.modelData.start))
                  : I18n.t("today.endOfDay").arg(root._hm(dr.modelData.start))
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }
        // Now: amber with a dot in bold; a thin grey line with the time on
        // the right in quiet.
        Rectangle {
            id: nowLine
            visible: dr.modelData.kind === "now"
            anchors.left: timeT.right
            anchors.leftMargin: root.plain ? Theme.spLg : Theme.spMd
            anchors.right: root.plain ? nowTime.left : parent.right
            anchors.rightMargin: root.plain ? Theme.spLg : 0
            anchors.verticalCenter: parent.verticalCenter
            height: root.plain ? 1 : 2
            color: root.plain ? Theme.borderStrong : Theme.nowLineColor
            Rectangle {
                visible: !root.plain
                width: Theme.spSm; height: Theme.spSm; radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.nowLineColor
            }
        }
        Text {
            id: nowTime
            visible: dr.modelData.kind === "now" && root.plain
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root._hm(dr.modelData.start)
            color: Theme.textDim
            font.family: Theme.fontUi
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsXs
        }
    }
}
