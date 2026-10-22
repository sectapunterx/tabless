// The two dots of a merge / pull request card (dogfood 2026-10-20 item 9):
// at rest only the dots, CI on the left and "can it be merged" on the
// right; under the cursor the request in quiet words. Hand-made task maps,
// no model and no forge.
import QtQuick
import QtTest
import TodoCpp
import "../../qml/ThemePresets.js" as Presets

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
            { d: { pipeline: "running", mergeStatus: "not_approved" }, ci: "half", ciInk: Theme.textDim, merge: "ring", mergeInk: Theme.textDim },
            { d: { pipeline: "success", mergeStatus: "mergeable" }, ci: "fill", ciInk: Theme.success, merge: "fill", mergeInk: Theme.success },
            { d: { pipeline: "success", conflicts: true }, ci: "fill", ciInk: Theme.success, merge: "ring", mergeInk: Theme.danger },
            { d: { pipeline: "success", draft: true, mergeStatus: "mergeable" }, ci: "fill", ciInk: Theme.success, merge: "ring", mergeInk: Theme.textDim },
            { d: { pipeline: "running", mergeStatus: "mergeable" }, ci: "half", ciInk: Theme.textDim, merge: "ring", mergeInk: Theme.textDim }
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
            // The key line says "!481"; the details only how it ended.
            compare(findChild(card, "tc-pr").text, I18n.t("mr.state." + states[i]));
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
        tryVerify(() => findChild(card, "tc-ci").visible, 2000, "the details did not show");
        // "!481" once, in the key line; the details do not say it again.
        compare(findChild(card, "tc-key").text, "!481");
        verify(!findChild(card, "tc-pr").visible, "the number twice: " + findChild(card, "tc-pr").text);
        const all = texts(card, []);
        compare(all.filter(t => t.indexOf("!481") >= 0).length, 1, "the number twice: " + all.join(" | "));
        // No glyph before the details: the tracker is named in words
        // (GitLab's "▲" read as the P0 mark or a whose-move arrow).
        verify(all.join(" | ").indexOf("▲") < 0, "a stray ▲: " + all.join(" | "));
        compare(findChild(card, "tc-badge").text, "GitLab");
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
        // The chip (dogfood 2026-10-22): "CI ✓", grey.
        compare(findChild(ready, "tc-ci").text, I18n.t("ci.chip.passed"));
        verify(!findChild(ready, "tc-ci-mark").visible, "a mark on a pass");
        verify(!findChild(ready, "tc-mr-ping-hint").visible, "a key hint with nobody to wait on");
        ready.destroy();
        const conflict = make(mr({ pipeline: "failed", conflicts: true }), { cursored: true });
        tryVerify(() => findChild(conflict, "tc-mr-merge").visible, 2000);
        compare(findChild(conflict, "tc-mr-merge").text, I18n.t("mr.merge.conflict"));
        // "✕ CI": the red ✕ is the only colour; the words stay grey.
        compare(findChild(conflict, "tc-ci").text, "CI");
        verify(findChild(conflict, "tc-ci-mark").visible);
        verify(Qt.colorEqual(findChild(conflict, "tc-ci-mark").color, Theme.danger));
        verify(Qt.colorEqual(findChild(conflict, "tc-ci").color, Theme.textMuted));
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
        tryVerify(() => findChild(card, "tc-ci").visible, 2000);
        verify(!findChild(card, "tc-mr-who").visible, "names shown with the setting off");
        verify(!findChild(card, "tc-mr-ping-hint").visible);
        // The dots stay: they are not "whose move".
        verify(dots(card).box.visible);
        card.destroy();
        AppController.appSettingsJson = before;
        tryVerify(() => AppController.showWhoseMove, 2000);
    }

    // Dogfood 2026-10-21: a pass read for the push before the head is not a
    // green dot. The left dot is an empty ring, named as such, and the
    // request is not "ready" on it.
    function test_a_run_about_an_older_push_is_an_empty_ring() {
        const card = make(mr({ pipeline: "success", ciHead: "aaa", headSha: "bbb", mergeStatus: "mergeable" }), { cursored: true });
        const d = dots(card);
        verify(d.ci.visible && d.merge.visible, "both dots");
        compare(d.ci.shape, "ring");
        verify(Qt.colorEqual(d.ci.ink, Theme.textDim), "an old pass is coloured: " + d.ci.ink);
        compare(d.ci.Accessible.name, I18n.t("mr.ci.unknown"));
        compare(d.merge.shape, "ring", "ready on a pass of another commit");
        tryVerify(() => findChild(card, "tc-ci").visible, 2000);
        compare(findChild(card, "tc-ci").text, I18n.t("mr.ci.unknown"));
        card.destroy();
        // About the head: green as before.
        const head = make(mr({ pipeline: "success", ciHead: "bbb", headSha: "bbb", mergeStatus: "mergeable" }));
        compare(dots(head).ci.shape, "fill");
        verify(Qt.colorEqual(dots(head).ci.ink, Theme.success));
        head.destroy();
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

    // The hollow "waiting" ring is quiet but seen: >= 3:1 against the card
    // in Tabless (heap-ink, dark) and Tabless light (borderStrong was ~1.3:1).
    function test_the_waiting_ring_is_visible_on_the_card() {
        const saved = AppController.appSettingsJson;
        const savedTheme = AppController.theme;
        const fails = [];
        for (const t of [{ id: "heap-ink", base: "dark" }, { id: "heap-light", base: "light" }]) {
            AppController.theme = t.base;
            AppController.appSettingsJson = JSON.stringify({ appearance: { darkPreset: t.id, lightPreset: t.id } });
            const card = make(mr({ pipeline: "running", mergeStatus: "not_approved" }));
            const ring = dots(card).merge;
            compare(ring.shape, "ring");
            for (const bg of [Theme.surfaceCard, Theme.surfaceCardHover]) {
                const r = Presets.contrast(String(ring.ink), String(bg));
                if (r < 3) fails.push(t.id + ": ring " + ring.ink + " on " + bg + " is " + r.toFixed(2) + ":1");
            }
            card.destroy();
        }
        AppController.appSettingsJson = saved;
        AppController.theme = savedTheme;
        compare(fails.length, 0, fails.join("; "));
    }

    // Dogfood 2026-10-22: the chip opens what failed under the card, the
    // board's key opens the same, Esc closes it. While it is up the card
    // counts as a menu (the board holds its keys back).
    function test_the_ci_chip_opens_the_popover() {
        const card = make(mr({ pipeline: "failed" }), { cursored: true });
        tryVerify(() => findChild(card, "tc-ci-chip").visible, 2000, "no chip");
        mouseClick(findChild(card, "tc-ci-chip"));
        tryVerify(() => card.menuOpen, 2000, "the chip opened nothing");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !card.menuOpen, 2000, "Esc left it open");
        verify(card.openCi(), "the key's way in");
        tryVerify(() => card.menuOpen, 2000);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !card.menuOpen, 2000);
        card.destroy();
        // No CI, no popover.
        const none = make(mr({}), { cursored: true });
        verify(!none.openCi());
        none.destroy();
    }
}
