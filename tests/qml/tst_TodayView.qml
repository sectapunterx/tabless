// Today (APP-260): the day by the hours, facts with only the non-zero parts,
// plurals through I18n, the side sections hidden when empty.
import QtQuick
import QtQuick.Shapes
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TodayView"
    when: windowShown
    visible: true
    // Wider than the small layout (Theme.compactWindowWidth).
    width: 1500
    height: 800

    Item { id: host; anchors.fill: parent }
    property var tasks: []
    property var events: []
    property date savedDate
    function initTestCase() {
        tc.savedDate = AppController.selectedDate;
        // A fresh test profile is the first run (FirstRunHero); these tests
        // look at the day itself.
        AppController.markWelcomeSeen();
    }
    function cleanup() {
        for (const id of tc.tasks) AppController.deleteTask(id);
        for (const id of tc.events) AppController.deleteEvent(id);
        tc.tasks = []; tc.events = [];
        AppController.clearPendingUndo();
        AppController.selectedDate = tc.savedDate;
    }
    // A Wednesday a year ahead: nothing of the demo on it.
    function probeDay() {
        const d = new Date(); d.setFullYear(d.getFullYear() + 1); d.setHours(0, 0, 0, 0);
        while (d.getDay() !== 3) d.setDate(d.getDate() + 1);
        return d;
    }

    function test_plurals() {
        if (I18n.lang === "ru") {
            compare(I18n.count(1, "today.n.meetings"), "1 встреча");
            compare(I18n.count(3, "today.n.meetings"), "3 встречи");
            compare(I18n.count(5, "today.n.meetings"), "5 встреч");
            compare(I18n.count(11, "today.n.meetings"), "11 встреч");
            compare(I18n.count(21, "today.n.meetings"), "21 встреча");
        } else {
            compare(I18n.count(1, "today.n.meetings"), "1 meeting");
            compare(I18n.count(2, "today.n.meetings"), "2 meetings");
        }
    }

    function test_the_day_shows_blocks_free_time_and_facts() {
        const day = tc.probeDay();
        AppController.selectedDate = day;
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = "today probe";
        d.scheduledAt = new Date(day.getFullYear(), day.getMonth(), day.getDate(), 14, 0);
        d.scheduledHasTime = true;
        d.estimateMinutes = 90;
        AppController.saveTask(d);
        tc.tasks.push(d.id);
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { anchors.fill: parent }', host);
        const kinds = v.rows.map(r => r.kind);
        verify(kinds.indexOf("task") >= 0, kinds);
        verify(kinds.indexOf("free") >= 0, "no free window: " + kinds);
        verify(kinds.indexOf("end") >= 0, "no end of the working day");
        verify(kinds.indexOf("now") < 0, "a day to come has no now line");
        const facts = findChild(v, "today-facts").text;
        compare(facts, I18n.count(1, "today.n.planned"), "only the non-zero parts");
        verify(findChild(v, "today-back").visible, "another day offers the way back");
        compare(v.dayData.load.tasks, 90);
        // a-19 (H2-Today): the free window hangs on a dashed rule.
        const rule = findChild(v, "today-free-rule");
        verify(rule !== null, "no rule beside the free window");
        compare(rule.data[0].strokeStyle, ShapePath.DashLine);
    }

    // R3-001: a task due today that also has a block in the day is still
    // listed under the deadlines, as the facts line counts it.
    function test_a_planned_deadline_is_still_listed() {
        const day = tc.probeDay();
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = "planned and due probe";
        d.scheduledAt = new Date(day.getFullYear(), day.getMonth(), day.getDate(), 11, 0);
        d.scheduledHasTime = true;
        d.dueAt = new Date(day.getFullYear(), day.getMonth(), day.getDate(), 18, 0);
        d.dueHasTime = true;
        verify(AppController.saveTask(d));
        tc.tasks.push(d.id);
        const data = AppController.todayData(day, false);
        verify(data.blocks.some(b => b.id === d.id), "the task has a block in the day");
        verify(data.deadlines.some(t => t.id === d.id), "and is listed under the deadlines");
        verify(data.facts.dueToday >= 1);
    }

    function test_an_empty_day_says_so_in_one_line() {
        AppController.selectedDate = tc.probeDay();
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { anchors.fill: parent }', host);
        compare(findChild(v, "today-facts").text, I18n.t("today.nothing"));
        // DG-010: no second "load" line under the facts.
        compare(findChild(v, "today-load"), null);
    }

    // "Кому написать" stays with nobody on it, and its "+" asks for a
    // new person: Today was a dead end for adding someone.
    function test_people_block_offers_add_even_when_empty() {
        AppController.selectedDate = tc.probeDay();
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { width: 1300; height: 800 }', host);
        const add = findChild(v, "today-person-add");
        verify(add !== null);
        if (v.people.length === 0) verify(findChild(v, "today-people").visible, "the block hides when nobody is on it");
        const spy = createTemporaryQmlObject('import QtTest; SignalSpy { signalName: "personAddRequested" }', host);
        spy.target = v;
        v.personAddRequested();
        compare(spy.count, 1);
    }

    function probeMeeting(day) {
        const ev = AppController.newEventDraft(10, day);
        ev.title = "today probe sync";
        ev.type = "sync";
        ev.end = 10.5;
        ev.date = day;
        ev.attendees = "Zoom";
        AppController.saveEvent(ev);
        tc.events.push(ev.id);
    }

    // DG-012: quiet draws plain rows with an icon and a lowercase caption,
    // bold the cards with "Встреча"; the arrows are boxed only in bold.
    function test_quiet_and_bold_draw_the_day_their_own_way() {
        const saved = AppController.appSettingsJson;
        try {
            const day = tc.probeDay();
            AppController.selectedDate = day;
            tc.probeMeeting(day);
            Style.apply("quiet");
            const v = createTemporaryQmlObject('import TodoCpp; TodayView { width: 1300; height: 800 }', host);
            verify(v.plain, "quiet is not plain");
            // a-24 (H2-Today-Calm): 36px under the header, not 16.
            const head = findChild(v, "today-header");
            const dayRows = findChild(v, "today-day");
            waitForRendering(v);
            if (!v.stacked && !findChild(v, "today-more-above").visible && !findChild(v, "today-empty").visible)
                fuzzyCompare(dayRows.mapToItem(v, 0, 0).y - head.mapToItem(v, 0, head.height).y, Theme.sp3xl + Theme.spXl, 1);
            const m = v.rows.filter(r => r.kind === "meeting")[0];
            verify(!!m, "no meeting row");
            // The type in the editor's words (A3-20): "team sync", not "meeting".
            compare(v._sub("meeting", m.block), [v._typeName(m.block, true), I18n.fmtMinutes(30), "Zoom"].join(" · "));
            compare(findChild(v, "today-prev").border.width, 0, "quiet arrows are boxed");
            Style.apply("bold");
            verify(!v.plain);
            compare(v._sub("meeting", m.block), [v._typeName(m.block, false), "Zoom"].join(" · "));
            compare(findChild(v, "today-prev").border.width, 1);
        } finally {
            AppController.appSettingsJson = saved;
        }
    }

    // DG-014: bold shows one in-progress card; the rest are lines.
    function test_one_card_for_what_is_in_progress() {
        const saved = AppController.appSettingsJson;
        try {
            Style.apply("bold");
            for (const n of ["a", "b"]) {
                const d = AppController.newTaskDraft("prog");
                d._isNew = true;
                d.title = "in progress probe " + n;
                AppController.saveTask(d);
                tc.tasks.push(d.id);
            }
            const v = createTemporaryQmlObject('import TodoCpp; TodayView { width: 1300; height: 800 }', host);
            verify(v.inProgress.length >= 2, "fewer than two in progress");
            verify(findChild(v, "today-lead").visible);
            compare(v.otherInProgress.length, v.inProgress.length - 1);
        } finally {
            AppController.appSettingsJson = saved;
        }
    }

    // a-18 (H2-Today): the in-progress card reads ID · PR · CI · timer; the
    // branch name is never on it, PR or not (no branch on cards).
    function test_the_lead_card_never_names_the_branch() {
        const saved = AppController.appSettingsJson;
        try {
            Style.apply("bold");
            const d = AppController.newTaskDraft("prog");
            d._isNew = true;
            d.title = "branch probe";
            d.branch = "fix/branch-probe-a18";
            AppController.saveTask(d);
            tc.tasks.push(d.id);
            const v = createTemporaryQmlObject('import TodoCpp; TodayView { width: 1300; height: 800 }', host);
            const lead = findChild(v, "today-lead");
            verify(lead.visible);
            const texts = [];
            (function walk(it) {
                if (it.text !== undefined && it.visible) texts.push(String(it.text));
                for (const c of it.children) walk(c);
            })(lead);
            verify(texts.every(s => s.indexOf("branch-probe-a18") < 0), texts.join(" | "));
        } finally {
            AppController.appSettingsJson = saved;
        }
    }

    function test_wrote_is_past_tense() {
        if (I18n.lang === "ru") compare(I18n.t("today.wrote"), "Написал");
        else compare(I18n.t("today.wrote"), "Wrote");
    }

    // The weekly recap is offered on the week's last working day only
    // (Mon-Fri by default: Friday yes, Thursday and Saturday no).
    function test_the_recap_line_is_on_the_last_working_day() {
        compare(AppController.todayData(new Date(2026, 9, 9), false).recapDay, true, "Friday");
        compare(AppController.todayData(new Date(2026, 9, 8), false).recapDay, false, "Thursday");
        compare(AppController.todayData(new Date(2026, 9, 10), false).recapDay, false, "Saturday");
    }

    // IDIOT-CAL-1: Return on a meeting of a series hands its day's occurrence
    // up, not null (which opened the whole series on its first day).
    function test_a_series_meeting_opens_its_occurrence() {
        const day = tc.probeDay();
        const first = new Date(day); first.setDate(first.getDate() - 3);
        const ev = AppController.newEventDraft(8, first);
        ev.title = "today series probe";
        ev.end = 8.5;
        ev.date = first;
        ev.rrule = "FREQ=DAILY";
        AppController.saveEvent(ev);
        tc.events.push(ev.id);
        AppController.selectedDate = day;
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { anchors.fill: parent }', host);
        const spy = createTemporaryQmlObject('import QtTest; SignalSpy { signalName: "eventClicked" }', host);
        spy.target = v;
        v.moveCursor(0, 0);
        const items = v._items();
        const at = items.findIndex(it => it.id === ev.id);
        verify(at >= 0, "the meeting is a cursor stop");
        v._placeIdx(at);
        v.openCursor();
        compare(spy.count, 1);
        compare(spy.signalArguments[0][1], Qt.formatDate(day, "yyyy-MM-dd"));
    }

    // IDIOT-CAL-7: the two parts of an overnight meeting are two cursor stops;
    // j walks down to the evening part and stops there.
    function test_j_reaches_the_evening_part_of_an_overnight_meeting() {
        const day = tc.probeDay();
        const first = new Date(day); first.setDate(first.getDate() - 3);
        const ev = AppController.newEventDraft(23, first);
        ev.title = "overnight probe";
        ev.start = 23;
        ev.end = 1;
        ev.date = first;
        ev.endDate = new Date(first.getFullYear(), first.getMonth(), first.getDate() + 1);
        ev.rrule = "FREQ=DAILY";
        AppController.saveEvent(ev);
        tc.events.push(ev.id);
        AppController.selectedDate = day;
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { anchors.fill: parent }', host);
        const parts = v._items().filter(it => it.id === ev.id);
        compare(parts.length, 2, "the 00:00 part and the 23:00 one");
        verify(parts[0].key !== parts[1].key);
        v.moveCursor(0, 0);
        for (let i = 0; i < v._items().length + 2; i++) v.moveCursor(0, 1);
        compare(v.cursorKey, v._items()[v._items().length - 1].key, "the walk ends on the last row");
    }

    // IDIOT-CAL-10: a task planned for the day without a time is on that
    // day's Today, as on the week's column, and the cursor reaches it.
    function test_a_date_only_task_is_on_its_day() {
        const day = tc.probeDay();
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = "date only probe";
        d.scheduledAt = new Date(day.getFullYear(), day.getMonth(), day.getDate(), 0, 0);
        d.scheduledHasTime = false;
        verify(AppController.saveTask(d));
        tc.tasks.push(d.id);
        const data = AppController.todayData(day, false);
        verify((data.dayOnly || []).some(t => t.id === d.id), "listed for the day");
        compare(data.facts.planned, 1);
        AppController.selectedDate = day;
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { anchors.fill: parent }', host);
        verify(!v.dayEmpty);
        verify(v._items().some(it => it.kind === "task" && it.id === d.id), "a cursor stop");
    }


    // APP-287: "N overdue" under Deadlines opened the list filtered to
    // tasks without a date.
    function test_overdue_link_asks_for_overdue_not_undated() {
        AppController.selectedDate = new Date();
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = "today overdue probe";
        const y = new Date(); y.setDate(y.getDate() - 2); y.setHours(0, 0, 0, 0);
        d.dueAt = y;
        AppController.saveTask(d);
        tc.tasks.push(d.id);
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { width: 1300; height: 800 }', host);
        const link = findChild(v, "today-overdue");
        tryVerify(function () { return link.visible; }, 2000, "no overdue link");
        const od = createTemporaryQmlObject('import QtQuick; import QtTest; SignalSpy { signalName: "overdueRequested" }', host);
        const un = createTemporaryQmlObject('import QtQuick; import QtTest; SignalSpy { signalName: "undatedRequested" }', host);
        od.target = v; un.target = v;
        let area = null;
        for (let i = 0; i < link.children.length; i++)
            if (link.children[i].activated !== undefined) area = link.children[i];
        verify(area !== null);
        area.activated();
        compare(od.count, 1);
        compare(un.count, 0);
    }

    // ── "Ближайшие" (dogfood 2026-10-19) ─────────────────────────────
    // The calendar settings these tests set; put back as they were.
    function _withCalendar(values, fn) {
        const saved = AppController.appSettingsJson;
        let s = {};
        try { s = JSON.parse(saved || "{}") || {}; } catch (e) { s = {}; }
        s.calendar = Object.assign({}, s.calendar || {}, values);
        AppController.appSettingsJson = JSON.stringify(s);
        try { fn(); } finally { AppController.appSettingsJson = saved; }
    }
    function _dueProbe(title, day, offset, hour) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        d.dueAt = new Date(day.getFullYear(), day.getMonth(), day.getDate() + offset, hour >= 0 ? hour : 0, 0);
        d.dueHasTime = hour >= 0;
        verify(AppController.saveTask(d));
        tc.tasks.push(d.id);
        return d.id;
    }

    function test_upcoming_deadlines_reach_the_horizon_by_day() {
        const day = tc.probeDay();
        const a = tc._dueProbe("upcoming probe d0", day, 0, -1);
        const b = tc._dueProbe("upcoming probe d2", day, 2, 10);
        const c = tc._dueProbe("upcoming probe d5", day, 5, -1);
        const far = tc._dueProbe("upcoming probe d9", day, 9, -1);
        tc._withCalendar({ deadlineHorizon: 7 }, function () {
            compare(AppController.deadlineHorizon(), 7);
            const data = AppController.todayData(day, false);
            compare(data.deadlineHorizon, 7);
            const ids = data.deadlines.map(t => t.id);
            verify(ids.indexOf(a) >= 0 && ids.indexOf(b) >= 0 && ids.indexOf(c) >= 0, ids);
            verify(ids.indexOf(far) < 0, "nine days ahead is past a 7-day horizon");
            verify(ids.indexOf(a) < ids.indexOf(b) && ids.indexOf(b) < ids.indexOf(c), "by day");
            // Each day says where it starts in the flat list.
            for (const g of data.deadlineDays) {
                for (let i = g.first; i < g.first + g.count; i++) {
                    const due = new Date(data.deadlines[i].due);
                    const gd = new Date(g.date);
                    compare(due.toDateString(), gd.toDateString(), "a row under another day's heading");
                }
            }
        });
        tc._withCalendar({ deadlineHorizon: 14 }, function () {
            verify(AppController.todayData(day, false).deadlines.some(t => t.id === far), "a 14-day horizon reaches it");
        });
        tc._withCalendar({ deadlineHorizon: 1 }, function () {
            const ids = AppController.todayData(day, false).deadlines.map(t => t.id);
            verify(ids.indexOf(a) >= 0 && ids.indexOf(b) < 0, "1 = through tomorrow, as before");
        });
    }

    function test_horizon_setting_snaps_to_a_choice() {
        tc._withCalendar({ deadlineHorizon: 10 }, function () { compare(AppController.deadlineHorizon(), 7); });
        tc._withCalendar({ deadlineHorizon: "banana" }, function () { compare(AppController.deadlineHorizon(), 7); });
        tc._withCalendar({ deadlineHorizon: 30 }, function () { compare(AppController.deadlineHorizon(), 30); });
        const choices = AppController.deadlineHorizonChoices();
        verify(choices.indexOf(7) >= 0, choices);
    }

    function test_tabs_split_today_from_upcoming() {
        const day = tc.probeDay();
        const a = tc._dueProbe("tab probe today", day, 0, 9);
        const b = tc._dueProbe("tab probe later", day, 3, -1);
        AppController.selectedDate = day;
        tc._withCalendar({ deadlineHorizon: 7, deadlinesTab: "upcoming" }, function () {
            const v = createTemporaryQmlObject('import TodoCpp; TodayView { width: 1300; height: 800 }', host);
            compare(v.dlTab, "upcoming");
            const kinds = v.dlRows.map(r => r.kind);
            compare(kinds[0], "day", "a day's heading first");
            const taskIds = v.dlRows.filter(r => r.kind === "task").map(r => r.task.id);
            verify(taskIds.indexOf(a) >= 0 && taskIds.indexOf(b) >= 0, taskIds);
            compare(v.dlRows.filter(r => r.kind === "day").length, 2);
            verify(findChild(v, "today-deadlines-horizon").visible, "the horizon beside the tabs");
            // The other tab: the shown day's only, no day headings.
            v.setDeadlinesTab("today");
            tryCompare(v, "dlTab", "today");
            const todayIds = v.dlRows.map(r => r.kind === "task" ? r.task.id : "day");
            compare(todayIds.indexOf("day"), -1);
            verify(todayIds.indexOf(a) >= 0 && todayIds.indexOf(b) < 0, todayIds);
            verify(!findChild(v, "today-deadlines-horizon").visible);
            compare(v.deadlinesQuery().indexOf("due:<="), -1, "today's list is that day's");
            // Picking a horizon goes back to "Ближайшие" and rebuilds.
            v.setDeadlineHorizon(3);
            tryCompare(v, "dlTab", "upcoming");
            tryCompare(v, "dlHorizon", 3);
        });
    }

    function test_day_headings_name_the_day() {
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { width: 1300; height: 800 }', host);
        const t = AppController.today;
        const at = (n) => new Date(t.getFullYear(), t.getMonth(), t.getDate() + n);
        compare(v.dueDayTitle({ kind: "today", date: at(0) }), I18n.t("common.today"));
        compare(v.dueDayTitle({ kind: "tomorrow", date: at(1) }), I18n.t("common.tomorrow"));
        const w = v.dueDayTitle({ kind: "weekday", date: at(3) });
        compare(w.charAt(0), w.charAt(0).toUpperCase(), "a weekday is capitalised");
        compare(w.toLowerCase(), I18n.locale.standaloneDayName(at(3).getDay(), Locale.LongFormat).toLowerCase());
        compare(v.dueDayTitle({ kind: "date", date: at(9) }), I18n.fmtDate(at(9), "dayMonth"));
        compare(v.horizonLabel(1), I18n.t("today.horizon.tomorrow"));
        compare(v.horizonLabel(7), I18n.count(7, "today.horizon.days"));
    }

    // "ещё N" opens Tasks · List with every deadline the tab holds.
    function test_more_opens_the_list_with_all_of_them() {
        const day = tc.probeDay();
        const ids = [];
        for (let i = 0; i < 10; i++) ids.push(tc._dueProbe("more probe " + i, day, i % 4, -1));
        AppController.selectedDate = day;
        tc._withCalendar({ deadlineHorizon: 7, deadlinesTab: "upcoming" }, function () {
            const v = createTemporaryQmlObject('import TodoCpp; TodayView { width: 1300; height: 800 }', host);
            compare(v.dlRows.filter(r => r.kind === "task").length, v._dlCap);
            verify(v.dlHidden >= 2, v.dlHidden);
            const more = findChild(v, "today-deadlines-more");
            verify(more.visible);
            const spy = createTemporaryQmlObject('import QtQuick; import QtTest; SignalSpy { signalName: "deadlinesRequested" }', host);
            spy.target = v;
            let area = null;
            for (let i = 0; i < more.children.length; i++)
                if (more.children[i].activated !== undefined) area = more.children[i];
            verify(area !== null);
            area.activated();
            compare(spy.count, 1);
            const q = spy.signalArguments[0][0];
            verify(q.indexOf("due:<=") >= 0, q);
            const listed = AppController.taskListRows(q, [], false, "date").filter(r => r.kind === "task").map(r => r.id);
            for (const id of ids) verify(listed.indexOf(id) >= 0, "missing from the list: " + id + " for " + q);
        });
    }
}
