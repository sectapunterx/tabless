// CI of a merge / pull request in the task document (APP-318): hidden for a
// task with no pipeline; the jobs and the log arrive by signal and are kept
// only while the panel shows that task.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TaskCiPanel"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Item { id: host; anchors.fill: parent }

    function test_hidden_without_a_pipeline_and_fills_from_signals() {
        const p = createTemporaryQmlObject('import TodoCpp; TaskCiPanel { width: 360 }', host);
        verify(p !== null);
        p.taskId = "no-such-task";
        verify(!p.visible, "no pipeline, no panel");
        // An answer for another task is not this panel's.
        AppController.ciJobsLoaded("other", [{ id: "1", name: "x", hasLog: true }], "");
        compare(p.jobs.length, 0);
        AppController.ciJobsLoaded("no-such-task", [{ id: "55", name: "test", stage: "test", seconds: 61, url: "https://g/j/55", hasLog: true }], "");
        compare(p.jobs.length, 1);
        p.logJob = "55";
        AppController.ciLogLoaded("no-such-task", "55", "ok\nFAILED: 1 test", "");
        compare(p.logText, "ok\nFAILED: 1 test");
        verify(AppController.isCiErrorLine("FAILED: 1 test"));
        // Another task: nothing carried over.
        p.taskId = "another";
        compare(p.jobs.length, 0);
        compare(p.logText, "");
    }

    // Dogfood 2026-10-22: a check read by `gh pr checks` (a PR only the git
    // watcher reads) has no log here: its row links to its page.
    function test_a_check_with_no_log_links_to_its_page() {
        const p = createTemporaryQmlObject('import TodoCpp; TaskCiPanel { width: 360 }', host);
        p.taskId = "no-such-task";
        AppController.ciJobsLoaded("no-such-task", [{ id: "https://x/job/12", name: "test (windows)", seconds: 252,
                                                      url: "https://x/job/12", hasLog: false, state: "failure" }], "");
        compare(p.jobs.length, 1);
        p.visible = true;   // no pipeline behind the made-up task: shown by hand
        const link = findChild(p, "task-ci-job-link");
        verify(link !== null);
        verify(link.visible, "no link to the check's page");
        compare(link.url, "https://x/job/12");
    }

    // The card's CI popover stays in the window (dogfood 2026-10-22): on the
    // last column it ran past the right edge and over the card below.
    function popRect(pop, anchor) {
        const at = anchor.mapToItem(null, pop.x, pop.y);
        return Qt.rect(at.x, at.y, pop.width, pop.height);
    }
    function overlaps(a, b) {
        return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
    }
    function inside(r) {
        return r.x >= 0 && r.y >= 0 && r.x + r.width <= tc.width && r.y + r.height <= tc.height;
    }
    function test_the_popover_stays_in_the_window() {
        const pop = createTemporaryQmlObject('import TodoCpp; CiPopover {}', host);
        const card = createTemporaryQmlObject('import QtQuick; Rectangle { x: 690; y: 100; width: 200; height: 80 }', host);
        const below = createTemporaryQmlObject('import QtQuick; Rectangle { x: 690; y: 192; width: 200; height: 80 }', host);
        // The last column: beside the card, not past the edge, not over the next card.
        pop.openAt(card, "no-such-task");
        tryVerify(() => pop.opened, 1000);
        let r = popRect(pop, card);
        verify(inside(r), "past the window: " + JSON.stringify(r));
        verify(!overlaps(r, Qt.rect(below.x, below.y, below.width, below.height)), "over the card below");
        verify(!overlaps(r, Qt.rect(card.x, card.y, card.width, card.height)), "over its own card");
        pop.close();
        tryVerify(() => !pop.visible, 1000);
        // The last card at the bottom: still in the window.
        card.y = tc.height - 90;
        pop.openAt(card, "no-such-task");
        tryVerify(() => pop.opened, 1000);
        r = popRect(pop, card);
        verify(inside(r), "past the bottom: " + JSON.stringify(r));
        pop.close();
        tryVerify(() => !pop.visible, 1000);
        // A card with room: under it, as before.
        card.x = 40; card.y = 100;
        pop.openAt(card, "no-such-task");
        tryVerify(() => pop.opened, 1000);
        compare(pop.y, card.height + Theme.spXs);
        compare(pop.x, Theme.spXl);
        // At the bottom of a column with room on the right: above the card.
        pop.close();
        tryVerify(() => !pop.visible, 1000);
        card.y = tc.height - 90;
        pop.openAt(card, "no-such-task");
        tryVerify(() => pop.opened, 1000);
        r = popRect(pop, card);
        verify(inside(r), "past the bottom: " + JSON.stringify(r));
        verify(!overlaps(r, Qt.rect(card.x, card.y, card.width, card.height)), "over its own card");
        pop.close();
    }
}
