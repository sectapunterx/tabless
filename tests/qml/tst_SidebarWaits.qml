// "Ждут меня" in the sidebar (dogfood 2026-10-21 item 2): the requests where
// someone waits for me, the longest wait first; Return / a click opens the
// thread itself, Up / Down walk the rows; a closed request, a resolved thread
// or no known login leaves nothing behind, and an empty block is hidden.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SidebarWaits"
    when: windowShown
    visible: true
    width: 320
    height: 600

    Item { id: host; anchors.fill: parent }

    readonly property string url482: "https://gitlab.company.ru/tel/app/-/merge_requests/482"
    readonly property string url479: "https://gitlab.company.ru/tel/app/-/merge_requests/479"

    function init() { AppController.clearMrThreadsForTest(); }
    function cleanup() { AppController.clearMrThreadsForTest(); }

    function hoursAgo(h) { return new Date(Date.now() - h * 3600 * 1000).toISOString(); }
    // Ira answered my word in a diff thread `h` hours ago.
    function answered(h, resolved) {
        return JSON.stringify([{ id: "d1", individual_note: false, notes: [
            { id: 101, type: "DiffNote", body: "А если 429?", author: { username: "ira" }, created_at: tc.hoursAgo(h + 2),
              system: false, resolvable: true, resolved: !!resolved, position: { new_path: "src/retry.cpp", new_line: 88 } },
            { id: 102, type: "DiffNote", body: "Проверю", author: { username: "fin" }, created_at: tc.hoursAgo(h + 1),
              system: false, resolvable: true, resolved: !!resolved },
            { id: 103, type: "DiffNote", body: "Нужен Retry-After", author: { username: "ira" }, created_at: tc.hoursAgo(h),
              system: false, resolvable: true, resolved: !!resolved }] }]);
    }
    function make() {
        const b = createTemporaryQmlObject('import TodoCpp; SidebarWaits { width: 260 }', host);
        verify(b !== null);
        return b;
    }
    function row(b, i) { return findChild(b, "sidebar-wait-" + i); }

    function test_empty_block_is_hidden() {
        const b = make();
        compare(b.rows.length, 0);
        verify(!b.visible, "nobody waits: no block");
    }

    function test_rows_oldest_first_with_reason_and_age() {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2)));
        // A review asked two days ago (the request's age), nothing given.
        verify(AppController.setMrThreadsForTest("gitlab", tc.url479, "Split ledger export", "ira", ["fin"], "fin", "[]"));
        const b = make();
        tryVerify(() => b.rows.length === 2, 1000, "both requests wait on me");
        verify(b.visible);
        compare(b.rows[0].ref, "!479", "the longest wait first");
        compare(b.rows[0].reason, "review");
        compare(b.rows[1].ref, "!482");
        compare(b.rows[1].reason, "replied");
        compare(findChild(b, "sidebar-waits-count").text, "2");
        tryVerify(() => row(b, 1) !== null, 1000);
        // "ответить в треде", "просят твоё ревью"; the age on the title's
        // line (dogfood 2026-10-22).
        const sub = findChild(row(b, 1), "sidebar-wait-sub").text;
        compare(sub, I18n.t("waits.thread"));
        verify(findChild(row(b, 1), "sidebar-wait-age").text.length > 0, "how long it has waited");
        compare(findChild(row(b, 0), "sidebar-wait-title").text, "Split ledger export");
        verify(findChild(row(b, 0), "sidebar-wait-sub").text.indexOf(I18n.t("waits.reviewYou")) === 0);
        compare(findChild(row(b, 0), "sidebar-wait-ref").font.family, Theme.fontMono, "the number is mono");
        verify(!findChild(row(b, 0), "sidebar-wait-ci").visible, "no ✕ without a failed CI");
    }

    function test_keys_walk_rows_and_return_opens_the_thread() {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2)));
        verify(AppController.setMrThreadsForTest("gitlab", tc.url479, "Split ledger export", "ira", ["fin"], "fin", "[]"));
        const b = make();
        tryVerify(() => row(b, 1) !== null, 1000);
        const first = findChild(row(b, 0), "sidebar-wait-open");
        first.forceActiveFocus();
        tryVerify(() => b.detailIndex === 0, 1000, "the focused row shows its card at once");
        keyClick(Qt.Key_Down);
        const second = findChild(row(b, 1), "sidebar-wait-open");
        verify(second.activeFocus, "Down moves to the next row");
        keyClick(Qt.Key_Return);
        compare(AppController.lastOpenedThreadUrlForTest(), tc.url482 + "#note_103", "Return opens the note that waits");
        keyClick(Qt.Key_Up);
        verify(first.activeFocus, "Up moves back");
        keyClick(Qt.Key_Space);
        compare(AppController.lastOpenedThreadUrlForTest(), tc.url479, "a review opens the request");
        // No card here for it: Shift+Return asks for none.
        const spy = createTemporaryQmlObject('import QtTest; SignalSpy { signalName: "taskRequested" }', host);
        spy.target = b;
        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        compare(spy.count, 0);
        // t opens the waiting thread, whatever the row would open on Enter.
        AppController.openMrThread(tc.url479);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_T);
        compare(AppController.lastOpenedThreadUrlForTest(), tc.url482 + "#note_103", "t opens the thread");
    }

    function test_i_and_the_middle_button_set_the_thread_aside() {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2)));
        verify(AppController.setMrThreadsForTest("gitlab", tc.url479, "Split ledger export", "ira", ["fin"], "fin", "[]"));
        const b = make();
        tryVerify(() => row(b, 1) !== null, 1000);
        compare(b.rows[1].threads.length, 1, "the row knows the thread that waits");
        // A review has no thread to set aside: i does nothing there.
        findChild(row(b, 0), "sidebar-wait-open").forceActiveFocus();
        keyClick(Qt.Key_I);
        compare(b.rows.length, 2);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_I);
        tryVerify(() => b.rows.length === 1, 1000, "i set the thread aside");
        compare(b.rows[0].ref, "!479");
        AppController.undo();
        tryVerify(() => b.rows.length === 2, 1000, "undo brought it back");
        tryVerify(() => row(b, 1) !== null, 1000);
        mouseClick(findChild(row(b, 1), "sidebar-wait-open"), 10, 5, Qt.MiddleButton);
        tryVerify(() => b.rows.length === 1, 1000, "the middle button set it aside");
    }

    function test_three_rows_then_more() {
        const urls = [];
        for (let i = 0; i < 5; i++) {
            const u = "https://gitlab.company.ru/tel/app/-/merge_requests/" + (600 + i);
            urls.push(u);
            verify(AppController.setMrThreadsForTest("gitlab", u, "Request " + i, "ira", ["fin"], "fin", "[]"));
        }
        const b = make();
        tryVerify(() => b.rows.length === 5, 1000);
        compare(findChild(b, "sidebar-waits-count").text, "5");
        compare(b.shownRows.length, 3, "three rows first");
        const more = findChild(b, "sidebar-waits-more");
        verify(more.visible);
        compare(more.text, I18n.t("waits.more").arg(2));
        findChild(b, "sidebar-waits-more-btn").activated();
        compare(b.shownRows.length, 5, "show takes them all");
    }

    function test_a_click_opens_the_thread() {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(1)));
        const b = make();
        tryVerify(() => row(b, 0) !== null, 1000);
        mouseClick(findChild(row(b, 0), "sidebar-wait-open"));
        compare(AppController.lastOpenedThreadUrlForTest(), tc.url482 + "#note_103");
    }

    function test_idiot_closed_resolved_and_no_login_leave_nothing() {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2)));
        const b = make();
        tryVerify(() => b.rows.length === 1, 1000);
        // Merged while the row was up.
        AppController.setMrOpenForTest(tc.url482, false);
        tryVerify(() => b.rows.length === 0, 1000, "a closed request still waits");
        verify(!b.visible);
        // Open again, then the thread is resolved on the forge.
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2)));
        tryVerify(() => b.rows.length === 1, 1000);
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2, true)));
        // Resolved after a sync: struck through a moment, then gone.
        tryVerify(() => b.liveCount === 0, 1000, "a resolved thread still waits");
        AppController.setWaitsClockForTest(new Date(Date.now() + 5000));
        tryVerify(() => b.rows.length === 0, 1000, "the resolved row did not leave");
        // No login known: nothing waits on anyone.
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", ["fin"], "", tc.answered(2)));
        compare(AppController.mrWaitRows().length, 0);
        // An answer that is not threads at all.
        verify(!AppController.setMrThreadsForTest("gitlab", tc.url479, "x", "ira", ["fin"], "fin", "<html>502</html>"));
        compare(AppController.mrWaitRows().length, 0, "GitLab without its threads says nothing about a review");
    }

    function test_setting_off_hides_the_block() {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2)));
        compare(AppController.mrWaitRows().length, 1);
        const saved = AppController.appSettingsJson;
        let s = {};
        try { s = JSON.parse(saved || "{}"); } catch (e) { s = {}; }
        s.git = s.git || {};
        s.git.mrThreads = false;
        AppController.appSettingsJson = JSON.stringify(s);
        compare(AppController.mrWaitRows().length, 0, "off: nothing shown");
        AppController.appSettingsJson = saved;
    }

    // The list cleans itself (dogfood 2026-10-22, Hints.dc.html): my answer
    // read by a sync strikes the row through, it leaves, and "ушло 1
    // сегодня · показать" lists it with "вернуть"; "синх HH:MM" on the right.
    function iReplied(h) {
        return JSON.stringify([{ id: "d1", individual_note: false, notes: [
            { id: 101, type: "DiffNote", body: "А если 429?", author: { username: "ira" }, created_at: tc.hoursAgo(h + 2),
              system: false, resolvable: true, resolved: false },
            { id: 102, type: "DiffNote", body: "Проверю", author: { username: "fin" }, created_at: tc.hoursAgo(h + 1),
              system: false, resolvable: true, resolved: false },
            { id: 103, type: "DiffNote", body: "Нужен Retry-After", author: { username: "ira" }, created_at: tc.hoursAgo(h),
              system: false, resolvable: true, resolved: false },
            { id: 104, type: "DiffNote", body: "Добавил", author: { username: "fin" }, created_at: tc.hoursAgo(0.5),
              system: false, resolvable: true, resolved: false }] }]);
    }

    function test_a_resolved_row_fades_and_goes_to_gone_today() {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2)));
        verify(AppController.setMrThreadsForTest("gitlab", tc.url479, "Split ledger export", "ira", ["fin"], "fin", "[]"));
        const b = make();
        tryVerify(() => b.rows.length === 2, 1000);
        // The forge did not answer: the row stays.
        verify(!AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", "<html>502</html>"));
        compare(b.rows.length, 2, "nothing leaves without a sync");
        compare(b.gone.length, 0);
        // My answer is read: struck through, dimmed, "ты ответил в треде".
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.iReplied(2)));
        tryVerify(() => b.rows.some(r => r.leaving), 1000, "no row fading out");
        compare(findChild(b, "sidebar-waits-count").text, "1", "a fading row is not counted");
        const at = b.rows.findIndex(r => r.leaving);
        tryVerify(() => row(b, at) !== null, 1000);
        verify(findChild(row(b, at), "sidebar-wait-title").font.strikeout, "the leaving row is struck through");
        compare(findChild(row(b, at), "sidebar-wait-sub").text, I18n.t("waits.why.replied"));
        tryVerify(() => row(b, at).opacity < 0.5, 1000, "the leaving row is not dimmed");
        // ~2 s later it is gone; the footer says so.
        tryVerify(() => b.rows.length === 1, 4000, "the resolved row did not leave");
        const gone = findChild(b, "sidebar-waits-gone");
        verify(gone.visible);
        compare(gone.text, I18n.t("waits.gone").arg(1) + " · " + I18n.t("waits.gone.show"));
        const synced = findChild(b, "sidebar-waits-synced");
        verify(synced.visible);
        compare(synced.text, I18n.t("waits.synced").arg(I18n.fmtTime(AppController.mrWaitsSyncedAt())));
        // "показать": dimmed, with "вернуть".
        findChild(b, "sidebar-waits-gone-toggle").activated();
        tryVerify(() => findChild(b, "sidebar-waits-gone-0") !== null, 1000);
        compare(findChild(findChild(b, "sidebar-waits-gone-0"), "sidebar-waits-gone-title").text, "Retry webhooks on 5xx");
        findChild(findChild(b, "sidebar-waits-gone-0"), "sidebar-waits-gone-back-btn").activated();
        tryVerify(() => b.rows.length === 2, 1000, "вернуть did not bring it back");
        verify(b.rows.some(r => r.returned), "the row came back for today");
        compare(b.gone.length, 0);
        verify(!gone.visible);
    }

    // "Отложить до завтра": the menu's item with the scheduling key; the
    // key itself on the focused row; Undo brings it back.
    function test_put_off_until_tomorrow_from_the_menu_and_the_key() {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url482, "Retry webhooks on 5xx", "fin", [], "fin", tc.answered(2)));
        verify(AppController.setMrThreadsForTest("gitlab", tc.url479, "Split ledger export", "ira", ["fin"], "fin", "[]"));
        const b = make();
        tryVerify(() => row(b, 1) !== null, 1000);
        mouseClick(findChild(row(b, 0), "sidebar-wait-open"), 20, 10, Qt.RightButton);
        const menu = findChild(b, "sidebar-wait-menu");
        tryVerify(() => menu.opened, 1000, "no menu on a right click");
        const snooze = findChild(menu, "sidebar-wait-menu-snooze");
        verify(snooze.visible);
        compare(snooze.text, I18n.t("waits.menu.snooze"));
        compare(snooze.keyText, AppController.shortcutText("task.schedule"));
        snooze.triggered();
        menu.close();
        tryVerify(() => b.rows.length === 1, 1000, "the menu did not put it off");
        compare(b.rows[0].ref, "!482");
        AppController.undo();
        tryVerify(() => b.rows.length === 2, 1000, "undo did not bring it back");
        // The key: s in Vim, Ctrl+Shift+S in Classic.
        tryVerify(() => row(b, 1) !== null, 1000);
        findChild(row(b, 1), "sidebar-wait-open").forceActiveFocus();
        if (AppController.keymapScheme === "vim") keyClick(Qt.Key_S);
        else keyClick(Qt.Key_S, Qt.ControlModifier | Qt.ShiftModifier);
        tryVerify(() => b.rows.length === 1, 1000, "the key did not put it off");
        verify(AppController.mrWaitSnoozed(b.rows[0].url === tc.url482 ? tc.url479 : tc.url482));
    }
}
