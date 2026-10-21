// Settings as one page with Tweaks merged in (APP-270): every section is on
// the page, the nav follows and drives the scroll, search finds a single
// setting inside a section, and the old Tweaks values (theme, density,
// reduced motion) read the same after the move.
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

    Item { id: host; anchors.fill: parent }

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

    function test_nav_scrolls_to_the_section_and_follows_the_scroll() {
        const sv = make();
        const fl = scroller(sv);
        tryVerify(() => fl.contentHeight > fl.height * 2, 3000, "the page is not one long page");
        verify(sv.openSection("data"));
        compare(sv.activeSection, "data");
        tryVerify(() => {
            const y = block(sv, "data").mapToItem(fl, 0, 0).y;
            return y >= -2 && y < fl.height / 2;
        }, 3000, "Data was not scrolled to the top");
        // A scroll by hand: the nav marks what is at the top.
        fl.contentY = 0;
        sv._navScroll = false;
        fl.contentY = 1;
        tryCompare(sv, "activeSection", "general");
    }

    function test_search_finds_a_setting_inside_a_section() {
        const sv = make();
        sv.searchText = I18n.t("settings.appearance.accent");
        verify(sv.searchMatches.some((m) => m.key === "settings.appearance.accent"));
        verify(block(sv, "appearance").visible);
        verify(!block(sv, "git").visible, "a section without a hit stays on the page");
        sv.searchText = "zzqxv-nothing";
        verify(sv.searchEmpty);
        sv.searchText = "";
        verify(block(sv, "git").visible);
    }

    // A5-26: the UI says "резервная копия" now, but "бэкап" (the word people
    // type) still finds the backups rows in Data, in either language.
    function test_search_finds_backups_by_the_old_word() {
        const sv = make();
        sv.searchText = "бэкап";
        verify(sv.searchMatches.some((m) => m.key === "settings.data.backups"), "бэкап finds nothing");
        verify(block(sv, "data").visible);
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
}
