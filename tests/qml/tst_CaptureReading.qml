// Quick capture reads the line before Enter, and says so (APP-221 batch B2):
// a repeat has its chip (A2-21), the end of a range is in the "when" chip, and
// "also a meeting" answers to Ctrl+M, the hint line naming it and Tab
// (A2-14).
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "CaptureReading"
    when: windowShown
    visible: true
    width: 1000
    height: 700

    Item { id: host; anchors.fill: parent }

    function open() {
        const qc = createTemporaryQmlObject('import TodoCpp; QuickCapturePopup { }', host);
        verify(qc !== null);
        qc.open();
        tryVerify(() => qc.opened);
        const input = findChild(qc, "qc-input");
        input.forceActiveFocus();
        return qc;
    }
    function type(qc, text) {
        const input = findChild(qc, "qc-input");
        input.text = text;
        input.cursorPosition = text.length;
        qc._refreshPreview();
    }
    function hints(qc) {
        const out = [];
        (function walk(it) {
            if (!it) return;
            if (it.objectName === "qc-keys-hint") {
                for (const c of it.children) if (c.text !== undefined) out.push(c.text);
                return;
            }
            for (const k of it.children || []) walk(k);
        })(qc.contentItem);
        return out;
    }

    function test_a_repeat_has_its_chip_before_enter() {
        const qc = open();
        type(qc, "Отчёт каждый понедельник");
        const chip = findChild(qc.contentItem, "qc-repeat");
        verify(chip !== null);
        verify(chip.visible, "no repeat chip");
        verify(chip.value.length > 0);
        type(qc, "Отчёт в понедельник");
        verify(!chip.visible, "a one-off is not a repeat");
        qc.close();
    }

    function test_the_when_chip_keeps_the_end_of_a_range() {
        const qc = open();
        type(qc, "release tomorrow 23:00-01:00");
        const chip = findChild(qc.contentItem, "qc-when");
        verify(chip.visible);
        verify(chip.value.indexOf("–") >= 0, "the end is dropped: " + chip.value);
        qc.close();
    }

    function test_ctrl_m_makes_it_a_meeting() {
        const qc = open();
        type(qc, "call with anna capturereading tomorrow 9:00-10:00");
        const box = findChild(qc.contentItem, "qc-meeting");
        verify(box.visible);
        verify(!box.checked);
        verify(hints(qc).indexOf(I18n.t("capture.hint.meeting")) >= 0, "the hint line does not name Ctrl M");
        verify(hints(qc).indexOf(I18n.t("capture.hint.tab")) >= 0, "the hint line does not name Tab");
        keyClick(Qt.Key_M, Qt.ControlModifier);
        verify(box.checked, "Ctrl M did not tick the meeting");
        keyClick(Qt.Key_M, Qt.ControlModifier);
        verify(!box.checked, "Ctrl M did not untick it");
        compare(findChild(qc, "qc-input").text, "call with anna capturereading tomorrow 9:00-10:00", "Ctrl M typed into the line");
        // With no time there is no meeting: Ctrl M says why.
        type(qc, "capturereading no time");
        keyClick(Qt.Key_M, Qt.ControlModifier);
        verify(!box.visible);
        compare(qc._hint, I18n.t("capture.meeting.needTime"));
        qc.close();
    }

    // Dogfood 2026-10-16: Enter on "@vik" with Viktor in the list picks
    // Viktor, as in a messenger, and the line stays open; it created the
    // task with the half-typed name.
    function test_enter_picks_the_person() {
        const draft = AppController.newPersonDraft();
        draft.name = "Viktorcapread";
        draft.id = AppController.suggestPersonId(draft.name);
        draft.state = "idle";
        verify(AppController.savePerson(draft));
        const qc = open();
        const spy = createTemporaryQmlObject('import QtTest; SignalSpy { signalName: "captured" }', host);
        spy.target = qc;
        try {
            const input = findChild(qc, "qc-input");
            type(qc, "write @viktorcapr");
            tryVerify(() => input.text.length > 0);
            keyClick(Qt.Key_Return);
            wait(50);
            compare(spy.count, 0, "Enter created the task with a half-typed name");
            verify(qc.opened);
            verify(input.text.indexOf("@" + draft.id + " ") >= 0, input.text);
        } finally {
            qc.close();
            AppController.deletePerson(draft.id);
        }
    }

    // APP-319: "напомни в 15:00" shows its own chip before Enter. Since the
    // reminder to self (dogfood 2026-10-22) "remind me" makes the line one,
    // and its preview says when; "reminder at 3pm" keeps the task's chip.
    function test_a_reminder_has_its_chip() {
        const qc = open();
        type(qc, "call the bank remind me at 3pm");
        const rc = findChild(qc, "reminder-capture");
        tryVerify(() => rc.visible, 1000, "no reminder preview");
        verify(findChild(rc, "reminder-chip-text").text.indexOf(I18n.fmtTime(new Date(2026, 0, 1, 15, 0))) >= 0);
        type(qc, "call the bank reminder at 3pm");
        const chip = findChild(qc, "qc-remind");
        verify(chip !== null);
        tryVerify(() => chip.visible, 1000, "no remind chip");
        qc.close();
    }

    function test_ctrl_m_then_enter_books_the_meeting() {
        const qc = open();
        const spy = createTemporaryQmlObject('import QtTest; SignalSpy { signalName: "captured" }', host);
        spy.target = qc;
        type(qc, "sync capturereading tomorrow 9:00-10:00");
        keyClick(Qt.Key_M, Qt.ControlModifier);
        keyClick(Qt.Key_Return);
        tryVerify(() => spy.count === 1);
        const title = String(spy.signalArguments[0][0]);
        const taskId = String(spy.signalArguments[0][2]);
        verify(title.indexOf(I18n.t("quick.done.meeting.sync")) === 0 || title.indexOf(I18n.t("quick.done.meeting.none")) === 0,
               "no meeting was booked: " + title);
        AppController.deleteTask(taskId);
    }

    // F1-new-4: a repeating capture made a meeting too books a repeating
    // meeting, the rule of its task, not one on the first day only.
    function test_a_repeating_meeting_repeats() {
        const qc = open();
        const spy = createTemporaryQmlObject('import QtTest; SignalSpy { signalName: "captured" }', host);
        spy.target = qc;
        type(qc, "capreading sync every 2 weeks on mon 15:00");
        keyClick(Qt.Key_M, Qt.ControlModifier);
        keyClick(Qt.Key_Return);
        tryVerify(() => spy.count === 1);
        const taskId = String(spy.signalArguments[0][2]);
        compare(AppController.taskById(taskId).recurrence, "every:2:week");
        const from = new Date();
        const to = new Date(from.getFullYear(), from.getMonth(), from.getDate() + 40);
        const occ = AppController.eventOccurrences(from, to).filter(o => o.taskId === taskId);
        verify(occ.length >= 2, "the meeting is booked once: " + occ.length);
        const master = AppController.eventById(occ[0].masterId || occ[0].id);
        compare(master.rrule, "FREQ=WEEKLY;INTERVAL=2");
        AppController.deleteEvent(master.id);
        AppController.deleteTask(taskId);
    }

    // A6-6: "UTC-5" is a zone said with the time, not a ticket key; the
    // whole app path (meta first, then the date) moves 15:00 there to here.
    function test_a_zone_offset_is_read_with_the_time() {
        const ref = new Date(2026, 9, 12, 10, 0);
        for (const zone of ["UTC-5", "GMT-5", "UTC-05:00"]) {
            const p = AppController.captureParse("call tue 15:00 " + zone, ref, []);
            compare(p.ticketKey, "", zone + " is not a ticket");
            compare(p.title, "call", zone);
            verify(p.whenHasTime, zone);
            const utc = new Date(Date.UTC(2026, 9, 13, 20, 0));
            compare(p.when.getTime(), utc.getTime(), zone + " is 20:00 UTC");
        }
        const et = AppController.captureParse("review tue 3pm ET", ref, []);
        compare(et.title, "review");
        compare(et.when.getTime(), new Date(Date.UTC(2026, 9, 13, 19, 0)).getTime(), "3pm EDT is 19:00 UTC");
    }

    // REG-1: lengths and "daily" inside an ordinary title stay in it.
    function test_title_words_are_not_lengths_or_repeats() {
        const ref = new Date(2026, 9, 12, 10, 0);
        for (const text of ["Fix 2h timeout", "Increase 30 min cache TTL", "Fix daily standup bot"]) {
            const p = AppController.captureParse(text, ref, []);
            compare(p.title, text);
            compare(p.estimateMinutes, 0, text);
            compare(p.recurrence, "", text);
        }
    }
}
