// An open task by the window's width (dogfood 2026-10-23, Main /
// TaskNarrow / Wide sheets; before it the Small sheet of 10-22): under 1500
// logical px the task takes the whole content area in two columns and the
// sidebar folds into the rail unless its width was set by hand; 1500–1800 it
// takes the area too; from 1800 the board stays live on the left and the
// task is a panel beside it — "Закрыть" and a click on the empty board put it
// away, a click on another card swaps it, its card is marked. Measured at
// 1280×720, 1366×768, 1600×900 and 1920×1080. Also Main's side of the
// sidebar's edge: the width is kept in settings and a width set by hand
// turns the auto-fold off.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SmallScreenLayout"
    when: windowShown
    visible: true
    width: 1100
    height: 760

    Item { id: host; anchors.fill: parent }

    property var win: null
    property var made: []
    property string savedSettings: ""

    function initTestCase() {
        tc.savedSettings = AppController.appSettingsJson;
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        verify(comp.status === Component.Ready, comp.errorString());
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1440;
        tc.win.height = 900;
        wait(1200);   // splash
        closePopups();
        tc.win.resetSidebarWidth();
        AppController.currentView = "today";
    }
    function cleanupTestCase() {
        if (tc.win) tc.win.destroy();
        tc.win = null;
        AppController.appSettingsJson = tc.savedSettings;
    }
    function cleanup() {
        const d = doc();
        if (d && d.opened) d.close();
        if (tc.win) tc.win.resetSidebarWidth();
        for (const id of tc.made) AppController.deleteTask(id);
        tc.made = [];
        AppController.clearPendingUndo();
    }

    function closePopups() {
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) {
            const o = cd[i];
            if (o && o.opened === true && o.close !== undefined) o.close();
        }
    }
    function find(root, pred) {
        if (!root) return null;
        if (pred(root)) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) { const r = find(kids[i], pred); if (r) return r; }
        return null;
    }
    function byName(name, root) {
        return find(root || tc.win.contentItem, function (it) { return it.objectName === name; });
    }
    function doc() { return byName("task-doc"); }
    function rail() { return byName("sidebar"); }
    function mkTask(title, extra) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        for (const k in (extra || {})) d[k] = extra[k];
        AppController.saveTask(d);
        tc.made.push(d.id);
        return d.id;
    }
    function sceneX(it) { return it.mapToItem(null, 0, 0).x; }
    function settle() {
        const sb = rail();
        tryVerify(function () { return Math.abs(sb.width - (sb.expanded ? sb.expandedWidth : sb.collapsedWidth)) < 0.5; },
                  2000, "the sidebar did not settle");
        wait(Theme.durMove + 50);
    }
    function sizeTo(w, h) {
        tc.win.width = w;
        tc.win.height = h;
        tryCompare(tc.win, "width", w);
        wait(50);
    }
    function openOnToday(w, h) {
        sizeTo(w, h);
        AppController.currentView = "today";
        const id = mkTask("small screen task with a title long enough to wrap somewhere", { branch: "feat/small-screen" });
        tc.win.openTask(id);
        const d = doc();
        tryCompare(d, "opened", true);
        settle();
        return d;
    }

    // ── the three sizes ──

    function check_small(w, h) {
        const d = openOnToday(w, h);
        const sb = rail();
        const main = byName("main-column");
        verify(tc.win.smallScreen, w + " should be a small screen");
        verify(!sb.expanded, "the sidebar should fold into the rail at " + w);
        compare(sb.width, sb.collapsedWidth);
        compare(d.width, main.width, "the task should take the whole content area");
        verify(sceneX(d) >= sceneX(sb) + sb.width - 0.5, "the task overlaps the sidebar");
        verify(!d.beside);
        compare(d.columns, 2);
        const side = byName("task-doc-side", d);
        verify(side.visible, "the properties column is gone at " + w);
        compare(side.width, Theme.taskSideColumn);
        const title = byName("task-doc-title", d);
        verify(title.visible);
        verify(title.width >= Theme.px(440), "the text is " + title.width + " px at " + w);
        verify(sceneX(title) + title.width <= sceneX(side) + 0.5, "the text runs under the properties column");
        verify(title.contentWidth > Theme.px(100), "the title is elided to nothing");
        // Blocks, the rare ones folded to a line: Код open, Время folded.
        verify(byName("task-doc-code", d).visible);
        verify(byName("task-block-body-code", d).visible);
        verify(byName("task-doc-time", d).visible);
        verify(!byName("task-block-body-time", d).visible, "Время is not folded on a small screen");
        // Esc goes back to the list.
        d.close();
        tryCompare(d, "opened", false);
        settle();
        verify(sb.expanded, "the sidebar did not open again after the task closed");
    }
    function test_1280x720() { check_small(1280, 720); }
    function test_1366x768() { check_small(1366, 768); }

    function test_1600x900_covers_the_area() {
        const d = openOnToday(Math.round(1600 * Theme.scale), Math.round(900 * Theme.scale));
        const sb = rail();
        const main = byName("main-column");
        verify(!tc.win.smallScreen);
        verify(!tc.win.taskWide);
        verify(sb.expanded, "the sidebar folded at 1600 logical px");
        compare(d.width, main.width, "under 1800 the task takes the whole area");
        verify(!d.beside);
        verify(!byName("panel-scrim").visible, "a scrim under a task that covers the area");
        d.close();
    }

    function test_1920x1080() {
        const d = openOnToday(Math.round(1920 * Theme.scale), Math.round(1080 * Theme.scale));
        const sb = rail();
        const main = byName("main-column");
        verify(!tc.win.smallScreen);
        verify(tc.win.taskWide);
        verify(sb.expanded);
        compare(sb.width, sb.expandedWidth);
        verify(d.beside, "no panel beside the view at 1920");
        verify(d.width < main.width, "the panel takes the whole area");
        verify(d.width <= Theme.taskTextMax + 2 * Theme.px(22) + Theme.taskSideColumn + 0.5, "the panel is " + d.width);
        verify(sceneX(d) >= sceneX(sb) + sb.width - 0.5);
        verify(!byName("panel-scrim").visible, "the view beside the panel is under a scrim");
        const side = byName("task-doc-side", d);
        verify(side.visible);
        verify(byName("task-doc-code", d).visible && byName("task-doc-time", d).visible, "the column is not stacked");
        const title = byName("task-doc-title", d);
        verify(title.width >= Theme.px(440));
        verify(main.width - d.width >= Theme.px(400), "the view beside the panel is squeezed");
        // The view keeps the rest: nothing of it under the panel.
        const view = byName("main-column").children[0];
        verify(sceneX(view) + view.width <= sceneX(d) + 0.5, "the view runs under the panel");
        // Закрыть.
        mouseClick(byName("task-doc-close-area", d));
        tryCompare(d, "opened", false);
    }

    function test_beside_the_live_board() {
        sizeTo(Math.round(1920 * Theme.scale), Math.round(1080 * Theme.scale));
        AppController.currentView = "board";
        const a = mkTask("beside a", {});
        const b = mkTask("beside b", {});
        tryVerify(() => cardOf(a) !== null && cardOf(b) !== null, 2000, "the cards are not on the board");
        wait(200);
        mouseClick(cardOf(a));
        const d = doc();
        tryCompare(d, "opened", true);
        compare(d.taskId, a);
        verify(d.beside);
        tryVerify(() => cardOf(a).docOpen, 1000, "the open card is not marked");
        verify(!cardOf(b).docOpen);
        // Another card: the panel shows it.
        wait(Qt.styleHints.mouseDoubleClickInterval + 50);
        mouseClick(cardOf(b));
        tryCompare(d, "taskId", b);
        verify(d.opened);
        tryVerify(() => cardOf(b).docOpen && !cardOf(a).docOpen, 1000, "the mark did not move");
        // The board's empty space: the panel goes away.
        wait(Qt.styleHints.mouseDoubleClickInterval + 50);
        const board = tc.win.activeViewItem();
        const card = cardOf(b);
        const p = card.mapToItem(board, card.width / 2, card.height + Theme.px(80));
        mouseClick(board, p.x, p.y);
        tryCompare(d, "opened", false);
        AppController.currentView = "today";
    }
    function cardOf(id) {
        return find(tc.win.contentItem, function (it) { return it.taskId === id && it.docOpen !== undefined && it.visible; });
    }

    // ── the sidebar's width, as Main keeps it ──

    function test_the_width_is_kept_in_settings() {
        sizeTo(1600, 900);
        tc.win.setSidebarWidth(260);
        settle();
        compare(rail().width, Theme.px(260));
        compare(JSON.parse(AppController.appSettingsJson).sidebarWidth, 260);
        tc.win.setSidebarWidth(999);
        compare(JSON.parse(AppController.appSettingsJson).sidebarWidth, 320, "kept past 320");
        tc.win.foldSidebarByEdge();
        settle();
        verify(!rail().expanded);
        const s = JSON.parse(AppController.appSettingsJson);
        compare(s.sideRailExpanded, false);
        compare(s.sidebarWidth, 320, "the width for the way back is lost");
        tc.win.resetSidebarWidth();
        settle();
        verify(rail().expanded);
        compare(rail().width, Theme.px(208));
        verify(JSON.parse(AppController.appSettingsJson).sidebarWidth === undefined);
    }

    function test_a_drag_in_the_window_is_kept() {
        sizeTo(1600, 900);
        settle();
        const sb = rail();
        const y = 400;
        const x0 = sb.width - Theme.px(2);
        mousePress(sb, x0, y);
        for (let i = 1; i <= 6; i++) mouseMove(sb, x0 + (Theme.px(270) - x0) * i / 6, y);
        mouseRelease(sb, Theme.px(270), y);
        settle();
        // Let go about 270 px out (the scale rounds a pixel or two).
        const kept = JSON.parse(AppController.appSettingsJson).sidebarWidth;
        verify(Math.abs(kept - 270) <= 3, "kept " + kept);
        compare(sb.widthSetting, kept);
        compare(sb.width, Theme.px(kept));
        mouseDoubleClickSequence(sb, sb.width - Theme.px(2), y);
        settle();
        compare(sb.width, Theme.px(208));
        verify(JSON.parse(AppController.appSettingsJson).sidebarWidth === undefined);
    }

    function test_a_width_set_by_hand_turns_the_auto_fold_off() {
        sizeTo(1440, 900);
        tc.win.setSidebarWidth(240);
        sizeTo(1000, 700);
        settle();
        verify(rail().expanded, "a small window folded a sidebar sized by hand");
        compare(rail().width, Theme.px(240));
        // And an open task on a small screen leaves it too.
        sizeTo(1280, 720);
        const d = openOnToday(1280, 720);
        verify(rail().expanded);
        compare(rail().width, Theme.px(240));
        const side = byName("task-doc-side", d);
        verify(side.visible, "no meta column beside a 240 px sidebar");
        verify(byName("task-doc-title", d).width >= Theme.px(440));
        d.close();
        // Without a choice by hand the window folds it again.
        tc.win.resetSidebarWidth();
        sizeTo(1000, 700);
        settle();
        verify(!rail().expanded);
        sizeTo(1440, 900);
    }
}
