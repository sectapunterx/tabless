// The 0.9 acceptance fixes for Today, the calendars, end of day and the
// weekly recap (APP-221 batch B3): what each screen says about a day must be
// true, and every item of it reachable.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "ZDiag05"
    when: windowShown
    visible: true
    width: 1500
    height: 800

    Item { id: host; anchors.fill: parent }
    property var tasks: []
    property var events: []
    property date savedDate

    function initTestCase() {
        tc.savedDate = AppController.selectedDate;
        AppController.markWelcomeSeen();
    }
    function cleanup() {
        for (const id of tc.tasks) AppController.deleteTask(id);
        for (const id of tc.events) AppController.deleteEvent(id);
        tc.tasks = []; tc.events = [];
        AppController.clearPendingUndo();
        AppController.selectedDate = tc.savedDate;
    }
    // A Wednesday two years ahead: nothing of the demo on it.
    function probeDay() {
        const d = new Date(); d.setFullYear(d.getFullYear() + 2); d.setHours(0, 0, 0, 0);
        while (d.getDay() !== 3) d.setDate(d.getDate() + 1);
        return d;
    }
    function addEvent(title, day, start, end, fields) {
        const ev = AppController.newEventDraft(start, day);
        ev.title = title;
        ev.date = day;
        ev.start = start;
        ev.end = end;
        Object.assign(ev, fields || {});
        AppController.saveEvent(ev);
        tc.events.push(ev.id);
        return ev.id;
    }
    function addTask(title, fields) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        Object.assign(d, fields || {});
        verify(AppController.saveTask(d));
        tc.tasks.push(d.id);
        return d.id;
    }
    function today(view) {
        return createTemporaryQmlObject('import TodoCpp; TodayView { anchors.fill: parent }', view || host);
    }

    // A1-6, A5-11: the way back names the key bound now, never the timer's T.
    function skip_test_back_to_today_names_the_real_key() {
        const day = tc.probeDay();
        AppController.selectedDate = day;
        const v = tc.today();
        const back = findChild(v, "today-back");
        verify(back.visible);
        verify(back.text.indexOf("(T)") < 0, back.text);
        const key = AppController.shortcutText("cal.today");
        verify(key.length > 0);
        verify(back.text.indexOf(key) > 0, back.text + " does not name " + key);
    }

    // A3-3, A6-3: a meeting across midnight says where it really ends and
    // how long it is, on both days.
    function skip_test_a_meeting_across_midnight_keeps_its_end() {
        const day = tc.probeDay();
        const next = new Date(day.getFullYear(), day.getMonth(), day.getDate() + 1);
        tc.addEvent("night release probe", day, 23, 1.5, { endDate: next });
        AppController.selectedDate = day;
        const v = tc.today();
        const it = v._items().filter(x => x.block && x.block.title === "night release probe")[0];
        verify(it, "the row is there");
        const sub = v._sub("meeting", it.block);
        verify(sub.indexOf(Theme.fmtHour(1.5)) >= 0, sub);
        verify(sub.indexOf(Theme.fmtHour(0)) < 0, "no 'until 00:00': " + sub);
        compare(v._rowLen(it.block), I18n.fmtMinutes(150));
        AppController.selectedDate = next;
        const w = tc.today();
        const it2 = w._items().filter(x => x.block && x.block.title === "night release probe")[0];
        verify(it2.block.fromPrevDay);
        verify(w._sub("meeting", it2.block).indexOf(Theme.fmtHour(1.5)) >= 0);
        compare(w._rowLen(it2.block), I18n.fmtMinutes(150));
    }

    // A2-23, A5-18: deadlines are told against the real today.
    function skip_test_deadline_words_are_about_the_real_today() {
        const now = AppController.today;
        const tomorrow = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1, 17, 0);
        tc.addTask("due tomorrow probe", { dueAt: tomorrow, dueHasTime: true });
        AppController.selectedDate = new Date(tomorrow.getFullYear(), tomorrow.getMonth(), tomorrow.getDate());
        const v = tc.today();
        const facts = findChild(v, "today-facts").text;
        const n = v.dayData.facts.dueToday;
        verify(n >= 1);
        verify(facts.indexOf(I18n.count(n, "today.n.dueTomorrow")) >= 0, "browsing tomorrow says due tomorrow: " + facts);
        verify(facts.indexOf(I18n.count(n, "today.n.due")) < 0 && facts.indexOf(I18n.count(n, "today.n.dueShort")) < 0, facts);
        compare(v._dueWord(tomorrow), I18n.t("quick.day.tomorrow"));
        const past = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 2);
        verify(v._dueWord(past) !== I18n.t("quick.day.tomorrow"));
        compare(v._dueWord(past), I18n.t("today.dueWasOn").arg(I18n.fmtDate(past, "dayMonth")));
    }

    // A2-33, A3-20: focus time is not counted as a meeting and is named as
    // its editor names it; a meeting's type is the editor's word.
    function skip_test_focus_is_not_a_meeting_and_types_match_the_editor() {
        const day = tc.probeDay();
        tc.addEvent("focus probe", day, 10, 11, { type: "focus" });
        tc.addEvent("sync probe", day, 10, 11, { type: "sync" });
        AppController.selectedDate = day;
        const v = tc.today();
        const facts = findChild(v, "today-facts").text;
        verify(facts.indexOf(I18n.count(1, "today.n.meetings")) >= 0, facts);
        const blocks = v.dayData.blocks;
        const focus = blocks.filter(b => b.title === "focus probe")[0];
        const sync = blocks.filter(b => b.title === "sync probe")[0];
        verify(v._sub("meeting", focus).indexOf(I18n.t("event.type.focus")) >= 0);
        verify(v._sub("meeting", sync).indexOf(I18n.t("event.type.sync")) >= 0);
    }

    // A6-4: a meeting kept in another zone says its hour there.
    function test_a_zoned_meeting_says_its_own_hour() {
        const v = tc.today();
        const sub = v._sub("meeting", { eventType: "none", srcTz: "America/New_York", srcStart: 19, start: 2, end: 2.25 });
        verify(sub.indexOf(Theme.fmtHour(19) + " New York") >= 0, sub);
    }

    // A6-4 (verify): Today opens a single meeting as the week does, from
    // its occurrence, which alone carries the zone it is kept in.
    function skip_test_today_opens_a_single_meeting_as_the_week_does() {
        const day = tc.probeDay();
        const id = tc.addEvent("single meeting probe", day, 15, 16);
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        const win = comp.createObject(null);
        try {
            const o = win._occurrenceOf(id, Qt.formatDate(day, "yyyy-MM-dd"));
            verify(o, "the occurrence of a meeting that does not repeat");
            compare(o.id, id);
            verify(o.srcTz !== undefined, "the zone fields come with it");
            win.openEvent(id, Qt.formatDate(day, "yyyy-MM-dd"));
            const ed = findChild(win.contentItem, "event-zone");
            verify(ed !== null);
            compare(ed.text, I18n.t("event.zone.local"), "a local meeting says it is on local time (A6-7)");
        } finally {
            win.destroy();
        }
    }

    // A3-20 (verify): a meeting titled as its type does not say it twice.
    function skip_test_a_title_that_is_its_type_is_not_repeated() {
        const day = tc.probeDay();
        const word = I18n.t("event.type.sync");
        tc.addEvent(word, day, 10, 11, { type: "sync" });
        AppController.selectedDate = day;
        const v = tc.today();
        const b = v.dayData.blocks.filter(x => x.title === word)[0];
        verify(b);
        const sub = v._sub("meeting", b);
        verify(sub.toLowerCase().indexOf(word.toLowerCase()) < 0, sub);
        // F3-EYES-1: nor its storage id, nor a task's "planned by you".
        verify(sub.indexOf(b.id) < 0, sub);
        verify(sub.indexOf(I18n.t("today.plannedByYou")) < 0, sub);
        // The same for a meeting with no type titled "Meeting", plain or not.
        const kind = I18n.t("event.kind.meeting");
        tc.addEvent(kind, day, 12, 13);
        const v2 = tc.today();
        const m = v2.dayData.blocks.filter(x => x.title === kind)[0];
        verify(m);
        const was = Style.chipFill ? "bold" : "quiet";
        try {
            for (const look of ["bold", "quiet"]) {
                Style.apply(look);
                const s2 = v2._sub("meeting", m);
                verify(s2.indexOf(m.id) < 0, look + ": " + s2);
                verify(s2.indexOf(I18n.t("today.plannedByYou")) < 0, look + ": " + s2);
            }
        } finally {
            Style.apply(was);
        }
        // A task still says its id.
        verify(v2._sub("task", { id: "APP-1", start: 9, end: 10 }).indexOf("APP-1") >= 0);
    }

    // A2-10 (verify): "N more above" has a band of its own, never over the
    // first visible row's title.
    function skip_test_more_above_does_not_cover_a_row() {
        const day = tc.probeDay();
        for (let i = 0; i < 30; i++) tc.addTask("above probe " + i, { scheduledAt: day });
        AppController.selectedDate = day;
        const v = tc.today();
        const list = findChild(v, "today-day");
        tryVerify(() => list.contentHeight > list.height, 2000, "the day scrolls");
        list.contentY = list.originY + 6 * Theme.px(40);
        const cue = findChild(v, "today-more-above");
        tryVerify(() => v.hiddenAbove > 0 && cue.visible, 1000);
        wait(50);
        const cueBottom = cue.mapToItem(v, 0, cue.height).y;
        const listTop = list.mapToItem(v, 0, 0).y;
        verify(cueBottom <= listTop + 0.5, "cue " + cueBottom + " over the rows from " + listTop);
    }

    // A6-17: a multi-day all-day block says which days and which of them.
    function skip_test_a_multi_day_block_says_its_span() {
        const day = tc.probeDay();
        const end = new Date(day.getFullYear(), day.getMonth(), day.getDate() + 3);
        tc.addEvent("conference probe", day, 0, 24, { allDay: true, endDate: end });
        AppController.selectedDate = new Date(day.getFullYear(), day.getMonth(), day.getDate() + 1);
        const v = tc.today();
        const b = v.dayData.allDay.filter(x => x.title === "conference probe")[0];
        verify(b);
        verify(v._sub("allday", b).indexOf(I18n.t("today.dayOfSpan").arg(2).arg(4)) >= 0, v._sub("allday", b));
    }

    // A2-26, A6-14, A3-22: the time gutter holds "без времени" whole.
    function skip_test_the_time_gutter_fits_its_words() {
        const v = tc.today();
        const tm = Qt.createQmlObject('import QtQuick; TextMetrics {}', v);
        tm.font.family = Theme.fontUi;
        tm.font.pixelSize = Theme.fsXs;
        tm.text = I18n.t("today.noTime");
        verify(v.timeLabelW >= tm.advanceWidth, v.timeLabelW + " < " + tm.advanceWidth);
    }

    // A2-24: a plan emptied by Done still says what was done.
    function skip_test_a_day_done_keeps_its_count() {
        const now = AppController.today;
        const id = tc.addTask("done today probe", { scheduledAt: new Date(now.getFullYear(), now.getMonth(), now.getDate()) });
        AppController.toggleDone([id]);
        AppController.selectedDate = now;
        const v = tc.today();
        const facts = findChild(v, "today-facts").text;
        verify(facts !== I18n.t("today.nothing") || (AppController.endOfDaySummary().closed || []).length === 0, facts);
    }

    // A1-2: the undated link is reached by the cursor, past the last row.
    function skip_test_the_cursor_reaches_the_undated_link() {
        tc.addTask("undated probe", {});
        const v = tc.today();
        const items = v._items();
        compare(items[items.length - 1].kind, "undated");
        let asked = 0;
        v.undatedRequested.connect(() => asked++);
        v.moveCursor(0, 0);
        for (let i = 0; i < items.length + 2; i++) v.moveCursor(0, 1);
        compare(v.cursorKey, "undated");
        v.openCursor();
        compare(asked, 1);
    }

    // A1-10, A2-30, A3-4, A2-31: end of day fits the window, scrolls, and
    // the rows have keys; a carried row says what was done.
    function skip_test_end_of_day_fits_and_has_row_keys() {
        const now = AppController.today;
        const ids = [];
        for (let i = 0; i < 12; i++)
            ids.push(tc.addTask("eod probe " + i, { scheduledAt: new Date(now.getFullYear(), now.getMonth(), now.getDate()) }));
        const d = createTemporaryQmlObject('import TodoCpp; EndOfDayDialog {}', host);
        d.showNow();
        tryCompare(d, "opened", true);
        wait(50);
        verify(d.height <= d.parent.height, "taller than the window: " + d.height + " > " + d.parent.height);
        const flick = findChild(d, "end-of-day-scroll");
        verify(flick.contentHeight > flick.height, "the list scrolls");
        verify(findChild(d, "end-of-day-keys").visible);
        const first = d.carryRows.findIndex(r => ids.indexOf(r.id) >= 0);
        verify(first >= 0);
        d.cursor = first;
        const id = d.carryRows[first].id;
        d.contentItem.forceActiveFocus();
        keyClick(Qt.Key_T);
        verify(d.carried[id] === "tomorrow", "t carried the row under the cursor");
        const t = AppController.taskById(id);
        const tomorrow = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1);
        compare(new Date(t.scheduledAt).getDate(), tomorrow.getDate());
        // j walks the rows and keeps the current one on the screen.
        d.cursor = 0;
        for (let i = 0; i < d.carryRows.length; i++) keyClick(Qt.Key_J);
        compare(d.cursor, d.carryRows.length - 1);
        verify(flick.contentY > 0, "scrolled to the last row");
        // Enter on a pill Tab reached is that pill, not the cursor row's
        // task opened (B3 verify).
        let opened = 0;
        d.taskActivated.connect(() => opened++);
        d.cursor = 0;
        const other = d.carryRows.findIndex((r, i) => i > 0 && ids.indexOf(r.id) >= 0 && d.carried[r.id] === undefined);
        verify(other > 0);
        const otherId = d.carryRows[other].id;
        const pills = [];
        const walk = (it) => {
            if (!it) return;
            if (it.objectName === "end-of-day-carry-tomorrow") pills.push(it);
            for (let i = 0; i < (it.children || []).length; i++) walk(it.children[i]);
        };
        walk(d.contentItem);
        const pill = pills.filter(p => p.parent && p.parent.objectName === "end-of-day-carry-" + otherId)[0];
        verify(pill, pills.length + " pills");
        pill.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(opened, 0, "no task opened");
        verify(d.opened, "the dialog stays");
        compare(d.carried[otherId], "tomorrow");
        d.close();
    }

    // A2-29: the recap opened on the first day of a week, or on a week with
    // nothing closed yet, shows the week that ended.
    function skip_test_recap_opens_on_the_week_that_ended() {
        const r = createTemporaryQmlObject('import TodoCpp; WeeklyRecapDialog {}', host);
        const far = tc.probeDay();   // nothing closed that week
        const d = r.startDay(far, false);
        compare(Math.round((far - d) / 86400000), 7);
        verify(findChild(r, "weekly-recap-prev") !== null);
        verify(findChild(r, "recap-keys") !== null);
    }

    // A1-7, A2-12, A6-10, EYES-1, A6-9: four overlapping things in a narrow
    // day do not cascade into slivers; what does not fit is "+N".
    function skip_test_week_folds_lanes_that_do_not_fit() {
        const day = tc.probeDay();
        tc.addEvent("Standup", day, 9, 9.5);
        tc.addEvent("Design review", day, 9, 10);
        tc.addEvent("1:1 Ana", day, 9.25, 9.75);
        tc.addEvent("Ops sync", day, 9.5, 11);
        AppController.selectedDate = day;
        const holder = createTemporaryQmlObject('import QtQuick; Item { width: 900; height: 600 }', host);
        const wv = createTemporaryQmlObject('import TodoCpp; WeekView { anchors.fill: parent }', holder);
        wait(50);
        verify(wv.laneFit < 4, "the probe day is narrow: fit " + wv.laneFit);
        const folds = wv.foldChips.filter(f => wv.isSameDay(wv.days[f.dayIndex].date, day));
        compare(folds.length, 1, JSON.stringify(wv.foldChips));
        const lanes = 4 - folds[0].n;
        compare(lanes, Math.min(wv.laneFit, 3));
        const di = folds[0].dayIndex;
        verify(findChild(wv, "week-fold-" + di) !== null, "+N chip drawn");
    }

    // F2-REG-2/3/4, F3-EYES-8: at 100-150% two lanes stand side by side,
    // none narrower than six letters and an ellipsis; the tray gives way
    // to them; a task title is never cut through a word.
    TextMetrics { id: sixLetters; text: "oooooo…" }
    FontMetrics { id: titleFm }
    function skip_test_week_lanes_keep_six_letters() {
        const saved = AppController.appSettingsJson;
        const day = tc.probeDay();
        tc.addEvent("Standup", day, 9, 9.5);
        tc.addEvent("Design review", day, 9, 10);
        tc.addEvent("1:1 Ana", day, 9.25, 9.75);
        tc.addEvent("Ops sync", day, 9.5, 11);
        tc.addEvent("Синхронизация", day, 14, 15);
        tc.addTask("Обновить pydantic до v2 и прогнать", { scheduledAt: new Date(day.getFullYear(), day.getMonth(), day.getDate(), 14, 0), hasTime: true, estimateMinutes: 60 });
        tc.addTask("tray probe undated");
        AppController.selectedDate = day;
        try {
            // The calendar's width in a 1280-1312px window: the sidebar
            // folds to icons at 150%.
            // The third value: the tray must stay. Those are the calendar's
            // widths in 1280x720 @100/110/125% and 1440x900 @125/150% windows
            // (the sidebar folds to icons from 125%). 1366x768 @150% (1300) is
            // checked by the rules only: whether 90px are left there depends on
            // the platform's font, so the tray may give way on Linux and macOS.
            for (const c of [[1, 1050], [1.1, 1083], [1.25, 1083], [1.5, 1200],
                             [1, 1072, true], [1.1, 1051, true], [1.25, 1218, true],
                             [1.25, 1180, true], [1.5, 1374, true], [1.5, 1300]]) {
                const o = JSON.parse(saved || "{}");
                o.appearance = Object.assign({}, o.appearance || {}, { uiScale: c[0] });
                AppController.appSettingsJson = JSON.stringify(o);
                const holder = createTemporaryQmlObject('import QtQuick; Item { width: ' + c[1] + '; height: 700 }', host);
                const cv = createTemporaryQmlObject('import TodoCpp; CalendarView { anchors.fill: parent; zoom: "week" }', holder);
                tryVerify(() => cv.calendarView && cv.calendarView.days.length === 7, 2000);
                const wv = cv.calendarView;
                wait(50);
                const tag = "scale " + c[0] + " width " + c[1] + ": ";
                sixLetters.font.family = Theme.fontUi;
                sixLetters.font.pixelSize = Theme.fsBlock;
                sixLetters.font.weight = Theme.fwHeading;
                verify(wv.minLaneW >= Math.ceil(sixLetters.advanceWidth), tag + wv.minLaneW);
                verify(wv.laneFit >= 2, tag + "two lanes side by side, fit " + wv.laneFit + " avail " + wv.laneAvail);
                verify(wv.laneAvail / wv.laneFit >= wv.minLaneW - 1e-9, tag + "no lane under the minimum");
                if (cv.trayW >= Theme.px(180)) verify(cv.trayW <= Theme.px(260), tag + cv.trayW);
                // The tray narrows rather than leave: its F6 stop and S stay
                // at these ordinary sizes (the first cut hid it under 180px).
                const trayItem = findChild(cv, "undated-tray");
                verify(trayItem, tag + "tray");
                compare(trayItem.visible, cv.trayW >= Theme.px(90), tag + "tray shown while 90px are left, " + cv.trayW);
                if (c[2]) verify(trayItem.visible, tag + "tray stays, " + cv.trayW);
                compare(trayItem.compact, cv.trayW < Theme.px(180), tag + "compact under 180px");
                // Every title drawn shows at least six letters' room.
                const titles = [];
                (function walk(it) {
                    if (!it) return;
                    if (it.objectName === "week-event-title" || it.objectName === "week-taskblock-title") titles.push(it);
                    for (const k of it.children || []) walk(k);
                })(wv);
                verify(titles.length >= 3, tag + titles.length);
                for (const t of titles) {
                    let b = t;
                    while (b && b._lane === undefined) b = b.parent;
                    if (!b || !b.visible) continue;
                    verify(t.width >= Math.ceil(sixLetters.advanceWidth) - 1, tag + t.text + " gets " + t.width);
                    // A word wider than the title is never wrapped through.
                    titleFm.font = t.font;
                    if (!wv.wordsFit(t.text, titleFm, t.width)) {
                        compare(t.wrapMode, Text.NoWrap, tag + t.text);
                        compare(t.elide, Text.ElideRight, tag + t.text);
                    }
                }
                holder.destroy();
                wait(20);
            }
        } finally {
            AppController.appSettingsJson = saved;
        }
        // The word check itself.
        titleFm.font.pixelSize = 12;
        verify(wv_wordsFit("a b", 200));
        verify(!wv_wordsFit("Обновить", 10));
    }
    function wv_wordsFit(text, w) {
        const wv = createTemporaryQmlObject('import TodoCpp; WeekView { width: 400; height: 300 }', host);
        const r = wv.wordsFit(text, titleFm, w);
        wv.destroy();
        return r;
    }

    // A2-13, A6-8: what is above the hours on screen has a chip that says so.
    function skip_test_week_says_what_is_above_the_grid() {
        const day = tc.probeDay();
        tc.addEvent("early sync probe", day, 7, 8);
        AppController.selectedDate = day;
        const holder = createTemporaryQmlObject('import QtQuick; Item { width: 1200; height: 600 }', host);
        const wv = createTemporaryQmlObject('import TodoCpp; WeekView { anchors.fill: parent }', holder);
        const flick = findChild(wv, "week-hour-scroll").contentItem;
        // Opened on the working day, or on the hour before now past
        // midnight: told by a pure rule, never by the clock of the run.
        compare(wv.workdayFirstHour(0), 0);
        compare(wv.workdayFirstHour(14), Math.max(0, wv.workStart - wv.hoursStart));
        tryVerify(() => wv.scrollToWorkday(), 2000, "the grid is laid out");
        wait(100);   // the opening scroll has stopped trying
        flick.contentY = 9 * wv.hourH;
        const chip = findChild(wv, "week-more-earlier");
        tryVerify(() => chip.visible, 1000);
        verify(chip.items.some(x => x.title === "early sync probe"));
    }

    // A7-14: the empty day zoom says "this day", not "this week".
    function skip_test_empty_day_zoom_says_day() {
        const saved = AppController.appSettingsJson;
        const lens = AppController.currentView;
        try {
            const wv = createTemporaryQmlObject('import TodoCpp; WeekView { anchors.fill: parent; zoom: "day" }', host);
            AppController.selectedDate = tc.probeDay();
            const e = findChild(wv, "week-empty");
            if (e.visible) compare(e.title, I18n.t("day.empty.title"));
            wv.destroy();
        } finally {
            AppController.appSettingsJson = saved;
            AppController.currentView = lens;
        }
    }
}
