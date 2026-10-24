// Errors & sync (0.8.1, R2-032…054): the conflict table, the tracker problem
// strip and first load, the sync indicator words, the timer and update lines
// in the sidebar, what's new once after an update.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp
import "../../qml/WhatsNew.js" as WhatsNew

TestCase {
    id: tc
    name: "ErrorsSync"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""
    property var made: []
    function init() {
        savedSettings = AppController.appSettingsJson;
        made = [];
    }
    function cleanup() {
        for (const id of made) {
            AppController.stopTaskTimer(id);
            AppController.deleteTask(id);
        }
        AppController.clearPendingUndo();
        AppController.appSettingsJson = savedSettings;
    }
    function mkTask(title) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        AppController.saveTask(d);
        tc.made.push(d.id);
        return d.id;
    }

    // ── R2-032: one table, per-field pick ──
    function conflictTask() {
        return { id: "es-conflict-1", title: "Mine title", desc: "mine body", priority: "P2", status: "prog",
                 labels: [{ id: "auth" }],
                 ticket: { provider: "github", key: "APP-101", conflict: true, conflicts: ["status", "title"],
                           remoteTitle: "Their title", remoteColumn: "review", remoteStatus: "In review",
                           updatedAt: new Date() } };
    }
    function test_conflict_is_one_table_with_a_pick_per_field() {
        const dlg = createTemporaryQmlObject('import TodoCpp; SyncConflictDialog { }', host);
        dlg.showFor(conflictTask());
        tryCompare(dlg, "opened", true);
        compare(dlg.rows.length, 2);
        verify(findChild(dlg.contentItem, "sync-conflict-title").text.indexOf("APP-101") === 0);
        // Every row starts on the local value: nothing changes by itself.
        compare(dlg.pickOf("status"), "mine");
        compare(dlg.pickOf("title"), "mine");
        mouseClick(findChild(dlg.contentItem, "sync-conflict-theirs-title"));
        compare(dlg.pickOf("title"), "theirs");
        // The keyboard picks too: Right takes the tracker's side of the row.
        keyClick(Qt.Key_Right);
        compare(dlg.pickOf(dlg.rows[dlg._row].field), "theirs");
        verify(findChild(dlg.contentItem, "sync-conflict-unchanged").text.indexOf("auth") >= 0, "labels not named");
        for (const n of ["sync-conflict-later", "sync-conflict-all-theirs", "sync-conflict-all-mine", "sync-conflict-apply"])
            verify(findChild(dlg.contentItem, n) !== null, n);
        mouseClick(findChild(dlg.contentItem, "sync-conflict-later"));
        tryCompare(dlg, "visible", false);
    }

    // ── R2-035/036: the strip and the first load ──
    function test_strip_says_each_problem_with_one_action() {
        const strip = createTemporaryQmlObject('import TodoCpp; TrackerStrip { width: 800 }', host);
        strip.sources = [
            { id: "github", name: "GitHub", failing: true, kind: "auth", failedAt: "12:10", failedAtMs: 1, offline: false },
            { id: "jira", name: "Jira", failing: true, kind: "rateLimited", failedAt: "16:00", failedAtMs: 2, offline: false },
            { id: "gitlab", name: "GitLab", failing: false, kind: "", offline: true, waiting: 2 },
            { id: "linear", name: "Linear", failing: false, kind: "", offline: false, inFlight: true, everOk: false }
        ];
        compare(strip.rows.length, 3);
        compare(strip.rows[0].fact, I18n.t("trk.strip.auth").arg("GitHub").arg("12:10"));
        compare(strip.rows[0].actionText, I18n.t("trk.strip.signIn"));
        compare(strip.rows[1].actionText, I18n.t("trk.strip.retryNow"));
        compare(strip.rows[2].fact, I18n.t("trk.strip.offline"));
        verify(strip.rows[2].more.indexOf("2") >= 0, "the waiting changes are not counted");
        compare(strip.firstLoads.length, 1);
        let asked = "";
        strip.signInRequested.connect(function (id) { asked = id; });
        strip.act(strip.rows[0]);
        compare(asked, "github");
        // × puts a line away until the failure changes.
        strip.hide(strip.rows[0].key);
        compare(strip.rows.length, 2);
        verify(strip.visible);
    }

    // ── 2026-10-21: a feature paused after failing — one line, two answers ──
    function test_paused_feature_line_and_its_two_buttons() {
        const strip = createTemporaryQmlObject('import TodoCpp; TrackerStrip { width: 860 }', host);
        strip.sources = [];
        strip.paused = [
            { id: "codeowners", feature: "codeowners", nameArg: "", kind: "errors", status: 500,
              host: "gitlab.company.ru", count: 6, since: "15:23", hidden: false },
            { id: "sync:jira", feature: "sync", nameArg: "Jira", kind: "noAccess", status: 401,
              host: "jira.company.ru", count: 3, since: "09:10", hidden: false },
            { id: "mrThreads", feature: "mrThreads", nameArg: "", kind: "errors", status: 0,
              host: "", count: 5, since: "08:00", hidden: true }
        ];
        compare(strip.rows.length, 2, "a hidden line stays away");
        compare(strip.rows[0].fact, I18n.t("pause.errorStatus").arg(I18n.t("pause.name.codeowners")).arg("gitlab.company.ru")
                                    .arg(500).arg("15:23").arg(I18n.count(6, "pause.times")));
        compare(strip.rows[0].more, I18n.t("pause.stopped"));
        compare(strip.rows[1].fact, I18n.t("pause.noAccess").arg(I18n.t("pause.name.sync").arg("Jira")).arg("jira.company.ru")
                                    .arg(401).arg("09:10").arg(I18n.count(3, "pause.times")));
        compare(strip.rows[1].more, I18n.t("pause.stoppedNoAccess"));
        if (I18n.lang === "ru")
            compare(strip.rows[0].fact, "CODEOWNERS: gitlab.company.ru отвечает ошибкой 500 с 15:23 (6 раз).");
        // A tracker's own failing line gives way to its paused line.
        strip.sources = [ { id: "jira", name: "Jira", failing: true, kind: "auth", failedAt: "09:10", failedAtMs: 3, offline: false } ];
        compare(strip.rows.length, 2);

        // The CODEOWNERS line's own two answers.
        const row = findChild(strip, "tracker-strip-row-0");
        verify(row);
        const retry = findChild(row, "tracker-strip-action");
        const off = findChild(row, "tracker-strip-action2");
        verify(retry && off);
        compare(retry.text, I18n.t("pause.retry"));
        compare(off.text, I18n.t("pause.off"));
        verify(off.visible);
        // Laid out (the layout polishes on the next frame)…
        const row1 = findChild(strip, "tracker-strip-row-1");
        tryVerify(function () { return row1.y > row.y && off.mapToItem(row, 0, 0).x > retry.mapToItem(row, 0, 0).x; });
        // …both answers stay on the line, inside the window, however long the fact.
        verify(off.mapToItem(host, off.width, 0).x <= host.width, "switch-off is pushed off the line");
        let retried = "", switched = "";
        strip.featureRetried.connect(function (id) { retried = id; });
        strip.featureSwitchedOff.connect(function (id) { switched = id; });
        mouseClick(retry);
        compare(retried, "codeowners");
        // "Switch off" writes the feature's own setting, nothing else.
        mouseClick(off);
        compare(switched, "codeowners");
        const settings = JSON.parse(AppController.appSettingsJson);
        compare((settings.git || {}).codeOwners, false);
        // The test profile outlives the run: put the switch back.
        settings.git.codeOwners = true;
        AppController.appSettingsJson = JSON.stringify(settings);
    }

    // ── R2-048: the indicator in words ──
    function test_sync_indicator_words() {
        const ps = createTemporaryQmlObject('import TodoCpp; ProfileSwitcher { width: 180; height: 28; syncing: false }', host);
        ps.sources = [];
        compare(ps.syncState, "none");
        ps.sources = [{ id: "jira", name: "Jira", failing: false, offline: false, lastOk: "2 min ago", items: 41 }];
        compare(ps.syncState, "synced");
        compare(ps.syncWord, "");
        ps.sources = [{ id: "jira", name: "Jira", failing: true, kind: "auth", offline: false }];
        compare(ps.syncState, "error");
        compare(ps.syncWord, I18n.count(1, "sync.ind.errors"));
        ps.sources = [{ id: "jira", name: "Jira", failing: false, offline: true }];
        compare(ps.syncState, "offline");
        compare(ps.syncWord, I18n.t("sync.ind.offline"));
        let asked = 0;
        ps.syncStatusRequested.connect(function () { asked++; });
        mouseClick(findChild(ps, "sidebar-sync-dot"));
        compare(asked, 1);
    }

    function test_sync_popover_lists_sources() {
        const pop = createTemporaryQmlObject('import TodoCpp; SyncPopover { }', host);
        pop.sources = [
            { id: "jira", name: "Jira", failing: false, offline: false, lastOk: "2 min ago", items: 41 },
            { id: "github", name: "GitHub", failing: true, kind: "auth", failedAt: "12:10", offline: false }
        ];
        verify(pop.rows.length >= 2);
        verify(pop.rows[0].detail.indexOf("41") >= 0);
        compare(pop.rows[1].action, "signIn");
        compare(pop.rows[1].detail, I18n.t("sync.src.authExpired").arg("12:10"));
    }

    // ── R2-052: the running timer in the sidebar ──
    function test_sidebar_shows_the_running_timer() {
        const id = mkTask("Обход ограничения попыток");
        const sb = createTemporaryQmlObject('import TodoCpp; Sidebar { width: 208; height: 600 }', host);
        const line = findChild(sb, "sidebar-timer");
        line.refresh();
        verify(!line.visible, "a timer line with no timer");
        AppController.startTaskTimer(id);
        line.refresh();
        verify(line.visible);
        compare(findChild(sb, "sidebar-timer-title").text, "Обход ограничения попыток");
        compare(findChild(sb, "sidebar-timer-clock").text, "0:00");
        let opened = "";
        sb.timerTaskRequested.connect(function (t) { opened = t; });
        mouseClick(findChild(sb, "sidebar-timer-open"));
        compare(opened, id);
        mouseClick(findChild(sb, "sidebar-timer-pause"));
        line.refresh();
        verify(!line.visible, "pause did not stop the timer");
    }

    // ── R2-053: the update line ──
    function test_update_line_at_the_bottom() {
        const sb = createTemporaryQmlObject('import TodoCpp; Sidebar { width: 208; height: 600 }', host);
        const line = findChild(sb, "sidebar-update");
        verify(!line.visible);
        sb.updateVersion = "0.8.2";
        verify(line.visible);
        verify(findChild(sb, "sidebar-update-text").text.indexOf("0.8.2") >= 0);
    }

    // ── R2-054: what's new, once ──
    function test_whats_new_is_shown_once_after_an_update() {
        compare(WhatsNew.lineOf("0.8.1"), "0.8");
        compare(WhatsNew.lineOf("v1.2.3"), "");
        verify(WhatsNew.forVersion("0.8.0") !== null);
        verify(WhatsNew.forVersion("0.9.0") !== null);
        compare(WhatsNew.lineOf("0.9.2"), "0.9");
        compare(WhatsNew.lineOf("0.10.0"), "0.10");
        verify(WhatsNew.forVersion("0.10.0") !== null);
        const dlg = createTemporaryQmlObject('import TodoCpp; WhatsNewDialog { }', host);
        // The release's date and time beside the version (dogfood 2026-10-23 #13):
        // at least the build's own time is always known.
        verify(findChild(dlg, "whats-new-released").text.length > 0, "no release date in What's new");
        // This profile ran this line already: nothing by itself.
        verify(!AppController.whatsNewDue);
        verify(!dlg.showIfUpdated());
        verify(!dlg.visible);
        // Settings → About still opens it.
        if (dlg.available) {
            verify(dlg.showNow());
            tryCompare(dlg, "opened", true);
            mouseClick(findChild(dlg.contentItem, "whats-new-ok"));
            tryCompare(dlg, "visible", false);
        }
    }

    // a-28 / a-29: the main button is filled in bold and outlined in quiet —
    // "Понятно" (N-Ntf-OS), "Открыть issue" (N-Err-Storage), "Создать
    // колонку «Готово»" (X-Err-Empty, which was filled in quiet too).
    function test_main_buttons_are_filled_in_bold_only() {
        const wn = createTemporaryQmlObject('import TodoCpp; WhatsNewDialog { }', host);
        const ri = createTemporaryQmlObject('import TodoCpp; ReportIssueDialog { }', host);
        const dc = createTemporaryQmlObject('import TodoCpp; DoneColumnDialog { }', host);
        const buttons = [findChild(wn.contentItem, "whats-new-ok"),
                         findChild(ri.contentItem, "report-issue-open"),
                         dc.buttons[0]];
        compare(dc.buttons[0].objectName, "done-column-create");
        for (const b of buttons) verify(b !== null && b.primary);
        Style.apply("bold");
        for (const b of buttons) verify(b.solid, b.objectName + " is outlined in bold");
        Style.apply("quiet");
        for (const b of buttons) verify(!b.solid, b.objectName + " is filled in quiet");
    }

    // ── R2-040: the report form shows what goes in ──
    function test_report_form_preview_follows_the_boxes() {
        const dlg = createTemporaryQmlObject('import TodoCpp; ReportIssueDialog { }', host);
        dlg.showNow();
        tryCompare(dlg, "opened", true);
        verify(dlg.withDiagnostics && !dlg.withTitles, "titles must be off by default");
        verify(dlg.preview.indexOf(AppController.appVersion) >= 0, "the version is not in the preview");
        mouseClick(findChild(dlg.contentItem, "report-issue-diag"));
        compare(dlg.preview, "");
        dlg.close();
    }

    function test_strings_exist_in_both_languages() {
        const keys = ["conflict.title", "conflict.apply", "tracker.notMine.body", "taskcard.mark.waiting",
                      "trk.strip.auth", "trk.first.title", "storage.strip.title", "storage.damaged.title",
                      "keychain.title", "report.title", "sync.ind.offline", "update.line.ready", "whatsnew.title"];
        for (const k of keys) {
            verify(I18n.dict.en[k] !== undefined, k + " missing in en");
            verify(I18n.dict.ru[k] !== undefined, k + " missing in ru");
        }
    }
}
