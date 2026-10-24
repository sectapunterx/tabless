// "Треды без ответа" in a merge request's document and a thread set aside
// (dogfood 2026-10-22 item 3): i, the middle button and the menu's
// "Игнорировать тред" take it out of every count; "1 в игноре · показать"
// lists it dimmed with "вернуть"; Undo brings it back; a new word in it
// brings it back by itself. Nothing is sent to the forge.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TaskThreadsPanel"
    when: windowShown
    visible: true
    width: 520
    height: 700

    Item { id: host; anchors.fill: parent }

    readonly property string url: "https://gitlab.company.ru/tel/app/-/merge_requests/482"

    function init() { AppController.clearMrThreadsForTest(); }
    function cleanup() { AppController.clearMrThreadsForTest(); }

    function hoursAgo(h) { return new Date(Date.now() - h * 3600 * 1000).toISOString(); }
    // Two threads on my request: Ira answered me in a diff thread; Max
    // started a general one. `more`: Ira wrote again since.
    function threads(more) {
        const ira = [
            { id: 101, type: "DiffNote", body: "А если 429?", author: { username: "ira" }, created_at: tc.hoursAgo(6),
              system: false, resolvable: true, resolved: false, position: { new_path: "src/retry.cpp", new_line: 88 } },
            { id: 102, type: "DiffNote", body: "Проверю", author: { username: "fin" }, created_at: tc.hoursAgo(5),
              system: false, resolvable: true, resolved: false },
            { id: 103, type: "DiffNote", body: "Нужен Retry-After", author: { username: "ira" }, created_at: tc.hoursAgo(4),
              system: false, resolvable: true, resolved: false }];
        if (more) ira.push({ id: 104, type: "DiffNote", body: "Ну что?", author: { username: "ira" }, created_at: tc.hoursAgo(1),
                             system: false, resolvable: true, resolved: false });
        return JSON.stringify([
            { id: "d1", individual_note: false, notes: ira },
            { id: "d2", individual_note: false, notes: [
                { id: 201, type: "DiscussionNote", body: "Please consider merging develop", author: { username: "max" },
                  created_at: tc.hoursAgo(3), system: false, resolvable: true, resolved: false }] }]);
    }
    function setUp(more) {
        verify(AppController.setMrThreadsForTest("gitlab", tc.url, "Retry webhooks on 5xx", "fin", [], "fin", tc.threads(more)));
        const id = AppController.addMrTaskForTest(tc.url, "Retry webhooks on 5xx");
        const p = createTemporaryQmlObject('import TodoCpp; TaskThreadsPanel { width: 480 }', host);
        verify(p !== null);
        p.taskId = id;
        tryVerify(() => p.threads.length === 2, 1000, "both threads wait on me");
        return { panel: p, task: id };
    }
    function item(p, i) { return findChild(p, "task-thread-" + i); }
    function open(p, i) { return findChild(item(p, i), "task-thread-open"); }
    // A rebuilt list is laid out a moment later: click only then.
    function ready(p, i) {
        tryVerify(() => item(p, i) !== null && open(p, i) !== null && open(p, i).height > 0 && open(p, i).width > 0, 1000,
                  "thread " + i + " not laid out");
        return open(p, i);
    }

    function test_i_sets_the_focused_thread_aside_and_undo_brings_it_back() {
        const s = setUp(false);
        const p = s.panel;
        compare(AppController.mrThreadsBrief(s.task).count, 2);
        verify(findChild(p, "task-threads-title").text.indexOf(" · 2") > 0, "the count is in the head");
        verify(!findChild(p, "task-threads-ignored").visible, "nothing aside yet");
        tryVerify(() => item(p, 1) !== null, 1000);
        open(p, 0).forceActiveFocus();
        keyClick(Qt.Key_I);
        tryVerify(() => p.threads.length === 1, 1000, "i did not set the thread aside");
        compare(AppController.mrThreadsBrief(s.task).count, 1, "the card still counts it");
        compare(p.ignoredThreads.length, 1);
        const toggle = findChild(p, "task-threads-ignored");
        verify(toggle.visible);
        compare(toggle.text, I18n.t("threads.ignoredCount").arg(1) + " · " + I18n.t("threads.show"));
        tryVerify(() => open(p, 0) !== null && open(p, 0).activeFocus, 1000, "the keyboard left the list");
        AppController.undo();
        tryVerify(() => p.threads.length === 2, 1000, "undo did not bring it back");
        compare(p.ignoredThreads.length, 0);
    }

    function test_middle_button_and_the_menu() {
        const s = setUp(false);
        const p = s.panel;
        tryVerify(() => item(p, 1) !== null, 1000);
        const opened = AppController.lastOpenedThreadUrlForTest();
        mouseClick(ready(p, 1), 20, 10, Qt.MiddleButton);
        tryVerify(() => p.threads.length === 1, 1000, "the middle button did not set it aside");
        compare(AppController.lastOpenedThreadUrlForTest(), opened, "the middle button opened the thread");

        // The menu: open, copy, then ignore.
        mouseClick(ready(p, 0), 20, 10, Qt.RightButton);
        const menu = findChild(p, "task-thread-menu");
        tryVerify(() => menu.opened, 1000, "no menu on a right click");
        compare(findChild(menu, "task-thread-menu-open").text, I18n.t("threads.menu.open").arg("GitLab"));
        compare(findChild(menu, "task-thread-menu-copy").text, I18n.t("threads.menu.copy"));
        const ignore = findChild(menu, "task-thread-menu-ignore");
        compare(ignore.text, I18n.t("threads.menu.ignore"));
        compare(ignore.keyText, I18n.t("threads.ignoreKeys"));
        ignore.triggered();
        menu.close();
        tryVerify(() => p.threads.length === 0, 1000, "the menu did not set it aside");
        verify(p.visible, "the block stays while something is aside");
        compare(AppController.mrWaitRows().length, 0, "nothing waits in the sidebar");

        // "показать": both dimmed, each with "вернуть".
        findChild(p, "task-threads-ignored-toggle").activated();
        tryVerify(() => item(p, 1) !== null, 1000);
        verify(findChild(item(p, 0), "task-thread-ignored-line").visible);
        findChild(item(p, 0), "task-thread-unignore-btn").activated();
        tryVerify(() => p.threads.length === 1, 1000, "вернуть did not bring it back");
        // The menu of a thread aside says the way back.
        mouseClick(ready(p, 1), 20, 10, Qt.RightButton);
        tryVerify(() => menu.opened, 1000);
        compare(findChild(menu, "task-thread-menu-ignore").text, I18n.t("threads.menu.unignore"));
        menu.close();
    }

    function test_a_new_word_brings_the_thread_back() {
        const s = setUp(false);
        const p = s.panel;
        tryVerify(() => item(p, 0) !== null, 1000);
        verify(AppController.ignoreMrThread(tc.url, "d1"));
        tryVerify(() => p.threads.length === 1, 1000);
        // The same thread read again with nothing new: still aside.
        verify(AppController.setMrThreadsForTest("gitlab", tc.url, "Retry webhooks on 5xx", "fin", [], "fin", tc.threads(false)));
        compare(p.threads.length, 1);
        // Ira writes again.
        verify(AppController.setMrThreadsForTest("gitlab", tc.url, "Retry webhooks on 5xx", "fin", [], "fin", tc.threads(true)));
        tryVerify(() => p.threads.length === 2, 1000, "a new word did not bring it back");
        compare(p.ignoredThreads.length, 0);
    }

    function test_idiot_keys_on_a_thread_do_not_open_it_or_type() {
        const s = setUp(false);
        const p = s.panel;
        tryVerify(() => item(p, 1) !== null, 1000);
        open(p, 0).forceActiveFocus();
        keyClick(Qt.Key_J);
        verify(open(p, 1).activeFocus, "j walks the threads");
        keyClick(Qt.Key_Return);
        verify(AppController.lastOpenedThreadUrlForTest().indexOf(tc.url) === 0, "Return opens the thread");
        // i on the last thread: the keyboard goes to the one left, not out
        // of the list.
        keyClick(Qt.Key_I);
        tryVerify(() => p.threads.length === 1, 1000);
        tryVerify(() => open(p, 0) !== null && open(p, 0).activeFocus, 1000, "the keyboard left the list");
        // The keys are said in the thread's tip.
        compare(open(p, 0).tip, I18n.t("threads.keys").arg("GitLab"));
    }

    // «C · тише» (Redo-Sheet-Quiet): the threads that wait on me first, each
    // with the warning's dot; the ones I wait on folded to one quiet line,
    // "○ Ждёшь ответа в N тредах · последний — … назад", that lists them on a
    // click or Enter; j / k walk through the line too.
    function test_waiting_on_me_comes_first_theirs_folded() {
        const mine = JSON.stringify([
            { id: "d1", individual_note: false, notes: [
                { id: 301, type: "DiffNote", body: "Why 3?", author: { username: "ira" }, created_at: tc.hoursAgo(6),
                  system: false, resolvable: true, resolved: false, position: { new_path: "src/a.cpp", new_line: 3 } },
                { id: 302, type: "DiffNote", body: "Fixed", author: { username: "fin" }, created_at: tc.hoursAgo(5),
                  system: false, resolvable: true, resolved: false }] },
            { id: "d2", individual_note: false, notes: [
                { id: 401, type: "DiscussionNote", body: "#remind @fin", author: { username: "max" },
                  created_at: tc.hoursAgo(3), system: false, resolvable: true, resolved: false }] }]);
        verify(AppController.setMrThreadsForTest("gitlab", tc.url, "Retry webhooks on 5xx", "fin", [], "fin", mine));
        const id = AppController.addMrTaskForTest(tc.url, "Retry webhooks on 5xx");
        const p = createTemporaryQmlObject('import TodoCpp; TaskThreadsPanel { width: 380 }', host);
        p.taskId = id;
        tryVerify(() => p.threads.length === 2, 1000);
        compare(p.onMeCount, 1);
        compare(p.onThemCount, 1);
        compare(p.shown.length, 1, "the ones I wait on are not folded");
        compare(p.shown[0].wait, "me", "waiting on me is not first");
        tryVerify(() => item(p, 0) !== null, 1000);
        compare(String(findChild(item(p, 0), "task-thread-dot").color), String(Theme.warning));
        const head = findChild(p, "task-threads-group-them");
        verify(head && head.visible, "no «Ждёшь ответа» line");
        compare(findChild(head, "task-threads-group-title").text.indexOf(I18n.count(1, "threads.waitIn")), 0);
        verify(findChild(head, "task-threads-group-latest").text.length > 0, "no «последний — … назад»");
        tryVerify(() => head.y > item(p, 0).y, 1000, "theirs above mine");
        // j from mine onto the line, Enter lists theirs, j onto the first.
        open(p, 0).forceActiveFocus();
        keyClick(Qt.Key_J);
        const toggle = findChild(p, "task-threads-group-them-toggle");
        verify(toggle.activeFocus, "j did not reach the folded line");
        keyClick(Qt.Key_Return);
        verify(p.themOpen);
        tryVerify(() => item(p, 1) !== null, 1000, "theirs did not open");
        compare(p.shown[1].wait === "me", false);
        tryVerify(() => item(p, 1).y > head.y, 1000, "theirs not under their line");
        keyClick(Qt.Key_J);
        verify(open(p, 1).activeFocus, "j did not go on into theirs");
        keyClick(Qt.Key_K);
        verify(toggle.activeFocus, "k did not come back to the line");
        // A click folds them again.
        mouseClick(toggle);
        verify(!p.themOpen);
        compare(p.shown.length, 1);
    }
}
