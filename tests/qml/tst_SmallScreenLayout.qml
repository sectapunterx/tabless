// An open task by the window's width («C · тише», dogfood 2026-10-23:
// Redo-Sheet-Quiet, Pick-Wide, Pick-Narrow, Pick-Full): always a sheet over
// the right of the view, the view behind it dimmed and inactive (owner
// review 2026-10-23): any click outside the sheet closes the task and does
// nothing else, no card opens or drags, the board's keys do nothing. Under
// 1500 logical px the sheet takes all of the content area but a strip of the
// view, with no properties column, and the sidebar folds into the rail unless
// its width was set by hand; from 1500 the sheet has its own width (1100,
// 1120 on a 1920 window) and the text and the properties side by side; `f`
// gives it the whole area. Measured at 1280×720, 1366×768, 1600×900 and
// 1920×1080. Also Main's side of the sidebar's edge: the width is kept in
// settings and a width set by hand turns the auto-fold off.
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
        // A sheet: all of the area but a strip of the view (Pick-Narrow).
        verify(d.beside);
        compare(d.width, Math.round(Math.max(Math.min(main.width, Theme.px(640)), main.width - Theme.taskSheetPeek)));
        verify(sceneX(d) >= sceneX(sb) + sb.width - 0.5, "the task overlaps the sidebar");
        tryVerify(() => byName("panel-scrim").visible, 1000, "the strip of the view is not dimmed");
        // No properties column: a line under the chips says what it holds.
        verify(!byName("task-doc-side", d).visible, "a properties column at " + w);
        verify(byName("task-doc-meta-line", d).visible, "no line of the properties at " + w);
        const title = byName("task-doc-title", d);
        verify(title.visible);
        verify(title.width >= Theme.px(440), "the text is " + title.width + " px at " + w);
        verify(title.contentWidth > Theme.px(100), "the title is elided to nothing");
        // "Код, треды, история ▾" opens the column's sections in place, the
        // rare ones folded to a line: Код open, Время open.
        verify(!byName("task-doc-code", d).visible);
        mouseClick(byName("task-doc-meta-more-area", d));
        tryVerify(() => byName("task-doc-code", d).visible, 1000, "the line did not open Код");
        verify(byName("task-block-body-code", d).visible);
        verify(byName("task-doc-time", d).visible);
        verify(byName("task-block-body-time", d).visible, "Время is folded on a small screen");
        d.metaOpen = false;
        // Esc goes back to the list.
        d.close();
        tryCompare(d, "opened", false);
        settle();
        verify(sb.expanded, "the sidebar did not open again after the task closed");
    }
    function test_1280x720() { check_small(1280, 720); }
    function test_1366x768() { check_small(1366, 768); }

    function test_1600x900_is_a_sheet() {
        const d = openOnToday(Math.round(1600 * Theme.scale), Math.round(900 * Theme.scale));
        const sb = rail();
        const main = byName("main-column");
        verify(!tc.win.smallScreen);
        verify(sb.expanded, "the sidebar folded at 1600 logical px");
        verify(d.beside);
        compare(d.width, Math.min(main.width, Theme.taskSheetWidth), "the sheet is not 1100 wide");
        tryVerify(() => byName("panel-scrim").visible, 1000, "the view behind the sheet is not dimmed");
        verify(byName("task-doc-side", d).visible, "no properties column at 1600");
        verify(!byName("task-doc-meta-line", d).visible);
        // `f`: the whole area, nothing to dim (Pick-Full).
        d.focusDocument();
        keyClick(Qt.Key_F);
        tryVerify(() => Math.abs(d.width - main.width) < 0.5, 1000, "f did not give the whole area: " + d.width);
        tryVerify(() => !byName("panel-scrim").visible, 1000, "a scrim under a task that covers the area");
        verify(byName("task-doc-text", d).width <= Theme.taskTextMax + 0.5);
        keyClick(Qt.Key_F);
        tryCompare(d, "width", Math.min(main.width, Theme.taskSheetWidth));
        d.close();
    }

    function test_1920x1080() {
        const d = openOnToday(Math.round(1920 * Theme.scale), Math.round(1080 * Theme.scale));
        const sb = rail();
        const main = byName("main-column");
        verify(!tc.win.smallScreen);
        verify(sb.expanded);
        compare(sb.width, sb.expandedWidth);
        verify(d.beside, "no sheet over the view at 1920");
        // The same sheet, a touch wider; more of the view shows (Pick-Wide).
        compare(d.width, Math.min(main.width, Theme.taskSheetWidthWide), "the sheet is " + d.width);
        verify(sceneX(d) >= sceneX(sb) + sb.width - 0.5);
        tryVerify(() => byName("panel-scrim").visible, 1000, "the view behind the sheet is not dimmed");
        const side = byName("task-doc-side", d);
        verify(side.visible);
        compare(side.width, Theme.taskSideColumn);
        verify(byName("task-doc-code", d).visible && byName("task-doc-time", d).visible, "the column is not stacked");
        const title = byName("task-doc-title", d);
        verify(title.width >= Theme.px(440));
        verify(main.width - d.width >= Theme.px(400), "the view beside the sheet is squeezed");
        // Whole pixels: a sheet at x.5 drew its text between pixels.
        compare(sceneX(d), Math.round(sceneX(d)), "the sheet stands between pixels");
        // Закрыть.
        mouseClick(byName("task-doc-close-area", d));
        tryCompare(d, "opened", false);
    }

    // The task is a sheet over the right of the board; the board behind it
    // is dimmed and inactive (owner review 2026-10-23).
    function openOnBoard() {
        sizeTo(Math.round(1920 * Theme.scale), Math.round(1080 * Theme.scale));
        AppController.currentView = "board";
        const a = mkTask("behind a", {});
        const b = mkTask("behind b", {});
        tryVerify(() => cardOf(a) !== null && cardOf(b) !== null, 2000, "the cards are not on the board");
        wait(200);
        mouseClick(cardOf(a));
        const d = doc();
        tryCompare(d, "opened", true);
        compare(d.taskId, a);
        verify(d.beside);
        tryVerify(() => byName("panel-scrim").visible, 1000, "the board behind the task is not dimmed");
        wait(Qt.styleHints.mouseDoubleClickInterval + 50);
        return { d: d, a: a, b: b };
    }
    function scenePoint(it) { return it.mapToItem(tc.win.contentItem, it.width / 2, it.height / 2); }

    function test_a_click_on_another_card_only_closes_the_task() {
        const o = openOnBoard();
        const p = scenePoint(cardOf(o.b));
        verify(p.x < sceneX(o.d), "the card is under the panel");
        mouseClick(tc.win.contentItem, p.x, p.y);
        tryCompare(o.d, "opened", false);
        // Nothing else: the card under the click did not open.
        wait(Qt.styleHints.mouseDoubleClickInterval + 100);
        verify(!o.d.opened, "the click went on to the card and opened it");
        compare(AppController.selectionCount, 0, "the click selected the card under it");
        AppController.currentView = "today";
    }

    function test_a_click_on_the_empty_board_closes_the_task() {
        const o = openOnBoard();
        const card = cardOf(o.b);
        const p = card.mapToItem(tc.win.contentItem, card.width / 2, card.height + Theme.px(80));
        mouseClick(tc.win.contentItem, p.x, p.y);
        tryCompare(o.d, "opened", false);
        AppController.currentView = "today";
    }

    function test_no_drag_on_the_board_behind_the_task() {
        const o = openOnBoard();
        const card = cardOf(o.b);
        const status = AppController.taskById(o.b).status;
        const p = scenePoint(card);
        mousePress(tc.win.contentItem, p.x, p.y);
        for (let i = 1; i <= 8; i++) mouseMove(tc.win.contentItem, p.x + i * Theme.px(40), p.y + i * Theme.px(4));
        verify(!card._dragging, "the card under the scrim is dragged");
        mouseRelease(tc.win.contentItem, p.x + 8 * Theme.px(40), p.y + 8 * Theme.px(4));
        tryCompare(o.d, "opened", false);
        compare(AppController.taskById(o.b).status, status, "the card moved to another column");
        AppController.currentView = "today";
    }

    function test_the_board_keys_are_inert_while_the_task_is_open() {
        const o = openOnBoard();
        const board = tc.win.activeViewItem();
        verify(tc.win._panelOpen);
        verify(tc.win._viewKeysBlocked, "the board's keys act under the task");
        const cursor = board.cursorTaskId;
        keyClick(Qt.Key_J);
        keyClick(Qt.Key_Down);
        wait(100);
        compare(board.cursorTaskId, cursor, "j / Down moved the board's cursor under the task");
        verify(o.d.opened, "a board key closed the task");
        // Esc still closes it.
        keyClick(Qt.Key_Escape);
        tryCompare(o.d, "opened", false);
        AppController.currentView = "today";
    }

    function test_the_panel_keeps_its_buttons() {
        const o = openOnBoard();
        verify(byName("task-doc-full", o.d).visible, "no На весь экран");
        verify(byName("task-doc-close", o.d).visible, "no Закрыть");
        mouseClick(byName("task-doc-close-area", o.d));
        tryCompare(o.d, "opened", false);
        AppController.currentView = "today";
    }
    // Ctrl+click on an @mention in a ticket's description (owner review
    // 2026-10-23): a person nobody has yet opens the editor on a new contact.
    function test_an_unknown_mention_offers_a_new_contact() {
        let pe = null;
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) if (cd[i] && cd[i].objectName === "person-editor") pe = cd[i];
        verify(pe !== null, "no person editor in Main");
        verify(AppController.personIdForHandle("i.kovaleva") === "", "the test profile knows i.kovaleva");
        tc.win.followMdLink("person", "i.kovaleva");
        tryCompare(pe, "opened", true);
        verify(pe.isNew, "not a new person");
        compare(pe.draft.name, "i.kovaleva");
        pe.close();
        tryCompare(pe, "opened", false);
    }
    function cardOf(id) {
        return find(tc.win.contentItem, function (it) { return it.taskId === id && it.boardKeys !== undefined && it.visible; });
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
        // A narrow window: the properties as a line under the chips.
        verify(!byName("task-doc-side", d).visible);
        verify(byName("task-doc-meta-line", d).visible, "no line of the properties beside a 240 px sidebar");
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
