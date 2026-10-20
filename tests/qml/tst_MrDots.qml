// The two dots of a merge / pull request card (dogfood 2026-10-20 item 9):
// at rest only the dots, CI on the left and "can it be merged" on the
// right; under the cursor the request in quiet words. Hand-made task maps,
// no model and no forge.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "MrDots"
    when: windowShown
    visible: true
    width: 420
    height: 500

    Item { id: host; anchors.fill: parent }

    Component {
        id: cardComp
        TaskCard { width: 320 }
    }

    function init() { Style.setFlag("cardDensity", "detailed"); }
    function cleanup() { Style.apply("bold"); }

    function mr(details, extra) {
        return Object.assign({
            id: "MRD-1", title: "Retry webhooks", priority: "P2", status: "review", labels: [],
            ticket: ({ provider: "gitlab", key: "!481", url: "https://gitlab.invalid/a/-/merge_requests/481",
                       details: Object.assign({ kind: "mr", state: "opened" }, details) })
        }, extra || {});
    }

    function make(task, props) {
        const o = cardComp.createObject(host, Object.assign({ task: task }, props || {}));
        verify(o !== null);
        return o;
    }

    function texts(item, out) {
        if (!item) return out;
        if (item.text !== undefined && typeof item.text === "string" && item.visible && item.text.length > 0) out.push(item.text);
        const kids = item.children || [];
        for (let i = 0; i < kids.length; i++) texts(kids[i], out);
        return out;
    }

    function dots(card) {
        return { box: findChild(card, "mr-dots"), ci: findChild(card, "mr-dot-ci"), merge: findChild(card, "mr-dot-merge") };
    }

    function test_at_rest_only_two_dots() {
        mouseMove(host, host.width - 1, host.height - 1);
        const card = make(mr({ pipeline: "failed", mergeStatus: "not_approved", reviewers: ["ira", "max"] }));
        const d = dots(card);
        verify(d.box.visible, "no dots on an open MR");
        verify(d.ci.visible && d.merge.visible);
        verify(Qt.colorEqual(d.ci.ink, Theme.danger), "failed CI is not the danger dot");
        compare(d.ci.shape, "fill");
        compare(d.merge.shape, "ring", "waiting for approvals is a hollow ring");
        compare(d.ci.Accessible.name, I18n.t("mr.ci.failed"));
        compare(d.merge.Accessible.name, I18n.t("mr.merge.waiting"));
        wait(Theme.durPop + 50);
        // Nothing else of the request at rest: no "PR #", no CI chip.
        const all = texts(card, []).join("\n");
        // The key line is the card's own ("!481"); the request adds nothing.
        verify(all.indexOf("PR") < 0, "a PR line shows at rest:\n" + all);
        verify(all.indexOf("CI") < 0, "a CI word shows at rest:\n" + all);
        verify(all.indexOf("ira") < 0, "a name shows at rest:\n" + all);
        verify(!findChild(card, "tc-pr").visible);
        verify(!findChild(card, "tc-ci").visible);
        card.destroy();
    }

    function test_each_state_has_its_shape() {
        const cases = [
            { d: { pipeline: "running", mergeStatus: "not_approved" }, ci: "half", ciInk: Theme.textDim, merge: "ring", mergeInk: Theme.borderStrong },
            { d: { pipeline: "success", mergeStatus: "mergeable" }, ci: "fill", ciInk: Theme.success, merge: "fill", mergeInk: Theme.success },
            { d: { pipeline: "success", conflicts: true }, ci: "fill", ciInk: Theme.success, merge: "ring", mergeInk: Theme.danger },
            { d: { pipeline: "success", draft: true, mergeStatus: "mergeable" }, ci: "fill", ciInk: Theme.success, merge: "ring", mergeInk: Theme.borderStrong },
            { d: { pipeline: "running", mergeStatus: "mergeable" }, ci: "half", ciInk: Theme.textDim, merge: "ring", mergeInk: Theme.borderStrong }
        ];
        for (let i = 0; i < cases.length; i++) {
            const c = cases[i];
            const card = make(mr(c.d));
            const d = dots(card);
            compare(d.ci.shape, c.ci, "case " + i);
            verify(Qt.colorEqual(d.ci.ink, c.ciInk), "case " + i + " ci ink " + d.ci.ink);
            compare(d.merge.shape, c.merge, "case " + i);
            verify(Qt.colorEqual(d.merge.ink, c.mergeInk), "case " + i + " merge ink " + d.merge.ink);
            card.destroy();
        }
    }

    function test_no_ci_no_left_dot_and_no_review_facts_no_right_dot() {
        const card = make(mr({}));
        const d = dots(card);
        verify(!d.box.visible, "dots with nothing known");
        card.destroy();
        const ciOnly = make(mr({ pipeline: "success" }));
        verify(dots(ciOnly).ci.visible);
        verify(!dots(ciOnly).merge.visible, "a merge dot with nothing known about reviews");
        ciOnly.destroy();
    }

    function test_merged_and_closed_have_no_dots() {
        const states = ["merged", "closed"];
        for (let i = 0; i < states.length; i++) {
            const card = make(mr({ state: states[i], pipeline: "failed", mergeStatus: "mergeable" }), { cursored: true });
            verify(!dots(card).box.visible, states[i] + " shows dots");
            tryVerify(() => findChild(card, "tc-pr").visible, 2000);
            compare(findChild(card, "tc-pr").text, "!481 · " + I18n.t("mr.state." + states[i]));
            card.destroy();
        }
    }

    function test_a_task_without_a_request_has_no_dots() {
        const card = make({ id: "MRD-2", title: "Plain", priority: "P2", status: "todo", labels: [], ticket: ({}) });
        verify(!dots(card).box.visible);
        card.destroy();
    }

    function test_under_the_cursor_the_request_in_words() {
        const card = make(mr({ pipeline: "running", mergeStatus: "not_approved", reviewers: ["ira", "@max"] }), { cursored: true });
        tryVerify(() => findChild(card, "tc-pr").visible, 2000, "the number did not show");
        compare(findChild(card, "tc-pr").text, "!481");
        compare(findChild(card, "tc-ci").text, I18n.t("mr.ci.running"));
        verify(Qt.colorEqual(findChild(card, "tc-ci").color, Theme.textMuted), "the CI words are coloured");
        const who = findChild(card, "tc-mr-who");
        verify(who.visible, "who we wait on is missing");
        compare(who.text, I18n.t("mr.waiting").arg("ira, max"));
        const hint = findChild(card, "tc-mr-ping-hint");
        verify(hint.visible, "no key hint");
        compare(hint.text, I18n.t("mr.pingHint").arg(AppController.shortcutText("task.pingReviewers")));
        // The names say whom it waits for: "waits for review" goes unsaid.
        verify(!findChild(card, "tc-mr-merge").visible);
        card.destroy();
    }

    function test_ready_and_conflict_in_words() {
        const ready = make(mr({ pipeline: "success", mergeStatus: "mergeable" }), { cursored: true });
        tryVerify(() => findChild(ready, "tc-mr-merge").visible, 2000);
        compare(findChild(ready, "tc-mr-merge").text, I18n.t("mr.merge.ready"));
        compare(findChild(ready, "tc-ci").text, I18n.t("mr.ci.passed"));
        verify(!findChild(ready, "tc-mr-ping-hint").visible, "a key hint with nobody to wait on");
        ready.destroy();
        const conflict = make(mr({ pipeline: "failed", conflicts: true }), { cursored: true });
        tryVerify(() => findChild(conflict, "tc-mr-merge").visible, 2000);
        compare(findChild(conflict, "tc-mr-merge").text, I18n.t("mr.merge.conflict"));
        compare(findChild(conflict, "tc-ci").text, I18n.t("mr.ci.failed"));
        conflict.destroy();
    }

    function test_whose_move_setting_hides_the_names() {
        const before = AppController.appSettingsJson;
        let s = {};
        try { s = JSON.parse(before || "{}"); } catch (e) { s = {}; }
        s.git = Object.assign({}, s.git || {}, { showWhoseMove: false });
        AppController.appSettingsJson = JSON.stringify(s);
        tryVerify(() => !AppController.showWhoseMove, 2000);
        const card = make(mr({ pipeline: "running", mergeStatus: "not_approved", reviewers: ["ira"] }), { cursored: true });
        tryVerify(() => findChild(card, "tc-pr").visible, 2000);
        verify(!findChild(card, "tc-mr-who").visible, "names shown with the setting off");
        verify(!findChild(card, "tc-mr-ping-hint").visible);
        // The dots stay: they are not "whose move".
        verify(dots(card).box.visible);
        card.destroy();
        AppController.appSettingsJson = before;
        tryVerify(() => AppController.showWhoseMove, 2000);
    }

    function test_a_folded_request_on_a_jira_card() {
        // A Jira card shows its folded MR by prState / prChecks.
        const card = make({ id: "MRD-3", title: "Jira", priority: "P2", status: "review", labels: [],
                            ticket: ({ provider: "jira", key: "PROJ-1", details: ({}) }),
                            prState: "open", prNumber: 77, prUrl: "https://gitlab.invalid/a/-/merge_requests/77",
                            prChecks: "failing" });
        verify(dots(card).ci.visible);
        verify(Qt.colorEqual(dots(card).ci.ink, Theme.danger));
        card.destroy();
    }
}
