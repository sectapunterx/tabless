// Tracker writes are opt-in, per tracker (APP-243), and a card the tracker is
// read-only for says so (APP-204). The C++ side — what is and is not sent —
// is covered by test_tracker_writes.cpp against a fake tracker.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "TrackerWrites"
    when: windowShown
    visible: true
    width: 900
    height: 1400

    Item { id: host; anchors.fill: parent }

    Component {
        id: cardComp
        TaskCard { width: 360 }
    }

    property string savedSettings: ""
    function init() {
        savedSettings = AppController.appSettingsJson;
        // The profile persists between runs: start from "nothing switched on".
        AppController.appSettingsJson = "{}";
    }
    function cleanup() {
        AppController.appSettingsJson = savedSettings;
    }

    function find(root, name) {
        if (!root) return null;
        if (root.objectName === name) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) {
            const r = find(kids[i], name);
            if (r) return r;
        }
        return null;
    }

    // Mark trackers connected: their settings rows show only then (DG-093).
    function connect(ids) {
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        s.integrations = s.integrations || ({});
        for (const id of ids) s.integrations[id] = Object.assign({}, s.integrations[id], { connected: true });
        AppController.appSettingsJson = JSON.stringify(s);
    }

    function makeCard(ticketFields, taskFields) {
        const ticket = Object.assign({ provider: "gitea", key: "#5", url: "https://gitea.example.com/a/b/issues/5" },
                                     ticketFields);
        const task = Object.assign({ id: "tw-card-5", title: "Mine", desc: "", priority: "P2", status: "prog",
                                     labels: [], ticket: ticket }, taskFields || {});
        const o = cardComp.createObject(host, { task: task });
        verify(o !== null);
        return o;
    }

    // ── Settings → Integrations: one switch per tracker that can be written ──
    function test_every_writable_tracker_has_its_own_switch_off_by_default() {
        const catalog = AppController.integrationCatalog();
        connect(catalog.map((c) => c.id));
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        sv.activeSection = "integrations";
        let writable = 0;
        for (let i = 0; i < catalog.length; i++) {
            const id = catalog[i].id;
            let card = null;
            tryVerify(function () { card = find(sv, "int-card-" + id); return card !== null; }, 2000, id + " card");
            sv.pickedTracker = id;
            tryVerify(function () { return card.visible; }, 1000);
            // "Запись в <tracker>": выключено | включено (DG-093).
            const sw = find(card, "int-write-status-" + id);
            verify(sw !== null, id + " has no write row");
            compare(sw.visible, catalog[i].writesStatus === true, id + ": write row shown on the wrong tracker");
            if (!sw.visible) continue;
            ++writable;
            compare(sw.value, "off", id + " starts switched on");
            verify(sw.label.indexOf(catalog[i].name) >= 0, id + ": the label does not name the tracker");
            verify(sw.hint.length > 0, id + " has no hint");
            // On / off is a switch at the row's right (mockup 21).
            verify(sw.asSwitch, id + ": no way to switch it on");
            verify(find(sw, "int-write-status-" + id + "-switch") !== null, id + ": the row draws no switch");
        }
        compare(writable, 5, "GitHub, GitLab, Gitea, Forgejo and Jira");
    }

    function test_switch_turns_on_only_its_own_tracker() {
        connect(["gitlab"]);
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        sv.activeSection = "integrations";
        let card = null;
        tryVerify(function () { card = find(sv, "int-card-gitlab"); return card !== null; }, 2000);
        sv.pickedTracker = "gitlab";
        const sw = find(card, "int-write-status-gitlab");
        sw.clicked();
        tryVerify(function () { return AppController.trackerWriteEnabled("gitlab"); }, 1000, "picking on did not switch it on");
        compare(AppController.trackerWriteProviders, ["gitlab"]);
        tryVerify(function () { return sw.value === "on"; }, 1000);
        sw.clicked();
        tryVerify(function () { return !AppController.trackerWriteEnabled("gitlab"); }, 1000);
    }

    function test_strings_exist_in_both_languages() {
        const keys = ["settings.integrations.writeStatus", "settings.integrations.writeStatus.offHint",
                      "settings.integrations.writeStatus.onHint", "taskcard.trackerStatus", "taskcard.outOfScope.noSync",
                      "taskcard.unsent", "taskcard.unsent.tip", "taskcard.sendPush", "taskcard.discardPush",
                      "ticket.trackerStatus", "tracker.readOnly.open", "tracker.readOnly.archive",
                      "tracker.confirm.title", "tracker.confirm.body", "tracker.confirm.send", "tracker.writeNotice.open"];
        for (let i = 0; i < keys.length; ++i) {
            verify(I18n.dict.en[keys[i]] !== undefined, keys[i] + " missing in en");
            verify(I18n.dict.ru[keys[i]] !== undefined, keys[i] + " missing in ru");
        }
    }

    // ── The card ──
    function test_unsent_move_with_writes_off_says_not_sent() {
        const card = makeCard({ syncState: "queued", unsynced: true, queued: true });
        verify(findChild(card, "tc-mark").text.indexOf(I18n.t("taskcard.mark.unsent")) >= 0);
        verify(findChild(card, "tc-mark").tip.indexOf(I18n.t("taskcard.unsent.tip")) === 0);
        card.destroy();
        // With the switch on it is the old "queued": it goes out on the next sync.
        AppController.setTrackerWriteEnabled("gitea", true);
        const on = makeCard({ syncState: "queued", unsynced: true, queued: true });
        verify(findChild(on, "tc-mark").text.indexOf(I18n.t("taskcard.mark.waiting")) >= 0);
        on.destroy();
    }

    function test_menu_offers_send_and_drop_for_an_unsent_move() {
        const card = makeCard({ syncState: "queued", unsynced: true, queued: true });
        const menu = card.contextMenu();
        verify(menu.canSendPush);
        verify(menu.canDropPush);
        card.releaseMenu();
        card.destroy();
        const synced = makeCard({ syncState: "synced" });
        const m2 = synced.contextMenu();
        verify(!m2.canSendPush);
        verify(!m2.canDropPush);
        synced.releaseMenu();
        synced.destroy();
    }

    function test_the_trackers_stage_is_in_the_tooltip_not_on_the_card() {
        const card = makeCard({ syncState: "synced", remoteStatus: "closed", remoteColumn: "done" }, { status: "prog" });
        verify(card.hoverDetails.indexOf(I18n.t("taskcard.trackerStatus").arg("closed")) >= 0,
               "the tracker's status is not told");
        verify(!findChild(card, "tc-mark").visible, "a local move reads as out of step");
        card.destroy();
        // In the column the tracker has: nothing to say.
        const same = makeCard({ syncState: "synced", remoteStatus: "closed", remoteColumn: "done" }, { status: "done" });
        verify(same.hoverDetails.indexOf(I18n.t("taskcard.trackerStatus").arg("closed")) < 0);
        same.destroy();
    }

    function test_out_of_scope_card_is_locked_only_while_writes_are_on() {
        const off = makeCard({ syncState: "synced", outOfScope: true });
        verify(!off._statusLocked, "locked with nothing written");
        off.destroy();
        AppController.setTrackerWriteEnabled("gitea", true);
        const on = makeCard({ syncState: "synced", outOfScope: true });
        verify(on._statusLocked);
        verify(on.hoverDetails.indexOf(I18n.t("taskcard.outOfScope.noSync")) >= 0, "the tooltip does not say why");
        on.destroy();
    }

    // ── "Send anyway?" ──
    function test_confirm_dialog_defaults_to_cancel() {
        const dlg = createTemporaryQmlObject('import TodoCpp; TrackerPushConfirmDialog { }', host);
        dlg.ask("tw-none", "WEB-5", "Fix it", "Jira", "In Review", "Done");
        tryCompare(dlg, "opened", true);
        // Whose ticket it is, and the question (X/N-Dlg-Conflict, R2-033).
        verify(dlg.fact.indexOf("WEB-5") >= 0 && dlg.fact.indexOf("Jira") >= 0,
               "the dialog does not name the issue and the tracker: " + dlg.fact);
        compare(dlg.title, I18n.t("tracker.notMine.outside").arg("Jira"));
        const cancel = findChild(dlg.contentItem, "trackerPushConfirmCancel");
        tryVerify(function () { return cancel.activeFocus; }, 1000, "Cancel is not the default");
        keyClick(Qt.Key_Return);
        tryCompare(dlg, "visible", false);
        // Esc cancels too.
        dlg.ask("tw-none", "WEB-5", "Fix it", "Jira", "In Review", "Done");
        tryCompare(dlg, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);
    }

    // ── Two actions on one toast: "Open in tracker" / "Archive" ──
    function test_toast_carries_two_actions() {
        const t = createTemporaryQmlObject('import TodoCpp; Toast { anchors.fill: parent }', host);
        let ran = "";
        t.showWithActions("WEB-5 is no longer in your filter", [
            { label: "Open", fn: function () { ran = "open"; } },
            { label: "Archive", fn: function () { ran = "archive"; } }
        ], 10, "warning");
        let second = null;
        tryVerify(function () { second = find(t, "toast-action-2"); return second !== null && second.visible; }, 1000);
        second.activated();
        compare(ran, "archive");
        compare(t.count, 0, "the toast stayed after its action");
    }
}
