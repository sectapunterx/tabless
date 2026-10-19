// Who owns what the task's PR changes (CODEOWNERS, dogfood 2026-10-19):
// hidden while nothing is known, each need with its owners and their state,
// "Кого пингануть" with the handles, and the sidebar rows that come from the
// same report. Read only — nobody is asked for a review.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TaskOwnersPanel"
    when: windowShown
    visible: true
    width: 420
    height: 600

    Item { id: host; anchors.fill: parent }

    readonly property string repo: "/qmltest-owners-repo"
    property string taskId: ""
    property string branch: ""

    function init() {
        const d = AppController.newTaskDraft("todo");
        d.title = "Owners probe";
        d.branch = "owners-probe-" + Date.now();
        verify(AppController.saveTask(d));
        tc.taskId = d.id;
        tc.branch = d.branch;
    }
    function cleanup() {
        AppController.setCodeOwnersReportForTest(tc.repo, tc.branch, ({}));
        AppController.deleteTask(tc.taskId);
    }

    function report(toPing) {
        return {
            found: true,
            location: ".github/CODEOWNERS",
            flavor: "github",
            prNumber: 41,
            prUrl: "https://github.com/o/n/pull/41",
            prTitle: "Owners probe",
            toPing: toPing,
            requirements: [{
                section: "", optional: false, approvals: 1, approvedCount: 0, state: "pending",
                files: ["src/a.cpp", "src/b.cpp"],
                owners: [{ handle: "@alice", state: "approved" }, { handle: "@org/core", state: "missing" }]
            }]
        };
    }

    function test_hidden_until_a_report_then_owners_and_who_to_ping() {
        const p = createTemporaryQmlObject('import TodoCpp; TaskOwnersPanel { width: 380 }', host);
        verify(p !== null);
        p.taskId = tc.taskId;
        verify(!p.visible, "nothing known, no panel");
        AppController.setCodeOwnersReportForTest(tc.repo, tc.branch, report(["@org/core"]));
        tryVerify(() => p.visible, 1000, "the report did not reach the panel");
        compare(p.requirements.length, 1);
        tryVerify(() => findChild(p, "task-owners-owner") !== null, 1000);
        const list = findChild(p, "task-owners-ping-list");
        verify(list !== null);
        compare(list.text, "@org/core");
        verify(findChild(p, "task-owners-ping").visible);
        verify(!findChild(p, "task-owners-allset").visible);

        // Everyone approved or asked: nothing left to ping.
        AppController.setCodeOwnersReportForTest(tc.repo, tc.branch, report([]));
        tryVerify(() => findChild(p, "task-owners-allset").visible, 1000);
        verify(!findChild(p, "task-owners-ping").visible);
    }

    function test_no_codeowners_file_says_so() {
        const p = createTemporaryQmlObject('import TodoCpp; TaskOwnersPanel { width: 380 }', host);
        p.taskId = tc.taskId;
        AppController.setCodeOwnersReportForTest(tc.repo, tc.branch,
            { found: false, location: "", requirements: [], toPing: [], prNumber: 41, prUrl: "https://github.com/o/n/pull/41" });
        tryVerify(() => p.visible, 1000);
        const empty = findChild(p, "task-owners-empty");
        verify(empty.visible);
        compare(empty.text, I18n.t("owners.notFound"));
    }

    function test_sidebar_rows_and_wrote_hides_one() {
        const b = createTemporaryQmlObject('import TodoCpp; SidebarPings { width: 260 }', host);
        verify(b !== null);
        AppController.setCodeOwnersReportForTest(tc.repo, tc.branch, report(["@org/core", "@bob"]));
        tryVerify(() => b.owners.filter(o => o.prNumber === 41).length === 2, 1000, "owner rows missing");
        verify(b.hasAny);
        if (b.open) tryVerify(() => findChild(b, "sidebar-owner-ping") !== null, 1000, "no row drawn");
        const row = b.owners.find(o => o.handle === "@bob" && o.prNumber === 41);
        AppController.dismissCodeOwnerPing(row.key);
        tryVerify(() => b.owners.filter(o => o.prNumber === 41).length === 1, 1000, "wrote did not hide the row");
        compare(b.owners.filter(o => o.prNumber === 41)[0].handle, "@org/core");
    }
}
