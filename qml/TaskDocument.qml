pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as QQC
import TodoCpp
import "TaskDates.js" as TaskDates
import "Effort.js" as Effort

// The task as a document (heap 2, APP-265; sheets H2-Task / Q-Task), in
// place of the modal editor. Opens as a panel on the right of the view (the
// sheets' "панель справа", the same as a meeting's); Enter or the expand
// icon makes it replace the whole content area, header included, with the
// breadcrumb "Задачи / В работе / APP-101" on top (DG-060). Esc goes back to
// the same task in the view. The title is a heading, the properties a row of
// chips, the body markdown edited where it is drawn, with the plan (my
// checklist) under it. Everything saves itself; Ctrl+Z undoes. The meta
// column: Код, Упоминается в, Время, История and Готово (d).
FocusScope {
    id: root
    objectName: "task-doc"

    // The open task's id; empty = closed.
    property string taskId: ""
    property bool full: false
    // How far a toast rises so it does not sit on the bottom Done (A2-34).
    readonly property real toastLift: root.opened && doneBottom.visible ? doneBottom.height + Theme.sp2xl : 0
    readonly property bool opened: root.taskId.length > 0
    visible: root.opened
    // Slides in from the right edge (APP-175).
    transform: [arrival.shift]
    PanelArrival { id: arrival; target: root; shown: root.opened }
    onFullChanged: arrival.replay()

    // Where to put the keyboard when it closes (the view asks back).
    signal closed(string taskId)
    signal internalLinkActivated(string kind, string target)

    // The quiet look (Q-Task): outline lowercase chips, plain meta lines, a
    // small outline Готово under История (DG-065).
    readonly property bool _quiet: !Style.fills

    // The task as the model has it now. Re-read on every change to the
    // tasks, so a pull or an undo shows here at once.
    property int _rev: 0
    Connections {
        target: AppController.tasks
        function onDataChanged() { root._rev++; }
        function onRowsRemoved() { root._rev++; }
        function onRowsInserted() { root._rev++; }
        function onLayoutChanged() { root._rev++; }
        function onModelReset() { root._rev++; }
    }
    Connections {
        target: AppController
        function onTaskHistoryChanged(taskId) { if (taskId === root.taskId) root._rev++; }
        // A picture of the ticket's text arrived (dogfood 2026-10-20).
        function onTrackerImagesChanged() { if (root._isTicket) root._rev++; }
    }
    readonly property var task: root._rev >= 0 && root.taskId.length > 0 ? AppController.taskById(root.taskId) : ({})
    readonly property bool _exists: !!(root.task && root.task.id)
    readonly property var _ticket: root._exists && root.task.ticket ? root.task.ticket : ({})
    readonly property bool _isTicket: !!root._ticket.provider
    readonly property var _badge: root._isTicket ? (AppController.providerBadges[root._ticket.provider] || ({})) : ({})
    // The tracker's text as shown: Jira Server wiki markup as markdown when
    // that is on (APP-313); the stored text stays as the tracker sent it.
    // A fence right under a line of text (Jira writes them so) drew its
    // header over that line (APP-314): a blank line around each fence, for
    // the read-only tracker text only; nothing is written back.
    function _spacedFences(t) {
        const lines = String(t || "").split("\n");
        const out = [];
        let inFence = false;
        for (let i = 0; i < lines.length; i++) {
            const fence = /^\s*(```|~~~)/.test(lines[i]);
            if (fence && !inFence && out.length > 0 && out[out.length - 1].trim().length > 0) out.push("");
            out.push(lines[i]);
            if (fence) {
                inFence = !inFence;
                if (!inFence && i + 1 < lines.length && lines[i + 1].trim().length > 0) out.push("");
            }
        }
        return out.join("\n");
    }
    readonly property string _trackerText: root._isTicket && root._rev >= 0 ? AppController.trackerDescription(root.taskId) : ""
    // A field changed both here and in the tracker since the last sync: the
    // tracker's side of each, as the editor shows it (A3-8). The document
    // said nothing; only the card carried a small "решить".
    readonly property bool _hasConflict: root.hasConflictOf(root.task)
    readonly property var _conflictRows: root.conflictRowsOf(root.task)
    function hasConflictOf(task) {
        return !!(task && task.ticket && task.ticket.provider && task.ticket.conflict);
    }
    function conflictRowsOf(task) {
        if (!root.hasConflictOf(task)) return [];
        const t = task.ticket;
        const fields = t.conflicts || [];
        const out = [];
        for (let i = 0; i < fields.length; ++i) {
            const f = fields[i];
            if (f === "title") out.push({ label: I18n.t("ticket.conflict.title"), value: String(t.remoteTitle || "") });
            else if (f === "body") out.push({ label: I18n.t("ticket.conflict.body"), value: String(t.remoteBody || "") || "—" });
            else if (f === "priority") out.push({ label: I18n.t("ticket.conflict.priority"), value: String(t.remotePriority || "") });
            else if (f === "status")
                out.push({ label: I18n.t("ticket.conflict.status"),
                           value: root._statusName(String(t.remoteColumn || "")) + (t.remoteStatus ? " (" + t.remoteStatus + ")" : "") });
        }
        return out;
    }
    // Keep mine, or take the tracker's: what is typed is saved first, then
    // the fields show the outcome.
    function resolveConflict(useTracker) {
        if (!root._hasConflict) return;
        // The buttons hide once it is resolved; focus left on them was
        // stranded on an invisible control.
        const fromButton = conflictUseTracker.activeFocus || conflictKeepMine.activeFocus;
        root.flush();
        AppController.resolveTrackerConflict(root.taskId, useTracker);
        root._load();
        if (fromButton) titleField.forceActiveFocus();
    }

    // ── editing what came from the tracker (owner item 16) ──
    // Off by default per tracker (Settings → Integrations). On, the
    // description, the Smart Checklist, the title, the tracker's due date and
    // the assignee are edited here and written to the tracker; the controller
    // shows the edit on the card at once and takes it back when the tracker
    // does not take it, so nothing here applies a value itself. Every call
    // answers trackerEditFinished once.
    // Where the calls go: AppController, or a stand-in in a test.
    property var trackerApi: AppController
    readonly property alias trackerDuePicker: trackerDuePicker
    readonly property alias assigneePicker: assigneePicker
    readonly property string _provider: root._isTicket ? String(root._ticket.provider || "") : ""
    readonly property string _trackerName: String(root._badge.name || root._provider || "")
    // A switched-off tracker (owner item 7): its cards are read only.
    readonly property bool _trackerOff: root._isTicket && (root.trackerApi.disabledTrackers || []).indexOf(root._provider) >= 0
    readonly property bool _canEdit: root._isTicket && !root._trackerOff && root._rev >= 0
                                     && AppController.appSettingsJson.length >= 0
                                     && root.trackerApi.canEditTrackerContent(root.taskId)
    // Writing could be switched on for this tracker: the quiet hint.
    readonly property bool _editHint: root._isTicket && !root._trackerOff && !root._canEdit
                                      && AppController.appSettingsJson.length >= 0
                                      && root.trackerApi.trackerCanEditContent(root._provider)
                                      && !root.trackerApi.trackerContentWriteEnabled(root._provider)
    readonly property var _clStatuses: root._canEdit && root._rev >= 0 ? root.trackerApi.smartChecklistStatuses(root.taskId) : []

    property bool descEditing: false
    // What was sent last for the description: back in the editor when the
    // tracker did not take it, so nothing typed is lost.
    property string _descDraft: ""
    property bool _titleEdited: false
    // Fields sent and not answered yet: {description: true, …}.
    property var _pending: ({})
    // The last answer, said under the chips: {kind: ok|bad, text}.
    property var _editNote: ({})
    // A conflict to settle: {field, mine, theirs}.
    property var _conflict: null
    Timer { id: editNoteTimer; interval: 4000; onTriggered: root._editNote = ({}) }

    function _fieldLabel(field) {
        return I18n.t("taskdoc.edit.field." + (["description", "title", "due", "assignee", "checklist"].indexOf(field) >= 0 ? field : "description"));
    }
    function _setPending(field, on) {
        const p = Object.assign({}, root._pending);
        if (on) p[field] = true; else delete p[field];
        root._pending = p;
    }
    function _checklistText(items) {
        const out = [];
        for (const it of (items || [])) {
            if (!it || String(it.text || "").length === 0) continue;
            out.push(it.state === "header" ? String(it.text) : smartList.labelOf(it) + " · " + String(it.text));
        }
        return out.join("\n");
    }
    function _resetEdits() {
        root.descEditing = false;
        root._descDraft = "";
        root._titleEdited = false;
        root._pending = ({});
        root._editNote = ({});
        root._conflict = null;
    }

    // The description: "Править e" opens it in the notes' editor.
    function startDescEdit() {
        if (!root._canEdit) return false;
        if (root.descEditing) { descEditor.focusEditor(); return true; }
        root.openBlock("desc");
        root.contentTab = "desc";
        root._conflict = null;
        const text = root._descDraft.length > 0 ? root._descDraft : String(root.trackerApi.trackerDescriptionSource(root.taskId) || "");
        root.descEditing = true;
        descEditor.load(text);
        Qt.callLater(descEditor.focusEditor);
        return true;
    }
    function saveDescEdit() {
        if (!root.descEditing) return;
        descEditor.flush();
        const text = descEditor.source.text;
        root.descEditing = false;
        root._descDraft = text;
        root._setPending("description", true);
        root.trackerApi.editTrackerDescription(root.taskId, text);
        Qt.callLater(trackerHead.focusHead);
    }
    function cancelDescEdit() {
        if (!root.descEditing) return;
        root.descEditing = false;
        root._descDraft = "";
        Qt.callLater(trackerHead.focusHead);
    }
    function commitTitle() {
        if (!root._titleEdited) return;
        root._titleEdited = false;
        const title = root._cleanTitle(titleField.text);
        if (title.length === 0 || title === root.task.title) {
            root._loading = true; titleField.text = root.task.title; root._loading = false;
            return;
        }
        root._setPending("title", true);
        root.trackerApi.editTrackerField(root.taskId, "title", title);
    }
    function revertTitle() {
        root._titleEdited = false;
        root._loading = true; titleField.text = root.task.title; root._loading = false;
    }
    function editTrackerDue(iso) {
        root._setPending("due", true);
        root.trackerApi.editTrackerField(root.taskId, "due", iso);
    }
    function editTrackerAssignee(who) {
        root._setPending("assignee", true);
        root.trackerApi.editTrackerField(root.taskId, "assignee", who);
    }
    function setChecklistStatus(index, name) {
        root._setPending("checklist", true);
        root.trackerApi.setTrackerChecklistItemStatus(root.taskId, index, name);
    }
    function setChecklistText(index, text) {
        root._setPending("checklist", true);
        root.trackerApi.setTrackerChecklistItemText(root.taskId, index, text);
    }
    function addChecklistItem(text) {
        root._setPending("checklist", true);
        root.trackerApi.addTrackerChecklistItem(root.taskId, text);
    }
    function keepMyEdit() {
        if (!root._conflict) return;
        const field = root._conflict.field;
        root._conflict = null;
        root._setPending(field, true);
        root.trackerApi.overwriteTrackerEdit(root.taskId, field);
        root.focusDocument();
    }
    function takeTrackerEdit() {
        if (!root._conflict) return;
        const field = root._conflict.field;
        root._conflict = null;
        if (field === "description") root._descDraft = "";
        root.trackerApi.discardTrackerEdit(root.taskId, field);
        root.focusDocument();
    }
    function dismissConflict() {
        if (!root._conflict) return;
        const field = root._conflict.field;
        root._conflict = null;
        root.trackerApi.discardTrackerEdit(root.taskId, field);
        root.focusDocument();
    }
    Connections {
        target: root.trackerApi
        ignoreUnknownSignals: true
        function onTrackerEditFinished(taskId, field, outcome, message, detail) {
            if (taskId !== root.taskId) return;
            root._setPending(field, false);
            if (outcome === "ok" || outcome === "unchanged") {
                if (field === "description") root._descDraft = "";
                if (outcome === "ok") {
                    root._editNote = { kind: "ok", text: I18n.t("taskdoc.edit.saved").arg(root._trackerName) };
                    editNoteTimer.restart();
                }
                return;
            }
            if (outcome === "conflict") {
                const d = detail || {};
                const list = field === "checklist";
                root._conflict = {
                    field: field,
                    mine: list ? root._checklistText(d.mine) : String(d.mine ?? ""),
                    theirs: list ? root._checklistText(d.theirs) : String(d.theirs ?? "")
                };
                editNoteTimer.stop();
                root._editNote = ({});
                Qt.callLater(() => { if (root._conflict) conflictPanel.focusFirst(); });
                return;
            }
            // Refused, failed, or not sent at all: said here, and the
            // description's text back in the editor.
            editNoteTimer.stop();
            root._editNote = { kind: "bad", text: String(message || "").length > 0 ? message
                                                  : I18n.t("taskdoc.edit.notSent").arg(root._trackerName) };
            if (field === "description" && root._descDraft.length > 0 && root._canEdit) {
                root.descEditing = true;
                descEditor.load(root._descDraft);
            }
        }
    }

    // ── opening and closing ──
    function open(id, intoTitle) {
        if (root.opened && id !== root.taskId) root.flush();
        if (id !== root.taskId) root._resetEdits();
        root._loadedId = "";
        root._profileId = AppController.activeProfileId;
        root.taskId = id;
        root._load();
        // From the top, the description folded again (it kept the place it
        // was scrolled to, 0.9.4), on the first tab.
        root.descExpanded = false;
        root.contentTab = "desc";
        docScroll.contentY = 0;
        // My notes wait with the caret at their end (dogfood 2026-10-16).
        body.caretToEnd();
        if (intoTitle === true) root.focusTitle();
        else root.takeFocus();
        // The first tracker text opened says whose it is, once (coach mark).
        if (root._isTicket && root._trackerText.length > 0)
            AppController.offerCoachMark("trackerText", { taskId: id, tracker: String(root._badge.name || root._ticket.provider || "") });
    }
    // Where the keyboard goes in the document: a task with no title yet into
    // its title; any other onto the document, where its keys work at once —
    // d is Done, / the insert menu, i the title, Esc back. Opened into the
    // title, "/" from the hint and "d" from the button were typed into it
    // (IDIOT-DOC-10, the owner's "/" report).
    function takeFocus() {
        if (String(titleField.text).trim().length === 0) root.focusTitle();
        else root.focusDocument();
    }
    // The document itself, not whatever field inside it had focus last: a
    // FocusScope hands the keyboard back to that child, a hidden chip field
    // included (IDIOT-DOC-6). The sink sits in the scope; the keys it takes
    // travel up to the document's own handlers.
    function focusDocument() {
        docFocus.forceActiveFocus();
    }
    Item {
        id: docFocus
        objectName: "task-doc-focus"
        width: 0
        height: 0
    }
    function focusTitle() {
        titleField.forceActiveFocus();
        titleField.cursorPosition = titleField.length;
    }
    // "/" on the document: a new line at the end and the insert menu on it,
    // as the hint under the text says.
    function slashInsert() {
        if (root._isTicket && body.readOnly) return;
        body.appendBlock();
        Qt.callLater(body.openSlashMenu);
    }
    // A linked card, or a checklist item that became one: opened here.
    function openOther(id) {
        if (id && id.length > 0 && AppController.taskById(id).id) root.open(id);
    }
    function close() {
        if (!root.opened) return;
        root.flush();
        const id = root.taskId;
        root.taskId = "";
        root.full = false;
        root.closed(id);
    }
    // The body's scroll, kept over a trip to Settings (dogfood 2026-10-22 #4).
    function scrollPos() { return docScroll.contentY; }
    function setScrollPos(y) { root._scrollTo(y); }
    // Write what is typed now (before a switch, a quit, a close).
    function flush() {
        if (root._draft) root._draft.flush();
        root.commitTitle();
        body.flush();
        saveTimer.stop();
        if (root._dirtyTitle || root._dirtyBody) root._save();
    }
    // The plan (my checklist) and the links, asked for from "+ свойство".
    function startPlan() { if (root._plan) root._plan.startAdding(); }
    function startLink(kind) { relations.startAdding(kind); }

    // A task deleted while open (another view, a pull): close, and say so.
    // Checked a moment later: a model mid-update can lack the row for an
    // instant (a reset), and that is not a deletion.
    on_ExistsChanged: if (root.opened && !root._exists && root._loadedId.length > 0) Qt.callLater(root._closeIfGone)
    function _closeIfGone() {
        if (!root.opened) return;
        const t = AppController.taskById(root.taskId);
        if (t && t.id) { root._rev++; return; }
        root.taskId = "";
        root.full = false;
        root.closed("");
    }

    // ── loading and saving ──
    property string _loadedId: ""
    // The profile the task was opened in.
    property string _profileId: ""
    property bool _loading: false
    property bool _dirtyTitle: false
    property bool _dirtyBody: false
    function _load() {
        if (!root._exists) return;
        root._loading = true;
        root._passingThrough = false;
        titleField.text = root.task.title;
        body.load(root._isTicket ? AppController.taskLocalNotes(root.taskId) : String(root.task.desc || ""));
        root._loadedId = root.taskId;
        root._dirtyTitle = false;
        root._dirtyBody = false;
        root._loading = false;
    }
    // The model changed under us (a pull, an undo): take it in unless the
    // person is typing in that very field.
    on_RevChanged: {
        if (!root._exists || root._loadedId !== root.taskId) return;
        if (!titleField.activeFocus && !root._dirtyTitle && !root._titleEdited && titleField.text !== root.task.title) {
            root._loading = true; titleField.text = root.task.title; root._loading = false;
        }
        const fresh = root._isTicket ? AppController.taskLocalNotes(root.taskId) : String(root.task.desc || "");
        if (!body.editing && !root._dirtyBody && body.source.text !== fresh) {
            root._loading = true; body.load(fresh); root._loading = false;
        }
        root._changedUnder = body.editing && body.source.text !== fresh && !root._dirtyBody;
    }
    property bool _changedUnder: false
    property real _doneClickAt: 0
    function _save() {
        if (!root._exists) return;
        saveTimer.stop();
        if (root._dirtyBody && root._isTicket) {
            AppController.setTaskLocalNotes(root.taskId, body.source.text);
            root._dirtyBody = false;
        }
        if (root._dirtyTitle || root._dirtyBody) {
            const d = Object.assign({}, root.task);
            d._originalId = root.taskId;
            // One line, and not a pasted page (IDIOT-DOC-14): line breaks
            // become spaces, the title stops at 500 characters.
            const title = root._cleanTitle(titleField.text);
            // An emptied title is not saved, and stays "not saved": the
            // indicator said "Saved" over an empty field (IDIOT-DOC-15).
            const titleOk = title.length > 0;
            if (!root._dirtyBody && !titleOk) return;
            if (root._dirtyTitle && titleOk) d.title = title;
            if (root._dirtyBody) d.desc = body.source.text;
            if (AppController.saveTask(d)) {
                if (titleOk) root._dirtyTitle = false;
                root._dirtyBody = false;
            }
        }
    }
    Timer { id: saveTimer; interval: 600; onTriggered: root._save() }
    function _cleanTitle(t) { return String(t || "").replace(/\s*\n\s*/g, " ").trim().substring(0, 500); }
    Connections {
        target: Qt.application
        function onAboutToQuit() { root.flush(); }
    }
    // Another profile is about to be opened: what is typed goes into this
    // one's task first (PRES-1), and the document closes with it.
    Connections {
        target: AppController
        function onFlushEditorsRequested() { root.flush(); }
        function onAboutToChangeActiveNote() { if (root.opened) root.flush(); }
        function onActiveProfileChanged() {
            if (!root.opened || root._profileId === AppController.activeProfileId) return;
            root.taskId = "";
            root.full = false;
            root.closed("");
        }
    }

    // ── fields ──
    function _setField(key, value) {
        const d = Object.assign({}, root.task);
        d._originalId = root.taskId;
        d[key] = value;
        AppController.saveTask(d);
    }
    function _statusName(id) {
        const sts = AppController.statuses;
        for (let i = 0; i < sts.length; i++) if (sts[i].id === id) return sts[i].name;
        return id;
    }
    // "Статус" in bold, "статус" in quiet (the sheets' chip keys).
    function _key(s) {
        const t = String(s || "");
        if (t.length === 0) return t;
        return root._quiet ? t.toLowerCase() : t.charAt(0).toUpperCase() + t.slice(1);
    }
    function _valid(d) { return !!(d && d.getTime && !isNaN(d.getTime())); }
    function _dayDiff(d) {
        const a = new Date(d.getFullYear(), d.getMonth(), d.getDate());
        const n = new Date();
        const b = new Date(n.getFullYear(), n.getMonth(), n.getDate());
        return Math.round((a - b) / 86400000);
    }
    // "сегодня", "завтра", "вчера", else "9 окт".
    function _relDay(d) {
        const k = root._dayDiff(d);
        if (k === 0) return I18n.t("common.today").toLowerCase();
        if (k === 1) return I18n.t("common.tomorrow").toLowerCase();
        if (k === -1) return I18n.t("common.yesterday").toLowerCase();
        return I18n.fmtDate(d, "dayMonth");
    }
    // "сегодня 17:00–18:30" (the estimate gives the end), "завтра".
    // "Remind me" (APP-319): "за 15 мин", or the moment, as the chips say it.
    function _remindLabel() {
        const t = root.task;
        if (t.remindLead !== undefined && t.remindLead >= 0) return I18n.t("taskdoc.remind.lead").arg(t.remindLead);
        return root._when(t.remindAt, true, 0);
    }
    function _when(d, hasTime, minutes) {
        if (!root._valid(d)) return "";
        if (!hasTime) return root._relDay(d);
        let s = root._relDay(d) + " " + I18n.fmtTime(d);
        if (minutes > 0) s += "–" + I18n.fmtTime(new Date(d.getTime() + minutes * 60000));
        return s;
    }
    // Which properties show as chips: a set one always, an empty one only
    // when asked for with "+ property".
    property var _shown: []
    function _has(key) {
        const t = root.task;
        if (!root._exists) return false;
        switch (key) {
        case "scheduled": return root._valid(t.scheduledAt);
        case "due": return root._valid(t.dueAt);
        case "labels": return (t.labels || []).length > 0;
        case "recurrence": return String(t.recurrence || "").length > 0;
        case "estimate": return root._effort.shown;
        case "tags": return root._localTags.length > 0;
        case "remind": return root._valid(t.remindAt) || (t.remindLead !== undefined && t.remindLead >= 0);
        }
        return true;
    }
    // My own layer (APP-238/239): both sides of priority and due, my tags.
    // The estimate in the card (item 19): the tracker's figures, read only,
    // and mine on the task. The chip says "spent / estimate"; the estimate is
    // mine, else the tracker's original one; spent is the tracker's logged
    // time plus my timer's.
    readonly property var _trackerEst: root._rev >= 0 && root._isTicket ? AppController.trackerEstimate(root.taskId) : ({})
    readonly property string _estProvider: root._isTicket ? String(root._ticket.provider || "") : ""
    // My timer, the running session included (a minute at a time).
    property int _clockTick: 0
    Timer { interval: 30000; repeat: true; running: !!root.task.isTiming && root.opened; onTriggered: root._clockTick++ }
    readonly property int _timerMin: root._exists && root._clockTick >= 0
        ? Math.round(((root.task.trackedSeconds || 0) + (root.task.isTiming ? AppController.elapsedSecondsFor(root.taskId) : 0)) / 60) : 0
    readonly property int _trackerSpent: root._trackerEst.spentMinutes || 0
    readonly property int _estMinutes: root._exists && root.task.estimateMinutes > 0 ? root.task.estimateMinutes
                                       : (root._trackerEst.originalMinutes || 0)
    readonly property var _effort: Effort.summary(root._trackerSpent + root._timerMin, root._estMinutes, root._estProvider)
    readonly property bool _hasPoints: root._trackerEst.points !== undefined
    // E in the document (Alt+E in Classic), Enter on the chip, the add
    // menu: the popover under the chip, which comes out for it.
    property bool _estChipAdded: false
    readonly property alias estimatePopup: estPopup
    function openEstimate() {
        if (!root.opened || !root._exists) return;
        root._estChipAdded = !root._visible("estimate");
        if (root._estChipAdded) root._shown = root._shown.concat(["estimate"]);
        Qt.callLater(root._openEstimatePopup);
    }
    function _openEstimatePopup() {
        estPopup.localMinutes = root.task.estimateMinutes || 0;
        estPopup.trackerMinutes = root._trackerEst.originalMinutes || 0;
        estPopup.trackerSpent = root._trackerSpent;
        estPopup.timerSpent = root._timerMin;
        estPopup.trackerRemaining = root._trackerEst.remainingMinutes !== undefined ? root._trackerEst.remainingMinutes : -1;
        estPopup.openFor();
    }
    readonly property var _tv: root._rev >= 0 && root._exists ? AppController.trackerValues(root.taskId) : ({})
    readonly property bool _mine: root._isTicket && (String(root._tv.myPriority || "").length > 0 || root._valid(root._tv.myDueAt))
    readonly property var _localTags: root._exists && root.task.localTags ? root.task.localTags : []
    property bool _tagsEditing: false
    function _visible(key) { return root._has(key) || root._shown.indexOf(key) >= 0; }
    onTaskIdChanged: { estPopup.close(); root._shown = []; root._tagsEditing = false; }

    // A date typed into a chip ("tomorrow 15:00", "пт"): read like the input.
    function _applyDate(field, text) {
        const s = String(text || "").trim();
        if (s.length === 0) {
            root._setField(field === "scheduled" ? "scheduledAt" : "dueAt", null);
            return;
        }
        const r = AppController.parseDateTime(s, new Date());
        if (r && r.ok) AppController.rescheduleTask(root.taskId, field, r.start, r.hasTime);
        else AppController.showToast(I18n.t("taskdoc.date.unknown").arg(s));  // it said nothing (IDIOT-DOC-15)
    }
    // Ctrl+Z with the keyboard in the document: the body's own steps first;
    // never the app's step that made this task — that deleted the task
    // under the caret (IDIOT-DOC-17).
    function undo() {
        if (body.source.canUndo) {
            body.source.undo();
            body.flush();
            return;
        }
        if (AppController.undoWouldRemoveTask(root.taskId)) {
            AppController.showToast(I18n.t("taskdoc.undo.wouldDelete"));
            return;
        }
        AppController.undo();
    }
    // A single-letter key reads as the keymap writes it ("d", "t").
    function _keyOf(id) {
        const k = AppController.shortcutText(id);
        return k.length === 1 ? k.toLowerCase() : k;
    }
    readonly property bool _done: AppController.statusCategory(root.task.status || "") === "done"

    // ── history (DG-061): the three newest facts, said shortly ──
    readonly property var _history: root._rev >= 0 && root._exists ? AppController.taskHistory(root.taskId) : []
    function _histWhen(at, withTime) {
        const d = new Date(at);
        if (!root._valid(d)) return "";
        const day = root._relDay(d);
        return withTime && root._dayDiff(d) === 0 ? day + " " + I18n.fmtTime(d) : day;
    }
    function _isoDay(s) {
        const m = /^(\d{4})-(\d{2})-(\d{2})/.exec(String(s || ""));
        return m ? I18n.fmtDate(new Date(+m[1], +m[2] - 1, +m[3]), "dayMonth") : String(s || "—");
    }
    function _histText(e) {
        const from = root._badge.name || root._ticket.provider || "";
        switch (e.kind) {
        case "created":
            if (e.sync && from.length > 0) return I18n.t(root._quiet ? "taskdoc.hist.fromQuiet" : "taskdoc.hist.createdFrom").arg(from);
            return I18n.t("taskdoc.hist.created");
        case "status":
            return root._quiet ? root._statusName(e.to).toLowerCase() : "→ " + root._statusName(e.to);
        case "pushed": return I18n.t("taskdoc.hist.pushed").arg(root._statusName(e.to));
        case "due": return I18n.t("taskdoc.hist.due").arg(String(e.to || "").length > 0 ? root._isoDay(e.to) : "—");
        case "scheduled": return I18n.t("taskdoc.hist.scheduled").arg(String(e.to || "").length > 0 ? root._isoDay(e.to) : "—");
        case "priority": return I18n.t("taskdoc.hist.priority").arg(String(e.to || "—"));
        case "title": return I18n.t("taskdoc.hist.title");
        }
        return String(e.kind);
    }
    readonly property var _historyRows: {
        const out = [];
        const h = root._history || [];
        for (let i = 0; i < h.length && out.length < 3; i++)
            out.push(root._histWhen(h[i].at, !root._quiet && h[i].kind === "status") + " · " + root._histText(h[i]));
        // Nothing recorded yet: the last column change, which is known.
        if (out.length === 0 && root._valid(root.task.statusChangedAt))
            out.push(root._histWhen(root.task.statusChangedAt, !root._quiet) + " · "
                     + (root._quiet ? root._statusName(root.task.status || "").toLowerCase() : "→ " + root._statusName(root.task.status || "")));
        return out;
    }

    // ── code (DG-061): branch, PR + CI, tracker ──
    readonly property int _prNumber: root._exists ? (root.task.prNumber || 0) : 0
    // CI of the request: the reading the card's dot and chip show too
    // (dogfood 2026-10-22). The raw prChecks was whichever of the watcher
    // and the tracker wrote last, and said "failed" beside a green card.
    readonly property var _ci: root._rev >= 0 && root._exists
        ? (AppController.mrRevision, AppController.ciOf(root.taskId)) : ({})
    readonly property string _ciState: String(root._ci.state || "")
    // "CI упал · 2 из 9", "CI идёт 4/7", "CI прошёл · старый пуш".
    readonly property string _ciWords: {
        const c = root._ci, s = root._ciState;
        if (s.length === 0) return "";
        if (s === "failed")
            return I18n.t("ci.state.failed") + ((c.total || 0) > 0 ? " · " + I18n.t("ci.row.of").arg(c.failed || 0).arg(c.total) : "");
        if (s === "running")
            return I18n.t("ci.state.running") + ((c.total || 0) > 0 ? " " + (c.done || 0) + "/" + c.total : "");
        if (s === "unknown" && String(c.staleState || "").length > 0)
            return I18n.t("ci.chip.stale").arg(I18n.t("ci.state." + c.staleState));
        return I18n.t("ci.state." + s);
    }
    readonly property string _ciWhen: [String(root._ci.sha || ""), String(root._ci.ago || "")].filter(x => x.length > 0).join(" · ")
    readonly property string _trackerLabel: root._isTicket
        ? ((root._badge.name || root._ticket.provider || "") + " " + (root.task.externalKey || "")).trim() : ""

    // The plan and the comment draft live in the body's tail (see below).
    readonly property Item _tail: body.tailItem
    readonly property var _plan: root._tail ? (root._tail as DocTail).plan : null
    readonly property var _draft: commentDraft

    Keys.onEscapePressed: root.close()
    // Tab walks the document and comes round inside it: it left for the
    // sidebar behind the scrim, where Esc no longer closed it (VERIFY-A-1).
    Keys.onTabPressed: (e) => { root._tabWithin(true); e.accepted = true; }
    Keys.onBacktabPressed: (e) => { root._tabWithin(false); e.accepted = true; }
    // "каждый месяц, 1-го", "по понедельникам": the rule as it is, its day
    // included (A2-22); the chip used to drop the ":1".
    function _recurLabel(r) {
        const rule = String(r || "");
        if (rule.length === 0) return "";
        // "раз в 2 недели", "по пн, ср" (B7).
        const interval = TaskDates.intervalText(rule);
        if (interval.length > 0) return interval;
        const monthly = /^every:month:(\d+)$/.exec(rule);
        if (monthly) {
            const l = I18n.t("editor.recur.monthlyOn").arg(monthly[1]);
            return l.charAt(0).toLowerCase() + l.slice(1);
        }
        const key = "taskdoc.repeat." + rule.replace("every:", "");
        const t = I18n.t(key);
        return t === key ? rule : t;
    }
    // The menu's rules: the five plain ones, the weekday and the day of the
    // month the task's own date gives, and the current rule when it is none
    // of these — so it is marked and picking it keeps it (A2-22).
    readonly property var _recurOptions: {
        const out = ["", "every:day", "every:weekday", "every:week", "every:2:week", "every:month"];
        const at = root._valid(root.task.scheduledAt) ? new Date(root.task.scheduledAt)
                 : root._valid(root.task.dueAt) ? new Date(root.task.dueAt) : new Date();
        const dows = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"];
        out.push("every:" + dows[at.getDay()]);
        out.push("every:month:" + at.getDate());
        const cur = String(root.task.recurrence || "");
        if (cur.length > 0 && out.indexOf(cur) < 0) out.push(cur);
        // Any other interval, typed (B7): "every 3 weeks", "пн, ср".
        out.push("custom");
        return out;
    }
    // The "Custom interval…" row's field: the words read by the same parser
    // as quick capture; what is not a repeat stays in the field, marked.
    property bool _recurEditing: false
    property bool _recurBad: false
    // The chip the row brought out for the field, taken back when nothing
    // is set ("Repeat" with no rule under it).
    property bool _recurChipAdded: false
    function startCustomRecur() {
        root._recurChipAdded = root._shown.indexOf("recurrence") < 0;
        root._shown = root._shown.concat(["recurrence"]);
        root._recurBad = false;
        root._recurEditing = true;
        recurField.text = "";
        Qt.callLater(recurField.forceActiveFocus);
    }
    function applyCustomRecur(text) {
        const rule = AppController.recurrenceFromText(text);
        if (rule.length === 0) { root._recurBad = true; return false; }
        root._recurEditing = false;
        root._recurBad = false;
        root._setField("recurrence", rule);
        root.focusDocument();
        return true;
    }
    function cancelCustomRecur() {
        root._recurEditing = false;
        if (root._recurChipAdded && !root._has("recurrence"))
            root._shown = root._shown.filter((k) => k !== "recurrence");
        root._recurChipAdded = false;
    }
    // The body is one Tab stop: its open block, the drawn text, a link drawn
    // in it — not my plan under it, which lives in the same scroll.
    function _inBody(it) {
        const tail = root._tail;
        for (let p = it; p; p = p.parent) {
            if (tail && p === tail) return false;
            if (p === body) return true;
        }
        return false;
    }
    // The visible Tab stops under an item, in the order they are drawn.
    function _stopsIn(item) {
        if (!item || !item.visible) return [];
        let out = item.activeFocusOnTab === true && item.enabled ? [item] : [];
        const kids = item.children || [];
        for (let i = 0; i < kids.length; i++) out = out.concat(root._stopsIn(kids[i]));
        return out;
    }
    // How many lines the title takes at its full size, measured apart from
    // the field so the size it picks does not feed back into it.
    TextMetrics {
        id: titleMetrics
        font.family: Theme.fontUi
        font.pixelSize: Theme.typeStep(root._quiet ? 7 : 8)
        font.weight: root._quiet ? Theme.fwTitle : Theme.fwHeading
        text: titleField.text
    }
    readonly property int _titleStepsDown: {
        const w = titleField.width;
        if (w <= 0) return 0;
        // A word wrapped whole leaves the end of each line short: a fifth.
        const lines = Math.ceil(titleMetrics.advanceWidth * 1.2 / w);
        return lines > 6 ? 3 : (lines > 3 ? 2 : (lines > 2 ? 1 : 0));
    }
    // Where Tab in the text indents rather than moves on: lines selected, a
    // list item, inside a ``` fence; Shift+Tab only where there is an indent
    // to take back. Not while the keyboard is only passing through: Tab from
    // the chips into a text that opens on "- item" indented it.
    property bool _passingThrough: false
    function _tabIndents(field, forward) {
        if (root._passingThrough) return false;
        const t = String(field.text);
        const a = field.selectionStart, b = field.selectionEnd;
        if (b > a && t.substring(a, b).indexOf("\n") >= 0) return true;
        const caret = field.cursorPosition;
        const start = t.lastIndexOf("\n", caret - 1) + 1;
        let end = t.indexOf("\n", caret);
        if (end < 0) end = t.length;
        const line = t.substring(start, end);
        if (!forward && !/^\s/.test(line)) return false;
        if (/^\s*([-*+]|\d+[.)])\s/.test(line)) return true;
        const fences = t.substring(0, start).split("\n").filter(l => /^\s*```/.test(l)).length;
        return fences % 2 === 1;
    }
    function _tabWithin(forward) {
        const cur = root.Window.activeFocusItem || root;
        const fromBody = cur !== bodyStop && root._inBody(cur);
        root._passingThrough = false;
        // Out of the body forward: my plan under it first. The field sits
        // over the list, outside it, so the focus chain went from the text
        // past the plan, round to the stop that hands the caret back to the
        // text: Tab never left the description (F2-REG-1, A4-15).
        if (fromBody && forward) {
            const stops = root._stopsIn(root._tail);
            if (stops.length > 0) {
                stops[0].forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
        // Out of the body backward: the last chip before it. The chain from
        // the text went round to the plan's add field instead, and the chips
        // and the title were never reached backward (F2-REG-1).
        if (fromBody && !forward) {
            const kids = bodyStop.parent ? bodyStop.parent.children : [];
            let before = [];
            for (let i = 0; i < kids.length && kids[i] !== bodyStop; i++)
                before = before.concat(root._stopsIn(kids[i]));
            if (before.length > 0) {
                before[before.length - 1].forceActiveFocus(Qt.BacktabFocusReason);
                return;
            }
        }
        // Leaving the body, never back into it: neither the stop that opens
        // it nor what is drawn inside it.
        const fits = function (it) {
            return it !== docFocus && it.visible
                && !(fromBody && (it === bodyStop || root._inBody(it)))
                && !(!fromBody && it !== bodyStop && root._inBody(it));
        };
        const reason = forward ? Qt.TabFocusReason : Qt.BacktabFocusReason;
        let next = cur.nextItemInFocusChain(forward);
        for (let n = 0; n < 200 && next; n++) {
            let inside = false;
            for (let p = next; p; p = p.parent)
                if (p === root) { inside = true; break; }
            // Out of the document: round to its other end. Shift+Tab from
            // the top went on to the board's cards behind it.
            if (!inside) break;
            if (fits(next)) {
                next.forceActiveFocus(reason);
                return;
            }
            next = next.nextItemInFocusChain(forward);
        }
        const all = root._stopsIn(root).filter(fits);
        if (all.length > 0) all[forward ? 0 : all.length - 1].forceActiveFocus(reason);
        else root.focusTitle();
    }
    // Return on the document goes into the text, at its end: the drawn
    // text gave no sign it could be typed into, and in rich the old
    // full-width toggle here never ran (body.editing is always true).
    Keys.onReturnPressed: (e) => {
        if (titleField.activeFocus || body.editor.activeFocus) { e.accepted = false; return; }
        body.caretToEnd();
        body.focusEnd();
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        Rectangle {
            // Beside a live board: a line between them. Over the whole
            // content area the sidebar's own edge is the line.
            visible: root.beside
            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 1
            color: Theme.border
        }
    }
    // Clicks stay in the panel.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = false }

    // ── layout (dogfood 2026-10-23: Main, TaskNarrow, Wide, Heavy sheets) ──
    // The window's width as Main has it; 0 in a test that makes the
    // document alone, which then goes by its own width.
    property real windowWidth: 0
    readonly property real _winW: root.windowWidth > 0 ? root.windowWidth : root.width
    // A window from 1800 logical px: the board or the list stays live on
    // the left and the task is a panel beside it, until `f` (or the expand
    // button) gives it the whole content area. Main says which.
    property bool wide: false
    readonly property bool beside: root.wide && !root.full
    // Only as Main says: a document made alone (a test) is not narrow.
    readonly property bool _narrowWindow: root.windowWidth > 0 && Theme.taskNarrowAt(root.windowWidth)
    // The UI settings, read where a binding needs them and written whole.
    function _settings() {
        try { return JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { return {}; }
    }
    function _writeSetting(key, value) {
        const s = root._settings();
        if (value === undefined || value === null) delete s[key];
        else s[key] = value;
        AppController.appSettingsJson = JSON.stringify(s);
    }
    // 1, 2 or 3 columns, chosen in the header (Ctrl+\ walks them); 0 = by
    // the window's width. Kept in the settings, for every task.
    readonly property int columnsChoice: {
        const n = Number(root._settings().taskDocColumns || 0);
        return n >= 1 && n <= 3 ? n : 0;
    }
    readonly property int _autoColumns: root.beside || root._narrowWindow ? 2 : 3
    // Three need the tracker's comments for their middle column; a task of
    // my own has none and stays in two.
    readonly property int columns: {
        const c = root.columnsChoice > 0 ? root.columnsChoice : root._autoColumns;
        return c === 3 && !root._isTicket ? 2 : c;
    }
    // The switch: the one in use again goes back to "by the window's width".
    function pickColumns(n) { root._writeSetting("taskDocColumns", n === root.columnsChoice ? null : n); }
    function cycleColumns() { root._writeSetting("taskDocColumns", root.columns % 3 + 1); }
    // `f`: the whole content area and back, where a panel beside the board
    // is what the window has.
    function toggleFull() { if (root.wide) root.full = !root.full; }

    // Every block folds by its header (click or Space), and stays folded for
    // every task: kept per kind in the settings. Folded until opened: the
    // rare ones; on a narrow window the time and the checklist too.
    readonly property var _foldSaved: {
        const f = root._settings().taskBlocksFolded;
        return f && typeof f === "object" ? f : ({});
    }
    function _foldDefault(kind) {
        if (kind === "owners" || kind === "mentions" || kind === "history") return true;
        return root._narrowWindow && (kind === "time" || kind === "check");
    }
    function isOpen(kind) {
        const v = root._foldSaved[kind];
        return v === undefined ? !root._foldDefault(kind) : !v;
    }
    function toggleBlock(kind) {
        const s = root._settings();
        const f = Object.assign({}, s.taskBlocksFolded && typeof s.taskBlocksFolded === "object" ? s.taskBlocksFolded : {});
        f[kind] = root.isOpen(kind);
        s.taskBlocksFolded = f;
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function openBlock(kind) { if (!root.isOpen(kind)) root.toggleBlock(kind); }
    // e on the document: the description into its editor (Checklist sheet).
    // The Vim keymap reads e as the estimate's key; on a ticket that can be
    // edited the document takes it (Main asks here first).
    readonly property var claimedKeys: ["task.estimate"]
    function runClaimed(id) {
        if (id !== "task.estimate" || !root._canEdit || root.descEditing) return false;
        if (String(AppController.shortcutFor("task.estimate")).toUpperCase() !== "E") return false;
        return root.startDescEdit();
    }

    readonly property int _docMax: root._quiet ? Theme.px(760) : Theme.px(780)
    // A narrow panel keeps its width for the text: smaller margins.
    readonly property bool _narrow: root.width < Theme.px(1000)
    readonly property int _padX: root.beside || root._narrow ? Theme.px(22) : Theme.px(28)
    readonly property int _sideW: Theme.taskSideColumn
    readonly property bool _hasCode: String(root.task.branch || "").length > 0 || root._isTicket || root._prNumber > 0
    // A short panel (a 720p screen at 125 %): tighter margins.
    readonly property bool _short: root.height < Theme.px(640)

    // Narrow (TaskNarrow sheet): the text column as two tabs, the
    // description and checklist, or the comments.
    readonly property bool contentTabs: root.columns === 2 && root._narrowWindow && root._isTicket
    property string contentTab: "desc"
    property int _ctabFocus: 0
    readonly property bool _onDescTab: !root.contentTabs || root.contentTab === "desc"
    readonly property bool _onCommentsTab: !root.contentTabs || root.contentTab === "comments"
    function pickContentTab(name) {
        root.contentTab = name;
        root._scrollTo(0);
    }

    // A long description folds to its first lines (Heavy sheet), "Читать
    // целиком · ещё N экрана" opens it, per task.
    property bool descExpanded: false
    readonly property real _foldH: Theme.px(200)
    readonly property bool _descLong: trackerBody.contentHeight > Math.max(Theme.px(320), docScroll.height * 0.9)
    readonly property bool _descFolded: root._descLong && !root.descExpanded
    // The pictures of the ticket's text: thumbnails under the folded text.
    readonly property var _descImages: {
        const out = [];
        const re = /!\[([^\]]*)\]\(([^)\s]+)[^)]*\)/g;
        let m;
        const t = root._trackerText;
        while ((m = re.exec(t)) !== null && out.length < 12) out.push({ alt: m[1], src: m[2] });
        return out;
    }
    function _screens(px) {
        const h = Math.max(1, docScroll.height);
        const n = Math.max(0.5, Math.round(px / h * 2) / 2);
        return I18n.lang === "ru" ? String(n).replace(".", ",") : String(n);
    }
    readonly property string _descSummary: {
        const bits = [];
        if (root._descLong) bits.push(I18n.t("taskdoc.block.screens").arg(root._screens(trackerBody.contentHeight)));
        else {
            const paras = root._trackerText.split(/\n\s*\n/).filter(p => p.trim().length > 0).length;
            if (paras > 0) bits.push(I18n.count(paras, "taskdoc.block.paragraphs"));
        }
        if (root._descImages.length > 0) bits.push(I18n.count(root._descImages.length, "taskdoc.block.shots"));
        return bits.join(" · ");
    }
    function foldDescription() {
        root.descExpanded = false;
        Qt.callLater(() => root._scrollTo(descSection.y));
    }

    // The one scroll of the text column.
    function _scrollTo(y) {
        const max = Math.max(0, docScroll.contentHeight - docScroll.height);
        docScroll.contentY = Math.max(0, Math.min(y, max));
    }
    // What has the keyboard, kept in view: a caret moving down my notes, a
    // checklist row reached with Tab.
    function _keepInView(item, y, h) {
        if (!item || !docContent) return;
        const p = item.mapToItem(docContent, 0, y);
        const top = docScroll.contentY, bottom = top + docScroll.height;
        if (p.y < top) root._scrollTo(p.y - Theme.spLg);
        else if (p.y + h > bottom) root._scrollTo(p.y + h - docScroll.height + Theme.spLg);
    }

    // The sections of a ticket's text, top to bottom, as laid out now:
    // [{kind, top, bottom}] in the scroll's coordinates.
    readonly property var _sections: {
        const out = [];
        const add = (kind, it) => {
            if (it.visible && it.parent === docContent) out.push({ kind: kind, top: it.y, bottom: it.y + it.height });
        };
        add("desc", descSection);
        add("check", checkSection);
        add("comments", commentsSection);
        return out;
    }
    // The sections there are, whichever tab or fold they are in.
    readonly property var _sectionKinds: {
        const k = [];
        if (root._isTicket && root._trackerText.length > 0) k.push("desc");
        if (smartList.totalCount > 0) k.push("check");
        if (root._isTicket && trackerComments.hasAny && !root._commentsApart) k.push("comments");
        return k;
    }
    readonly property real _tailRoom: {
        const s = root._sections;
        if (!root._sectionBarShown || s.length === 0) return Theme.sp2xl;
        return Math.max(Theme.sp2xl, docScroll.height - (docContent.implicitHeight - s[s.length - 1].top));
    }
    // The section bar (Heavy sheet): a ticket with two or more sections
    // and more of them than the column shows, in one scroll.
    readonly property bool _sectionBarShown: root._isTicket && !root.contentTabs && root._sections.length >= 2
                                             && docContent.implicitHeight > docCol.height
    // The section being read: the last whose top is above the top of the
    // view; at the very end, the last one.
    readonly property string activeSection: {
        const s = root._sections;
        if (s.length === 0) return "";
        // At the very end the last one, if it starts in the upper half
        // (a short last section never reaches the top).
        if (docScroll.contentY > 0 && docScroll.atYEnd
                && s[s.length - 1].top < docScroll.contentY + docScroll.height / 2) return s[s.length - 1].kind;
        let k = s[0].kind;
        for (let i = 0; i < s.length; i++) if (s[i].top <= docScroll.contentY + Theme.spLg) k = s[i].kind;
        return k;
    }
    // The open section whose header has scrolled away: its header stays on
    // top of the column until the next one pushes it up.
    readonly property var _sticky: {
        const s = root._sections;
        const y = docScroll.contentY;
        for (let i = 0; i < s.length; i++) {
            if (!root.isOpen(s[i].kind)) continue;
            if (s[i].top < y && s[i].bottom > y + stickyHead.implicitHeight)
                return { kind: s[i].kind, push: Math.min(0, s[i].bottom - y - stickyHead.implicitHeight) };
        }
        return { kind: "", push: 0 };
    }
    // A section's header clicked in the bar, or its digit (1 2 3): there,
    // and open.
    function jumpTo(kind) {
        if (root.contentTabs) {
            root.pickContentTab(kind === "comments" ? "comments" : "desc");
            if (kind === "comments") return;
        }
        root.openBlock(kind);
        const it = kind === "desc" ? descSection : kind === "check" ? checkSection : commentsSection;
        if (!it.visible || it.parent !== docContent) return;
        Qt.callLater(() => root._scrollTo(it.y));
    }
    function _sectionLabel(kind) {
        if (kind === "desc") return I18n.t("taskdoc.sec.desc");
        if (kind === "check") return I18n.t("taskdoc.sec.check").arg(smartList.doneCount).arg(smartList.totalCount);
        return I18n.t("taskdoc.sec.comments").arg(trackerComments.total);
    }
    // f, 1 2 3 and Ctrl+\ on the document, wherever nothing typed takes them.
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Backslash && (e.modifiers & Qt.ControlModifier)) {
            root.cycleColumns();
            e.accepted = true;
            return;
        }
        if (e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return;
        if (e.key === Qt.Key_F && root.wide) {
            root.toggleFull();
            e.accepted = true;
            return;
        }
        // e: the description into its editor; a: a new checklist item —
        // on a ticket whose tracker takes edits from here.
        if (e.key === Qt.Key_E && !(e.modifiers & Qt.ShiftModifier) && root._canEdit && descSection.visible) {
            e.accepted = root.startDescEdit();
            return;
        }
        if (e.key === Qt.Key_A && !(e.modifiers & Qt.ShiftModifier) && root._canEdit && checkSection.visible) {
            root.openBlock("check");
            smartList.startAdd();
            e.accepted = true;
            return;
        }
        const n = e.key - Qt.Key_1;
        const kinds = root._sectionKinds;
        if (n >= 0 && n < kinds.length && kinds.length > 1) {
            root.jumpTo(kinds[n]);
            e.accepted = true;
        }
    }

    // Three columns (Main sheet): the text, the comments, the properties;
    // two: the text with its comments in one scroll, and the properties; one:
    // the text alone, the properties folded into a line under the chips.
    readonly property bool _sideShown: root.columns >= 2 && root.width >= Theme.px(440) + root._sideW
    readonly property real _flexW: root.width - (root._sideShown ? root._sideW : 0)
    readonly property bool _commentsApart: root.columns === 3 && root._sideShown && root._flexW >= Theme.px(760)
    // The text's width: 5 to the comments' 4 in three columns (the sheet's
    // 1.25 : 1); at most 760 beside a board; the sheet's 780 in one column
    // of the whole area.
    readonly property real _docW: root._commentsApart ? Math.round(root._flexW * 5 / 9)
                                : root.beside ? Math.min(root._flexW, Theme.taskTextMax + 2 * root._padX)
                                : root.columns === 1 && root.full ? Math.min(root._flexW, root._docMax + 2 * root._padX)
                                : root._flexW

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── the text column ──
        ColumnLayout {
            id: docCol
            objectName: "task-doc-text"
            Layout.preferredWidth: root._docW
            Layout.maximumWidth: root._docW
            Layout.fillHeight: true
            spacing: 0

            // The header. Beside a board (Wide sheet): the key, and two
            // buttons, "⤢ На весь экран f" and "× Закрыть Esc". Over the
            // content area (Main sheet): "Задачи / Code Review / TEL-401198",
            // then the column switch, "Ctrl \", "Esc назад".
            RowLayout {
                objectName: "task-doc-head"
                Layout.fillWidth: true
                Layout.leftMargin: root._padX
                Layout.rightMargin: root._padX
                Layout.topMargin: root._short ? Theme.spSm : Theme.spMd
                Layout.bottomMargin: root._short ? Theme.spSm : Theme.spMd
                Layout.preferredHeight: Theme.chipH
                spacing: Theme.spMd
                Text {
                    objectName: "task-doc-crumbs-root"
                    visible: !root.beside
                    text: I18n.t("sidebar.tasks")
                    color: crumbCA.hovered ? Theme.text : Theme.textMuted
                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    ClickArea {
                        id: crumbCA
                        label: parent.text
                        onActivated: root.close()
                        // The first stop: Shift+Tab here goes round to the
                        // end, not on to the board behind the document.
                        Keys.onBacktabPressed: (e) => { root._tabWithin(false); e.accepted = true; }
                    }
                }
                Text {
                    objectName: "task-doc-crumbs"
                    visible: !root.beside
                    text: "/" + (root._quiet ? "" : "  " + root._statusName(root.task.status || "") + "   /")
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    // The name the card shows: the tracker key (A3-10, A1-29).
                    objectName: "task-doc-crumb-id"
                    text: root.task.externalKey || root.taskId
                    color: crumbIdCA.hovered ? Theme.text : root.beside ? Theme.text : Theme.textMuted
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsSm
                    ClickArea {
                        id: crumbIdCA
                        objectName: "task-doc-crumb-id-copy"
                        label: I18n.t("taskcard.copyId")
                        tip: I18n.t("taskcard.copyId.tip")
                        activeFocusOnTab: false
                        onActivated: AppController.copyTaskKey(parent.text)
                    }
                }
                Text {
                    visible: root.beside && root._isTicket
                    text: "· " + (root._badge.name || root._ticket.provider || "")
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Item { Layout.fillWidth: true }
                // Saved, or saving while the typing settles.
                Text {
                    objectName: "task-doc-saved"
                    readonly property bool noTitle: root._cleanTitle(titleField.text).length === 0
                    readonly property bool pending: saveTimer.running || root._dirtyTitle || root._dirtyBody
                    text: noTitle ? I18n.t("taskdoc.needsTitle")
                        : pending ? I18n.t("taskdoc.saving") : (root._quiet ? I18n.t("taskdoc.saved").toLowerCase() : I18n.t("taskdoc.saved"))
                    color: noTitle ? Theme.warning : pending || root._quiet ? Theme.textMuted : Theme.success
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                // 1 · 2 · 3 колонки (Ctrl+\): over the content area only.
                Row {
                    id: colSwitch
                    objectName: "task-doc-columns"
                    visible: !root.beside && !root._narrow
                    spacing: Theme.sp2xs
                    Accessible.role: Accessible.Grouping
                    Accessible.name: I18n.t("taskdoc.layout.label")
                    Repeater {
                        model: [1, 2, 3]
                        delegate: Rectangle {
                            id: colBtn
                            required property int modelData
                            readonly property bool current: root.columns === colBtn.modelData
                            objectName: "task-doc-columns-" + colBtn.modelData
                            implicitWidth: colLabel.implicitWidth + 2 * Theme.spSm
                            implicitHeight: Theme.chipHSmall
                            radius: Theme.radiusSm
                            color: colBtn.current ? Theme.panel3 : colCA.hovered ? Theme.panel2 : "transparent"
                            border.width: colCA.keyboardFocused ? 1 : 0
                            border.color: Theme.focusRing
                            Text {
                                id: colLabel
                                anchors.centerIn: parent
                                text: colBtn.current ? I18n.count(colBtn.modelData, "taskdoc.layout.columns") : String(colBtn.modelData)
                                color: colBtn.current ? Theme.text : Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                            }
                            ClickArea {
                                id: colCA
                                label: I18n.count(colBtn.modelData, "taskdoc.layout.columns")
                                tip: I18n.t("taskdoc.layout.tip").arg(I18n.count(colBtn.modelData, "taskdoc.layout.columns"))
                                checkable: true
                                checked: colBtn.current
                                onActivated: root.pickColumns(colBtn.modelData)
                            }
                        }
                    }
                }
                Text {
                    visible: colSwitch.visible && Style.keyHints
                    text: "Ctrl \\"
                    color: Theme.textMuted
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsXs
                }
                Row {
                    visible: Style.keyHints && !root.beside
                    spacing: Theme.spXs
                    Text {
                        anchors.baseline: backText.baseline
                        text: "Esc"
                        color: Theme.text
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsSm
                        font.weight: Theme.fwHeading
                    }
                    Text {
                        id: backText
                        text: I18n.t("taskdoc.back")
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                    }
                }
                // Beside a board: "На весь экран f"; over the whole area of
                // a wide window: back to the panel. Not where the window has
                // no panel to go back to.
                HeadButton {
                    objectName: "task-doc-full"
                    visible: root.wide
                    icon: root.full ? "collapse" : "expand"
                    text: root.full ? I18n.t("taskdoc.toPanel") : I18n.t("taskdoc.expand")
                    keyText: "f"
                    tip: root.full ? I18n.t("taskdoc.panel") : I18n.t("taskdoc.full")
                    onActivated: root.toggleFull()
                }
                HeadButton {
                    objectName: "task-doc-close"
                    visible: root.beside
                    icon: "close"
                    text: I18n.t("taskdoc.closePanel")
                    keyText: "Esc"
                    strong: true
                    onActivated: root.close()
                }
            }
            Rectangle {
                visible: root.beside
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            // A pull changed the text while you were in it: said quietly.
            Text {
                objectName: "task-doc-changed"
                Layout.fillWidth: true
                Layout.leftMargin: root._padX
                Layout.rightMargin: root._padX
                Layout.bottomMargin: Theme.spMd
                visible: root._changedUnder
                text: I18n.t("taskdoc.changedUnder")
                color: Theme.warning
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                objectName: "task-doc-conflict"
                Layout.fillWidth: true
                Layout.leftMargin: root._padX
                Layout.rightMargin: root._padX
                Layout.bottomMargin: Theme.spMd
                visible: root._hasConflict
                spacing: Theme.spXs
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("ticket.conflict.head")
                    textFormat: Text.PlainText
                    color: Theme.warning
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    font.weight: Theme.fwTitle
                    wrapMode: Text.WordWrap
                }
                Repeater {
                    model: root._conflictRows
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.label + ": " + modelData.value
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                        wrapMode: Text.WordWrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                    }
                }
                RowLayout {
                    spacing: Theme.spSm
                    PillButton {
                        id: conflictUseTracker
                        objectName: "task-doc-conflict-use-tracker"
                        text: I18n.t("ticket.conflict.useTracker")
                        onClicked: root.resolveConflict(true)
                    }
                    PillButton {
                        id: conflictKeepMine
                        objectName: "task-doc-conflict-keep-mine"
                        text: I18n.t("ticket.conflict.keepMine")
                        onClicked: root.resolveConflict(false)
                    }
                }
            }

            // "Описание 1 · Чек-лист 3/20 2 · Комментарии 14 3" (Heavy
            // sheet): where each section is, the one being read lit up.
            RowLayout {
                id: sectionBar
                objectName: "task-doc-section-bar"
                Layout.fillWidth: true
                visible: root._sectionBarShown
                spacing: Theme.spXs
                Accessible.role: Accessible.Grouping
                Accessible.name: I18n.t("taskdoc.sec.bar")
                Item { Layout.preferredWidth: root._padX - Theme.spSm }
                Repeater {
                    model: root._sections
                    delegate: Rectangle {
                        id: secBtn
                        required property var modelData
                        required property int index
                        readonly property bool current: root.activeSection === secBtn.modelData.kind
                        objectName: "task-doc-section-" + secBtn.modelData.kind
                        Layout.topMargin: Theme.spSm
                        Layout.bottomMargin: Theme.spSm
                        implicitWidth: secRow.implicitWidth + 2 * Theme.spSm
                        implicitHeight: Theme.chipHSmall
                        radius: Theme.radiusSm
                        color: secBtn.current ? Theme.panel3 : secCA.hovered ? Theme.panel2 : "transparent"
                        border.width: secCA.keyboardFocused ? 1 : 0
                        border.color: Theme.focusRing
                        Row {
                            id: secRow
                            anchors.centerIn: parent
                            spacing: Theme.spXs
                            Text {
                                text: root._sectionLabel(secBtn.modelData.kind)
                                color: secBtn.current ? Theme.text : Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                font.features: Theme.tabularNums
                            }
                            Text {
                                visible: Style.keyHints
                                text: String(secBtn.index + 1)
                                color: Theme.textDim
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fsXs
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        ClickArea {
                            id: secCA
                            label: root._sectionLabel(secBtn.modelData.kind)
                            showTip: false
                            checkable: true
                            checked: secBtn.current
                            onActivated: root.jumpTo(secBtn.modelData.kind)
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: root.wide && Style.keyHints
                    Layout.rightMargin: root._padX
                    text: I18n.t("taskdoc.sec.full") + " <b><font face=\"" + Theme.fontMono + "\">f</font></b>"
                    textFormat: Text.StyledText
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsXs
                }
            }
            Rectangle {
                visible: sectionBar.visible
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            // Everything below scrolls as one: the title, the chips, the
            // description, the checklist, my notes and (in one or two
            // columns) the comments. Nothing inside scrolls on its own.
            Item {
                id: scrollHost
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Flickable {
                    id: docScroll
                    objectName: "task-doc-scroll"
                    anchors.fill: parent
                    contentWidth: width
                    // With the section bar, room under the last section
                    // for its header to reach the top (a jump to it).
                    contentHeight: docContent.y + docContent.implicitHeight + root._tailRoom

                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    QQC.ScrollBar.vertical: ThinScrollBar {}

                    ColumnLayout {
                        id: docContent
                        objectName: "task-doc-content"
                        x: root._padX
                        y: root._short ? Theme.spSm : Theme.spLg
                        width: docScroll.width - 2 * root._padX
                        spacing: 0

                        QQC.TextArea {
                            id: titleField
                            objectName: "task-doc-title"
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spLg
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            selectByMouse: true
                            color: Theme.text
                            font.family: Theme.fontDisplay
                            // The sheet's 24 px heading, a step or two down
                            // for a title of more than three lines (F3-EYES-10).
                            font.pixelSize: Theme.typeStep(7 - root._titleStepsDown)
                            font.weight: root._quiet ? Theme.fwTitle : Theme.fwHeading
                            padding: 0
                            // Basic binds its own side padding; the title shares
                            // the chips' left edge (R3-037).
                            leftPadding: 0
                            rightPadding: 0
                            background: Item {}
                            placeholderText: I18n.t("taskdoc.titlePh")
                            placeholderTextColor: Theme.textDim
                            // A ticket that takes edits: the title goes to the
                            // tracker on Enter or on leaving the field, Esc puts
                            // it back; nothing is saved as a local title.
                            onTextChanged: if (!root._loading) {
                                if (root._canEdit) root._titleEdited = true;
                                else { root._dirtyTitle = true; saveTimer.restart(); }
                            }
                            // Left empty, the stored title comes back into view
                            // rather than silently on the next open (IDIOT-DOC-15).
                            onActiveFocusChanged: {
                                if (activeFocus) return;
                                if (root._titleEdited) { root.commitTitle(); return; }
                                if (root.opened && root._exists && root._cleanTitle(titleField.text).length === 0) {
                                    root._loading = true; titleField.text = root.task.title; root._loading = false;
                                    root._dirtyTitle = false;
                                }
                            }
                            Keys.onReturnPressed: (e) => { root.commitTitle(); body.focusEditor(); e.accepted = true; }
                            Keys.onEnterPressed: (e) => { root.commitTitle(); body.focusEditor(); e.accepted = true; }
                            // Tab moves on, as in any form; it typed a tab into
                            // the title (IDIOT-DOC-13).
                            Keys.onTabPressed: (e) => { root._tabWithin(true); e.accepted = true; }
                            Keys.onBacktabPressed: (e) => { root._tabWithin(false); e.accepted = true; }
                            // An edit for the tracker: the first Esc takes it
                            // back, the next closes as before.
                            Keys.onEscapePressed: {
                                if (root._titleEdited) { root.revertTitle(); root.focusDocument(); }
                                else root.close();
                            }
                            Keys.onDownPressed: (e) => {
                                if (titleField.text.indexOf("\n", titleField.cursorPosition) < 0
                                    && titleField.positionAt(titleField.cursorRectangle.x, titleField.cursorRectangle.y + titleField.cursorRectangle.height + 4) >= titleField.length) {
                                    body.focusEditor();
                                    e.accepted = true;
                                } else e.accepted = false;
                            }
                        }

                        // Статус · Приоритет · Когда · Срок · Метки · … · + свойство
                        Flow {
                            id: chips
                            objectName: "task-doc-chips"
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spXl
                            spacing: Theme.spSm
                            PropertyChip {
                                id: statusChip
                                objectName: "task-doc-status"
                                key: root._key(I18n.t("taskdoc.prop.status"))
                                value: root._quiet ? root._statusName(root.task.status || "").toLowerCase() : root._statusName(root.task.status || "")
                                ring: AppController.statusCategory(root.task.status || "")
                                onClicked: statusMenu.popup(statusChip, 0, statusChip.height + Theme.spXs)
                            }
                            PropertyChip {
                                id: priorityChip
                                objectName: "task-doc-priority"
                                key: root._key(root._isTicket ? I18n.t("taskmenu.myPriority") : I18n.t("taskdoc.prop.priority"))
                                value: root.task.priority || ""
                                valueColor: Style.urgency ? Theme.priorityInk(root.task.priority || "P2") : Theme.text
                                onClicked: priorityMenu.popup(priorityChip, 0, priorityChip.height + Theme.spXs)
                            }
                            DateChip {
                                objectName: "task-doc-scheduled"
                                field: "scheduled"
                                visible: root._visible("scheduled")
                                key: root._key(I18n.t("capture.when"))
                                value: root._when(root.task.scheduledAt, root.task.scheduledHasTime, root.task.estimateMinutes || 0)
                            }
                            DateChip {
                                objectName: "task-doc-due"
                                field: "due"
                                visible: root._visible("due")
                                key: root._key(root._isTicket ? I18n.t("taskmenu.myDue").replace("…", "") : I18n.t("capture.due"))
                                value: root._when(root.task.dueAt, root.task.dueHasTime, 0)
                                    // A deadline before the plan: a grey note, not an error.
                                    + (root._valid(root.task.dueAt) && root._valid(root.task.scheduledAt)
                                       && root.task.dueAt < root.task.scheduledAt ? " · " + I18n.t("taskdoc.dueBeforePlan") : "")
                                // Due today or tomorrow: the "soon" signal (bold only).
                                valueColor: root._valid(root.task.dueAt) && !root._done && root._dayDiff(root.task.dueAt) <= 1
                                            ? (root._dayDiff(root.task.dueAt) < 0 ? Theme.signalUrgent : Theme.signalNow) : Theme.text
                            }
                            // The ticket's own due date and assignee, written
                            // to the tracker (owner item 16): only while it
                            // takes edits from here. "Мой срок" stays mine.
                            PropertyChip {
                                id: trackerDueChip
                                objectName: "task-doc-tracker-due"
                                visible: root._canEdit
                                key: root._key(I18n.t("taskdoc.edit.due")) + " " + root._trackerName
                                value: (root._valid(root._tv.trackerDueAt) ? root._when(root._tv.trackerDueAt, false, 0) : "—")
                                       + (root._pending.due ? " · " + I18n.t("taskdoc.edit.sendingShort") : "")
                                onClicked: trackerDuePicker.openAt(root._valid(root._tv.trackerDueAt) ? root._tv.trackerDueAt : null, trackerDueChip)
                                DatePickerPopup {
                                    id: trackerDuePicker
                                    objectName: "task-doc-tracker-due-picker"
                                    clearable: true
                                    y: trackerDueChip.height + Theme.spXs
                                    onPicked: (d) => root.editTrackerDue(Qt.formatDate(d, "yyyy-MM-dd"))
                                    onCleared: root.editTrackerDue("")
                                    onClosed: trackerDueChip.focusChip()
                                }
                            }
                            PropertyChip {
                                id: assigneeChip
                                objectName: "task-doc-tracker-assignee"
                                visible: root._canEdit
                                key: root._key(I18n.t("taskdoc.edit.assignee"))
                                value: (String(root.task.assignee || "").length > 0 ? String(root.task.assignee) : "—")
                                       + (root._pending.assignee ? " · " + I18n.t("taskdoc.edit.sendingShort") : "")
                                onClicked: assigneePicker.openAt(assigneeChip)
                                TrackerAssigneePicker {
                                    id: assigneePicker
                                    api: root.trackerApi
                                    taskId: root.taskId
                                    current: String(root.task.assignee || "")
                                    onPicked: (who) => root.editTrackerAssignee(who)
                                    onClosed: assigneeChip.focusChip()
                                }
                            }
                            TextChip {
                                objectName: "task-doc-labels"
                                visible: root._visible("labels")
                                key: root._key(I18n.t("taskdoc.prop.labels"))
                                value: (root.task.labels || []).map(l => l.id).join(" ")
                                onCommitted: (text) => {
                                    const ids = String(text).split(/[\s,]+/).map(w => w.replace(/^#/, "")).filter(w => w.length > 0);
                                    const prev = root.task.labels || [];
                                    root._setField("labels", ids.map(id => {
                                        const was = prev.find(l => l.id === id);
                                        return { id: id, color: was ? was.color : "" };
                                    }));
                                }
                            }
                            // My tags (APP-239): mine on any card, never the tracker's.
                            PropertyChip {
                                id: tagsChip
                                objectName: "task-doc-tags"
                                visible: root._visible("tags")
                                key: root._key(I18n.t("local.tags.key"))
                                value: root._localTags.map(l => "#" + l.id).join(" ")
                                onClicked: { root._tagsEditing = true; tagsEditor.open(); }
                            }
                            PropertyChip {
                                id: recurChip
                                objectName: "task-doc-recurrence"
                                visible: root._visible("recurrence")
                                key: root._key(I18n.t("taskdoc.prop.repeat"))
                                value: root._recurLabel(root.task.recurrence)
                                onClicked: recurMenu.popup(recurChip, 0, recurChip.height + Theme.spXs)
                            }
                            QQC.TextField {
                                id: recurField
                                objectName: "task-doc-recur-custom"
                                visible: root._recurEditing
                                width: Theme.px(280)
                                height: Theme.chipH
                                leftPadding: Theme.spMd
                                rightPadding: Theme.spMd
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                color: Theme.text
                                placeholderText: I18n.t("recur.custom.ph")
                                placeholderTextColor: Theme.textDim
                                background: Rectangle {
                                    radius: Theme.radiusMd
                                    color: Theme.panel2
                                    border.color: root._recurBad ? Theme.danger : Theme.focusRing
                                    border.width: 1
                                }
                                onTextChanged: root._recurBad = false
                                onAccepted: root.applyCustomRecur(recurField.text)
                                onActiveFocusChanged: if (!activeFocus && root._recurEditing) root.cancelCustomRecur()
                                Keys.onEscapePressed: (e) => { root.cancelCustomRecur(); root.focusDocument(); e.accepted = true; }
                                Keys.onReturnPressed: (e) => { recurField.accepted(); e.accepted = true; }
                                Keys.onEnterPressed: (e) => { recurField.accepted(); e.accepted = true; }
                                // What Enter and Esc do, and what was not read.
                                QQC.ToolTip.visible: recurField.visible && recurField.activeFocus
                                QQC.ToolTip.text: root._recurBad ? I18n.t("recur.custom.err") : I18n.t("recur.custom.hint")
                            }
                            // "7ч 30м / 10ч" with a clock (item 19); over the estimate
                            // in the warning colour with "+4ч". Empty, it waits in
                            // "+ свойство" like any empty property; E opens it here.
                            PropertyChip {
                                id: estChip
                                objectName: "task-doc-estimate"
                                visible: root._visible("estimate")
                                icon: "timer"
                                mono: root._effort.shown
                                value: root._effort.shown ? root._effort.text : root._key(I18n.t("capture.estimate"))
                                valueColor: root._effort.over ? Theme.warning : root._effort.shown ? Theme.text : Theme.textMuted
                                onClicked: root.openEstimate()
                                EstimatePopup {
                                    id: estPopup
                                    y: estChip.height + Theme.spXs
                                    taskId: root.taskId
                                    provider: root._estProvider
                                    providerName: root._badge.name || root._estProvider
                                    onFinished: (byKey) => {
                                        if (root._estChipAdded && !root._has("estimate"))
                                            root._shown = root._shown.filter((k) => k !== "estimate");
                                        root._estChipAdded = false;
                                        if (byKey && estChip.visible) estChip.focusChip();
                                        else if (byKey) root.focusDocument();
                                    }
                                }
                            }
                            // Story points, when the tracker has them: a number apart
                            // from time, never added to it.
                            PropertyChip {
                                objectName: "task-doc-points"
                                visible: root._hasPoints
                                outlined: true
                                value: I18n.t("estimate.points").arg(root._trackerEst.points)
                                valueColor: Theme.textMuted
                                onClicked: root.openEstimate()
                            }
                            TextChip {
                                id: remindChip
                                objectName: "task-doc-remind"
                                visible: root._visible("remind")
                                key: root._key(I18n.t("taskdoc.prop.remind"))
                                value: root._remindLabel()
                                QQC.ToolTip.visible: remindChip._editing
                                QQC.ToolTip.text: I18n.t("taskdoc.remind.ph")
                                // Left as it was shown: nothing to read again (a shown
                                // "15:00" read again after 15:00 would be tomorrow's).
                                onCommitted: (text) => { if (String(text) !== remindChip.value) AppController.setTaskReminder(root.taskId, text); }
                            }
                            PropertyChip {
                                id: addChip
                                objectName: "task-doc-add"
                                add: true
                                value: I18n.t("taskdoc.addProp")
                                onClicked: addMenu.popup(addChip, 0, addChip.height + Theme.spXs)
                            }
                        }

                        // With the properties column folded away, what it
                        // would say about the tracker and the notes still
                        // shows (A1-29, A4-13).
                        RowLayout {
                            objectName: "task-doc-inline-meta"
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spLg
                            visible: !sideCol.visible && (root._isTicket || inlineMentions.count > 0)
                            spacing: Theme.spMd
                            Text {
                                objectName: "task-doc-tracker-inline"
                                visible: root._isTicket
                                text: root._trackerLabel
                                color: trackerInlineCA.hovered ? Theme.text : Theme.textMuted
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                font.underline: trackerInlineCA.hovered
                                ClickArea {
                                    id: trackerInlineCA
                                    label: parent.text
                                    enabled: String(root.task.externalUrl || "").length > 0
                                    onActivated: AppController.openTaskExternal(root.taskId)
                                }
                            }
                            Text {
                                objectName: "task-doc-local-only"
                                visible: root._isTicket && !AppController.trackerWriteEnabled(root._ticket.provider || "")
                                text: "· " + I18n.t("taskdoc.localOnly").arg(root._badge.name || root._ticket.provider || "")
                                color: Theme.textDim
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                            }
                            Text {
                                id: inlineMentions
                                objectName: "task-doc-mentions-inline"
                                readonly property int count: root._rev >= 0 && root.opened ? AppController.notesMentioningTask(root.taskId).length : 0
                                visible: count > 0
                                text: (root._isTicket ? "· " : "") + I18n.t("taskdoc.mentionedIn").arg(count)
                                color: mentionsCA.hovered ? Theme.text : Theme.textMuted
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                font.underline: mentionsCA.hovered
                                // Two columns have room for the properties.
                                ClickArea { id: mentionsCA; label: parent.text; onActivated: root.pickColumns(2) }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // Mine over the tracker's (APP-238): what the tracker
                        // says, said quietly, and "changed in the tracker" when
                        // it moved since I set mine — never overwriting mine.
                        RowLayout {
                            objectName: "task-doc-tracker-values"
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spLg
                            visible: root._mine
                            spacing: Theme.spMd
                            Text {
                                objectName: "task-doc-tracker-values-text"
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: {
                                    const tv = root._tv;
                                    const bits = [];
                                    if (String(tv.myPriority || "").length > 0) bits.push(String(tv.trackerPriority || "—"));
                                    if (root._valid(tv.myDueAt))
                                        bits.push(root._valid(tv.trackerDueAt) ? I18n.t("local.tracker.due").arg(root._when(tv.trackerDueAt, tv.trackerDueHasTime, 0))
                                                                               : I18n.t("local.tracker.noDue"));
                                    return I18n.t("local.tracker.says").arg(root._badge.name || root._ticket.provider || "").arg(bits.join(", "))
                                        + (tv.priorityChanged || tv.dueChanged ? " · " + I18n.t("local.trackerChanged") : "");
                                }
                                color: root._tv.priorityChanged || root._tv.dueChanged ? Theme.signalNow : Theme.textDim
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                            }
                            Text {
                                objectName: "task-doc-keep-mine"
                                visible: !!(root._tv.priorityChanged || root._tv.dueChanged)
                                text: I18n.t("local.tracker.keepMine")
                                color: keepCA.hovered ? Theme.text : Theme.textMuted
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                                ClickArea { id: keepCA; label: parent.text; onActivated: AppController.acknowledgeTrackerChange(root.taskId) }
                            }
                            Text {
                                objectName: "task-doc-reset-tracker"
                                text: I18n.t("local.tracker.reset")
                                color: resetCA.hovered ? Theme.text : Theme.textMuted
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                                ClickArea { id: resetCA; label: parent.text; onActivated: AppController.resetToTracker(root.taskId, "") }
                            }
                        }

                        TaskLocalTags {
                            id: tagsEditor
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spLg
                            visible: root._tagsEditing
                            taskId: root.taskId
                            rev: root._rev
                            tags: root._localTags
                            onDone: root._tagsEditing = false
                        }

                        // Narrow (TaskNarrow sheet): «Описание и чек-лист» ·
                        // «Комментарии · 5», ← / → between them.
                        RowLayout {
                            objectName: "task-doc-content-tabs"
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spLg
                            visible: root.contentTabs
                            spacing: Theme.spXl
                            Accessible.role: Accessible.PageTabList
                            Repeater {
                                model: ["desc", "comments"]
                                delegate: Item {
                                    id: ctab
                                    required property string modelData
                                    readonly property bool current: root.contentTab === ctab.modelData
                                    objectName: "task-doc-ctab-" + ctab.modelData
                                    implicitWidth: ctabText.implicitWidth
                                    implicitHeight: ctabText.implicitHeight + Theme.spMd
                                    Text {
                                        id: ctabText
                                        text: ctab.modelData === "desc" ? I18n.t("taskdoc.tab.desc")
                                                                        : I18n.t("taskdoc.tab.comments").arg(trackerComments.total)
                                        color: ctab.current ? Theme.text : Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsMd
                                        font.weight: ctab.current ? Theme.fwTitle : Theme.fwBody
                                        font.features: Theme.tabularNums
                                    }
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: Theme.px(2)
                                        visible: ctab.current || ctabCA.keyboardFocused
                                        color: ctabCA.keyboardFocused && !ctab.current ? Theme.focusRing : Theme.text
                                    }
                                    ClickArea {
                                        id: ctabCA
                                        objectName: "task-doc-ctab-" + ctab.modelData + "-area"
                                        label: ctabText.text
                                        showTip: false
                                        role: Accessible.PageTab
                                        checkable: true
                                        checked: ctab.current
                                        onActivated: root.pickContentTab(ctab.modelData)
                                        // ← / →: the other tab, the keyboard with it.
                                        Keys.onLeftPressed: { root.pickContentTab("desc"); root._ctabFocus++; }
                                        Keys.onRightPressed: { root.pickContentTab("comments"); root._ctabFocus++; }
                                    }
                                    Connections {
                                        target: root
                                        function on_CtabFocusChanged() { if (ctab.current) ctabCA.forceActiveFocus(Qt.TabFocusReason); }
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Rectangle {
                            visible: root.contentTabs
                            Layout.fillWidth: true
                            Layout.topMargin: -Theme.spLg
                            Layout.bottomMargin: Theme.spLg
                            Layout.preferredHeight: 1
                            color: Theme.border
                        }

                        // One quiet line about writing to the tracker: where
                        // to switch it on, that it is switched off, that edits
                        // go there, what is being sent, and how the last one
                        // ended (owner item 16).
                        Text {
                            id: editNote
                            objectName: "task-doc-edit-note"
                            readonly property string kind: root._editNote.kind || ""
                            readonly property bool sending: Object.keys(root._pending).length > 0
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spMd
                            visible: text.length > 0 && root._onDescTab
                            text: {
                                if (!root._isTicket) return "";
                                if (root._trackerOff) return descSection.visible ? "" : I18n.t("taskdoc.edit.trackerOff").arg(root._trackerName);
                                if (root._editHint) return I18n.t("taskdoc.edit.hint");
                                if (!root._canEdit) return "";
                                if (editNote.kind.length > 0) return String(root._editNote.text || "");
                                if (editNote.sending) return I18n.t("taskdoc.edit.sending").arg(root._trackerName);
                                if (titleField.activeFocus) return I18n.t("taskdoc.edit.titleKeys").arg(root._trackerName);
                                return I18n.t("taskdoc.edit.goesTo").arg(root._trackerName);
                            }
                            wrapMode: Text.WordWrap
                            color: editNote.kind === "bad" ? Theme.warning : editNote.kind === "ok" ? Theme.success : Theme.textDim
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                        }
                        // An edit that met the tracker's own change: both
                        // versions, and which one stays.
                        TrackerEditConflict {
                            id: conflictPanel
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spLg
                            visible: !!root._conflict && root._onDescTab
                            fieldLabel: root._conflict ? root._fieldLabel(root._conflict.field) : ""
                            trackerName: root._trackerName
                            mine: root._conflict ? root._conflict.mine : ""
                            theirs: root._conflict ? root._conflict.theirs : ""
                            canOverwrite: !!root._conflict && root._conflict.field !== "checklist"
                            onKeepMine: root.keepMyEdit()
                            onTakeTheirs: root.takeTrackerEdit()
                            onDismissed: root.dismissConflict()
                        }

                        // ── Описание: the tracker's text, read-only (APP-237) ──
                        ColumnLayout {
                            id: descSection
                            objectName: "task-doc-desc"
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spMd
                            visible: root._isTicket && (root._trackerText.length > 0 || root._canEdit) && root._onDescTab
                            spacing: 0
                            TaskBlockHead {
                                id: trackerHead
                                objectName: "task-doc-tracker-readonly"
                                Layout.fillWidth: true
                                kind: "desc"
                                title: I18n.t("taskdoc.block.desc")
                                sub: I18n.t("taskdoc.block.from").arg(root._badge.name || root._ticket.provider || "")
                                summary: root.descEditing ? "" : root._descSummary
                                summaryWhenOpen: !root.descExpanded
                                open: root.isOpen("desc") || root.descEditing
                                onToggled: if (!root.descEditing) root.toggleBlock("desc")
                                // Why it takes no typing (APP-313), in the tip
                                // of the mark (APP-314) — while it takes none.
                                WhyMark { visible: !root._canEdit && !root._trackerOff }
                                // The tracker switched off (owner item 7).
                                Text {
                                    objectName: "task-doc-tracker-off"
                                    visible: root._trackerOff
                                    text: I18n.t("taskdoc.edit.trackerOff").arg(root._trackerName)
                                    color: Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                }
                                // "Править e" (Checklist sheet).
                                Rectangle {
                                    objectName: "task-doc-desc-edit"
                                    visible: root._canEdit && !root.descEditing
                                    implicitWidth: editRow.implicitWidth + 2 * Theme.spSm
                                    implicitHeight: Theme.chipHSmall
                                    radius: Theme.radiusMd
                                    color: editCA.hovered ? Theme.panel2 : "transparent"
                                    border.width: 1
                                    border.color: Theme.buttonLine
                                    Row {
                                        id: editRow
                                        anchors.centerIn: parent
                                        spacing: Theme.spXs
                                        Text {
                                            text: I18n.t("taskdoc.edit.edit")
                                            color: Theme.text
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsSm
                                        }
                                        Text {
                                            visible: Style.keyHints
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: "e"
                                            color: Theme.textMuted
                                            font.family: Theme.fontMono
                                            font.pixelSize: Theme.fsXs
                                        }
                                    }
                                    ClickArea {
                                        id: editCA
                                        objectName: "task-doc-desc-edit-area"
                                        label: I18n.t("taskdoc.edit.editDesc").arg(root._trackerName)
                                        onActivated: root.startDescEdit()
                                    }
                                }
                                Text {
                                    objectName: "task-doc-desc-fold"
                                    visible: root.isOpen("desc") && root._descLong && root.descExpanded && !root.descEditing
                                    text: I18n.t("taskdoc.desc.fold")
                                    color: foldCA.hovered || foldCA.keyboardFocused ? Theme.text : Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                    font.underline: foldCA.hovered
                                    ClickArea { id: foldCA; label: parent.text; showTip: false; onActivated: root.foldDescription() }
                                }
                            }
                            // Drawn as a note is in rich (APP-313), and only
                            // read. As tall as it is: the column scrolls, not
                            // the text; a long one folds to its first lines.
                            // As tall as it is drawn (every row made, so its
                            // height is measured, not estimated); folded, a
                            // window on its first lines.
                            // The editor (Checklist sheet): the description in
                            // the notes' own editor, prefilled with the
                            // tracker's text as markdown; Ctrl+Enter sends it,
                            // Esc leaves it with nothing sent.
                            ColumnLayout {
                                id: descEditBox
                                objectName: "task-doc-desc-editor"
                                visible: root.descEditing
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.spLg
                                Layout.topMargin: Theme.spXs
                                spacing: Theme.spSm
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: Math.max(Theme.px(72), descEditor.contentHeight + 2 * Theme.spSm)
                                    radius: Theme.radiusMd
                                    color: Theme.bg
                                    border.width: 1
                                    border.color: Theme.borderStrong
                                    MdBlockEditor {
                                        id: descEditor
                                        objectName: "task-doc-desc-editor-body"
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: Theme.spSm
                                        height: descEditor.contentHeight
                                        scrolls: false
                                        noteType: true
                                        headingRules: false
                                        wholeDocument: true
                                        rich: true
                                        tailRoom: false
                                        placeholder: I18n.t("taskdoc.edit.descPh")
                                        menuTitle: I18n.t("taskdoc.edit.editDesc").arg(root._trackerName)
                                        onEscaped: root.cancelDescEdit()
                                        onBlockKey: (event) => {
                                            const mods = event.modifiers & ~Qt.KeypadModifier;
                                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (mods & Qt.ControlModifier)) {
                                                root.saveDescEdit();
                                                event.accepted = true;
                                            } else if (event.key === Qt.Key_Escape && mods === Qt.NoModifier) {
                                                root.cancelDescEdit();
                                                event.accepted = true;
                                            }
                                        }
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: Theme.spSm
                                    PillButton {
                                        objectName: "task-doc-desc-save"
                                        solid: true
                                        small: true
                                        text: I18n.t("taskdoc.edit.saveTo").arg(root._trackerName)
                                        keyHint: "Ctrl Enter"
                                        onClicked: root.saveDescEdit()
                                    }
                                    PillButton {
                                        objectName: "task-doc-desc-cancel"
                                        small: true
                                        text: I18n.t("taskdoc.edit.cancel")
                                        keyHint: "Esc"
                                        onClicked: root.cancelDescEdit()
                                    }
                                    Text {
                                        objectName: "task-doc-desc-edit-caption"
                                        width: Math.min(implicitWidth, descEditBox.width)
                                        height: Theme.chipH
                                        verticalAlignment: Text.AlignVCenter
                                        text: I18n.t("taskdoc.edit.noSilent").arg(root._trackerName)
                                        wrapMode: Text.WordWrap
                                        color: Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsXs
                                    }
                                }
                            }
                            // Nothing in the tracker's description yet.
                            Text {
                                objectName: "task-doc-desc-empty"
                                visible: root.isOpen("desc") && !root.descEditing && root._trackerText.length === 0
                                Layout.leftMargin: Theme.spLg
                                Layout.bottomMargin: Theme.spSm
                                text: I18n.t("taskdoc.edit.descEmpty")
                                color: Theme.textDim
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                            }
                            Item {
                                id: descClip
                                visible: root.isOpen("desc") && !root.descEditing && root._trackerText.length > 0
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.spLg
                                Layout.preferredHeight: root._descFolded ? Math.min(trackerBody.contentHeight, root._foldH)
                                                                         : trackerBody.contentHeight
                                clip: true
                                MdBlockEditor {
                                    id: trackerBody
                                    objectName: "task-doc-tracker-body"
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: trackerBody.contentHeight
                                    scrolls: false
                                    text: root._spacedFences(root._trackerText)
                                    readOnly: true
                                    noteType: true
                                    headingRules: false
                                    wholeDocument: true
                                    rich: true
                                    imageTaskId: root._isTicket ? root.taskId : ""
                                    tailRoom: false
                                    menuTitle: I18n.t("taskdoc.fromTracker").arg(root._badge.name || root._ticket.provider || "")
                                    onInternalLinkActivated: (kind, target) => root.internalLinkActivated(kind, target)
                                }
                                // The folded text fades into its button.
                                Rectangle {
                                    visible: root._descFolded
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: Theme.px(48)
                                    gradient: Gradient {
                                        GradientStop { position: 0; color: Theme.withAlpha(Theme.bg, 0) }
                                        GradientStop { position: 1; color: Theme.bg }
                                    }
                                }
                            }
                            // Folded: the screenshots as thumbnails (Heavy sheet).
                            Flow {
                                objectName: "task-doc-desc-thumbs"
                                visible: root.isOpen("desc") && root._descFolded && root._descImages.length > 0 && !root.descEditing
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.spLg
                                Layout.topMargin: Theme.spSm
                                spacing: Theme.spSm
                                Repeater {
                                    model: root._descFolded ? root._descImages : []
                                    delegate: Rectangle {
                                        id: thumb
                                        required property var modelData
                                        readonly property string attId: {
                                            const name = String(thumb.modelData.src).replace(/^.*\//, "");
                                            return /^[0-9a-f]{32}(\.[a-z0-9]{1,12})?$/.test(name) ? name : "";
                                        }
                                        objectName: "task-doc-desc-thumb"
                                        width: Theme.px(132)
                                        height: Theme.px(84)
                                        radius: Theme.radiusMd
                                        color: Theme.panel2
                                        border.width: 1
                                        border.color: thumbCA.hovered || thumbCA.keyboardFocused ? Theme.borderStrong : Theme.border
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            visible: /^(file:|qrc:|image:)/.test(String(thumb.modelData.src))
                                            source: visible ? thumb.modelData.src : ""
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectCrop
                                            sourceSize.width: Theme.px(264)
                                        }
                                        Text {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: Theme.spXs
                                            text: String(thumb.modelData.alt || thumb.modelData.src).replace(/^.*\//, "")
                                            textFormat: Text.PlainText
                                            elide: Text.ElideMiddle
                                            color: Theme.textMuted
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsXs
                                        }
                                        ClickArea {
                                            id: thumbCA
                                            label: String(thumb.modelData.alt || "") || I18n.t("notes.image.open")
                                            showTip: false
                                            onActivated: {
                                                if (thumb.attId.length > 0) trackerBody.openImage(thumb.attId, String(thumb.modelData.alt || ""));
                                                else root.descExpanded = true;
                                            }
                                        }
                                    }
                                }
                            }
                            // "Читать целиком · ещё 1,5 экрана".
                            Text {
                                id: readAll
                                objectName: "task-doc-desc-read-all"
                                visible: root.isOpen("desc") && root._descFolded && !root.descEditing
                                Layout.leftMargin: Theme.spLg
                                Layout.topMargin: Theme.spSm
                                text: I18n.t("taskdoc.desc.readAll").arg(root._screens(Math.max(0, trackerBody.contentHeight - root._foldH)))
                                color: readAllCA.hovered || readAllCA.keyboardFocused ? Theme.text : Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                font.underline: readAllCA.hovered
                                ClickArea {
                                    id: readAllCA
                                    objectName: "task-doc-desc-read-all-area"
                                    label: readAll.text
                                    showTip: false
                                    onActivated: root.descExpanded = true
                                }
                            }
                            // Opened whole: "Конец описания · Свернуть ↑ · К чек-листу 2".
                            Rectangle {
                                objectName: "task-doc-desc-end"
                                visible: root.isOpen("desc") && root._descLong && root.descExpanded && !root.descEditing
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.spMd
                                implicitHeight: endRow.implicitHeight + 2 * Theme.spSm
                                color: "transparent"
                                DashedRect { anchors.fill: parent; radius: Theme.radiusMd; strokeColor: Theme.borderStrong }
                                RowLayout {
                                    id: endRow
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spMd
                                    anchors.rightMargin: Theme.spMd
                                    spacing: Theme.spSm
                                    Text {
                                        text: I18n.t("taskdoc.desc.end")
                                        color: Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsSm
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: I18n.t("taskdoc.desc.foldUp")
                                        color: foldUpCA.hovered || foldUpCA.keyboardFocused ? Theme.text : Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsSm
                                        ClickArea { id: foldUpCA; label: parent.text; showTip: false; onActivated: root.foldDescription() }
                                    }
                                    Text {
                                        visible: checkSection.visible
                                        text: "·"
                                        color: Theme.textDim
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsSm
                                    }
                                    Text {
                                        visible: checkSection.visible
                                        text: I18n.t("taskdoc.desc.toChecklist") + (Style.keyHints ? "  2" : "")
                                        color: toCheckCA.hovered || toCheckCA.keyboardFocused ? Theme.text : Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsSm
                                        ClickArea { id: toCheckCA; label: parent.text; showTip: false; onActivated: root.jumpTo("check") }
                                    }
                                }
                            }
                        }

                        // ── Чек-лист: the ticket's Smart Checklist, read only ──
                        ColumnLayout {
                            id: checkSection
                            objectName: "task-doc-check"
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spMd
                            visible: smartList.totalCount > 0 && root._onDescTab
                            spacing: 0
                            Rectangle {
                                visible: descSection.visible
                                Layout.fillWidth: true
                                Layout.bottomMargin: Theme.spXs
                                Layout.preferredHeight: 1
                                color: Theme.border
                            }
                            TaskBlockHead {
                                Layout.fillWidth: true
                                kind: "check"
                                title: I18n.t("taskdoc.block.check")
                                sub: "Smart Checklist"
                                summary: root._checkSummary
                                open: root.isOpen("check")
                                onToggled: root.toggleBlock("check")
                            }
                            TaskSmartChecklist {
                                id: smartList
                                // Counted while folded too: the header says so.
                                visible: root.isOpen("check") && smartList.totalCount > 0
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.spLg + Theme.spMd
                                showHead: false
                                // Status, text and a new item to the tracker
                                // (owner item 16), while it takes edits.
                                editable: root._canEdit
                                statuses: root._clStatuses
                                onStatusPicked: (index, name) => root.setChecklistStatus(index, name)
                                onTextEdited: (index, text) => root.setChecklistText(index, text)
                                onItemAdded: (text) => root.addChecklistItem(text)
                                items: root._isTicket && root._ticket.details && root._ticket.details.smartChecklist
                                       ? root._ticket.details.smartChecklist : []
                            }
                        }

                        // ── my notes, and on a task of my own its whole text ──
                        // My notes on a tracker card: never sent, never pulled
                        // over. Its own section, labelled like the tracker's.
                        RowLayout {
                            id: myNotesHead
                            objectName: "task-doc-mynotes-head"
                            visible: root._isTicket && root._onDescTab
                            Layout.fillWidth: true
                            Layout.topMargin: descSection.visible || checkSection.visible ? Theme.spSm : 0
                            spacing: Theme.spMd
                            Text {
                                text: I18n.t("taskdoc.myNotes")
                                color: Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                                font.weight: Theme.fwTitle
                            }
                            // A long notepad copied into Knowledge, naming the task.
                            Text {
                                objectName: "task-doc-notes-to-note"
                                visible: body.text.trim().length > 0
                                text: I18n.t("local.notes.toNote")
                                color: toNoteCA.hovered ? Theme.text : Theme.textDim
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                                ClickArea {
                                    id: toNoteCA
                                    label: parent.text
                                    onActivated: {
                                        root.flush();
                                        const title = AppController.copyTaskNotesToNote(root.taskId);
                                        if (title.length > 0) root.internalLinkActivated("note", title);
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // The body is a Tab stop of its own, after the chips
                        // and before Done (A4-14): this stop hands it the caret.
                        Item {
                            id: bodyStop
                            objectName: "task-doc-body-stop"
                            Layout.preferredHeight: 0
                            Layout.preferredWidth: 0
                            activeFocusOnTab: true
                            onActiveFocusChanged: if (activeFocus) {
                                root._passingThrough = true;
                                body.focusEditor();
                            }
                        }

                        // The body, then my plan under it and the slash hint
                        // (DG-062). As tall as it is: the column scrolls.
                        MdBlockEditor {
                            id: body
                            objectName: "task-doc-body"
                            visible: root._onDescTab
                            Layout.fillWidth: true
                            Layout.leftMargin: -Theme.px(24)
                            Layout.rightMargin: -Theme.px(24)
                            Layout.preferredHeight: body.contentHeight
                            scrolls: false
                            clip: true
                            // A tracker card's notes: a short line until typed in.
                            tailRoom: !root._isTicket
                            placeholder: I18n.t("taskdoc.bodyPh")
                            ghostCaret: true
                            menuTitle: I18n.t("textmenu.title.taskBody")
                            // The note look (APP-292): the same md4c drawing as Knowledge.
                            noteType: true
                            headingRules: false
                            wikiTargets: AppController.wikiTargets(body.source.text)
                            wholeDocument: true
                            // Drawn as the note is in rich (APP-313).
                            rich: true
                            onEdited: if (!root._loading) { root._dirtyBody = true; saveTimer.restart(); }
                            onEscaped: root.close()
                            onInternalLinkActivated: (kind, target) => root.internalLinkActivated(kind, target)
                            // Tab in the text moves on to my plan, as everywhere
                            // in the document; it indents only where an indent
                            // means something (F2-REG-1).
                            onBlockKey: (event) => {
                                const isTab = event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab;
                                if (!isTab) {
                                    if (event.key !== Qt.Key_Shift) root._passingThrough = false;
                                    return;
                                }
                                const forward = event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier);
                                if ((event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                                    || root._tabIndents(body.editor, forward)) return;
                                root._tabWithin(forward);
                                event.accepted = true;
                            }
                            tail: Component { DocTail {} }
                        }
                        // The caret kept in view while typing down my notes.
                        Connections {
                            target: body.editor
                            function onCursorRectangleChanged() {
                                if (body.editor.activeFocus)
                                    root._keepInView(body.editor, body.editor.cursorRectangle.y, body.editor.cursorRectangle.height);
                            }
                        }

                        // ── Комментарии (in one or two columns) ──
                        ColumnLayout {
                            id: commentsSection
                            objectName: "task-doc-comments-section"
                            // In three columns the middle one holds them.
                            parent: root._commentsApart ? commentsContent : docContent
                            Layout.fillWidth: true
                            Layout.topMargin: root._commentsApart ? 0 : Theme.spLg
                            // A ticket's: its comments, or at least my draft
                            // of an answer (not the draft's own visibility,
                            // which is this one's).
                            visible: root._isTicket && root._onCommentsTab
                            spacing: Theme.spSm
                            Rectangle {
                                visible: !root._commentsApart && !root.contentTabs
                                Layout.fillWidth: true
                                Layout.bottomMargin: Theme.spXs
                                Layout.preferredHeight: 1
                                color: Theme.border
                            }
                            TaskBlockHead {
                                visible: !root._commentsApart && !root.contentTabs && trackerComments.hasAny
                                Layout.fillWidth: true
                                kind: "comments"
                                title: I18n.t("taskdoc.comments.head")
                                sub: root._badge.name || root._ticket.provider || ""
                                summary: (trackerComments.total > 0 ? trackerComments.total + " · " : "") + I18n.t("taskdoc.comments.newestFirst")
                                open: root.isOpen("comments")
                                onToggled: root.toggleBlock("comments")
                            }
                            // The ticket's own comments, read only.
                            TaskTrackerComments {
                                id: trackerComments
                                Layout.fillWidth: true
                                Layout.leftMargin: root._commentsApart || root.contentTabs ? 0 : Theme.spLg
                                // Folded, the comments are not drawn but still
                                // counted: the header and the tab say how many.
                                folded: !(root._commentsApart || root.contentTabs || root.isOpen("comments"))
                                showHead: root._commentsApart || root.contentTabs
                                taskId: root._isTicket ? root.taskId : ""
                                active: root.opened
                                trackerName: root._badge.name || root._ticket.provider || ""
                            }
                            // My draft of an answer (APP-241), under them.
                            TaskCommentDraft {
                                id: commentDraft
                                Layout.fillWidth: true
                                Layout.leftMargin: trackerComments.Layout.leftMargin
                                visible: root._isTicket && (root._commentsApart || root.contentTabs || root.isOpen("comments"))
                                taskId: root._isTicket ? root.taskId : ""
                                rev: root._rev
                                trackerName: root._badge.name || root._ticket.provider || ""
                            }
                        }

                        // Done, under the text while the properties are
                        // folded away (one column).
                        DoneButton {
                            id: doneBottom
                            objectName: "task-doc-done-bottom"
                            visible: !sideCol.visible
                            Layout.topMargin: Theme.spXl
                            Layout.fillWidth: !root._quiet
                        }
                    }
                }

                // The header of the section being scrolled through stays on
                // top of the column, its fold with it, until the next header
                // pushes it up (Heavy sheet).
                Rectangle {
                    id: stickyBox
                    objectName: "task-doc-sticky"
                    visible: root._sticky.kind.length > 0
                    x: 0
                    y: root._sticky.push
                    z: 2
                    width: scrollHost.width - Theme.spSm
                    implicitHeight: stickyHead.implicitHeight
                    height: implicitHeight
                    color: Theme.bg
                    TaskBlockHead {
                        id: stickyHead
                        objectName: "task-doc-sticky-head"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: root._padX
                        anchors.rightMargin: root._padX - Theme.spSm
                        kind: root._sticky.kind.length > 0 ? root._sticky.kind : "desc"
                        title: stickyHead.kind === "desc" ? I18n.t("taskdoc.block.desc")
                             : stickyHead.kind === "check" ? I18n.t("taskdoc.block.check") : I18n.t("taskdoc.comments.head")
                        summary: stickyHead.kind === "check" ? root._checkSummary : ""
                        open: true
                        // An opened long description folds from here too.
                        Text {
                            objectName: "task-doc-sticky-fold"
                            visible: stickyHead.kind === "desc" && root._descLong && root.descExpanded
                            text: I18n.t("taskdoc.desc.fold")
                            color: stickyFoldCA.hovered || stickyFoldCA.keyboardFocused ? Theme.text : Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSm
                            font.underline: stickyFoldCA.hovered
                            ClickArea { id: stickyFoldCA; label: parent.text; showTip: false; onActivated: root.foldDescription() }
                        }
                        // Folded from here, the section's start comes back
                        // into view, folded.
                        onToggled: {
                            const k = stickyHead.kind;
                            const it = k === "desc" ? descSection : k === "check" ? checkSection : commentsSection;
                            if (k === "desc" && root._descLong && root.descExpanded) { root.foldDescription(); return; }
                            root.toggleBlock(k);
                            Qt.callLater(() => root._scrollTo(it.y));
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.border
                    }
                }
            }
        }

        // ── the comments column (three columns) ──
        Rectangle {
            id: commentsCol
            objectName: "task-doc-comments-col"
            visible: root._commentsApart
            Layout.fillHeight: true
            Layout.preferredWidth: root._flexW - root._docW
            Layout.maximumWidth: root._flexW - root._docW
            color: Theme.panel
            Rectangle {
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: 1
                color: Theme.border
            }
            Flickable {
                id: commentsScroll
                objectName: "task-doc-comments-scroll"
                anchors.fill: parent
                anchors.leftMargin: 1
                contentWidth: width
                contentHeight: commentsContent.implicitHeight + 2 * Theme.spXl
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                QQC.ScrollBar.vertical: ThinScrollBar {}
                ColumnLayout {
                    id: commentsContent
                    objectName: "task-doc-comments-content"
                    x: Theme.spXl
                    y: Theme.spXl
                    width: commentsScroll.width - 2 * Theme.spXl
                    spacing: 0
                }
            }
        }

        // ── the properties column: Код, Треды, Владельцы кода, Упоминается
        // в, Время, История, each folding by its header; Готово pinned under
        // them, always on screen ──
        Rectangle {
            id: sideCol
            objectName: "task-doc-side"
            Layout.fillHeight: true
            Layout.preferredWidth: root._sideW
            visible: root._sideShown
            color: Theme.panel
            Rectangle {
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: 1
                color: Theme.border
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 1
                spacing: 0
                Flickable {
                    id: sideScroll
                    objectName: "task-doc-side-scroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: sideContent.implicitHeight + 2 * Theme.spLg
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    QQC.ScrollBar.vertical: ThinScrollBar {}
                    // What has the keyboard here stays in view: a thread
                    // walked with ↑↓, a block header reached with Tab.
                    function keepInView(it) {
                        if (!it) return;
                        let inside = false;
                        for (let p = it; p; p = p.parent) if (p === sideContent) { inside = true; break; }
                        if (!inside) return;
                        const p = it.mapToItem(sideContent, 0, 0);
                        const max = Math.max(0, sideScroll.contentHeight - sideScroll.height);
                        if (p.y < sideScroll.contentY) sideScroll.contentY = Math.max(0, p.y - Theme.spLg);
                        else if (p.y + it.height > sideScroll.contentY + sideScroll.height)
                            sideScroll.contentY = Math.min(max, p.y + it.height - sideScroll.height + Theme.spLg);
                    }
                    Connections {
                        target: root.Window.window
                        function onActiveFocusItemChanged() { sideScroll.keepInView(root.Window.activeFocusItem); }
                    }

                    ColumnLayout {
                        id: sideContent
                        x: Theme.spLg
                        y: Theme.spLg
                        width: sideScroll.width - 2 * Theme.spLg
                        spacing: Theme.spXs

                        // Код: the branch, the PR and its CI, the tracker.
                        SideBlock {
                            id: codeBlock
                            objectName: "task-doc-code"
                            kind: "code"
                            title: I18n.t("taskdoc.code")
                            visible: root._hasCode
                            summary: root._ciState.length > 0 ? root._ciWords : root._prNumber > 0 ? "#" + root._prNumber : ""
                            SideRow {
                                objectName: "task-doc-branch"
                                visible: String(root.task.branch || "").length > 0
                                label: I18n.t("taskdoc.branch")
                                value: String(root.task.branch || "")
                                mono: true
                            }
                            // "!2342  ● CI прошёл  f710078 · 22 ч": the dot as
                            // the card's (Threads sheet), filled when it passed
                            // or failed, a ring while it runs.
                            SideRow {
                                id: prRow
                                objectName: "task-doc-pr"
                                visible: root._prNumber > 0
                                readonly property string ref: String(root._ci.ref || "").length > 0 ? root._ci.ref : "#" + root._prNumber
                                label: I18n.t("taskdoc.pr")
                                value: prRow.ref
                                link: String(root.task.prUrl || "")
                                Row {
                                    visible: root._ciWords.length > 0
                                    spacing: Theme.spSm
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.spXs
                                        Rectangle {
                                            objectName: "task-doc-pr-ci-mark"
                                            readonly property string state_: root._ciState
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Theme.px(8)
                                            height: Theme.px(8)
                                            radius: width / 2
                                            readonly property color ink: root._ciInk
                                            color: root._ciState === "running" || root._ciState === "unknown" ? "transparent" : ink
                                            border.width: root._ciState === "running" || root._ciState === "unknown" ? 1.5 : 0
                                            border.color: ink
                                            Accessible.ignored: true
                                        }
                                        Text {
                                            objectName: "task-doc-pr-ci"
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: root._ciWords
                                            textFormat: Text.PlainText
                                            color: root._ciState === "success" || root._ciState === "failed" ? root._ciInk : Theme.text
                                            font.family: Theme.fontUi
                                            font.features: Theme.tabularNums
                                            font.pixelSize: Theme.fsSm
                                        }
                                    }
                                    Text {
                                        objectName: "task-doc-pr-ci-when"
                                        visible: text.length > 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: root._ciWhen
                                        textFormat: Text.PlainText
                                        color: Theme.textMuted
                                        font.family: Theme.fontMono
                                        font.features: Theme.tabularNums
                                        font.pixelSize: Theme.fsXs
                                    }
                                }
                            }
                            SideRow {
                                objectName: "task-doc-tracker"
                                visible: root._isTicket
                                label: I18n.t("taskdoc.tracker")
                                value: root._trackerLabel
                                link: String(root.task.externalUrl || "")
                                arrow: true
                            }
                            // CI of the merge / pull request (APP-318): the
                            // failed jobs and their logs, on demand.
                            TaskCiPanel {
                                Layout.fillWidth: true
                                taskId: root.taskId
                                rev: root._rev
                            }
                        }

                        // Треды: waiting on me first, then on them.
                        SideBlock {
                            objectName: "task-doc-threads"
                            kind: "threads"
                            title: I18n.t("taskdoc.block.threads")
                            visible: threadsPanel.all.length > 0
                            summary: {
                                const bits = [];
                                if (threadsPanel.onMeCount > 0)
                                    bits.push("<font color=\"" + Theme.warning + "\">" + I18n.t("taskdoc.block.onMe").arg(threadsPanel.onMeCount) + "</font>");
                                if (threadsPanel.onThemCount > 0) bits.push(I18n.t("taskdoc.block.onThem").arg(threadsPanel.onThemCount));
                                return bits.join(" · ");
                            }
                            // Threads without an answer, mine to answer first.
                            TaskThreadsPanel {
                                id: threadsPanel
                                Layout.fillWidth: true
                                taskId: root.taskId
                                showTitle: false
                            }
                        }

                        // Владельцы кода: who owns the PR's changes.
                        SideBlock {
                            objectName: "task-doc-owners"
                            kind: "owners"
                            title: I18n.t("taskdoc.block.owners")
                            visible: ownersPanel.known || ownersPanel.reason.length > 0
                            summary: ownersPanel.reason === "off" ? I18n.t("taskdoc.block.off")
                                   : ownersPanel.toPing.length > 0 ? I18n.t("taskdoc.block.toPing").arg(ownersPanel.toPing.length)
                                   : ownersPanel.requirements.length > 0 ? I18n.t("taskdoc.block.allSet") : ""
                            TaskOwnersPanel {
                                id: ownersPanel
                                Layout.fillWidth: true
                                taskId: root.taskId
                                showTitle: false
                            }
                        }

                        // Упоминается в: notes that name the task.
                        SideBlock {
                            id: mentionsBox
                            objectName: "task-doc-mentions"
                            kind: "mentions"
                            title: I18n.t("taskdoc.mentioned")
                            readonly property var notes: root._rev >= 0 ? AppController.notesMentioningTask(root.taskId) : []
                            visible: mentionsBox.notes.length > 0
                            summary: I18n.count(mentionsBox.notes.length, "taskdoc.block.notes")
                            Repeater {
                                model: mentionsBox.notes
                                delegate: RowLayout {
                                    id: mention
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: Theme.spSm
                                    Text {
                                        Layout.maximumWidth: mentionsBox.width * 0.65
                                        text: mention.modelData.title
                                        elide: Text.ElideRight
                                        color: Theme.text
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsSm
                                        font.underline: true
                                        ClickArea {
                                            label: mention.modelData.title
                                            onActivated: root.internalLinkActivated("note", mention.modelData.title)
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "· " + (root._valid(mention.modelData.updated) ? root._relDay(mention.modelData.updated)
                                                                                             : I18n.fmtDate(mention.modelData.updated, "dayMonth"))
                                        elide: Text.ElideRight
                                        color: Theme.textMuted
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsSm
                                    }
                                }
                            }
                        }

                        // Links drawn by hand (APP-240): only while there are any.
                        TaskRelations {
                            id: relations
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spXs
                            Layout.bottomMargin: Theme.spXs
                            taskId: root.taskId
                            rev: root._rev
                            visible: relations.links.length > 0 || relations._adding.length > 0
                            onOpenTask: (id) => root.openOther(id)
                        }

                        // Время: the live timer, Пауза t, all of it against the estimate.
                        SideBlock {
                            id: timeBox
                            objectName: "task-doc-time"
                            kind: "time"
                            title: I18n.t("taskdoc.time")
                            property int _tick: 0
                            Timer { interval: 1000; repeat: true; running: !!root.task.isTiming && root.opened; onTriggered: timeBox._tick++ }
                            readonly property string clock: {
                                const s = timeBox._tick >= 0 && root.task.isTiming ? AppController.elapsedSecondsFor(root.taskId) : 0;
                                const m = Math.floor(s / 60);
                                return Math.floor(m / 60) + ":" + String(m % 60).padStart(2, "0");
                            }
                            // Everything tracked, the running session included.
                            readonly property int totalMin: Math.round(((timeBox._tick >= 0 ? root.task.trackedSeconds || 0 : 0)
                                                                       + (root.task.isTiming ? AppController.elapsedSecondsFor(root.taskId) : 0)) / 60)
                            summary: root.task.isTiming ? timeBox.clock
                                   : timeBox.totalMin > 0 ? AppController.formatDuration(timeBox.totalMin, root._estProvider)
                                   : I18n.t("taskdoc.block.noTime") + (Style.keyHints ? " · " + root._keyOf("task.timer") : "")
                            RowLayout {
                                spacing: Theme.spLg
                                Text {
                                    objectName: "task-doc-timer"
                                    visible: !!root.task.isTiming
                                    text: timeBox.clock
                                    color: Theme.signalNow
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.typeStep(3)
                                }
                                PillButton {
                                    objectName: "task-doc-timer-toggle"
                                    text: root.task.isTiming ? I18n.t("taskdoc.pause") : I18n.t("taskcard.startTimer")
                                    keyHint: root._keyOf("task.timer")
                                    small: true
                                    onClicked: root.task.isTiming ? AppController.stopTaskTimer(root.taskId) : AppController.startTaskTimer(root.taskId)
                                }
                            }
                            // "всего 3 ч 10 мин из оценки 4 ч".
                            Text {
                                objectName: "task-doc-time-line"
                                Layout.fillWidth: true
                                visible: text.length > 0
                                wrapMode: Text.WordWrap
                                text: {
                                    const bits = [];
                                    // In the chip's units ("1ч 30м", Duration), and
                                    // with nothing tracked only the estimate.
                                    const fmt = (m) => AppController.formatDuration(m, root._estProvider);
                                    const est = root.task.estimateMinutes || 0;
                                    if (timeBox.totalMin > 0)
                                        bits.push(I18n.t("taskdoc.tracked").arg(fmt(timeBox.totalMin))
                                                  + (est > 0 ? " " + I18n.t("taskdoc.ofEstimate").arg(fmt(est)) : ""));
                                    else if (est > 0)
                                        bits.push(I18n.t("local.card.estimate").arg(fmt(est)));
                                    return bits.join(" · ");
                                }
                                color: Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                            }
                            // Session by session (APP-251), folded.
                            TaskSessions {
                                Layout.fillWidth: true
                                taskId: root.taskId
                                rev: root._rev
                            }
                        }

                        // История: the newest facts.
                        SideBlock {
                            objectName: "task-doc-history"
                            kind: "history"
                            title: I18n.t("taskdoc.history")
                            visible: root._historyRows.length > 0
                            last: true
                            summary: root._history.length > 0
                                     ? root._history.length + " · " + root._histWhen(root._history[0].at, false)
                                     : root._historyRows.length > 0 ? root._historyRows[0].split(" · ")[0] : ""
                            Repeater {
                                model: root._historyRows
                                delegate: Text {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: modelData
                                    elide: Text.ElideRight
                                    color: Theme.textMuted
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.fsSm
                                }
                            }
                        }

                        // "Клик или Space на заголовке — свернуть. Свёрнутое
                        // запоминается для всех задач."
                        Text {
                            objectName: "task-doc-block-hint"
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spSm
                            text: I18n.t("taskdoc.block.hint")
                            wrapMode: Text.WordWrap
                            color: Theme.textDim
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsXs
                        }
                    }
                }
                // Готово d, under the blocks, never scrolled away.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }
                DoneButton {
                    id: doneSide
                    objectName: "task-doc-done-bold"
                    Layout.fillWidth: true
                    Layout.margins: Theme.spLg
                }
            }
        }

        // The rest of the area, empty, where the text keeps its width.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    // ── menus ──
    AppMenu {
        id: statusMenu
        Instantiator {
            model: AppController.statuses
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.name
                marked: modelData.id === root.task.status
                note: modelData.id === root.task.status ? I18n.t("taskdoc.now") : ""
                onTriggered: AppController.moveTaskTo(root.taskId, modelData.id, "")
            }
            onObjectAdded: (i, o) => statusMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => statusMenu.removeItem(o)
        }
    }
    AppMenu {
        id: priorityMenu
        Instantiator {
            model: ["P0", "P1", "P2", "P3"]
            delegate: AppMenuItem {
                required property string modelData
                text: modelData
                marked: modelData === root.task.priority
                onTriggered: AppController.setTaskPriority(root.taskId, modelData)
            }
            onObjectAdded: (i, o) => priorityMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => priorityMenu.removeItem(o)
        }
    }
    AppMenu {
        id: recurMenu
        Instantiator {
            model: root._recurOptions
            delegate: AppMenuItem {
                required property string modelData
                objectName: modelData === "custom" ? "task-doc-recur-custom-row" : ""
                text: modelData === "" ? I18n.t("taskdoc.repeat.none")
                    : modelData === "custom" ? I18n.t("recur.custom") : root._recurLabel(modelData)
                marked: String(root.task.recurrence || "") === modelData
                onTriggered: modelData === "custom" ? root.startCustomRecur() : root._setField("recurrence", modelData)
            }
            onObjectAdded: (i, o) => recurMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => recurMenu.removeItem(o)
        }
    }
    // + свойство: the empty properties, then the local layer's extras that
    // the sheets give no place at rest (DG-061): the plan and a link.
    AppMenu {
        id: addMenu
        objectName: "task-doc-add-menu"
        Instantiator {
            model: ["scheduled", "due", "labels", "tags", "recurrence", "estimate", "remind", "plan", "link"]
            delegate: AppMenuItem {
                required property string modelData
                visible: modelData === "plan" ? !(root._plan && root._plan.visible)
                       : modelData === "link" ? true : !root._visible(modelData)
                height: visible ? implicitHeight : 0
                text: I18n.t("taskdoc.add." + modelData)
                onTriggered: {
                    if (modelData === "plan") { root.startPlan(); return; }
                    if (modelData === "link") { root.startLink("related"); return; }
                    if (modelData === "estimate") { root.openEstimate(); return; }
                    root._shown = root._shown.concat([modelData]);
                    if (modelData === "tags") { root._tagsEditing = true; Qt.callLater(tagsEditor.open); }
                    if (modelData === "remind") Qt.callLater(remindChip.clicked);
                }
            }
            onObjectAdded: (i, o) => addMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => addMenu.removeItem(o)
        }
    }

    // What goes under the body, inside its scroll: my plan, the slash hint,
    // and on a tracker card the answer I am writing (APP-241).
    component DocTail: ColumnLayout {
        id: tail
        readonly property alias plan: checklist
        // In line with the body's blocks.
        x: 0
        spacing: Theme.spLg
        TaskLocalChecklist {
            id: checklist
            Layout.fillWidth: true
            taskId: root.taskId
            rev: root._rev
            onOpenTask: (id) => root.openOther(id)
            onTabOut: (forward) => root._tabWithin(forward)
        }
        // "Пишите прямо здесь. / — вставить чек-лист, код, ссылку на задачу."
        Text {
            objectName: "task-doc-slash-hint"
            Layout.fillWidth: true
            Layout.topMargin: Theme.spXs
            visible: !root._quiet && body.text.trim().length > 0
            text: I18n.t("taskdoc.hint.write") + " <b><font face=\"" + Theme.fontMono + "\" color=\"" + Theme.text + "\">/</font></b> "
                  + I18n.t("taskdoc.hint.slash")
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsLg
            // The hint is where the text goes on: a click there types at
            // the end, and "/" opens the insert menu.
            ClickArea {
                objectName: "task-doc-slash-hint-click"
                label: I18n.t("taskdoc.hint.write")
                showTip: false
                cursorShape: Qt.IBeamCursor
                onActivated: body.appendBlock()
            }
        }
    }

    // "● 3 готово · ● 1 в работе · 4 осталось" (the Checklist sheet's dots).
    readonly property string _checkSummary: {
        const dot = (c) => "<font color=\"" + c + "\">●</font> ";
        const bits = [];
        if (smartList.doneCount > 0) bits.push(dot(Theme.success) + I18n.t("taskdoc.block.done").arg(smartList.doneCount));
        if (smartList.progressCount > 0) bits.push(dot(Theme.info) + I18n.t("taskdoc.block.progress").arg(smartList.progressCount));
        bits.push(I18n.t("taskdoc.block.left").arg(smartList.leftCount));
        return bits.join(" · ");
    }
    // The CI's colour, as the card's dot: passed, failed, or quiet.
    readonly property color _ciInk: root._ciState === "success" ? Theme.success
                                  : root._ciState === "failed" ? Theme.danger
                                  : root._ciState === "running" ? Theme.info : Theme.textDim

    // A block of the properties column: its header, then what it holds,
    // a hairline under it.
    component SideBlock: ColumnLayout {
        id: sb
        property string kind: ""
        property string title: ""
        property string summary: ""
        property bool last: false
        default property alias content: sbBody.data
        readonly property bool open: root.isOpen(sb.kind)
        Layout.fillWidth: true
        spacing: 0
        TaskBlockHead {
            Layout.fillWidth: true
            kind: sb.kind
            title: sb.title
            summary: sb.summary
            compact: true
            open: sb.open
            onToggled: root.toggleBlock(sb.kind)
        }
        ColumnLayout {
            id: sbBody
            objectName: "task-block-body-" + sb.kind
            visible: sb.open
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spXl
            Layout.bottomMargin: Theme.spSm
            spacing: Theme.spSm
        }
        Rectangle {
            visible: !sb.last
            Layout.fillWidth: true
            Layout.topMargin: Theme.spXs
            Layout.preferredHeight: 1
            color: Theme.border
        }
    }

    // A labelled button of the panel's header: "⤢ На весь экран f".
    component HeadButton: Rectangle {
        id: hb
        property string icon: ""
        property string text: ""
        property string keyText: ""
        property string tip: ""
        property bool strong: false
        signal activated()
        implicitWidth: hbRow.implicitWidth + 2 * Theme.spMd
        implicitHeight: Theme.chipH
        radius: Theme.radiusMd
        color: hbCA.hovered ? Theme.panel3 : Theme.panel2
        border.width: 1
        border.color: hbCA.keyboardFocused ? Theme.focusRing : Theme.border
        Row {
            id: hbRow
            anchors.centerIn: parent
            spacing: Theme.spSm
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: hb.icon
                color: hb.strong || hbCA.hovered ? Theme.text : Theme.textMuted
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: hb.text
                color: Theme.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
            Text {
                visible: Style.keyHints && hb.keyText.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: hb.keyText
                color: Theme.textMuted
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsXs
            }
        }
        ClickArea {
            id: hbCA
            objectName: hb.objectName + "-area"
            label: hb.text
            tip: hb.tip
            showTip: hb.tip.length > 0
            onActivated: hb.activated()
        }
    }

    // The (i) beside "Описание": why the tracker's text takes no typing
    // (APP-313/314), and where the first tracker text's coach mark hangs.
    component WhyMark: Rectangle {
        objectName: "task-doc-tracker-why"
        width: Theme.px(15)
        height: Theme.px(15)
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: whyCA.hovered ? Theme.textMuted : Theme.borderStrong
        // The "i" drawn, not set: a glyph sits on the font's baseline and
        // came out low and to one side.
        Column {
            anchors.centerIn: parent
            spacing: Math.max(1, Math.round(parent.height * 0.08))
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(2, Math.round(parent.parent.width * 0.14))
                height: width
                radius: width / 2
                color: whyCA.hovered ? Theme.text : Theme.textDim
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(2, Math.round(parent.parent.width * 0.12))
                height: Math.round(parent.parent.height * 0.36)
                radius: width / 2
                color: whyCA.hovered ? Theme.text : Theme.textDim
            }
        }
        ClickArea {
            id: whyCA
            label: I18n.t("taskdoc.fromTracker.readOnly").arg(root._badge.name || root._ticket.provider || "")
        }
        // The first tracker text opened (coach mark): whose text this is,
        // then where the user's own goes.
        CoachMarkHost {
            markId: "trackerText"
            inPanel: true
            when: root.opened && trackerHead.visible && !root._canEdit && !root._trackerOff
                  && String(AppController.coachMark.taskId || "") === root.taskId
            sourceComponent: TrackerTextCoachmark {
                hint: AppController.coachMark
                headAnchor: trackerHead
                notesAnchor: myNotesHead
            }
        }
    }

    // Готово d: filled neutral in bold (H2-Task), a small outline in quiet.
    component DoneButton: PillButton {
        // A control takes Tab itself: without these it left the document for
        // the sidebar (VERIFY-A-1).
        Keys.onTabPressed: (e) => { root._tabWithin(true); e.accepted = true; }
        Keys.onBacktabPressed: (e) => { root._tabWithin(false); e.accepted = true; }
        text: root._done ? I18n.t("taskmenu.reopen") : I18n.t("taskmenu.done")
        shortcutId: "task.done"
        keyHint: root._quiet ? "" : root._keyOf("task.done")
        solid: !root._quiet
        // A double click is one Done, not Done and back (IDIOT-DOC-20).
        onClicked: {
            const now = Date.now();
            if (now - root._doneClickAt < 500) return;
            root._doneClickAt = now;
            AppController.toggleDone([root.taskId]);
        }
    }
    // A chip whose value is typed: a click turns it into a field.
    component TextChip: PropertyChip {
        id: tc
        signal committed(string text)
        property bool _editing: false
        onClicked: { tc._editing = true; tcField.text = tc.value; tcField.forceActiveFocus(); tcField.selectAll(); }
        QQC.TextField {
            id: tcField
            visible: tc._editing
            anchors.fill: parent
            leftPadding: Theme.spMd
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
            color: Theme.text
            background: Rectangle { radius: Theme.radiusMd; color: Theme.panel2; border.color: Theme.focusRing; border.width: 1 }
            onAccepted: { tc._editing = false; tc.committed(tcField.text); root.focusDocument(); }
            onActiveFocusChanged: if (!activeFocus && tc._editing) { tc._editing = false; tc.committed(tcField.text); }
            // Hidden, the field kept the keyboard: what was typed went
            // nowhere and Esc no longer closed the document (IDIOT-DOC-6).
            Keys.onEscapePressed: (e) => { tc._editing = false; root.focusDocument(); e.accepted = true; }
            Keys.onReturnPressed: (e) => { tcField.accepted(); e.accepted = true; }
            Keys.onEnterPressed: (e) => { tcField.accepted(); e.accepted = true; }
        }
    }
    // A date chip: typed words ("завтра 15:00", "пт"), read like the input.
    component DateChip: TextChip {
        property string field: ""
        onCommitted: (text) => root._applyDate(field, text)
    }
    // "Ветка  fix/login-throttle" (bold: a label column; quiet: the value
    // alone, as Q-Task writes it).
    component SideRow: RowLayout {
        id: sr
        property string label: ""
        property string value: ""
        property string link: ""
        property bool mono: false
        property bool arrow: false
        default property alias extra: tailRow.data
        Layout.fillWidth: true
        spacing: Theme.spMd
        Text {
            visible: !root._quiet
            text: sr.label
            Layout.preferredWidth: Theme.px(52)
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsMd
        }
        Row {
            id: tailRow
            Layout.fillWidth: true
            Layout.preferredWidth: Theme.px(40)
            spacing: Theme.spSm
            Text {
                id: valueText
                width: Math.min(implicitWidth, tailRow.width - (arrowIcon.visible ? arrowIcon.width + tailRow.spacing : 0))
                text: sr.value
                elide: Text.ElideRight
                color: root._quiet ? Theme.textMuted : Theme.text
                font.family: sr.mono ? Theme.fontMono : Theme.fontUi
                font.pixelSize: sr.mono ? Theme.fsSm : (root._quiet ? Theme.fsMd : Theme.fsSm)
                font.underline: sr.link.length > 0 && (root._quiet || !sr.mono)
                ClickArea {
                    enabled: sr.link.length > 0
                    label: sr.value
                    onActivated: Qt.openUrlExternally(sr.link)
                }
            }
            Icon {
                id: arrowIcon
                visible: sr.arrow
                anchors.verticalCenter: valueText.verticalCenter
                name: "arrow-out"
                size: Theme.iconSize - 2
                color: Theme.textMuted
            }
        }
    }
}
