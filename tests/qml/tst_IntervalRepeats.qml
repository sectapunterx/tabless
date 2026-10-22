// Interval repeats (B7) and the repeats ahead in Week and Month (A2-25).
//
// The rules themselves, the parser and the next-occurrence walk are covered
// in C++ (test_recur.cpp, test_capture_parse.cpp, test_appcontroller.cpp);
// these cases are about the words a rule is shown in, the editors' "Custom
// interval…" row and the faint, read-only chips the calendars draw.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp
import "../../qml/TaskDates.js" as TaskDates

TestCase {
    id: tc
    name: "IntervalRepeats"
    when: windowShown
    visible: true
    width: 1100
    height: 760

    Item { id: host; anchors.fill: parent }

    property var made: []
    property string lang0: ""
    property var date0: null

    function initTestCase() {
        tc.lang0 = AppController.language;
        tc.date0 = AppController.selectedDate;
    }
    function cleanup() {
        for (let i = 0; i < tc.made.length; i++) AppController.deleteTask(tc.made[i]);
        tc.made = [];
        AppController.clearPendingUndo();
        AppController.language = tc.lang0;
        AppController.selectedDate = tc.date0;
    }

    function make(qml) {
        const o = createTemporaryQmlObject(qml, host);
        verify(o !== null);
        return o;
    }
    // A day far ahead: the shared QML test profile is never wiped, and no
    // task an earlier run left behind is anywhere near it.
    function probeDay(offset) {
        const d = new Date();
        d.setDate(d.getDate() + offset);
        d.setHours(0, 0, 0, 0);
        return d;
    }
    function mkRepeating(title, rule, day, hour) {
        const d = AppController.newTaskDraft("todo");
        d.title = title;
        d.recurrence = rule;
        d.scheduledAt = hour === undefined ? day : new Date(day.getFullYear(), day.getMonth(), day.getDate(), hour, 0);
        d.scheduledHasTime = hour !== undefined;
        AppController.saveTask(d);
        tc.made.push(d.id);
        return d.id;
    }
    function typeText(s) { for (let i = 0; i < s.length; i++) keyClick(s[i]); }

    // ── The words a rule is shown in ──

    function test_rule_words_ru_and_en() {
        AppController.language = "ru";
        compare(TaskDates.repeatText("every:2:day"), "раз в 2 дня");
        compare(TaskDates.repeatText("every:5:week"), "раз в 5 недель");
        compare(TaskDates.repeatText("every:2:month:15"), "раз в 2 месяца · 15-го");
        compare(TaskDates.repeatText("every:mon,wed"), "по пн, ср");
        compare(TaskDates.intervalTitle("every:3:week"), "Раз в 3 недели");
        // The old rules read as before.
        compare(TaskDates.repeatText("every:day"), "каждый день");
        compare(TaskDates.intervalText("every:day"), "");
        AppController.language = "en";
        compare(TaskDates.repeatText("every:2:day"), "every 2 days");
        compare(TaskDates.repeatText("every:3:week"), "every 3 weeks");
        compare(TaskDates.repeatText("every:mon,wed,fri"), "every mon, wed, fri");
        compare(TaskDates.intervalTitle("every:2:month"), "Every 2 months");
    }

    // ── The task document's "Custom interval…" row ──

    function test_document_custom_row_sets_an_interval() {
        AppController.language = "en";
        const id = mkRepeating("doc repeat probe", "", probeDay(700));
        const doc = make('import TodoCpp; TaskDocument { anchors.fill: parent }');
        doc.open(id);
        verify(doc._recurOptions.indexOf("every:2:week") >= 0, "every 2 weeks is a row of its own");
        compare(doc._recurOptions[doc._recurOptions.length - 1], "custom", "the custom row comes last");
        doc.startCustomRecur();
        const field = findChild(doc, "task-doc-recur-custom");
        verify(field !== null);
        tryVerify(() => field.visible && field.activeFocus, 1000, "the field did not take the keyboard");
        // Not a repeat: the field stays, marked, and nothing is saved.
        typeText("banana");
        keyClick(Qt.Key_Return);
        verify(doc._recurBad, "a word that is not a repeat is marked");
        verify(field.visible);
        compare(String(AppController.taskById(id).recurrence || ""), "");
        // A repeat: saved as the engine's rule, the field goes.
        field.text = "every 3 weeks";
        keyClick(Qt.Key_Return);
        tryCompare(field, "visible", false);
        compare(AppController.taskById(id).recurrence, "every:3:week");
        compare(findChild(doc, "task-doc-recurrence").value, "every 3 weeks");
        // Esc leaves the rule as it was.
        doc.startCustomRecur();
        tryVerify(() => field.activeFocus, 1000);
        typeText("mon, wed");
        keyClick(Qt.Key_Escape);
        verify(!field.visible);
        compare(AppController.taskById(id).recurrence, "every:3:week");
        doc.close();
    }

    // Esc on the custom row of a task with no rule takes back the "Repeat"
    // chip the row brought out: no empty chip is left behind.
    function test_document_custom_row_esc_leaves_no_empty_chip() {
        AppController.language = "en";
        const id = mkRepeating("doc repeat esc probe", "", probeDay(705));
        const doc = make('import TodoCpp; TaskDocument { anchors.fill: parent }');
        doc.open(id);
        const chip = findChild(doc, "task-doc-recurrence");
        verify(chip !== null);
        verify(!chip.visible, "no rule, no chip");
        doc.startCustomRecur();
        const field = findChild(doc, "task-doc-recur-custom");
        tryVerify(() => field.visible && field.activeFocus, 1000);
        verify(chip.visible, "the chip is out while the field is open");
        keyClick(Qt.Key_Escape);
        verify(!field.visible);
        verify(!chip.visible, "Esc with nothing set takes the chip back");
        compare(String(AppController.taskById(id).recurrence || ""), "");
        doc.close();
    }

    // ── Ctrl N's repeat box ──

    // Down on the closed box stops at the last rule: it does not step onto
    // the custom row and open its field.
    function test_editor_down_stops_before_the_custom_row() {
        AppController.language = "en";
        const te = make('import TodoCpp; TaskEditor { }');
        te.showFor({ id: "", title: "editor down probe", _isNew: true });
        te.detailsOpen = true;
        const box = findChild(te, "te-recurrence");
        verify(box !== null);
        const lastRule = box._vals.length - 2;
        verify(box._vals[lastRule] !== "custom");
        box.currentIndex = lastRule - 1;
        box.forceActiveFocus();
        tryVerify(() => box.activeFocus, 1000);
        keyClick(Qt.Key_Down);
        compare(box.currentIndex, lastRule, "Down still steps between the rules");
        keyClick(Qt.Key_Down);
        compare(box.currentIndex, lastRule, "Down stops at the last rule");
        verify(!box._customOpen, "the custom field did not open");
        verify(!box.popup.visible);
        te.close();
    }

    function test_editor_custom_row_sets_an_interval() {
        AppController.language = "ru";
        const te = make('import TodoCpp; TaskEditor { }');
        te.showFor({ id: "", title: "editor repeat probe", _isNew: true });
        te.detailsOpen = true;   // the repeat box sits under Details
        const box = findChild(te, "te-recurrence");
        verify(box !== null);
        compare(box._vals[box._vals.length - 1], "custom", "the custom row comes last");
        compare(box.model[box.model.length - 1], "Свой интервал…");
        box.currentIndex = box._vals.indexOf("every:day");
        box.activated(box._vals.length - 1);
        compare(box._vals[box.currentIndex], "every:day", "picking the row keeps the rule there was until one is typed");
        const field = findChild(te, "te-recurrence-custom");
        tryVerify(() => field.visible && field.activeFocus, 1000);
        field.text = "ерунда";   // keyClick takes Latin letters only
        keyClick(Qt.Key_Return);
        verify(box._customBad);
        field.text = "пн, ср";
        keyClick(Qt.Key_Return);
        verify(!box._customOpen);
        compare(box.value(), "every:mon,wed");
        compare(box.currentText, "По пн, ср");
        // An interval from capture opens as itself, not as "none".
        te.showFor({ id: "", title: "x", recurrence: "every:2:day", _isNew: true });
        compare(box.value(), "every:2:day");
        te.close();
    }

    // ── A2-25: the repeats ahead in Week ──

    function test_week_shows_repeats_ahead_faint_and_read_only() {
        AppController.language = "en";
        // A Monday, so the repeats every other day fall in the same week
        // whatever day the suite runs on (on a Saturday the next one was
        // the next week's Monday, and the week showed none).
        const day = probeDay(735);
        day.setDate(day.getDate() - (day.getDay() + 6) % 7);
        const id = mkRepeating("week ghost probe", "every:2:day", day, 9);
        AppController.selectedDate = day;
        const rows = AppController.tasks.rowCount();
        const wv = make('import TodoCpp; WeekView { anchors.fill: parent }');
        const days = wv.buildDays();
        compare(AppController.tasks.rowCount(), rows, "nothing is created ahead");
        let ghosts = 0;
        let realOnDay = false;
        for (let k = 0; k < days.length; k++) {
            for (let i = 0; i < days[k].tasks.length; i++) {
                const t = days[k].tasks[i];
                if (t.id !== id) continue;
                if (t.ghost) {
                    ghosts++;
                    verify(days[k].date > day, "a repeat ahead is after the task's own day");
                    compare(wv._daysBetween(day, days[k].date) % 2, 0, "every other day from the task's day");
                }
            }
            for (let i = 0; i < days[k].blocks.length; i++)
                if (days[k].blocks[i].id === id) realOnDay = wv.isSameDay(days[k].date, day);
        }
        verify(realOnDay, "the task itself is its block on its own day");
        verify(ghosts >= 1, "the week shows the repeats ahead");

        // The cursor stops on one; Return opens the task itself and the move
        // keys leave it alone.
        let opened = "";
        wv.taskClicked.connect((tid) => { opened = tid; });
        let col = -1, at = -1;
        for (let k = 0; k < days.length && col < 0; k++) {
            const items = wv._dayItems(k);
            for (let i = 0; i < items.length; i++)
                if (items[i].kind === "ghost" && items[i].id === id) { col = k; at = i; break; }
        }
        verify(col >= 0, "a repeat ahead is a cursor stop");
        wv._place(col, at);
        verify(wv.cursorKey.indexOf("ghost:") === 0);
        compare(wv.cursorTaskId, "", "a repeat ahead is not the task the keys act on");
        verify(!wv.moveKeyTaskByDays(1), "a repeat ahead does not move");
        const before = AppController.taskById(id).scheduledAt;
        compare(AppController.taskById(id).scheduledAt.getTime(), before.getTime());
        wv.openCursor();
        compare(opened, id, "Return opens the real task");
        // Its chip: the repeat mark, no flag, the time first.
        tryVerify(() => findChild(wv, "week-ghost-" + id) !== null, 2000, "no chip drawn for the repeat ahead");
        const titles = [];
        const walk = (it) => {
            if (!it) return;
            if (it.objectName === "week-ghost-title") titles.push(it.text);
            for (let i = 0; i < it.children.length; i++) walk(it.children[i]);
        };
        walk(wv);
        verify(titles.indexOf(I18n.fmtTime(new Date(2000, 0, 1, 9, 0)) + " week ghost probe") >= 0, "the chip says its time and title");
        compare(wv.dayFacts(days[col]).indexOf(I18n.t("cal.dueColon")), -1, "a repeat ahead is not a deadline of the day");
    }

    // ── A2-25: and in Month ──

    // Fixed dates, not "today + N": the month grid ends with the month, so a
    // probe that lands late in its month has only the one or two repeats left
    // in it (2028-11-20, today + 760 on 2026-10-22, had three and failed the
    // nightly). June 2099 starts on a Monday; the 29th is its last Monday.
    function monthGhosts(cells, id, day) {
        let ghosts = 0, ahead = 0;
        for (let c = 0; c < cells.length; c++) {
            const dow = cells[c].date.getDay();
            if (cells[c].date > day && (dow === 1 || dow === 4)) ahead++;
            for (let i = 0; i < cells[c].tasks.length; i++) {
                const t = cells[c].tasks[i];
                if (t.id !== id || !t.ghost) continue;
                ghosts++;
                verify(cells[c].date > day, "a repeat ahead is after the task's own day");
                verify(dow === 1 || dow === 4, "only on Mondays and Thursdays");
            }
        }
        compare(ghosts, ahead, "every Monday and Thursday ahead in the grid has its repeat");
        return ghosts;
    }

    function test_month_late_in_the_month_shows_the_repeats_left_in_its_grid() {
        const day = new Date(2099, 5, 29);
        const id = mkRepeating("month ghost late probe", "every:mon,thu", day);
        AppController.selectedDate = day;
        const mv = make('import TodoCpp; MonthView { anchors.fill: parent }');
        // Thursday 2 July is the grid's only one left; the rest are July's.
        compare(monthGhosts(mv.buildCells(), id, day), 1);
    }

    function test_month_shows_repeats_ahead_and_enter_opens_the_task() {
        AppController.language = "ru";
        const day = new Date(2099, 5, 1);
        const id = mkRepeating("month ghost probe", "every:mon,thu", day);
        AppController.selectedDate = day;
        const mv = make('import TodoCpp; MonthView { anchors.fill: parent }');
        const cells = mv.buildCells();
        const ghosts = monthGhosts(cells, id, day);
        verify(ghosts >= 8, "a month holds several repeats ahead");
        // The tip says what it is.
        const g = cells.find(c => c.tasks.some(t => t.ghost && t.id === id)).tasks.find(t => t.ghost && t.id === id);
        compare(mv.ghostTip(g), "Повтор «month ghost probe» · по пн, чт · Enter откроет задачу");
        // On a cell with one: the cursor's Return opens the task, the move
        // keys do nothing.
        let target = null;
        for (let c = 0; c < cells.length; c++)
            if (cells[c].tasks.some(t => t.ghost && t.id === id)) { target = cells[c].date; break; }
        AppController.selectedDate = target;
        let opened = "";
        mv.taskClicked.connect((tid) => { opened = tid; });
        tryVerify(() => mv._selCell >= 0);
        const items = mv._cellItems(mv._selCell);
        const at = items.findIndex(it => it.kind === "ghost" && it.id === id);
        verify(at >= 0);
        mv._placeIdx(at);
        verify(!mv.moveKeyTaskByDays(1));
        verify(!mv.moveKeyTaskByTime(1));
        compare(mv.cursorTaskId, "");
        mv.openCursor();
        compare(opened, id);
    }
}
