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

    // ── opening and closing ──
    function open(id, intoTitle) {
        if (root.opened && id !== root.taskId) root.flush();
        root._loadedId = "";
        root._profileId = AppController.activeProfileId;
        root.taskId = id;
        root._load();
        // The tracker text from its top, measured afresh: it kept the place
        // it was scrolled to, and the height it had shrunk to (0.9.4).
        trackerBody.scrollToTop();
        root._trackerFullH = trackerBody.contentHeight;
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
    // Write what is typed now (before a switch, a quit, a close).
    function flush() {
        if (root._draft) root._draft.flush();
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
        if (!titleField.activeFocus && !root._dirtyTitle && titleField.text !== root.task.title) {
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
    readonly property string _prChecks: root._exists ? String(root.task.prChecks || "") : ""
    readonly property string _trackerLabel: root._isTicket
        ? ((root._badge.name || root._ticket.provider || "") + " " + (root.task.externalKey || "")).trim() : ""

    // The plan and the comment draft live in the body's tail (see below).
    readonly property Item _tail: body.tailItem
    readonly property var _plan: root._tail ? (root._tail as DocTail).plan : null
    readonly property var _draft: root._tail ? (root._tail as DocTail).draft : null

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
            visible: !root.full
            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 1
            color: Theme.border
        }
    }
    // Clicks stay in the panel.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = false }

    // The sheets' column: 780 px of text (760 quiet), then the meta column.
    readonly property int _docMax: root._quiet ? Theme.px(760) : Theme.px(780)
    // A narrow panel (a 1280 px window with the board beside it) keeps its
    // width for the text: smaller margins, and the meta column only where
    // the text still gets 600 px (APP-314: 440 px of text beside a column
    // of mostly empty space).
    readonly property bool _narrow: root.width < Theme.px(1000)
    readonly property int _padX: root._narrow ? Theme.px(28) : root._quiet ? Theme.px(48) : Theme.px(40)
    readonly property int _sideW: root._quiet ? Theme.px(280) : Theme.px(300)
    // A short panel (a 720p screen at 125 %): tighter margins and smaller
    // floors for the tracker text and my notes, so a long ticket still leaves
    // my notes, the comments and Done on the panel (0.9.4).
    readonly property bool _short: root.height < Theme.px(640)
    // The tracker text's full height, as tall as it has measured (see the
    // tracker box): reset when its text changes or the document opens.
    property real _trackerFullH: 0

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── the document ──
        ColumnLayout {
            Layout.fillWidth: true
            // The sheet's 780 includes its padding.
            Layout.maximumWidth: root.full ? root._docMax - 2 * root._padX : -1
            Layout.fillHeight: true
            Layout.leftMargin: root._padX
            Layout.rightMargin: root._padX
            Layout.topMargin: root._short ? Theme.spMd : root._quiet ? Theme.px(40) : Theme.px(22)
            spacing: 0

            // Задачи / В работе / APP-101          Сохранено  Esc назад  ⤢
            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: root._short ? Theme.spMd : Theme.px(26)
                Layout.preferredHeight: Theme.chipHSmall
                spacing: Theme.spMd
                Text {
                    objectName: "task-doc-crumbs-root"
                    text: I18n.t("sidebar.tasks")
                    color: crumbCA.hovered ? Theme.text : (root._quiet ? Theme.textMuted : Theme.text)
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
                    text: "/" + (root._quiet ? "" : "  " + root._statusName(root.task.status || "") + "   /")
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Text {
                    // The name the card shows: the tracker key (A3-10, A1-29).
                    objectName: "task-doc-crumb-id"
                    text: root.task.externalKey || root.taskId
                    color: crumbIdCA.hovered ? Theme.text : Theme.textMuted
                    font.family: Theme.fontMono
                    ClickArea {
                        id: crumbIdCA
                        objectName: "task-doc-crumb-id-copy"
                        label: I18n.t("taskcard.copyId")
                        tip: I18n.t("taskcard.copyId.tip")
                        activeFocusOnTab: false
                        onActivated: AppController.copyTaskKey(parent.text)
                    }
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
                Row {
                    visible: Style.keyHints
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
                // Panel ↔ the whole content area (Enter).
                Item {
                    objectName: "task-doc-full"
                    implicitWidth: Theme.chipHSmall; implicitHeight: Theme.chipHSmall
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSm
                        color: fullCA.hovered ? Theme.panel2 : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    }
                    Icon {
                        anchors.centerIn: parent
                        name: root.full ? "collapse" : "expand"
                        color: fullCA.hovered ? Theme.text : Theme.textMuted
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    }
                    ClickArea {
                        id: fullCA
                        label: root.full ? I18n.t("taskdoc.panel") : I18n.t("taskdoc.full")
                        onActivated: root.full = !root.full
                    }
                }
            }

            // A pull changed the text while you were in it: said quietly.
            Text {
                objectName: "task-doc-changed"
                Layout.fillWidth: true
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

            QQC.TextArea {
                id: titleField
                objectName: "task-doc-title"
                Layout.fillWidth: true
                Layout.bottomMargin: root._quiet ? Theme.px(14) : Theme.px(16)
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                selectByMouse: true
                color: Theme.text
                font.family: Theme.fontDisplay
                // 32 px bold, 28 px quiet (H2-Task / Q-Task); a step or two
                // down for a title of more than three lines at that size: one
                // of seven lines took most of a 720 px panel (F3-EYES-10).
                font.pixelSize: Theme.typeStep((root._quiet ? 7 : 8) - root._titleStepsDown)
                font.weight: root._quiet ? Theme.fwTitle : Theme.fwHeading
                padding: 0
                // Basic binds its own side padding; the title shares the
                // chips' left edge (R3-037).
                leftPadding: 0
                rightPadding: 0
                background: Item {}
                placeholderText: I18n.t("taskdoc.titlePh")
                placeholderTextColor: Theme.textDim
                onTextChanged: if (!root._loading) { root._dirtyTitle = true; saveTimer.restart(); }
                // Left empty, the stored title comes back into view rather
                // than silently on the next open (IDIOT-DOC-15).
                onActiveFocusChanged: if (!activeFocus && root.opened && root._exists && root._cleanTitle(titleField.text).length === 0) {
                    root._loading = true; titleField.text = root.task.title; root._loading = false;
                    root._dirtyTitle = false;
                }
                Keys.onReturnPressed: (e) => { body.focusEditor(); e.accepted = true; }
                Keys.onEnterPressed: (e) => { body.focusEditor(); e.accepted = true; }
                // Tab moves on, as in any form; it typed a tab into the
                // title (IDIOT-DOC-13).
                Keys.onTabPressed: (e) => { root._tabWithin(true); e.accepted = true; }
                Keys.onBacktabPressed: (e) => { root._tabWithin(false); e.accepted = true; }
                Keys.onEscapePressed: root.close()
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
                Layout.bottomMargin: root._quiet ? Theme.px(30) : Theme.px(26)
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

            // With the meta column folded away, what it would say about the
            // tracker and the notes still shows (A1-29, A4-13).
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
                    // The whole document has room for the meta column.
                    ClickArea { id: mentionsCA; label: parent.text; onActivated: root.full = true }
                }
                Item { Layout.fillWidth: true }
            }

            // Mine over the tracker's (APP-238): what the tracker says, said
            // quietly, and "changed in the tracker" when it moved since I
            // set mine — never overwriting mine. Reset is one action.
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

            // The tracker's text, read-only, above my notes (APP-237).
            // Both this block and my notes fill the height: a Layout keeps an
            // item that does not fill at its preferred height and never
            // shrinks it, so a long ticket on a small screen pushed my notes,
            // the comments and Done off the panel (0.9.4).
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: root._isTicket && root._trackerText.length > 0
                Layout.verticalStretchFactor: 3
                Layout.bottomMargin: root._short ? Theme.spSm : Theme.spLg
                visible: root._isTicket && root._trackerText.length > 0
                spacing: 0
                // One line over the text, why it takes no typing (APP-313)
                // in the tip of the mark beside it (APP-314: two lines of
                // explanation sat on the description and read as part of it).
                RowLayout {
                    id: trackerHead
                    objectName: "task-doc-tracker-readonly"
                    Layout.bottomMargin: root._short ? Theme.spXs : Theme.spLg
                    spacing: Theme.spSm
                    Text {
                        text: I18n.t("taskdoc.fromTracker.short").arg(root._badge.name || root._ticket.provider || "")
                        color: Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                        font.weight: Theme.fwTitle

                        // The first tracker text opened (coach mark): whose
                        // text this is, then where the user's own goes.
                        CoachMarkHost {
                            markId: "trackerText"
                            inPanel: true
                            when: root.opened && trackerHead.visible
                                  && String(AppController.coachMark.taskId || "") === root.taskId
                            sourceComponent: TrackerTextCoachmark {
                                hint: AppController.coachMark
                                headAnchor: trackerHead
                                notesAnchor: myNotesHead
                            }
                        }
                    }
                    Rectangle {
                        objectName: "task-doc-tracker-why"
                        Layout.preferredWidth: Theme.px(15)
                        Layout.preferredHeight: Theme.px(15)
                        radius: width / 2
                        color: "transparent"
                        border.width: 1
                        border.color: whyCA.hovered ? Theme.textMuted : Theme.borderStrong
                        // The "i" drawn, not set: a glyph sits on the font's
                        // baseline and came out low and to one side.
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
                    }
                }
                // Drawn as a note is in rich (APP-313), and only read: the
                // text can be selected and copied across its lines, a box is
                // not ticked, nothing goes back to the tracker.
                MdBlockEditor {
                    id: trackerBody
                    objectName: "task-doc-tracker-body"
                    Layout.fillWidth: true
                    Layout.leftMargin: -Theme.px(24)
                    Layout.rightMargin: -Theme.px(24)
                    // Half the document at most (a long ticket scrolls inside);
                    // with nothing in my notes yet it takes most of the page
                    // (APP-314: the text scrolled in a box over empty space).
                    // It grows to that cap from a few lines, taking the room
                    // first; on a small screen it gives way down to them and
                    // scrolls, my notes and Done stay on the panel (0.9.4).
                    Layout.fillHeight: true
                    Layout.verticalStretchFactor: 3
                    Layout.maximumHeight: Math.min(Math.max(Theme.px(320),
                        root.height * (body.text.trim().length > 0 ? 0.45 : 0.62)), root._trackerFullH)
                    Layout.preferredHeight: Math.min(root._short ? Theme.px(64) : Theme.px(96), root._trackerFullH)
                    Layout.minimumHeight: Math.min(root._short ? Theme.px(64) : Theme.px(96), root._trackerFullH)
                    // The list's content height is an estimate that shrinks
                    // as a long text is scrolled: the box followed it down to
                    // nothing (0.9.4). The tallest seen for this text holds.
                    onContentHeightChanged: root._trackerFullH = Math.max(root._trackerFullH, trackerBody.contentHeight)
                    onTextChanged: root._trackerFullH = trackerBody.contentHeight
                    clip: true
                    text: root._spacedFences(root._trackerText)
                    readOnly: true
                    noteType: true
                    headingRules: false
                    wholeDocument: true
                    rich: true
                    imageTaskId: root._isTicket ? root.taskId : ""
                    tailRoom: false
                    scrollCue: true
                    menuTitle: I18n.t("taskdoc.fromTracker").arg(root._badge.name || root._ticket.provider || "")
                    onInternalLinkActivated: (kind, target) => root.internalLinkActivated(kind, target)
                }
            }
            // My notes on a tracker card: never sent, never pulled over.
            // Its own section, labelled like the tracker's (APP-314).
            RowLayout {
                id: myNotesHead
                objectName: "task-doc-mynotes-head"
                visible: root._isTicket
                Layout.topMargin: root._trackerText.length > 0 ? Theme.spSm : 0
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
            }

            // The body is a Tab stop of its own, after the chips and before
            // Done (A4-14): this stop hands the caret to it.
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

            // The body, then my plan under it and the slash hint (DG-062),
            // all in one scroll.
            MdBlockEditor {
                id: body
                objectName: "task-doc-body"
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Under a long tracker text my notes kept no height at all,
                // and Done sat right under their label (0.9.4): the tracker
                // box gives way first.
                Layout.minimumHeight: root._isTicket ? (root._short ? Theme.px(96) : Theme.px(160)) : 0
                // Its own text does not claim the room: the tracker box above
                // takes it first, up to its cap, and the rest is my notes'.
                Layout.preferredHeight: root._isTicket && root._trackerText.length > 0 ? (root._short ? Theme.px(96) : Theme.px(160)) : -1
                Layout.verticalStretchFactor: 1
                Layout.leftMargin: -Theme.px(24)
                Layout.rightMargin: -Theme.px(24)
                // The text ends above Done: it ran on beneath the button, with
                // nothing between them (F3-EYES-10).
                clip: true
                placeholder: I18n.t("taskdoc.bodyPh")
                ghostCaret: true
                menuTitle: I18n.t("textmenu.title.taskBody")
                // The note look (APP-292): the same md4c drawing as Knowledge.
                noteType: true
                headingRules: false
                wikiTargets: AppController.wikiTargets(body.source.text)
                wholeDocument: true
                // Drawn as the note is in rich (APP-313): one text across its lines.
                rich: true
                onEdited: if (!root._loading) { root._dirtyBody = true; saveTimer.restart(); }
                onEscaped: root.close()
                onInternalLinkActivated: (kind, target) => root.internalLinkActivated(kind, target)
                // Tab in the text moves on to my plan, as everywhere in the
                // document; it indents only where an indent means something
                // (a list item, code, lines selected). The caret arrives in
                // the text now, and Tab there indented an empty line for ever
                // (F2-REG-1).
                onBlockKey: (event) => {
                    const isTab = event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab;
                    // Any other key: the keyboard is at work in the text now.
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

            // Done, under the text while the meta column is folded away.
            DoneButton {
                id: doneBottom
                objectName: "task-doc-done-bottom"
                visible: !sideCol.visible
                Layout.bottomMargin: root._short ? Theme.spMd : Theme.sp2xl
                Layout.fillWidth: !root._quiet
            }
        }

        // ── the meta column: code, mentions, time, history, Done ──
        Rectangle {
            id: sideCol
            objectName: "task-doc-side"
            Layout.fillHeight: true
            Layout.preferredWidth: root._sideW
            // Folded away only where the text would get less than 440 px:
            // at 600 the panel lost the column on most screens, and with it
            // the code, the time and Done (0.9.4).
            visible: root.width >= 2 * root._padX + Theme.px(440) + root._sideW
            color: Theme.bg
            Rectangle {
                visible: !root._quiet
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: 1
                color: Theme.border
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: root._quiet ? Theme.spSm : Theme.sp3xl
                anchors.rightMargin: root._quiet ? Theme.px(28) : Theme.sp3xl
                anchors.bottomMargin: Theme.sp3xl
                anchors.topMargin: root._quiet ? Theme.px(96) : Theme.px(64)
                spacing: root._quiet ? Theme.sp3xl : Theme.px(26)

                // Код: hidden whole when there is nothing to say.
                ColumnLayout {
                    objectName: "task-doc-code"
                    // A layout in a layout fills by default; these sit at their
                    // own height so the column reads top-down (R2-013).
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    visible: String(root.task.branch || "").length > 0 || root._isTicket || root._prNumber > 0
                    spacing: root._quiet ? Theme.spXs : Theme.spMd
                    MetaHead { text: I18n.t("taskdoc.code") }
                    SideRow {
                        objectName: "task-doc-branch"
                        visible: String(root.task.branch || "").length > 0
                        label: I18n.t("taskdoc.branch")
                        value: String(root.task.branch || "")
                        mono: true
                    }
                    // PR #482  CI ✓ (bold) / "PR #482 · CI прошёл" (quiet).
                    SideRow {
                        id: prRow
                        objectName: "task-doc-pr"
                        visible: root._prNumber > 0
                        label: I18n.t("taskdoc.pr")
                        value: root._quiet ? "PR #" + root._prNumber
                                             + (root._prChecks.length > 0 ? " · " + I18n.t("taskdoc.ci." + root._prChecks) : "")
                                           : "#" + root._prNumber
                        link: root._quiet ? "" : String(root.task.prUrl || "")
                        Row {
                            visible: !root._quiet && root._prChecks.length > 0
                            spacing: Theme.sp2xs
                            readonly property color ink: root._prChecks === "passing" ? Theme.success
                                                       : root._prChecks === "failing" ? Theme.danger : Theme.textMuted
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "CI"
                                color: parent.ink
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                            }
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: root._prChecks === "passing" ? "check" : root._prChecks === "failing" ? "close" : "timer"
                                size: Theme.iconSize - 2
                                color: parent.ink
                            }
                        }
                    }
                    SideRow {
                        objectName: "task-doc-tracker"
                        visible: root._isTicket
                        label: I18n.t("taskdoc.tracker")
                        value: root._trackerLabel
                        link: String(root.task.externalUrl || "")
                        arrow: !root._quiet
                    }
                    // CI of the merge / pull request (APP-318): the failed
                    // jobs and their logs, on demand.
                    TaskCiPanel {
                        Layout.fillWidth: true
                        taskId: root.taskId
                        rev: root._rev
                    }
                    // Who owns the PR's changes (CODEOWNERS): who to ping.
                    TaskOwnersPanel {
                        Layout.fillWidth: true
                        taskId: root.taskId
                    }
                }

                // Упоминается в: notes that name the task.
                ColumnLayout {
                    objectName: "task-doc-mentions"
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    readonly property var notes: root._rev >= 0 ? AppController.notesMentioningTask(root.taskId) : []
                    visible: notes.length > 0
                    spacing: root._quiet ? Theme.spXs : Theme.spMd
                    MetaHead { text: I18n.t("taskdoc.mentioned") }
                    Repeater {
                        model: parent.notes
                        delegate: ColumnLayout {
                            id: mention
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                text: mention.modelData.title
                                elide: Text.ElideRight
                                color: noteCA.hovered ? Theme.text : (root._quiet ? Theme.textMuted : Theme.text)
                                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                                font.family: Theme.fontUi
                                font.pixelSize: root._quiet ? Theme.fsMd : Theme.fsMd
                                font.weight: root._quiet ? Theme.fwBody : Theme.fwTitle
                                ClickArea {
                                    id: noteCA
                                    label: mention.modelData.title
                                    onActivated: root.internalLinkActivated("note", mention.modelData.title)
                                }
                            }
                            Text {
                                visible: !root._quiet
                                text: I18n.t("taskdoc.noteOn").arg(root._valid(mention.modelData.updated) ? root._relDay(mention.modelData.updated)
                                                                                                        : I18n.fmtDate(mention.modelData.updated, "dayMonth"))
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
                    taskId: root.taskId
                    rev: root._rev
                    onOpenTask: (id) => root.openOther(id)
                }

                // Время: the live timer, Пауза t, all of it against the estimate.
                ColumnLayout {
                    id: timeBox
                    objectName: "task-doc-time"
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: root._quiet ? Theme.spXs : Theme.spMd
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
                    MetaHead { text: I18n.t("taskdoc.time") }
                    RowLayout {
                        visible: !root._quiet
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
                    // "всего 3 ч 10 мин из оценки 4 ч" (bold);
                    // "0:42 сейчас · всего 3 ч 10 мин" (quiet).
                    Text {
                        objectName: "task-doc-time-line"
                        Layout.fillWidth: true
                        visible: text.length > 0
                        wrapMode: Text.WordWrap
                        text: {
                            const bits = [];
                            if (root._quiet && root.task.isTiming) bits.push(I18n.t("taskdoc.timerNow").arg(timeBox.clock));
                            // In the chip's units ("1ч 30м", Duration), and
                            // with nothing tracked only the estimate.
                            const fmt = (m) => AppController.formatDuration(m, root._estProvider);
                            const est = !root._quiet ? (root.task.estimateMinutes || 0) : 0;
                            if (timeBox.totalMin > 0)
                                bits.push(I18n.t("taskdoc.tracked").arg(fmt(timeBox.totalMin))
                                          + (est > 0 ? " " + I18n.t("taskdoc.ofEstimate").arg(fmt(est)) : ""));
                            else if (est > 0)
                                bits.push(I18n.t("local.card.estimate").arg(fmt(est)));
                            return bits.join(" · ");
                        }
                        color: root._quiet ? Theme.textMuted : Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: root._quiet ? Theme.fsMd : Theme.fsSm
                    }
                    // Quiet: start / pause is a text link.
                    Text {
                        objectName: "task-doc-timer-link"
                        visible: root._quiet
                        text: root.task.isTiming ? I18n.t("taskdoc.pause").toLowerCase() : I18n.t("taskcard.startTimer").toLowerCase()
                        color: timerCA.hovered ? Theme.text : Theme.textDim
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                        font.underline: timerCA.hovered
                        ClickArea {
                            id: timerCA
                            label: parent.text
                            onActivated: root.task.isTiming ? AppController.stopTaskTimer(root.taskId) : AppController.startTaskTimer(root.taskId)
                        }
                    }
                    // Session by session (APP-251), folded.
                    TaskSessions {
                        Layout.fillWidth: true
                        taskId: root.taskId
                        rev: root._rev
                    }
                }

                // История: the newest facts.
                ColumnLayout {
                    objectName: "task-doc-history"
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    visible: root._historyRows.length > 0
                    spacing: Theme.spXs
                    MetaHead { text: I18n.t("taskdoc.history") }
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

                // Quiet: a small outline Готово right under История (DG-065).
                DoneButton {
                    objectName: "task-doc-done"
                    visible: root._quiet
                }

                Item { Layout.fillHeight: true }

                // Bold: Готово d, the main action, at the bottom.
                DoneButton {
                    objectName: "task-doc-done-bold"
                    visible: !root._quiet
                    Layout.fillWidth: true
                }
            }
        }

        // In the whole content area the text and its meta column keep the
        // sheet's widths, the rest stays empty.
        Item {
            visible: root.full
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
        readonly property alias draft: commentDraft
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
        // The ticket's own comments, read only, above my draft of an answer.
        TaskTrackerComments {
            id: trackerComments
            Layout.fillWidth: true
            Layout.topMargin: Theme.spLg
            taskId: root._isTicket ? root.taskId : ""
            active: root.opened
            trackerName: root._badge.name || root._ticket.provider || ""
        }
        TaskCommentDraft {
            id: commentDraft
            Layout.fillWidth: true
            visible: root._isTicket
            taskId: root._isTicket ? root.taskId : ""
            rev: root._rev
            trackerName: root._badge.name || root._ticket.provider || ""
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
    // A section head of the meta column: 12 px, muted.
    component MetaHead: Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.sp2xs
        color: root._quiet ? Theme.textDim : Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        font.weight: root._quiet ? Theme.fwBody : Theme.fwHeading
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
