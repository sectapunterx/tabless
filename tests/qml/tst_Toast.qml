// The toast stack (audit UX-16, APP-225).
//
// One slot used to hold every notice, so a later toast replaced a pending
// "Undo" and the undo was gone; the bar had no width cap (1093px at 1100);
// and every toast from C++ arrived as "info", failures included.
// APP-225: on a big monitor the toasts were small, far from the eyes and
// gone before they were seen. Their time now stands still while they are
// hovered, focused or the window is in the background; they last by kind;
// a wide work area gets them in its bottom-right corner.
import QtQuick
import QtTest
import TodoCpp
import "../../qml/ToastTiming.js" as Timing

TestCase {
    id: tc
    name: "Toast"
    when: windowShown
    visible: true
    width: 1100
    height: 600

    property string savedSettings: ""

    Item { id: host; anchors.fill: parent }

    function initTestCase() { tc.savedSettings = AppController.appSettingsJson; }
    function cleanup() { AppController.appSettingsJson = tc.savedSettings; }

    // The window an offscreen test runs in may or may not count as active;
    // the timing tests say themselves whether the toasts can be seen.
    function make(extra) {
        const t = createTemporaryQmlObject(
            'import TodoCpp; Toast { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; '
            + (extra || "") + ' }', host);
        verify(t !== null);
        t.appVisible = true;
        t.hoverHeld = false;
        return t;
    }

    function cards(t) {
        const out = [];
        const walk = function (it) {
            if (!it) return;
            if (it.objectName === "toast-card") out.push(it);
            const kids = it.children || [];
            for (let i = 0; i < kids.length; i++) walk(kids[i]);
        };
        walk(t);
        return out;
    }

    function child(root, name) {
        if (!root) return null;
        if (root.objectName === name) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) {
            const r = child(kids[i], name);
            if (r) return r;
        }
        return null;
    }

    function test_undo_toast_survives_later_toasts() {
        const t = make();
        let undone = 0;
        t.showWithAction("Deleted: A", "Undo", 10, function () { undone++; });
        t.show("Saved");
        t.show("Synced");
        t.show("Another thing");
        wait(0);
        let undo = null;
        for (const c of cards(t)) if (c.modelData.actionLabel === "Undo") undo = c;
        verify(undo !== null, "a later toast replaced the Undo toast");
        verify(t.count <= t.maxVisible);
        const ma = (function find(it) {
            if (it.cursorShape === Qt.PointingHandCursor && it.clicked !== undefined) return it;
            const kids = it.children || [];
            for (let i = 0; i < kids.length; i++) { const r = find(kids[i]); if (r) return r; }
            return null;
        })(undo);
        mouseClick(ma);
        compare(undone, 1);
    }

    function test_action_toasts_queue_rather_than_replace() {
        const t = make();
        for (let i = 0; i < 4; i++) t.showWithAction("Deleted " + i, "Undo", 10, function () {});
        compare(t.count, t.maxVisible);
        compare(t.maxVisible, 1, "one toast at a time (DG-140)");
        compare(t._queue.length, 3, "the others wait for the slot");
        compare(t.waiting, 3, "and are counted as +N");
        t.dismiss(t._items[0].id);
        compare(t.count, t.maxVisible);
        compare(t._queue.length, 2);
    }

    // a-27 (N/X-Ntf-Toasts): a burst is one toast with a count and the log,
    // "Готово: 3 задачи  +2 события  Журнал  Ctrl Shift L".
    function test_a_burst_is_one_toast_with_a_count_and_the_log() {
        const t = make();
        let logs = 0;
        t.logRequested.connect(function () { logs++; });
        t.show("Saved", "success");
        t.show("Synced", "info");
        t.show("Done: 3 tasks", "success");
        wait(0);
        compare(t.count, 1);
        compare(t.message, "Done: 3 tasks", "the newest is on screen");
        const c = cards(t)[0];
        const more = child(c, "toast-more");
        verify(more.visible, "no count of the ones pushed off");
        compare(more.text, I18n.count(2, "toast.more"));
        const log = child(c, "toast-log");
        verify(log.parent.visible, "no way to the log");
        compare(child(c, "toast-log-key").text, AppController.shortcutText("log.open"));
        mouseClick(log);
        compare(logs, 1);
        compare(t.count, 0, "the log link closes the toast");
        // A single notice has neither.
        t.show("Alone");
        wait(0);
        const c2 = cards(t)[0];
        verify(!child(c2, "toast-more").visible);
        verify(!child(c2, "toast-log").parent.visible);
    }

    // A5-19 follow-up: "Updated: 0.8.2 → 0.8.9 · What's new" carries an
    // action, yet nothing is lost by missing it. An Undo raised during its ten
    // seconds is shown at once instead of waiting behind it as "+1".
    function test_yielding_action_toast_gives_way_to_an_undo() {
        const t = make();
        t.showWithAction("Updated: 0.8.2 → 0.8.9", "What's new", 10, function () {}, "info", "", true);
        compare(t.count, 1);
        t.showUndo("Deleted: RET-1", "Undo", 10, function () {});
        compare(t.count, 1);
        compare(t.message, "Deleted: RET-1", "the Undo is on screen");
        compare(t.waiting, 0, "nothing waits behind the update notice");
        // A plain action toast still holds its place.
        t.showWithAction("Sync failed", "Retry", 10, function () {});
        compare(t.message, "Deleted: RET-1");
        compare(t.waiting, 1);
    }

    // PERSONA-7: the Undo on screen is the one Ctrl+Z takes back — the newest.
    function test_newest_undo_replaces_an_older_one() {
        const t = make();
        let which = "";
        t.showUndo("Created A", "Undo", 10, function () { which = "A"; });
        t.showUndo("Moved B", "Undo", 10, function () { which = "B"; });
        compare(t.count, 1);
        compare(t.message, "Moved B");
        compare(t._queue.length, 0, "the older Undo waits behind the newer one");
        t._items[0].actionFn();
        compare(which, "B");
    }

    // IDIOT-SHELL-4: no history behind it — the toast keeps its words and
    // stops offering Undo.
    function test_drop_undo_strips_the_action() {
        const t = make();
        t.showUndo("Deleted: A", "Undo", 10, function () {}, "success", "Ctrl Z");
        compare(t.actionLabel, "Undo");
        t.dropUndo();
        compare(t.count, 1);
        compare(t.message, "Deleted: A");
        compare(t.actionLabel, "");
        compare(t._items[0].key, "");
    }

    // Dogfood 2026-10-30: the Undo button fades with the toast, Ctrl Z does
    // not, and the toast says so; a toast without Undo says nothing of it.
    function test_undo_toast_says_the_key_works_any_time() {
        const t = make();
        t.showUndo("Deleted: A", "Undo", 10, function () {}, "success", "Ctrl Z");
        let c = cards(t)[0];
        verify(child(c, "toast-undo-anytime").visible);
        compare(child(c, "toast-undo-anytime").text, I18n.t("undo.anyTime"));
        t.dropUndo();
        c = cards(t)[0];
        verify(!child(c, "toast-undo-anytime").visible, "no button, no key: nothing to say");
        t.clear();
        t.showWithAction("Retry?", "Retry", 0, function () {}, "warning", "Ctrl R");
        verify(!child(cards(t)[0], "toast-undo-anytime").visible);
    }

    // A1-21: Ctrl+Z took back the action an Undo toast names; the toast
    // goes, and the notice waiting behind it shows.
    function test_an_undone_action_takes_its_toast_away() {
        const t = make();
        t.showUndo("RD-21: Tue 14:30", "Undo", 10, function () {}, "success", "Ctrl Z", 7);
        t.show("waiting", "info");
        compare(t._queue.length, 1, "a plain notice waits behind the Undo");
        t.retireUndone(7);
        compare(t.message, "RD-21: Tue 14:30", "still the newest entry: it stays");
        t.retireUndone(6);
        compare(t.message, "waiting", "undone: the waiting notice shows instead");
        compare(t._queue.length, 0);
    }

    // The default serial is the newest history entry's, read when it shows.
    function test_undo_toast_remembers_the_entry_it_names() {
        const t = make();
        t.showUndo("Moved", "Undo", 10, function () {});
        compare(t._items[0].serial, Number(AppController.undoSerialForToast()));
    }

    function test_width_is_capped_and_long_text_wraps() {
        const t = make();
        let longText = "";
        for (let i = 0; i < 60; i++) longText += "word" + i + " ";
        t.show(longText);
        wait(0);
        const c = cards(t)[0];
        verify(c.width <= Theme.toastMaxWidth, "toast is " + c.width + "px wide");
        verify(c.height > 3 * Theme.fsLg, "long text should wrap onto more lines");
    }

    function test_kind_tints_and_errors_stay_longer() {
        const t = make();
        t.show("Sync failed", "error");
        compare(t.kind, "error");
        verify(t._items[0].ms > Timing.duration("info", 0, false), "an error must stay up longer than a notice");
    }

    function test_plain_duplicate_refreshes_instead_of_stacking() {
        const t = make();
        t.show("Sync is running");
        t.show("Sync is running");
        compare(t.count, 1);
    }

    function test_toasts_expire() {
        const t = make();
        t._push({ id: 99, message: "brief", kind: "info", actionLabel: "", actionFn: null, ms: 50 });
        compare(t.count, 1);
        tryCompare(t, "count", 0, 1000);
    }

    // ── APP-225: durations ──────────────────────────────────────────────
    function test_durations_by_kind() {
        compare(Timing.duration("info", 0, false), 4000);
        compare(Timing.duration("success", 0, false), 4000);
        compare(Timing.duration("warning", 0, false), 5500);
        compare(Timing.duration("error", 0, false), 8000);
        compare(Timing.duration("info", 0, true), 8000, "an Undo stays at least 8 s");
        compare(Timing.duration("info", 5, true), 8000, "a caller's 5 s Undo is lifted to 8 s");
        compare(Timing.duration("info", 15, true), 15000, "a longer ask is kept");
        compare(Timing.duration("error", 3, true), 8000);
        const t = make();
        t.show("note");
        compare(t._items[0].ms, 4000);
        t.show("careful", "warning");
        compare(t._items[0].ms, 5500, "a plain notice gives way to the next");
        t.showWithAction("Deleted", "Undo", 5, function () {});
        compare(t._items[0].ms, 8000);
    }

    // Two actions (APP-204's refused move): same look, same clock, both
    // buttons inside the width cap, and either one closes the toast.
    function test_two_actions_get_the_same_rules() {
        const t = make();
        let ran = "";
        t.showWithActions("WEB-5 is no longer in your filter — the drop was refused", [
            { label: "Open in tracker", fn: function () { ran = "open"; } },
            { label: "Archive", fn: function () { ran = "archive"; } }
        ], 0, "warning");
        compare(t._items[0].ms, 8000, "a toast with actions stays at least 8 s");
        t.showWithActions("short", [{ label: "Go", fn: function () {} }], 3);
        compare(t._queue[0].ms, 8000);
        wait(0);
        const c = cards(t)[0];
        verify(c.width <= Theme.toastMaxWidth);
        const second = child(c, "toast-action-2");
        verify(second !== null && second.parent.visible);
        second.activated();
        compare(ran, "archive");
        compare(t.count, 1, "the second action closed its toast; the waiting one came up");
        compare(t.message, "short");
    }

    // The clock itself, with `now` passed in.
    function test_timing_rules_are_pure() {
        const a = Timing.start({}, 1000, 0, false);
        compare(Timing.remaining(a, 400), 600);
        Timing.hold(a, 400);
        compare(Timing.remaining(a, 5000), 600, "held: time stands still");
        Timing.resume(a, 5000);
        compare(Timing.remaining(a, 5500), 100);
        const b = Timing.start({}, 1000, 0, true);
        compare(Timing.remaining(b, 99999), 1000, "shown while unseen: the countdown has not started");
        const s = Timing.sweep([a, b], 5600);
        compare(s.keep.length, 1);
        compare(s.expired, 1);
        compare(s.next, Infinity, "nothing left is counting down");
        verify(!Timing.corner(1100, 1200));
        verify(Timing.corner(1440, 1200));
    }

    // ── APP-225: the countdown waits for the user ───────────────────────
    function test_timer_pauses_while_hovered() {
        const t = make();
        t.hoverHeld = true;
        t._push({ id: 101, message: "hovered", kind: "info", actionLabel: "", actionFn: null, ms: 80 });
        verify(t.held);
        wait(300);
        compare(t.count, 1, "a hovered toast ran out");
        t.hoverHeld = false;
        tryCompare(t, "count", 0, 1000);
    }

    function test_timer_pauses_while_window_inactive() {
        const t = make();
        t._push({ id: 102, message: "running", kind: "info", actionLabel: "", actionFn: null, ms: 400 });
        t.appVisible = false;
        verify(t.held);
        const left = Timing.remaining(t._items[0], Date.now());
        wait(600);
        compare(t.count, 1, "a toast ran out behind another window");
        verify(Math.abs(Timing.remaining(t._items[0], Date.now()) - left) < 1, "time moved while held");
        t.appVisible = true;
        tryCompare(t, "count", 0, 1500);
    }

    function test_shown_while_minimized_starts_counting_when_seen() {
        const t = make();
        t.appVisible = false;
        t._push({ id: 103, message: "while away", kind: "info", actionLabel: "", actionFn: null, ms: 80 });
        wait(300);
        compare(t.count, 1);
        compare(Timing.remaining(t._items[0], Date.now()), 80);
        t.appVisible = true;
        tryCompare(t, "count", 0, 1000);
    }

    function test_timer_pauses_while_action_focused() {
        const t = make();
        t.showWithAction("Deleted", "Undo", 0, function () {});
        let act = null;
        tryVerify(function () { act = child(t, "toast-action"); return act !== null; }, 1000);
        act.forceActiveFocus();
        tryCompare(t, "focusInside", true);
        verify(t.held);
        verify(t._items[0].since < 0, "the countdown kept going with the keyboard on the toast");
        act.focus = false;
        host.forceActiveFocus();
        tryCompare(t, "held", false);
        verify(t._items[0].since >= 0);
    }

    function test_app_visible_follows_the_window() {
        const t = createTemporaryQmlObject('import TodoCpp; Toast {}', host);
        compare(t.appVisible, t.Window.active);
    }

    // ── APP-225: where the stack sits ───────────────────────────────────
    function test_narrow_area_keeps_bottom_centre() {
        const t = createTemporaryQmlObject('import TodoCpp; Toast { width: 1000; areaWidth: 1000; anchors.bottom: parent.bottom }', host);
        t.appVisible = true;
        t.show("centred");
        wait(0);
        verify(!t.wide);
        const st = child(t, "toast-stack");
        verify(Math.abs(st.x + st.width / 2 - t.width / 2) <= 1, "stack at " + st.x);
    }

    // DG-140: a wide work area keeps it at the bottom centre too.
    function test_wide_area_keeps_bottom_centre() {
        const w = Theme.toastWideFrom + 300;
        const t = createTemporaryQmlObject('import TodoCpp; Toast { width: ' + w + '; areaWidth: ' + w + '; anchors.bottom: parent.bottom }', host);
        t.appVisible = true;
        t.show("short");
        wait(0);
        verify(!t.wide);
        const st = child(t, "toast-stack");
        verify(Math.abs(st.x + st.width / 2 - t.width / 2) <= 1, "stack at " + st.x);
    }

    // ── APP-225: how it looks and arrives ───────────────────────────────
    // DG-140: a ring for the kind, the action underlined, its key after it;
    // an error is told by its shape, not a red bar.
    function test_kind_ring_action_and_key() {
        const t = make();
        t.show("note");
        t.showWithAction("broke", "Retry", 10, function () {}, "error", "Ctrl Z");
        wait(0);
        const cs = cards(t);
        compare(cs.length, 1, "one toast at a time");
        compare(child(cs[0], "toast-kind-icon").category, "blocked");
        compare(child(cs[0], "toast-message").font.pixelSize, Theme.fsMd);
        compare(child(cs[0], "toast-key").text, "Ctrl Z");
        verify(child(cs[0], "toast-key").visible);
        verify(child(cs[0], "toast-accent") === null, "no accent bar");
    }

    function test_new_toast_slides_in() {
        AppController.appSettingsJson = JSON.stringify({ appearance: { reducedMotion: false } });
        compare(Theme.reducedMotion, false);
        const t = make();
        t.show("first");
        wait(0);
        let c = cards(t)[0];
        verify(c.entering);
        verify(c.transform[0].y > 0 || c.opacity < 1, "a new toast appeared without its entrance");
        tryCompare(c.transform[0], "y", 0, 1000);
        t.show("second");
        wait(0);
        const cs = cards(t);
        compare(cs.length, 1, "the plain notice gave way");
        compare(cs[0].modelData.message, "second");
    }

    function test_reduced_motion_only_appears() {
        AppController.appSettingsJson = JSON.stringify({ appearance: { reducedMotion: true } });
        compare(Theme.reducedMotion, true);
        const t = make();
        t.show("broke", "error");
        wait(0);
        const c = cards(t)[0];
        compare(c.transform[0].y, 0, "it moved with Reduce motion on");
        compare(c.opacity, 1);
    }
}
