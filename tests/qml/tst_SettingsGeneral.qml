// Settings that open at once and read at a glance (dogfood 2026-10-20,
// mockup 5A): Основное first with the most-changed settings, the
// explanations behind a "?" that the keyboard opens too, rarely used rows
// folded under "Тонкая настройка" (a search or a reveal opens the fold), and a
// first frame within budget because only the sections in view are built.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SettingsGeneral"
    when: windowShown
    visible: true
    width: 1100
    height: 720

    Item { id: host; anchors.fill: parent }

    property string _saved: ""
    function initTestCase() { _saved = AppController.appSettingsJson; }
    function cleanup() { AppController.appSettingsJson = _saved; }

    function make() {
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        verify(sv !== null);
        return sv;
    }
    function block(sv, id) { return findChild(sv, "settings-block-" + id); }
    function inside(item, ancestor) {
        for (let p = item; p; p = p.parent)
            if (p === ancestor) return true;
        return false;
    }
    function rowByLabel(scope, text) {
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
        return walk(scope);
    }
    function foldOf(item) {
        for (let p = item; p; p = p.parent)
            if (p.foldCount !== undefined && p.forced !== undefined) return p;
        return null;
    }

    // Основное is the first section and the page opens on it: the four
    // cards and the working day, in the first frame.
    function test_general_renders_first() {
        const sv = make();
        compare(sv.sections[0].id, "general");
        compare(sv.activeSection, "general");
        const general = block(sv, "general");
        verify(general.loaded, "Основное was not built for the first frame");
        for (const name of ["settings-general-theme-card", "settings-general-keymap-card",
                            "settings-general-notif-card", "settings-general-trackers-card", "settings-general-workday"])
            verify(findChild(general, name) !== null && findChild(general, name).visible, name);
        // Two cards side by side on a wide page.
        const theme = findChild(general, "settings-general-theme-card");
        const keymap = findChild(general, "settings-general-keymap-card");
        tryVerify(() => Math.abs(theme.y - keymap.y) < 1 && keymap.x > theme.x + theme.width - 1, 2000, "cards are not two-up");
        verify(Math.abs(theme.width - keymap.width) <= 1, "the halves differ: " + theme.width + " / " + keymap.width);
    }

    // The cards change the settings they name, and say the state back.
    function test_general_cards_act() {
        const savedTheme = AppController.theme;
        const savedScheme = AppController.keymapScheme;
        const sv = make();
        findChild(sv, "settings-general-theme-light-click").activated();
        compare(AppController.theme, "light");
        findChild(sv, "settings-general-keymap-classic-click").activated();
        compare(AppController.keymapScheme, "classic");
        AppController.keymapScheme = savedScheme;
        AppController.theme = savedTheme;

        // Notifications: the switch is the system notifications; the line
        // says what reminds and whether it sounds.
        sv.set("notifications", "desktopNotif", true);
        sv.set("sound", "enabled", false);
        const line = findChild(sv, "settings-general-notif-line");
        verify(line.text.indexOf(I18n.t("settings.general.notif.soundOff")) >= 0, line.text);
        findChild(sv, "settings-general-notif-click").activated();
        compare(sv.settings.notifications.desktopNotif, false);
        compare(line.text, I18n.t("settings.general.notif.off"));

        // Trackers: a connected one is a chip, the list says how many.
        const s = JSON.parse(AppController.appSettingsJson || "{}");
        s.integrations = Object.assign({}, s.integrations || {}, { redmine: { connected: true } });
        AppController.appSettingsJson = JSON.stringify(s);
        tryVerify(() => findChild(sv, "settings-general-tracker-redmine") !== null, 1000, "no chip for a connected tracker");
        const nav = findChild(sv, "settings-nav-connected-integrations");
        verify(nav.visible);
        verify(nav.text.indexOf(String(sv.connectedTrackers)) >= 0);
        findChild(sv, "settings-general-connect-click").activated();
        compare(sv.activeSection, "integrations");
    }

    // The explanation waits behind "?": Enter on the "?" opens it, F1 on the
    // row toggles it, Esc closes it — no mouse needed.
    function test_tip_toggles_by_keyboard() {
        const sv = make();
        const row = findChild(sv, "settings-general-lunch");
        verify(row.hint.length > 0);
        compare(row.tipOpen, false);
        verify(findChild(row, "settings-tip") === null, "the explanation shows at rest");
        const button = findChild(row, "settings-tip-click");
        verify(button.activeFocusOnTab, "the ? is not on the Tab path");
        button.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Return);
        compare(row.tipOpen, true);
        const tip = findChild(row, "settings-tip");
        verify(tip !== null && tip.visible, "no explanation under the row");
        verify(row.height > tip.height, "the row did not make room for the tip");
        keyClick(Qt.Key_Escape);
        compare(row.tipOpen, false, "Esc did not close the tip");

        // F1 from the row itself (a switch row takes focus).
        row.forceActiveFocus(Qt.TabFocusReason);
        const was = row.checked;
        keyClick(Qt.Key_F1);
        compare(row.tipOpen, true, "F1 did not open the tip");
        compare(row.checked, was, "F1 toggled the switch");
        keyClick(Qt.Key_F1);
        compare(row.tipOpen, false);
        // A row with no explanation has no "?".
        const noHint = findChild(sv, "settings-general-week-start");
        verify(findChild(noHint, "settings-tip-button") === null);
    }

    // A rarely used row is folded; a search for it opens the fold and Enter
    // scrolls to it.
    function test_fold_expands_on_search_hit() {
        const sv = make();
        sv.loadAllSections();
        const text = I18n.t("settings.notif.snoozeShort");
        const row = findChild(sv, "settings-snooze-short");
        const fold = foldOf(row);
        verify(fold !== null, "the snooze row is not folded");
        fold.userOpen = false;
        verify(!fold.open && !row.visible, "the folded row shows at rest");
        sv.searchText = text;
        verify(fold.open, "the search left the fold closed");
        // The page follows the search to the section that has it.
        tryVerify(() => row.visible, 2000, "the page did not follow the search");
        const field = findChild(sv, "settings-search");
        field.forceActiveFocus();
        keyClick(Qt.Key_Return);
        compare(sv.activeSection, "notifications");
        tryVerify(() => {
            let fl = row.parent;
            while (fl && fl.contentY === undefined) fl = fl.parent;
            const y = row.mapToItem(fl, 0, 0).y;
            return y >= 0 && y + row.height <= fl.height;
        }, 4000, "the folded setting was not scrolled into view");
        sv.searchText = "";
    }

    // Ctrl K → a folded setting: the fold opens for it, without a search.
    function test_reveal_opens_the_fold() {
        const sv = make();
        verify(sv.revealItem({ section: "calendar", key: "settings.cal.snap" }));
        let row = null;
        tryVerify(() => { row = rowByLabel(block(sv, "calendar"), I18n.t("settings.cal.snap")); return row !== null && row.visible; },
                  4000, "the folded row was not shown");
        verify(foldOf(row).open);
    }

    // The fold says how many settings it holds, counting only the rows that
    // can show; open or folded is remembered for the section.
    function test_fold_count_and_memory() {
        const sv = make();
        sv.loadAllSections();
        sv.activeSection = "notifications";
        const fold = foldOf(findChild(sv, "settings-snooze-short"));
        tryVerify(() => fold.foldCount > 0, 1000);
        const text = findChild(fold, "settings-more-text");
        // "Отложить, мелодии, сводки — 7 настроек": what is inside, how much.
        compare(text.text, I18n.t("settings.more.sum.notifications") + " — " + I18n.count(fold.foldCount, "settings.more.n"));
        // The volume rows hide while the sound is off: not counted.
        sv.set("sound", "enabled", false);
        tryVerify(() => fold.foldCount > 0, 1000);
        const off = fold.foldCount;
        sv.set("sound", "enabled", true);
        tryVerify(() => fold.foldCount > off, 1000, "rows shown by the sound switch are not counted");
        sv.set("sound", "enabled", false);
        tryCompare(fold, "foldCount", off);

        findChild(fold, "settings-more-click").activated();
        verify(fold.open);
        compare(sv.settings.settingsFolds.notifications, true);
        const again = make();
        again.loadAllSections();
        verify(foldOf(findChild(again, "settings-snooze-short")).open, "the open fold was not remembered");
        findChild(foldOf(findChild(again, "settings-snooze-short")), "settings-more-click").activated();
        verify(!(again.settings.settingsFolds || {}).notifications);
    }

    // Opening Settings: only what is in view is built before the first
    // frame (generous budget offscreen); the rest follows by itself.
    function test_open_time_budget() {
        // A first build pays for compiling the types; the budget is for an
        // ordinary open.
        make().destroy();
        wait(50);
        const t0 = Date.now();
        const sv = make();
        waitForRendering(sv, 3000);
        const took = Date.now() - t0;
        verify(took < 300, "first frame took " + took + " ms");
        const blocks = sv._blocks();
        verify(blocks.some((b) => !b.loaded), "every section was built before the first frame");
        tryVerify(() => sv._blocks().every((b) => b.loaded), 10000, "the other sections never arrived");
        // The background build is a section at a time, each whole: on Qt 6.9
        // (the Linux and macOS builds) a section still incubating in an
        // asynchronous Loader when Settings was left, searched or closed
        // corrupted the heap.
        for (const b of blocks) {
            const loader = findChild(b, "settings-block-loader-" + b.sectionId);
            verify(loader !== null && !loader.asynchronous, b.sectionId + " is built asynchronously");
        }
        // The search index waits for a search.
        compare(sv.searchIndex.length, 0);
        sv.searchText = "x";
        verify(sv.searchIndex.length > 50);
        sv.searchText = "";
    }

    // Settings closed, searched or jumped in while the other sections are
    // still being built: each is whole or not there, and nothing is left
    // half-made to crash on (Qt 6.9, see test_open_time_budget).
    function test_closed_while_the_sections_are_still_built() {
        for (let k = 0; k < 4; k++) {
            const sv = make();
            wait(k * 25);
            if (k === 1) sv.searchText = "a";
            if (k === 2) sv.activeSection = sv.sections[sv.sections.length - 1].id;
            for (const b of sv._blocks())
                if (b.loaded) verify(findChild(b, "settings-block-loader-" + b.sectionId).item !== null, b.sectionId + " half built");
            sv.destroy();
        }
        gc();
        wait(100);
        const sv = make();
        tryVerify(() => sv._blocks().every((b) => b.loaded), 10000, "the sections never arrived after a close");
    }

    // Help and About sit under the list and open their sections.
    function test_help_and_about_under_the_list() {
        const sv = make();
        const help = findChild(sv, "settings-nav-help-click");
        verify(help !== null && help.visible);
        help.activated();
        compare(sv.activeSection, "help");
        findChild(sv, "settings-nav-about-click").activated();
        compare(sv.activeSection, "about");
        verify(findChild(sv, "settings-search-slash").visible, "no / on the search");
    }
}
