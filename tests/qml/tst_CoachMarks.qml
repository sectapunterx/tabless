// One-time coach marks (CoachMark.qml, CoachMarkHost.qml, src/hints/
// CoachMarks.h): each card says what the real app does and names, any way
// out reads it, the host waits for a quiet window and shows one at a time,
// and none of them turns anything on.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "CoachMarks"
    when: windowShown
    visible: true
    width: 1000
    height: 800

    Item { id: host; anchors.fill: parent }

    function initTestCase() { AppController.setCoachMarksEnabled(true); }
    function cleanupTestCase() { AppController.setCoachMarksEnabled(false); }
    function init() {
        AppController.forgetCoachMarks("");
        // The host tests open windows of their own; shortcuts answer only in
        // the active one.
        tc.Window.window.requestActivate();
        tryVerify(function () { return tc.Window.active; }, 1000);
    }
    function cleanup() { AppController.forgetCoachMarks(""); }

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
    function anchorItem() {
        return createTemporaryQmlObject('import QtQuick; Rectangle { x: 100; y: 300; width: 240; height: 80 }', host);
    }
    function make(type, props) {
        const a = anchorItem();
        // A bare CoachMark needs its id when it is made (required).
        const id = type === "CoachMark" ? ' markId: "' + props.markId + '"' : "";
        const cm = createTemporaryQmlObject('import TodoCpp; ' + type + ' {' + id + ' }', a);
        cm.anchor = a;
        for (const k in props) if (k !== "markId") cm[k] = props[k];
        return cm;
    }
    function text(cm, name) { return find(cm.contentItem, name).text; }
    function press(cm, name) { find(cm.contentItem, name).clicked(); }
    function showAndRead(cm, id) {
        verify(AppController.offerCoachMark(id, cm.hint || {}));
        cm.open();
        tryVerify(function () { return cm.opened; }, 1000);
    }

    // ── The card itself ──

    function test_one_step_card_reads_on_its_button() {
        const cm = make("CoachMark", { markId: "sprint", title: "T", body: "B" });
        showAndRead(cm, "sprint");
        compare(text(cm, "coachmark-badge"), I18n.t("coach.new").toUpperCase());
        verify(!find(cm.contentItem, "coachmark-secondary").visible, "no second button unless given");
        compare(text(cm, "coachmark-primary"), I18n.t("coach.gotIt"));
        cm.primaryClicked.connect(function () { cm.finish(); });
        press(cm, "coachmark-primary");
        tryVerify(function () { return !cm.visible; }, 1000);
        verify(AppController.coachMarkSeen("sprint"));
        compare(Object.keys(AppController.coachMark).length, 0);
    }

    function test_escape_reads_it() {
        const cm = make("CoachMark", { markId: "conflict", title: "T" });
        showAndRead(cm, "conflict");
        cm.forceActiveFocus();
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return !cm.visible; }, 1000);
        verify(AppController.coachMarkSeen("conflict"));
    }

    // A card that takes no keys (quick capture, the delete toast) still goes
    // on Esc, and leaves the focus where it was.
    function test_without_focus_escape_still_reads_it() {
        const field = createTemporaryQmlObject('import QtQuick; TextInput { width: 100; height: 20 }', host);
        field.forceActiveFocus();
        const cm = make("CoachMark", { markId: "captureDates", title: "T", takesFocus: false });
        showAndRead(cm, "captureDates");
        verify(field.activeFocus, "the card took the caret");
        // A field's Esc is its own (quick capture closes, and the card with
        // it); with the keyboard anywhere else Esc reads the card.
        field.focus = false;
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return !cm.visible; }, 1000);
        verify(AppController.coachMarkSeen("captureDates"));
    }

    function test_right_side_flips_left_without_room() {
        const a = createTemporaryQmlObject('import QtQuick; Rectangle { x: 800; y: 100; width: 180; height: 60 }', host);
        const cm = createTemporaryQmlObject('import TodoCpp; CoachMark { markId: "sprint" }', a);
        cm.anchor = a;
        cm.open();
        tryVerify(function () { return cm.opened; }, 1000);
        verify(cm.x < 0, "opened off the right edge: " + cm.x);
        cm.close();
    }

    // ── The host ──

    Component {
        id: quietWindow
        ApplicationWindow {
            id: w
            width: 600; height: 400
            visible: true
            property bool coachMarksQuiet: false
            property bool coachMarksQuietInPanel: false
            property alias hostItem: h
            property alias anchorRect: r
            Rectangle { id: r; width: 100; height: 40 }
            CoachMarkHost {
                id: h
                markId: "weekKeys"
                sourceComponent: CoachMark { markId: "weekKeys"; anchor: r; title: "T" }
            }
        }
    }

    function test_host_waits_for_a_quiet_window() {
        const w = createTemporaryObject(quietWindow, tc);
        verify(AppController.offerCoachMark("weekKeys"));
        tryVerify(function () { return !!w.hostItem.item; }, 1000);
        wait(50);
        verify(!w.hostItem.item.opened, "opened while typing or over a modal");
        w.coachMarksQuiet = true;
        tryVerify(function () { return w.hostItem.item && w.hostItem.item.opened; }, 1000);
        verify(AppController.coachMark.shown);
        w.hostItem.item.close();
        tryVerify(function () { return AppController.coachMarkSeen("weekKeys"); }, 1000);
        verify(!AppController.offerCoachMark("sprint"), "a second one this session");
    }

    function test_host_closing_with_its_anchor_reads_it() {
        const w = createTemporaryObject(quietWindow, tc);
        w.coachMarksQuiet = true;
        w.hostItem.closesWithAnchor = true;
        verify(AppController.offerCoachMark("weekKeys"));
        tryVerify(function () { return w.hostItem.item && w.hostItem.item.opened; }, 1000);
        w.hostItem.when = false;
        tryVerify(function () { return AppController.coachMarkSeen("weekKeys"); }, 1000);
    }

    function test_host_hands_back_an_unshown_one_when_its_anchor_goes() {
        const w = createTemporaryObject(quietWindow, tc);
        verify(AppController.offerCoachMark("weekKeys"));
        tryVerify(function () { return !!w.hostItem.item; }, 1000);
        w.hostItem.when = false;
        tryVerify(function () { return Object.keys(AppController.coachMark).length === 0; }, 1000);
        verify(!AppController.coachMarkSeen("weekKeys"), "never shown, so never read");
    }

    // ── The eight ──

    function test_tracker_move_says_only_here_and_points_to_writing() {
        const cm = make("TrackerMoveCoachmark", { hint: { taskId: "jira-HT-5", provider: "jira", tracker: "Jira", remoteStatus: "To Do" } });
        showAndRead(cm, "trackerMove");
        compare(text(cm, "coachmark-title"), I18n.t("coach.trackerMove.title"));
        verify(text(cm, "coachmark-body").indexOf("To Do") >= 0, text(cm, "coachmark-body"));
        press(cm, "coachmark-secondary");
        compare(cm.step, 2);
        const body2 = text(cm, "coachmark-body");
        verify(body2.indexOf(I18n.t("settings.section.integrations.title")) >= 0, body2);
        verify(body2.indexOf(I18n.t("settings.integrations.writeStatus").arg("Jira")) >= 0, body2);
        compare(text(cm, "coachmark-primary"), I18n.t("coach.openSettings"));
        verify(!AppController.trackerWriteEnabled("jira"));
        cm.close();
    }

    function test_tracker_text_walks_from_the_text_to_my_notes() {
        const head = anchorItem();
        const notes = anchorItem();
        const cm = createTemporaryQmlObject('import TodoCpp; TrackerTextCoachmark {}', head);
        cm.headAnchor = head;
        cm.notesAnchor = notes;
        cm.hint = { taskId: "jira-HT-5", tracker: "Jira" };
        showAndRead(cm, "trackerText");
        compare(cm.anchor, head);
        compare(text(cm, "coachmark-title"), I18n.t("coach.trackerText.title").arg("Jira"));
        press(cm, "coachmark-primary");
        compare(cm.step, 2);
        compare(cm.anchor, notes);
        verify(text(cm, "coachmark-title").indexOf(I18n.t("taskdoc.myNotes")) >= 0);
        verify(text(cm, "coachmark-body").indexOf(I18n.t("local.notes.toNote")) >= 0);
        press(cm, "coachmark-primary");
        tryVerify(function () { return !cm.visible; }, 1000);
        verify(AppController.coachMarkSeen("trackerText"));
    }

    function test_branch_link_marks_the_key_and_names_the_switches() {
        const cm = make("BranchLinkCoachmark", { hint: { taskId: "jira-TEL-7", key: "TEL-7", branch: "fix/TEL-7-jobs", moved: true } });
        showAndRead(cm, "branchLink");
        verify(find(cm.contentItem, "coachmark-branch").visible);
        const body = text(cm, "coachmark-body");
        verify(body.indexOf("fix/TEL-7-jobs") >= 0 && body.indexOf(I18n.t("coach.branchLink.moved")) >= 0, body);
        press(cm, "coachmark-secondary");
        const body2 = text(cm, "coachmark-body");
        verify(body2.indexOf(I18n.t("settings.git.link")) >= 0, body2);
        verify(body2.indexOf(I18n.t("settings.git.autoMove")) >= 0 || I18n.lang === "ru", body2);
        cm.close();
    }

    function test_conflict_leaves_the_choice_to_the_compare_dialog() {
        const cm = make("ConflictCoachmark", { hint: { taskId: "jira-HT-5", field: "title", tracker: "Jira" } });
        let compared = 0;
        cm.compare.connect(function () { compared++; });
        showAndRead(cm, "conflict");
        verify(text(cm, "coachmark-body").indexOf(I18n.t("taskcard.mark.conflict").arg("Jira")) >= 0, text(cm, "coachmark-body"));
        compare(text(cm, "coachmark-secondary"), I18n.t("coach.later"));
        press(cm, "coachmark-primary");
        compare(compared, 1);
        tryVerify(function () { return !cm.visible; }, 1000);
        verify(AppController.coachMarkSeen("conflict"));
    }

    function test_week_keys_show_the_keys_as_bound() {
        const cm = make("WeekKeysCoachmark", {});
        showAndRead(cm, "weekKeys");
        compare(text(cm, "coachmark-badge"), I18n.t("coach.weekKeys.badge").toUpperCase());
        const body = text(cm, "coachmark-body");
        verify(body.indexOf(AppController.shortcutText("cal.today")) >= 0, body);
        verify(body.indexOf(I18n.lang === "ru" ? "Настройки → Клавиши" : "Settings → Keys") >= 0, body);
        verify(find(cm.contentItem, "coachmark-key-back").keys.length > 0);
        cm.close();
    }

    function test_time_machine_takes_no_keys_and_names_undo() {
        const cm = make("TimeMachineCoachmark", {});
        verify(!cm.takesFocus);
        let opened = 0;
        cm.openTimeMachine.connect(function () { opened++; });
        showAndRead(cm, "timeMachine");
        verify(text(cm, "coachmark-body").indexOf(AppController.shortcutText("undo")) >= 0);
        press(cm, "coachmark-secondary");
        compare(opened, 1);
        tryVerify(function () { return !cm.visible; }, 1000);
    }

    function test_sprint_offers_its_query_only_for_the_active_one() {
        const cm = make("SprintCoachmark", { hint: { name: "S 14", state: "active", end: new Date(2026, 9, 22), tasks: 7 } });
        showAndRead(cm, "sprint");
        verify(text(cm, "coachmark-title").indexOf("S 14") >= 0);
        verify(text(cm, "coachmark-body").indexOf("7") >= 0);
        verify(find(cm.contentItem, "coachmark-secondary").visible);
        verify(text(cm, "coachmark-secondary").indexOf("sprint:current") >= 0);
        cm.hint = { name: "S 15", state: "future", end: new Date(2026, 9, 29), tasks: 2 };
        verify(!find(cm.contentItem, "coachmark-secondary").visible);
        cm.close();
    }

    // ── 6A: keys, skip all, the key chip ──

    function test_enter_is_the_primary_button() {
        const cm = make("CoachMark", { markId: "sprint", title: "T" });
        let primary = 0;
        cm.primaryClicked.connect(function () { primary++; cm.finish(); });
        showAndRead(cm, "sprint");
        keyClick(Qt.Key_Return);
        compare(primary, 1);
        tryVerify(function () { return !cm.visible; }, 1000);
        verify(AppController.coachMarkSeen("sprint"));
    }

    function test_tab_walks_skip_all_and_the_buttons() {
        const cm = make("CoachMark", { markId: "sprint", title: "T", secondaryText: "Later" });
        showAndRead(cm, "sprint");
        const seen = [];
        for (let i = 0; i < 3; i++) {
            keyClick(Qt.Key_Tab);
            const f = cm.contentItem.Window.activeFocusItem;
            seen.push(f ? (f.objectName || (f.parent ? f.parent.objectName : "")) : "");
        }
        verify(seen.indexOf("coachmark-skipAll") >= 0, JSON.stringify(seen));
        verify(seen.indexOf("coachmark-secondary") >= 0, JSON.stringify(seen));
        verify(seen.indexOf("coachmark-primary") >= 0, JSON.stringify(seen));
        cm.close();
    }

    function test_skip_all_reads_every_hint_until_help_brings_them_back() {
        const cm = make("CoachMark", { markId: "conflict", title: "T" });
        showAndRead(cm, "conflict");
        find(cm.contentItem, "coachmark-skipAll").activate();
        tryVerify(function () { return !cm.visible; }, 1000);
        verify(AppController.coachMarksSkipped());
        verify(AppController.coachMarkSeen("ciPanel"), "one never shown is read too");
        AppController.noteHintVisit("settings");
        verify(!AppController.offerCoachMark("weekKeys"));
        AppController.forgetCoachMarks("");   // Help → Show hints again
        verify(!AppController.coachMarksSkipped());
        verify(AppController.offerCoachMark("weekKeys"));
    }

    function test_key_chip_only_when_given() {
        const cm = make("CoachMark", { markId: "sprint", title: "T" });
        showAndRead(cm, "sprint");
        verify(!find(cm.contentItem, "coachmark-keys").visible);
        cm.keys = "Ctrl+K";
        cm.keysCaption = "anywhere";
        verify(find(cm.contentItem, "coachmark-keys").visible);
        compare(text(cm, "coachmark-keys-caption"), "anywhere");
        cm.close();
    }

    // ── Feature hints (FeatureHintHost) ──

    Component {
        id: featureWindow
        ApplicationWindow {
            id: fw
            width: 900; height: 600
            visible: true
            property bool coachMarksQuiet: true
            property bool coachMarksQuietInPanel: true
            property var commands: []
            property var shown: []
            property var _navBack: []
            function runCommand(id) { fw.commands = fw.commands.concat([id]); }
            function showTask(t) { fw.shown = fw.shown.concat([t.id]); }
            function navStep(d) { fw.commands = fw.commands.concat(["nav" + d]); }
            property alias field: fld
            property alias card: cardItem
            Rectangle {
                id: cardItem
                x: 40; y: 80; width: 260; height: 70
                property var task: ({ id: "PAY-412", title: "Retry webhooks" })
                Rectangle { objectName: "tc-ci"; x: 8; y: 44; width: 30; height: 16 }
            }
            Rectangle { objectName: "view-header-title"; x: 40; y: 10; width: 200; height: 30 }
            TextInput { id: fld; x: 400; y: 10; width: 200; height: 24 }
            FeatureHintHost { win: fw }
        }
    }

    function featureCard(w) {
        const kids = w.Overlay.overlay ? w.Overlay.overlay.children : [];
        for (let i = 0; i < kids.length; i++) {
            const c = find(kids[i], "coachmark-title");
            if (c && c.visible) return kids[i];
        }
        return null;
    }
    // Every setting the app keeps, without the hints' own bookkeeping.
    function snapshot() {
        function clean(o) {
            if (!o || typeof o !== "object") return o;
            const r = Array.isArray(o) ? [] : {};
            for (const k in o) if (!/^hint|^featureUse$/.test(k)) r[k] = clean(o[k]);
            return r;
        }
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}"); } catch (e) { s = { raw: AppController.appSettingsJson }; }
        return JSON.stringify({ s: clean(s), safety: AppController.safety, theme: AppController.theme,
                                view: AppController.currentView });
    }

    // The new CI card stands on the CI mark of a real card; "Try it" opens
    // that card once and changes no setting.
    function test_feature_hint_tries_the_thing_once_and_changes_no_setting() {
        const w = createTemporaryObject(featureWindow, tc);
        w.requestActivate();
        tryVerify(function () { return w.active; }, 1000);
        const before = snapshot();
        verify(AppController.offerCoachMark("ciPanel"));
        let card = null;
        tryVerify(function () { card = featureCard(w); return card !== null; }, 4000);
        compare(text(card, "coachmark-title"), I18n.t("coach.f.ciPanel.title"));
        compare(text(card, "coachmark-badge"), I18n.t("coach.f.newUnique").toUpperCase());
        verify(find(card, "coachmark-keys").visible, "the key chip");
        compare(text(card, "coachmark-primary"), I18n.t("coach.f.try"));
        compare(text(card, "coachmark-secondary"), I18n.t("coach.later"));
        find(card, "coachmark-primary").clicked();
        tryVerify(function () { return w.shown.length === 1; }, 1000);
        compare(w.shown[0], "PAY-412");
        tryVerify(function () { return AppController.coachMarkSeen("ciPanel"); }, 1000);
        wait(100);
        compare(w.shown.length, 1, "once");
        compare(snapshot(), before, "a setting changed");
    }

    // Every feature hint's "Try it" runs a command, never a setting: the
    // standup draft opens once though Settings → Safety net keeps it off.
    function test_every_try_is_a_command_not_a_setting() {
        const w = createTemporaryObject(featureWindow, tc);
        w.requestActivate();
        tryVerify(function () { return w.active; }, 1000);
        const safetyBefore = JSON.stringify(AppController.safety);
        const ids = ["standupDraft", "endOfDay", "weekRecap", "timeMachineOpen", "savedViews", "quickCapture",
                     "commandLine", "eventLog", "cheatSheet"];
        for (let i = 0; i < ids.length; i++) {
            AppController.forgetCoachMarks("");
            verify(AppController.offerCoachMark(ids[i]), ids[i]);
            let card = null;
            tryVerify(function () { card = featureCard(w); return card !== null; }, 4000, ids[i]);
            verify(text(card, "coachmark-title").length > 0 && text(card, "coachmark-title").indexOf("coach.f.") < 0, ids[i]);
            find(card, "coachmark-primary").clicked();
            tryVerify(function () { return w.commands.length === i + 1; }, 1000, ids[i]);
            tryVerify(function () { return featureCard(w) === null; }, 1000, ids[i]);
        }
        compare(w.commands, ["standup.draft", "endOfDay.open", "recap.open", "timeMachine.open", "savedview.save",
                             "quick-capture", "palette.open", "log.open", "hotkeys.open"]);
        compare(JSON.stringify(AppController.safety), safetyBefore);
    }

    // "Later": not read, not again today.
    function test_later_leaves_it_unread() {
        const w = createTemporaryObject(featureWindow, tc);
        verify(AppController.offerCoachMark("cheatSheet"));
        let card = null;
        tryVerify(function () { card = featureCard(w); return card !== null; }, 4000);
        find(card, "coachmark-secondary").clicked();
        tryVerify(function () { return featureCard(w) === null; }, 1000);
        verify(!AppController.coachMarkSeen("cheatSheet"));
        AppController.noteHintVisit("settings");
        verify(!AppController.offerCoachMark("cheatSheet"), "again today");
        compare(w.commands.length, 0, "Later ran nothing");
    }

    // No anchor on screen: back unread, to come on a later visit.
    function test_feature_hint_without_its_anchor_goes_back_unread() {
        const w = createTemporaryObject(featureWindow, tc);
        verify(AppController.offerCoachMark("notesRegions"));
        tryVerify(function () { return Object.keys(AppController.coachMark).length === 0; }, 4000);
        verify(!AppController.coachMarkSeen("notesRegions"));
        verify(featureCard(w) === null);
    }

    // ── Words ──

    function test_every_string_in_both_languages_with_the_same_placeholders() {
        const en = I18n.dict.en, ru = I18n.dict.ru;
        let n = 0;
        for (const k in en) {
            if (k.indexOf("coach.") !== 0) continue;
            n++;
            verify(ru[k] !== undefined, k + " missing in ru");
            for (let i = 1; i <= 3; i++)
                compare(ru[k].indexOf("%" + i) >= 0, en[k].indexOf("%" + i) >= 0, k + " %" + i);
        }
        verify(n > 30, "coach.* strings: " + n);
    }
}
