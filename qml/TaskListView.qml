pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import TodoCpp
import "QueryWords.js" as QueryWords
import "TaskDates.js" as TaskDates
import "Motion.js" as Motion

// Tasks → List (APP-263, sheets H2-List / Q-List): the tasks the query lets
// through, one row each, in groups — by date (default: Overdue, Today,
// Tomorrow, This week, Next week, Later, No date), by status, by priority or
// by profile. A row: the status shape, the key, the title, a label, P0/P1 and
// the date on the right. "No date" starts folded with its count; a group
// folded or opened stays so. The keys are the board's: j k, Return, d, s,
// m, v / Space, e; a hint bar along the bottom in the bold style.
Item {
    id: root
    objectName: "task-list-view"

    property string searchText: ""
    property var prioritiesFilter: ({})
    // H2-List insets rows 8px, Q-List 10px (R3-034).
    readonly property int _rowInset: Style.fills ? Theme.spMd : Theme.spLg
    property bool showArchived: false
    // date | status | priority | profile
    property string groupBy: "date"
    signal taskClicked(string id)
    // "сбросить фильтр · Esc" under a filter that found nothing (DG-160).
    signal resetFilterRequested()
    // "show them" under "all done": the default "not done" lets go (EYE-27).
    signal showDoneRequested()
    // The default "не готово" is not a search (DG-020).
    readonly property bool _searching: root.searchText.replace(/(^|\s)is:open(?=\s|$)/gi, " ").trim().length > 0
                                       || root.activePriorities.length > 0

    readonly property var activePriorities: {
        const out = [];
        for (const k in root.prioritiesFilter) if (root.prioritiesFilter[k]) out.push(k);
        return out;
    }

    // The filter as words for "Ничего под «…»" (DG-160).
    readonly property string filterLabel: QueryWords.label(root.searchText, root.activePriorities)
    readonly property bool nothingFound: root.taskCount === 0 && root._searching
    // Nothing listed, no search, yet the profile has tasks: the default "not
    // done" hides them all. "No tasks yet" read as "your tasks are gone"
    // (EYE-27).
    readonly property int hiddenByDefault: root.taskCount === 0 && !root._searching && !root._archive
                                           ? Number(AppController.statusCounts["_total"] || 0) : 0
    // The archive (X-Oth-Archive-People): the list under "is:archived".
    readonly property bool _archive: /(^|\s)is:archived(\s|$)/i.test(root.searchText)

    // ── Rows ─────────────────────────────────────────────────────────
    // Built in C++ (AppController.taskListRows, views/TaskListGroups) and
    // laid out by a ListView, which builds only the rows on screen: a list of
    // a few thousand tasks costs a few dozen delegates.
    property var _all: []
    property var _rows: []
    // Rows that were not on screen before the last rebuild fade in
    // (APP-175); `_seen` is Motion.arrivals' memory, never a binding.
    property var _fresh: ({})
    readonly property var _seen: ({})
    property int taskCount: 0

    // Folded groups by groupId; "No date" is folded until opened. Kept in
    // the UI settings, so a group stays the way it was left.
    property var _folds: ({})
    function _loadFolds() {
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        root._folds = (s.listFolds && typeof s.listFolds === "object") ? s.listFolds : ({});
    }
    // "решить" on a row's tracker mark: the same here-vs-tracker dialog the
    // card opens (R3-144). Made on first use.
    Loader {
        id: conflictLoader
        active: false
        sourceComponent: SyncConflictDialog {
            onClosed: Qt.callLater(() => { conflictLoader.active = false; })
        }
    }
    function openConflict(id) {
        conflictLoader.active = true;
        const dlg = conflictLoader.item as SyncConflictDialog;
        if (dlg) dlg.showFor(AppController.taskById(id));
    }
    // How many groups the query left. The only group is never folded: the
    // "N без даты — посмотреть и решить" link landed on one folded line, and
    // j / d / s had nothing to act on (A1-1, A2-9, A5-17).
    property int _groupCount: 0
    function isFolded(groupId) {
        if (root._groupCount <= 1) return false;
        const v = root._folds[groupId];
        return v === undefined ? groupId === "none:" : !!v;
    }
    function toggleGroup(groupId) {
        const next = Object.assign({}, root._folds);
        next[groupId] = !root.isFolded(groupId);
        root._folds = next;
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        s.listFolds = next;
        AppController.appSettingsJson = JSON.stringify(s);
        root._layout();
    }

    function refresh() {
        root._all = AppController.taskListRows(root.searchText, root.activePriorities, root.showArchived, root.groupBy);
        root._layout();
    }
    function _layout() {
        let groups = 0;
        for (let i = 0; i < root._all.length; i++)
            if (root._all[i].kind === "group") groups++;
        root._groupCount = groups;
        // Where the cursor was, to keep its place when its task leaves.
        const oldIds = root._taskIds();
        const oldAt = oldIds.indexOf(root.cursorTaskId);
        const out = [];
        let n = 0;
        for (let i = 0; i < root._all.length; i++) {
            const r = root._all[i];
            if (r.kind === "task") {
                n++;
                if (root.isFolded(r.groupId)) continue;
            }
            out.push(r);
        }
        const keep = list.contentY;
        const ids = [];
        for (let i = 0; i < out.length; i++) if (out[i].kind === "task") ids.push(out[i].id);
        root._fresh = Motion.arrivals(root._seen, ids);
        root._rows = out;
        root.taskCount = n;
        list.contentY = Math.min(keep, Math.max(0, list.contentHeight - list.height));
        // The cursor's task left the list (s moved it out of "без даты", d
        // finished it): the row that took its place gets the cursor, so the
        // triage loop goes on and no key acts on a task nobody sees (A1-3).
        if (root.cursorTaskId && oldAt >= 0 && ids.indexOf(root.cursorTaskId) < 0) {
            if (ids.length === 0) root.clearCursor();
            else root.cursorTaskId = ids[Math.min(oldAt, ids.length - 1)];
        }
    }
    Timer { id: refreshSoon; interval: 0; onTriggered: root.refresh() }
    function _refreshSoon() { refreshSoon.restart(); }
    onSearchTextChanged: _refreshSoon()
    onActivePrioritiesChanged: _refreshSoon()
    onShowArchivedChanged: _refreshSoon()
    onGroupByChanged: _refreshSoon()
    Connections {
        target: AppController.tasks
        function onDataChanged() { root._refreshSoon(); }
        function onRowsInserted() { root._refreshSoon(); }
        function onRowsRemoved() { root._refreshSoon(); }
        function onModelReset() { root._refreshSoon(); }
        function onRowsMoved() { root._refreshSoon(); }
    }
    Connections {
        target: AppController
        function onStatusesChanged() { root._refreshSoon(); }
        function onTodayChanged() { root._refreshSoon(); }
        function onActiveProfileChanged() { root._refreshSoon(); }
    }
    Component.onCompleted: { root._loadFolds(); root.refresh(); }

    // ── Cursor and selection (the board's names, so Main's keys work) ──
    property string cursorTaskId: ""
    property bool cursorVisible: false
    property string hoveredTaskId: ""
    property string _anchorId: ""
    readonly property bool cardMenuOpen: menuHost.menuOpen
    readonly property bool dialogOpen: menuHost.menuOpen

    function _taskIds() {
        const out = [];
        for (let i = 0; i < root._rows.length; i++)
            if (root._rows[i].kind === "task") out.push(root._rows[i].id);
        return out;
    }
    function _indexOfTask(id) {
        for (let i = 0; i < root._rows.length; i++)
            if (root._rows[i].kind === "task" && root._rows[i].id === id) return i;
        return -1;
    }
    function clearCursor() {
        root.cursorTaskId = "";
        root.cursorVisible = false;
    }
    function moveCursor(dx, dy) {
        if (dy === 0) return;
        let ids = root._taskIds();
        // Only folded groups on screen: j opens the first one rather than
        // doing nothing under a "j k выбор" hint.
        if (ids.length === 0) {
            for (let i = 0; i < root._all.length; i++) {
                if (root._all[i].kind === "group" && root.isFolded(root._all[i].groupId)) {
                    root.toggleGroup(root._all[i].groupId);
                    break;
                }
            }
            ids = root._taskIds();
        }
        if (ids.length === 0) return;
        root.cursorVisible = true;
        const at = ids.indexOf(root.cursorTaskId);
        root.cursorTaskId = at < 0 ? ids[0] : ids[Math.max(0, Math.min(ids.length - 1, at + dy))];
        root._anchorId = "";
        root._reveal();
    }
    // Shift + j / k: a range from where it started.
    function extendSelection(dy) {
        const ids = root._taskIds();
        if (ids.length === 0) return;
        root.cursorVisible = true;
        let at = ids.indexOf(root.cursorTaskId);
        if (at < 0) { root.cursorTaskId = ids[0]; return; }
        if (!root._anchorId || ids.indexOf(root._anchorId) < 0) root._anchorId = root.cursorTaskId;
        at = Math.max(0, Math.min(ids.length - 1, at + dy));
        const a = ids.indexOf(root._anchorId);
        const anchor = root._anchorId;
        root.cursorTaskId = ids[at];
        root._anchorId = anchor;
        AppController.setSelectedTaskIds(ids.slice(Math.min(a, at), Math.max(a, at) + 1));
        root._reveal();
    }
    function _reveal() {
        const i = root._indexOfTask(root.cursorTaskId);
        if (i >= 0) list.positionViewAtIndex(i, ListView.Contain);
        if (root.cursorTaskId) AppController.markTaskSeen(root.cursorTaskId);
    }
    function _actionCardId() {
        if (root.cursorTaskId && root._indexOfTask(root.cursorTaskId) >= 0) return root.cursorTaskId;
        if (AppController.selectionCount === 1) return AppController.selectedTaskIds[0];
        return root.hoveredTaskId || "";
    }
    function openCursor() {
        if (!root.cursorTaskId || root._indexOfTask(root.cursorTaskId) < 0) { root.moveCursor(0, 1); return; }
        root.taskClicked(root.cursorTaskId);
    }
    function toggleCursorSelection() {
        if (!root.cursorTaskId || root._indexOfTask(root.cursorTaskId) < 0) { root.moveCursor(0, 1); return; }
        root.cursorVisible = true;
        AppController.toggleTaskSelection(root.cursorTaskId);
    }
    // Every task the filter lets through, folded groups included, as on the
    // board: Ctrl A took 60 of 1200 here and all 1200 there, and said nothing
    // about the folded rest (A3-16, A5-24). A big delete asks first (A1-28).
    function selectAllVisible() {
        const ids = [];
        for (let i = 0; i < root._all.length; i++)
            if (root._all[i].kind === "task") ids.push(root._all[i].id);
        AppController.setSelectedTaskIds(ids);
    }
    function archiveCursor() {
        if (AppController.selectionCount > 0) { AppController.setSelectedTasksArchived(true); return; }
        const id = root._actionCardId();
        if (id) AppController.setArchived(id, true);
    }
    function openCursorMenu() {
        const id = root._actionCardId();
        if (!id) { root.moveCursor(0, 1); return; }
        root.cursorTaskId = id;
        root.cursorVisible = true;
        root._reveal();
        list.forceLayout();
        const row = list.itemAtIndex(root._indexOfTask(id));
        menuHost.taskId = id;
        menuHost.anchorItem = row || root;
        menuHost.releaseMenu();
        menuHost.openMenu();
    }
    function openTaskMenu(id) {
        menuHost.taskId = id;
        menuHost.anchorItem = root;
        menuHost.releaseMenu();
        menuHost.popup();
    }
    // z a: fold the group the cursor is in.
    function toggleCursorColumn() {
        const i = root._indexOfTask(root.cursorTaskId);
        if (i >= 0) root.toggleGroup(root._rows[i].groupId);
    }
    Connections {
        target: AppController
        function onSelectedTaskIdsChanged() { if (AppController.selectionCount === 0) root._anchorId = ""; }
    }

    TaskMenuHost {
        id: menuHost
        boardKeys: true
        onOpenRequested: root.taskClicked(menuHost.taskId)
    }

    // ── Layout ───────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: list
            objectName: "task-list"
            Layout.fillWidth: true
            Layout.fillHeight: root.taskCount > 0
            // Rows and headers sit inset from the query bar's edge (R3-034).
            Layout.leftMargin: Theme.pagePadX
            Layout.rightMargin: Theme.pagePadX
            // Quiet rows keep to a reading width (Q-List: 900 px, DG-032).
            Layout.maximumWidth: Style.fills ? -1 : Theme.px(900)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root._rows
            cacheBuffer: 400
            reuseItems: true
            ScrollBar.vertical: ThinScrollBar {}
            // Room under the last row for the selection bar, which covered
            // half of it (A5-25).
            footer: Item { width: 1; height: Theme.sp2xl + (AppController.selectionCount > 0 ? Theme.px(42) + Theme.sp2xl : 0) }
            TapHandler { onTapped: { AppController.clearSelection(); root.clearCursor(); } }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool isGroup: row.modelData.kind === "group"
                readonly property string taskId: row.isGroup ? "" : row.modelData.id
                // A tracker task's sync facts, for its mark (R3-144); only a
                // own row (this profile) can be one.
                readonly property var _task: !row.isGroup && row.modelData.own !== false
                    ? AppController.taskById(row.taskId) : null
                readonly property var ticket: row._task ? (row._task.ticket || ({})) : ({})
                width: list.width
                function _arrive() {
                    if (!row.isGroup && root._fresh[row.taskId]) { row.opacity = 0; rowFade.restart(); }
                    else { rowFade.stop(); row.opacity = 1; }
                }
                Component.onCompleted: row._arrive()
                ListView.onReused: row._arrive()
                NumberAnimation { id: rowFade; target: row; property: "opacity"; to: 1; duration: Theme.durPop; easing.type: Theme.easeEnter }
                height: row.isGroup ? groupHead.implicitHeight + (row.index === 0 ? Theme.spLg : Theme.sp2xl) + Theme.spMd
                                    : Theme.px(36)

                // ── a group ──
                Item {
                    id: groupHead
                    visible: row.isGroup
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spMd
                    anchors.leftMargin: root._rowInset
                    implicitHeight: headRow.implicitHeight
                    readonly property bool folded: row.isGroup && root.isFolded(row.modelData.groupId)
                    RowLayout {
                        id: headRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: Theme.spMd
                        StatusRing {
                            visible: row.isGroup && row.modelData.key === "status"
                            category: row.isGroup && row.modelData.category ? row.modelData.category : "todo"
                        }
                        SectionHeader {
                            objectName: "list-group-" + (row.isGroup ? row.modelData.groupId : "")
                            title: row.isGroup ? root.groupTitle(row.modelData) : ""
                            // H2-List 15/600, Q-List 14/500 (R3-034).
                            titleSize: Style.fills ? Theme.fsLg : Theme.px(14)
                            titleWeight: Style.fills ? Theme.fwHeading : Theme.fwTitle
                            // Quiet group titles are plain (Q-List).
                            titleColor: !Style.fills ? Theme.textMuted
                                      : row.isGroup && row.modelData.key === "today" ? Theme.signalNow
                                      : row.isGroup && row.modelData.key === "overdue" ? Theme.signalUrgent
                                      : Theme.text
                            note: !row.isGroup ? ""
                                : groupHead.folded ? (Style.chipFill ? I18n.count(row.modelData.count, "list.tasks") + " — " + I18n.t("list.expand")
                                                                     : "· " + row.modelData.count)
                                : root.groupNote(row.modelData)
                        }
                        Item { Layout.fillWidth: true }
                    }
                    ClickArea {
                        objectName: "list-group-toggle"
                        // The only group cannot fold (isFolded).
                        enabled: row.isGroup && root._groupCount > 1
                        label: (groupHead.folded ? I18n.t("list.expand") : I18n.t("list.collapse")) + " " + (row.isGroup ? root.groupTitle(row.modelData) : "")
                        showTip: false
                        onActivated: root.toggleGroup(row.modelData.groupId)
                    }
                }

                // ── a task ──
                Rectangle {
                    id: taskRow
                    objectName: "list-row"
                    visible: !row.isGroup
                    anchors.fill: parent
                    readonly property bool cursored: root.cursorVisible && root.cursorTaskId === row.taskId
                    readonly property bool selected: AppController.selectionCount >= 0 && AppController.isTaskSelected(row.taskId)
                    radius: Theme.radiusMd
                    color: taskRow.selected || taskRow.cursored ? Theme.surfaceCard
                         : rowHover.hovered ? Theme.surfaceCardHover : "transparent"
                    // Out of step with the tracker: the edge says so, as on
                    // the card — amber / red in bold, a plain line in quiet.
                    border.width: rowMark.conflict || rowMark.pending ? 1 : 0
                    border.color: rowMark.conflict ? (Style.urgency ? Theme.danger : Theme.borderStrong)
                                : rowMark.pending ? (Style.urgency ? Theme.warning : Theme.borderStrong)
                                : "transparent"
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                        height: 1
                        color: Theme.border
                        visible: Style.chipFill && !taskRow.cursored && !taskRow.selected
                    }
                    HoverHandler {
                        id: rowHover
                        onHoveredChanged: {
                            if (hovered) root.hoveredTaskId = row.taskId;
                            else if (root.hoveredTaskId === row.taskId) root.hoveredTaskId = "";
                        }
                    }
                    RowLayout {
                        // Over the row's MouseArea, so "вернуть" takes its own click.
                        z: 1
                        anchors.fill: parent
                        anchors.leftMargin: root._rowInset
                        anchors.rightMargin: root._rowInset
                        spacing: Theme.spLg
                        Item {
                            Layout.preferredWidth: Theme.statusRingSize
                            Layout.preferredHeight: Theme.statusRingSize
                            StatusRing {
                                id: ring
                                visible: !taskRow.selected
                                subject: row.taskId
                                category: row.isGroup ? "todo" : (row.modelData.category || "todo")
                            }
                            // Selected: a check in a circle (form, not colour).
                            Rectangle {
                                objectName: "list-row-check"
                                visible: taskRow.selected
                                anchors.fill: parent
                                radius: width / 2
                                color: Theme.text
                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    color: Theme.bg
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsXs
                                    font.weight: Theme.fwTitle
                                }
                            }
                            CursorBar {
                                anchors.left: parent.left
                                anchors.top: parent.bottom
                                anchors.topMargin: Theme.spXs
                                shown: taskRow.cursored
                            }
                        }
                        // Bold: key before the title; quiet: after it (Q-List).
                        Text {
                            objectName: "list-row-key"
                            visible: Style.chipFill && !root._archive
                            Layout.preferredWidth: Theme.px(68)
                            text: row.isGroup ? "" : row.modelData.key
                            textFormat: Text.PlainText
                            color: Theme.textDim
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsXs
                            elide: Text.ElideRight
                        }
                        Text {
                            objectName: "list-row-title"
                            Layout.fillWidth: true
                            text: row.isGroup ? "" : row.modelData.title
                            textFormat: Text.PlainText
                            // An archive row is a quiet record (X/N-Oth-Archive-People).
                            color: rowMark.gone || root._archive ? Theme.textMuted : Theme.text
                            font.strikeout: rowMark.gone
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsMd
                            font.weight: root._archive ? Theme.fwBody : Theme.fwTaskTitle
                            elide: Text.ElideRight
                        }
                        // A merge / pull request's two dots (dogfood 2026-10-20 item 9).
                        MrDots {
                            objectName: "list-row-mr"
                            Layout.alignment: Qt.AlignVCenter
                            task: row._task
                        }
                        TrackerMark {
                            id: rowMark
                            objectName: "list-row-mark"
                            Layout.maximumWidth: Theme.px(320)
                            elide: Text.ElideRight
                            taskId: row.taskId
                            ticket: row.ticket
                            trackerName: row.ticket.provider ? String((AppController.providerBadges[row.ticket.provider] || {}).name || "") : ""
                            writeOn: !!row.ticket.provider && AppController.trackerWriteProviders.indexOf(row.ticket.provider) >= 0
                            onResolveRequested: root.openConflict(row.taskId)
                        }
                        Rectangle {
                            objectName: "list-row-label"
                            visible: Style.chipFill && !root._archive && !row.isGroup && String(row.modelData.label || "").length > 0
                            radius: Theme.radiusSm
                            color: Theme.chipBg
                            implicitWidth: Math.min(Theme.px(120), labelT.implicitWidth + 2 * Theme.spSm)
                            implicitHeight: labelT.implicitHeight + 2 * Theme.sp2xs
                            Text {
                                id: labelT
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, Theme.px(120) - 2 * Theme.spSm)
                                elide: Text.ElideRight
                                text: row.isGroup ? "" : String(row.modelData.label || "")
                                textFormat: Text.PlainText
                                color: Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                            }
                        }
                        Text {
                            objectName: "list-row-key-quiet"
                            visible: !Style.chipFill && !root._archive
                            text: row.isGroup ? "" : row.modelData.key
                            textFormat: Text.PlainText
                            color: Theme.textDim
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsXs
                        }
                        Text {
                            objectName: "list-row-priority"
                            visible: !root._archive
                            readonly property string pri: row.isGroup ? "" : String(row.modelData.priority || "")
                            Layout.preferredWidth: Theme.px(24)
                            text: Theme.priorityShown(pri) ? pri : ""
                            color: Theme.priorityInk(pri)
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                            font.weight: Theme.fwTitle
                        }
                        // The archive row's facts: "APP-111 · 6 окт", the key and the
                        // day it left (R3-055).
                        Text {
                            objectName: "list-row-archived"
                            visible: root._archive
                            text: row.isGroup ? "" : root.archivedFacts(row.modelData)
                            textFormat: Text.PlainText
                            color: Theme.textDim
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            font.pixelSize: Theme.fsSm
                        }
                        Text {
                            objectName: "list-row-date"
                            visible: !root._archive
                            // A fixed column, as the sheet's 92 px: a long date
                            // ("сегодня · 10:00 · срок 16 окт") widened its own
                            // row and pushed the label and P out of line. It ends
                            // with "…" instead (b-03).
                            Layout.preferredWidth: Theme.px(120)
                            Layout.minimumWidth: Theme.px(120)
                            Layout.maximumWidth: Theme.px(120)
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignRight
                            readonly property var d: row.isGroup ? null : root.rowDate(row.modelData)
                            readonly property int days: d ? TaskDates.daysFrom(d.date, AppController.today) : NaN
                            text: (I18n.lang, row.isGroup || !d) ? "" : root.rowDateText(row.modelData)
                            color: days < 0 && row.modelData.category !== "done" ? Theme.signalUrgent
                                 : days === 0 ? Theme.signalNow : Theme.textMuted
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            font.pixelSize: Theme.fsSm
                        }
                        // The archive's main action on the current row (DG-161,
                        // X-Oth-Archive-People): "вернуть", the same restore as
                        // the task menu. No key hint: no key is bound to it.
                        Text {
                            objectName: "list-row-restore"
                            readonly property bool shown: root._archive && !row.isGroup && !!row.modelData.archived
                                                          && (taskRow.cursored || rowHover.hovered)
                            Layout.preferredWidth: restoreMetrics.advanceWidth
                            opacity: shown ? 1 : 0
                            visible: root._archive
                            text: I18n.t("list.restore")
                            color: restoreCA.hovered ? Theme.text : Theme.textMuted
                            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                            font.underline: restoreCA.hovered
                            TextMetrics { id: restoreMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; text: I18n.t("list.restore") }
                            ClickArea {
                                id: restoreCA
                                enabled: parent.shown
                                label: parent.text
                                onActivated: AppController.setArchived(row.taskId, false)
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !row.isGroup
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) { root.openTaskMenu(row.taskId); return; }
                            if (mouse.modifiers & Qt.ControlModifier) {
                                AppController.toggleTaskSelection(row.taskId);
                                return;
                            }
                            if (mouse.modifiers & Qt.ShiftModifier) {
                                const ids = root._taskIds();
                                const a = Math.max(0, ids.indexOf(root.cursorTaskId));
                                const b = ids.indexOf(row.taskId);
                                AppController.setSelectedTaskIds(ids.slice(Math.min(a, b), Math.max(a, b) + 1));
                                return;
                            }
                            if (AppController.selectionCount > 0) AppController.clearSelection();
                            root.cursorTaskId = row.taskId;
                            root.cursorVisible = false;
                            root.taskClicked(row.taskId);
                        }
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: row.isGroup ? "" : row.modelData.key + " " + row.modelData.title
                }
            }
        }

        // Nothing matches the query: say so, once, in the middle (X-Err-Empty).
        Item { visible: root.taskCount === 0; Layout.fillHeight: true }
        EmptyState {
            objectName: "list-empty"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(root.width - 96, 360)
            visible: root.taskCount === 0
            title: root.nothingFound ? I18n.t("view.empty.noMatchFor").arg(root.filterLabel)
                 : root.hiddenByDefault > 0 ? I18n.t("list.empty.allDone") : I18n.t("list.empty")
            // Where the filter would find something (A3-13).
            hint: root.nothingFound ? AppController.matchesElsewhere(root.searchText)
                : root.hiddenByDefault > 0 ? I18n.count(root.hiddenByDefault, "list.empty.hiddenDone") : ""
            line: root.nothingFound ? I18n.t("view.empty.resetFilter")
                : root.hiddenByDefault > 0 ? I18n.t("list.empty.showDone")
                : I18n.t("tasks.empty.line").arg(AppController.shortcutText("task.new"))
            lineLink: root.nothingFound || root.hiddenByDefault > 0
            onLineActivated: root.hiddenByDefault > 0 ? root.showDoneRequested() : root.resetFilterRequested()
        }
        Item { visible: root.taskCount === 0; Layout.fillHeight: true }

        // The keys along the bottom (H2-List), in the bold style.
        Rectangle {
            objectName: "list-hints"
            Layout.fillWidth: true
            // The selection bar takes this place with its own keys (A5-25).
            visible: Style.keyHints && AppController.selectionCount === 0
            implicitHeight: hintRow.implicitHeight + 2 * Theme.spLg
            color: Theme.bg
            Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: Theme.border }
            Row {
                id: hintRow
                anchors.left: parent.left
                anchors.leftMargin: Theme.sp2xl
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spXl
                Repeater {
                    model: [["j k", "list.hint.move"], ["d", "list.hint.done"], ["s", "list.hint.schedule"],
                            ["↵", "list.hint.open"], ["m", "list.hint.menu"], ["?", "list.hint.keys"], ["v", "list.hint.mark"]]
                    delegate: Row {
                        id: hint
                        required property var modelData
                        spacing: Theme.spXs
                        Text {
                            text: hint.modelData[0]
                            color: Theme.text
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsXs
                            font.weight: Theme.fwTitle
                        }
                        Text {
                            text: I18n.t(hint.modelData[1])
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                        }
                    }
                }
            }
        }
    }

    // ── Text ─────────────────────────────────────────────────────────
    function groupTitle(g) {
        if (g.key === "status") return g.name || g.value;
        if (g.key === "priority") return g.value.length > 0 ? g.value : I18n.t("list.group.noPriority");
        if (g.key === "profile") return g.value;
        if (g.key === "month") {
            if (!g.from || !g.from.getTime) return I18n.t("list.group.none");
            const m = I18n.monthName(g.from.getMonth());
            const name = m.charAt(0).toUpperCase() + m.slice(1);
            return g.from.getFullYear() === new Date().getFullYear() ? name : name + " " + g.from.getFullYear();
        }
        return I18n.t("list.group." + g.key);
    }
    function groupNote(g) {
        // "Октябрь 6": an archive month carries its count (R3-056).
        if (g.key === "month") return String(g.count);
        const from = g.from, to = g.to;
        if (!from || !from.getTime) return "";
        if (g.key === "today" || g.key === "tomorrow") return I18n.fmtDate(from, "weekdayDay");
        if (g.key === "week") return I18n.t("list.until").arg(I18n.fmtDate(to, "weekdayDay"));
        if (g.key === "nextWeek") {
            const sameMonth = from.getMonth() === to.getMonth();
            return (sameMonth ? String(from.getDate()) : I18n.fmtDate(from, "dayMonth")) + " – " + I18n.fmtDate(to, "dayMonth");
        }
        return "";
    }
    function archivedFacts(r) {
        const c = r.changed;
        const day = c && c.getTime && !isNaN(c.getTime()) ? I18n.fmtDate(c, "dayMonth") : "";
        return day.length > 0 ? r.key + " · " + day : r.key;
    }
    // The date a row shows: when, else the deadline.
    function rowDate(r) {
        if (r.when && r.when.getTime && !isNaN(r.when.getTime())) return { date: r.when, timed: !!r.whenHasTime };
        if (r.due && r.due.getTime && !isNaN(r.due.getTime())) return { date: r.due, timed: !!r.dueHasTime };
        return null;
    }
    // A task with both a "when" and a deadline shows both (APP-263).
    function rowDateText(r) {
        const d = root.rowDate(r);
        if (!d) return "";
        const main = TaskDates.rowText(d.date, d.timed, AppController.today, !Style.fills);
        const hasDue = r.due && r.due.getTime && !isNaN(r.due.getTime());
        if (d.date === r.when && hasDue && TaskDates.daysFrom(r.due, r.when) !== 0)
            return main + " · " + I18n.t("list.due").arg(TaskDates.rowText(r.due, !!r.dueHasTime, AppController.today, !Style.fills));
        return main;
    }
}
