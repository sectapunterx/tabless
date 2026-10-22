import QtQuick
import QtQuick.Shapes
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Controls as QQC
import TodoCpp
import "PlainText.js" as MdPlain
import "Motion.js" as Motion
import "TaskDates.js" as TaskDates
import "Effort.js" as Effort

Rectangle {
    id: card
    objectName: "tc-card"
    property var task         // QVariantMap-like with id,title,desc,priority,status,deadline,branch,archived,blockedStuck
    property string scheduled
    property string taskId: task ? task.id : ""
    readonly property bool _isStuck: !!(card.task && card.task.blockedStuck === true)
    readonly property bool _isArchived: !!(card.task && card.task.archived === true)
    // Selection state — re-evaluates via AppController.selectedTaskIdsChanged
    // (selectionCount is read in the binding so QML tracks the dependency).
    // The keyboard cursor is on this card. Distinct from selection: the cursor
    // is where the next key acts, a selection is what a bulk action acts on,
    // and a card can be either, both or neither.
    property bool cursored: false
    // On the board, where Return / E act on the card under the cursor: its
    // menu shows those keys (APP-166).
    property bool boardKeys: false
    readonly property bool _selected: AppController.selectionCount >= 0
        && AppController.isTaskSelected(card.taskId)
    // Ticket identity (HEAP-117). `ticket` is empty for a locally-created task,
    // which is what every ticket-only chip below keys off.
    readonly property var _ticket: (card.task && card.task.ticket) ? card.task.ticket : ({})
    readonly property bool _isTicket: !!(card._ticket.provider)
    readonly property var _badge: card._isTicket
        ? (AppController.providerBadges[card._ticket.provider] || ({}))
        : ({})
    // heap changes this tracker's status when the card moves (APP-243; off
    // unless the user turned it on in Settings → Integrations).
    readonly property bool _writeOn: card._isTicket
        && AppController.trackerWriteProviders.indexOf(card._ticket.provider) >= 0
    // Outside the filter or gone while writes are on: the tracker is
    // read-only for this card, so its status does not move (APP-204).
    readonly property bool _statusLocked: card._writeOn && (!!card._ticket.outOfScope || !!card._ticket.gone)
    // Read by the views so a bare "O" can open whichever card is under the
    // cursor when nothing is selected.
    readonly property bool hovered: hoverArea.containsMouse
    // Any of the card's menus is up. The board holds its arrow/letter keys
    // back while one is, or Down moved the board cursor under the menu.
    // `visible`, not `opened`: opened only turns true once the fade-in ends,
    // and a key pressed during it would still reach the board.
    readonly property bool menuOpen: menuHost.menuOpen || (ciPopLoader.item !== null && ciPopLoader.item.visible)
    readonly property bool _done: !!(card.task && AppController.statusCategory(card.task.status) === "done")
    signal clicked()

    // Open the card's menu from the keyboard (board key M, or the Menu key),
    // anchored on the card rather than on a pointer that may be elsewhere.
    function openMenu() { menuHost.openMenu(); }
    Keys.onMenuPressed: card.openMenu()

    // The CI popover under the card (dogfood 2026-10-22): a click on the
    // chip or the board's key. Made on first use, as the menus are.
    Loader {
        id: ciPopLoader
        active: false
        sourceComponent: CiPopover {}
    }
    // Whether a point of the card (its own coordinates) is on the CI chip.
    function _onCiChip(x, y) {
        if (!ciChip.visible) return false;
        const p = ciChip.mapFromItem(card, x, y);
        return p.x >= 0 && p.y >= 0 && p.x <= ciChip.width && p.y <= ciChip.height;
    }
    function openCi() {
        if (card._ciDot.length === 0 || card.taskId.length === 0) return false;
        ciPopLoader.active = true;
        ciPopLoader.item.openAt(card, card.taskId);
        return true;
    }

    // Here vs tracker, field by field (APP-163). Made on first use: a board of
    // cards must not each carry a dialog.
    Loader {
        id: conflictLoader
        active: false
        sourceComponent: SyncConflictDialog {
            // Not from inside its own signal: that would destroy the sender.
            onClosed: Qt.callLater(() => { conflictLoader.active = false; })
        }
    }
    readonly property var conflictDialog: conflictLoader.item
    function openConflictDialog() {
        if (!card.task) return;
        conflictLoader.active = true;
        const fresh = AppController.taskById(card.task.id);
        const dlg = conflictLoader.item as SyncConflictDialog;
        if (dlg) dlg.showFor(fresh && fresh.id ? fresh : card.task);
    }

    // When the work is planned, for a task that has a scheduledAt of its own
    // worth showing: no deadline, a different day, or a clock time. Before,
    // a task with only a schedule looked undated (TASKS-12).
    function _schedLabel(noClock) {
        if (card.scheduled && card.scheduled.length > 0) return card.scheduled;
        const t = card.task;
        if (!t || !t.scheduledAt || !t.scheduledAt.getTime || isNaN(t.scheduledAt.getTime())) return "";
        const s = t.scheduledAt;
        const dl = t.deadline;
        const sameDay = dl && dl.getTime && !isNaN(dl.getTime())
            && dl.getFullYear() === s.getFullYear() && dl.getMonth() === s.getMonth() && dl.getDate() === s.getDate();
        if (sameDay && !t.scheduledHasTime) return "";
        return TaskDates.cardText(s, !!t.scheduledHasTime && !noClock, AppController.today);
    }

    signal rangeSelectRequested(string anchorId)
    // A status picked from the card's own menu, just before the task moves:
    // the board lays a card that goes to Done on its stack (APP-176) and has
    // to see where the card was while it is still there.
    signal statusPicked(string statusId)

    // Time tracking (HEAP-78): tick once a second while this task's timer runs
    // so the elapsed chip stays live; _timerTick is read in the chip binding.
    property int _timerTick: 0
    Timer {
        interval: 1000; repeat: true
        running: !!(card.task && card.task.isTiming === true)
        onTriggered: card._timerTick++
    }
    function _fmtElapsed(s) {
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
        if (h > 0) return h + "h " + m + "m";
        if (m > 0) return m + "m " + sec + "s";
        return sec + "s";
    }
    function _recurLabel(r) {
        return TaskDates.repeatText(r);
    }

    // Selection and the keyboard cursor look different in shape, not only
    // in colour (VISU-4, APP-174): on a monochrome theme accent and
    // accentStrong are both near-white, and the two were the same 2px white
    // border. Cursor (and Tab focus): the FocusRing on the card's edge, in
    // the cursor colour, with its halo inside. Selected: a filled card and a
    // check in a circle, no ring and no border of its own.
    radius: Theme.radius
    color: _isArchived ? Theme.withAlpha(Theme.surfaceCard, 0.55)
        : markT.outOfScope && !_selected ? "transparent"
        : _selected ? Qt.tint(Theme.surfaceCard, Theme.withAlpha(Theme.text, 0.09))
        : hoverArea.containsMouse ? Theme.surfaceCardHover
        : Theme.surfaceCard
    // heap 2 (APP-262): a card is a surface without a border; the cursor and
    // the selection are told apart by shape (ring, check), not by an outline
    // colour.
    // Lifted, the card is an opaque surface with no accent edge (R3-052).
    border.color: dragArea.drag.active ? "transparent"
                : _isStuck ? Theme.danger
                // Out of step with the tracker (R2-034): the edge says so —
                // amber / red in bold, a plain line in quiet.
                : markT.conflict ? (Style.urgency ? Theme.danger : Theme.borderStrong)
                : markT.pending ? (Style.urgency ? Theme.warning : Theme.borderStrong)
                // A white card on the light ground had no edge at all (1.06:1,
                // EYES-4): the light themes keep a hairline, dark ones none.
                : Theme.cardBorder
    border.width: _isStuck && !dragArea.drag.active ? 2 : 1
    // A list's add/remove transitions fade this, not `opacity`, so the
    // archived dim below keeps its binding through them (APP-175).
    property real enterFade: 1
    opacity: enterFade * (dragArea.drag.active ? 1.0 : (_isArchived ? 0.7 : 1.0))
    scale: dragArea.drag.active ? 1.03 : 1.0
    transformOrigin: Item.Center
    z: dragArea.drag.active ? 1000 : 0
    // A lifted card grows a little and settles back without overshoot
    // (APP-175); with reduced motion it simply is where it was put.
    Behavior on scale { NumberAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
    Behavior on border.color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }

    // Outside the tracker's filter: a dashed edge, "only yours" (R2-034).
    Shape {
        objectName: "tc-dashed"
        anchors.fill: parent
        visible: markT.outOfScope && !dragArea.drag.active
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.borderStrong
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 3]
            fillColor: "transparent"
            startX: card.radius; startY: 0.5
            PathLine { x: card.width - card.radius; y: 0.5 }
            PathArc { x: card.width - 0.5; y: card.radius; radiusX: card.radius - 0.5; radiusY: card.radius - 0.5 }
            PathLine { x: card.width - 0.5; y: card.height - card.radius }
            PathArc { x: card.width - card.radius; y: card.height - 0.5; radiusX: card.radius - 0.5; radiusY: card.radius - 0.5 }
            PathLine { x: card.radius; y: card.height - 0.5 }
            PathArc { x: 0.5; y: card.height - card.radius; radiusX: card.radius - 0.5; radiusY: card.radius - 0.5 }
            PathLine { x: 0.5; y: card.radius }
            PathArc { x: card.radius; y: 0.5; radiusX: card.radius - 0.5; radiusY: card.radius - 0.5 }
        }
    }

    FocusRing {
        objectName: "tc-cursor-ring"
        anchors.margins: 0
        radius: card.radius
        haloInside: true
        visible: (card.cursored || card.activeFocus) && !dragArea.drag.active
    }

    // A card the last sync brought in that has not been opened or reached
    // by the cursor yet (APP-180): a quiet dot in the cursor's colour, in
    // the corner, clear of everything the card says.
    readonly property bool isNew: AppController.unseenRevision >= 0 && AppController.isTaskUnseen(card.taskId)
    Rectangle {
        objectName: "tc-new-dot"
        visible: card.isNew
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spXs
        width: 6
        height: 6
        radius: 3
        color: Theme.focusRing
        z: 2
    }

    implicitWidth: parent ? parent.width : 260
    implicitHeight: contentCol.implicitHeight + 2 * Theme.spLg

    // A card is a button to assistive tech and to the Tab key.
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: card.task ? ((card._isTicket ? (card._ticket.key || "") : card.task.id) + " " + card.task.title) : ""
    Accessible.onPressAction: card.clicked()
    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()

    // What the card's small chips say only under the pointer (APP-184): the
    // tracker, why it is out of step, whose move it is, the labels past two.
    // Read to a screen reader, and shown when Tab lands on the card.
    readonly property string hoverDetails: {
        if (!card.task) return "";
        const parts = [];
        if (card._isTicket) {
            const tracker = (card._badge.name || card._ticket.provider || "")
                + (card._ticket.project ? " · " + card._ticket.project : "");
            if (tracker.length > 0) parts.push(tracker);
            if (markT.visible) parts.push(markT.tip);
            // The tracker's own status, when the card sits somewhere else:
            // with writes off that is the normal case, and the card itself
            // stays uncluttered (APP-243).
            if (card._ticket.remoteStatus && card._ticket.remoteColumn !== card.task.status)
                parts.push(I18n.t("taskcard.trackerStatus").arg(card._ticket.remoteStatus));
        }
        if (mrDots.visible)
            parts.push([mrDots.ciText, mrDots.mergeText].filter(s => s.length > 0).join(" · "));
        if (moveChip.visible && moveChip.tip.length > 0) parts.push(moveChip.tip);
        const labels = card.task.labels || [];
        if (labels.length > 2) parts.push(labels.slice(2).map(l => l.id).join(", "));
        if (localMarks.tip.length > 0) parts.push(localMarks.tip);
        return parts.join("
");
    }
    Accessible.description: card.hoverDetails
    QQC.ToolTip.visible: card.activeFocus && card.hoverDetails.length > 0
    QQC.ToolTip.delay: 600
    QQC.ToolTip.text: card.hoverDetails

    // Drag.active is automatically driven by MouseArea.drag.active
    Drag.active: dragArea.drag.active
    Drag.dragType: Drag.Internal
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: 20

    property real homeX: 0
    property real homeY: 0
    // Where the card goes while it is dragged: an item above everything it
    // can be dropped on. Without one it drags inside its own parent, which
    // clips it and draws it under later siblings.
    property Item dragLayer: null
    // Reparented by hand rather than with a State + ParentChange: undoing a
    // ParentChange also restores the card's stacking order against the
    // sibling it was lifted from, and in a list that recycles its delegates
    // that sibling may since have gone back to the pool — Qt warned on every
    // drop into another column.
    readonly property bool _lifted: dragArea.drag.active && card.dragLayer !== null
    property Item _homeParent: null
    // Where the lifted card came from keeps a dashed, faded placeholder
    // (X/N-Oth-Select-Drag, R3-051).
    property Item _origin: null
    Component.onDestruction: if (card._origin) card._origin.destroy()
    Component {
        id: originComp
        DashedRect {
            objectName: "tc-drag-origin"
            // The card's own fill and title, faded, inside a dashed edge — in
            // both styles (R4-038: quiet showed an empty outline).
            opacity: 0.35
            radius: Theme.radius
            fillColor: Theme.surfaceCard
            Text {
                anchors.fill: parent
                anchors.margins: Theme.spLg
                text: card.task ? card.task.title : ""
                textFormat: Text.PlainText
                color: Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
                font.weight: Theme.fwTaskTitle
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }
    // Where the card was let go, in window coordinates: a card dropped back
    // where it came from glides home from there instead of jumping (APP-167).
    property var _dropAt: null
    transform: Translate { id: settle }
    ParallelAnimation {
        id: settleAnim
        NumberAnimation { target: settle; property: "x"; to: 0; duration: Theme.durMove; easing.type: Theme.easeEnter }
        NumberAnimation { target: settle; property: "y"; to: 0; duration: Theme.durMove; easing.type: Theme.easeEnter }
    }
    function _settleHome() {
        settleAnim.stop();
        settle.x = 0;
        settle.y = 0;
        if (!card._dropAt || Theme.motion === 0) return;
        const home = card.mapToItem(null, 0, 0);
        const off = Motion.settleFrom(card._dropAt.x, card._dropAt.y, home.x, home.y, Theme.motion);
        card._dropAt = null;
        if (off.x === 0 && off.y === 0) return;
        settle.x = off.x;
        settle.y = off.y;
        settleAnim.start();
    }
    on_LiftedChanged: {
        if (card._lifted) {
            const p = card.mapToItem(card.dragLayer, 0, 0);
            card._homeParent = card.parent;
            card._origin = originComp.createObject(card.parent, { x: card.x, y: card.y, width: card.width, height: card.height });
            card.parent = card.dragLayer;
            card.x = p.x;
            card.y = p.y;
        } else if (card._homeParent) {
            if (card._origin) { card._origin.destroy(); card._origin = null; }
            card.parent = card._homeParent;
            card._homeParent = null;
            // Back in the list: the list owns x/y, so put the card where it
            // was pressed rather than where the drag left it.
            card.x = card.homeX;
            card.y = card.homeY;
            card._settleHome();
        }
    }

    // What sits on a card (APP-179): the title, up to two lines, and one line
    // under it: the key, the date, and the priority when it is P0 or P1.
    // That is all a board shows at rest. Under the cursor — the keyboard's or
    // the pointer's — the card opens the first line of its description, how
    // far along its checklist is, and the quieter facts. An alert that needs
    // the user (out of step with the tracker, stuck, archived) gets its own
    // row above the title, and only while there is one. No people and no
    // branch: heap shows the tasks that are mine, so the assignee is always
    // me, and the branch is the editor's business.
    // The pointer opens a card once it rests there: opening on the way past
    // grew every card it crossed, and the column below jumped under the
    // pointer aimed at the next one (EYE-2).
    readonly property bool detailsOpen: (card.cursored || card.activeFocus || card._hoverRested)
        && !dragArea.drag.active
    property bool _hoverRested: false
    // The description's first line with words in it: as prose (_excerpt)
    // and with its bold, italic, code and links drawn (_excerptHtml, APP-313).
    // A Jira Server card reads its wiki markup as markdown when that is on.
    readonly property string _excerptLine: {
        const desc = card._ticket.provider === "jira" && card.task && card.task.id
            ? AppController.trackerDescription(card.task.id) : String((card.task && card.task.desc) || "");
        // Code blocks out first: their fences would be a line of their own.
        const lines = desc.replace(/```[\s\S]*?```/g, " ").split("\n");
        for (let i = 0; i < lines.length; i++) {
            // A heading ("## Description", Jira's "h2. Description") names a
            // section, it says nothing; a Jira image ("!image-….png|thumbnail!")
            // is markup, not prose (EYE-7).
            const raw = lines[i].trim();
            if (/^(#{1,6}\s|h[1-6]\.\s)/i.test(raw)) continue;
            const line = raw.replace(/![^\s!|]+\.(?:png|jpe?g|gif|webp|svg|bmp)(?:\|[^!]*)?!/gi, " ");
            if (MdPlain.plain(line, 160).trim().length > 0) return line;
        }
        return "";
    }
    readonly property string _excerpt: MdPlain.plain(card._excerptLine, 160).trim()
    readonly property string _excerptHtml: card._excerpt.length > 0
        ? MdPlain.inlineHtml(card._excerptLine, 160, { code: Theme.accent, link: Theme.accent, mono: Theme.fontMono }) : ""
    readonly property var _cl: (card.task && card.task.checklist) ? card.task.checklist : ({})
    readonly property bool _timing: !!(card.task && card.task.isTiming)
    readonly property int _tracked: card.task ? (card.task.trackedSeconds || 0) : 0
    readonly property var _waiting: (AppController.safety && AppController.safety.waitingOn)
        ? AppController.waitingOn[card.taskId] : undefined
    // From each fact's own condition, not from the children's `visible`:
    // that reads false while the row is hidden, so a card given its task
    // after creation never showed its facts.
    // CI of a merge / pull request (APP-318): its own pipeline, or for a
    // Jira card the folded MR's checks. "" = none.
    readonly property string _ci: {
        const p = card._isTicket && card._ticket.details ? String(card._ticket.details.pipeline || "") : "";
        if (p.length > 0) return p;
        const c = card.task ? String(card.task.prChecks || "") : "";
        return c === "passing" ? "success" : c === "failing" ? "failed" : c === "pending" ? "running" : "";
    }
    // The chip's word: the dot's (mrOf) on a request, else the card's own
    // pipeline. "failed" | "running" | "passed" | "unknown" | "canceled" | "".
    readonly property string _ciDot: mrDots.ci.length > 0 ? mrDots.ci
        : card._mrCandidate ? ""
        : card._ci === "success" ? "passed" : card._ci
    // ciOf: the failed job's name, n/m, an old push's word. Only under the
    // cursor or with the popover open.
    readonly property var _ciMore: card._ciDot.length > 0 && card.taskId.length > 0
        && (card.detailsOpen || (ciPopLoader.item !== null && ciPopLoader.item.visible))
        ? (AppController.mrRevision, AppController.ciOf(card.taskId)) : ({})
    readonly property string _ciChipText: {
        const d = card._ciDot, c = card._ciMore;
        const word = (s) => s === "failed" ? "CI" + (String(c.firstFailed || "").length > 0 ? " · " + c.firstFailed : "")
            : s === "passed" || s === "success" ? I18n.t("ci.chip.passed")
            : s === "running" ? I18n.t("ci.state.running") + ((c.total || 0) > 0 ? " " + (c.done || 0) + "/" + c.total : "")
            : s === "canceled" ? I18n.t("ci.state.canceled")
            : "";
        if (d === "unknown") {
            // What the run of the push before said: "CI идёт 4/7 · старый пуш".
            const was = String(c.staleState || "");
            const old = was === "failed" ? I18n.t("ci.state.failed") : word(was);
            return old.length > 0 ? I18n.t("ci.chip.stale").arg(old) : I18n.t("mr.ci.unknown");
        }
        return word(d);
    }
    // A card that is, or carries, a merge / pull request: only these look
    // their dots up.
    readonly property bool _mrCandidate: (card.task ? String(card.task.prState || "") : "").length > 0
        || (card._isTicket && !!card._ticket.details
            && (card._ticket.details.kind === "mr" || card._ticket.details.kind === "pr"))
    readonly property var _mr: mrDots.mr
    readonly property var _mrWaiting: card._mr.waiting || []
    readonly property var _mrApproved: card._mr.approved || []
    // How long the request has been open, for "ждём: … · 2 дня".
    readonly property int _mrDays: {
        const s = card._mr.since;
        if (!s || !s.getTime || isNaN(s.getTime())) return 0;
        return Math.max(0, -TaskDates.daysFrom(s, AppController.today));
    }
    readonly property bool _hasFacts: schedMore.label.length > 0 || prT.prState.length > 0 || card._ci.length > 0
        || (!card._timing && card._tracked > 0)
        || !!(card.task && card.task.recurrence && String(card.task.recurrence).length > 0)
        || (card._isTicket && (card._ticket.commentCount || 0) > 0) || labelRep.count > 0
        || card._attachmentCount > 0 || card._waiting !== undefined || card._isTicket
    readonly property bool _hasDetails: card._excerpt.length > 0 || (card._cl.total || 0) > 0 || card._hasFacts
        || localMarks.hasContent
    // The detailed card (APP-281 A1) keeps the description's first line, the
    // checklist and the pull request at rest; the other facts wait for the
    // cursor as on a compact one.
    // One line at rest, the most telling one: the pull request, else the
    // checklist, else the description's first line. Three lines turned the
    // board into a feed; the rest waits for the cursor.
    // A merge / pull request at rest is its two dots, nothing more (dogfood
    // 2026-10-20 item 9): it has no line of its own here.
    readonly property string _restLine: !Style.detailedCards ? ""
        : (card._cl.total || 0) > 0 ? "checklist"
        : card._excerpt.length > 0 ? "excerpt" : ""
    readonly property bool _hasRestDetails: card._restLine.length > 0
    readonly property bool _alerting: card._isStuck || card._isArchived

    ColumnLayout {
        id: contentCol
        anchors.fill: parent
        anchors.margins: Theme.spLg
        spacing: Theme.spSm

        // Alerts: shown only when the card is out of step with something.
        RowLayout {
            objectName: "tc-alerts"
            Layout.fillWidth: true
            visible: card._alerting
            spacing: Theme.spSm
            Rectangle {
                objectName: "tc-stuck"
                visible: card._isStuck
                radius: Theme.radiusSm
                color: Theme.withAlpha(Theme.danger, 0.14)
                implicitWidth: stuckT.implicitWidth + 10
                implicitHeight: stuckT.implicitHeight + 2
                Text {
                    id: stuckT
                    anchors.centerIn: parent
                    text: I18n.t("task.chip.stuck")
                    color: Theme.danger
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                    font.weight: Theme.fwTitle
                }
            }
            Text {
                visible: card._isArchived
                text: I18n.t("task.chip.arch")
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spSm
        // Selected (DG-026, N-Oth-Select-Drag): a check in a filled ring
        // before the title — form, not colour; the card's fill says it too.
        // While a selection exists the other cards carry the empty ring, so
        // the choice is offered on them as well (R4-037).
        Rectangle {
            objectName: "tc-selected-mark"
            visible: card._selected || AppController.selectionCount > 0
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Math.max(0, (titleT.lineHeight - height) / 2)
            implicitWidth: Theme.statusRingSize
            implicitHeight: implicitWidth
            radius: width / 2
            color: card._selected ? Theme.textMuted : "transparent"
            border.width: card._selected ? 0 : 1.5
            border.color: Theme.textMuted
            Icon {
                visible: card._selected
                anchors.centerIn: parent
                name: "check"
                size: Math.round(parent.width * 0.75)
                color: Theme.bg
            }
        }
        Text {
            id: titleT
            objectName: "tc-title"
            Layout.fillWidth: true
            text: card.task ? card.task.title : ""
            // A mirrored title is written by whoever filed the issue. Text
            // defaults to AutoText, which would render HTML — and an <img> in
            // it fetches from the network on their say-so.
            textFormat: Text.PlainText
            color: markT.gone ? Theme.textMuted : Theme.text
            font.strikeout: markT.gone
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
            font.weight: Theme.fwTaskTitle
            // H2-Board 1.35, Q-Board 1.4 (R3-031) of the em. A proportional
            // lineHeight multiplies the font's own leading (R4-021).
            lineHeightMode: Text.FixedHeight
            lineHeight: Math.round(font.pixelSize * (Style.fills ? 1.35 : 1.4))
            // Wrap, not WordWrap: a URL or a long identifier has no space to
            // break at and ran off the card (TASKS-27).
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        // A merge / pull request's two dots, CI and "can it be merged"
        // (dogfood 2026-10-20 item 9): all a request shows at rest.
        MrDots {
            id: mrDots
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Math.max(0, (titleT.lineHeight - height) / 2)
            task: card.task
            active: card._mrCandidate
        }
        }

        // The one line of facts: key, date, priority.
        RowLayout {
            objectName: "tc-meta"
            Layout.fillWidth: true
            spacing: Theme.spMd
            Text {
                id: keyT
                objectName: "tc-key"
                // A mirrored issue is known by its tracker key, not by the
                // synthetic heap id ("github-68") the merge invented for it.
                text: card._isTicket ? (card._ticket.key || "") : (card.task ? card.task.id : "")
                textFormat: Text.PlainText
                // Metadata, so muted: accentStrong is white on a monochrome
                // theme and the key was as loud as the title (VISU-3).
                color: Theme.textMuted
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsXs
                wrapMode: Text.NoWrap
                // A click copies the key (dogfood 2026-10-16: there was no
                // way to copy it at all).
                ClickArea {
                    objectName: "tc-key-copy"
                    label: I18n.t("taskcard.copyId")
                    tip: I18n.t("taskcard.copyId.tip")
                    cursorShape: Qt.PointingHandCursor
                    activeFocusOnTab: false
                    onActivated: AppController.copyTaskKey(parent.text)
                }
            }
            // The due line's box: its width comes from the row alone, so the
            // text can pick the short form by it without feeding its own
            // width back into the layout (a binding loop on "text").
            Item {
                id: dueBox
                visible: dueT.dlText.length > 0
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: dueFull.advanceWidth + 1
                implicitHeight: dueT.implicitHeight
            Text {
                id: dueT
                objectName: "tc-due"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                readonly property int days: {
                    if (!card.task || !card.task.deadline) return 99999;
                    const dl = card.task.deadline;
                    // An unset date arrives as an Invalid Date — truthy, but its
                    // time is NaN, which used to render as "NaNd".
                    if (!dl.getTime || isNaN(dl.getTime())) return 99999;
                    return TaskDates.daysFrom(dl, AppController.today);
                }
                // Red is for a date already past, and only that (APP-179):
                // today and the next few days used to be red and amber too,
                // and a board of them read as a board of alarms.
                readonly property bool overdue: dlText.length > 0 && days < 0
                readonly property string dlText: {
                    if (days === 99999) return "";
                    const dl = card.task.deadline;
                    // Finished or archived work is not overdue (PLAT-24).
                    if ((card._done || card._isArchived) && days < 0) return "";
                    // A task due at a clock time shows it; a bare date does not.
                    let clock = "";
                    // dueHasTime since schema v10; views that still hand over the
                    // old single flag keep working.
                    const timed = card.task.dueHasTime !== undefined ? card.task.dueHasTime : card.task.hasTime;
                    if (timed && card.task.dueAt && card.task.dueAt.getHours) {
                        clock = " " + I18n.fmtTime(card.task.dueAt);
                    }
                    return TaskDates.cardText(dl, false, AppController.today) + clock;
                }
                // Today and tomorrow are amber in the bold style (APP-262);
                // past, red; the quiet style says it in words only.
                readonly property bool near: dlText.length > 0 && (days === 0 || days === 1)
                // Where "19 д просрочки" does not fit beside the key and the
                // priority, the short form "−19 д": it was cut to
                // "19 д прос…" on most cards (A3-21, A6-15).
                readonly property string shortText: {
                    if (!overdue) return dlText;
                    const timed = card.task.dueHasTime !== undefined ? card.task.dueHasTime : card.task.hasTime;
                    const clock = timed && card.task.dueAt && card.task.dueAt.getHours ? " " + I18n.fmtTime(card.task.dueAt) : "";
                    return I18n.t("task.due.overdueShort").arg(-days) + clock;
                }
                // Still too long, the clock goes before the date is cut:
                // "сегодня 1…" said neither the day nor the time (b-20).
                readonly property string bareText: {
                    if (days === 99999) return "";
                    if (overdue) return I18n.t("task.due.overdueShort").arg(-days);
                    return TaskDates.cardText(card.task.deadline, false, AppController.today);
                }
                TextMetrics { id: dueFull; font: dueT.font; text: dueT.dlText }
                TextMetrics { id: dueShort; font: dueT.font; text: dueT.shortText }
                // Width 0 = not laid out yet: the whole text, not a guess.
                text: dueBox.width <= 0 || dueBox.width >= dueFull.advanceWidth ? dlText
                    : overdue && dueBox.width >= dueShort.advanceWidth ? shortText
                    : bareText
                Accessible.name: dlText
                // Never under the card edge: the Russian overdue text ran past
                // it with the number cut off (EYES-2).
                elide: Text.ElideRight
                color: overdue ? Theme.signalUrgent : near ? Theme.signalNow : Theme.textDim
                font.family: Theme.fontUi
                font.features: Theme.tabularNums
                font.pixelSize: Theme.fsXs
            }
            }
            // A task with only a plan, no deadline, says when it is planned
            // here, or it looked undated (TASKS-12). With a deadline too, the
            // plan is one of the details.
            Text {
                id: schedT
                objectName: "tc-scheduled"
                readonly property string label: (AppController.today, I18n.lang, card._schedLabel())
                // Without the clock when the line is short of room (b-20).
                readonly property string bare: (AppController.today, I18n.lang, card._schedLabel(true))
                visible: label.length > 0 && !dueT.visible
                TextMetrics { id: schedFull; font: schedT.font; text: schedT.label }
                text: schedT.width <= 0 || schedT.width >= schedFull.advanceWidth ? label : bare
                // Inside the card's padding, as the due line is: "tomorrow
                // 15:00" ran to the card's edge (EYE-13).
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                // Its width from the full label, not from the text it picks
                // by that width, so the two do not chase each other.
                Layout.preferredWidth: schedFull.advanceWidth + 1
                elide: Text.ElideRight
                color: Theme.textDim
                font.family: Theme.fontUi
                font.features: Theme.tabularNums
                font.pixelSize: Theme.fsXs
            }
            // A running timer is the one live fact, so it stays on the line;
            // time already tracked is a detail. Click to stop.
            Text {
                id: timerT
                objectName: "tc-timer"
                visible: card._timing
                text: {
                    card._timerTick;  // re-evaluate each tick while running
                    return card.task && card._timing ? "● " + card._fmtElapsed(AppController.elapsedSecondsFor(card.task.id)) : "";
                }
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.features: Theme.tabularNums
                font.pixelSize: Theme.fsXs
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (card.task) AppController.stopTaskTimer(card.task.id)
                }
            }
            // A repeating task says so at rest, not only with the details
            // open: it read as a one-off (A2-25).
            Text {
                objectName: "tc-repeat-mark"
                visible: !card.detailsOpen && !!(card.task && card.task.recurrence && String(card.task.recurrence).length > 0)
                text: "↻"
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                Accessible.name: card._recurLabel(card.task ? card.task.recurrence : "")
            }
            Item { Layout.fillWidth: true }
            // P0 and P1 in bold, P0 only in quiet (R3-030); P2 and P3 are
            // blank on every card, as H2-Board / Q-Board draw them.
            Text {
                id: priT
                objectName: "tc-priority"
                readonly property string pri: card.task ? String(card.task.priority || "") : ""
                readonly property bool loud: pri === "P0" || pri === "P1"
                visible: Theme.priorityShown(pri)
                text: pri
                color: loud ? Theme.priorityInk(pri) : Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                font.weight: loud ? Theme.fwTitle : Theme.fwBody
            }
        }

        // Out of step with the tracker (X/N-Err-Tracker, R2-034); its own
        // line under the key: beside the date and priority it had no room.
        TrackerMark {
            id: markT
            objectName: "tc-mark"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            taskId: card.task ? card.task.id : ""
            ticket: card._ticket
            trackerName: card._badge.name || ""
            writeOn: card._writeOn
            onResolveRequested: card.openConflictDialog()
        }
        // Under the cursor: the description's first line, the checklist, and
        // the quieter facts. Opens and closes in a pop; `visible` holds while
        // it closes so the height can run down to nothing.
        Item {
            id: details
            objectName: "tc-details"
            Layout.fillWidth: true
            readonly property bool open: (card.detailsOpen && card._hasDetails) || card._hasRestDetails
            Layout.preferredHeight: details.open ? detailsCol.implicitHeight : 0
            visible: details.open || details.height > 0
            opacity: details.open ? 1 : 0
            clip: true
            Behavior on Layout.preferredHeight { NumberAnimation { duration: Theme.durPop; easing.type: Theme.easeEnter } }
            Behavior on opacity { NumberAnimation { duration: Theme.durPop; easing.type: Theme.easeEnter } }

            ColumnLayout {
                id: detailsCol
                width: parent.width
                spacing: Theme.spSm

                Text {
                    objectName: "tc-excerpt"
                    Layout.fillWidth: true
                    visible: card._excerpt.length > 0 && (card.detailsOpen || card._restLine === "excerpt")
                    text: card._excerptHtml
                    textFormat: Text.StyledText
                    color: Theme.textMuted
                    font.pixelSize: Theme.fsSm
                    // Two lines at 1.45 (APP-314): cut to one, the line read
                    // as a fragment and the eye went back to finish it.
                    wrapMode: Text.Wrap
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Math.round(font.pixelSize * 1.45)
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                // Checklist progress. A template ships its steps as markdown
                // task items; the bar says how far along it is without opening
                // the card.
                RowLayout {
                    id: checklistRow
                    readonly property int _total: card._cl.total || 0
                    readonly property int _done: card._cl.done || 0
                    visible: _total > 0 && (card.detailsOpen || card._restLine === "checklist")
                    Layout.fillWidth: true
                    spacing: Theme.spSm

                    Text {
                        objectName: "tc-checklist"
                        text: checklistRow._done + "/" + checklistRow._total
                        color: checklistRow._done === checklistRow._total ? Theme.success : Theme.textMuted
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                    }
                    // A bar rather than only a number: the ratio is the thing
                    // being read.
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 3
                        radius: Theme.radiusXs
                        color: Theme.panel3
                        Rectangle {
                            width: parent.width * (checklistRow._total > 0 ? checklistRow._done / checklistRow._total : 0)
                            height: parent.height
                            radius: parent.radius
                            // textDim, not accent: a white bar was as loud as
                            // the title on a monochrome theme (VISU-3).
                            color: checklistRow._done === checklistRow._total ? Theme.success : Theme.textDim
                        }
                    }
                }

                // My own layer: the next step, notes / draft / my tags marks
                // (APP-236…241). Its own component; the card only places it.
                TaskCardLocal {
                    id: localMarks
                    Layout.fillWidth: true
                    task: card.task
                }

                // The quieter facts. Flow, not a row: on a narrow column the
                // line wraps instead of pushing facts out past the card's edge.
                Flow {
                    id: metaFlow
                    Layout.fillWidth: true
                    visible: card.detailsOpen ? card._hasFacts : card._restLine === "pr"
                    spacing: Theme.spLg

                    // Provider badge: which tracker this card mirrors
                    // (HEAP-117), muted like the rest.
                    Text {
                        objectName: "tc-badge"
                        visible: card._isTicket && card.detailsOpen
                        // The tracker by name, in words: GitLab's glyph "▲"
                        // read as the P0 mark or a whose-move arrow before
                        // the request's line (acceptance 2026-10-20).
                        text: card._badge.name || card._ticket.provider || ""
                        textFormat: Text.PlainText
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                        QQC.ToolTip.visible: badgeHover.hovered
                        QQC.ToolTip.text: (card._badge.name || card._ticket.provider || "")
                            + (card._ticket.project ? " · " + card._ticket.project : "")
                            + (card._ticket.remoteStatus
                               ? "\n" + I18n.t("taskcard.trackerStatus").arg(card._ticket.remoteStatus) : "")
                        HoverHandler { id: badgeHover }
                    }
                    Text {
                        id: schedMore
                        objectName: "tc-scheduled-more"
                        readonly property string label: dueT.visible ? schedT.label : ""
                        visible: label.length > 0 && card.detailsOpen
                        text: "▸ " + label
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                    }
                    // Waiting on a reply (APP-158): that it waits and for how
                    // long. Who it waits on is in the editor; the card shows
                    // no people.
                    Text {
                        objectName: "tc-waiting"
                        visible: card._waiting !== undefined && card.detailsOpen
                        text: card._waiting ? I18n.t("waiting.chip.card").arg(card._waiting.days) : ""
                        textFormat: Text.PlainText
                        color: Theme.warning
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                    }
                    // How many files are attached. Opening them is the editor's job.
                    Text {
                        objectName: "tc-attachments"
                        visible: card._attachmentCount > 0 && card.detailsOpen
                        text: "📎 " + card._attachmentCount
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                        Accessible.name: I18n.t("att.card.count").arg(card._attachmentCount)
                    }
                    // The request under the cursor, in quiet words (dogfood
                    // 2026-10-20 item 9): "!481", how CI is doing, whether it
                    // can be merged, who it waits on. No colour: the dots
                    // at rest carry that.
                    Text {
                        id: prT
                        objectName: "tc-pr"
                        readonly property string prState: card.task ? String(card.task.prState || "") : ""
                        readonly property string ref: {
                            const r = String(card._mr.ref || "");
                            if (r.length > 0) return r;
                            const n = card.task ? (card.task.prNumber || 0) : 0;
                            return n > 0 ? "#" + n : "";
                        }
                        readonly property string state: String(card._mr.state || prT.prState)
                        // "!481" once: the key line already says it on an
                        // MR card; then only merged / closed is left to say.
                        readonly property bool refShown: prT.ref.length > 0 && keyT.text === prT.ref
                        readonly property string stateText: prT.state === "merged" || prT.state === "closed"
                                                            ? I18n.t("mr.state." + prT.state) : ""
                        visible: card.detailsOpen && text.length > 0
                        text: prT.ref.length === 0 ? ""
                            : prT.refShown ? prT.stateText
                            : prT.ref + (prT.stateText.length > 0 ? " · " + prT.stateText : "")
                        textFormat: Text.PlainText
                        color: Theme.textDim
                        font.family: Theme.fontMono
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                        // On a Jira card the request was folded in from
                        // another tracker (APP-293): the line says so on
                        // hover, and where to keep them apart (0.9.4).
                        readonly property bool folded: card._ticket.provider === "jira"
                        HoverHandler { id: prHover; enabled: prT.folded }
                        ToolTip.visible: prT.folded && prHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: I18n.t("foldhint.tip").arg(String(card.task ? card.task.prUrl || "" : "").indexOf("github") >= 0 ? "GitHub" : "GitLab")
                    }
                    // CI as a chip (dogfood 2026-10-22): "✕ CI · test-linux",
                    // "CI ✓", "CI идёт 4/7 · старый пуш". Grey words; the only
                    // colour is the red ✕ of a failure. A click (or the
                    // board's key) opens what failed and the log's end.
                    Rectangle {
                        id: ciChip
                        objectName: "tc-ci-chip"
                        visible: card.detailsOpen && ciT.text.length > 0
                        width: ciRow.implicitWidth + 2 * Theme.spXs
                        height: ciRow.implicitHeight + Theme.sp2xs
                        radius: Theme.radiusSm
                        color: "transparent"
                        border.width: 1
                        border.color: ciCA.hovered || ciCA.keyboardFocused ? Theme.borderStrong : Theme.border
                        Row {
                            id: ciRow
                            anchors.centerIn: parent
                            spacing: Theme.sp2xs
                            Text {
                                objectName: "tc-ci-mark"
                                visible: card._ciDot === "failed"
                                text: "✕"
                                color: Theme.danger
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                                Accessible.ignored: true
                            }
                            Text {
                                id: ciT
                                objectName: "tc-ci"
                                text: card._ciChipText
                                textFormat: Text.PlainText
                                color: Theme.textMuted
                                font.family: Theme.fontUi
                                font.features: Theme.tabularNums
                                font.pixelSize: Theme.fsXs
                            }
                        }
                        ClickArea {
                            objectName: "tc-ci-open"
                            id: ciCA
                            label: card._ciDot.length > 0 ? I18n.t(card._ciDot === "canceled" ? "ci.state.canceled" : "mr.ci." + card._ciDot) : ""
                            tip: I18n.t("ci.chip.tip").arg((AppController.shortcuts, AppController.shortcutText("board.ciPopover")))
                            activeFocusOnTab: false
                            onActivated: card.openCi()
                        }
                    }
                    // Can it be merged, in words. "Waits for review" goes
                    // without saying once the names below say whom.
                    Text {
                        objectName: "tc-mr-merge"
                        visible: card.detailsOpen && text.length > 0
                        text: mrDots.merge === "waiting" && !card._mr.draft && mrWho.visible && card._mrWaiting.length > 0
                              ? "" : mrDots.mergeText
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    // Who the request waits on, and for how long; else who
                    // approved it. A fact read off the request, under the
                    // "show whose move" setting like the move itself.
                    Text {
                        id: mrWho
                        objectName: "tc-mr-who"
                        visible: card.detailsOpen && AppController.showWhoseMove && text.length > 0
                        text: card._mrWaiting.length > 0
                              ? I18n.t("mr.waiting").arg(card._mrWaiting.join(", "))
                                + (card._mrDays > 0 ? " · " + I18n.count(card._mrDays, "mr.days") : "")
                              : card._mrApproved.length > 0 ? I18n.t("mr.approved").arg(card._mrApproved.join(", ")) : ""
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                    }
                    // Threads on the request that wait for me (dogfood
                    // 2026-10-21): "1 тред ждёт тебя · 2 ч". Looked up only
                    // under the cursor, from what was already read.
                    Text {
                        objectName: "tc-mr-threads"
                        readonly property var brief: card.detailsOpen && mrDots.visible
                            ? (AppController.mrThreadsRevision, AppController.mrThreadsBrief(card.taskId)) : ({})
                        visible: (brief.count || 0) > 0
                        text: visible ? I18n.count(brief.count, "waits.card")
                                        + (String(brief.ago || "").length > 0 ? " · " + brief.ago : "") : ""
                        textFormat: Text.PlainText
                        // Grey, as the rest of the card's lines (dogfood
                        // 2026-10-22): the only colours are a failed CI's ✕
                        // and an overdue date. A thread set aside is not
                        // counted (AppController.mrThreadsBrief).
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                    }
                    // "P — в «Кому написать»": the key that puts them on the
                    // list (task.pingReviewers), whichever it is bound to.
                    Text {
                        objectName: "tc-mr-ping-hint"
                        readonly property string key: card._mrWaiting.length > 0
                            ? (AppController.shortcuts, AppController.shortcutText("task.pingReviewers")) : ""
                        visible: mrWho.visible && card._mrWaiting.length > 0 && key.length > 0
                        text: I18n.t("mr.pingHint").arg(key)
                        textFormat: Text.PlainText
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    // Whose move it is on that PR (APP-156): read off the PR — a
                    // review asked of me, red CI, an approval — and only ever
                    // shown, never acted on. "Mine" is the accent; "waiting"
                    // stays dim.
                    Item {
                        id: moveChip
                        objectName: "tc-move"
                        readonly property string move: card.task ? String(card.task.prMove || "") : ""
                        readonly property string reason: card.task ? String(card.task.prMoveReason || "") : ""
                        readonly property bool mine: move === "mine"
                        // "Waiting" says nothing the names do not (item 9).
                        visible: AppController.showWhoseMove && move.length > 0 && prT.prState.length > 0 && card.detailsOpen
                                 && !(moveChip.move === "theirs" && mrWho.visible)
                        implicitWidth: moveT.implicitWidth
                        implicitHeight: moveT.implicitHeight
                        Text {
                            id: moveT
                            objectName: "tc-move-text"
                            text: moveChip.mine ? I18n.t("taskcard.move.mine") : I18n.t("taskcard.move.theirs")
                            textFormat: Text.PlainText
                            color: moveChip.mine ? Theme.accentStrong : Theme.textDim
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            font.pixelSize: Theme.fsXs
                            font.weight: moveChip.mine ? Theme.fwTitle : Theme.fwBody
                        }
                        readonly property string tip: moveChip.reason.length > 0 ? I18n.t("taskcard.move." + moveChip.reason) : ""
                        QQC.ToolTip.visible: moveHover.hovered && moveChip.tip.length > 0
                        QQC.ToolTip.text: moveChip.tip
                        HoverHandler { id: moveHover }
                        Accessible.role: Accessible.StaticText
                        Accessible.name: moveT.text + (moveChip.tip.length > 0 ? " — " + moveChip.tip : "")
                    }
                    // Time already tracked — click to start again.
                    Text {
                        objectName: "tc-tracked"
                        visible: !card._timing && card._tracked > 0 && card.detailsOpen
                        text: "⧗ " + card._fmtElapsed(card._tracked)
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: if (card.task) AppController.startTaskTimer(card.task.id)
                        }
                    }
                    // Recurrence (HEAP-77).
                    Text {
                        visible: !!(card.task && card.task.recurrence && String(card.task.recurrence).length > 0) && card.detailsOpen
                        text: "↻ " + card._recurLabel(card.task ? card.task.recurrence : "")
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                    }
                    // Comment count. -1 means the provider never said, which is
                    // not the same as "no comments" — the count stays away for
                    // both.
                    Text {
                        objectName: "tc-comments"
                        visible: card._isTicket && (card._ticket.commentCount || 0) > 0 && card.detailsOpen
                        text: "❝ " + (card._ticket.commentCount || 0)
                        textFormat: Text.PlainText
                        color: Theme.textDim
                        font.pixelSize: Theme.fsXs
                    }
                    // Labels (HEAP-124): a dot in the colour the tracker gave it
                    // and the name in muted text. Two fit; the rest are counted.
                    Repeater {
                        id: labelRep
                        model: card.detailsOpen && card.task && card.task.labels ? card.task.labels.slice(0, 2) : []
                        delegate: Row {
                            id: labelRow
                            required property var modelData
                            spacing: Theme.spXs
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 6; height: 6; radius: 3
                                color: labelRow.modelData.color || Theme.textDim
                            }
                            Text {
                                text: labelRow.modelData.id
                                textFormat: Text.PlainText
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsXs
                                // Half the facts row at most.
                                width: Math.min(implicitWidth, labelRow.parent ? labelRow.parent.width * 0.5 : implicitWidth)
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Text {
                        readonly property int more: card.detailsOpen && card.task && card.task.labels ? card.task.labels.length - 2 : 0
                        visible: more > 0
                        text: "+" + more
                        color: Theme.textDim
                        font.pixelSize: Theme.fsXs
                        QQC.ToolTip.visible: moreHover.hovered
                        QQC.ToolTip.text: card.task && card.task.labels
                            ? card.task.labels.slice(2).map(l => l.id).join(", ") : ""
                        HoverHandler { id: moreHover }
                    }
                }
            }
        }
        // Time spent against the estimate (item 19): "7ч 30м / 10ч",
        // monochrome; over it the warning colour and "+4ч". At rest, as
        // the sheet draws it; nothing without an estimate or time spent.
        Text {
            objectName: "tc-effort"
            readonly property var effort: Effort.ofCard(card.task)
            Layout.fillWidth: true
            visible: effort.shown
            text: effort.text
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: effort.over ? Theme.warning : Theme.textMuted
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsXs
        }
    }

    // "+N" badge shown while dragging a selected card with siblings.
    Rectangle {
        visible: dragArea.drag.active && card._selected && AppController.selectionCount > 1
        anchors.top: parent.top; anchors.right: parent.right
        anchors.margins: -6
        z: 5
        width: bulkT.implicitWidth + 10
        height: 18
        radius: 9
        color: Theme.accent
        border.color: Theme.accentStrong
        border.width: 1
        Text {
            id: bulkT
            anchors.centerIn: parent
            text: "+" + (AppController.selectionCount - 1)
            color: Theme.textOnAccent
            font.family: Theme.fontUi
            font.features: Theme.tabularNums
            font.pixelSize: Theme.fsXs
            font.weight: Theme.fwTitle
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onContainsMouseChanged: {
            if (containsMouse) hoverRest.restart();
            else { hoverRest.stop(); card._hoverRested = false; }
        }
    }
    Timer {
        id: hoverRest
        interval: 450
        onTriggered: card._hoverRested = hoverArea.containsMouse
    }

    // Files dragged in from a file manager attach to this task — one undo
    // step for the whole drop. Only external drags carry text/uri-list, so a
    // card being moved around the board passes straight through.
    readonly property int _attachmentCount: card.task ? (card.task.attachmentCount || 0) : 0
    property bool _fileOver: false
    function attachDroppedFiles(urls) {
        if (!card.taskId || !urls || urls.length === 0) return 0;
        const list = [];
        for (let i = 0; i < urls.length; ++i) list.push(urls[i]);
        return AppController.attachFilesToTask(card.taskId, list);
    }
    DropArea {
        objectName: "tc-file-drop"
        anchors.fill: parent
        keys: ["text/uri-list"]
        onEntered: (drag) => { drag.accepted = drag.hasUrls; card._fileOver = drag.accepted; }
        onExited: card._fileOver = false
        onDropped: (drop) => {
            card._fileOver = false;
            if (drop.hasUrls && card.attachDroppedFiles(drop.urls) > 0) drop.accept(Qt.CopyAction);
        }
    }
    Rectangle {
        visible: card._fileOver
        anchors.fill: parent
        radius: Theme.radiusMd
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
        z: 6
    }
    MouseArea {
        id: dragArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        drag.target: card
        drag.threshold: 5
        // A card the tracker is read-only for does not offer the hand: its
        // status cannot move while heap writes to that tracker (APP-204).
        cursorShape: card._statusLocked ? Qt.ArrowCursor
                   : dragArea.drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property bool didDrag: false
        onPressed: (mouse) => {
            card.homeX = card.x; card.homeY = card.y; didDrag = false;
            if (mouse.button === Qt.RightButton) card.contextMenu().popup();
        }
        onPositionChanged: {
            if (!drag.active) return;
            didDrag = true;
            card._dropAt = card.mapToItem(null, 0, 0);
        }
        // cancelDrag() takes the grab away: no drop, the card goes home.
        onCanceled: {
            card.x = card.homeX; card.y = card.homeY;
            didDrag = false;
        }
        onReleased: (mouse) => {
            const wasDrag = didDrag;
            card.Drag.drop();
            card.x = card.homeX; card.y = card.homeY;
            didDrag = false;
            if (wasDrag || mouse.button !== Qt.LeftButton) return;

            const ctrl = (mouse.modifiers & Qt.ControlModifier) !== 0;
            const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0;
            if (ctrl) {
                AppController.toggleTaskSelection(card.taskId);
            } else if (shift) {
                card.rangeSelectRequested(card.taskId);
            } else if (card._onCiChip(mouse.x, mouse.y)) {
                // The chip sits under this area: its click is read here.
                card.openCi();
            } else {
                if (AppController.selectionCount > 0) AppController.clearSelection();
                card.clicked();
            }
        }
    }

    // Esc mid-drag, or a popup taking the keyboard, lets go of the card
    // without dropping it, as the calendar's drag does: it landed in the
    // column under the pointer on release, behind the palette too
    // (IDIOT-TASKS-8). True when there was a drag to cancel.
    function cancelDrag() {
        if (!dragArea.drag.active) return false;
        card._dropAt = null;
        dragArea.enabled = false;
        Qt.callLater(function () { dragArea.enabled = true; });
        return true;
    }

    // One-time coach marks about this card (CoachMark.qml): the first card a
    // review was folded into, the first tracker card moved only here, and the
    // first one changed on both sides. Made only for that one card.
    readonly property bool _coachMine: card.visible && !!card.task
                                       && String(AppController.coachMark.taskId || "") === String(card.task.id || "")
    CoachMarkHost {
        markId: "reviewFold"
        when: card._coachMine
        sourceComponent: ReviewFoldCoachmark {
            anchor: card
            hint: AppController.coachMark
        }
    }
    CoachMarkHost {
        markId: "trackerMove"
        when: card._coachMine
        sourceComponent: TrackerMoveCoachmark {
            anchor: card
            hint: AppController.coachMark
        }
    }
    CoachMarkHost {
        markId: "conflict"
        when: card._coachMine && markT.visible
        sourceComponent: ConflictCoachmark {
            anchor: markT
            hint: AppController.coachMark
            onCompare: card.openConflictDialog()
        }
    }

    // The task menu, shared by every view (APP-268): TaskMenuHost builds
    // it, its status and priority lists on first use, parented to the card.
    TaskMenuHost {
        id: menuHost
        taskId: card.taskId
        task: card.task
        anchorItem: card
        boardKeys: card.boardKeys
        onOpenRequested: card.clicked()
        onStatusPicked: (sid) => card.statusPicked(sid)
    }
    readonly property var _menu: menuHost.menu
    readonly property var _statusMenu: menuHost.statusList
    readonly property var _priorityMenu: menuHost.priorityList
    function contextMenu() { return menuHost.contextMenu(); }
    function releaseMenu() { menuHost.releaseMenu(); }
    function openSubMenu(which) { menuHost.openSubMenu(which); }
    function backToMenu(which) { menuHost.backToMenu(which); }
    function statusMenu() { return menuHost.statusMenu(); }
    function priorityMenu() { return menuHost.priorityMenu(); }
}
