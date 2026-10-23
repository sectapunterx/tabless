import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Controls as QQC
import TodoCpp
import "QueryWords.js" as QueryWords
import "Motion.js" as Motion
import "BoardFit.js" as BoardFit

Item {
    id: root
    property string searchText: ""
    readonly property var stages: ["backlog", "todo", "prog", "half", "blocked", "review", "done"]
    readonly property string _searchKeepDone: root.searchText.replace(/(^|\s)is:open(?=\s|$)/gi, " ").trim()
    // The applied saved view's query, as the folded Done column reads it:
    // strict there too (IDIOT-TASKS-11).
    readonly property string _strictKeepDone: AppController.strictQuery.replace(/(^|\s)is:open(?=\s|$)/gi, " ").trim()
    // How every column is ordered. "manual" is the board's own rank, which is
    // what a drag writes; the others are read-only views over the same cards,
    // so switching back to manual restores the order the user arranged rather
    // than whatever the last sort left behind.
    property string sortMode: "manual"
    property var prioritiesFilter: ({})
    property var scheduleMap: ({})
    property bool showArchived: false
    // The card under the cursor, read by Main.qml so a single-key action knows
    // which ticket it means when nothing is selected (HEAP-117).
    property string hoveredTaskId: ""
    signal taskClicked(string id)
    // A click on the empty board (not on a card): the task beside it goes away.
    signal emptyClicked()
    // The task open beside the board (a wide window): its card is marked.
    property string openTaskId: ""
    signal createInStatus(string statusId)
    // "сбросить фильтр · Esc" under a filter that found nothing (DG-160).
    signal resetFilterRequested()

    // Anchor for shift-click range selection. Reset when the selection
    // disappears so the next shift-click starts a fresh range.
    property string shiftAnchorId: ""
    Connections {
        target: AppController

        function onSelectedTaskIdsChanged() {
            if (AppController.selectionCount === 0) root.shiftAnchorId = "";
        }

        function onStatusesChanged() { root._syncColumns(); }

        // Sidebar Blocked / Code Review buttons jump the board to a column.
        function onFocusedStatusChanged() {
            if (AppController.focusedStatus.length > 0) root.focusColumn(AppController.focusedStatus);
        }
    }

    // ── Columns, updated in place (TASKS-19) ──────────────────────────
    // AppController.statuses is a plain list, and a Repeater over a list
    // rebuilds every delegate whenever it changes: renaming one column threw
    // away all of them, with their scroll positions. This model is patched
    // row by row, so an edit touches only the column it is about.
    ListModel { id: colModel }
    function _syncColumns() {
        const sts = AppController.statuses;
        const want = {};
        for (let i = 0; i < sts.length; i++) want[sts[i].id] = true;
        for (let i = colModel.count - 1; i >= 0; i--)
            if (!want[colModel.get(i).sid]) colModel.remove(i);
        for (let i = 0; i < sts.length; i++) {
            const s = sts[i];
            const row = { sid: String(s.id), sname: String(s.name || ""), scolor: String(s.color || ""), swip: Number(s.wip || 0) };
            let at = -1;
            for (let j = i; j < colModel.count; j++) if (colModel.get(j).sid === row.sid) { at = j; break; }
            if (at < 0) { colModel.insert(i, row); continue; }
            if (at !== i) colModel.move(at, i, 1);
            const cur = colModel.get(i);
            if (cur.sname !== row.sname) colModel.setProperty(i, "sname", row.sname);
            if (cur.scolor !== row.scolor) colModel.setProperty(i, "scolor", row.scolor);
            if (cur.swip !== row.swip) colModel.setProperty(i, "swip", row.swip);
        }
    }

    // ── Collapsed columns (TASKS-32) ───────────────────────────────────
    // A column folded to a narrow strip keeps its name and count and gives
    // its width to the others. Remembered per column in the UI settings.
    property var collapsed: ({})
    function _loadCollapsed() {
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        const ids = Array.isArray(s.boardCollapsed) ? s.boardCollapsed : [];
        const out = {};
        for (let i = 0; i < ids.length; i++) out[ids[i]] = true;
        root.collapsed = out;
    }
    // Done is folded into a narrow column with "Show" unless the user opened
    // it (APP-262); which done-stage columns are open is remembered.
    property var doneShown: ({})
    function _loadDoneShown() {
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        const ids = Array.isArray(s.boardDoneShown) ? s.boardDoneShown : [];
        const out = {};
        for (let i = 0; i < ids.length; i++) out[ids[i]] = true;
        root.doneShown = out;
    }
    function isDoneColumn(statusId) {
        return AppController.statuses.length >= 0 && AppController.statusCategory(statusId) === "done";
    }
    // The task a drag carries, or "" (a file from a file manager).
    function dragTaskId(src) {
        return src && src.taskId ? String(src.taskId) : "";
    }
    // A tracker card whose tracker is not written to changes its column
    // here only; the target says so while the card is over it (a-30).
    function dropStaysLocal(src, statusId) {
        const id = root.dragTaskId(src);
        return id.length > 0 && root.moveStaysLocal(AppController.taskById(id), statusId);
    }
    function moveStaysLocal(t, statusId) {
        const provider = t && t.ticket ? String(t.ticket.provider || "") : "";
        return provider.length > 0 && String(t.status) !== statusId && !AppController.trackerWriteEnabled(provider);
    }
    // An empty column folds to a strip by itself (APP-314): six empty
    // columns spread across a 2K screen squeezed the one with cards and sent
    // the eye over a screen of "Nothing here yet". It stays a drop target and
    // opens when a card lands in it; one the user opens stays open, empty or
    // not (boardEmptyOpen).
    property var emptyOpen: ({})
    function _loadEmptyOpen() {
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        const ids = Array.isArray(s.boardEmptyOpen) ? s.boardEmptyOpen : [];
        const out = {};
        for (let i = 0; i < ids.length; i++) out[ids[i]] = true;
        root.emptyOpen = out;
    }
    function isEmptyColumn(statusId) {
        return !((AppController.statusCounts || {})[statusId] > 0);
    }
    function isFolded(statusId) {
        if (root.isDoneColumn(statusId)) return !root.doneShown[statusId];
        if (root.collapsed[statusId]) return true;
        return root.isEmptyColumn(statusId) && !root.emptyOpen[statusId];
    }
    function _saveEmptyOpen(next) {
        root.emptyOpen = next;
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        s.boardEmptyOpen = Object.keys(next);
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function toggleCollapsed(statusId) {
        // An empty column folded by itself opens on the same gesture, and
        // folding it back clears that, so it folds by itself again.
        if (!root.isDoneColumn(statusId) && !root.collapsed[statusId] && root.isEmptyColumn(statusId)) {
            const open = Object.assign({}, root.emptyOpen);
            if (open[statusId]) delete open[statusId]; else open[statusId] = true;
            root._saveEmptyOpen(open);
            return;
        }
        if (root.isDoneColumn(statusId)) {
            const shown = Object.assign({}, root.doneShown);
            if (shown[statusId]) delete shown[statusId]; else shown[statusId] = true;
            root.doneShown = shown;
            let st = {};
            try { st = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { st = {}; }
            st.boardDoneShown = Object.keys(shown);
            AppController.appSettingsJson = JSON.stringify(st);
            return;
        }
        const next = Object.assign({}, root.collapsed);
        if (next[statusId]) delete next[statusId]; else next[statusId] = true;
        root.collapsed = next;
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        s.boardCollapsed = Object.keys(next);
        AppController.appSettingsJson = JSON.stringify(s);
    }
    // Fold or unfold the column the keyboard cursor is in. The cursor stays
    // on its card when the column folds, so z a again unfolds it rather than
    // folding the next column (IDIOT-TASKS-13).
    function toggleCursorColumn() {
        const cols = _visibleByColumn();
        const pos = _cursorPos(cols);
        let c = pos ? pos.col : root._foldedColumnOf(root.cursorTaskId);
        if (c < 0 && colRepeater.count > 0) c = 0;
        const col = c >= 0 ? colRepeater.itemAt(c) : null;
        if (!col) return;
        root.toggleCollapsed(String(col["statusId"]));
        // The column that just opened (Done, off at the right edge) comes
        // into view; z a used to change nothing on screen (A2-5).
        root._revealColumnSoon(c);
    }
    // Scrolls the board sideways so column `c` is in view, once the fold has
    // been laid out.
    function _revealColumnSoon(c) {
        revealColumnTimer.column = c;
        revealColumnTimer.restart();
    }
    function _revealColumn(c) {
        const col = colRepeater.itemAt(c);
        if (!col) return;
        const maxX = Math.max(0, hscroll.contentWidth - hscroll.width);
        let x = hscroll.contentX;
        if (col.x < x) x = col.x;
        else if (col.x + col.width > x + hscroll.width) x = col.x + col.width - hscroll.width;
        x = Math.max(0, Math.min(maxX, x));
        if (x !== hscroll.contentX) {
            outerAnim.stop();
            colScrollAnim.stop();
            hscroll.contentX = x;
        }
    }
    Timer {
        id: revealColumnTimer
        property int column: -1
        interval: 30
        onTriggered: root._revealColumn(revealColumnTimer.column)
    }
    // The board follows a card the keyboard moved to another column: the id
    // under the cursor does not change, so nothing else scrolled to it, and
    // the card and the cursor went off screen (A2-1).
    function _revealCursorSoon() {
        revealCursorTimer.restart();
    }
    Timer {
        id: revealCursorTimer
        interval: 30
        onTriggered: root.revealCursor()
    }
    // d on the card under the cursor (Main.markDone): the cursor steps to its
    // neighbour in the column first, as e does, so d j d … works down a
    // column and the cursor never follows the card into a folded Done
    // (A2-4, A4-16).
    function stepCursorOff(id) {
        if (!id || id !== root.cursorTaskId) return;
        const cols = _visibleByColumn();
        const pos = _cursorPos(cols);
        if (!pos) return;
        root.cursorTaskId = root._neighbourOf(cols, Number(pos["col"]), Number(pos["row"]));
    }
    // The card the cursor takes when the one at `pos` leaves: the next one
    // in its column, else the one above; a lone card hands the cursor to the
    // nearest column with cards (right first), at the same row or its last.
    // "" when the board has no other card, so j starts afresh instead of
    // the cursor sitting in a folded Done where j/k/l do nothing.
    function _neighbourOf(cols, col: int, row: int): string {
        const ids = cols[col].ids;
        if (row + 1 < ids.length) return ids[row + 1];
        if (row > 0) return ids[row - 1];
        for (let d = 1; d < cols.length; d++) {
            for (const c of [col + d, col - d]) {
                if (c < 0 || c >= cols.length || cols[c].ids.length === 0) continue;
                return cols[c].ids[Math.min(row, cols[c].ids.length - 1)];
            }
        }
        return "";
    }

    // True while any card's menu is up; Main.qml holds the board keys back.
    property int _openCardMenus: 0
    readonly property bool cardMenuOpen: _openCardMenus > 0
    // One of the board's own dialogs or menus is up (new column, WIP limit,
    // delete confirmation, a card menu). Main.qml keeps Ctrl+Z from acting
    // on the board behind it.
    readonly property bool dialogOpen: cardMenuOpen || wipPopup.opened || archivePopup.opened || addColumnPopup.opened
                                       || confirmDelete.opened || colorPopup.opened

    // One source of truth for the column width. focusColumn() scrolls by
    // index × width, so a literal here and a different literal in the delegate
    // silently put the wrong column on screen.
    //
    // heap 2 (APP-262): columns share the width like `flex: 1 1 150px` — at
    // least 150, growing to fill the board — with 16 px between them, so six
    // columns and a folded Done fit a 1440 px window. Down to 130 so they
    // still fit a ~1300 px one (a clipped Done column read as broken); past
    // that the board scrolls sideways.
    readonly property int minColumnWidth: Theme.px(130)
    readonly property int maxColumnWidth: Theme.px(360)
    // A folded column is a 36 px strip, the name run down it (BoardDrag
    // sheet), whether folded by hand, empty, or by a narrow board.
    readonly property int foldedWidth: Theme.px(36)
    // A folded Done is 88 px on the sheet, and wider when its own header
    // needs it: "Готово 2" read "Гото… 2" at 100 % in Inter (b-04).
    readonly property int doneFoldedWidth: Math.max(Theme.px(88),
        Math.ceil(doneNameFm.advanceWidth + doneCountFm.advanceWidth) + Theme.statusRingSize
        + 2 * Theme.spXs + 2 * Theme.spMd + 2)
    readonly property string _doneName: {
        const sts = AppController.statuses;
        for (let i = 0; i < sts.length; i++)
            if (root.isDoneColumn(sts[i].id)) return String(sts[i].name || "");
        return "";
    }
    TextMetrics {
        id: doneNameFm
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsMd
        font.weight: Theme.fwHeading
        text: root._doneName
    }
    TextMetrics {
        id: doneCountFm
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsMd
        font.features: Theme.tabularNums
        text: "00"
    }
    readonly property int columnGap: Theme.sp2xl
    // The "+ column" tile at the end of the row and the gap before it: the
    // columns leave it room, or it alone sent the board sideways.
    readonly property int tileRoom: Theme.chipH + root.columnGap

    // ── A narrow board folds columns by itself (BoardDrag sheet) ──────
    // When the open columns do not fit at their least width, they fold into
    // strips rather than send the board sideways: Done first, then empty
    // ones, then the outermost — never the cursor's, the selection's, the
    // one a card is over or came from. Worked out (BoardFit.fit) from the
    // width each time, never saved: only a fold by hand is remembered.
    // The column a click or Enter on a strip opened; another gives way.
    property string _wantOpen: ""
    // The strip a dragged card rested on for 0.4 s, open until the drag ends.
    property string _dragOpen: ""
    // The open column a dragged card is over.
    property string _dragTarget: ""
    function _statusOfTask(id) {
        if (!id) return "";
        const t = AppController.taskById(id);
        return t && t.id ? String(t.status || "") : "";
    }
    readonly property var boardFit: {
        const sts = AppController.statuses;
        const counts = AppController.statusCounts || {};
        const keep = {};
        const add = (sid) => { if (sid) keep[sid] = true; };
        const cursorSid = root._statusOfTask(root.cursorTaskId);
        add(cursorSid);
        const sel = AppController.selectedTaskIds;
        for (let i = 0; i < sel.length && i < 200; i++) add(root._statusOfTask(sel[i]));
        add(root._dragTarget);
        add(root._dragOpen);
        add(root._wantOpen);
        if (root.dragCard) add(root._statusOfTask(root.dragCard.taskId));
        // What each column shows: one whose cards the search hides is as
        // empty here as one with none.
        const shown = {};
        for (let c = 0; c < colRepeater.count; c++) {
            const it = colRepeater.itemAt(c);
            if (it) shown[it["statusId"]] = Number(it["visibleCount"]);
        }
        const cols = [];
        let anchor = -1;
        for (let i = 0; i < sts.length; i++) {
            const sid = String(sts[i].id);
            if (sid === cursorSid) anchor = i;
            const empty = !(counts[sid] > 0) || shown[sid] === 0;
            cols.push({ id: sid, done: root.isDoneColumn(sid), empty: empty, folded: root.isFolded(sid) });
        }
        return BoardFit.fit({ width: hscroll.width - root.tileRoom, gap: root.columnGap, minWidth: root.minColumnWidth,
                              stripWidth: root.foldedWidth, doneFoldedWidth: root.doneFoldedWidth,
                              columns: cols, keep: keep, anchor: anchor });
    }
    // Folded into a strip by the narrow board, not by its own fold.
    function isAutoStrip(statusId) {
        return !!root.boardFit.strips[statusId];
    }
    // A click or Enter on a strip: one the narrow board folded opens and
    // another gives way; one folded by hand (or empty, or Done) opens as
    // before.
    function expandStrip(statusId) {
        if (root.isAutoStrip(statusId)) {
            root._wantOpen = statusId;
            if (root.isDoneColumn(statusId) && !root.doneShown[statusId]) root.toggleCollapsed(statusId);
            return;
        }
        root.toggleCollapsed(statusId);
    }
    readonly property int columnWidth: {
        const sts = AppController.statuses;
        const strips = root.boardFit.strips;
        let open = 0, fixed = 0;
        for (let i = 0; i < sts.length; i++) {
            const sid = sts[i].id;
            if (strips[sid]) fixed += root.foldedWidth;
            else if (!root.isFolded(sid)) open++;
            else fixed += root.isDoneColumn(sid) ? root.doneFoldedWidth : root.foldedWidth;
        }
        const gaps = Math.max(0, sts.length - 1) * root.columnGap;
        const avail = hscroll.width - root.tileRoom - fixed - gaps;
        if (open === 0) return root.minColumnWidth;
        return Math.max(root.minColumnWidth, Math.min(root.maxColumnWidth, Math.floor(avail / open)));
    }

    // ── Column focus (sidebar Blocked / Code Review buttons) ──────────
    // Scroll the target status column into view and briefly highlight it.
    property string _focusPulseStatus: ""
    NumberAnimation {
        id: colScrollAnim
        target: hscroll; property: "contentX"
        duration: Theme.durMove; easing.type: Theme.easeEnter
    }
    Timer { id: focusPulseTimer; interval: 1100; onTriggered: root._focusPulseStatus = "" }

    function focusColumn(statusId) {
        const st = AppController.statuses;
        let idx = -1;
        for (let i = 0; i < st.length; ++i) { if (st[i].id === statusId) { idx = i; break; } }
        if (idx < 0) return;
        const it = colRepeater.itemAt(idx);
        const maxX = Math.max(0, hscroll.contentWidth - hscroll.width);
        colScrollAnim.to = Math.max(0, Math.min(it ? it.x : 0, maxX));
        colScrollAnim.restart();
        root._focusPulseStatus = statusId;
        focusPulseTimer.restart();
    }

    // A focus requested from another view fires focusedStatusChanged before this
    // board exists; pick it up once the board is created and laid out.
    Component.onCompleted: {
        root._syncColumns();
        root._loadCollapsed();
        root._loadDoneShown();
        root._loadEmptyOpen();
        if (AppController.focusedStatus && AppController.focusedStatus.length > 0)
            Qt.callLater(function() { root.focusColumn(AppController.focusedStatus); });
    }

    // Walk every column's TaskCard repeater, collect ids of currently
    // visible cards (respects search/priority/archived filters), hand them
    // to AppController as the new selection set.
    function columnCommand(kind) {
        const cols = root._visibleByColumn();
        const pos = root._cursorPos(cols);
        const idx = pos ? pos.col : 0;
        const c = colRepeater.itemAt(idx);
        if (!c) return;
        const sid = String(c["statusId"]);
        if (kind === "color") colorPopup.openFor(sid, c["statusColor"], c);
        else if (kind === "archive") archivePopup.openFor(sid, String(c["statusName"]));
        else if (kind === "doing" && sid !== "prog")
            AppController.setStatusDoing(sid, !AppController.isDoingStatus(sid));
    }
    function selectAllVisible() {
        AppController.setSelectedTaskIds(_flatVisibleIds());
    }

    // ── Keyboard cursor ───────────────────────────────────────────────
    // The board is the app's main surface and was mouse-only: there was no way
    // to move between cards, open one, or move one, without dragging.
    //
    // The cursor is a task id rather than a (column, row) pair, so it survives
    // a filter change, a drag, or the card moving column — all of which
    // renumber the rows underneath it.
    property string cursorTaskId: ""
    // The column whose header holds the keyboard (Tab onto one of its
    // buttons); "" when none. Ctrl+Shift+H / L move that column (DG-133,
    // keymap "на заголовке колонки"); a header marks itself with `headerOf`.
    function focusedHeaderStatus() {
        for (let it = root.Window.activeFocusItem; it; it = it.parent)
            if (typeof it.headerOf === "string" && it.headerOf.length > 0) return it.headerOf;
        return "";
    }
    function moveFocusedColumn(dx) {
        const sid = root.focusedHeaderStatus();
        if (!sid) return;
        const sts = AppController.statuses;
        const i = sts.findIndex(st => st.id === sid);
        if (i < 0 || i + dx < 0 || i + dx >= sts.length) return;
        AppController.moveStatus(sid, i + dx);
        // The header keeps the keyboard, so the key can be pressed again.
        Qt.callLater(function () {
            for (let k = 0; k < colRepeater.count; k++) {
                const c = colRepeater.itemAt(k);
                if (c && c["statusId"] === sid) { c["focusHeader"](); return; }
            }
        });
    }
    // The ring is drawn only once the keyboard has moved the cursor. A click
    // still puts the cursor on the card (so J/K carry on from there), but a
    // ring left behind by a mouse click read as the card being stuck
    // selected after its editor closed.
    property bool cursorVisible: false
    function clearCursor() {
        root.cursorTaskId = "";
        root.cursorVisible = false;
    }
    function clearSelectionAndCursor() {
        if (AppController.selectionCount > 0) AppController.clearSelection();
        root.clearCursor();
    }

    // Visible ids per column, in board order. The same walk _flatVisibleIds()
    // does, but keeping the column structure that left/right needs.
    function _visibleByColumn() {
        const cols = [];
        for (let c = 0; c < colRepeater.count; c++) {
            const col = colRepeater.itemAt(c);
            if (!col || !col.taskFilter) { cols.push({ statusId: "", ids: [] }); continue; }
            // A folded column shows no cards, so the cursor walks past it.
            cols.push({ statusId: col.statusId, ids: col.folded ? [] : col.taskFilter.ids() });
        }
        return cols;
    }

    // The index of the folded column that holds `id`, or -1.
    function _foldedColumnOf(id) {
        if (!id) return -1;
        for (let c = 0; c < colRepeater.count; c++)
            if (root._columnHolds(colRepeater.itemAt(c), id)) return c;
        return -1;
    }
    // Whether the folded column `col` holds `id`.
    function _columnHolds(col, id) {
        return !!col && col.folded && !!col.taskFilter && col.taskFilter.ids().indexOf(id) >= 0;
    }

    // Where the cursor currently sits, or null when it points at nothing on
    // screen (a filter may have hidden it).
    function _cursorPos(cols) {
        for (let c = 0; c < cols.length; c++) {
            const r = cols[c].ids.indexOf(root.cursorTaskId);
            if (r >= 0) return { col: c, row: r };
        }
        return null;
    }

    // First visible card, used when a key arrives with no cursor yet.
    function _firstVisible(cols) {
        for (let c = 0; c < cols.length; c++)
            if (cols[c].ids.length > 0) return cols[c].ids[0];
        return "";
    }

    // ── Selecting from the keyboard (APP-128) ─────────────────────────
    // Shift+Up/Down grows a range from an anchor, the card the run started
    // on, over whatever was selected before it; walking back shrinks the
    // range again. Any other cursor move (plain arrows, a click, a column
    // change) ends the run.
    property string _selAnchor: ""
    property var _selBase: []
    property bool _extending: false
    onCursorTaskIdChanged: {
        if (!root._extending) root._selAnchor = "";
        if (root.cursorVisible) Qt.callLater(root.revealCursor);
        root._seeCursor();
    }
    onCursorVisibleChanged: {
        if (root.cursorVisible) Qt.callLater(root.revealCursor);
        root._seeCursor();
    }
    // A new card the keyboard cursor reaches has been seen (APP-180).
    function _seeCursor() {
        if (root.cursorVisible && root.cursorTaskId) AppController.markTaskSeen(root.cursorTaskId);
    }

    // The board scrolls to the card the keyboard moved to (VISU-19, PERA-7):
    // the cursor walked into a column past the right edge and the board
    // stayed put, leaving 60px of the card on screen. Sideways, the whole
    // column comes into view; down the column, the card does.
    function revealCursor() {
        if (!root.cursorVisible || !root.cursorTaskId) return;
        const cols = _visibleByColumn();
        const pos = _cursorPos(cols);
        if (!pos) return;
        const col = colRepeater.itemAt(pos.col);
        if (!col) return;
        // Down the column: _cardItem() scrolls the card's list to it.
        root._cardItem(root.cursorTaskId);
        const maxX = Math.max(0, hscroll.contentWidth - hscroll.width);
        let x = hscroll.contentX;
        if (col.x < x) x = col.x;
        else if (col.x + col.width > x + hscroll.width) x = col.x + col.width - hscroll.width;
        x = Math.max(0, Math.min(maxX, x));
        if (x !== hscroll.contentX) {
            outerAnim.stop();
            colScrollAnim.stop();
            hscroll.contentX = x;
        }
    }
    function _placeCursor(id) {
        root._extending = true;
        root.cursorTaskId = id;
        root._extending = false;
    }
    function _union(a, b) {
        const out = a.slice();
        for (const id of b) if (out.indexOf(id) < 0) out.push(id);
        return out;
    }

    function extendSelection(dy) {
        root.cursorVisible = true;
        const cols = _visibleByColumn();
        const pos = _cursorPos(cols);
        if (!pos) { root.cursorTaskId = _firstVisible(cols); return; }
        const ids = cols[pos.col].ids;
        if (root._selAnchor === "" || ids.indexOf(root._selAnchor) < 0) {
            root._selAnchor = root.cursorTaskId;
            root._selBase = AppController.selectedTaskIds.filter((id) => id !== root.cursorTaskId);
        }
        const r = Math.max(0, Math.min(ids.length - 1, pos.row + dy));
        root._placeCursor(ids[r]);
        const a = ids.indexOf(root._selAnchor);
        const range = ids.slice(Math.min(a, r), Math.max(a, r) + 1);
        AppController.setSelectedTaskIds(root._union(root._selBase, range));
    }

    // Shift+Left/Right: the whole column the cursor is in joins the
    // selection, and the cursor steps to the next column with cards, so a
    // second press takes that one too.
    function selectColumnAndStep(dx) {
        root.cursorVisible = true;
        const cols = _visibleByColumn();
        const pos = _cursorPos(cols);
        if (!pos) { root.cursorTaskId = _firstVisible(cols); return; }
        AppController.setSelectedTaskIds(root._union(AppController.selectedTaskIds, cols[pos.col].ids));
        for (let c = pos.col + dx; c >= 0 && c < cols.length; c += dx) {
            if (cols[c].ids.length > 0) {
                root.cursorTaskId = cols[c].ids[Math.min(pos.row, cols[c].ids.length - 1)];
                return;
            }
        }
    }

    // Ctrl+Left/Right: the selection, when there is one, goes to the column
    // beside the cursor's (or beside the first selected card's); otherwise
    // the cursor's card moves, as Shift+H/L always did.
    function moveSelectionOrCard(dx) {
        if (AppController.selectionCount === 0) { root.moveCursorCard(dx, 0); return; }
        const cols = _visibleByColumn();
        let from = _cursorPos(cols);
        if (!from || !AppController.isTaskSelected(root.cursorTaskId)) {
            const first = AppController.selectedTaskIds[0];
            from = null;
            for (let c = 0; c < cols.length && !from; c++)
                if (cols[c].ids.indexOf(first) >= 0) from = { col: c, row: 0 };
        }
        if (!from) return;
        const c = from.col + dx;
        if (c < 0 || c >= cols.length || !cols[c].statusId) return;
        AppController.moveSelectedTasksToStatus(cols[c].statusId);
        root._revealCursorSoon();
    }

    function moveCursor(dx, dy) {
        root.cursorVisible = true;
        const cols = _visibleByColumn();
        let pos = _cursorPos(cols);
        // On a card of a column z a just folded: up and down have nothing to
        // walk, so the cursor waits there for z a to unfold it; sideways it
        // steps on from that column (IDIOT-TASKS-13).
        const folded = pos ? -1 : root._foldedColumnOf(root.cursorTaskId);
        if (folded >= 0) {
            if (dx === 0) return;
            pos = { col: folded, row: 0 };
        }
        if (!pos) {
            root.cursorTaskId = _firstVisible(cols);
            return;
        }
        let c = pos.col;
        let r = pos.row;
        if (dy !== 0) {
            r = Math.max(0, Math.min(cols[c].ids.length - 1, r + dy));
        }
        if (dx !== 0) {
            // Skip empty columns rather than stopping at one — a gap in the
            // middle of the board should not swallow the cursor.
            let next = c;
            for (let step = c + dx; step >= 0 && step < cols.length; step += dx) {
                if (cols[step].ids.length > 0) { next = step; break; }
            }
            c = next;
            r = Math.max(0, Math.min(cols[c].ids.length - 1, r));
        }
        if (cols[c].ids.length === 0) return;
        root.cursorTaskId = cols[c].ids[r];
    }

    function openCursor() {
        root.cursorVisible = true;
        const cols = _visibleByColumn();
        if (!_cursorPos(cols)) { root.cursorTaskId = _firstVisible(cols); return; }
        if (root.cursorTaskId) root.taskClicked(root.cursorTaskId);
    }

    // The card the next key acts on: the cursor's, else the single selected
    // one, else the one under the pointer.
    function _actionCardId() {
        const cols = _visibleByColumn();
        if (root.cursorTaskId && _cursorPos(cols)) return root.cursorTaskId;
        if (AppController.selectionCount === 1) return AppController.selectedTaskIds[0];
        return root.hoveredTaskId || "";
    }
    function _cardItem(id) {
        for (let c = 0; c < colRepeater.count; c++) {
            const col = colRepeater.itemAt(c);
            if (!col || !col.taskList) continue;
            const list = col.taskList;
            const ids = col.taskFilter.ids();
            const row = ids.indexOf(id);
            if (row < 0) continue;
            list.positionViewAtIndex(row, ListView.Contain);
            list.forceLayout();
            return list.itemAtIndex(row);
        }
        return null;
    }
    // M (and the Menu key): the card menu for the card the keyboard is on.
    function openCursorMenu() {
        const id = root._actionCardId();
        if (!id) { root.moveCursor(0, 0); return; }
        root.cursorTaskId = id;
        root.cursorVisible = true;
        const card = root._cardItem(id);
        if (card && card.openMenu) card.openMenu();
    }
    // The CI popover of the card the keyboard is on (dogfood 2026-10-22):
    // what failed and the end of its log. A card with no CI says nothing.
    function openCursorCi() {
        const id = root._actionCardId();
        if (!id) { root.moveCursor(0, 0); return; }
        root.cursorTaskId = id;
        root.cursorVisible = true;
        const card = root._cardItem(id);
        if (card && card.openCi) card.openCi();
    }
    // E: archive the selection, or the cursor's card when nothing is selected.
    function archiveCursor() {
        if (AppController.selectionCount > 0) { AppController.setSelectedTasksArchived(true); return; }
        const id = root._actionCardId();
        if (!id) return;
        // Keep the cursor on the board: step to a neighbour first.
        const cols = _visibleByColumn();
        const pos = _cursorPos(cols);
        if (pos && id === root.cursorTaskId) {
            const ids = cols[pos.col].ids;
            root.cursorTaskId = pos.row + 1 < ids.length ? ids[pos.row + 1] : (pos.row > 0 ? ids[pos.row - 1] : "");
        }
        AppController.setArchived(id, true);
    }

    function toggleCursorSelection() {
        root.cursorVisible = true;
        const cols = _visibleByColumn();
        if (!_cursorPos(cols)) { root.cursorTaskId = _firstVisible(cols); return; }
        if (root.cursorTaskId) AppController.toggleTaskSelection(root.cursorTaskId);
    }

    // Move the card under the cursor. Vertically it swaps with its neighbour;
    // horizontally it changes column, landing at the same depth.
    //
    // Under a sort the column order on screen is not the manual order: Shift+K
    // under "Priority" moved nothing visible and rewrote the manual order
    // behind it (TASKS-1, audit 2026-09-30). A sorted column has no position
    // to move to, so a vertical move says so and a column change keeps the
    // card where the sort puts it - the same rule as a drop.
    function moveCursorCard(dx, dy) {
        root.cursorVisible = true;
        const cols = _visibleByColumn();
        const pos = _cursorPos(cols);
        if (!pos) { root.cursorTaskId = _firstVisible(cols); return; }
        const id = root.cursorTaskId;
        const manual = root.sortMode === "manual";

        if (dy !== 0 && !manual) {
            AppController.showToast(I18n.t("board.sorted.noReorder"));
            return;
        }
        if (dy !== 0) {
            const ids = cols[pos.col].ids;
            const target = pos.row + dy;
            if (target < 0 || target >= ids.length) return;
            // Moving down means landing after the card currently below, which
            // is "before the one after that".
            const beforeId = dy > 0
                ? (target + 1 < ids.length ? ids[target + 1] : "")
                : ids[target];
            AppController.moveTaskTo(id, cols[pos.col].statusId, beforeId);
            return;
        }

        let c = pos.col + dx;
        if (c < 0 || c >= cols.length) return;
        const destIds = cols[c].ids;
        const beforeId = manual && pos.row < destIds.length ? destIds[pos.row] : "";
        root.stackOnClose(id, cols[pos.col].statusId, cols[c].statusId, 1);
        AppController.moveTaskTo(id, cols[c].statusId, beforeId);
        root._revealCursorSoon();
    }

    // Flat ordered list of visible task ids across the entire board, column
    // by column in render order, top-to-bottom inside each column. Used by
    // selectAllVisible() and shift-range select. A folded column shows no
    // cards: Ctrl+A, Del deleted the ones it hid (IDIOT-TASKS-14).
    function _flatVisibleIds() {
        const out = [];
        for (const col of root._visibleByColumn())
            for (let i = 0; i < col.ids.length; i++) out.push(col.ids[i]);
        return out;
    }

    // Shift-click range: spans the flat visible list (cross-column). First
    // shift-click sets the anchor; subsequent shift-click selects every
    // ticket between anchor and target inclusive.
    function _rangeSelect(targetId) {
        const ordered = _flatVisibleIds();
        if (ordered.length === 0) return;
        const haveAnchor = root.shiftAnchorId && ordered.indexOf(root.shiftAnchorId) >= 0;
        if (!haveAnchor) {
            root.shiftAnchorId = targetId;
            AppController.toggleTaskSelection(targetId);
            return;
        }
        const ai = ordered.indexOf(root.shiftAnchorId);
        const ti = ordered.indexOf(targetId);
        const lo = Math.min(ai, ti);
        const hi = Math.max(ai, ti);
        const merged = AppController.selectedTaskIds.slice();
        for (let i = lo; i <= hi; i++) {
            if (merged.indexOf(ordered[i]) < 0) merged.push(ordered[i]);
        }
        AppController.setSelectedTaskIds(merged);
    }

    // ── Closing a task: the done moment (APP-176, APP-205) ──────────────
    // A single card moved into Done folds into a bar where it stood and
    // flies into the Done column's counter, which ticks up as it lands.
    // The one big move in the app; a bulk move and "Reduce motion" skip it,
    // and so does a Done column that is folded or off screen. Called just
    // before the move, while the card is still where the user saw it.
    // (Done used to keep a stack of bars under its header for the card to
    // land on; it was noise over the first card, APP-205.)
    property bool stackRunning: false
    // The columns cannot name the board, so they reach it through this: the
    // board asks, the column the card goes to answers with where its counter
    // is (it owns that), and the board flies the card there. A drop or a
    // card's status menu asks the board the same way.
    component StackBus: QtObject {
        // The column whose counter waits while a card is on its way.
        property string holdStatus: ""
        signal requested(Item card, string title, string toStatus)
        signal launchRequested(Item card, string title, string toStatus, real colLeft, real colRight,
                               real toX, real toY, real toW, color barColor)
        signal dropped(var source, string toStatus, int moved)
    }
    StackBus { id: stackBus }
    Connections {
        target: stackBus
        function onLaunchRequested(card: Item, title: string, toStatus: string, colLeft: real, colRight: real,
                                   toX: real, toY: real, toW: real, barColor: color) {
            root.launchStack(card, title, toStatus, colLeft, colRight, toX, toY, toW, barColor);
        }
        function onDropped(source: var, toStatus: string, moved: int) {
            root.stackOnDrop(source, toStatus, moved);
        }
    }
    function stackOnClose(id, fromStatus, toStatus, moved) {
        if (!Motion.shouldStack(moved, fromStatus, toStatus, Theme.motion)) return false;
        let card = null;
        for (let c = 0; c < colRepeater.count && !card; c++) card = root._cardIn(colRepeater.itemAt(c), id);
        if (!card || !card.task) return false;
        stackBus.requested(card, String(card.task.title || ""), toStatus);
        return root.stackRunning;
    }
    // A card dropped or sent from its menu: anything with the card's
    // taskId and task.
    function stackOnDrop(source, toStatus, moved) {
        if (!source || !source.taskId) return false;
        return root.stackOnClose(source.taskId, source.task ? source.task.status : "", toStatus, moved);
    }
    // The card for `id` if this column's list has built it; never scrolls.
    function _cardIn(col, id) {
        if (!col || !col.taskList) return null;
        const row = col.taskFilter.ids().indexOf(id);
        return row >= 0 ? col.taskList.itemAtIndex(row) : null;
    }
    // From the Done column: its left and right edge and its counter, in
    // scene coordinates. Nothing flies to a column off screen.
    function launchStack(card: Item, title: string, toStatus: string, colLeft: real, colRight: real,
                         toX: real, toY: real, toW: real, barColor: color): bool {
        const vp = hscroll.mapToItem(null, 0, 0);
        if (colLeft < vp.x || colRight > vp.x + hscroll.width + 1) return false;
        const from = card.mapToItem(root, 0, 0);
        const to = root.mapFromItem(null, toX, toY);
        stackFlyer.launch(from.x, from.y, card.width, card.height, title, to.x, to.y, toW, barColor);
        stackBus.holdStatus = toStatus;
        return true;
    }

    // Columns that do not fit sit off to the right (APP-200): how many, so
    // the board can say so instead of letting one hide under the panel.
    readonly property int hiddenColumnsRight: {
        const edge = hscroll.contentX + hscroll.width;
        const _deps = [rowL.implicitWidth, colRepeater.count, root.collapsed, root.doneShown, root.columnWidth, root.boardFit];
        let n = 0;
        for (let c = 0; c < colRepeater.count; c++) {
            const it = colRepeater.itemAt(c);
            // Any part past the edge: a column cut at a third had no chip (EYES-1).
            if (it && it.x + it.width > edge + 1) n++;
        }
        return n;
    }

    // The priorities the filter bar has switched on, as a plain list for the
    // per-column proxies. Empty means "no priority filter" — which is what an
    // all-chips-off filter bar means, not "nothing passes".
    readonly property var activePriorities: {
        const out = [];
        for (const k in root.prioritiesFilter) if (root.prioritiesFilter[k]) out.push(k);
        return out;
    }

    function _scrollOuter(dy) {
        if (dy === 0) return;
        const maxX = Math.max(0, hscroll.contentWidth - hscroll.width);
        if (maxX <= 0) return;
        const base = outerAnim.running ? outerAnim.to : hscroll.contentX;
        const newX = Math.max(0, Math.min(maxX, base - dy));
        if (newX === base) return;
        outerAnim.from = hscroll.contentX;
        outerAnim.to = newX;
        outerAnim.restart();
    }

    NumberAnimation {
        id: outerAnim
        target: hscroll
        property: "contentX"
        duration: Theme.durMove
        easing.type: Theme.easeEnter
    }

    Flickable {
        id: hscroll
        objectName: "board-hscroll"
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadX
        anchors.rightMargin: Theme.pagePadX
        anchors.topMargin: Style.fills ? Theme.spXs : Theme.px(16)
        anchors.bottomMargin: Theme.sp2xl
        contentWidth: rowL.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        clip: true
        // The columns that do not fit sit off to the right with nothing to
        // say so but a card cut in half (design audit DES-6): while the board
        // overflows, its scrollbar stays in view.
        ScrollBar.horizontal: ThinScrollBar {
            objectName: "board-hscrollbar"
            policy: hscroll.contentWidth > hscroll.width ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }
        // pressDelay: 0 — keep card-drag activation instant on press.
        // Horizontal flick by dragging empty board space is rarely used
        // on desktop: the mouse wheel scrolls the board sideways where no
        // column has anything to scroll (ScrollFeel, the app-wide wheel).
        pressDelay: 0
        // A click on empty board (between or below the columns) lets go of
        // the selection and the keyboard cursor. Cards take their own clicks
        // first, so this only sees the empty space.
        TapHandler {
            onTapped: { root.clearSelectionAndCursor(); root.emptyClicked(); }
        }

        Row {
            id: rowL
            // A strip under the columns for the scrollbar: it was drawn over
            // the last card of every column (EYES-6).
            height: hscroll.height - (hscroll.contentWidth > hscroll.width ? Theme.spMd : 0)
            spacing: root.columnGap

            Repeater {
                id: colRepeater
                model: colModel
                // The board's search state too, for the column's empty state
                // (Qt 6.9's qmllint does not resolve root inside the column).
                onItemAdded: (index, item) => {
                    item["bus"] = stackBus;
                    item["filtering"] = Qt.binding(() => root._filtering);
                    item["nothingFound"] = Qt.binding(() => root._nothingFound);
                    item["boardEmpty"] = Qt.binding(() => root._boardTotal === 0);
                    item["board"] = root;
                    item["isDone"] = Qt.binding(() => root.isDoneColumn(item["statusId"]));
                    item["autoStrip"] = Qt.binding(() => root.isAutoStrip(item["statusId"]));
                    item["folded"] = Qt.binding(() => root.isFolded(item["statusId"]) || item["autoStrip"]);
                }
                // A column's way to the stack: its drops and its cards' menus
                // ask the board through this, not by the board's id.

                Rectangle {
                    id: col
                    required property string sid
                    required property string sname
                    required property string scolor
                    required property int swip
                    required property int index
                    readonly property string statusId: sid
                    readonly property string statusName: sname
                    function startRename() { col.renaming = true; renameField.forceActiveFocus(); renameField.selectAll(); }
                    function finishRenameOnBlur() {
                        if (!col.renaming || renameField.activeFocus) return;
                        let inBoard = false;
                        for (let p = renameField.Window.activeFocusItem; p; p = p.parent) {
                            if (p === renameField.Overlay.overlay) break;
                            if (p === col.board) { inBoard = true; break; }
                        }
                        if (inBoard) AppController.renameStatus(col.statusId, renameField.text.trim());
                        else renameField.text = col.statusName;
                        col.renaming = false;
                    }
                    readonly property color statusColor: scolor
                    // Set by colRepeater (Qt 6.9's qmllint does not resolve
                    // root inside the column): the board, whether this is a
                    // done-stage column, and whether it is folded (APP-262).
                    property var board: null
                    property bool isDone: false
                    readonly property string category: (AppController.statuses, AppController.statusCategory(col.statusId))
                    property bool folded: false
                    // Folded by the narrow board (BoardFit), not by its own
                    // fold; a Done folded so is a strip too, not the wide
                    // "Show" column.
                    property bool autoStrip: false
                    readonly property bool strip: col.folded && (!col.isDone || col.autoStrip)
                    readonly property alias taskList: bodyFlick
                    readonly property alias taskFilter: colFilter
                    property bool dragOver: false
                    // The card over it is a tracker card with writes off (a-30).
                    property bool dropLocalOnly: false
                    // Folding or opening under a drag: the other drop area
                    // takes over on the next move.
                    onFoldedChanged: col.dragOver = false

                    // ── Room for a dragged card (BoardDrag sheet) ─────────
                    // Under the manual order the column opens an empty slot
                    // the height of the card where it would land, and the
                    // cards below slide down; the place the card came from
                    // stays a dim slot while it is over its own column and
                    // closes when it leaves. Under a sort only the column is
                    // marked: the sort decides where the card goes.
                    readonly property var dragCard: col.board ? col.board.dragCard : null
                    readonly property bool manual: !col.board || col.board.sortMode === "manual"
                    readonly property bool settling: !!col.board && col.board.settling
                    readonly property bool isOrigin: col.dragCard !== null && col.dragCard["_homeParent"] === bodyFlick.contentItem
                    // Where the slot opens, in the list's own layout, and
                    // whether that is where the card already is (no slot:
                    // its own place is the slot).
                    property real slotLayoutY: 0
                    property bool slotNoop: false
                    readonly property bool slotOn: col.dragOver && !col.folded && col.dragCard !== null
                                                   && col.manual && !col.slotNoop
                    readonly property real slotH: col.dragCard !== null ? col.dragCard.height : 0
                    readonly property bool originOpen: col.isOrigin && col.dragOver
                    readonly property bool originCollapsed: col.isOrigin && !col.dragOver
                    readonly property real originY: col.isOrigin ? Number(col.dragCard["homeY"]) : 0
                    // How far a card at layout `y` moves aside.
                    function shiftFor(y, lifted) {
                        if (lifted || col.dragCard === null) return 0;
                        let s = 0;
                        if (col.originCollapsed && y > col.originY) s -= col.slotH + bodyFlick.spacing;
                        if (col.slotOn && y >= col.slotLayoutY) s += col.slotH + bodyFlick.spacing;
                        return s;
                    }
                    readonly property int visibleCount: colFilter.count
                    // Set by colRepeater: a search or filter is on / it hides
                    // every card on the board / the board has no cards at all.
                    property bool filtering: false
                    property bool nothingFound: false
                    property bool boardEmpty: false
                    // Advisory work-in-progress limit. 0 = none. Over the
                    // limit the badge turns, and that is all it does: a hard
                    // cap would make a drag silently do nothing, which reads
                    // as a bug rather than as a rule.
                    readonly property int wipLimit: col.swip
                    // The column's real load, not what a filter leaves on
                    // screen (IDIOT-TASKS-12).
                    readonly property int loadCount: Number(AppController.statusCounts[col.statusId] || 0)
                    readonly property bool overWip: col.wipLimit > 0 && col.loadCount > col.wipLimit
                    property bool renaming: false
                    readonly property bool isFirst: index === 0
                    readonly property bool isLast:  index === AppController.statuses.length - 1
                    // Drives the reveal of the header's move/delete icons. A
                    // HoverHandler, not the MouseArea below the header row:
                    // hover stops at the first item that accepts it, so with a
                    // MouseArea driving this the icons vanished the moment the
                    // pointer touched the column name (or one of the icons),
                    // which made them impossible to click.
                    property bool headerHovered: false
                    // The icons also show while the keyboard is on one of them
                    // (design audit DES-19): hidden at rest, they stay on the
                    // Tab path, as IconButton's do.
                    readonly property bool headerKeyFocus: moveLeftIcon.keyFocused || moveRightIcon.keyFocused
                                                           || deleteIcon.keyFocused || foldIcon.keyFocused
                    readonly property bool headerRevealed: col.headerHovered || col.headerKeyFocus
                    // After Ctrl+Shift+H / L the header keeps the keyboard (DG-133).
                    function focusHeader() { foldIcon.focusKey(); }
                    // Briefly emphasised when the sidebar Blocked / Code Review
                    // button jumps focus to this column.
                    readonly property bool focusPulse: root._focusPulseStatus === col.statusId
                    // Set by the board when the column is made.
                    property StackBus bus: null
                    // While a closed card is on its way the counter keeps
                    // the count from before the move, and ticks up when the
                    // card lands in it (APP-205).
                    readonly property bool stackHold: !!col.bus && col.bus.holdStatus === col.statusId
                    property int heldCount: 0
                    readonly property int shownCount: col.stackHold ? col.heldCount : col.visibleCount
                    onStackHoldChanged: if (!col.stackHold && Theme.motion > 0) cntLand.restart()
                    // The board asks for a card closed into this column.
                    Connections {
                        target: col.bus
                        function onRequested(card: Item, title: string, toStatus: string) {
                            // A folded Done (the default, APP-262) still
                            // takes the card, onto its folded header.
                            if (toStatus !== col.statusId || col.strip) return;
                            col.heldCount = col.visibleCount;
                            const left = col.mapToItem(null, 0, 0);
                            // Onto the counter: a bar as wide as the pill,
                            // through its middle.
                            const target = col.folded ? doneHead : cntPill;
                            const pill = target.mapToItem(null, 0, (target.height - 4) / 2);
                            col.bus.launchRequested(card, title, col.statusId, left.x, left.x + col.width, pill.x, pill.y,
                                                    target.width, col.statusColor);
                        }
                    }

                    width: !col.board ? 0 : col.strip ? col.board.foldedWidth : col.folded ? col.board.doneFoldedWidth : col.board.columnWidth
                    height: rowL.height
                    radius: Theme.radius
                    // No frame and no fill (APP-262): a drop target is marked
                    // by an outline, a shape rather than a colour change.
                    color: Theme.surfaceColumn
                    border.color: focusPulse ? Theme.focusRing : "transparent"
                    border.width: focusPulse ? 2 : 1
                    clip: true
                    Behavior on border.color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }

                    // The drop target's frame hugs the column's cards, not
                    // the window (R3-050): 1.5px blue in bold, a 1px line in
                    // quiet (R3-049). A folded Done becomes a tall strip.
                    Rectangle {
                        id: dropFrame
                        objectName: "column-drop-frame"
                        visible: col.dragOver
                        z: 30
                        width: parent.width
                        height: col.strip ? col.height : col.folded ? Math.min(col.height, Theme.px(200))
                              : Math.min(col.height, Theme.px(38) + 2 * Theme.spMd + bodyFlick.contentHeight)
                        radius: col.radius
                        color: "transparent"
                        border.color: Style.urgency ? Theme.info : Theme.borderStrong
                        border.width: Style.urgency ? 1.5 : 1
                    }
                    // Folded Done under the pointer: ring, name and
                    // "отпустите" in the middle of the strip (R3-053).
                    Column {
                        objectName: "column-done-drop"
                        visible: col.folded && !col.strip && col.dragOver
                        z: 31
                        anchors.horizontalCenter: dropFrame.horizontalCenter
                        y: (dropFrame.height - height) / 2
                        spacing: Theme.spXs
                        StatusRing {
                            anchors.horizontalCenter: parent.horizontalCenter
                            category: "done"
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: col.statusName
                            color: Theme.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsMd
                            font.weight: Theme.fwTitle
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: I18n.t("kanban.done.drop")
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                        }
                        Text {
                            objectName: "column-done-drop-local-only"
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: col.dropLocalOnly
                            text: I18n.t("kanban.drop.localOnly")
                            color: Theme.textDim
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                        }
                    }

                    // Folded: the name runs down the strip, with the count; a
                    // click (or Z on the board) opens it again.
                    Item {
                        objectName: "column-done-folded"
                        anchors.fill: parent
                        visible: col.folded && !col.strip && !col.dragOver
                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.spMd
                            anchors.rightMargin: Theme.spMd
                            anchors.top: parent.top
                            anchors.topMargin: (Theme.px(38) - doneHead.height) / 2
                            spacing: Theme.spMd
                            Row {
                                id: doneHead
                                spacing: Theme.spXs
                                StatusRing {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: Style.chipFill
                                    category: "done"
                                }
                                // Measured, not the Text's own implicitWidth: an
                                // elided Text redoes that when its width changes.
                                TextMetrics {
                                    id: doneNameText
                                    font: doneName.font
                                    text: doneName.text
                                }
                                Text {
                                    id: doneName
                                    objectName: "column-done-folded-name"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(Math.ceil(doneNameText.advanceWidth), col.width - 2 * Theme.spMd
                                                    - (Style.chipFill ? doneCount.implicitWidth + Theme.statusRingSize + 2 * Theme.spXs : 0))
                                    elide: Text.ElideRight
                                    // Quiet: "Done · 2"; bold: ring, name, count.
                                    text: col.statusName + (Style.chipFill ? "" : " · " + col.shownCount)
                                    color: Style.chipFill ? Theme.text : Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsMd
                                    font.weight: Style.chipFill ? Theme.fwHeading : Theme.fwBody
                                }
                                Text {
                                    id: doneCount
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: Style.chipFill
                                    text: col.shownCount
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsMd
                                }
                            }
                            Text {
                                objectName: "column-done-show"
                                // Quiet: the header itself opens it (Q-Board).
                                // Nothing done, nothing to show (F3-EYES-18).
                                visible: (Style.chipFill && col.shownCount > 0) || col.dragOver
                                text: col.dragOver ? I18n.t("kanban.done.drop") : I18n.t("kanban.done.show")
                                color: col.dragOver ? Theme.text : Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                // A dim rule a little under the baseline, as the
                                // sheet draws a link (b-25).
                                Rectangle {
                                    objectName: "column-done-show-underline"
                                    visible: !col.dragOver
                                    y: parent.baselineOffset + Theme.px(3)
                                    width: parent.contentWidth
                                    height: 1
                                    color: Theme.borderStrong
                                }
                                ClickArea {
                                    objectName: "column-done-show-area"
                                    enabled: col.folded && !col.strip
                                    label: I18n.t("kanban.done.show") + " " + col.statusName
                                    showTip: false
                                    onActivated: col.board.toggleCollapsed(col.statusId)
                                }
                            }
                        }
                    }
                    Item {
                        objectName: "column-folded"
                        anchors.fill: parent
                        visible: col.strip
                        Column {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: Theme.spLg
                            spacing: Theme.spLg
                            Text {
                                objectName: "column-folded-mark"
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: Theme.statusMark(col.statusId)
                                color: col.statusColor
                                font.pixelSize: Theme.fsSm
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: col.visibleCount
                                color: Theme.textDim
                                font.family: Theme.fontUi
                                font.features: Theme.tabularNums
                                font.pixelSize: Theme.fsSm
                            }
                            Item {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: foldedName.implicitHeight
                                height: foldedName.implicitWidth
                                Text {
                                    id: foldedName
                                    anchors.centerIn: parent
                                    rotation: 90
                                    text: col.statusName
                                    color: Theme.text
                                    font.pixelSize: Theme.fsMd
                                    font.weight: Theme.fwTitle
                                }
                            }
                        }
                        ClickArea {
                            objectName: "column-expand"
                            enabled: col.folded
                            label: I18n.t("kanban.expand") + " " + col.statusName
                            tip: I18n.t("kanban.expand")
                            shortcutId: "board.collapseColumn"
                            onActivated: col.board.expandStrip(col.statusId)
                        }
                    }
                    // A card held over a strip the narrow board folded opens
                    // it after 0.4 s, until the drag ends, so it can land in
                    // place (BoardDrag sheet).
                    Timer {
                        id: stripDwell
                        interval: 400
                        onTriggered: if (col.dragOver && col.autoStrip) col.board._dragOpen = col.statusId
                    }
                    // A folded column is still a drop target; the outline
                    // says so while a card is over it (X-Oth-Select-Drag).
                    DropArea {
                        objectName: "column-folded-drop"
                        anchors.fill: parent
                        enabled: col.folded
                        onEntered: (drag) => {
                            if (drag.hasUrls && !col.board.dragTaskId(drag.source)) { drag.accepted = false; return; }
                            col.dropLocalOnly = col.board.dropStaysLocal(drag.source, col.statusId);
                            col.dragOver = true;
                            if (col.autoStrip) stripDwell.restart();
                        }
                        onExited: { col.dragOver = false; stripDwell.stop(); }
                        onDropped: (drop) => {
                            stripDwell.stop();
                            col.board.beginSettle();
                            col.dragOver = false;
                            const id = col.board.dragTaskId(drop.source);
                            if (!id) return;
                            if (AppController.isTaskSelected(id) && AppController.selectionCount > 1)
                                AppController.moveSelectedTasksToStatus(col.statusId);
                            else
                                AppController.moveTaskTo(id, col.statusId, "");
                            drop.accept(Qt.MoveAction);
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        visible: !col.folded

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            color: "transparent"
                            HoverHandler { onHoveredChanged: col.headerHovered = hovered }
                            readonly property string headerOf: col.statusId
                            // The header's menu from the keyboard: Menu or
                            // Shift+F10 on any of its buttons, which pass the
                            // key up to here.
                            Keys.onMenuPressed: colHeaderMenu.popup()
                            // Ctrl Shift H / L on the header (APP-278, DG-133)
                            // are catalogue keys now (board.columnLeft /
                            // Right): KeyRouter runs them while a header holds
                            // the keyboard (moveFocusedColumn), so they are
                            // rebindable and listed; elsewhere Ctrl Shift L is
                            // the log.
                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_F2) {
                                    col.startRename();
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
                                    colHeaderMenu.popup();
                                    event.accepted = true;
                                }
                            }
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                height: 1; color: Theme.border
                                visible: Style.chipFill
                            }
                            RowLayout {
                                id: headRow
                                anchors.fill: parent
                                spacing: Theme.spMd
                                // The column's stage as a shape (APP-259), not
                                // its colour; the colour is in the menu.
                                StatusRing {
                                    id: colorSwatch
                                    objectName: "column-stage"
                                    category: col.category
                                }
                                Item {
                                    id: nameBox
                                    // As wide as the name, shrinking only when
                                    // the column has no room for it (then the
                                    // count still follows it).
                                    Layout.preferredWidth: Math.max(0, Math.min(nameMetrics.advanceWidth + 1,
                                        headRow.width - colorSwatch.width - cntPill.implicitWidth - 3 * headRow.spacing))
                                    Layout.preferredHeight: 22
                                    TextMetrics {
                                        id: nameMetrics
                                        font: colName.font
                                        text: col.statusName
                                    }
                                    Text {
                                        id: colName
                                        objectName: "column-name"
                                        visible: !col.renaming
                                        anchors.verticalCenter: parent.verticalCenter
                                        // The name as the user wrote it. Uppercase with
                                        // tracking shouted across seven columns and cut
                                        // "In progress" to "IN PROGR…" on a 1680px window.
                                        text: col.statusName
                                        color: Style.fills ? Theme.text : Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsMd
                                        font.weight: Style.fills ? Theme.fwHeading : Theme.fwBody
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        visible: !col.renaming
                                        onDoubleClicked: { col.renaming = true; renameField.forceActiveFocus(); renameField.selectAll() }
                                        cursorShape: Qt.IBeamCursor
                                        ToolTip.visible: containsMouse
                                        // A name cut short is given whole here
                                        // (VISU-6), ahead of the rename hint.
                                        ToolTip.text: colName.truncated
                                            ? col.statusName + " · " + I18n.t("kanban.tip.rename")
                                            : I18n.t("kanban.tip.rename")
                                        ToolTip.delay: 500
                                        hoverEnabled: true
                                    }
                                    TextField {
                                        id: renameField
                                        ContextMenu.menu: TextEditMenu { editor: renameField }
                                        visible: col.renaming
                                        anchors.fill: parent
                                        text: col.statusName
                                        // Typing breaks the declarative binding above; re-sync to the
                                        // authoritative name every time the field opens so an
                                        // Escape-cancelled edit can't linger and be auto-committed by
                                        // the blur handler on the next rename (onVisibleChanged below).
                                        color: Theme.text
                                        background: Rectangle { radius: Theme.radiusSm; color: Theme.panel; border.color: Theme.accent; border.width: 1 }
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsMd
                                        font.weight: Theme.fwTitle
                                        selectByMouse: true
                                        // As long as a saved view's name (IDIOT-TASKS-16).
                                        maximumLength: 60
                                        // Done or not, the keyboard goes back to the board: the
                                        // hidden field kept it, j and d were typed into it and
                                        // Esc did nothing (IDIOT-TASKS-1).
                                        onAccepted: { AppController.renameStatus(col.statusId, text.trim()); col.renaming = false; col.board.forceActiveFocus() }
                                        // A blur commits only when the person went on inside the
                                        // board (a click on another column); a view switch or a
                                        // popup taking the keyboard is a cancel, as Esc is: Ctrl+1
                                        // or Ctrl+K renamed the column to the half-typed name
                                        // (IDIOT-TASKS-15). Read once focus has landed.
                                        onActiveFocusChanged: if (!activeFocus && col.renaming) Qt.callLater(col.finishRenameOnBlur)
                                        // Put the name back before letting go: hiding the
                                        // field blurs it, and the blur handler above commits
                                        // whatever is in it while renaming is still set.
                                        Keys.onEscapePressed: { text = col.statusName; col.renaming = false; col.board.forceActiveFocus() }
                                        onVisibleChanged: {
                                            if (visible) text = col.statusName;
                                            else if (activeFocus) col.board.forceActiveFocus();
                                        }
                                    }
                                }
                                // The count, a plain number (APP-262); over the
                                // WIP limit it says so in words and, in the
                                // bold style only, in red.
                                Rectangle {
                                    id: cntPill
                                    objectName: "column-count"
                                    // A narrow column has room for the name and
                                    // one fact: while a card that stays here is
                                    // over it, that fact is "только здесь" (a-30).
                                    visible: !localOnlyText.visible
                                    radius: Theme.radiusPill
                                    color: "transparent"
                                    implicitWidth: cntT.implicitWidth
                                    implicitHeight: 18
                                    Text {
                                        id: cntT; anchors.centerIn: parent
                                        text: col.wipLimit > 0
                                            ? col.shownCount + "/" + col.wipLimit
                                            : col.shownCount
                                        color: col.overWip ? Theme.signalUrgent : Theme.textDim
                                        font.family: Theme.fontUi
                                        font.features: Theme.tabularNums
                                        font.pixelSize: Theme.fsSm
                                        font.weight: col.overWip ? Theme.fwTitle : Theme.fwBody
                                    }
                                    // The count and the over-limit warning for a
                                    // screen reader, not only on hover (APP-184).
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: col.overWip
                                        ? I18n.t("kanban.wip.over").arg(col.statusName).arg(col.wipLimit)
                                        : cntT.text
                                    QQC.ToolTip.visible: col.overWip && wipHover.hovered
                                    QQC.ToolTip.text: I18n.t("kanban.wip.over").arg(col.statusName).arg(col.wipLimit)
                                    HoverHandler { id: wipHover }
                                    // The closed card has landed: a small
                                    // pop as the count ticks up.
                                    SequentialAnimation {
                                        id: cntLand
                                        NumberAnimation { target: cntPill; property: "scale"; to: 1.18; duration: Theme.durTap; easing.type: Theme.easeEnter }
                                        NumberAnimation { target: cntPill; property: "scale"; to: 1; duration: Theme.durPop; easing.type: Theme.easeExit }
                                    }
                                }
                                // The column's estimates added up (APP-246): a
                                // fact beside the count, tasks without one
                                // counted apart. Quiet shows it on hover only.
                                // Cut mid-word at the column's edge ("~105 h :",
                                // "~3 h 45 mir", A2-7, A4-24): it shows whole,
                                // in whole hours, or not at all; the tooltip and
                                // the screen reader keep the exact sum.
                                TextMetrics { id: estFull; font: colEst.font; text: colEst.fullText }
                                TextMetrics { id: estShort; font: colEst.font; text: colEst.shortText }
                                Text {
                                    id: colEst
                                    objectName: "column-estimate"
                                    readonly property string fullText: "~" + I18n.fmtMinutes(col.estimate.minutes)
                                    readonly property string shortText: col.estimate.minutes >= 60
                                        ? "~" + I18n.fmtMinutes(Math.round(col.estimate.minutes / 60) * 60) : colEst.fullText
                                    readonly property real room: headRow.width - colorSwatch.width - nameBox.width
                                                                 - cntPill.implicitWidth - 4 * headRow.spacing
                                    // Gives way to "· только здесь" while a
                                    // card that stays here is over it (a-30).
                                    visible: col.estimate.minutes > 0 && (!Style.quiet || col.headerRevealed) && colEst.text.length > 0
                                             && !localOnlyText.visible
                                    Layout.preferredWidth: Math.max(0, Math.min(colEst.implicitWidth, colEst.room))
                                    elide: Text.ElideRight
                                    text: colEst.room >= estFull.advanceWidth + 1 ? colEst.fullText
                                        : colEst.room >= estShort.advanceWidth + 1 ? colEst.shortText : ""
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.features: Theme.tabularNums
                                    font.pixelSize: Theme.fsSm
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: colEst.fullText
                                        + (col.estimate.without > 0 ? " · " + I18n.count(col.estimate.without, "estimate.without") : "")
                                    QQC.ToolTip.visible: (col.estimate.without > 0 || colEst.text !== colEst.fullText) && estHover.hovered
                                    QQC.ToolTip.text: colEst.fullText
                                        + (col.estimate.without > 0 ? " · " + I18n.count(col.estimate.without, "estimate.without") : "")
                                    HoverHandler { id: estHover }
                                }
                                // A tracker card with writes off over the
                                // column: the move stays here, said in the
                                // header beside the name, over no card (a-30,
                                // X-Oth-Select-Drag). It takes the count's and
                                // the estimate's place for the drag: a 163 px
                                // column at 100 % has no room for all three.
                                TextMetrics { id: localOnlyFm; font: localOnlyText.font; text: I18n.t("kanban.drop.localOnly") }
                                Text {
                                    id: localOnlyText
                                    objectName: "column-drop-local-only"
                                    visible: col.dragOver && col.dropLocalOnly && !col.folded
                                    readonly property real room: headRow.width - colorSwatch.width - nameBox.width
                                        - 2 * headRow.spacing
                                    Layout.preferredWidth: Math.max(0, Math.min(Math.ceil(localOnlyFm.advanceWidth), room))
                                    elide: Text.ElideRight
                                    text: I18n.t("kanban.drop.localOnly")
                                    color: Theme.textDim
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsXs
                                }
                                // Off during the caption: its spacing is the
                                // caption's last few pixels.
                                Item { Layout.fillWidth: true; visible: !localOnlyText.visible }
                            }
                            // "+" at the header's end, laid over it like the
                            // other tools: shown on hover or the keyboard, and
                            // at rest it takes no room from the name (APP-262:
                            // the header is the ring, the name, the count).
                            Rectangle {
                                id: colAdd
                                readonly property bool shown: col.headerRevealed || addMA.keyboardFocused
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 22; height: 22
                                z: 3
                                radius: Theme.radiusSm
                                opacity: shown ? 1 : 0
                                color: addMA.hovered || addMA.keyboardFocused ? Theme.panel3 : Theme.bg
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                Text {
                                    anchors.centerIn: parent
                                    text: "+"
                                    color: addMA.hovered || addMA.keyboardFocused ? Theme.text : Theme.textDim
                                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                    font.pixelSize: Theme.fsLg
                                }
                                ClickArea {
                                    id: addMA
                                    objectName: "column-add"
                                    label: I18n.t("kanban.addTask")
                                    onActivated: col.board.createInStatus(col.statusId)
                                }
                            }
                            // Move-left / Move-right / Delete / Fold, laid over the
                            // end of the name while the pointer is on the header
                            // (design audit DES-6). They used to hold four slots
                            // in the row even while hidden, which left the name
                            // about 100px: "To Do" read "To …" and "К выполнению"
                            // "К вып…" on a 1600px window. Laid over, the name
                            // gets the whole width at rest and still nothing
                            // reflows when the icons fade in. `visible` carries
                            // only the structural conditions.
                            Rectangle {
                                objectName: "column-hover-icons"
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 22 + Theme.spMd
                                width: hoverIcons.implicitWidth + Theme.spSm
                                height: hoverIcons.implicitHeight
                                color: Theme.bg
                                opacity: col.headerRevealed ? 1 : 0
                                visible: !col.renaming
                                z: 2
                                Row {
                                    id: hoverIcons
                                    anchors.right: parent.right
                                    spacing: Theme.spXs
                                    HoverIcon {
                                        id: moveLeftIcon
                                        objectName: "column-move-left"
                                        icon: "chevron-left"; tip: I18n.t("kanban.moveLeft"); shortcutId: "board.columnLeft"
                                        visible: !col.isFirst
                                        revealed: col.headerRevealed
                                        onActivated: AppController.moveStatus(col.statusId, col.index - 1)
                                    }
                                    HoverIcon {
                                        id: moveRightIcon
                                        objectName: "column-move-right"
                                        icon: "chevron-right"; tip: I18n.t("kanban.moveRight"); shortcutId: "board.columnRight"
                                        visible: !col.isLast
                                        revealed: col.headerRevealed
                                        onActivated: AppController.moveStatus(col.statusId, col.index + 1)
                                    }
                                    HoverIcon {
                                        id: deleteIcon
                                        objectName: "column-delete"
                                        // A bin, not a cross: the cross read as "hide the
                                        // column" (IDIOT-TASKS-21).
                                        icon: "trash"; tip: I18n.t("kanban.deleteColumn")
                                        danger: true
                                        visible: AppController.statuses.length > 1
                                        revealed: col.headerRevealed
                                        onActivated: root.requestDeleteColumn(col.statusId, col.statusName)
                                    }
                                    HoverIcon {
                                        id: foldIcon
                                        objectName: "column-fold"
                                        icon: "collapse"; tip: I18n.t("kanban.collapse")
                                        shortcutId: "board.collapseColumn"
                                        revealed: col.headerRevealed
                                        onActivated: root.toggleCollapsed(col.statusId)
                                    }
                                }
                            }
                            MouseArea {
                                id: headHover
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: (mouse) => { if (mouse.button === Qt.RightButton) colHeaderMenu.popup() }
                                z: -1
                            }

                            AppMenu {
                                id: colHeaderMenu
                                objectName: "column-menu"
                                // The stage list opens once this menu has
                                // closed (TaskMenuHost does the same).
                                property bool _openStage: false
                                onClosed: if (colHeaderMenu._openStage) { colHeaderMenu._openStage = false; stageMenu.popup(); stageMenu.currentIndex = 1 + Math.max(0, col.board ? col.board.stages.indexOf(col.category) : 0); }
                                AppMenuHeader {
                                    text: I18n.t("colmenu.header").arg(col.statusName) + " · " + I18n.count(col.shownCount, "list.tasks")
                                }
                                AppMenuItem { objectName: "col-menu-new"; text: I18n.t("colmenu.newTask"); shortcutId: "task.newBelow"; onTriggered: root.createInStatus(col.statusId) }
                                AppMenuItem { objectName: "col-menu-new-column"; text: I18n.t("kanban.newColumn") + "…"; onTriggered: addColumnPopup.openAfter(col.index) }
                                AppMenuItem { objectName: "col-menu-rename"; text: I18n.t("kanban.rename"); keyText: "F2"; onTriggered: col.startRename() }
                                AppMenuItem {
                                    objectName: "col-menu-stage"
                                    text: I18n.t("colmenu.stage")
                                    opensList: true
                                    note: I18n.t("status.stage." + col.category).toLowerCase()
                                    onTriggered: colHeaderMenu._openStage = true
                                }
                                AppMenuItem { objectName: "col-menu-fold"; text: I18n.t("colmenu.fold"); shortcutId: "board.collapseColumn"; onTriggered: root.toggleCollapsed(col.statusId) }
                                AppMenuItem { objectName: "col-menu-wip"; text: I18n.t("colmenu.wip"); onTriggered: wipPopup.openFor(col.statusId, col.statusName, col.wipLimit, col) }
                                // When its cards go to the archive (APP-295): the
                                // dialog was only reachable from the palette.
                                AppMenuItem {
                                    objectName: "col-menu-archive"
                                    text: I18n.t("kanban.archive.set")
                                    note: {
                                        const days = (AppController.statuses, AppController.statusArchiveDays(col.statusId));
                                        return days > 0 ? days + " " + I18n.t("common.days")
                                                        : I18n.t("settings.tasks.archiveNever").toLowerCase();
                                    }
                                    onTriggered: archivePopup.openFor(col.statusId, col.statusName)
                                }
                                AppMenuSeparator {}
                                AppMenuItem { text: I18n.t("kanban.moveLeft");  enabled: !col.isFirst; shortcutId: "board.columnLeft"; onTriggered: AppController.moveStatus(col.statusId, col.index - 1) }
                                AppMenuItem { text: I18n.t("kanban.moveRight"); enabled: !col.isLast; shortcutId: "board.columnRight"; onTriggered: AppController.moveStatus(col.statusId, col.index + 1) }
                                AppMenuSeparator {}
                                AppMenuItem { objectName: "col-menu-delete"; danger: true; text: I18n.t("colmenu.delete"); enabled: AppController.statuses.length > 1; onTriggered: root.requestDeleteColumn(col.statusId, col.statusName) }
                            }
                            // "Этап колонки ›": the stage sets the ring and
                            // what counts as done (X-Menus-Column).
                            AppMenu {
                                id: stageMenu
                                objectName: "column-stage-menu"
                                backOnLeft: true
                                onBack: { colHeaderMenu.popup(); colHeaderMenu.currentIndex = 3; }
                                AppMenuItem {
                                    isBack: true
                                    text: "‹ " + I18n.t("colmenu.stageBack")
                                    onTriggered: stageMenu.goBack()
                                }
                                Repeater {
                                    model: col.board ? col.board.stages : []
                                    AppMenuItem {
                                        id: stageRow
                                        required property string modelData
                                        objectName: "col-stage-" + stageRow.modelData
                                        ring: stageRow.modelData
                                        text: I18n.t("status.stage." + stageRow.modelData)
                                        note: col.category === stageRow.modelData ? I18n.t("taskmenu.now") : ""
                                        onTriggered: AppController.setStatusCategory(col.statusId, stageRow.modelData)
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            // A ListView, not a Repeater in a Flickable: only
                            // the cards on screen are built. A board of 2000
                            // tasks held 580 MB of delegates the other way.
                            ListView {
                                id: bodyFlick
                                objectName: "column-list"
                                readonly property StackBus bus: col.bus
                                // For its cards: the column's room for a
                                // dragged card (shiftFor, settling).
                                readonly property var column: col
                                anchors.fill: parent
                                anchors.topMargin: Theme.spMd
                                clip: true
                                spacing: Theme.spMd
                                // A couple of cards past each edge. Every card
                                // in the cache is rebound when a filter change
                                // reshuffles the column, and with pooled
                                // delegates scrolling no longer needs a deep
                                // pre-built margin to stay smooth.
                                cacheBuffer: 200
                                boundsBehavior: Flickable.StopAtBounds
                                flickableDirection: Flickable.VerticalFlick
                                // One proxy per column, so only the cards that
                                // belong here are in the model at all.
                                model: colFilter
                                // A card that comes into the column fades in
                                // and the cards around a change close up
                                // instead of jumping (APP-175). The proxy
                                // inserts and removes rows one by one, so only
                                // the cards that changed move. A card that
                                // leaves goes at once: a search empties a
                                // column in one keystroke, and a filtered-out
                                // card must not linger on the board.
                                add: Transition {
                                    NumberAnimation { property: "enterFade"; from: 0; to: 1; duration: Theme.durPop; easing.type: Theme.easeEnter }
                                }
                                // Not around a drop: the cards are already
                                // where the slot put them (BoardDrag sheet).
                                displaced: Transition {
                                    enabled: !col.settling
                                    NumberAnimation { property: "y"; duration: Theme.durMove; easing.type: Theme.easeEnter }
                                    // A card displaced mid-fade lands fully drawn.
                                    NumberAnimation { property: "enterFade"; to: 1; duration: Theme.durPop; easing.type: Theme.easeEnter }
                                }
                                // A search or filter change swaps most of the
                                // rows in every column at once; building a
                                // fresh TaskCard for each was ~2.5 ms a card
                                // and ~300 ms a keystroke at 3k tasks. Pooled
                                // delegates are rebound instead.
                                reuseItems: true
                                // pressDelay: 0 — same rationale as the
                                // outer hscroll: instant drag on cards.
                                pressDelay: 0

                                // Draggable thumb for tall columns — mouse wheel
                                // already scrolls (ScrollFeel); this adds a
                                // grabbable bar when a column overflows.
                                ScrollBar.vertical: ThinScrollBar {}

                                // The empty part of a column, like the empty
                                // board around it.
                                TapHandler {
                                    onTapped: { root.clearSelectionAndCursor(); root.emptyClicked(); }
                                }

                                // The wheel scrolls this column (ScrollFeel). Only a
                                // column with nothing to scroll hands it to the board,
                                // which scrolls sideways; one that runs out mid-gesture
                                // keeps it, or the whole board slid the moment a long
                                // column ran out. Shift+wheel is for the board.

                                delegate: TaskCard {
                                            id: tc
                                            boardKeys: true
                                            docOpen: root.openTaskId.length > 0 && tc.id === root.openTaskId
                                            required property string id
                                            required property string title
                                            required property string desc
                                            required property string priority
                                            required property string status
                                            required property var checklist
                                            required property var deadline
                                            required property string branch
                                            required property bool archived
                                            required property bool blockedStuck
                                            required property string prState
                                            required property string prMove
                                            required property string prMoveReason
                                            required property int    prNumber
                                            required property string prUrl
                                            required property int    gitAhead
                                            required property int    gitBehind
                                            required property var    recentCommits
                                            required property int    trackedSeconds
                                            required property bool   isTiming
                                            required property string recurrence
                                            required property var    labels
                                            required property var    dueAt
                                            required property bool   dueHasTime
                                            required property var    scheduledAt
                                            required property bool   scheduledHasTime
                                            required property var    ticket
                                            required property string searchText
                                            required property int    attachmentCount
                                            required property var    statusChangedAt
                                            width: bodyFlick.width
                                            // A pooled card waits, culled but still
                                            // a child of the list, until a row needs
                                            // it: hide it so it is not taken for a
                                            // card on the board (hover, focus, the
                                            // tree walks in tests), and drop the menu
                                            // it opened for another task.
                                            ListView.onPooled: {
                                                tc.releaseMenu();
                                                tc.visible = false;
                                            }
                                            ListView.onReused: { tc.visible = true; tc.enterFade = 1; }
                                            // A card dropped into another column
                                            // leaves this one while it is still
                                            // lifted into the drag layer. Hold its
                                            // removal until the drag lets go, or the
                                            // pool would hand a card parented to the
                                            // drag layer to another row.
                                            ListView.delayRemove: tc.Drag.active

                                            readonly property var taskData: ({
                                                id: tc.id, title: tc.title, desc: tc.desc,
                                                priority: tc.priority, status: tc.status,
                                                deadline: tc.deadline, branch: tc.branch,
                                                archived: tc.archived, blockedStuck: tc.blockedStuck,
                                                prState: tc.prState, prNumber: tc.prNumber, prUrl: tc.prUrl,
                                                prMove: tc.prMove, prMoveReason: tc.prMoveReason,
                                                gitAhead: tc.gitAhead, gitBehind: tc.gitBehind,
                                                recentCommits: tc.recentCommits,
                                                trackedSeconds: tc.trackedSeconds, isTiming: tc.isTiming,
                                                recurrence: tc.recurrence,
                                                labels: tc.labels, dueAt: tc.dueAt, dueHasTime: tc.dueHasTime,
                                                scheduledAt: tc.scheduledAt, scheduledHasTime: tc.scheduledHasTime,
                                                ticket: tc.ticket, searchText: tc.searchText,
                                                checklist: tc.checklist, attachmentCount: tc.attachmentCount,
                                                statusChangedAt: tc.statusChangedAt
                                            })
                                            // Which card a bare "O" acts on when
                                            // nothing is selected.
                                            onHoveredChanged: {
                                                if (hovered) root.hoveredTaskId = tc.id;
                                                else if (root.hoveredTaskId === tc.id) root.hoveredTaskId = "";
                                            }
                                            task: taskData
                                            cursored: root.cursorVisible && root.cursorTaskId === tc.id
                                            dragLayer: boardDragLayer
                                            // Moved aside for the slot a
                                            // dragged card opens above it, or
                                            // up into the place one left.
                                            slotShift: tc._shiftIn(tc.ListView.view)
                                            slotInstant: tc._settlingIn(tc.ListView.view)
                                            // The column, by way of the list.
                                            function _shiftIn(view) {
                                                return view && view.column ? view.column.shiftFor(tc.y, tc._lifted) : 0;
                                            }
                                            function _settlingIn(view) {
                                                return !!(view && view.column && view.column.settling);
                                            }
                                            scheduled: root.scheduleMap[tc.id] || ""
                                            // Clicking a card also puts the
                                            // keyboard cursor on it, so mouse
                                            // and keyboard never disagree about
                                            // where "here" is.
                                            onClicked: {
                                                root.cursorTaskId = tc.id;
                                                root.cursorVisible = false;
                                                root.taskClicked(tc.id);
                                            }
                                            onRangeSelectRequested: (anchorId) => root._rangeSelect(anchorId)
                                            onStatusPicked: (sid) => tc._stack(tc.ListView.view, sid)
                                            // The column's bus, by way of the list.
                                            function _stack(view, sid) {
                                                if (view && view.bus) view.bus.dropped(tc, sid, 1);
                                            }
                                            onMenuOpenChanged: root._openCardMenus += menuOpen ? 1 : -1
                                            Component.onDestruction: if (menuOpen) root._openCardMenus--
                                        }

                                // An empty column says how a card gets here
                                // (APP-167); one whose cards the search or a
                                // filter hides, that nothing in it matches.
                                // While a search or filter is on: "nothing
                                // matches" and no invitation to drag cards in,
                                // and nothing at all when the whole board came
                                // up empty — the board says that once (FUNC-1).
                                // Same on a board with no cards yet: one
                                // board-level state, the columns keep only
                                // their headers (EYE-4).
                                Item {
                                    visible: col.visibleCount === 0 && !col.nothingFound && !col.boardEmpty
                                    width: bodyFlick.width
                                    height: colEmpty.implicitHeight + Theme.spXl
                                    EmptyState {
                                        id: colEmpty
                                        objectName: "column-empty"
                                        y: Theme.spXl
                                        width: parent.width
                                        compact: true
                                        glyph: "columns"
                                        title: col.filtering ? I18n.t("view.empty.noMatch.title") : I18n.t("kanban.empty")
                                        // Quiet keeps an empty column to its
                                        // header and two words (EYES-7).
                                        line: col.filtering || Style.quiet ? ""
                                            : (AppController.statusCounts[col.statusId] || 0) > 0 ? I18n.t("kanban.empty.noMatch") : I18n.t("kanban.empty.hint")
                                    }
                                }

                                // Where the card would land: an empty slot its
                                // height, a dashed edge on the panel colour,
                                // under the cards sliding round it.
                                DashedRect {
                                    objectName: "drop-slot"
                                    z: -1
                                    visible: opacity > 0
                                    opacity: col.slotOn ? 1 : 0
                                    width: bodyFlick.width
                                    y: col.slotLayoutY
                                    height: col.slotH
                                    radius: Theme.radius
                                    strokeColor: Theme.borderStrong
                                    fillColor: Theme.panel
                                    Behavior on opacity { NumberAnimation { duration: Theme.durSlot; easing.type: Theme.easeEnter } }
                                    Behavior on y {
                                        enabled: col.slotOn && !col.settling
                                        NumberAnimation { duration: Theme.durSlot; easing.type: Theme.easeEnter }
                                    }
                                }
                                // Where the card came from: the same slot,
                                // dimmer, while the card is over its own
                                // column; it closes when the card leaves.
                                // Held over its own place, it is the slot.
                                DashedRect {
                                    objectName: "drop-origin"
                                    z: -1
                                    visible: col.isOrigin && opacity > 0
                                    opacity: !col.originOpen ? 0 : col.slotNoop && col.manual ? 1 : 0.6
                                    width: bodyFlick.width
                                    y: col.originY
                                    height: col.originCollapsed ? 0 : col.slotH
                                    radius: Theme.radius
                                    strokeColor: col.slotNoop && col.manual ? Theme.borderStrong : Theme.border
                                    fillColor: col.slotNoop && col.manual ? Theme.panel : "transparent"
                                    Behavior on opacity { NumberAnimation { duration: Theme.durSlot; easing.type: Theme.easeEnter } }
                                    Behavior on height {
                                        enabled: !col.settling
                                        NumberAnimation { duration: Theme.durSlot; easing.type: Theme.easeEnter }
                                    }
                                }
                            }

                            DropArea {
                                id: colDrop
                                anchors.fill: parent

                                // The card the dragged one would land above,
                                // or "" for the end of the column. Recomputed
                                // as the pointer moves so the indicator and
                                // the drop agree.
                                property string beforeId: ""

                                // Which card sits under `y` (in the list's
                                // layout, the cards where they would be with
                                // no slot): the first whose midpoint the
                                // pointer is above. The dragged card is
                                // skipped — it is still in the column it came
                                // from, and counting it would make a one-place
                                // move look like no move at all. `noop`: the
                                // place is right after the dragged card, where
                                // it already is.
                                function _slotAt(y, draggedId) {
                                    // Cards off screen are not built; the
                                    // ones around the pointer always are, so
                                    // skipping the unbuilt ones is safe.
                                    let dragged = -2;
                                    let end = 0;
                                    for (let i = 0; i < bodyFlick.count; i++) {
                                        const item = bodyFlick.itemAtIndex(i) as TaskCard;
                                        if (!item) continue;
                                        if (item.taskId === draggedId) { dragged = i; continue; }
                                        if (y < item.y + item.height / 2)
                                            return { id: item.taskId, y: item.y, noop: dragged === i - 1 };
                                        end = item.y + item.height + bodyFlick.spacing;
                                    }
                                    return { id: "", y: end, noop: dragged >= 0 && dragged === bodyFlick.count - 1 };
                                }

                                function _update(drag) {
                                    const src = drag.source;
                                    const p = bodyFlick.contentItem.mapFromItem(colDrop, drag.x, drag.y);
                                    const id = src && src.taskId ? String(src.taskId) : "";
                                    const slot = _slotAt(p.y, id);
                                    colDrop.beforeId = slot.id;
                                    col.slotLayoutY = slot.y;
                                    // Several cards in the hand land together
                                    // at the slot, so it opens even next to
                                    // the one held.
                                    col.slotNoop = slot.noop && !(AppController.isTaskSelected(id) && AppController.selectionCount > 1);
                                }

                                // Files from a file manager are for the card
                                // under the pointer (TaskCard attaches them),
                                // not a move: let them through.
                                onEntered: (drag) => {
                                    if (drag.hasUrls && !(drag.source && drag.source["taskId"])) { drag.accepted = false; return; }
                                    col.dropLocalOnly = col.board.dropStaysLocal(drag.source, col.statusId);
                                    _update(drag);
                                    col.dragOver = true;
                                    col.board._dragTarget = col.statusId;
                                }
                                onPositionChanged: (drag) => _update(drag)
                                onExited: {
                                    col.dragOver = false;
                                    if (col.board._dragTarget === col.statusId) col.board._dragTarget = "";
                                }
                                onDropped: (drop) => {
                                    // The cards moved aside go straight to the
                                    // new layout: no slide back, then again.
                                    col.board.beginSettle();
                                    if (col.board._dragOpen === col.statusId) col.board._wantOpen = col.statusId;
                                    col.dragOver = false;
                                    const src = drop.source;
                                    if (!src || !src.taskId) return;
                                    // Under a sort, a drop still changes the
                                    // column — it just cannot choose where in
                                    // it the card lands. A drop back into its
                                    // own column is no move at all: with no
                                    // anchor it used to send the card to the
                                    // end of the manual order (TASKS-1).
                                    const manual = root.sortMode === "manual";
                                    const target = manual ? colDrop.beforeId : "";
                                    if (AppController.isTaskSelected(src.taskId)
                                        && AppController.selectionCount > 1) {
                                        // Several cards: no stack (APP-176).
                                        if (col.bus) col.bus.dropped(src, col.statusId, AppController.selectionCount);
                                        if (manual) AppController.moveSelectedTasksTo(col.statusId, target);
                                        else AppController.moveSelectedTasksToStatus(col.statusId);
                                    } else if (manual || AppController.taskById(src.taskId).status !== col.statusId) {
                                        if (col.bus) col.bus.dropped(src, col.statusId, 1);
                                        AppController.moveTaskTo(src.taskId, col.statusId, target);
                                    }
                                    drop.accept(Qt.MoveAction);
                                }
                            }

                            // Right-click on empty body → Add task
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: (mouse) => { if (mouse.button === Qt.RightButton) bodyMenu.popup() }
                                z: -2
                            }
                            AppMenu {
                                id: bodyMenu
                                AppMenuItem { text: I18n.t("kanban.addTask"); onTriggered: root.createInStatus(col.statusId) }
                            }
                        }
                    }

                    // The column's own view of the task model: its status, plus
                    // whatever the filter bar and the search box say. Filtering
                    // in C++ is what keeps the delegate count proportional to
                    // the tasks rather than to tasks × columns.
                    TaskFilterProxy {
                        id: colFilter
                        sourceModel: AppController.tasks
                        status: col.statusId
                        statuses: AppController.statuses
                        showArchived: root.showArchived
                        // The folded Done is how the board keeps done work out
                        // of sight: "не готово" leaves its count and its cards
                        // to it (H2-Board shows "Готово 2").
                        searchText: col.isDone && col.board ? col.board._searchKeepDone : root.searchText
                        strictQuery: col.isDone && col.board ? col.board._strictKeepDone : AppController.strictQuery
                        priorities: root.activePriorities
                        sortMode: root.sortMode
                        today: AppController.today
                        newIds: AppController.syncNewTaskIds
                    }
                    // An edited estimate changes no count: its own tick.
                    ChangeTick { id: estTick }
                    Connections {
                        target: AppController.tasks
                        function onDataChanged() { estTick.bump() }
                    }
                    readonly property var estimate: (estTick.rev >= 0 && colFilter.count > 0)
                        ? AppController.estimateSummary(colFilter.ids()) : ({ minutes: 0, without: 0 })
                }
            }

            // "+ column" at the end of the row (APP-289): columns are made on
            // the board itself, not only in Settings → Tasks.
            Rectangle {
                id: addColTile
                width: Theme.chipH
                height: Theme.chipH
                y: (Theme.px(38) - height) / 2
                radius: Theme.radiusMd
                color: addColCA.hovered || addColCA.keyboardFocused ? Theme.panel2 : "transparent"
                PlusGlyph {
                    anchors.centerIn: parent
                    color: addColCA.hovered || addColCA.keyboardFocused ? Theme.text : Theme.textDim
                }
                ClickArea {
                    id: addColCA
                    objectName: "board-add-column"
                    label: I18n.t("kanban.newColumn")
                    onActivated: addColumnPopup.openAfter(-1)
                }
            }
        }
    }

    // ── Popups ──────────────────────────────────────────────────────────────

    Popup {
        PopupArrival { popup: colorPopup; dialog: false }
        id: colorPopup
        modal: false
        focus: true
        // Parented to the swatch it was opened from (see openFor), so "outside
        // the parent" is "outside the palette and the swatch" — a press anywhere
        // else closes it, while a press on the swatch falls through to openFor,
        // which toggles.
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        padding: Theme.spMd
        background: PopupSurface {}
        property string forStatusId: ""

        readonly property var swatches: Theme.swatches

        function openFor(id, currentColor, anchorItem) {
            const sameSwatch = colorPopup.opened && colorPopup.forStatusId === id;
            colorPopup.close();
            if (sameSwatch) return;
            forStatusId = id;
            if (anchorItem) {
                colorPopup.parent = anchorItem;
                const p = anchorItem.mapToItem(root, 0, 0);
                const wantX = Math.max(8, Math.min(p.x + anchorItem.width / 2 - 90,
                                                   root.width - colorPopup.width - 8));
                colorPopup.x = wantX - p.x;
                colorPopup.y = anchorItem.height + 6;
            }
            open();
        }

        contentItem: Grid {
            columns: 5
            spacing: Theme.spSm
            Repeater {
                model: colorPopup.swatches
                delegate: Rectangle {
                    id: pickSwatch
                    required property string modelData
                    required property int index
                    width: 22; height: 22; radius: 11
                    color: modelData
                    border.color: Theme.border
                    border.width: 1
                    ClickArea {
                        label: I18n.t("kanban.swatch").arg(pickSwatch.index + 1)
                        showTip: false
                        onActivated: {
                            AppController.setStatusColor(colorPopup.forStatusId, pickSwatch.modelData);
                            colorPopup.close();
                        }
                    }
                }
            }
        }
    }

    // A card being dragged is lifted in here, above every column. In its own
    // column it was clipped by the column and drawn under the columns to its
    // right, so it looked like it slid underneath them.
    Item {
        id: boardDragLayer
        objectName: "board-drag-layer"
        anchors.fill: parent
        z: 1000
    }
    // A card is being dragged (it sits on the drag layer meanwhile); Esc
    // cancels it through Main (IDIOT-TASKS-8).
    readonly property bool dragActive: root.dragCard !== null
    // The card in the hand, or null.
    readonly property var dragCard: {
        const kids = boardDragLayer.children;
        for (let i = 0; i < kids.length; i++)
            if (typeof kids[i].cancelDrag === "function" && kids[i].Drag.active) return kids[i];
        return null;
    }
    // A drag that ends gives back the strip it opened and the column it was
    // over; the cards put aside for it go back without sliding, as the
    // column lays itself out anew in the same frame (BoardDrag sheet).
    onDragActiveChanged: {
        if (root.dragActive) return;
        root._dragOpen = "";
        root._dragTarget = "";
    }
    // True for a moment around a drop or a cancel: the cards a slot had
    // moved aside go straight to where the new layout puts them, instead of
    // sliding back and then being slid again by the list.
    property bool settling: false
    function beginSettle() {
        root.settling = true;
        settleTimer.restart();
    }
    Timer {
        id: settleTimer
        interval: 50
        onTriggered: root.settling = false
    }
    function cancelDrag() {
        if (root.dragActive) root.beginSettle();
        const kids = boardDragLayer.children;
        for (let i = 0; i < kids.length; i++)
            if (typeof kids[i].cancelDrag === "function" && kids[i].cancelDrag()) return true;
        return false;
    }

    // The closed card on its way to Done (APP-176): it folds into a bar
    // where it stood (the first third), then flies into the Done column's
    // counter (APP-205). One curve for both. Drawn here, over every column,
    // and gone when it lands.
    Rectangle {
        id: stackFlyer
        objectName: "stack-flyer"
        visible: false
        z: 1001
        property real toX: 0
        property real toY: 0
        property real toW: 0
        property real foldY: 0
        property color barColor: Theme.stDone
        color: Theme.panel2
        border.color: Theme.border
        border.width: height > 8 ? 1 : 0
        radius: Theme.radius
        function launch(x: real, y: real, w: real, h: real, title: string,
                        tx: real, ty: real, tw: real, barColor: color) {
            stackAnim.stop();
            stackBus.holdStatus = "";
            stackFlyer.x = x;
            stackFlyer.y = y;
            stackFlyer.width = w;
            stackFlyer.height = h;
            stackFlyer.radius = Theme.radius;
            stackFlyer.color = Theme.panel2;
            stackFlyer.opacity = 1;
            stackFlyer.foldY = y + (h - 4) / 2;
            stackFlyer.toX = tx;
            stackFlyer.toY = ty;
            stackFlyer.toW = tw;
            stackFlyer.barColor = barColor;
            flyerTitle.text = title;
            flyerTitle.opacity = 1;
            stackFlyer.visible = true;
            root.stackRunning = true;
            stackAnim.restart();
        }
        function land() {
            stackFlyer.visible = false;
            stackBus.holdStatus = "";
            root.stackRunning = false;
        }
        Text {
            id: flyerTitle
            x: Theme.spLg
            y: Theme.spLg
            width: parent.width - 2 * Theme.spLg
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            font.weight: Theme.fwTitle
            elide: Text.ElideRight
        }
        SequentialAnimation {
            id: stackAnim
            ParallelAnimation {
                NumberAnimation { target: flyerTitle; property: "opacity"; to: 0; duration: Theme.durTap; easing.type: Theme.easeEnter }
                NumberAnimation { target: stackFlyer; property: "height"; to: 4; duration: Theme.durStackFold; easing.type: Theme.easeEnter }
                NumberAnimation { target: stackFlyer; property: "y"; to: stackFlyer.foldY; duration: Theme.durStackFold; easing.type: Theme.easeEnter }
                NumberAnimation { target: stackFlyer; property: "radius"; to: 2; duration: Theme.durStackFold; easing.type: Theme.easeEnter }
                ColorAnimation { target: stackFlyer; property: "color"; to: stackFlyer.barColor; duration: Theme.durStackFold; easing.type: Theme.easeEnter }
            }
            ParallelAnimation {
                NumberAnimation { target: stackFlyer; property: "x"; to: stackFlyer.toX; duration: Theme.durStackFly; easing.type: Theme.easeEnter }
                NumberAnimation { target: stackFlyer; property: "y"; to: stackFlyer.toY; duration: Theme.durStackFly; easing.type: Theme.easeEnter }
                NumberAnimation { target: stackFlyer; property: "width"; to: stackFlyer.toW; duration: Theme.durStackFly; easing.type: Theme.easeEnter }
                NumberAnimation { target: stackFlyer; property: "opacity"; to: 0.75; duration: Theme.durStackFly; easing.type: Theme.easeEnter }
            }
            ScriptAction { script: stackFlyer.land() }
        }
    }

    // Columns off to the right (APP-200): at 1600px the fourth went under the
    // side panel with nothing to say it was there. Says how many; a click
    // scrolls one column over.
    // heap 2 (APP-262): the last visible column fades out at the edge, so a
    // cut column never reads as the end of the board.
    Rectangle {
        objectName: "board-edge-fade"
        visible: root.hiddenColumnsRight > 0
        anchors.right: parent.right
        anchors.rightMargin: Theme.sp2xl
        // From the header row down: the cut column's ring and name showed
        // above the fade beside the chip (EYES-1).
        anchors.top: hscroll.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.sp2xl
        width: Math.max(Theme.sp3xl * 2, hiddenCols.width + Theme.spXl * 2)
        z: 899
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.withAlpha(Theme.bg, 0) }
            GradientStop { position: 1; color: Theme.bg }
        }
    }
    // Under the chip, the cut column's header (its ring, its "show") read
    // as one jumble with it (A4-24): the header line there is plain.
    Rectangle {
        id: hiddenBand
        objectName: "board-hidden-columns-band"
        visible: root.hiddenColumnsRight > 0
        anchors.right: parent.right
        anchors.rightMargin: Theme.sp2xl
        anchors.top: hscroll.top
        // Solid under the chip, fading in on the left: a hard edge cut the
        // cut column's name and count in half ("Triage (", "Blocked 3").
        readonly property real solidWidth: Math.max(Theme.sp3xl * 2, hiddenCols.width + Theme.spXl * 2) + Theme.spXl
        width: solidWidth + Theme.sp3xl
        // The header line and the folded column's "show" under it.
        height: Theme.px(52)
        z: 899
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.withAlpha(Theme.bg, 0) }
            GradientStop { position: Theme.sp3xl / Math.max(1, hiddenBand.width); color: Theme.bg }
            GradientStop { position: 1; color: Theme.bg }
        }
    }
    Rectangle {
        id: hiddenCols
        objectName: "board-hidden-columns"
        visible: root.hiddenColumnsRight > 0
        anchors.right: parent.right
        anchors.rightMargin: Theme.sp2xl
        // On the line of the column headers: a column starts at the top of
        // the scroll area with its 38 px header (EYES-1: it sat 12 px low).
        anchors.top: hscroll.top
        anchors.topMargin: (38 - height) / 2
        z: 900
        radius: Theme.radiusMd
        color: hiddenColsMA.hovered || hiddenColsMA.keyboardFocused ? Theme.panel3 : Theme.bg
        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        implicitWidth: hiddenColsT.implicitWidth + 2 * Theme.spMd
        implicitHeight: hiddenColsT.implicitHeight + 2 * Theme.spXs
        Text {
            id: hiddenColsT
            objectName: "board-hidden-columns-text"
            anchors.centerIn: parent
            text: I18n.t("kanban.hiddenColumns").arg(root.hiddenColumnsRight) + " ›"
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsSm
        }
        ClickArea {
            id: hiddenColsMA
            label: hiddenColsT.text
            tip: I18n.t("kanban.hiddenColumns.tip")
            onActivated: root._scrollOuter(-(root.columnWidth + root.columnGap))
        }
    }

    // ── Inline components ───────────────────────────────────────────────────

    component HoverIcon: Rectangle {
        id: hoverIcon
        property string icon: ""
        property string tip: ""
        property string shortcutId: ""
        property bool danger: false
        // Faded out rather than hidden: the slot stays in the header layout, so
        // nothing shifts when the pointer arrives and the icon is already under
        // the cursor when it fades in. Hidden, it takes no clicks, but it stays
        // on the Tab path and shows itself when the keyboard lands on it
        // (design audit DES-19).
        property bool revealed: false
        readonly property bool keyFocused: hoverIconMA.keyboardFocused
        readonly property bool shown: revealed || keyFocused
        readonly property bool hot: hoverIconMA.hovered || keyFocused
        signal activated()
        width: 20
        height: 20
        radius: Theme.radiusSm
        opacity: shown ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: hoverIcon.shown ? Theme.durTap : Theme.durTapOut
                easing.type: hoverIcon.shown ? Theme.easeEnter : Theme.easeExit
            }
        }
        color: hoverIcon.hot ? (danger ? Theme.withAlpha(Theme.danger, 0.16) : Theme.panel3)
                             : "transparent"
        border.color: hoverIcon.hot ? (danger ? Theme.danger : Theme.border) : "transparent"
        border.width: 1
        function focusKey() { hoverIconMA.forceActiveFocus(Qt.TabFocusReason); }
        Icon {
            anchors.centerIn: parent
            name: hoverIcon.icon
            size: Theme.iconSize
            color: hoverIcon.hot ? (hoverIcon.danger ? Theme.danger : Theme.text) : Theme.textMuted
        }
        ClickArea {
            id: hoverIconMA
            label: hoverIcon.tip
            shortcutId: hoverIcon.shortcutId
            showTip: hoverIcon.shown
            acceptedButtons: hoverIcon.shown ? Qt.LeftButton : Qt.NoButton
            cursorShape: hoverIcon.shown ? Qt.PointingHandCursor : Qt.ArrowCursor
            onActivated: hoverIcon.activated()
        }
    }

    // ── Board-level empty state ──
    // Shown when the profile has no tasks at all (e.g. right after "Start
    // fresh"). Non-interactive so the column "+" affordances stay reachable.
    // Live tasks: a board whose every card is archived is empty too, and
    // says where they went (TASKS-33).
    readonly property int _boardTotal: AppController.statusCounts["_total"] || 0
    // A search or a priority filter that hides every card is not an empty
    // board: it says the search found nothing, once, like the other views
    // (FUNC-1). It used to be "Nothing here yet" in every column.
    // The default "не готово" is not a search (DG-020).
    readonly property bool _filtering: root._searchKeepDone.length > 0 || root.activePriorities.length > 0
    // Every card the search lets through, in any column.
    TaskFilterProxy {
        id: boardFilter
        sourceModel: AppController.tasks
        statuses: AppController.statuses
        showArchived: root.showArchived
        searchText: root.searchText
        strictQuery: AppController.strictQuery
        priorities: root.activePriorities
        today: AppController.today
    }
    readonly property bool _nothingFound: root._filtering && root._boardTotal > 0 && boardFilter.count === 0
    readonly property bool nothingFound: root._nothingFound
    // The filter as words for "Ничего под «…»" (DG-160).
    readonly property string filterLabel: QueryWords.label(root.searchText, root.activePriorities)
    property int _allRows: AppController.tasks.rowCount()
    Connections {
        target: AppController.tasks
        function onModelReset()   { root._allRows = AppController.tasks.rowCount() }
        function onRowsInserted() { root._allRows = AppController.tasks.rowCount() }
        function onRowsRemoved()  { root._allRows = AppController.tasks.rowCount() }
    }
    // An empty board and a filter that found nothing: one plain centred
    // line and its key, no card, no picture (N/X-Err-Empty, R3-131). The
    // columns have no border to cross, so it needs no backing.
    EmptyState {
        id: boardEmptyCol
        objectName: "board-empty-state"
        anchors.centerIn: parent
        width: Math.min(parent.width - 96, 360)
        visible: root._boardTotal === 0 || root._nothingFound
        title: root._nothingFound ? I18n.t("view.empty.noMatchFor").arg(root.filterLabel)
             : root._allRows > 0 ? I18n.t("board.empty.archivedTitle") : I18n.t("board.empty.title")
        // Where the filter would find something (A3-13).
        hint: root._nothingFound ? AppController.matchesElsewhere(root.searchText) : ""
        // The keys as bound now, not as they shipped (design audit DES-15).
        lineLink: root._nothingFound
        onLineActivated: root.resetFilterRequested()
        line: root._nothingFound ? I18n.t("view.empty.resetFilter")
            : root._allRows > 0
              ? I18n.t("board.empty.archivedHint").arg(AppController.shortcutText("task.new"))
              : I18n.t("tasks.empty.line").arg(AppController.shortcutText("task.new"))
    }
    // ── Column delete: confirm when it is not empty ───────────────────
    // Deleting a column re-homes every card in it. That is undoable, but a
    // five-second toast is a poor place to discover that thirty cards just
    // moved — so a non-empty column asks first. An empty one does not: there
    // is nothing to lose and a dialog would only be in the way.
    //
    // The count is every card the delete re-homes — archived ones and the
    // ones a filter is hiding included. The column's visible count skipped
    // the confirmation for a column full of filtered-out cards (TASKS-8).
    //
    // A column that saved views filter on asks too, and names them: deleting
    // it drops their clause and they show everything (IDIOT-TASKS-11).
    function requestDeleteColumn(statusId, statusName) {
        const count = AppController.countByStatus(statusId);
        const views = AppController.savedViewsUsingStatus(statusId);
        if (count <= 0 && views.length === 0) {
            AppController.deleteStatus(statusId);
            return;
        }
        confirmDelete.statusId = statusId;
        confirmDelete.statusName = statusName;
        confirmDelete.cardCount = count;
        confirmDelete.views = views;
        confirmDelete.open();
    }

    // X-Dlg-Small (R2-041): the question, one line of fact with the undo
    // key, "Перенести задачи в [column ▾]", Отмена / Удалить колонку.
    SmallDialog {
        id: confirmDelete
        objectName: "confirm-delete-column"
        parent: Overlay.overlay
        property string statusId: ""
        property string statusName: ""
        property int cardCount: 0
        property var views: []
        // The other columns, in board order.
        readonly property var targets: (AppController.statuses || []).filter(st => st.id !== confirmDelete.statusId)
        // The default target is a column of the same stage: Enter on the Done
        // column's dialog moved every finished task to Backlog as open work.
        // The last done-stage column has none, so the person picks one and
        // is told the tasks reopen (IDIOT-TASKS-12).
        readonly property string category: (AppController.statuses, AppController.statusCategory(confirmDelete.statusId))
        function defaultTarget() {
            for (let i = 0; i < confirmDelete.targets.length; i++)
                if (AppController.statusCategory(confirmDelete.targets[i].id) === confirmDelete.category) return i;
            return confirmDelete.category === "done" ? -1 : 0;
        }
        readonly property bool reopens: confirmDelete.cardCount > 0 && confirmDelete.category === "done"
            && (targetBox.currentIndex < 0 || targetBox.currentIndex >= confirmDelete.targets.length
                || AppController.statusCategory(confirmDelete.targets[targetBox.currentIndex].id) !== "done")
        readonly property bool canDelete: confirmDelete.cardCount === 0 || targetBox.currentIndex >= 0

        title: I18n.t("kanban.confirmDelete.title").arg(confirmDelete.statusName)
        fact: I18n.t("kanban.confirmDelete.fact")
                .arg(I18n.count(confirmDelete.cardCount, "query.n.tasks"))
                .arg(AppController.shortcutText("undo").replace(/\+/g, " "))
        // Enter is the main button, as in every small dialog (X-Dlg-Small);
        // Tab reaches the column field.
        onOpened: { targetBox.currentIndex = confirmDelete.defaultTarget(); deleteKeys.forceActiveFocus(); }
        onAccepted: confirmDelete.deleteNow()
        function deleteNow() {
            if (!confirmDelete.canDelete) return;
            const t = confirmDelete.targets[targetBox.currentIndex];
            AppController.deleteStatus(confirmDelete.statusId, t ? t.id : "");
            confirmDelete.close();
        }

        ColumnLayout {
            id: deleteKeys
            Layout.fillWidth: true
            spacing: Theme.spXs
            focus: true
            Keys.onReturnPressed: confirmDelete.accepted()
            Keys.onEnterPressed: confirmDelete.accepted()
            Text {
                visible: confirmDelete.cardCount > 0
                text: I18n.t("kanban.confirmDelete.into")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
            AppComboBox {
                id: targetBox
                objectName: "confirm-delete-target"
                visible: confirmDelete.cardCount > 0
                Accessible.name: I18n.t("kanban.confirmDelete.into")
                Layout.fillWidth: true
                model: confirmDelete.targets.map(st => st.name)
                displayText: currentIndex < 0 ? I18n.t("kanban.confirmDelete.pick") : currentText
            }
            Text {
                objectName: "confirm-delete-reopens"
                visible: confirmDelete.reopens
                Layout.fillWidth: true
                text: I18n.t("kanban.confirmDelete.reopens").arg(I18n.count(confirmDelete.cardCount, "query.n.tasks"))
                color: Theme.warning
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                wrapMode: Text.Wrap
            }
            Text {
                objectName: "confirm-delete-views"
                visible: confirmDelete.views.length > 0
                Layout.fillWidth: true
                text: I18n.t("kanban.confirmDelete.views").arg(confirmDelete.views.join(", "))
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                wrapMode: Text.Wrap
            }
        }

        buttons: [
            PillButton {
                text: I18n.t("common.cancel")
                onClicked: confirmDelete.close()
            },
            PillButton {
                objectName: "confirm-delete-ok"
                text: I18n.t("kanban.confirmDelete.ok")
                danger: true
                enabled: confirmDelete.canDelete
                onClicked: confirmDelete.deleteNow()
            }
        ]
    }

    // ── New column (APP-289) ──────────────────────────────────────────
    // From the "+" tile it goes last; from a column's menu, right of it.
    QQC.Dialog {
        PopupArrival { popup: addColumnPopup }
        id: addColumnPopup
        objectName: "add-column-popup"
        property int afterIndex: -1
        readonly property var swatches: Theme.swatches
        property color picked: swatches[0]

        function openAfter(index) {
            addColumnPopup.afterIndex = index;
            addColumnPopup.picked = addColumnPopup.swatches[0];
            addColNameField.text = "";
            addColumnPopup.open();
            addColNameField.forceActiveFocus();
        }

        modal: true
        Overlay.modal: ModalScrim {}
        anchors.centerIn: Overlay.overlay
        parent: Overlay.overlay
        padding: Theme.inset
        title: I18n.t("kanban.newColumn")
        // The other dialogs' title (DES-24), on one line: a title that
        // elides or wraps changes its implicitWidth with the width the dialog
        // gives it, a binding loop on the dialog's implicitWidth (b-23).
        header: DialogHeader { text: addColumnPopup.title; wrapMode: Text.NoWrap }

        background: ModalSurface {}

        function commit() {
            const n = addColNameField.text.trim();
            if (n.length === 0) return;
            const before = AppController.statuses.length;
            AppController.addStatus(n, String(addColumnPopup.picked));
            const list = AppController.statuses;
            // A taken name adds nothing; the dialog stays for another name.
            if (list.length === before) return;
            if (addColumnPopup.afterIndex >= 0 && addColumnPopup.afterIndex + 1 < list.length - 1)
                AppController.moveStatus(list[list.length - 1].id, addColumnPopup.afterIndex + 1);
            addColumnPopup.close();
        }

        contentItem: ColumnLayout {
            spacing: Theme.spMd
            TextField {
                id: addColNameField
                ContextMenu.menu: TextEditMenu { editor: addColNameField }
                objectName: "add-column-name"
                Layout.fillWidth: true
                Layout.preferredWidth: 280
                maximumLength: 60
                placeholderText: I18n.t("kanban.colName.ph")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                font.family: Theme.fontUi
                background: FieldFrame {}
                onAccepted: addColumnPopup.commit()
            }
            Row {
                spacing: Theme.spSm
                Repeater {
                    model: addColumnPopup.swatches
                    delegate: Rectangle {
                        id: newSwatch
                        required property string modelData
                        required property int index
                        readonly property bool isPicked: String(addColumnPopup.picked).toLowerCase() === modelData.toLowerCase()
                        width: Theme.px(22); height: width; radius: width / 2
                        color: modelData
                        border.color: newSwatch.isPicked ? Theme.text : Theme.border
                        border.width: 2
                        ClickArea {
                            label: I18n.t("kanban.swatch").arg(newSwatch.index + 1)
                            showTip: false
                            role: Accessible.RadioButton
                            checkable: true
                            checked: newSwatch.isPicked
                            onActivated: addColumnPopup.picked = newSwatch.modelData
                        }
                    }
                }
            }
        }

        footer: RowLayout {
            spacing: Theme.spMd
            Item { Layout.fillWidth: true }
            PillButton { text: I18n.t("common.cancel"); onClicked: addColumnPopup.close() }
            PillButton { objectName: "add-column-create"; text: I18n.t("kanban.create"); primary: true; onClicked: addColumnPopup.commit() }
            Item { Layout.preferredWidth: 10 }
        }
    }

    // ── Work-in-progress limit ────────────────────────────────────────
    QQC.Dialog {
        PopupArrival { popup: wipPopup }
        id: wipPopup
        objectName: "wip-popup"
        property string statusId: ""
        property string statusName: ""

        function openFor(id, name, current, anchorItem) {
            wipPopup.statusId = id;
            wipPopup.statusName = name;
            wipField.text = current > 0 ? String(current) : "";
            wipPopup.open();
            wipField.forceActiveFocus();
            wipField.selectAll();
        }

        modal: true
        Overlay.modal: ModalScrim {}
        anchors.centerIn: Overlay.overlay
        parent: Overlay.overlay
        padding: Theme.inset
        title: I18n.t("kanban.wip.title").arg(wipPopup.statusName)
        // The other dialogs' title (DES-24), on one line: a title that
        // elides or wraps changes its implicitWidth with the width the dialog
        // gives it, a binding loop on the dialog's implicitWidth (b-23).
        header: DialogHeader { text: wipPopup.title; wrapMode: Text.NoWrap }

        background: ModalSurface {}

        function commit() {
            AppController.setStatusWipLimit(wipPopup.statusId, parseInt(wipField.text || "0") || 0);
            wipPopup.close();
        }

        contentItem: ColumnLayout {
            spacing: Theme.spMd
            TextField {
                id: wipField
                ContextMenu.menu: TextEditMenu { editor: wipField }
                objectName: "wip-field"
                Layout.fillWidth: true
                Layout.preferredWidth: 220
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0; top: 999 }
                placeholderText: "0"
                color: Theme.text
                font.family: Theme.fontUi
                font.features: Theme.tabularNums
                background: FieldFrame {}
                onAccepted: wipPopup.commit()
            }
            Text {
                Layout.preferredWidth: 220
                text: I18n.t("kanban.wip.hint")
                color: Theme.textDim
                font.pixelSize: Theme.fsSm
                wrapMode: Text.Wrap
            }
        }

        footer: RowLayout {
            spacing: Theme.spMd
            Item { Layout.fillWidth: true }
            PillButton { text: I18n.t("common.cancel"); onClicked: wipPopup.close() }
            PillButton { text: I18n.t("common.save"); onClicked: wipPopup.commit() }
            Item { Layout.preferredWidth: 10 }
        }
    }

    // ── Auto-archive (APP-122) ────────────────────────────────────────
    // Cards that sit in the column this many days go to the archive. For
    // Done it is the same number as Settings → Tasks.
    QQC.Dialog {
        PopupArrival { popup: archivePopup }
        id: archivePopup
        objectName: "archive-popup"
        property string statusId: ""
        property string statusName: ""

        function openFor(id, name) {
            archivePopup.statusId = id;
            archivePopup.statusName = name;
            const days = AppController.statusArchiveDays(id);
            archiveField.text = days > 0 ? String(days) : "";
            archivePopup.open();
            archiveField.forceActiveFocus();
            archiveField.selectAll();
        }

        modal: true
        Overlay.modal: ModalScrim {}
        anchors.centerIn: Overlay.overlay
        parent: Overlay.overlay
        padding: Theme.inset
        title: I18n.t("kanban.archive.title").arg(archivePopup.statusName)
        // The other dialogs' title (DES-24), on one line: a title that
        // elides or wraps changes its implicitWidth with the width the dialog
        // gives it, a binding loop on the dialog's implicitWidth (b-23).
        header: DialogHeader { text: archivePopup.title; wrapMode: Text.NoWrap }

        background: ModalSurface {}

        function commit() {
            AppController.setStatusArchiveDays(archivePopup.statusId, parseInt(archiveField.text || "0") || 0);
            archivePopup.close();
        }

        contentItem: ColumnLayout {
            spacing: Theme.spMd
            TextField {
                id: archiveField
                ContextMenu.menu: TextEditMenu { editor: archiveField }
                objectName: "archive-field"
                Layout.fillWidth: true
                Layout.preferredWidth: 220
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0; top: 3650 }
                placeholderText: "0"
                color: Theme.text
                font.family: Theme.fontUi
                font.features: Theme.tabularNums
                background: FieldFrame {}
                onAccepted: archivePopup.commit()
            }
            Text {
                Layout.preferredWidth: 220
                text: I18n.t("kanban.archive.hint")
                color: Theme.textDim
                font.pixelSize: Theme.fsSm
                wrapMode: Text.Wrap
            }
        }

        footer: RowLayout {
            spacing: Theme.spMd
            Item { Layout.fillWidth: true }
            PillButton { text: I18n.t("common.cancel"); onClicked: archivePopup.close() }
            PillButton { objectName: "archive-save"; text: I18n.t("common.save"); onClicked: archivePopup.commit() }
            Item { Layout.preferredWidth: 10 }
        }
    }

}
