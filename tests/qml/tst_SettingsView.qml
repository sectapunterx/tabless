// SettingsView (qml/SettingsView.qml) defaults.
//
// SettingsView owns a `defaults` blob that _mergeDefaults() lays user-stored
// settings on top of, and _persistNow() writes the *whole* merged result back
// to AppController.appSettingsJson. So a default here is not inert: the first
// time the user changes any setting at all, every default in this blob becomes
// a stored value that Theme then reads. A default that disagrees with Theme's
// own fallback changes the app's behaviour as a side effect of an unrelated
// click, which is what these cases pin.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SettingsView"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Item { id: host; anchors.fill: parent }

    function make() {
        const sv = createTemporaryQmlObject(
            'import TodoCpp; SettingsView { anchors.fill: parent }', host);
        verify(sv !== null);
        // Sections past the first screen are built a moment later; these
        // cases read them at once.
        sv.loadAllSections();
        return sv;
    }

    function test_smoke_load() {
        const sv = make();
        verify(sv.defaults !== undefined);
        verify(sv.defaults.calendar !== undefined);
    }

    // showWeekends defaulted to false here while Theme defaulted it to true.
    // Changing the accent colour was enough to persist the blob, and the
    // weekend columns then disappeared from the calendar — a setting the user
    // never touched, changed by a click somewhere else.
    function test_calendar_defaults_agree_with_theme() {
        const sv = make();
        const saved = AppController.appSettingsJson;

        AppController.appSettingsJson = "";   // Theme falls back to its own defaults
        const themeWeekStart    = Theme.weekStart;
        const themeTimeFormat   = Theme.timeFormat;
        const themeSnapMinutes  = Theme.snapMinutes;
        const themeShowWeekends = Theme.showWeekends;
        AppController.appSettingsJson = saved;

        compare(sv.defaults.calendar.showWeekends, themeShowWeekends);
        compare(sv.defaults.calendar.weekStart,    themeWeekStart);
        compare(sv.defaults.calendar.timeFormat,   themeTimeFormat);
        compare(sv.defaults.calendar.snapMinutes,  themeSnapMinutes);
    }

    // _mergeDefaults keeps an explicitly stored value, including a falsy one.
    function test_merge_defaults_keeps_a_stored_false() {
        const sv = make();
        const merged = sv._mergeDefaults({ calendar: { showWeekends: false } });
        compare(merged.calendar.showWeekends, false);
        // unrelated keys still come from the defaults
        compare(merged.calendar.weekStart, sv.defaults.calendar.weekStart);
    }

    // ─── Integrations: connecting by hand ─────────────────────────────
    // The Connect button was hidden on any card that could do a browser
    // sign-in, so an OAuth-capable provider filled in under Advanced — a
    // personal access token, a self-hosted Jira — showed a full set of fields
    // and nothing but "Test connection" to press.

    // Settings are app-wide, so every case that writes them puts back what it
    // found — including one that fails partway through.
    property string savedSettings: ""
    property bool settingsSaved: false

    function cleanup() {
        if (!settingsSaved)
            return;
        AppController.appSettingsJson = savedSettings;
        settingsSaved = false;
    }

    // One provider's card, picked in the tracker list, with `cfg` as its
    // stored config.
    function _card(sv, provider, cfg) {
        if (!settingsSaved) {
            savedSettings = AppController.appSettingsJson;
            settingsSaved = true;
        }
        const settings = JSON.parse(AppController.appSettingsJson || "{}");
        settings.integrations = settings.integrations || ({});
        settings.integrations[provider] = cfg;
        AppController.appSettingsJson = JSON.stringify(settings);
        sv._loadFromController();
        sv.activeSection = "integrations";
        tryVerify(function () { return findChild(sv, "int-card-" + provider) !== null; },
                  2000, provider + " must have a card on the Integrations page");
        const card = findChild(sv, "int-card-" + provider);
        // Only the picked tracker's detail shows (DG-093); a hidden one makes
        // every child read `visible: false`.
        sv.pickedTracker = provider;
        tryVerify(function () { return card.visible; }, 1000, provider + " detail did not show");
        return card;
    }

    function test_connect_is_offered_for_a_hand_filled_oauth_card() {
        const sv = make();

        // A client ID is what makes a card one-click-capable without a baked
        // credential — the self-hosted path, and the shape every OAuth card
        // has in a release build.
        const card = _card(sv, "jira", { clientId: "cid" });
        compare(card.canOneClick, true);

        const connect = findChild(sv, "int-connect-jira");
        verify(connect !== null);
        compare(connect.visible, false, "the browser button is the default for a one-click card");

        card.advanced = true;
        compare(connect.visible, true, "fields on screen with no way to connect is a dead end");
    }

    // Redmine has no browser flow in any build, so its card is the token-only
    // shape — the one the Connect button never left.
    function test_connect_stays_on_a_card_that_cannot_use_the_browser() {
        const sv = make();

        const card = _card(sv, "redmine", ({}));
        compare(card.canOneClick, false);
        compare(findChild(sv, "int-connect-redmine").visible, true);
    }

    function test_a_connected_card_has_nothing_left_to_connect() {
        const sv = make();

        const card = _card(sv, "jira", { clientId: "cid", connected: true });
        card.advanced = true;
        compare(findChild(sv, "int-connect-jira").visible, false);
    }

    // A stored 0 is a real value. These reads used `|| <default>`, so a
    // A stored 0 is a real value. These reads used `|| <default>`, so a
    // hand-edited zero silently became the default instead.
    function test_merge_defaults_keeps_a_stored_zero() {
        const sv = make();
        const merged = sv._mergeDefaults({
            calendar: { snapMinutes: 0, focusBlockDuration: 0 },
            notifications: { deadlineLeadHours: 0 }
        });
        compare(merged.calendar.snapMinutes, 0);
        compare(merged.calendar.focusBlockDuration, 0);
        compare(merged.notifications.deadlineLeadHours, 0);
    }

    // A mapped status must show its column, not "Auto" (audit B1): the combo
    // read indexOfValue() before it had a model and never looked again.
    function test_status_map_combo_shows_the_chosen_column() {
        const sv = make();
        const card = _card(sv, "jira", {
            clientId: "cid", connected: true,
            seenStatuses: ["In Progress"],
            statusMap: { "In Progress": "blocked" }
        });
        tryVerify(function () { return findChild(sv, "status-map-combo") !== null; }, 2000);
        const combo = findChild(sv, "status-map-combo");
        tryCompare(combo, "value", "blocked");
        verify(combo.currentIndex >= 0);
        compare(combo.options[combo.currentIndex].value, "blocked");
    }

    // The mapping is open, as the sheet shows it (DG-093), and a status the
    // built-in table does not know says the default was picked.
    function test_status_map_is_open_and_flags_unknown_statuses() {
        const sv = make();
        const card = _card(sv, "jira", {
            clientId: "cid", connected: true,
            seenStatuses: ["In Progress", "Needs triage", "Done"]
        });
        tryVerify(function () { return findChild(card, "status-map-combo") !== null; }, 2000);
        const block = findChild(card, "status-map-jira");
        verify(block.visible, "the mapping is hidden");
        const rows = AppController.statusMappingFor("jira");
        const triage = rows.filter((r) => r.status === "Needs triage")[0];
        compare(triage.known, false);
        compare(rows.filter((r) => r.status === "Done")[0].known, true);
    }

    // Dogfood 2026-10-20: the statuses of the synced projects, a heading per
    // project when there are several, none "not recognised" when Jira said
    // what kind each one is.
    function test_status_map_groups_project_statuses() {
        const sv = make();
        const card = _card(sv, "jira", {
            clientId: "cid", connected: true,
            projectStatuses: {
                "HT": [ { name: "Selected for Development", category: "new" },
                        { name: "Ready for Testing", category: "indeterminate" } ],
                "OPS": [ { name: "Awaiting CAB", category: "indeterminate" } ]
            }
        });
        tryVerify(function () { return findChild(card, "status-map-combo") !== null; }, 2000);
        const rows = AppController.statusMappingFor("jira");
        compare(rows.length, 3);
        for (let i = 0; i < rows.length; i++)
            verify(rows[i].known, rows[i].status + " reads as not recognised");
        compare(rows.filter((r) => r.status === "Awaiting CAB")[0].column, "prog");
        // Folded while nothing needs a decision; opened to look.
        findChild(card, "status-map-jira").userOpen = true;
        const heads = [];
        const walk = (item) => {
            for (let i = 0; i < item.children.length; i++) {
                const c = item.children[i];
                if (c.objectName === "status-map-group" && c.visible) heads.push(c.text);
                walk(c);
            }
        };
        walk(card);
        verify(heads.indexOf("HT") >= 0 && heads.indexOf("OPS") >= 0, "group headings: " + heads.join(","));
    }

    // ─── Integrations: busy buttons and the last error (DES-5) ─────────
    // "Sync now" and "Test connection" stayed clickable while the request ran,
    // and a failure lived only in a toast that was gone in three seconds.

    SignalSpy { id: activatedSpy; signalName: "activated" }

    function test_a_running_sync_is_busy_and_ignores_a_second_press() {
        const sv = make();
        IntegrationActivity.clear("redmine");
        _card(sv, "redmine", { connected: true });
        const sync = findChild(sv, "int-sync-redmine");
        verify(sync !== null);
        compare(sync.available, true);

        IntegrationActivity.start("redmine", "sync");
        compare(sync.busy, true);
        compare(sync.available, false, "a busy button still took presses");
        compare(sync.shownText, I18n.t("settings.integrations.syncing"));
        compare(sync.Accessible.name, I18n.t("settings.integrations.syncing"));

        activatedSpy.clear();
        activatedSpy.target = sync;
        sync.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(activatedSpy.count, 0, "Return ran a sync that is already running");
        verify(sync.activeFocus, "going busy took the keyboard away");

        IntegrationActivity.finish("redmine", "sync", true, "");
        compare(sync.busy, false);
        compare(sync.available, true);
    }

    function test_a_failure_stays_on_the_card_until_the_next_success() {
        const sv = make();
        IntegrationActivity.clear("redmine");
        _card(sv, "redmine", { connected: true });
        const err = findChild(sv, "int-last-error-redmine");
        const synced = findChild(sv, "int-last-sync-redmine");
        compare(err.visible, false);

        IntegrationActivity.start("redmine", "test");
        IntegrationActivity.finish("redmine", "test", false, "Redmine: 401 bad token");
        compare(err.visible, true, "the error went with the toast");
        verify(err.text.indexOf("401 bad token") >= 0, err.text);
        compare(findChild(sv, "int-test-redmine").busy, false);

        IntegrationActivity.finish("redmine", "sync", true, "");
        compare(err.visible, false, "a sync that worked left the old error up");
        compare(synced.visible, true, "no last-sync time after a sync");
    }

    // The real round trip: a card marked connected with no token has no
    // tracker behind it, so AppController answers the sync at once — as a
    // failure the card keeps, not only a toast.
    function test_sync_now_with_no_tracker_shows_why() {
        const sv = make();
        IntegrationActivity.clear("redmine");
        _card(sv, "redmine", { connected: true });
        const sync = findChild(sv, "int-sync-redmine");
        sync.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(sync.busy, false, "the answer came back and the button stayed busy");
        const err = findChild(sv, "int-last-error-redmine");
        compare(err.visible, true);
        verify(err.text.length > 0);
    }

    // Settings is rebuilt as the user moves around; an answer that came in
    // meanwhile is still on the card when they come back.
    function test_the_last_error_outlives_the_settings_view() {
        IntegrationActivity.clear("redmine");
        IntegrationActivity.finish("redmine", "sync", false, "Redmine sync failed: 404");
        const sv = make();
        _card(sv, "redmine", { connected: true });
        const err = findChild(sv, "int-last-error-redmine");
        compare(err.visible, true);
        verify(err.text.indexOf("404") >= 0, err.text);
        IntegrationActivity.clear("redmine");
        compare(err.visible, false);
    }

    function test_a_busy_limit_frees_a_button_nobody_answered() {
        const sv = make();
        IntegrationActivity.clear("redmine");
        _card(sv, "redmine", { connected: true });
        IntegrationActivity.start("redmine", "sync");
        IntegrationActivity.expire(Date.now() + 10 * 60 * 1000);
        compare(findChild(sv, "int-sync-redmine").busy, false);
        compare(findChild(sv, "int-last-error-redmine").visible, false,
                "a timeout is not an answer and must not invent an error");
    }

    // ─── Settings actions on the keyboard (DES-8) ──────────────────────
    // Export, Import, Restore, Wipe, Report, Logs, Check updates were
    // Rectangle + MouseArea: no Tab stop, no name, no disabled state.

    // SHELL-9 (audit 2026-09-30): the standup time took "25:00", "abc" and
    // an empty field as they were, and the reminder then stopped. Invalid
    // input is put back; "9:30" is stored as "09:30".
    function test_standup_time_is_validated_and_normalised() {
        const sv = make();
        sv.activeSection = "calendar";
        tryVerify(function () { return findChild(sv, "standupTimeRow-field") !== null; }, 2000);
        const row = findChild(sv, "standupTimeRow");
        const field = findChild(sv, "standupTimeRow-field");
        sv.set("calendar", "standupTime", "10:00");

        const bad = ["25:00", "abc", "", "9:3"];
        for (let i = 0; i < bad.length; i++) {
            field.text = bad[i];
            row.commitPending();
            compare(sv.settings.calendar.standupTime, "10:00", "stored '" + bad[i] + "'");
        }
        field.text = "25:00";
        verify(row.invalid);
        verify(row.status.length > 0, "no message for an invalid time");

        field.text = "9:30";
        row.commitPending();
        compare(sv.settings.calendar.standupTime, "09:30");
        sv.set("calendar", "standupTime", "10:00");
    }

    function test_data_actions_are_on_the_tab_path_and_named() {
        const sv = make();
        sv.activeSection = "data";
        // Резервные копии: two buttons; Экспорт: one, named for its row.
        tryVerify(function () { return findChild(sv, "settings-export-export-click") !== null; }, 2000);
        const exp = findChild(sv, "settings-backups-restore-click");
        const imp = findChild(sv, "settings-export-export-click");
        verify(exp.activeFocusOnTab);
        compare(exp.Accessible.role, Accessible.Button);
        compare(exp.Accessible.name, I18n.t("settings.data.restoreDots"));
        compare(imp.Accessible.name, I18n.t("settings.data.export") + ": " + I18n.t("settings.data.exportDots"));
        exp.forceActiveFocus();
        keyClick(Qt.Key_Tab);
        verify(imp.activeFocus, "Tab from Backups did not reach Export");
    }

    // Design audit DES-16: "Delete everything" asks in a dialog that names
    // the profiles that go, with focus on Cancel; nothing is wiped until the
    // red button in it is pressed.
    function test_wipe_asks_in_a_dialog_with_focus_on_cancel() {
        const sv = make();
        sv.activeSection = "data";
        tryVerify(function () { return findChild(sv, "settings-wipe") !== null; }, 2000);
        const wipe = findChild(sv, "settings-wipe");
        // Under the section's "more" line (DG-090).
        findChild(findChild(sv, "settings-block-data"), "settings-more-toggle").parent.userOpen = true;
        const dialog = findChild(sv, "settings-wipe-dialog");
        verify(dialog !== null);
        const before = AppController.profiles.length;
        wipe.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryVerify(function () { return dialog.opened; }, 1000, "Space did not open the wipe dialog");
        const body = findChild(dialog.contentItem, "settings-wipe-dialog-body");
        const firstName = AppController.profiles[0].name;
        verify(body.text.indexOf(firstName) >= 0, "the dialog does not name the profiles: " + body.text);
        const cancel = findChild(dialog.contentItem, "settings-wipe-cancel");
        tryVerify(function () { return cancel.activeFocus; }, 1000, "focus is not on Cancel");
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return !dialog.opened; }, 1000);
        compare(AppController.profiles.length, before, "the profiles were touched");
    }

    function test_about_actions_are_named_buttons() {
        const sv = make();
        sv.activeSection = "about";
        tryVerify(function () { return findChild(sv, "settings-open-logs-open-click") !== null; }, 2000);
        const names = {
            "settings-report-issue-open-click": I18n.t("settings.help.report") + ": " + I18n.t("settings.help.open"),
            "settings-open-logs-open-click": I18n.t("settings.about.logs") + ": " + I18n.t("settings.about.openLogs"),
            "settings-check-updates-click": I18n.t("settings.about.checkUpdates")
        };
        for (const id in names) {
            const b = findChild(sv, id);
            verify(b !== null, id);
            verify(b.activeFocusOnTab, id + " is not on the Tab path");
            compare(b.Accessible.role, Accessible.Button, id);
            compare(b.Accessible.name, names[id], id);
        }
    }

    // The button itself: Return, Enter and Space run it, a disabled one
    // neither runs nor looks live. (The Settings instances open a browser,
    // a folder or the network, so their keys are pinned here instead.)
    function test_action_button_keys_and_disabled_state() {
        const b = createTemporaryQmlObject('import TodoCpp; ActionButton { text: "Go" }', host);
        activatedSpy.clear();
        activatedSpy.target = b;
        b.forceActiveFocus();
        verify(b.activeFocus);
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Enter);
        keyClick(Qt.Key_Space);
        compare(activatedSpy.count, 3);
        compare(b.opacity, 1);

        b.enabled = false;
        compare(b.available, false);
        compare(b.opacity, 0.45, "a disabled button looked live");
        b.press();
        compare(activatedSpy.count, 3, "a disabled button still ran");
    }

    // SHELL-11: the section list is one Tab stop. Tab from it goes into the
    // open section's body instead of opening the next section; Down still
    // moves through the list and opens what it lands on.
    function test_tab_from_the_nav_enters_the_section_body() {
        const sv = make();
        sv.activeSection = "appearance";
        const profileRow = findChild(sv, "settings-nav-appearance");
        const appearanceRow = findChild(sv, "settings-nav-tasks");
        verify(profileRow !== null && appearanceRow !== null);
        verify(profileRow.activeFocusOnTab);
        verify(!appearanceRow.activeFocusOnTab, "every nav row is a Tab stop");
        profileRow.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Tab);
        compare(sv.activeSection, "appearance", "Tab opened another section");
        const f = profileRow.Window.window.activeFocusItem;
        verify(f !== null);
        verify(String(f.objectName).indexOf("settings-nav-") !== 0, "Tab stayed in the nav: " + f.objectName);

        profileRow.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Down);
        verify(appearanceRow.activeFocus);
        compare(sv.activeSection, "tasks");
        verify(appearanceRow.activeFocusOnTab);
        verify(!profileRow.activeFocusOnTab);

        // A search that hides the open section leaves its first hit the stop.
        const target = sv.sections[sv.sections.length - 1];
        sv.searchText = target.title;
        verify(sv._navTabIndex >= 0, "no nav row is a Tab stop under a search");
        verify(sv._sectionMatches(sv.sections[sv._navTabIndex]));
        verify(sv.sections[sv._navTabIndex].id !== "tasks");
        sv.searchText = "";
    }

    // ─── Search finds single settings (APP-207) ───────────────────────

    // The page item showing `text` as a row label.
    function _row(sv, text) {
        const walk = (it) => {
            const kids = it ? it.children : [];
            for (let i = 0; i < kids.length; i++) {
                const k = kids[i];
                if (k && k.hasLabel !== undefined && k.stackBelow !== undefined && k.label === text) return k;
                const sub = walk(k);
                if (sub) return sub;
            }
            return null;
        };
        return walk(sv);
    }
    function _inside(item, ancestor) {
        for (let p = item; p; p = p.parent)
            if (p === ancestor) return true;
        return false;
    }

    // A setting's own title finds its section in every section that has
    // settings, and the nav row says how many it found.
    function test_search_finds_a_setting_of_every_section() {
        const sv = make();
        sv._indexWanted = true;   // built on the first keystroke otherwise
        const seen = {};
        for (let i = 0; i < sv.searchIndex.length; i++) {
            const e = sv.searchIndex[i];
            if (e.kind !== "row" || seen[e.section] || !sv.openSection(e.section)) continue;
            seen[e.section] = true;
            sv.searchText = e.title;
            const sec = sv.sections.filter((s) => s.id === e.section)[0];
            verify(sv._sectionMatches(sec), e.title + " does not find " + e.section);
            verify((sv.searchCounts[e.section] || 0) >= 1, "no count for " + e.section);
            const navRow = findChild(sv, "settings-nav-" + e.section);
            verify(navRow.visible, e.section + " hidden while its setting matches");
            const badge = findChild(sv, "settings-nav-count-" + e.section);
            verify(badge.visible && Number(badge.text) >= 1, "no count shown for " + e.section);
        }
        sv.searchText = "";
        verify(Object.keys(seen).length >= 10, "too few sections with rows: " + Object.keys(seen));
    }

    // A section with no matching setting and a title that does not match
    // leaves the nav.
    function test_search_hides_sections_without_a_hit() {
        const sv = make();
        sv.searchText = I18n.t("settings.sound.meetingMinutes");
        verify(findChild(sv, "settings-nav-notifications").visible);
        verify(!findChild(sv, "settings-nav-git").visible, "Git kept with nothing matching");
        sv.searchText = "";
    }

    // Enter: the section opens, the page scrolls to the first matching
    // setting and focus lands on it.
    function test_enter_opens_the_section_on_the_setting() {
        const sv = make();
        sv.activeSection = "about";
        const text = I18n.t("settings.safety.beforeImport");   // mid-page, Подстраховка
        sv.searchText = text;
        const field = findChild(sv, "settings-search");
        field.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(sv.activeSection, "safety");
        tryVerify(() => _row(sv, text) !== null, 2000, "row not built");
        const row = _row(sv, text);
        // Focus lands once the scroll has settled on the laid-out page.
        tryVerify(() => _inside(sv.Window.activeFocusItem, row), 4000, "focus is not on the setting");
        // In view: the row sits inside the body's visible band.
        tryVerify(() => {
            let fl = row.parent;
            while (fl && fl.contentY === undefined) fl = fl.parent;
            const y = row.mapToItem(fl, 0, 0).y;
            return y >= 0 && y + row.height <= fl.height;
        }, 3000, "the setting was not scrolled into view");
        compare(row.searchMark, 1, "the matching row is not marked");
        sv.searchText = "";
    }

    // Matching rows of the open section are marked, the rest step back;
    // clearing the search puts them all back.
    function test_open_section_marks_hits_and_dims_the_rest() {
        const sv = make();
        sv.activeSection = "appearance";
        const hitText = I18n.t("settings.appearance.reducedMotion");
        const other = I18n.t("settings.system.startAtLogin");
        tryVerify(() => _row(sv, hitText) !== null && _row(sv, other) !== null);
        sv.searchText = hitText;
        tryCompare(_row(sv, hitText), "searchMark", 1);
        tryCompare(_row(sv, other), "searchMark", -1);
        verify(_row(sv, other).opacity < 1);
        sv.searchText = "";
        tryCompare(_row(sv, other), "searchMark", 0);
        compare(_row(sv, other).opacity, 1);
    }

    // People search in English in a Russian UI: an English title still finds
    // the setting.
    function test_english_query_finds_in_russian() {
        const savedLang = AppController.language;
        AppController.language = "ru";
        const sv = make();
        sv.searchText = "Animations";
        const found = sv.searchMatches.map((m) => m.key);
        const appearanceHit = sv._sectionMatches(sv.sections.filter((s) => s.id === "appearance")[0]);
        const ruTitle = I18n.t("settings.appearance.reducedMotion");
        sv.searchText = "";
        AppController.language = savedLang;
        verify(found.indexOf("settings.appearance.reducedMotion") >= 0, "English query found nothing in ru: " + found);
        verify(appearanceHit);
        verify(ruTitle !== "Animations", "the UI was not in Russian");
    }

    // Nothing matches: the nav says so; Esc clears and every section is back.
    function test_nothing_found_then_escape_clears() {
        const sv = make();
        const field = findChild(sv, "settings-search");
        field.forceActiveFocus();
        sv.searchText = "zzqxv-no-such-setting";
        verify(sv.searchEmpty);
        const empty = findChild(sv, "settings-search-empty");
        verify(empty !== null && empty.visible, "no empty state");
        keyClick(Qt.Key_Escape);
        compare(sv.searchText, "");
        compare(field.text, "");
        verify(!empty.visible);
        for (let i = 0; i < sv.sections.length; i++)
            verify(findChild(sv, "settings-nav-" + sv.sections[i].id).visible, sv.sections[i].id + " still hidden");
    }

    // Down from the search walks the filtered nav, skipping hidden rows.
    function test_arrows_walk_the_filtered_nav() {
        const sv = make();
        sv.activeSection = "profile";
        sv.searchText = I18n.t("settings.sound.meetingMinutes");
        const field = findChild(sv, "settings-search");
        field.forceActiveFocus();
        keyClick(Qt.Key_Down);
        const f = sv.Window.activeFocusItem;
        verify(f && f.visible && String(f.objectName).indexOf("settings-nav-") === 0, "Down did not reach a visible nav row");
        verify(sv._navTabIndex >= 0 && sv._sectionMatches(sv.sections[sv._navTabIndex]));
        sv.searchText = "";
    }

    // Integration fields come from the provider catalogue: "JQL" finds Jira.
    function test_provider_fields_are_searchable() {
        const sv = make();
        sv.searchText = "JQL";
        const hit = sv.searchMatches.filter((m) => m.section === "integrations");
        sv.searchText = "";
        verify(hit.length >= 1, "JQL found nothing");
        compare(hit[0].objectName, "int-card-jira");
    }

    // The row of Settings > Keys that is recording, if any.
    function _recording(sv) {
        const hint = I18n.t("settings.keys.recordHint");
        const walk = function (it) {
            if (!it) return false;
            if (it.text === hint && it.visible) return true;
            const kids = it.children || [];
            for (let i = 0; i < kids.length; i++) if (walk(kids[i])) return true;
            return false;
        };
        return walk(sv);
    }

    // IDIOT-SHELL-11: Enter answers the conflict box (Replace) instead of
    // being recorded; Backspace clears what was heard and unbinds nothing.
    // IDIOT-SHELL-10: leaving the row stops the recording.
    function test_key_recording_conflict_and_leaving() {
        AppController.resetAllShortcuts();
        const sv = make();
        sv.activeSection = "shortcuts";
        tryVerify(function () { return findChild(sv, "hotkey-chip-palette.open") !== null; }, 3000);
        const chip = findChild(sv, "hotkey-chip-palette.open");
        const taken = AppController.shortcutFor("task.new");
        verify(taken.length > 0);
        chip.forceActiveFocus();
        keyClick(Qt.Key_Return);
        verify(_recording(sv), "Enter did not start recording");
        keyClick(Qt.Key_Backspace);
        verify(AppController.shortcutFor("palette.open").length > 0, "Backspace took the key off");
        verify(_recording(sv));
        keyClick(Qt.Key_N, Qt.ControlModifier);
        tryVerify(function () { return findChild(sv, "settings-keys-conflict").visible; }, 1000, "no conflict box");
        keyClick(Qt.Key_Return);
        compare(AppController.shortcutFor("palette.open"), "Ctrl+N", "Enter did not replace");
        verify(!_recording(sv));
        AppController.resetAllShortcuts();
        wait(50);
        const chip2 = findChild(sv, "hotkey-chip-palette.open");
        chip2.forceActiveFocus();
        keyClick(Qt.Key_Return);
        verify(_recording(sv));
        findChild(sv, "settings-keys-search").forceActiveFocus();
        tryVerify(function () { return !_recording(sv); }, 1000, "the recording stayed armed after focus left");
        AppController.resetAllShortcuts();
    }


    // APP-291: the "statuses → columns" block folds by its heading.
    function test_status_map_folds() {
        const sv = make();
        const card = _card(sv, "jira", {
            clientId: "cid", connected: true,
            seenStatuses: ["In Progress", "Done"]
        });
        tryVerify(function () { return findChild(card, "status-map-combo") !== null; }, 2000);
        const block = findChild(card, "status-map-jira");
        const toggle = findChild(card, "status-map-toggle-jira");
        verify(block.visible && toggle.visible);
        const was = block.open;
        let area = null;
        for (let i = 0; i < toggle.children.length; i++)
            if (toggle.children[i].activated !== undefined) area = toggle.children[i];
        verify(area !== null);
        area.activated();
        compare(block.open, !was);
        compare(findChild(card, "status-map-combo").visible, !was);
        area.activated();
        compare(block.open, was);
    }

    // ─── 0.9 acceptance polish (B6) ───────────────────────────────────

    // A7-3: the options say who checks, and the hint follows the choice:
    // "only by hand" does not promise a check on launch.
    // APP-319: lunch is off by default; switched on, the calendar has it, and
    // its times show to be changed.
    function test_lunch_switch() {
        if (!settingsSaved) { savedSettings = AppController.appSettingsJson; settingsSaved = true; }
        const sv = make();
        sv.activeSection = "calendar";
        tryVerify(function () { return findChild(sv, "settings-lunch") !== null; }, 2000);
        sv.set("calendar", "lunchOn", false);
        compare(AppController.lunch.start, undefined, "off: no lunch");
        verify(!findChild(sv, "settings-lunch-start").visible);
        sv.set("calendar", "lunchOn", true);
        compare(AppController.lunch.start, 13);
        compare(AppController.lunch.end, 14);
        tryVerify(function () { return findChild(sv, "settings-lunch-start").visible; });
        sv.set("calendar", "lunchEnd", "12:00");
        compare(AppController.lunch.start, undefined, "an end before the start is no lunch");
    }

    function test_update_hint_follows_the_choice() {
        if (!settingsSaved) { savedSettings = AppController.appSettingsJson; settingsSaved = true; }
        const sv = make();
        sv.activeSection = "about";
        tryVerify(function () { return findChild(sv, "settings-updates") !== null; }, 2000);
        const row = findChild(sv, "settings-updates");
        sv.set("updates", "autoCheck", false);
        compare(row.status, sv.updatesHint(AppController.updateStatus, false));
        compare(sv.updatesHint("", true), I18n.t("settings.about.updates.hint"));
        compare(sv.updatesHint("", false), I18n.t("settings.about.updates.hint.manual"));
        compare(sv.updatesHint("Up to date", false), "Up to date", "a status in flight wins");
        compare(I18n.dict.en["settings.about.updates.auto"], "Automatically");
        compare(I18n.dict.ru["settings.about.updates.auto"], "Автоматически");
        verify(I18n.dict.en["settings.about.whatsNew.hint"].indexOf("ru/en") < 0);
        verify(I18n.dict.ru["settings.about.whatsNew.hint"].indexOf("ru/en") < 0);
    }

    // A7-6 / A5-26: a backup row says when it was taken the way the rest
    // of the app does, never the raw ISO stamp; the row button that opens
    // the time machine is called that.
    function test_backup_times_are_human() {
        const sv = make();
        sv.activeSection = "data";
        tryVerify(function () { return findChild(sv, "settings-data-root") !== null; }, 2000);
        const data = findChild(sv, "settings-data-root");
        const iso = "2026-10-12T14:45:01";
        const said = data.whenTaken(iso);
        verify(said.indexOf("T14") < 0 && said.indexOf("2026-10-12") < 0, "raw stamp: " + said);
        verify(said.indexOf(I18n.fmtTime(new Date(iso))) >= 0, "no time in " + said);
        const now = new Date();
        compare(data.whenTaken(now.toISOString()), I18n.t("settings.data.today").arg(I18n.fmtTime(now)));
        compare(I18n.dict.en["settings.data.restoreDots"], "Time machine…");
        compare(I18n.dict.ru["settings.data.restoreDots"], "Машина времени…");
    }

    // b-11 (0.9.4, owner) over A6-16: scale and contrast go under "more" as
    // DG-090 has it — the sheet shows four rows at rest; Ctrl +/- still
    // scale from anywhere. A7-11: the window and start-up rows have a group
    // of their own.
    function test_scale_and_contrast_are_folded() {
        const sv = make();
        sv.activeSection = "appearance";
        tryVerify(function () { return findChild(sv, "settings-ui-scale") !== null; }, 2000);
        const more = findChild(findChild(sv, "settings-block-appearance"), "settings-more-toggle").parent;
        verify(!more.open, "the fold is open");
        for (const name of ["settings-ui-scale", "settings-contrast"]) {
            const row = findChild(sv, name);
            verify(_inside(row, more), name + " is not under more");
        }
        verify(findChild(sv, "settings-ui-scale").hint.indexOf(AppController.keyText(AppController.shortcutFor("zoom.in"))) >= 0,
               "the scale hint names the zoom key");
        const tray = findChild(sv, "settings-close-to-tray");
        verify(!_inside(tray, more), "close to tray is under more");
        verify(_inside(tray, findChild(sv, "settings-window-group")));
        verify(I18n.dict.ru["close.ask.body"].indexOf("в Настройках → Внешний вид → Окно и запуск") >= 0);
    }

    // A7-8: after a key is recorded the focus stays on that key, so Tab
    // goes on to the next row instead of starting over at the sidebar.
    function test_focus_stays_on_the_key_just_recorded() {
        AppController.resetAllShortcuts();
        const sv = make();
        sv.activeSection = "shortcuts";
        tryVerify(function () { return findChild(sv, "hotkey-chip-task.copyId") !== null; }, 3000);
        let chip = findChild(sv, "hotkey-chip-task.copyId");
        chip.forceActiveFocus();
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_9, Qt.ControlModifier | Qt.AltModifier);
        compare(AppController.shortcutFor("task.copyId"), "Ctrl+Alt+9");
        tryVerify(function () {
            const c = findChild(sv, "hotkey-chip-task.copyId");
            return c !== null && c.activeFocus;
        }, 1000, "focus left the key just recorded");
        AppController.resetAllShortcuts();
    }

    // Dogfood 2026-10-19: a repo on an SSH dev server is added from host +
    // path, tested first, and a failure says what to do; CODEOWNERS hints
    // have their own switch. Nothing here connects anywhere: the answer is
    // handed in as AppController would send it.
    function test_remote_repo_form_and_code_owners_switch() {
        const sv = make();
        sv.activeSection = "git";
        tryVerify(function () { return findChild(sv, "settings-git-code-owners") !== null; }, 2000);
        verify(findChild(sv, "settings-git-code-owners").checked, "CODEOWNERS hints are on by default");
        findChild(sv, "settings-git-repos").triggered("add");
        tryVerify(function () { return findChild(sv, "settings-git-remote-host") !== null; }, 2000, "no remote form");
        const form = findChild(sv, "settings-git-remote");
        const test = findChild(sv, "settings-git-remote-test");
        verify(!test.enabled, "nothing to test yet");
        findChild(sv, "settings-git-remote-host").text = "me@devbox";
        findChild(sv, "settings-git-remote-path").text = "/srv/app";
        compare(form.entry, "me@devbox:/srv/app");
        verify(form.valid);
        verify(test.enabled);
        form.testedEntry = form.entry;
        form.status = "testing";
        AppController.remoteRepoTested("me@devbox:/srv/app", "auth", "Permission denied (publickey)", "");
        compare(form.status, "auth");
        const status = findChild(sv, "settings-git-remote-status");
        verify(status.text.indexOf(I18n.t("settings.git.remote.err.auth")) === 0, status.text);
        verify(status.text.indexOf("publickey") > 0);
        // Another entry's answer is not this form's.
        AppController.remoteRepoTested("other:/x", "", "", "main");
        compare(form.status, "auth");
        AppController.remoteRepoTested("me@devbox:/srv/app", "", "", "main");
        compare(form.status, "ok");
        compare(status.text, I18n.t("settings.git.remote.ok").arg("main"));
        // Editing the path forgets the old answer.
        findChild(sv, "settings-git-remote-path").text = "/srv/other";
        compare(form.status, "");
    }

    // Dogfood 2026-10-20 item 2: the form says what is wrong as it is typed,
    // shows examples, and takes a port or a whole pasted address.
    function test_remote_repo_form_hints_as_you_type() {
        const sv = make();
        sv.activeSection = "git";
        tryVerify(function () { return findChild(sv, "settings-git-repos") !== null; }, 2000);
        findChild(sv, "settings-git-repos").triggered("add");
        tryVerify(function () { return findChild(sv, "settings-git-remote-host") !== null; }, 2000, "no remote form");
        const form = findChild(sv, "settings-git-remote");
        const host = findChild(sv, "settings-git-remote-host");
        const path = findChild(sv, "settings-git-remote-path");
        const problem = findChild(sv, "settings-git-remote-problem");
        const test = findChild(sv, "settings-git-remote-test");
        const add = findChild(sv, "settings-git-remote-add");
        compare(host.placeholderText, I18n.t("settings.git.remote.hostPh"));
        compare(path.placeholderText, I18n.t("settings.git.remote.pathPh"));
        verify(findChild(sv, "settings-git-remote-examples").text.indexOf("me@dev01:2222") >= 0);
        verify(!problem.visible, "nothing typed, nothing wrong");
        // A pasted web address: said at once, nothing to test or add.
        host.text = "https://gitlab.corp/tel/bts/l2-ref-model";
        compare(form.problem, "web");
        verify(problem.visible);
        compare(problem.text, I18n.t("settings.git.remote.bad.web"));
        verify(!test.enabled);
        verify(!add.enabled);
        host.text = "git@gitlab.corp:tel/bts/l2-ref-model.git";
        compare(form.problem, "forge");
        // A port in the host field.
        host.text = "me@dev01:2222";
        path.text = "~/src/app";
        compare(form.problem, "");
        verify(!problem.visible);
        compare(form.entry, "ssh://me@dev01:2222/~/src/app");
        verify(test.enabled);
        path.text = "C:\\src\\app";
        compare(form.problem, "winPath");
        verify(!test.enabled);
        // The whole address pasted into the host field, path left empty.
        path.text = "";
        host.text = "me@dev01:/srv/app";
        compare(form.entry, "me@dev01:/srv/app");
        verify(add.enabled);
    }

    function _tagFor(item, entry) {
        if (!item) return null;
        if (item.objectName === "settings-git-remote-tag" && item.parent && item.parent.modelData === entry) return item;
        const kids = item.children || [];
        for (let i = 0; i < kids.length; i++) {
            const f = tc._tagFor(kids[i], entry);
            if (f) return f;
        }
        if (item.contentItem && item.contentItem !== item) return tc._tagFor(item.contentItem, entry);
        return null;
    }

    // Review 2026-10-19: Add works untested and after a failed test (the
    // server may be down right now), then tests, and the row says what it
    // found. The poll goes nowhere: a .invalid host never resolves.
    function test_remote_repo_add_then_tests_on_the_row() {
        const saved = AppController.appSettingsJson;
        try {
            const sv = make();
            sv.activeSection = "git";
            tryVerify(function () { return findChild(sv, "settings-git-repos") !== null; }, 2000);
            findChild(sv, "settings-git-repos").triggered("add");
            tryVerify(function () { return findChild(sv, "settings-git-remote-host") !== null; }, 2000, "no remote form");
            const form = findChild(sv, "settings-git-remote");
            const asked = [];
            form.runTest = function (e) { asked.push(e); };
            findChild(sv, "settings-git-remote-host").text = "me@review-probe.invalid";
            findChild(sv, "settings-git-remote-path").text = "/srv/review-probe";
            const entry = form.entry;
            verify(entry.length > 0 && form.valid, entry);
            // A failed test does not stop Add.
            form.testedEntry = entry;
            AppController.remoteRepoTested(entry, "unreachable", "", "");
            compare(form.status, "unreachable");
            const add = findChild(sv, "settings-git-remote-add");
            verify(add.enabled, "Add waits for a passing test");
            add.activated();
            compare(asked, [entry], "Add did not test what it added");
            let tag = null;
            tryVerify(function () { tag = tc._tagFor(form.parent, entry); return tag !== null; }, 2000, "no row for the added repo");
            verify(tag.text.indexOf(I18n.t("settings.git.remote.testing")) > 0, tag.text);
            AppController.remoteRepoTested(entry, "auth", "Permission denied (publickey)", "");
            verify(tag.text.indexOf(I18n.t("settings.git.remote.err.auth")) > 0, tag.text);
            verify(tag.text.indexOf("publickey") > 0, tag.text);
            compare(tag.color, Theme.danger);
        } finally {
            AppController.appSettingsJson = saved;
        }
    }

    // Dogfood 2026-10-20 #10: About says which build runs — "Stable 0.9.5",
    // never "stable" on a feature build — and has a Channel row.
    function test_about_names_the_build() {
        const sv = make();
        sv.activeSection = "about";
        tryVerify(function () { return findChild(sv, "settings-section-sub-about") !== null; }, 2000);
        const sub = findChild(sv, "settings-section-sub-about");
        compare(sub.text, Brand.buildLine());
        verify(sub.text.indexOf("%1") < 0);
        if (Brand.channelBuild) {
            verify(sub.text.indexOf(AppController.buildSha) >= 0, sub.text);
        } else {
            verify(sub.text.indexOf(AppController.appVersion) >= 0, sub.text);
            compare(sub.text, I18n.t("build.stable").arg(AppController.appVersion));
        }
        verify(findChild(sv, "settings-about-channel") !== null);
        compare(sv.updatesHint("", true), I18n.t(Brand.channelBuild ? "settings.about.updates.hint.channel"
                                                                    : "settings.about.updates.hint").arg(Brand.channel));
    }

    // Dogfood 2026-10-20 #12: the guide opens on a part, Esc clears its
    // search and then closes it, and "Show" goes to that screen.
    function test_help_reader_keys() {
        const sv = make();
        const savedView = AppController.currentView;
        sv.openHelp("help-notes");
        const reader = findChild(sv, "settings-help-reader");
        tryCompare(reader, "opened", true);
        const doc = reader.contentItem;
        compare(doc.currentAnchor, "help-notes");
        doc.focusSearch();
        doc.query = "календарь";
        keyClick(Qt.Key_Escape);
        compare(doc.query, "");
        verify(reader.visible, "the first Esc only clears the search");
        keyClick(Qt.Key_Escape);
        tryCompare(reader, "visible", false);
        sv.openHelp("help-git");
        tryCompare(reader, "opened", true);
        doc.showRequested("settings:git");
        tryCompare(reader, "visible", false);
        compare(sv.activeSection, "git");
        sv.openHelp("");
        tryCompare(reader, "opened", true);
        doc.showRequested("notes");
        tryCompare(reader, "visible", false);
        compare(AppController.currentView, "notes");
        AppController.currentView = savedView;
    }
}
