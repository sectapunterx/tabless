// The tracker's facts on a ticket's document (dogfood 2026-10-19): Jira's
// estimate read only beside mine, and the comments block, which stays out of
// the way while there is nothing read (no tracker connected here).
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TaskTrackerFacts"
    when: windowShown
    visible: true
    width: 1400
    height: 860

    Item { id: host; width: 1400; height: 860 }
    property string importedProfile: ""
    property string homeProfile: ""

    function cleanup() {
        if (tc.importedProfile.length > 0) {
            AppController.activeProfileId = tc.homeProfile;
            AppController.deleteProfile(tc.importedProfile);
            tc.importedProfile = "";
        }
        AppController.clearPendingUndo();
    }
    function importTicket(meta, estimateMinutes) {
        tc.homeProfile = AppController.activeProfileId;
        AppController.importProfileFromJson(JSON.stringify({ profile: {
            id: "facts-" + Date.now(), name: "Tracker facts", color: "#5cc2dd",
            statuses: [{ id: "todo", name: "To Do", color: "#888888" }],
            tasks: [{ id: "TF-1", title: "facts ticket", status: "todo", priority: "P2",
                      estimateMinutes: estimateMinutes,
                      externalId: "PLAT-301", externalProvider: "jira",
                      externalUrl: "https://jira.example.invalid/browse/PLAT-301",
                      externalMeta: meta }] } }), true);
        tc.importedProfile = AppController.activeProfileId;
        verify(tc.importedProfile !== tc.homeProfile, "the import did not open its profile");
        const m = AppController.tasks;
        for (let i = 0; i < m.rowCount(); i++)
            if (m.data(m.index(i, 0), m.roleOf("title")) === "facts ticket") return m.data(m.index(i, 0), m.roleOf("id"));
        fail("the ticket was not imported");
        return "";
    }
    function make() {
        const doc = createTemporaryQmlObject('import TodoCpp; TaskDocument { anchors.fill: parent }', host);
        verify(doc !== null);
        return doc;
    }

    function test_the_trackers_estimate_reads_beside_mine() {
        const id = importTicket({ status: "To Do", column: "todo",
                                  remoteDetails: { estimate: { original: 28800, remaining: 7200, spent: 3600, points: 3 } } }, 45);
        const doc = make();
        doc.open(id);
        const est = findChild(doc, "task-doc-tracker-estimate");
        verify(est !== null);
        tryVerify(() => est.visible, 2000, "the tracker's estimate is not shown");
        verify(est.text.indexOf("3 SP") >= 0, est.text);
        verify(est.text.indexOf("Jira") >= 0, est.text);
        // Mine stands as it is.
        verify(findChild(doc, "task-doc-estimate").value.length > 0, "my estimate went missing");
        compare(AppController.taskById(id).estimateMinutes, 45);
        doc.close();
    }

    function test_no_estimate_in_the_tracker_shows_nothing() {
        const id = importTicket({ status: "To Do", column: "todo" }, 0);
        const doc = make();
        doc.open(id);
        verify(!findChild(doc, "task-doc-tracker-estimate").visible);
        doc.close();
    }

    // No tracker connected and nothing read: the block keeps out of the way
    // rather than an error on every ticket.
    function test_comments_stay_hidden_with_nothing_read() {
        const id = importTicket({ status: "To Do", column: "todo" }, 0);
        const doc = make();
        doc.open(id);
        const comments = findChild(doc, "task-doc-comments");
        verify(comments !== null, "no comments block in the document");
        wait(50);
        verify(!comments.visible, "an empty, unreadable comments block is on screen");
        doc.close();
    }
}
