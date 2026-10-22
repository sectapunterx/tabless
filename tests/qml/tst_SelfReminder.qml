// A reminder to self said in a capture line (dogfood 2026-10-22): quick
// capture and the Ctrl N editor read "напомни мне …", say what Enter makes,
// offer the times when none was said, and Today lists the reminders with a
// bell. Keyboard only, and the wrong keys too: Enter spam, Esc mid-choice,
// digits typed into the title.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SelfReminder"
    when: windowShown
    visible: true
    width: 1200
    height: 800

    Item { id: host; anchors.fill: parent }
    property var made: []
    property date savedDate

    function initTestCase() {
        tc.savedDate = AppController.selectedDate;
        AppController.markWelcomeSeen();
        // The test profile outlives the run: a crashed run's reminders.
        for (const r of AppController.todayData(AppController.today).reminders || [])
            if (/selfrem|купить 2/.test(String(r.title))) AppController.deleteTask(r.id);
    }
    function cleanup() {
        for (const id of tc.made) AppController.deleteTask(id);
        tc.made = [];
        AppController.clearPendingUndo();
        AppController.selectedDate = tc.savedDate;
    }

    function openCapture() {
        const qc = createTemporaryQmlObject('import TodoCpp; QuickCapturePopup { }', host);
        qc.open();
        tryVerify(() => qc.opened);
        findChild(qc, "qc-input").forceActiveFocus();
        return qc;
    }
    function spyOn(qc) {
        const spy = createTemporaryQmlObject('import QtTest; SignalSpy { signalName: "captured" }', host);
        spy.target = qc;
        return spy;
    }
    function type(qc, text) {
        const input = findChild(qc, "qc-input");
        input.text = text;
        input.cursorPosition = text.length;
        qc._refreshPreview();
        return input;
    }
    function valid(d) { return !!(d && d.getTime && !isNaN(d.getTime())); }
    function madeTask(spy, i) {
        const id = String(spy.signalArguments[i][2]);
        tc.made.push(id);
        return AppController.taskById(id);
    }

    function test_a_time_said_shows_the_chip_and_enter_makes_the_reminder() {
        const qc = openCapture();
        const spy = spyOn(qc);
        type(qc, "напомни мне позвонить в банк selfrem завтра в 15:00");
        const rc = findChild(qc, "reminder-capture");
        verify(rc.visible, "no reminder preview");
        verify(!findChild(qc, "qc-chips").visible, "the task chips speak over the reminder");
        const chip = findChild(rc, "reminder-chip-text").text;
        verify(chip.indexOf(I18n.t("self.chip")) === 0, chip);
        verify(chip.indexOf("15:00") > 0, chip);
        verify(!findChild(rc, "reminder-options").visible, "a time was said: nothing to ask");
        compare(findChild(rc, "reminder-title").text, I18n.t("quick.quote").arg("позвонить в банк selfrem"));
        keyClick(Qt.Key_Return);
        tryVerify(() => spy.count === 1);
        const t = madeTask(spy, 0);
        compare(t.title, "позвонить в банк selfrem");
        verify(t.reminder, "not flagged as a reminder");
        const at = new Date(t.remindAt);
        compare(at.getHours(), 15);
        verify(!valid(t.scheduledAt), "the time is the reminder's, not a plan");
        verify(!qc.opened, "Enter closes the capture");
    }

    function test_no_time_offers_the_choices_and_enter_takes_the_first() {
        const qc = openCapture();
        const spy = spyOn(qc);
        type(qc, "напомни мне ответить Антону selfrem");
        const rc = findChild(qc, "reminder-capture");
        verify(findChild(rc, "reminder-options").visible, "no choices");
        const first = rc.options[0];
        verify(first.key === "hour" || first.key === "nextDay", first.key);
        compare(rc.options[rc.options.length - 1].key, "none");
        keyClick(Qt.Key_Return);
        tryVerify(() => spy.count === 1);
        const t = madeTask(spy, 0);
        verify(t.reminder);
        compare(new Date(t.remindAt).getTime(), first.at.getTime(), "Enter did not take the default");
    }

    // Digits in the title are the title's: the row takes them only after ↓.
    function test_digits_are_text_until_the_row_is_entered() {
        const qc = openCapture();
        const spy = spyOn(qc);
        const input = type(qc, "напомни мне selfrem купить 2 ");
        const rc = findChild(qc, "reminder-capture");
        keyClick(Qt.Key_1);
        compare(spy.count, 0, "a digit in the title picked a time");
        verify(input.text.endsWith("2 1"), input.text);
        verify(!rc.choosing);
        // ↓ into the row; Esc goes back to the line and keeps the popup.
        keyClick(Qt.Key_Down);
        verify(rc.choosing, "↓ did not enter the row");
        keyClick(Qt.Key_Escape);
        verify(!rc.choosing, "Esc mid-choice did not go back");
        verify(qc.opened, "Esc mid-choice closed the capture");
        // Typing in the row goes back to the line, the letter included.
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_A);
        verify(!rc.choosing);
        verify(input.text.endsWith("1a"), input.text);
        // The last digit is "no time": no notification, still in Today.
        keyClick(Qt.Key_Down);
        const last = rc.options.length - 1;
        keyClick(Qt.Key_0 + last);
        tryVerify(() => spy.count === 1);
        const t = madeTask(spy, 0);
        verify(t.reminder);
        verify(!valid(t.remindAt), "\"no time\" got a time");
    }

    function test_arrows_and_enter_pick_in_the_row() {
        const qc = openCapture();
        const spy = spyOn(qc);
        type(qc, "remind me to stretch selfrem");
        const rc = findChild(qc, "reminder-capture");
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Right);
        compare(rc.choiceIndex, 1);
        const pick = rc.options[1];
        keyClick(Qt.Key_Return);
        tryVerify(() => spy.count === 1);
        const t = madeTask(spy, 0);
        compare(t.title, "stretch selfrem");
        compare(new Date(t.remindAt).getTime(), pick.at.getTime());
    }

    function test_tab_makes_an_ordinary_task() {
        const qc = openCapture();
        const spy = spyOn(qc);
        type(qc, "напомни Антону про ревью selfrem");
        const rc = findChild(qc, "reminder-capture");
        verify(rc.visible);
        keyClick(Qt.Key_Tab);
        verify(!rc.visible, "Tab left the reminder on");
        verify(qc.opened, "Tab left the capture");
        keyClick(Qt.Key_Return);
        tryVerify(() => spy.count === 1);
        const t = madeTask(spy, 0);
        verify(!t.reminder, "Tab still made a reminder");
        verify(t.title.indexOf("напомни") === 0, "the phrase left the title: " + t.title);
    }

    function test_enter_spam_makes_one() {
        const qc = openCapture();
        const spy = spyOn(qc);
        type(qc, "напомни мне selfrem спам");
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Return);
        wait(50);
        compare(spy.count, 1, "Enter spam made more than one");
        madeTask(spy, 0);
    }

    function test_a_phrase_alone_says_what_is_missing() {
        const qc = openCapture();
        const spy = spyOn(qc);
        type(qc, "напомни мне");
        keyClick(Qt.Key_Return);
        wait(30);
        compare(spy.count, 0);
        compare(qc._hint, I18n.t("self.empty"));
        verify(qc.opened);
        qc.close();
    }

    function test_not_a_reminder() {
        const qc = openCapture();
        type(qc, "напоминание о встрече selfrem");
        verify(!findChild(qc, "reminder-capture").visible, "a title read as a reminder");
        qc.close();
    }

    function test_ctrl_n_editor_makes_a_reminder_too() {
        const te = createTemporaryQmlObject('import TodoCpp; TaskEditor { }', host);
        te.showFor(AppController.newTaskDraft(""));
        tryVerify(() => te.opened);
        const title = findChild(te, "te-title");
        title.forceActiveFocus();
        const id = AppController.newQuickTaskDraft().id;
        title.text = "напомни мне проверить пайплайн selfrem через 20 минут";
        const rc = findChild(te, "reminder-capture");
        tryVerify(() => rc.visible, 1000, "no reminder preview in the editor");
        verify(!findChild(te, "te-title-read").visible);
        keyClick(Qt.Key_Return);
        tryVerify(() => !te.opened);
        tc.made.push(id);
        const t = AppController.taskById(id);
        compare(t.title, "проверить пайплайн selfrem");
        verify(t.reminder);
        const mins = (new Date(t.remindAt).getTime() - Date.now()) / 60000;
        verify(mins > 15 && mins <= 21, "not in 20 minutes: " + mins);
    }

    function test_today_lists_them_with_a_bell() {
        AppController.selectedDate = AppController.today;
        const now = new Date();
        const at = AppController.reminderDraft("напомни мне selfrem в банк сегодня в 23:59", now);
        AppController.saveTask(at);
        tc.made.push(at.id);
        const none = AppController.reminderDraft("напомни мне selfrem домен", now, "none");
        AppController.saveTask(none);
        tc.made.push(none.id);
        const v = createTemporaryQmlObject('import TodoCpp; TodayView { anchors.fill: parent }', host);
        const rows = v.rows.filter(r => r.block && r.block.reminder && (r.block.id === at.id || r.block.id === none.id));
        compare(rows.length, 2);
        const timed = rows.filter(r => r.block.id === at.id)[0];
        const later = rows.filter(r => r.block.id === none.id)[0];
        verify(!timed.block.someday);
        verify(later.block.someday, "not under someday");
        verify(v.rows.some(r => r.block && r.block.somedayFirst && r.start <= later.start), "no rule above someday");
        verify(later.start > timed.start, "someday is not at the bottom");
        compare(v._sub("task", timed.block), I18n.t("today.reminder"));
        compare(v._somedayWord(later.block), I18n.t("today.someday"));
    }
}
