// Settings with Tweaks merged in (APP-270), one section on screen at a time
// (dogfood 2026-10-21): the nav switches the page, the keyboard goes into a
// page and back, search finds a single setting in any section and opens its
// page and fold, labels keep their one line, and the old Tweaks values
// (theme, density, reduced motion) read the same after the move.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SettingsPage"
    when: windowShown
    visible: true
    width: 1000
    height: 640

    Item {
        id: host
        anchors.fill: parent
        // Keys that got past the page (and a modal over it).
        property int leaked: 0
        Keys.onPressed: (event) => host.leaked++
    }

    property string _saved: ""
    property string _theme: ""
    property string _density: ""
    function initTestCase() {
        _saved = AppController.appSettingsJson;
        _theme = AppController.theme;
        _density = AppController.density;
    }
    function cleanup() {
        AppController.appSettingsJson = _saved;
        AppController.theme = _theme;
        AppController.density = _density;
    }

    function make() {
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        verify(sv !== null);
        // Sections past the first screen are built a moment later; these
        // cases read them at once.
        sv.loadAllSections();
        return sv;
    }
    function block(sv, id) { return findChild(sv, "settings-block-" + id); }
    function scroller(sv) {
        let fl = block(sv, "appearance").parent;
        while (fl && fl.contentY === undefined) fl = fl.parent;
        return fl;
    }

    function test_every_section_is_on_one_page_general_first() {
        const sv = make();
        // Основное, then the look (mockup 5A).
        compare(sv.sections[0].id, "general");
        compare(sv.sections[1].id, "appearance");
        verify(sv.sections.every((s) => s.id !== "cpp"), "the unused C++ stub is still listed");
        for (let i = 0; i < sv.sections.length; i++)
            verify(block(sv, sv.sections[i].id) !== null, "no block for " + sv.sections[i].id);
        // The page lists them in nav order.
        const order = sv._blocks().map((b) => b.sectionId);
        compare(order.join(","), sv.sections.map((s) => s.id).join(","));
        // No profile section (DG-101): the sheets have none.
        verify(sv.sections.findIndex((s) => s.id === "profile") < 0);
    }

    function visibleBlocks(sv) { return sv._blocks().filter((b) => b.visible).map((b) => b.sectionId); }
    function isInside(item, ancestor) {
        for (let p = item; p; p = p.parent)
            if (p === ancestor) return true;
        return false;
    }

    // Dogfood 2026-10-21: one section per screen, not one long scroll.
    function test_one_section_on_screen_at_a_time() {
        const sv = make();
        compare(visibleBlocks(sv).join(","), "general");
        for (let i = 0; i < sv.sections.length; i++) {
            const id = sv.sections[i].id;
            sv.activeSection = id;
            compare(visibleBlocks(sv).join(","), id);
        }
    }

    function test_nav_switches_the_page_from_its_top() {
        const sv = make();
        const fl = scroller(sv);
        sv.activeSection = "appearance";
        tryVerify(() => fl.contentHeight > fl.height, 3000, "Внешний вид fits one screen: nothing to scroll");
        fl.contentY = 200;
        mouseClick(findChild(sv, "settings-nav-calendar"));
        compare(sv.activeSection, "calendar");
        compare(visibleBlocks(sv).join(","), "calendar");
        compare(fl.contentY, 0, "a new page opened scrolled down");
        // Help and About under the list switch it too.
        mouseClick(findChild(sv, "settings-nav-about-click"));
        compare(sv.activeSection, "about");
        compare(visibleBlocks(sv).join(","), "about");
    }

    // ↑/↓ in the list switch the page, Enter or → goes into it, Esc comes
    // back to the list; "/" and Ctrl+F go to the search.
    function test_keys_walk_the_list_into_a_page_and_back() {
        const sv = make();
        const search = findChild(sv, "settings-search");
        search.forceActiveFocus();
        keyClick(Qt.Key_Down);
        verify(findChild(sv, "settings-nav-general").activeFocus, "↓ from the search did not reach the list");
        keyClick(Qt.Key_Down);
        compare(sv.activeSection, "appearance");
        const page = block(sv, "appearance");
        keyClick(Qt.Key_Right);
        verify(isInside(sv.Window.activeFocusItem, page), "→ did not go into the page");
        keyClick(Qt.Key_Escape);
        verify(findChild(sv, "settings-nav-appearance").activeFocus, "Esc did not come back to the list");
        keyClick(Qt.Key_Return);
        verify(isInside(sv.Window.activeFocusItem, page), "Enter did not go into the page");
        keyClick("/");
        verify(search.activeFocus, "/ in a page did not reach the search");
        compare(search.text, "");
        findChild(sv, "settings-nav-appearance").forceActiveFocus();
        keyClick(Qt.Key_F, Qt.ControlModifier);
        verify(search.activeFocus, "Ctrl+F did not reach the search");
        // The page of About, under the list, the same way.
        sv.activeSection = "about";
        findChild(sv, "settings-nav-about-click").forceActiveFocus();
        keyClick(Qt.Key_Return);
        verify(isInside(sv.Window.activeFocusItem, block(sv, "about")), "Enter on About did not go into its page");
        keyClick(Qt.Key_Escape);
        verify(findChild(sv, "settings-nav-about-click").activeFocus, "Esc did not come back to About");
    }

    // A search finds a setting in any section: the page follows the search,
    // Enter lands on the row, and its fold opens for it and stays open.
    function test_search_jumps_to_the_page_and_opens_its_fold() {
        const sv = make();
        compare(sv.activeSection, "general");
        const text = I18n.t("settings.notif.snoozeShort");
        const search = findChild(sv, "settings-search");
        search.forceActiveFocus();
        sv.searchText = text;
        tryCompare(sv, "activeSection", "notifications");
        compare(visibleBlocks(sv).join(","), "notifications");
        keyClick(Qt.Key_Return);
        const row = findChild(sv, "settings-snooze-short");
        tryVerify(() => row.visible, 2000, "the row in the fold is not on screen");
        tryVerify(() => isInside(sv.Window.activeFocusItem, row), 4000, "focus is not on the setting");
        sv.searchText = "";
        verify(row.visible, "the fold closed under the setting just found");
        compare(visibleBlocks(sv).join(","), "notifications");
        // The fold says what is inside and how much.
        const head = findChild(block(sv, "notifications"), "settings-more-text");
        verify(head.text.indexOf(I18n.t("settings.more.sum.notifications")) === 0, head.text);
    }

    // The label column broke "Все клавиши", "С чего начать", "Рабочие часы"
    // word by word after the "?" came (dogfood 2026-10-21): a short label
    // keeps its one line, and the "?" sits right after its words.
    function checkLabels(sv, section, names) {
        sv.activeSection = section;
        for (let i = 0; i < names.length; i++) {
            const row = findChild(sv, names[i]);
            verify(row !== null, names[i]);
            if (!row.visible) continue;
            const label = findChild(row, "settings-row-label");
            tryVerify(() => row.width > 0 && label.width > 0, 2000, names[i] + " not laid out");
            // Once the page has settled (a page just switched lays out over
            // a frame or two).
            tryCompare(label, "lineCount", 1, 2000, names[i] + " «" + row.label + "» wraps at " + Theme.scale);
            verify(label.width >= label.implicitWidth - 1, names[i] + " is squeezed: " + label.width + " < " + label.implicitWidth);
            const tip = findChild(row, "settings-tip-button");
            if (tip) {
                const gap = tip.mapToItem(row, 0, 0).x - (label.mapToItem(row, 0, 0).x + label.width);
                verify(gap >= 0 && gap <= Theme.spSm + 2, names[i] + ": the ? is " + gap + "px from its label");
            }
        }
    }
    function setScale(s) {
        const o = JSON.parse(tc._saved || "{}");
        o.appearance = Object.assign({}, o.appearance || {}, { uiScale: s });
        AppController.appSettingsJson = JSON.stringify(o);
        compare(Theme.scale, s);
    }
    function test_short_labels_keep_one_line_data() {
        return [ { tag: "100%", scale: 1.0 }, { tag: "150%", scale: 1.5 } ];
    }
    function test_short_labels_keep_one_line(data) {
        setScale(data.scale);
        const sv = make();
        checkLabels(sv, "help", ["settings-help-keys", "settings-help-start", "settings-help-hints-mode", "settings-help-hints",
                                 "settings-help-hints-list", "settings-report-issue"]);
        checkLabels(sv, "about", ["settings-whats-new", "settings-feature-build", "settings-updates"]);
        checkLabels(sv, "calendar", ["settings-work-hours", "settings-workdays", "settings-lunch", "settings-week-start"]);
        checkLabels(sv, "general", ["settings-general-work-hours", "settings-general-lunch"]);
        checkLabels(sv, "notifications", ["settings-notif-meetings", "settings-ci-failed", "settings-quiet-hours"]);
    }

    // The guide's reader is a modal layer: opened with the mouse, the keys
    // typed are the reader's, never the page's or the app's behind it (a
    // typed "t" said "t теперь — таймер", dogfood 2026-10-21).
    function test_help_reader_holds_the_keyboard() {
        const sv = make();
        sv.activeSection = "help";
        // The keyboard sits on the page, as after any click on it.
        findChild(sv, "settings-nav-help-click").forceActiveFocus();
        host.leaked = 0;
        const open = findChild(sv, "settings-help-start-open-click");
        verify(open !== null);
        mouseClick(open);
        const reader = findChild(sv, "settings-help-reader");
        tryCompare(reader, "opened", true);
        tryVerify(() => isInside(sv.Window.activeFocusItem, reader.contentItem), 2000, "the guide did not take the keyboard");
        keyClick(Qt.Key_T);
        keyClick(Qt.Key_G);
        keyClick(Qt.Key_B);
        compare(host.leaked, 0, "keys went past the reader");
        verify(reader.visible);
        keyClick(Qt.Key_Escape);
        tryCompare(reader, "visible", false);
    }

    function test_search_finds_a_setting_inside_a_section() {
        const sv = make();
        sv.searchText = I18n.t("settings.appearance.accent");
        verify(sv.searchMatches.some((m) => m.key === "settings.appearance.accent"));
        // The page follows the search to the section that has it.
        tryVerify(() => block(sv, "appearance").visible, 2000, "the page did not follow the search");
        verify(!findChild(sv, "settings-nav-git").visible, "a section without a hit stays in the list");
        sv.searchText = "zzqxv-nothing";
        verify(sv.searchEmpty);
        sv.searchText = "";
        verify(findChild(sv, "settings-nav-git").visible);
        verify(block(sv, "appearance").visible, "clearing the search moved the page");
    }

    // A5-26: the UI says "резервная копия" now, but "бэкап" (the word people
    // type) still finds the backups rows in Data, in either language.
    function test_search_finds_backups_by_the_old_word() {
        const sv = make();
        sv.searchText = "бэкап";
        verify(sv.searchMatches.some((m) => m.key === "settings.data.backups"), "бэкап finds nothing");
        tryVerify(() => block(sv, "data").visible, 2000, "the page did not follow the search");
        verify(!sv.searchEmpty);
        sv.searchText = "";
    }

    function test_old_tweaks_values_read_the_same() {
        AppController.appSettingsJson = JSON.stringify({ appearance: { reducedMotion: true, cursorColor: "#d97706" } });
        AppController.theme = "light";
        AppController.density = "compact";
        const sv = make();
        compare(findChild(sv, "settings-theme").value, "light");
        compare(findChild(sv, "settings-density").value, "compact");
        compare(findChild(sv, "settings-motion").value, "min");
        // A swatch picked before is kept: the accent reads "custom", the
        // swatch row still shows it.
        compare(Theme.accentTone, "custom");
        compare(findChild(sv, "settings-cursor-color").value, "#d97706");
    }

    function test_theme_accent_density_motion_apply_at_once() {
        const sv = make();
        findChild(sv, "settings-theme").selected("system");
        compare(AppController.theme, "system");
        verify(Theme.slot === "dark" || Theme.slot === "light");
        findChild(sv, "settings-accent").selected("ink");
        compare(Theme.accentTone, "ink");
        verify(Theme.cursorColorPick.length > 0);
        findChild(sv, "settings-accent").selected("lavender");
        compare(Theme.accentTone, "lavender");
        compare(Theme.cursorColorPick, Theme.dark ? "#b1a7f0" : "#5a4fb3", "lavender is the logo's, on any theme");
        findChild(sv, "settings-density").selected("spacious");
        compare(AppController.density, "spacious");
        verify(Theme.spacious);
        findChild(sv, "settings-motion").selected("min");
        verify(Theme.reducedMotion);
        findChild(sv, "settings-motion").selected("full");
        verify(!Theme.reducedMotion);
    }

    function test_style_switch_and_custom() {
        const sv = make();
        sv.activeSection = "appearance";
        findChild(sv, "settings-style").picked("quiet");
        compare(Style.name, "quiet");
        findChild(sv, "settings-style").picked("bold");
        compare(Style.name, "bold");
        const reset = findChild(sv, "settings-style-reset");
        verify(!reset.visible);
        findChild(sv, "settings-style-counters").toggled(false);
        compare(Style.name, "custom");
        verify(reset.visible, "no way back from Custom");
        // Тихий / Насыщенный / Свой, always (DG-098); Свой only says so.
        compare(findChild(sv, "settings-style").options.length, 3);
        findChild(sv, "settings-style").picked("custom");
        compare(Style.name, "custom");
        reset.activated();
        compare(Style.name, "bold");
    }

    // Справка → Подсказки (dogfood 2026-10-22, Hints.dc.html): "Порциями ·
    // Все сразу · Не показывать", "прочитано X из Y" with "Сбросить", and
    // every hint as a list — its text, and its place when it has a view.
    function test_hints_mode_counter_and_the_list() {
        const was = AppController.hintsMode;
        const sv = make();
        sv.activeSection = "help";
        const mode = findChild(sv, "settings-help-hints-mode");
        verify(mode !== null);
        findChild(sv, "settings-help-hints-mode-all-click").activated();
        compare(AppController.hintsMode, "all");
        compare(mode.status, I18n.t("settings.help.hintsMode.allHint"));
        findChild(sv, "settings-help-hints-mode-off-click").activated();
        compare(AppController.hintsMode, "off");
        compare(mode.status, I18n.t("settings.help.hintsMode.offHint"));
        verify(findChild(sv, "settings-help-hints-mode-off").sel, "the picked one is marked");
        findChild(sv, "settings-help-hints-mode-batch-click").activated();
        compare(AppController.hintsMode, "batch");

        const reset = findChild(sv, "settings-help-hints");
        compare(reset.status, I18n.t("settings.help.hintsRead").arg(AppController.coachMarksRead).arg(AppController.coachMarkCount));

        findChild(sv, "settings-help-hints-list-open-click").activated();
        const list = findChild(sv, "hints-list");
        verify(list !== null);
        tryVerify(() => list.opened, 1000, "the list did not open");
        compare(list.items.length, AppController.coachMarkCount, "every hint is listed");
        const titles = list.items.map(m => m.title);
        verify(titles.indexOf(I18n.t("coach.waits.title1")) >= 0, "the sidebar's hint is listed");
        verify(titles.every(t => t.length > 0 && t.indexOf("coach.") !== 0), "a title is missing: " + titles.join(" | "));
        verify(list.items.every(m => m.body.length > 0 && m.body.indexOf("coach.") !== 0), "a text is missing");
        tryVerify(() => findChild(list.contentItem, "hints-list-row-waitsIntro") !== null, 1000);
        findChild(findChild(list.contentItem, "hints-list-row-waitsIntro"), "hints-list-open").activated();
        compare(list.openId, "waitsIntro");
        compare(findChild(list.contentItem, "hints-list-title").text, I18n.t("coach.waits.title1"));
        verify(findChild(list.contentItem, "hints-list-body").text.indexOf(I18n.t("coach.waits.body1")) === 0);
        verify(!findChild(list.contentItem, "hints-list-go").visible, "the sidebar has no view to go to");
        findChild(list.contentItem, "hints-list-back").clicked();
        compare(list.openId, "");
        list.show("ciRed");
        verify(findChild(list.contentItem, "hints-list-go").visible, "the board is a place to go");
        list.close();
        tryVerify(() => !list.visible, 1000);
        AppController.hintsMode = was;
    }
}
