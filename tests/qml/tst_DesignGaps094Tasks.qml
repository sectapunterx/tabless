// Design gaps 0.9.4, group "tasks" (docs/DESIGN-GAPS-0.9.4.md): the app
// against the reference sheets — board, list, calendar, menus, dialogs,
// Settings → Appearance — and the binding loops the log showed (b-01, b-23).
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "DesignGaps094Tasks"
    when: windowShown
    visible: true
    width: 1200
    height: 800

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""
    property string savedStyle: ""
    property var madeTasks: []
    property var madeEvents: []

    function init() {
        tc.savedSettings = AppController.appSettingsJson;
        tc.savedStyle = Style.name;
    }
    function cleanup() {
        for (const id of tc.madeTasks) AppController.deleteTask(id);
        for (const id of tc.madeEvents) AppController.deleteEvent(id);
        AppController.clearPendingUndo();
        tc.madeTasks = [];
        tc.madeEvents = [];
        AppController.appSettingsJson = tc.savedSettings;
        if (tc.savedStyle === "quiet" || tc.savedStyle === "bold") Style.apply(tc.savedStyle);
    }

    function all(root, name, acc) {
        if (!root) return acc;
        if (root.objectName === name) acc.push(root);
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) all(kids[i], name, acc);
        return acc;
    }
    function inside(item, ancestor) {
        for (let p = item; p; p = p.parent) if (p === ancestor) return true;
        return false;
    }
    function addTask(fields) {
        const d = AppController.newTaskDraft(fields.status || "todo");
        d._isNew = true;
        for (const k in fields) d[k] = fields[k];
        AppController.saveTask(d);
        tc.madeTasks.push(d.id);
        return d.id;
    }
    function addEvent(day, start, end, title) {
        const ev = AppController.newEventDraft(start, day);
        ev.title = title;
        ev.end = end;
        ev.date = day;
        AppController.saveEvent(ev);
        tc.madeEvents.push(ev.id);
        return ev.id;
    }
    // The sheets are drawn at 100 %; a fresh test profile starts at the
    // system's 110 %, where the card and the lanes have other widths.
    function atScale1() {
        const o = JSON.parse(AppController.appSettingsJson || "{}");
        o.appearance = Object.assign({}, o.appearance || {}, { uiScale: 1 });
        AppController.appSettingsJson = JSON.stringify(o);
    }
    function today(h, m) {
        const t = AppController.today;
        return new Date(t.getFullYear(), t.getMonth(), t.getDate(), h || 0, m || 0);
    }

    // ── b-01: the day view's deadline titles read their width off a metric ──
    function test_day_view_has_no_binding_loop() {
        failOnWarning(/Binding loop detected/);
        const day = new Date(AppController.today);
        day.setDate(day.getDate() + 611);
        day.setHours(0, 0, 0, 0);
        for (let i = 0; i < 3; i++)
            addTask({ title: "gap b-01 a deadline with a title much longer than the room a third of the line has " + i,
                      dueAt: day, scheduledAt: day, hasTime: false });
        const prev = AppController.selectedDate;
        AppController.selectedDate = day;
        const wv = createTemporaryQmlObject('import TodoCpp; WeekView { anchors.fill: parent; zoom: "day" }', host);
        verify(wv !== null);
        tryVerify(() => all(wv, "week-due-title", []).length > 0, 2000, "no deadline in the day view");
        wv.width = 900;
        wait(50);
        wv.width = 1200;
        wait(50);
        const titles = all(wv, "week-due-title", []);
        for (const t of titles) verify(t.width <= Math.ceil(t.implicitWidth) + 1, "a title wider than its text");
        AppController.selectedDate = prev;
    }

    // ── b-23: the board's dialogs and the one-line meeting input ──
    function test_board_dialogs_have_no_binding_loop() {
        failOnWarning(/Binding loop detected/);
        const board = createTemporaryQmlObject('import TodoCpp; KanbanBoard { anchors.fill: parent }', host);
        verify(board !== null);
        wait(100);
        verify(!board.dialogOpen);
        // And open: the title is the dialog's own header now.
        board.columnCommand("archive");
        tryVerify(() => board.dialogOpen);
        wait(50);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !board.dialogOpen);
    }

    function test_event_capture_typing_has_no_binding_loop() {
        failOnWarning(/Binding loop detected/);
        const cap = createTemporaryQmlObject('import TodoCpp; EventCapture { }', host);
        cap.openAt({ date: today(), start: 9, end: 10 });
        tryVerify(() => cap.opened);
        const input = findChild(cap.contentItem, "event-capture-input");
        input.forceActiveFocus();
        keyClick(Qt.Key_S);
        keyClick(Qt.Key_Space);
        input.text = "standup 14:00 for 30 min every week";
        keyClick(Qt.Key_Space);
        verify(cap.marks.length > 0, "the read words are not tinted");
        compare(cap.parsed.start, 14);
        cap.close();
    }

    // ── a-30: a tracker card with writes off says "only here" on its target ──
    function test_drop_target_says_only_here_for_a_local_move() {
        AppController.appSettingsJson = "{}";   // every tracker's writes off
        const board = createTemporaryQmlObject('import TodoCpp; KanbanBoard { anchors.fill: parent }', host);
        wait(50);
        const ticket = { id: "gap-a30", title: "x", status: "todo", ticket: { provider: "github", key: "#482" } };
        verify(board.moveStaysLocal(ticket, "prog"), "a move with writes off goes nowhere else");
        verify(!board.moveStaysLocal(ticket, "todo"), "its own column is no move");
        verify(!board.moveStaysLocal({ id: "gap-a30b", status: "todo" }, "prog"), "a task of our own");
        // Every column open: an empty one folds by itself (APP-314).
        const open = {};
        for (const st of AppController.statuses) open[st.id] = true;
        board.emptyOpen = open;
        wait(50);
        const captions = all(board, "column-drop-local-only", []);
        verify(captions.length > 0);
        // The caption sits in the column's header row, after the count.
        const colOf = (it) => { let c = it; while (c && c.dropLocalOnly === undefined) c = c.parent; return c; };
        const at = captions.findIndex(c => colOf(c) && !colOf(c).folded);
        verify(at >= 0, "no open column");
        const caption = captions[at];
        const col = colOf(caption);
        captions[0] = caption;
        verify(!caption.visible);
        col.dropLocalOnly = true;
        col.dragOver = true;
        wait(50);
        {
            verify(captions[0].visible, "no caption while the card is over the column");
            compare(captions[0].text, I18n.t("kanban.drop.localOnly"));
            verify(!captions[0].truncated, "the caption is cut");
            verify(!all(col, "column-count", [])[0].visible, "the count keeps the caption's room");
            const head = all(col, "column-name", [])[0];
            verify(head !== undefined);
            const yc = captions[0].mapToItem(col, 0, captions[0].height / 2).y;
            const yh = head.mapToItem(col, 0, head.height / 2).y;
            verify(Math.abs(yc - yh) <= 2, "not on the header line");
        }
        col.dragOver = false;
        verify(all(col, "column-count", [])[0].visible, "the count did not come back");
        compare(I18n.dict.ru["kanban.drop.localOnly"], "только здесь");
    }

    // ── b-04: the folded Done's header is whole ──
    function test_folded_done_header_is_not_cut_data() {
        return [ { tag: "bold", style: "bold" }, { tag: "quiet", style: "quiet" } ];
    }
    function test_folded_done_header_is_not_cut(data) {
        Style.apply(data.style);
        const board = createTemporaryQmlObject('import TodoCpp; KanbanBoard { anchors.fill: parent }', host);
        wait(50);
        const names = all(board, "column-done-folded-name", []).filter(n => n.visible);
        verify(names.length > 0, "no folded Done");
        for (const n of names) verify(!n.truncated, "cut: " + n.text);
        verify(board.doneFoldedWidth >= Theme.px(88));
    }

    // ── b-25: a link's underline is a dim rule under the baseline ──
    function test_done_show_underline_is_dim_and_low() {
        Style.apply("bold");
        const board = createTemporaryQmlObject('import TodoCpp; KanbanBoard { anchors.fill: parent }', host);
        wait(50);
        const lines = all(board, "column-done-show-underline", []);
        verify(lines.length > 0);
        const link = lines[0].parent;
        verify(!link.font.underline, "the text's own underline is back");
        compare(lines[0].color, Theme.borderStrong);
        verify(lines[0].y >= link.baselineOffset + 2, "tight to the glyphs");
    }

    // ── b-03: the list's date column has one width ──
    function test_list_date_column_is_fixed() {
        addTask({ title: "gap b-03 short", dueAt: today(), scheduledAt: today(), hasTime: false });
        addTask({ title: "gap b-03 long", scheduledAt: today(10, 0), scheduledHasTime: true, hasTime: true,
                  dueAt: new Date(AppController.today.getTime() + 86400000), dueHasTime: false });
        const l = createTemporaryQmlObject('import TodoCpp; TaskListView { anchors.fill: parent }', host);
        l.searchText = "gap b-03";
        l.groupBy = "date";
        l.refresh();
        let dates = [];
        tryVerify(() => { dates = all(l, "list-row-date", []).filter(d => d.visible && d.text.length > 0); return dates.length >= 2; },
                  2000, "no dated rows");
        for (const d of dates) compare(d.width, dates[0].width, "date column width differs: " + d.text);
        const pris = all(l, "list-row-priority", []).filter(p => p.visible);
        for (const p of pris) compare(p.mapToItem(l, 0, 0).x, pris[0].mapToItem(l, 0, 0).x, "P out of line");
    }

    // ── b-07: both delete rows of an event's menu are red ──
    function test_delete_series_is_danger() {
        const wv = createTemporaryQmlObject('import TodoCpp; WeekView { anchors.fill: parent }', host);
        const one = findChild(wv, "week-event-menu-delete");
        const series = findChild(wv, "week-event-menu-delete-series");
        verify(one !== null && series !== null);
        verify(one.danger);
        verify(series.danger, "Удалить серию… is not red");
    }

    // ── b-08: the bold style fills the main button of these dialogs ──
    function test_bold_primary_buttons_are_solid() {
        Style.apply("bold");
        const tm = createTemporaryQmlObject('import TodoCpp; TimeMachineDialog { }', host);
        const restore = findChild(tm.contentItem, "time-machine-restore-all");
        verify(restore !== null);
        verify(restore.solid);
        const st = createTemporaryQmlObject('import TodoCpp; StandupDraftDialog { }', host);
        verify(findChild(st.contentItem, "standup-draft-copy").solid);
        const push = createTemporaryQmlObject('import TodoCpp; TrackerPushConfirmDialog { }', host);
        verify(findChild(push, "trackerPushConfirmCancel").solid);
        Style.apply("quiet");
        verify(!restore.solid, "quiet stays outlined");
    }

    // ── b-09: the recap's day bars ──
    function test_recap_bars_are_blue_in_bold_grey_in_quiet() {
        const d = createTemporaryQmlObject('import TodoCpp; WeeklyRecapDialog { }', host);
        d.showNow();
        tryVerify(() => d.opened);
        let bars = [];
        tryVerify(() => { bars = all(d.contentItem, "recap-bar", []); return bars.length > 0; });
        Style.apply("bold");
        compare(bars[0].color, Theme.info);
        Style.apply("quiet");
        compare(bars[0].color, Theme.borderStrong);
        d.close();
    }

    // ── b-11: Appearance at rest — scale and contrast under "more" ──
    function test_scale_and_contrast_are_folded() {
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        sv.activeSection = "appearance";
        tryVerify(() => findChild(sv, "settings-ui-scale") !== null, 2000);
        const more = findChild(findChild(sv, "settings-block-appearance"), "settings-more-toggle").parent;
        verify(!more.open);
        for (const name of ["settings-ui-scale", "settings-contrast"]) {
            const row = findChild(sv, name);
            verify(inside(row, more), name + " is not under more");
            verify(!row.visible || !more.open, name + " shows at rest");
        }
        const font = findChild(sv, "settings-font-face");
        verify(font.visible && !inside(font, more), "the font row left the sheet");
    }

    // 0.9.4 owner: the fold reads as a control (a chevron that turns, the
    // muted text colour), and opens on a click.
    function test_more_is_a_visible_control() {
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        sv.activeSection = "appearance";
        let head = null;
        tryVerify(() => { head = findChild(findChild(sv, "settings-block-appearance"), "settings-more-toggle"); return head !== null; }, 2000);
        const more = head.parent;
        const text = findChild(head, "settings-more-text");
        compare(text.color, Theme.textMuted);
        verify(!more.open);
        const click = findChild(head, "settings-more-click");
        compare(click.width, head.width);
        click.activated();
        tryVerify(() => more.open, 1000);
        // Remembered per section: fold it back for the next run.
        click.activated();
        tryVerify(() => !more.open, 1000);
    }

    // ── b-25: Settings' text links ──
    function test_settings_link_underline_is_dim() {
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        sv.activeSection = "appearance";
        let line = null;
        tryVerify(() => { line = findChild(sv, "settings-style-reset-underline"); return line !== null; }, 2000);
        compare(line.color, Theme.borderStrong);
        verify(!line.parent.font.underline);
        verify(line.y >= line.parent.baselineOffset + 2);
    }

    // ── b-15: a tracker task's menu header names the tracker ──
    function test_task_menu_header_names_the_tracker() {
        const h = createTemporaryQmlObject('import TodoCpp; TaskMenuHost { }', host);
        h.task = { id: "gap-b15", title: "Обход ограничения попыток", status: "todo",
                   ticket: { provider: "github", key: "#482" } };
        const name = (AppController.providerBadges["github"] || {}).name;
        compare(name, "GitHub");
        compare(h.headerKey(), "GitHub #482");
        h.task = { id: "APP-109", title: "Рефакторинг", status: "todo" };
        compare(h.headerKey(), "APP-109");
    }

    // ── b-17: a row that opens a list and has a key shows only the key ──
    function test_list_row_with_a_key_has_no_arrow() {
        const withKey = createTemporaryQmlObject('import TodoCpp; AppMenuItem { text: "a"; opensList: true; keyText: "Ctrl Shift J / K" }', host);
        verify(!withKey._arrow, "a chevron after the key");
        const bare = createTemporaryQmlObject('import TodoCpp; AppMenuItem { text: "b"; opensList: true }', host);
        verify(bare._arrow, "a list row without a key lost its arrow");
    }

    // ── b-18: a quiet block's time range ends with "…" in a narrow lane ──
    function test_narrow_block_range_elides() {
        atScale1();
        Style.apply("quiet");
        const day = today();
        for (let i = 0; i < 3; i++) addEvent(day, 9, 13, "gap b-18 " + i);
        const prev = AppController.selectedDate;
        AppController.selectedDate = day;
        const wv = createTemporaryQmlObject('import TodoCpp; WeekView { anchors.fill: parent }', host);
        let ranges = [];
        tryVerify(() => { ranges = all(wv, "week-event-range-under", []).filter(r => r.visible); return ranges.length >= 2; },
                  2000, "no quiet ranges");
        for (const r of ranges) {
            compare(r.elide, Text.ElideRight);
            verify(r.width <= r.parent.width + 0.5, "the range runs past its block");
            verify(r.contentWidth <= r.width + 0.5 || r.truncated, "cut without an ellipsis");
        }
        AppController.selectedDate = prev;
    }

    // ── b-20: the card's date drops the clock before it cuts the date ──
    Component {
        id: cardComp
        TaskCard { }
    }
    function test_card_date_drops_the_clock_first() {
        // Russian, where "сегодня 13:30" beside the key and P0 ran short.
        const lang = AppController.language;
        AppController.language = "ru";
        atScale1();
        const due = today(13, 30);
        const card = cardComp.createObject(host, {
            width: 320,
            task: { id: "PAYMENTS-1150", title: "Оплата", desc: "", priority: "P0", status: "todo",
                    labels: [], deadline: due, dueAt: due, dueHasTime: true }
        });
        verify(card !== null);
        const due1 = findChild(card, "tc-due");
        verify(due1 !== null);
        tryVerify(() => due1.dlText.length > 0);
        verify(due1.dlText.indexOf(I18n.fmtTime(due)) > 0, "no clock to drop");
        let sawBare = false;
        for (let w = 400; w >= 60; w -= 10) {
            card.width = w;
            wait(0);
            if (due1.text === due1.bareText) sawBare = true;
            if (due1.truncated) compare(due1.text, due1.bareText, "cut with the clock still on at " + w);
        }
        card.destroy();
        AppController.language = lang;
        verify(sawBare, "the clock never went");
    }

    // ── b-21: the log's source column is as wide as its widest word ──
    function test_event_log_source_column_fits_every_word() {
        const log = createTemporaryQmlObject('import TodoCpp; EventLogDialog { }', host);
        const m = createTemporaryQmlObject('import QtQuick; import TodoCpp; TextMetrics { font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }', host);
        for (const w of [I18n.t("eventLog.src.reminder"), I18n.t("eventLog.src.sync"), I18n.t("eventLog.src.tasks"),
                         I18n.t("eventLog.src.data"), Brand.name]) {
            m.text = w;
            verify(log.sourceColumnWidth >= Math.ceil(m.advanceWidth), w + " does not fit");
        }
    }

    // ── b-22: the conflict dialog reads a description as prose ──
    function test_conflict_body_is_plain_text() {
        const dlg = createTemporaryQmlObject('import TodoCpp; SyncConflictDialog { }', host);
        dlg.showFor({ id: "gap-b22", title: "t", desc: "`SyncTest.resumesAfterTimeout` падает **иногда**", priority: "P2",
                      status: "prog", labels: [],
                      ticket: { provider: "jira", key: "APP-101", conflict: true, conflicts: ["body"],
                                remoteBody: "добавлен комментарий о `Redis`", updatedAt: new Date() } });
        tryCompare(dlg, "opened", true);
        compare(dlg.rows.length, 1);
        compare(dlg.rows[0].mine, "SyncTest.resumesAfterTimeout падает иногда");
        compare(dlg.rows[0].theirs, "добавлен комментарий о Redis");
        dlg.close();
    }
}
