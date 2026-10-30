// The Monday recap (WEAK PECAP, qml/WeeklyRecapDialog.qml): when it opens by
// itself, that it opens once a week, and what it lists. The grouping itself is
// AppController::weeklyRecapFor's and is tested in C++.
//
// APP-211: a week counts as seen only once the dialog was closed, and it
// opens by itself only where it can be seen. The rule is a pure function
// given `now`, so the scenarios below run without a clock.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "WeeklyRecapDialog"
    when: windowShown
    visible: true
    width: 800
    height: 700

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""

    readonly property var sample: ({
        weekStart: "2026-09-28", weekEnd: "2026-10-05",
        groups: [
            { from: "backlog", fromName: "Backlog", fromColor: "#8a8e98",
              to: "prog", toName: "In Progress", toColor: "#5aa9e6",
              tasks: [ { id: "APP-1", title: "one", priority: "P2" },
                       { id: "APP-2", title: "two", priority: "P1" } ] },
            { from: "prog", fromName: "In Progress", fromColor: "#5aa9e6",
              to: "done", toName: "Done", toColor: "#6ec18a",
              tasks: [ { id: "APP-3", title: "three", priority: "P2" } ] }
        ]
    })

    function init() {
        tc.savedSettings = AppController.appSettingsJson;
        AppController.appSettingsJson = "";
    }
    function cleanup() {
        AppController.appSettingsJson = tc.savedSettings;
    }

    function make() {
        const d = createTemporaryQmlObject('import TodoCpp; WeeklyRecapDialog {}', host);
        verify(d !== null);
        return d;
    }

    function test_week_key_is_the_monday() {
        const d = make();
        compare(d.weekKey(new Date(2026, 9, 5)), "2026-10-05");   // Monday
        compare(d.weekKey(new Date(2026, 9, 11)), "2026-10-05");  // Sunday
        compare(d.weekKey(new Date(2026, 9, 12)), "2026-10-12");
    }

    function test_due_once_a_week_and_only_with_moves() {
        const d = make();
        const monday = new Date(2026, 9, 5);
        verify(d.isDue(monday, tc.sample));
        verify(!d.isDue(monday, { groups: [] }), "an empty week opened the recap");

        d._markSeen(monday);
        verify(!d.isDue(new Date(2026, 9, 7), tc.sample), "shown twice in one week");
        verify(d.isDue(new Date(2026, 9, 12), tc.sample), "not shown the next week");
    }

    // The decision, for every way the window can be when the week turns.
    function test_shows_only_when_it_can_be_seen_data() {
        return [
            { tag: "on screen, in front, nothing over it", visible: true, active: true, overlay: false, content: true, want: true },
            { tag: "in the tray or minimised", visible: false, active: false, overlay: false, content: true, want: false },
            { tag: "behind another app", visible: true, active: false, overlay: false, content: true, want: false },
            { tag: "under the task editor", visible: true, active: true, overlay: true, content: true, want: false },
            { tag: "nothing moved last week", visible: true, active: true, overlay: false, content: false, want: false }
        ];
    }
    function test_shows_only_when_it_can_be_seen(row) {
        const d = make();
        compare(d.shouldShowRecap(new Date(2026, 9, 5, 0, 1), "2026-09-28", row.visible, row.active, row.overlay, row.content),
                row.want);
    }

    function test_a_seen_week_is_not_shown_again() {
        const d = make();
        // Seen on Monday: not again that week, wherever the window is...
        verify(!d.shouldShowRecap(new Date(2026, 9, 7, 9, 0), "2026-10-05", true, true, false, true));
        verify(!d.isUnseen(new Date(2026, 9, 11, 23, 59), "2026-10-05", true));
        // ...and the next week is new.
        verify(d.shouldShowRecap(new Date(2026, 9, 12, 0, 0), "2026-10-05", true, true, false, true));
    }

    // (a) open over Sunday → Monday midnight with the window in the tray:
    // nothing is marked; the window coming back shows it; closing marks it.
    function test_week_turns_in_the_tray() {
        const d = make();
        const monday = new Date(2026, 9, 5, 0, 0, 30);
        verify(!d.shouldShowRecap(monday, d.seenWeek, false, false, false, true));
        compare(d.seenWeek, "", "the week was marked seen with the window hidden");
        // Restored from the tray an hour later.
        verify(d.shouldShowRecap(new Date(2026, 9, 5, 1, 0), d.seenWeek, true, true, false, true));
        d.recap = tc.sample;
        d.open();
        tryVerify(() => d.opened);
        compare(d.seenWeek, "", "opening alone marked the week seen");
        d.close();
        tryVerify(() => !d.visible);
        compare(d.seenWeek, "2026-10-05");
        verify(!d.shouldShowRecap(new Date(2026, 9, 5, 1, 1), d.seenWeek, true, true, false, true));
    }

    // (b) the task editor is open when the week turns: shown once it closes.
    function test_week_turns_under_an_overlay() {
        const d = make();
        const monday = new Date(2026, 9, 5, 0, 0);
        verify(!d.shouldShowRecap(monday, d.seenWeek, true, true, true, true));
        verify(d.shouldShowRecap(new Date(2026, 9, 5, 0, 5), d.seenWeek, true, true, false, true));
    }

    // (c) asleep from Friday to Tuesday: the resume is the first chance.
    function test_resume_after_a_long_sleep() {
        const d = make();
        AppController.appSettingsJson = JSON.stringify({ notifications: { recapSeenWeek: "2026-09-28" } });
        compare(d.seenWeek, "2026-09-28");
        verify(d.shouldShowRecap(new Date(2026, 9, 6, 8, 30), d.seenWeek, true, true, false, true));
    }

    // (d) a restart after the recap was seen: the setting survives, so the
    // recap does not come back.
    function test_restart_after_seeing_it() {
        AppController.appSettingsJson = JSON.stringify({ notifications: { recapSeenWeek: "2026-10-05" } });
        const d = make();
        verify(!d.shouldShowRecap(new Date(2026, 9, 6, 9, 0), d.seenWeek, true, true, false, true));
    }

    // The live entry point stands down while the window cannot be seen,
    // and marks nothing.
    function test_show_if_due_waits_for_the_window() {
        const d = make();
        verify(!d.showIfDue(false, false, false));
        verify(!d.showIfDue(true, false, false));
        verify(!d.showIfDue(true, true, true));
        verify(!d.opened);
        compare(d.seenWeek, "");
    }

    // The button's way in: any week, empty or not, and closing it marks the
    // week seen, which takes the dot away.
    function test_show_now_opens_an_empty_week_and_closing_clears_the_dot() {
        const d = make();
        d.showNow();
        tryVerify(() => d.opened);
        d.recap = { weekStart: "2026-09-28", weekEnd: "2026-10-05", groups: [] };
        compare(d.taskCount, 0);
        d.close();
        tryVerify(() => !d.visible);
        compare(d.seenWeek, "2026-10-05");
    }

    function test_no_dot_once_seen_or_when_switched_off() {
        const d = make();
        AppController.appSettingsJson = JSON.stringify({ notifications: { recapSeenWeek: d.weekKey(new Date()) } });
        verify(!d.unseen, "a dot for a week already seen");
        AppController.appSettingsJson = JSON.stringify({ notifications: { weeklyRecap: false } });
        verify(!d.unseen, "a dot with the recap switched off");
        AppController.appSettingsJson = "";
        compare(d.unseen, d.hasContent);
    }

    function test_switched_off_is_never_due() {
        AppController.appSettingsJson = JSON.stringify({ notifications: { weeklyRecap: false } });
        const d = make();
        verify(!d.isDue(new Date(2026, 9, 5), tc.sample));
    }

    // DG-123: the facts of a week — the closed list, a bar per day, the
    // first three titles and "and N more".
    readonly property var facts: ({
        weekStart: new Date(2026, 9, 5), weekEnd: new Date(2026, 9, 11),
        perDay: [2, 1, 0, 3, 2, 0, 0],
        closed: [ { id: "APP-1", title: "one" }, { id: "APP-2", title: "two" }, { id: "APP-3", title: "three" },
                  { id: "APP-4", title: "four" }, { id: "APP-5", title: "five" } ]
    })
    function test_it_shows_the_week_facts() {
        const d = make();
        d.open();
        tryVerify(() => d.opened);
        d.facts = tc.facts;
        d.meetings = [];
        d.meetingMinutes = 660;
        compare(findChild(d.contentItem, "recap-facts").text,
                I18n.count(5, "recap.closedN") + " · " + I18n.t("recap.meetings").arg(I18n.fmtMinutes(660)));
        compare(d.dayRows.length, 5, "Monday to Friday, no empty weekend");
        verify(findChild(d.contentItem, "recap-task-APP-3") !== null);
        compare(findChild(d.contentItem, "recap-task-APP-4"), null, "only the first three");
        compare(findChild(d.contentItem, "recap-more").text, I18n.t("recap.more").arg(2));
        verify(d.markdown().indexOf("- APP-5 five") >= 0);
        if (I18n.lang === "ru") compare(d.rangeText(), "5 – 11 октября");
        d.close();
    }

    function test_a_task_opens_and_the_recap_closes() {
        const d = make();
        d.open();
        tryVerify(() => d.opened);
        d.facts = tc.facts;
        let got = "";
        d.taskActivated.connect((id) => { got = id; });
        findChild(d.contentItem, "recap-task-area-APP-3").activated();
        compare(got, "APP-3");
        tryVerify(() => !d.visible);
    }

    function test_brackets_step_through_weeks() {
        const d = make();
        d.open();
        tryVerify(() => d.opened);
        const a = d.facts.weekStart;
        d.stepWeek(-1);
        compare(Math.round((a.getTime() - d.facts.weekStart.getTime()) / 86400000), 7);
        d.close();
    }
    // Dogfood 2026-10-16: "Next week →" pages forward like "← Week before"
    // (it closed the dialog for the calendar), and stops at this week.
    function test_next_week_pages_and_stops_at_this_week() {
        const d = make();
        d.open();
        tryVerify(() => d.opened);
        d._load(new Date());
        const next = findChild(d.contentItem, "weekly-recap-next");
        verify(!next.enabled, "no week after this one");
        const a = d.facts.weekStart;
        d.stepWeek(1);
        compare(d.facts.weekStart.getTime(), a.getTime(), "] past this week does nothing");
        d.stepWeek(-2);
        verify(next.enabled);
        next.clicked();
        verify(d.opened, "the summary stays open");
        compare(Math.round((a.getTime() - d.facts.weekStart.getTime()) / 86400000), 7);
        d.close();
    }
    // New and put-off tasks are counted and listed beside the closed ones.
    function test_new_and_put_off_are_counted() {
        const d = make();
        d.open();
        tryVerify(() => d.opened);
        d.facts = { weekStart: new Date(2026, 9, 5), weekEnd: new Date(2026, 9, 11), perDay: [0, 0, 0, 0, 0, 0, 0], closed: [],
                    created: [ { id: "APP-7", title: "seven" }, { id: "APP-8", title: "eight" } ],
                    moved: [ { id: "APP-9", title: "nine" } ] };
        d.meetings = [];
        d.meetingMinutes = 0;
        compare(findChild(d.contentItem, "recap-facts").text,
                [I18n.t("recap.noneClosed"), I18n.count(2, "recap.createdN"), I18n.count(1, "recap.movedN")].join(" · "));
        verify(findChild(d.contentItem, "recap-created-APP-7") !== null);
        verify(findChild(d.contentItem, "recap-moved-APP-9") !== null);
        verify(!findChild(d.contentItem, "recap-section-closed").visible);
        verify(d.markdown().indexOf("- APP-9 nine") >= 0);
        d.close();
    }
    // F3-EYES-11: a week with nothing closed says so in one line, with no
    // column of "0 closed" days.
    function test_an_empty_week_says_so_once() {
        const d = make();
        d.open();
        tryVerify(() => d.opened);
        d.facts = { weekStart: new Date(2026, 9, 5), weekEnd: new Date(2026, 9, 11), perDay: [0, 0, 0, 0, 0, 0, 0], closed: [] };
        d.meetings = [];
        d.meetingMinutes = 0;
        compare(findChild(d.contentItem, "recap-facts").text, I18n.t("recap.noneClosed"));
        verify(!findChild(d.contentItem, "recap-days").visible, "no per-day rows");
        d.facts = tc.facts;
        verify(findChild(d.contentItem, "recap-days").visible, "a week with closed tasks keeps them");
        d.close();
    }

    // Dogfood 2026-10-30: in progress now, each ticket's chain of columns
    // (blocked in red), the week's meetings by day; first few and "ещё N ·
    // все"; all of it in the facts line and the Markdown.
    function test_in_progress_moves_and_meetings() {
        const d = make();
        d.open();
        tryVerify(() => d.opened);
        d._load(new Date());
        const ws = d.facts.weekStart, we = d.facts.weekEnd;
        const st = (id, name, category) => ({ id: id, name: name, category: category });
        const chain = (id, statuses, a, b) => ({ id: id, key: "TEL-" + id, title: "t" + id, statuses: statuses,
                                                 first: new Date(ws.getFullYear(), ws.getMonth(), ws.getDate() + a, 10),
                                                 last: new Date(ws.getFullYear(), ws.getMonth(), ws.getDate() + b, 10) });
        const prog = st("prog", "In Progress", "prog"), review = st("review", "Code review", "review"),
              done = st("done", "Done", "done"), blocked = st("blocked", "Blocked", "blocked");
        d.facts = { weekStart: ws, weekEnd: we, perDay: [0, 0, 0, 0, 0, 0, 0], closed: [],
                    inWork: [ { id: "1", key: "TEL-1", title: "migration", statusName: "In Progress", category: "prog",
                                since: new Date(), timing: true, weekSeconds: 5 * 3600 + 20 * 60 } ],
                    chains: [ chain("1", [prog, review, done], 0, 3), chain("2", [prog, blocked], 4, 4),
                              chain("3", [prog, done], 2, 2), chain("4", [prog, done], 2, 2), chain("5", [prog, done], 2, 2) ] };
        const mon = new Date(ws.getFullYear(), ws.getMonth(), ws.getDate());
        const ms = [];
        for (let i = 0; i < 7; i++) ms.push({ id: "m" + i, title: "meet " + i, date: mon, start: 9 + i, minutes: 30 });
        d.meetings = ms;
        d.meetingMinutes = 210;
        compare(findChild(d.contentItem, "recap-facts").text,
                [I18n.t("recap.noneClosed"), I18n.count(5, "recap.chainsN"), I18n.t("recap.meetingsN").arg("7 · " + I18n.fmtMinutes(210))].join(" · "));
        verify(findChild(d.contentItem, "recap-section-inWork").visible);
        verify(findChild(d.contentItem, "recap-inWork-1") !== null);
        verify(d.inWorkMeta(d.inWorkRows[0]).indexOf(I18n.fmtMinutes(320)) > 0, d.inWorkMeta(d.inWorkRows[0]));
        // The chain: every stop, the days it ran over; blocked is red.
        compare(d.chainText(d.chainRows[0]), "In Progress → Code review → Done");
        compare(d.dayRange(d.chainRows[0].first, d.chainRows[0].last), I18n.dayName(1) + "–" + I18n.dayName(4));
        verify(d.chainStyled(d.chainRows[1]).indexOf(String(Theme.danger)) >= 0);
        verify(findChild(d.contentItem, "recap-chain-4") !== null);
        compare(findChild(d.contentItem, "recap-chain-5"), null, "only the first four");
        const all = findChild(d.contentItem, "recap-all-chains");
        verify(all.visible);
        compare(all.text, I18n.t("recap.moreAll").arg(1));
        d.showAll("chains");
        verify(findChild(d.contentItem, "recap-chain-5") !== null);
        verify(!all.visible);
        // Meetings by day: the day's name on its first one only, five shown.
        compare(d.meetingRows.length, 5);
        compare(d.meetingRows[0].day, I18n.dayName(1));
        compare(d.meetingRows[1].day, "");
        verify(findChild(d.contentItem, "recap-all-meetings").visible);
        // The Markdown has the sections in the dialog's order.
        const md = d.markdown();
        const a = md.indexOf(I18n.t("recap.inWork")), b = md.indexOf(I18n.t("recap.chains")), c = md.indexOf(I18n.t("recap.meetingsTitle"));
        verify(a > 0 && b > a && c > b, md);
        verify(md.indexOf("TEL-1 t1: In Progress → Code review → Done") > 0, md);
        verify(md.indexOf("meet 6") > 0, "every meeting, not only the five shown");
        // A past week has no "now".
        d.facts = Object.assign({}, d.facts, { weekStart: new Date(2026, 9, 5), weekEnd: new Date(2026, 9, 11) });
        compare(d.inWorkRows.length, 0);
        d.close();
    }

    // F3-EYES-4: every button stays inside the dialog, in both languages.
    function test_buttons_stay_inside() {
        const lang = AppController.language;
        try {
            for (const l of ["ru", "en"]) {
                AppController.language = l;
                const d = make();
                d.open();
                tryVerify(() => d.opened);
                for (const n of ["weekly-recap-copy", "weekly-recap-prev", "weekly-recap-next"]) {
                    const b = findChild(d.contentItem, n);
                    const p = b.mapToItem(d.contentItem, 0, 0);
                    verify(p.x + b.width <= d.width - Theme.inset + 0.5, l + ": " + n + " ends at " + (p.x + b.width) + " of " + d.width);
                }
                d.close();
                d.destroy();
            }
        } finally {
            AppController.language = lang;
        }
    }
}
