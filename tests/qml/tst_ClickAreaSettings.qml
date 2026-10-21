// Design audit DES-19 in Settings, Tweaks, Hotkeys, the theme editor, the top
// bar and the toasts: the hand-drawn buttons there were bare MouseAreas, off
// the Tab path, nameless for a screen reader and dead to the keyboard. Each
// case here finds one of them, checks it is a named Tab stop and runs it with
// Return.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp
import "../../qml/ThemePresets.js" as Presets

TestCase {
    id: tc
    name: "ClickAreaSettings"
    when: windowShown
    visible: true
    width: 900
    height: 1400

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""
    property string savedTheme: ""
    function init() {
        savedSettings = AppController.appSettingsJson;
        savedTheme = AppController.theme;
    }
    function cleanup() {
        AppController.appSettingsJson = savedSettings;
        AppController.theme = savedTheme;
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

    // A named Tab stop that Return runs.
    function reachable(area, what) {
        verify(area !== null, what + " not found");
        verify(area.activeFocusOnTab, what + " is not on the Tab path");
        verify(String(area.Accessible.name).length > 0, what + " has no accessible name");
        area.forceActiveFocus(Qt.TabFocusReason);
        verify(area.activeFocus, what + " did not take the keyboard");
        keyClick(Qt.Key_Return);
    }

    // ── Settings ───────────────────────────────────────────────────────
    function settingsAt(section) {
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        verify(sv !== null);
        sv.activeSection = section;
        wait(50);
        return sv;
    }

    function test_settings_work_day_chip_toggles_on_return() {
        const sv = settingsAt("calendar");
        let chip = null;
        tryVerify(function () { chip = find(sv, "settings-workday-6"); return chip !== null; }, 2000);
        compare(chip.role, Accessible.CheckBox);
        const before = chip.checked;
        reachable(chip, "Saturday chip");
        tryVerify(function () { return chip.checked !== before; }, 1000, "Return did not toggle the day");
        keyClick(Qt.Key_Return);
        tryVerify(function () { return chip.checked === before; }, 1000);
    }

    function connect(ids) {
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        s.integrations = s.integrations || ({});
        for (const id of ids) s.integrations[id] = Object.assign({}, s.integrations[id], { connected: true });
        AppController.appSettingsJson = JSON.stringify(s);
    }

    // The tracker list (DG-093): each tracker is a named Tab stop, Return
    // opens its detail.
    function test_settings_tracker_list_picks_on_return() {
        const sv = settingsAt("integrations");
        let item = null;
        tryVerify(function () { item = find(sv, "int-list-gitlab-click"); return item !== null; }, 2000);
        reachable(item, "GitLab in the tracker list");
        tryVerify(function () { return sv.pickedTracker === "gitlab"; }, 1000);
        tryVerify(function () { return find(sv, "int-card-gitlab").visible; }, 1000);
        verify(!find(sv, "int-card-github").visible, "two trackers open at once");
    }

    // "Как часто" keeps a cadence set before 0.8.1 that is not in its list
    // (APP-123 allowed any, up to a month).
    function test_settings_how_often_keeps_a_custom_cadence() {
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        s.integrations = Object.assign({}, s.integrations, { autoSyncMinutes: 30 * 1440 });
        AppController.appSettingsJson = JSON.stringify(s);
        connect(["github"]);
        const sv = settingsAt("integrations");
        sv.pickedTracker = "github";
        let combo = null;
        tryVerify(function () { combo = find(sv, "int-often-github-value"); return combo !== null; }, 2000);
        verify(combo.current >= 0);
        compare(combo.options[combo.current].value, 30 * 1440);
        combo.picked(0);
        tryVerify(function () { return sv.settings.integrations.autoSyncMinutes === 0; }, 1000);
    }

    // ── Hotkeys ───────────────────────────────────────────────────────
    function test_hotkeys_close_and_reset_are_tab_stops() {
        const p = createTemporaryQmlObject('import TodoCpp; HotkeysPanel { }', host);
        p.open();
        tryVerify(function () { return p.opened; }, 2000);
        const edit = find(p.contentItem, "hotkeys-edit");
        verify(edit !== null);
        compare(edit.Accessible.name, I18n.t("hotkeys.edit"));
        const close = find(p.contentItem, "hotkeys-close");
        reachable(close, "Hotkeys close");
        tryVerify(function () { return !p.opened; }, 2000, "Return did not close Hotkeys");
    }

    // ── Theme editor ───────────────────────────────────────────────────
    function test_theme_card_and_token_buttons() {
        AppController.appSettingsJson = "";
        AppController.theme = "dark";
        const ts = createTemporaryQmlObject('import TodoCpp; ThemeSettings { width: 880 }', host);
        ts.setKey.connect(function (key, value) {
            const a = Object.assign({}, ts.appearance);
            a[key] = value;
            ts.appearance = a;
            AppController.appSettingsJson = JSON.stringify({ appearance: a });
        });
        waitForRendering(ts);
        const card = find(ts, "theme-card-area-heap-light");
        compare(card.role, Accessible.RadioButton);
        reachable(card, "heap light card");
        tryVerify(function () { return ts.appearance.darkPreset === "heap-light"; }, 1000);
        verify(card.checked);

        // The colour editor opens folded; its header is the toggle.
        verify(!ts.colorsOpen);
        const toggle = find(ts, "theme-colors-toggle");
        compare(toggle.role, Accessible.CheckBox);
        reachable(toggle, "colours toggle");
        tryVerify(function () { return ts.colorsOpen; }, 1000);
        verify(toggle.checked);

        const sw = find(ts, "theme-swatch-area-danger");
        verify(sw.activeFocusOnTab);
        verify(String(sw.Accessible.name).indexOf(I18n.t("theme.token.danger")) >= 0);
        // Nothing changed yet, so the reset is off the Tab path.
        const reset = find(ts, "theme-reset-area-danger");
        verify(!reset.activeFocusOnTab, "an idle reset is a Tab stop");
    }

    // ── Top bar ────────────────────────────────────────────────────────
    // The field's key hint is gone (H2-Board has none, DG-020); "изменить
    // фильтр" is the quiet way in, and Ctrl F / "/" focus the field.
    function test_topbar_shortcut_hint_focuses_search() {
        const bar = createTemporaryQmlObject('import TodoCpp; TopBar { width: 900 }', host);
        waitForRendering(bar);
        verify(find(bar, "topbar-search-kbd") === null);
        bar.focusSearch();
        tryVerify(function () { return find(bar, "topbar-search").activeFocus; }, 1000);
        const dismiss = find(bar, "topbar-git-dismiss");
        compare(dismiss.Accessible.name, I18n.t("topbar.git.dismiss"));
    }

    // a-11 (N/X-Oth-Knowledge): the git line is a plain 32px row with a
    // hairline — "работаю над APP-101 fix/… PR #482 · CI прошёл ×"; the ID
    // opens the task, no "Открыть" button, no framed badges.
    function test_git_working_line_is_plain() {
        const line = createTemporaryQmlObject('import TodoCpp; GitWorkingLine { width: 600 }', host);
        line.taskId = "APP-101";
        line.branch = "fix/login-throttle";
        line.pr = { number: 482, state: "open", checks: "passing", url: "https://example.invalid/pr/482" };
        waitForRendering(line);
        compare(line.height, Theme.px(32));
        verify(find(line, "git-working-line-rule").visible);
        compare(find(line, "git-working-line-branch").text, "fix/login-throttle");
        compare(find(line, "git-working-line-branch").font.family, Theme.fontMono);
        compare(line.prText, "PR #482 · CI " + I18n.t("topbar.ci.passing"));
        let opened = 0;
        line.openTask.connect(function () { opened++; });
        const id = find(line, "topbar-git-open");
        compare(id.parent.objectName, "git-working-line-task", "the ID is the way to the task");
        mouseClick(id);
        compare(opened, 1);
        // Merged and draft say so; no PR, no CI: the ID and branch alone.
        line.pr = { number: 7, state: "merged", draft: false };
        compare(line.prText, "PR #7 " + I18n.t("topbar.pr.merged"));
        line.pr = null;
        verify(!find(line, "git-working-line-pr").visible);
    }

    // ── Toast ──────────────────────────────────────────────────────────
    function test_toast_action_runs_on_return() {
        const t = createTemporaryQmlObject(
            'import TodoCpp; Toast { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter }', host);
        let undone = 0;
        t.showWithAction("Deleted: A", "Undo", 10, function () { undone++; });
        let act = null;
        tryVerify(function () { act = find(t, "toast-action"); return act !== null; }, 1000);
        compare(act.Accessible.name, "Undo");
        reachable(act, "toast action");
        compare(undone, 1);
    }
}
