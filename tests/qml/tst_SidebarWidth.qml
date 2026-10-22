// The sidebar's edge (dogfood 2026-10-22, SidebarWidth sheet): dragged for
// a width of 160–320, let go under 140 it folds into the rail, from the rail
// it opens again, a double click goes back to 208, ← / → from the keyboard.
// Narrow (under 190) the rows drop their keys and the blocks shorten.
// The sidebar on its own here; Main's side (persisting, the auto-fold) is in
// tst_SmallScreenLayout.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SidebarWidth"
    when: windowShown
    visible: true
    width: 600
    height: 720

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""
    function init() { tc.savedSettings = AppController.appSettingsJson; }
    function cleanup() { AppController.appSettingsJson = tc.savedSettings; }

    // A sidebar that keeps what the edge says, as Main does.
    function bar(expanded, setting) {
        const sb = createTemporaryQmlObject('import TodoCpp\nSidebar {\n'
            + ' height: 700\n expanded: ' + (expanded ? "true" : "false") + '\n widthSetting: ' + (setting || 0) + '\n'
            + ' property var chosen: []\n property int rails: 0\n property int resets: 0\n'
            + ' onWidthChosen: (w) => { chosen = chosen.concat([w]); widthSetting = w; expanded = true; }\n'
            + ' onRailChosen: { rails++; expanded = false; }\n'
            + ' onWidthReset: { resets++; widthSetting = 0; expanded = true; }\n'
            + '}', host);
        verify(sb !== null);
        tryCompare(sb, "width", sb.implicitWidth);
        wait(Theme.durMove + 50);
        return sb;
    }
    function edgeX(sb) { return sb.width - Theme.px(2); }
    // Press on the edge, move in steps (the host does not move), let go.
    function settled(sb) {
        tryVerify(function () { return Math.abs(sb.width - (sb.expanded ? sb.expandedWidth : sb.collapsedWidth)) < 0.5; },
                  2000, "the sidebar did not settle");
    }
    function drag(sb, toX) {
        settled(sb);
        const y = 300;
        const x0 = edgeX(sb);
        // Held 2 px inside the edge: the edge lands at toX.
        const end = toX - Theme.px(2);
        mousePress(host, x0, y);
        const steps = 6;
        for (let i = 1; i <= steps; i++) mouseMove(host, x0 + (end - x0) * i / steps, y);
        mouseRelease(host, end, y);
    }
    function at(x) { return x - Theme.px(2); }

    function test_the_edge_is_at_the_right_and_named() {
        const sb = bar(true);
        const edge = findChild(sb, "sidebar-edge");
        verify(edge !== null);
        compare(edge.width, Theme.px(5));
        compare(edge.x + edge.width, sb.width);
        verify(edge.activeFocusOnTab, "the keyboard cannot reach the edge");
        compare(edge.Accessible.name, I18n.t("sidebar.edge"));
        verify(edge.tip.indexOf("160") >= 0 && edge.tip.indexOf("320") >= 0, edge.tip);
        compare(findChild(sb, "sidebar-edge-area").cursorShape, Qt.SplitHCursor);
    }

    function test_a_drag_sets_the_width_within_160_320() {
        const sb = bar(true);
        compare(sb.width, Theme.px(208));
        drag(sb, Theme.px(280));
        compare(sb.chosen[sb.chosen.length - 1], 280);
        tryCompare(sb, "width", Theme.px(280));
        drag(sb, Theme.px(600));
        compare(sb.chosen[sb.chosen.length - 1], 320, "wider than 320");
        drag(sb, Theme.px(150));
        compare(sb.chosen[sb.chosen.length - 1], 160, "between 140 and 160 lands on 160");
        compare(sb.rails, 0);
    }

    function test_the_drag_shows_where_it_will_land() {
        const sb = bar(true);
        const y = 300;
        const x0 = edgeX(sb);
        mousePress(host, x0, y);
        mouseMove(host, x0 + Theme.px(20), y);
        mouseMove(host, at(Theme.px(250)), y);
        verify(sb.dragging);
        compare(sb.width, Theme.px(250), "the sidebar does not follow the pointer");
        mouseMove(host, at(Theme.px(100)), y);
        compare(sb.width, sb.collapsedWidth, "under 140 it should show the rail");
        verify(!sb.wide);
        mouseMove(host, at(Theme.px(200)), y);
        verify(sb.wide);
        mouseRelease(host, at(Theme.px(200)), y);
        verify(!sb.dragging);
        compare(sb.chosen[sb.chosen.length - 1], 200);
    }

    function test_under_140_snaps_to_the_rail_and_the_rail_opens_again() {
        const sb = bar(true);
        drag(sb, Theme.px(100));
        compare(sb.rails, 1);
        compare(sb.chosen.length, 0);
        tryCompare(sb, "width", sb.collapsedWidth);
        // From the rail, rightwards: open again at the width let go at.
        drag(sb, Theme.px(240));
        compare(sb.chosen[sb.chosen.length - 1], 240);
        tryCompare(sb, "width", Theme.px(240));
        verify(sb.expanded);
        // A short drag from the rail stays the rail.
        drag(sb, Theme.px(100));
        compare(sb.rails, 2);
        drag(sb, Theme.px(120));
        compare(sb.rails, 3, "under 140 from the rail is still the rail");
    }

    function test_a_double_click_brings_the_default_back() {
        const sb = bar(true, 300);
        compare(sb.width, Theme.px(300));
        settled(sb);
        mouseDoubleClickSequence(host, edgeX(sb), 300);
        compare(sb.resets, 1);
        tryCompare(sb, "width", Theme.px(208));
        compare(sb.chosen.length, 0, "a double click is not a drag");
    }

    function test_a_click_without_a_drag_changes_nothing() {
        const sb = bar(true);
        settled(sb);
        mouseClick(host, edgeX(sb), 300);
        compare(sb.chosen.length, 0);
        compare(sb.rails, 0);
        compare(sb.width, Theme.px(208));
    }

    function test_the_keyboard_moves_the_edge() {
        const sb = bar(true, 170);
        const edge = findChild(sb, "sidebar-edge");
        edge.forceActiveFocus(Qt.TabFocusReason);
        verify(edge.activeFocus);
        keyClick(Qt.Key_Right);
        compare(sb.chosen[sb.chosen.length - 1], 180);
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Left);
        compare(sb.chosen[sb.chosen.length - 1], 160);
        keyClick(Qt.Key_Left);
        compare(sb.rails, 1, "← at 160 folds into the rail");
        keyClick(Qt.Key_Right);
        verify(sb.expanded, "→ on the rail opens it");
        keyClick(Qt.Key_Return);
        compare(sb.resets, 1);
    }

    function test_narrow_drops_row_keys_and_shortens_the_blocks() {
        const wide = bar(true, 260);
        verify(!wide.narrow);
        const navKeys = function (sb) {
            const out = [];
            const walk = function (it) {
                if (!it) return;
                // The footer's "Ctrl K" stays: it is not a row's key.
                if (String(it).indexOf("KeyHint") === 0 && it.visible && it.objectName !== "sidebar-palette-key") out.push(it);
                const kids = it.children || [];
                for (let i = 0; i < kids.length; i++) walk(kids[i]);
            };
            walk(sb);
            return out;
        };
        if (!Style.keyHints) skip("key hints are off in this style");
        verify(navKeys(wide).length > 0, "no key hints on a wide sidebar");
        compare(findChild(wide, "sidebar-pings").narrow, false);

        const narrow = bar(true, 160);
        verify(narrow.narrow);
        compare(navKeys(narrow).length, 0, "the rows keep their keys at 160");
        verify(findChild(narrow, "sidebar-waits").narrow);
        const pings = findChild(narrow, "sidebar-pings");
        verify(pings.narrow);
        compare(findChild(pings, "sidebar-pings-title").text, I18n.t("today.people.short"));
    }

    function test_the_rail_is_never_narrow() {
        const sb = bar(false, 160);
        verify(!sb.narrow);
        compare(sb.width, sb.collapsedWidth);
    }
}
