import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import TodoCpp
import "PopupStack.js" as PopupStack
import "TaskDates.js" as TaskDates

Popup {
    PopupArrival { popup: root }
    id: root
    modal: true
    focus: true
    // Esc and a click on the backdrop go through requestClose(), which asks
    // before throwing edits away (TASKS-9); Qt's own close-on-Escape would
    // drop them without a word.
    closePolicy: Popup.NoAutoClose
    padding: 0
    // Wide enough for a description to be read, wider still while it is
    // expanded for writing; never taller than the window, with the body
    // scrolling between a fixed header and footer.
    width: Math.min(descExpanded ? 880 : 640, _maxW)
    height: Math.min(implicitHeight, _maxH)
    readonly property real _maxW: (Overlay.overlay ? Overlay.overlay.width : 1200) - 48
    readonly property real _maxH: (Overlay.overlay ? Overlay.overlay.height : 900) - 48

    // Dimmed backdrop so the underlying app stays visible behind the popup.
    Overlay.modal: ModalScrim {}
    // A press beside the editor (APP-126); see PopupStack.js.
    Overlay.onPressed: if (PopupStack.isTopmost(root, Overlay.overlay) && PopupStack.pressedOutside(root, AppController.lastPressGlobalPos())) root.requestClose()

    property var draft: ({})
    // What the fields held when the editor opened; anything else is an edit.
    property string _baseline: ""
    // Why the last Save did not go through, shown above the footer.
    property string _error: ""
    readonly property bool _archived: !!(root.draft && root.draft.archived)
    property bool isNew: false
    // The "seen this before" hint under the description was clicked (APP-159).
    signal seenBeforeActivated(var hit)
    // Ctrl+click on a note, a task, a tag in the drawn description.
    signal descLinkActivated(string kind, string target)
    // "edit" | "preview" for the description. Starts on edit — the editor is
    // where you go to change things.
    // Both kept across opens: whoever likes Details open, or writes long
    // descriptions, should not have to ask again for every task.
    property bool detailsOpen: false
    property bool descExpanded: false
    readonly property real _descMinH: 120
    readonly property real _descMaxH: descExpanded ? Math.max(280, _maxH - 360) : 280
    // A closed Details says what it holds.
    readonly property string _detailsSummary: {
        const parts = [];
        if (branchField.text.length) parts.push("⎇ " + branchField.text);
        if (recurBox.currentIndex > 0) parts.push("↻ " + recurBox.currentText);
        if (scheduledField.text.length) parts.push("▸ " + scheduledField.text);
        if (estimateField.text.length) parts.push(estimateField.text + " min");
        const labels = root.labelsFromText(labelsField.text);
        if (labels.length) parts.push(labels.join(", "));
        if (somedayBox.checked) parts.push(I18n.t("editor.someday"));
        return parts.join("  ·  ");
    }
    // The task's files, as chips. An existing task attaches and detaches on
    // the stored task at once (each its own undo step, like a drag on the
    // board); a new one keeps them here until Create.
    property var _attachments: []

    function _refreshAttachments() {
        if (!root.isNew && root._originalId.length > 0)
            root._attachments = AppController.taskAttachments(root._originalId);
    }
    // File URLs from the picker, a drop or the clipboard.
    function attachUrls(urls) {
        if (!urls || urls.length === 0) return 0;
        const list = [];
        for (let i = 0; i < urls.length; ++i) list.push(urls[i]);
        if (!root.isNew && root._originalId.length > 0) {
            const n = AppController.attachFilesToTask(root._originalId, list);
            root._refreshAttachments();
            return n;
        }
        const added = AppController.importAttachments(list);
        const next = root._attachments.slice();
        for (let j = 0; j < added.length; ++j) {
            if (!next.some(a => a.id === added[j].id)) next.push(added[j]);
        }
        root._attachments = next;
        return added.length;
    }
    function removeAttachment(attachmentId) {
        if (!root.isNew && root._originalId.length > 0) {
            AppController.removeTaskAttachment(root._originalId, attachmentId);
            root._refreshAttachments();
            return;
        }
        root._attachments = root._attachments.filter(a => a.id !== attachmentId);
    }
    // Ctrl+V in the description with a file or an image on the clipboard: the
    // file is stored and linked where the caret is, as markdown. Returns false
    // when the clipboard holds nothing to attach, so the paste stays a paste.
    function pasteAttachment(field) {
        if (!AppController.clipboardHasAttachment()) return false;
        const added = AppController.importClipboardAttachments();
        if (added.length === 0) return true;
        const refs = added.map(a => a.ref).join("\n\n");
        field.remove(field.selectionStart, field.selectionEnd);
        // A pasted .csv / .xlsx is a table (APP-315), which has to start and
        // end on lines of its own.
        const pos = field.cursorPosition;
        const text = field.text;
        let before = "";
        if (pos > 0 && text.charAt(pos - 1) !== "\n") before = "\n\n";
        else if (pos > 1 && text.charAt(pos - 2) !== "\n") before = "\n";
        const after = pos < text.length && text.charAt(pos) !== "\n" ? "\n\n" : "";
        field.insert(pos, before + refs + after);
        return true;
    }

    // Undo from the toast puts a detached file back on the stored task.
    // Held as a var: Qt 6.9's qmllint reads the TaskModel type as not a QObject.
    readonly property var _taskModel: AppController.tasks
    Connections {
        target: root._taskModel
        enabled: root.opened && !root.isNew
        function onDataChanged() { root._refreshAttachments() }
    }

    FileDialog {
        id: attachDialog
        fileMode: FileDialog.OpenFiles
        title: I18n.t("att.dialog.title")
        onAccepted: root.attachUrls(selectedFiles)
    }

    // Original id at the moment of opening the editor. Used so that even if
    // the user edits idField, AppController can find and rename the existing
    // row instead of inserting a duplicate.
    property string _originalId: ""
    // The profile the editor was opened in: the task lives there, and what is
    // saved or deleted here goes there, whatever profile is active by then
    // (PRES-1).
    property string _profileId: ""
    // A new task's profile (A5-14, A5-21): the one picked in the header, or
    // the one a "+Name" in the title names; the one it was opened in by
    // default. Saving goes there, as quick capture's picker does.
    property string _pickedProfileId: ""
    property string _titleProfileId: ""
    // The priority a "p1" in a new task's title will give, shown in the box
    // until the box is picked by hand (EYE-10: chip P1, box still P2).
    property string _titlePriority: ""
    property bool _priorityPicked: false
    readonly property string _targetProfileId: root._titleProfileId || root._pickedProfileId || root._profileId
    readonly property string _targetProfileName: {
        const p = AppController.profileById(root._targetProfileId);
        return p && p.name ? p.name : "";
    }
    // Takes the "+Name" out of the title: it said where, not what.
    function _cutTitleProfile() {
        const p = AppController.captureParse(titleField.text || "", new Date(), []);
        const marks = p.marks || [];
        for (let i = 0; i < marks.length; i++) {
            if (marks[i].kind !== "profile") continue;
            const raw = titleField.text;
            titleField.text = (raw.slice(0, marks[i].start) + " " + raw.slice(marks[i].end)).replace(/\s+/g, " ").trim();
            return p.profileId || "";
        }
        return "";
    }
    function _backToOwnProfile() {
        if (root._profileId && AppController.activeProfileId !== root._profileId)
            AppController.activeProfileId = root._profileId;
    }

    // ── Mirrored tracker issue (HEAP-117) ──
    // Empty for a locally-created task, which is what the strip keys off.
    readonly property var _ticket: (root.draft && root.draft.ticket) ? root.draft.ticket : ({})
    readonly property bool _isTicket: !!(root._ticket.provider)
    readonly property var _badge: root._isTicket
        ? (AppController.providerBadges[root._ticket.provider] || ({}))
        : ({})
    // Only the fields the tracker actually filled in, as label/value pairs.
    readonly property var _ticketFacts: {
        if (!root._isTicket) return [];
        const t = root._ticket;
        const out = [];
        const add = (key, value) => {
            if (value !== undefined && value !== null && String(value).length > 0) {
                out.push({ label: I18n.t(key), value: String(value) });
            }
        };
        // No assignee: heap mirrors the issues assigned to you, so it would
        // only ever say your own name.
        // Where the issue really is: the column here may be the user's own
        // when heap does not write that tracker (APP-243).
        add("ticket.trackerStatus", t.remoteStatus);
        add("ticket.author", t.author);
        add("ticket.type", t.issueType);
        add("ticket.milestone", t.milestone);
        // -1 means the provider never said, which is not the same as none.
        if ((t.commentCount || -1) >= 0) {
            out.push({ label: I18n.t("ticket.comments"), value: String(t.commentCount) });
        }
        const when = (d) => (d && d.getTime && !isNaN(d.getTime())) ? AppController.shortDate(d) : "";
        add("ticket.created", when(t.createdAt));
        add("ticket.updated", when(t.updatedAt));
        return out;
    }

    // A field both heap and the tracker changed since the last sync (audit
    // INT-4). Resolving it hides the panel for the rest of this dialog.
    property bool _conflictResolved: false
    readonly property bool _hasConflict: root._isTicket && !!root._ticket.conflict && !root._conflictResolved
    // The tracker's side of each conflicting field, as label/value pairs.
    readonly property var _conflictRows: {
        if (!root._hasConflict) return [];
        const t = root._ticket;
        const fields = t.conflicts || [];
        const out = [];
        for (let i = 0; i < fields.length; ++i) {
            const f = fields[i];
            if (f === "title") out.push({ label: I18n.t("ticket.conflict.title"), value: String(t.remoteTitle || "") });
            else if (f === "body") out.push({ label: I18n.t("ticket.conflict.body"), value: String(t.remoteBody || "") || "—" });
            else if (f === "priority") out.push({ label: I18n.t("ticket.conflict.priority"), value: String(t.remotePriority || "") });
            else if (f === "status") {
                const names = root.statusNames();
                const at = root.statusList().indexOf(String(t.remoteColumn || ""));
                out.push({ label: I18n.t("ticket.conflict.status"),
                           value: (at >= 0 ? names[at] : String(t.remoteColumn || ""))
                                  + (t.remoteStatus ? " (" + t.remoteStatus + ")" : "") });
            }
        }
        return out;
    }
    // Put the tracker's values in the fields (so Save keeps them) and on the
    // stored card, in one undoable step.
    function _takeTrackerVersion() {
        const t = root._ticket;
        const fields = t.conflicts || [];
        if (fields.indexOf("title") >= 0 && String(t.remoteTitle || "").length > 0) titleField.text = t.remoteTitle;
        if (fields.indexOf("body") >= 0) descField.text = String(t.remoteBody || "");
        if (fields.indexOf("priority") >= 0 && String(t.remotePriority || "").length > 0)
            priBox.currentIndex = Math.max(0, ["P0", "P1", "P2", "P3"].indexOf(t.remotePriority));
        if (fields.indexOf("status") >= 0) {
            const at = root.statusList().indexOf(String(t.remoteColumn || ""));
            if (at >= 0) statusBox.currentIndex = at;
        }
        AppController.resolveTrackerConflict(root._originalId, true);
        root._conflictResolved = true;
    }

    // What happened to this task (APP-165). Read on open and whenever the
    // controller says this task gained an event; never written from here.
    property bool historyOpen: false
    property int _historyRev: 0
    readonly property var _history: (root._historyRev, root._originalId ? AppController.taskHistory(root._originalId) : [])
    // The list as the fold shows it: worked out here, so the delegates only
    // read their own row.
    readonly property var _historyRows: root._history.map(e => ({ when: root._historyWhen(e.at), text: root._historyText(e) }))
    function _historyWhen(at) {
        return I18n.fmtDateTime(at, "weekdayDay");
    }
    function _statusName(id) {
        const at = root.statusList().indexOf(String(id || ""));
        return at >= 0 ? root.statusNames()[at] : String(id || "—");
    }
    function _historyText(e) {
        const v = (s) => String(s || "").length > 0 ? String(s) : "—";
        let line;
        switch (e.kind) {
        case "created":   return I18n.t(e.sync ? "history.pulled" : "history.created");
        case "pushed":    return I18n.t("history.pushed").arg(root._statusName(e.to));
        case "status":    line = I18n.t("history.status").arg(root._statusName(e.from)).arg(root._statusName(e.to)); break;
        case "title":     line = I18n.t("history.title").arg(v(e.from)).arg(v(e.to)); break;
        case "priority":  line = I18n.t("history.priority").arg(v(e.from)).arg(v(e.to)); break;
        case "due":       line = I18n.t("history.due").arg(v(e.from)).arg(v(e.to)); break;
        case "scheduled": line = I18n.t("history.scheduled").arg(v(e.from)).arg(v(e.to)); break;
        case "profile":   line = I18n.t("history.profile").arg(v(e.from)).arg(v(e.to)); break;
        default:          line = String(e.kind);
        }
        return e.sync ? I18n.t("history.viaTracker").arg(line) : line;
    }

    // Comments, held only while this dialog is open (HEAP-117).
    property var _comments: []
    property string _commentsError: ""
    property bool _commentsRequested: false

    Connections {
        target: AppController

        function onTaskHistoryChanged(taskId) {
            if (taskId === root._originalId) root._historyRev++;
        }

        function onTicketCommentsLoaded(taskId, comments, error) {
            // A reply for a ticket the user has since navigated away from.
            if (taskId !== (root._originalId || (root.draft.id || ""))) return;
            root._comments = comments;
            root._commentsError = error || "";
        }
    }

    function showFor(initialDraft) {
        // Never carry one ticket's comments over to the next.
        _comments = [];
        _commentsError = "";
        _commentsRequested = false;
        _conflictResolved = false;
        historyOpen = false;
        _historyRev++;
        draft = initialDraft || {};
        isNew = !!draft._isNew;
        _originalId = isNew ? "" : (draft.id || "");
        // Opening a card a sync brought in is seeing it (APP-180).
        if (_originalId.length > 0) AppController.markTaskSeen(_originalId);
        _profileId = AppController.activeProfileId;
        _pickedProfileId = "";
        _titleProfileId = "";
        // New tasks: leave idField empty with a TODO hint — real id is
        // assigned on save. Edit: pre-fill with existing id (editable).
        idField.text = isNew ? "" : (draft.id || "");
        titleField.text = draft.title || "";
        descField.text = draft.desc || "";
        statusBox.currentIndex = Math.max(0, statusList().indexOf(draft.status));
        priBox.currentIndex = Math.max(0, ["P0", "P1", "P2", "P3"].indexOf(draft.priority || "P2"));
        _priorityPicked = false;
        branchField.text = draft.branch || "";
        // Each datetime has its own clock flag (schema v10): a date-only
        // deadline next to a timed schedule stays date-only.
        deadlineField.text = formatWhen(draft.dueAt, draft.dueHasTime);
        scheduledField.text = formatWhen(draft.scheduledAt, draft.scheduledHasTime);
        labelsField.text = labelsToText(draft.labels);
        estimateField.text = draft.estimateMinutes > 0 ? String(draft.estimateMinutes) : "";
        somedayBox.checked = !!draft.someday;
        recurBox.setRecurrence(draft.recurrence || "");
        _attachments = isNew ? AppController.describeAttachments(draft.attachments || [])
                             : AppController.taskAttachments(draft.id || "");
        _error = "";
        discardPrompt.close();
        remind.reset();
        _baseline = _snapshot();
        _refreshTitleChips();
        open();
        // Keyboard-first: Ctrl+N and type. Nothing had focus, so whatever was
        // typed before reaching for the mouse went nowhere and the task was
        // saved without a title. Focus lands now, not after the enter
        // transition, so the first keystroke is not lost either. An existing
        // task opens with the caret at the end of its title.
        titleField.forceActiveFocus();
        if (isNew) titleField.selectAll();
        else titleField.cursorPosition = titleField.text.length;
        // Kick a one-shot PR/state refresh for this task's branch across all
        // watched repos. Result lands on TaskModel via repoStateUpdated and
        // chips on the underlying TaskCard update without re-opening.
        if (draft.id && !isNew) AppController.refreshGitForTaskBranch(draft.id);
    }

    // Everything the user can change, as one comparable string.
    function _snapshot() {
        return JSON.stringify([idField.text, titleField.text, descField.text, statusBox.currentIndex,
                               priBox.currentIndex, branchField.text, deadlineField.text, scheduledField.text,
                               labelsField.text, estimateField.text, somedayBox.checked, recurBox.currentIndex,
                               // An existing task's files are saved as they are attached.
                               root.isNew ? root._attachments.map(a => a.id) : []]);
    }
    function isDirty() {
        return root.opened && root._snapshot() !== root._baseline;
    }

    // Esc and the backdrop. Clean: close. Edited: ask, keyboard first —
    // Enter saves, Ctrl+D discards, Esc keeps editing.
    function requestClose() {
        if (discardPrompt.opened) return;
        if (!root.isDirty()) { root.close(); return; }
        discardPrompt.open();
    }

    // Runs `next` once nothing typed here is left to lose: at once when the
    // editor is clean or closed, otherwise after the save/discard prompt —
    // never after "keep editing". Opening another task from a notification and
    // quitting the app used to drop the edits without asking (TASKS-18,
    // SHELL-29, audit 2026-09-30).
    property var _afterPrompt: null
    function settleThen(next) {
        if (!root.isDirty()) { next(); return; }
        root._afterPrompt = next;
        if (!discardPrompt.opened) discardPrompt.open();
    }
    function _runAfterPrompt() {
        const next = root._afterPrompt;
        root._afterPrompt = null;
        if (next && !root.isDirty()) next();
    }

    function statusList() {
        const out = [];
        const sts = AppController.statuses;
        for (let i = 0; i < sts.length; i++) out.push(sts[i].id);
        return out;
    }

    function statusNames() {
        const out = [];
        const sts = AppController.statuses;
        for (let i = 0; i < sts.length; i++) out.push(sts[i].name);
        return out;
    }

    function formatDate(d) {
        if (!d || !d.getFullYear) return "";
        return d.getFullYear() + "-" + (d.getMonth() + 1).toString().padStart(2, "0") + "-" + d.getDate().toString().padStart(2, "0");
    }

    // Renders a stored datetime back into the field. The clock part is only
    // shown when the task actually carries one — a bare date must not come back
    // as "… 00:00" and turn itself into a timed task on the next save.
    function formatWhen(d, hasTime) {
        if (!d || !d.getFullYear || isNaN(d.getTime())) return "";
        const iso = formatDate(d);
        if (!hasTime) return iso;
        return iso + " " + String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0");  // machine-format: the field parses it back
    }

    // "YYYY-MM-DD" or "YYYY-MM-DD HH:MM" — the shape formatWhen writes back into
    // the field, parsed without going through the natural-language parser.
    readonly property var _isoRe: /^(\d{4})-(\d{1,2})-(\d{1,2})(?:[ T](\d{1,2}):(\d{2}))?$/

    function parseDate(s) {
        if (!s) return undefined;
        const r = AppController.parseDateTime(s, new Date());
        if (r && r.ok && r.start) return r.start;
        const m = root._isoRe.exec(s.trim());
        if (!m) return undefined;
        return new Date(parseInt(m[1]), parseInt(m[2]) - 1, parseInt(m[3]),
                        m[4] ? parseInt(m[4]) : 0, m[5] ? parseInt(m[5]) : 0);
    }

    // Does this field carry a clock time, as opposed to a bare date?
    function parseHasTime(s) {
        if (!s) return false;
        const r = AppController.parseDateTime(s, new Date());
        if (r && r.ok) return !!r.hasTime;
        const m = root._isoRe.exec(s.trim());
        return !!(m && m[4]);
    }

    function labelsToText(labels) {
        if (!labels || !labels.length) return "";
        const out = [];
        for (let i = 0; i < labels.length; ++i) out.push(labels[i].id);
        return out.join(", ");
    }

    // Tab on a markdown list line ("- a", "* [ ] b", "1. c") nests the item two
    // spaces deeper; Shift+Tab takes up to two back out. Returns false off a
    // list line, where Tab moves focus instead.
    readonly property var _listLineRe: /^(\s*)([-*+]|\d+[.)])\s/

    function indentListLine(field, outdent) {
        const text = field.text;
        const pos = field.cursorPosition;
        const start = text.lastIndexOf("\n", pos - 1) + 1;
        const endNl = text.indexOf("\n", pos);
        const line = text.substring(start, endNl < 0 ? text.length : endNl);
        const m = root._listLineRe.exec(line);
        if (!m) return false;
        if (outdent) {
            const n = Math.min(2, m[1].length);
            if (n > 0) {
                field.remove(start, start + n);
                field.cursorPosition = Math.max(start, pos - n);
            }
        } else {
            field.insert(start, "  ");
            field.cursorPosition = pos + 2;
        }
        return true;
    }

    function labelsFromText(s) {
        const out = [];
        const parts = (s || "").split(",");
        for (let i = 0; i < parts.length; ++i) {
            const id = parts[i].trim();
            if (id.length > 0) out.push(id);
        }
        return out;
    }

    property var _deadlinePreview: ({ok: false})

    function _refreshDeadlinePreview() {
        const s = deadlineField.text;
        if (!s) {
            _deadlinePreview = {ok: false};
            return;
        }
        _deadlinePreview = AppController.parseDateTime(s, new Date()) || {ok: false};
        root._refreshTitleChips();
    }

    // Forwards to heap::text::extractMeta (C++, unit-tested).
    function _extractMeta(raw) {
        return AppController.extractTaskMeta(raw || "");
    }

    // What the title field turns into on save (TASKS-10). An existing task's
    // title is saved as typed: "Critical crash on login" or a Jira "Blocker:
    // …" is a name, not quick-add syntax, and each save used to cut more of it
    // away. A new task reads the title the way quick-add does — "p0", "#billing",
    // "APP-104" and a "// note" tail become the priority, labels, id and
    // description — but only when all of them can be applied: a priority picked
    // in the box that disagrees, an id typed by hand or a ticket key already
    // taken leaves the title as typed (minus the "//" tail), so no word is lost.
    // Dates, a length and a repeat are read too, the way quick capture reads
    // them (A1-4, A5-1): "Call bank tomorrow 3pm p1" was saved with the date
    // still in the title and no date at all. `read` is _readTitle()'s answer.
    function _titleTokens(meta, typedId, read) {
        const raw = titleField.text || "";
        if (!root.isNew)
            return { title: raw.trim(), priority: "", labels: [], id: "" };
        const asTyped = { title: String(meta.head || "").trim(), priority: "", labels: [], id: "" };
        const autoPri = root.draft.priority || "P2";
        if (meta.priority && meta.priority !== priBox.currentText && (priBox.currentText !== autoPri || root._priorityPicked))
            return asTyped;
        let id = "";
        if (meta.ticketKey && meta.ticketKey !== typedId) {
            const idTyped = idField.text.trim().length > 0;
            const taken = !!AppController.taskById(meta.ticketKey).id;
            if (idTyped || taken)
                return asTyped;
            id = meta.ticketKey;
        }
        // Nothing but tokens ("p0 #ops") is not a title.
        if (String(meta.title || "").trim().length === 0)
            return asTyped;
        const out = { title: meta.title, priority: meta.priority || "", labels: meta.labels || [], id: id };
        // A date alone ("tomorrow 3pm") is not a title either: then it stays.
        const r = read || ({});
        if (String(r.title || "").trim().length > 0) {
            const valid = (d) => !!(d && d.getTime && !isNaN(d.getTime()));
            out.title = r.title;
            if (valid(r.when)) { out.when = r.when; out.whenHasTime = !!r.whenHasTime; }
            if (valid(r.due)) { out.due = r.due; out.dueHasTime = !!r.dueHasTime; }
            if (r.estimateMinutes > 0) out.estimate = r.estimateMinutes;
            if (r.recurrence) out.recurrence = r.recurrence;
        }
        return out;
    }

    // A new task's title read as quick capture reads it. A part whose own
    // field is already filled in (a deadline typed below, a plan, a length, a
    // repeat) stays words in the title: what was typed there wins.
    function _readTitle(raw) {
        if (!root.isNew || !raw || String(raw).trim().length === 0) return ({});
        const now = new Date();
        let p = AppController.captureParse(raw, now, []);
        const schedTyped = scheduledField.text.trim().length > 0;
        const dueTyped = deadlineField.text.trim().length > 0;
        const estTyped = (parseInt(estimateField.text || "0") || 0) > 0;
        const recurTyped = recurBox.currentIndex > 0;
        const rejected = [];
        const spans = p.spans || [];
        for (let i = 0; i < spans.length; ++i) {
            const k = spans[i].kind;
            if ((k === "when" && (schedTyped || (recurTyped && p.recurrence))) || (k === "due" && dueTyped)
                || (k === "estimate" && estTyped)
                // The editor sets no reminder of its own: "напомни в 15:00"
                // is a reminder to self (ReminderCapture) or, after Tab,
                // words in the title — never silently cut out.
                || k === "remind")
                rejected.push(spans[i].text);
        }
        if (rejected.length > 0) p = AppController.captureParse(raw, now, rejected);
        return p;
    }

    // What the title will give on save, as the chips quick capture shows.
    property var _titleChips: []
    function _refreshTitleChips() {
        if (!root.isNew || titleField.text.trim().length === 0) {
            root._titleChips = [];
            root._titleProfileId = "";
            root._titlePriority = "";
            return;
        }
        const meta = root._extractMeta(titleField.text);
        const typedId = idField.text.trim() || root.draft.id || "";
        const read = root._readTitle(titleField.text);
        root._titleProfileId = String(read.profileId || "");
        const t = root._titleTokens(meta, typedId, read);
        const when = (d, hasTime) => I18n.fmtDate(d, "weekdayDay") + (hasTime ? " · " + I18n.fmtTime(d) : "");
        const chips = [];
        if (t.when) chips.push({ kind: "when", key: I18n.t("capture.when"), value: when(t.when, t.whenHasTime) });
        if (t.due) chips.push({ kind: "due", key: I18n.t("capture.due"), value: when(t.due, t.dueHasTime) });
        if (t.recurrence) chips.push({ kind: "repeat", key: I18n.t("capture.repeat"), value: root._recurText(t.recurrence) });
        if (t.estimate) chips.push({ kind: "estimate", key: I18n.t("capture.estimate"), value: I18n.fmtMinutes(t.estimate) });
        if (t.priority) chips.push({ kind: "priority", key: I18n.t("capture.priority"), value: t.priority });
        root._titlePriority = t.priority || "";
        for (const l of (t.labels || [])) chips.push({ kind: "label", key: I18n.t("capture.label"), value: "#" + l });
        if (read.profileName) chips.push({ kind: "profile", key: I18n.t("capture.profile"), value: String(read.profileName) });
        root._titleChips = chips;
    }
    function _recurText(r) {
        const interval = TaskDates.intervalTitle(r);
        if (interval.length > 0) return interval;
        const m = /^every:month:(\d+)$/.exec(String(r || ""));
        if (m) return I18n.t("editor.recur.monthlyOn").arg(m[1]);
        const i = recurBox._base.indexOf(r);
        return i >= 0 ? recurBox.model[i] : r;
    }

    function _mergeLabels(typed, fromTitle) {
        const out = typed.slice();
        for (const l of fromTitle) {
            if (!out.some(x => x.toLowerCase() === l.toLowerCase()))
                out.push(l);
        }
        return out;
    }

    // Resolve @handles into display names via PersonModel. Unknown handles
    // are kept as "@handle" so context isn't silently dropped.
    function _resolvePeopleNames(handles) {
        if (!handles || handles.length === 0) return [];
        const out = [];
        const known = AppController.people;
        for (let i = 0; i < handles.length; ++i) {
            const h = handles[i];
            const p = AppController.personById(h);
            if (p && p.id) { out.push(p.name || p.id); continue; }
            // Fallback: case-insensitive scan over name's first token / id.
            let matched = false;
            for (let j = 0; j < known.rowCount(); ++j) {
                const idx = known.index(j, 0);
                const id  = String(known.data(idx, Qt.UserRole + 1) || "");
                const nm  = String(known.data(idx, Qt.UserRole + 2) || "");
                const firstWord = nm.split(/\s+/)[0] || "";
                if (id.toLowerCase() === h.toLowerCase()
                    || firstWord.toLowerCase() === h.toLowerCase()) {
                    out.push(nm || id);
                    matched = true;
                    break;
                }
            }
            // "@r.losev" for Роман Лосев: a login made of one person's name.
            const byLogin = matched ? "" : AppController.personIdForHandle(h);
            if (byLogin) out.push(AppController.personById(byLogin).name || byLogin);
            else if (!matched) out.push("@" + h);
        }
        return out;
    }

    // Forwards to heap::text::classifyKind (C++, unit-tested).
    function _classifyKind(text) {
        return AppController.classifyTaskKind(text || "");
    }

    // Shared by the Save button and the Ctrl+Return shortcut. Returns whether
    // the task was saved (and the editor closed).
    function _save() {
        if (!root._commit()) return false;
        root.close();
        return true;
    }

    // Validates and saves without closing. A refusal says why and keeps the
    // draft: an unreadable date used to be saved as no date, an empty title
    // did nothing at all.
    function _commit() {
        root._error = "";
        if (titleField.text.trim().length === 0) {
            root._error = I18n.t("editor.err.title");
            titleField.forceActiveFocus();
            return false;
        }
        // "напомни мне …" in a new task's title (dogfood 2026-10-22).
        if (root.isNew) {
            remind.refresh();
            if (remind.active) return root._commitReminder();
        }
        if (deadlineField.text.trim().length > 0 && root.parseDate(deadlineField.text) === undefined) {
            root._error = I18n.t("editor.err.date").arg(deadlineField.text.trim());
            deadlineField.forceActiveFocus();
            return false;
        }
        if (scheduledField.text.trim().length > 0 && root.parseDate(scheduledField.text) === undefined) {
            root._error = I18n.t("editor.err.date").arg(scheduledField.text.trim());
            root.detailsOpen = true;
            scheduledField.forceActiveFocus();
            return false;
        }
        // A new task goes to the profile picked for it ("+Name" in the
        // title wins), and the "+Name" leaves the title.
        if (root.isNew) {
            const named = root._cutTitleProfile();
            root._profileId = named || root._pickedProfileId || root._profileId;
        }
        // New tasks: if user left idField blank, fall back to the
        // auto-generated id captured in the draft. Edit: take the
        // (possibly renamed) text from idField.
        // An emptied field on an existing task keeps its id: saving a task
        // under "" made it unreachable.
        let finalId = idField.text.trim();
        if (finalId.length === 0) {
            finalId = root.isNew ? (root.draft.id || "") : root._originalId;
        }
        const meta = root._extractMeta(titleField.text);
        const inlineDesc = root.isNew ? meta.desc : "";
        const handleNames = root._resolvePeopleNames(meta.handles);
        const parsed = root._titleTokens(meta, finalId, root._readTitle(titleField.text));
        const cleanedTitle = parsed.title;
        if (parsed.id) finalId = parsed.id;

        // Due and scheduled are typed independently; a task with only
        // a due date is scheduled for that day, which is what the
        // single deadline field always meant. A date read from a new
        // task's title fills a field left empty.
        const dueTyped = root.parseDate(deadlineField.text);
        const schedTyped = root.parseDate(scheduledField.text);
        const dueAt = dueTyped || parsed.due;
        const scheduledAt = schedTyped || parsed.when || dueTyped;
        const dueHasTime = dueTyped ? root.parseHasTime(deadlineField.text) : (parsed.due ? parsed.dueHasTime : false);
        const scheduledHasTime = schedTyped ? root.parseHasTime(scheduledField.text)
                                            : (parsed.when ? parsed.whenHasTime : (dueTyped ? dueHasTime : false));

        const d = {
            _isNew: root.isNew,
            _profileId: root._profileId,
            // Pass the original id alongside the (possibly edited)
            // new id so saveTask can rename rather than insert a
            // duplicate row when the user changes the id field.
            _originalId: root._originalId,
            id: finalId,
            title: cleanedTitle,
            // Inline "//..." appends to the description field.
            desc: inlineDesc.length > 0
                  ? ((descField.text || "").trim().length > 0
                      ? descField.text.trim() + "\n" + inlineDesc
                      : inlineDesc)
                  : descField.text,
            priority: parsed.priority || priBox.currentText,
            status: root.statusList()[statusBox.currentIndex],
            scheduledAt: scheduledAt,
            dueAt: dueAt,
            dueHasTime: dueHasTime,
            scheduledHasTime: scheduledHasTime,
            branch: branchField.text,
            recurrence: recurBox.value() || parsed.recurrence || "",
            // Plain names; saveTask keeps each existing label's colour.
            labels: root._mergeLabels(root.labelsFromText(labelsField.text), parsed.labels),
            estimateMinutes: parseInt(estimateField.text || "0") || parsed.estimate || 0,
            someday: somedayBox.checked
        };
        // A new task's files ride along with the draft; an existing task's
        // are on it already.
        if (root.isNew) {
            d.attachments = root._attachments.map(a => ({ id: a.id, name: a.name, size: a.size, mime: a.mime }));
        }
        // Before the undo group: a profile switch ends the undo history, and
        // inside a group it would record one profile's models against another's.
        root._backToOwnProfile();
        // A task and the meeting it books are one undo step.
        AppController.beginUndoGroup(I18n.t("editor.undo.save").arg(finalId));
        try {
            return root._saveDraft(d, cleanedTitle, handleNames);
        } finally {
            AppController.endUndoGroup();
        }
    }

    // A reminder to self: the title read without its phrase, at the time said
    // or picked; the column, description, a hand-picked priority and labels
    // typed below ride along.
    function _commitReminder() {
        if (remind.title.trim().length === 0) {
            root._error = I18n.t("self.empty");
            titleField.forceActiveFocus();
            return false;
        }
        const d = remind.draft();
        root._profileId = String(d._targetProfileId || root._pickedProfileId || root._profileId);
        d._profileId = root._profileId;
        d.status = root.statusList()[statusBox.currentIndex];
        if (descField.text.trim().length > 0)
            d.desc = d.desc ? descField.text.trim() + "\n" + d.desc : descField.text;
        if (root._priorityPicked) d.priority = priBox.currentText;
        const typed = root.labelsFromText(labelsField.text);
        if (typed.length > 0) d.labels = root._mergeLabels(typed, d.labels || []);
        if (branchField.text.length > 0) d.branch = branchField.text;
        d.attachments = root._attachments.map(a => ({ id: a.id, name: a.name, size: a.size, mime: a.mime }));
        root._backToOwnProfile();
        AppController.beginUndoGroup(I18n.t("editor.undo.save").arg(d.id));
        try {
            if (!AppController.saveTask(d)) {
                root._error = I18n.t("editor.err.refused");
                return false;
            }
        } finally {
            AppController.endUndoGroup();
        }
        return true;
    }

    function _saveDraft(d, cleanedTitle, handleNames) {
        // A refused save (id taken, review without a branch) keeps the editor
        // open on the draft; closing it threw the user's edits away. The
        // controller's toast says why.
        if (!AppController.saveTask(d)) {
            root._error = I18n.t("editor.err.refused");
            return false;
        }
        // The parsed clock time now lives on the task itself, so a
        // focus block is no longer needed to keep it. A "sync" still
        // means a meeting, and a meeting is a calendar event.
        if (root.isNew
            && root._deadlinePreview && root._deadlinePreview.ok
            && root._deadlinePreview.hasTime
            && root._deadlinePreview.start) {
            const kind = root._classifyKind(
                (titleField.text || "") + " "
              + (descField.text || "") + " "
              + (deadlineField.text || ""));
            const dt = root._deadlinePreview.start;
            if (kind === "sync") {
                const startHour = dt.getHours() + dt.getMinutes() / 60.0;
                // Honour parsed range "12:00-13:00", else 30m.
                let endHour = startHour + 0.5;
                const pe = root._deadlinePreview.end;
                if (pe && pe.getTime && pe.getTime() > 0) {
                    const eh = pe.getHours() + pe.getMinutes() / 60.0;
                    if (eh > startHour) endHour = eh;
                }
                const ev = AppController.newEventDraft(startHour, dt);
                ev.type      = "sync";
                ev.title     = (cleanedTitle || "").substring(0, 40);
                ev.end       = endHour;
                ev.taskId    = d.id;
                ev.date      = dt;
                ev.attendees = handleNames.join(", ");
                AppController.saveEvent(ev);
            }
            // The calendar stays on the day the user was looking at: jumping
            // it to the task's date on save was a surprise, not a feature.
        }
        return true;
    }

    anchors.centerIn: Overlay.overlay

    // Keyboard-first: Escape already closes, but there was no way to commit
    // without reaching for the mouse. Plain Enter belongs to the text fields
    // (and would fight the multi-line description), so Ctrl+Enter saves.
    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: root.opened && !discardPrompt.opened
        onActivated: root._save()
    }
    // Attach files without reaching for the button.
    Shortcut {
        sequence: "Ctrl+Shift+A"
        enabled: root.opened && !discardPrompt.opened
        onActivated: attachDialog.open()
    }

    background: ModalSurface {
        border.color: fileDrop.containsDrag ? Theme.accent : Theme.modalBorder
        border.width: fileDrop.containsDrag ? 2 : 1
        // Files dropped anywhere on the editor are attached to the task.
        DropArea {
            id: fileDrop
            objectName: "te-file-drop"
            anchors.fill: parent
            keys: ["text/uri-list"]
            onEntered: (drag) => { drag.accepted = drag.hasUrls; }
            onDropped: (drop) => {
                if (!drop.hasUrls) return;
                if (root.attachUrls(drop.urls) > 0) drop.accept(Qt.CopyAction);
            }
        }
    }

    // What the dialog is for comes first — title, status, priority, when it
    // is due and what it says — then everything else behind "Details". It
    // used to be one 480px column of every field at once, with a 70px box for
    // the description, so a real ticket's text was read through a slot.
    contentItem: ColumnLayout {
        spacing: 0
        // Esc from any field bubbles up to here (text fields do not take it).
        Keys.onEscapePressed: (e) => { e.accepted = true; root.requestClose(); }

        // ── Header: what this is, and where it lives ──
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.topMargin: Theme.inset; Layout.bottomMargin: Theme.spXl
            spacing: Theme.spMd
            Text {
                text: root.isNew ? I18n.t("editor.new.task") : I18n.t("editor.edit.task")
                color: Theme.text
                font.pixelSize: Theme.fsLg
                font.weight: Theme.fwHeading
            }
            Text {
                visible: !root.isNew
                // A mirrored issue is known by its tracker key, not the id the
                // merge invented for it (HEAP-117).
                text: root._isTicket ? (root._ticket.key || "") : idField.text
                textFormat: Text.PlainText
                color: Theme.accentStrong
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsMd
                font.weight: Theme.fwTitle
                ClickArea {
                    objectName: "te-id-copy"
                    label: I18n.t("taskcard.copyId")
                    tip: I18n.t("taskcard.copyId.tip")
                    activeFocusOnTab: false
                    onActivated: AppController.copyTaskKey(parent.text)
                }
            }
            Item { Layout.fillWidth: true }
            // "в Работа ▾" (A5-14): where a new task goes, as quick capture
            // says it; another profile from the menu, or "+Name" in the title.
            Item {
                objectName: "te-profile"
                visible: root.isNew
                implicitWidth: teProfRow.implicitWidth
                implicitHeight: teProfRow.implicitHeight
                Layout.maximumWidth: Theme.px(220)
                Row {
                    id: teProfRow
                    spacing: Theme.spXs
                    Text {
                        objectName: "te-profile-name"
                        anchors.verticalCenter: parent.verticalCenter
                        text: I18n.t("capture.inProfile").arg(root._targetProfileName)
                        color: teProfCA.hovered ? Theme.text : Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSm
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, Theme.px(200))
                    }
                    Icon {
                        visible: (AppController.profiles || []).length > 1
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-down"
                        size: Theme.iconSize - 4
                        color: teProfCA.hovered ? Theme.text : Theme.textMuted
                    }
                }
                ClickArea {
                    id: teProfCA
                    label: I18n.t("capture.inProfile").arg(root._targetProfileName)
                    tip: I18n.t("capture.inProfileHint")
                    enabled: (AppController.profiles || []).length > 1
                    onActivated: teProfileMenu.popup(teProfRow, 0, teProfRow.height + Theme.spXs)
                }
                AppMenu {
                    id: teProfileMenu
                    Instantiator {
                        model: AppController.profiles
                        delegate: AppMenuItem {
                            id: teProfItem
                            required property var modelData
                            text: teProfItem.modelData.name
                        }
                        // Wired here, where the editor is in scope.
                        onObjectAdded: (idx, obj) => {
                            teProfileMenu.insertItem(idx, obj);
                            const item = obj as AppMenuItem;
                            const id = String((AppController.profiles[idx] || {}).id || "");
                            if (!item) return;
                            item.marked = Qt.binding(() => id === root._targetProfileId);
                            item.triggered.connect(() => {
                                root._pickedProfileId = id;
                                Qt.callLater(() => titleField.forceActiveFocus());
                            });
                        }
                        onObjectRemoved: (idx, obj) => teProfileMenu.removeItem(obj)
                    }
                }
            }
            PillButton {
                objectName: "te-ticket-open"
                visible: root._isTicket && String(root._ticket.url || "").length > 0
                text: "↗  " + I18n.t("ticket.open")
                onClicked: AppController.openTaskExternal(root._originalId || (root.draft.id || ""))
            }
        }

        ScrollView {
            id: bodyScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: bodyCol.implicitHeight
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: bodyCol
                width: bodyScroll.availableWidth
                spacing: Theme.spXl

                // ── Mirrored tracker issue: one line of whose it is (HEAP-117) ──
                // The rest of what the tracker says sits under Details.
                Rectangle {
                    objectName: "te-ticket-strip"
                    visible: root._isTicket
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    implicitHeight: stripCol.implicitHeight + 2 * Theme.spLg
                    radius: Theme.radius
                    color: Theme.withAlpha(Theme.panel2, 0.6)
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: stripCol
                        anchors.fill: parent
                        anchors.margins: Theme.spLg
                        spacing: Theme.spXs
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spSm
                            Text {
                                text: root._badge.icon || "◍"
                                textFormat: Text.PlainText
                                color: root._badge.color || Theme.textMuted
                                font.pixelSize: Theme.fsXs
                                font.weight: Theme.fwTitle
                            }
                            Text {
                                text: (root._badge.name || root._ticket.provider || "")
                                      + (root._ticket.project ? " · " + root._ticket.project : "")
                                textFormat: Text.PlainText
                                color: Theme.textMuted
                                font.pixelSize: Theme.fsSm
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("ticket.overwriteHint")
                            textFormat: Text.PlainText
                            color: Theme.textDim
                            font.pixelSize: Theme.fsXs
                            wrapMode: Text.WordWrap
                        }
                        // Both sides changed a field since the last sync: show
                        // the tracker's version and let the user pick one.
                        ColumnLayout {
                            objectName: "te-ticket-conflict"
                            visible: root._hasConflict
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spSm
                            spacing: Theme.spXs
                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("ticket.conflict.head")
                                textFormat: Text.PlainText
                                color: Theme.warning
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
                                    font.pixelSize: Theme.fsSm
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 4
                                    elide: Text.ElideRight
                                }
                            }
                            RowLayout {
                                spacing: Theme.spSm
                                PillButton {
                                    objectName: "te-conflict-use-tracker"
                                    text: I18n.t("ticket.conflict.useTracker")
                                    onClicked: root._takeTrackerVersion()
                                }
                                PillButton {
                                    objectName: "te-conflict-keep-mine"
                                    text: I18n.t("ticket.conflict.keepMine")
                                    onClicked: {
                                        AppController.resolveTrackerConflict(root._originalId, false);
                                        root._conflictResolved = true;
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Title ──
                // Wraps instead of scrolling sideways: a long title opened
                // scrolled to its end, the start off to the left and its first
                // visible letter cut (SCALE-6). Still one line of text — Enter
                // does not break it, a pasted newline becomes a space, and Tab
                // moves on.
                TextArea {
                    id: titleField
                    ContextMenu.menu: TextEditMenu { editor: titleField }
                    objectName: "te-title"
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    placeholderText: I18n.t("editor.ph.titleShort")
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    font.pixelSize: Theme.fsLg
                    font.weight: Theme.fwTitle
                    background: FieldBg {}
                    color: Theme.text
                    placeholderTextColor: Theme.textDim
                    Keys.onPressed: (event) => {
                        // "+Na…" offers the profiles (A5-21): arrows walk the
                        // list, Tab takes one, Enter only once one is picked.
                        if (titleAc.isOpen) {
                            if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                                titleAc.moveSelection(event.key === Qt.Key_Down ? 1 : -1);
                                event.accepted = true;
                                return;
                            }
                            if ((event.key === Qt.Key_Tab || ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                 && titleAc.navigated)) && titleAc.accept()) {
                                event.accepted = true;
                                return;
                            }
                            if (event.key === Qt.Key_Escape) {
                                titleAc.dismiss();
                                event.accepted = true;
                                return;
                            }
                        }
                        // A reminder to self: Tab makes it an ordinary task, ↓
                        // steps into the times (ReminderCapture).
                        if (root.isNew && remind.handleKey(event)) return;
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            // Ctrl+Enter is the save shortcut's. Plain Enter in a
                            // new task's title creates it, as quick capture does;
                            // it used to do nothing and say nothing (A2-11, A5-2).
                            if (!(event.modifiers & Qt.ControlModifier)) {
                                event.accepted = true;
                                if (root.isNew && !(event.modifiers & Qt.ShiftModifier)) root._save();
                            }
                            return;
                        }
                        if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab) return;
                        const back = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier);
                        const next = titleField.nextItemInFocusChain(!back);
                        if (next) next.forceActiveFocus(back ? Qt.BacktabFocusReason : Qt.TabFocusReason);
                        event.accepted = true;
                    }
                    onCursorPositionChanged: if (root.isNew) titleAc.refresh()
                    onTextChanged: {
                        titleReadTimer.restart();
                        // "Give the task a title" is answered by typing one
                        // (EYE-11); it stayed red until the next save.
                        if (root._error === I18n.t("editor.err.title") && text.trim().length > 0) root._error = "";
                        if (root.isNew) titleAc.refresh();
                        if (!/[\r\n\t]/.test(text)) return;
                        const at = cursorPosition;
                        text = text.replace(/[\r\n\t]+/g, " ");
                        cursorPosition = Math.min(at, text.length);
                    }
                    MentionAutocomplete {
                        id: titleAc
                        target: titleField
                        enablePeople: false
                        enableTickets: false
                        enableProfiles: true
                    }
                    Timer {
                        id: titleReadTimer
                        interval: 80
                        repeat: false
                        onTriggered: root._refreshTitleChips()
                    }
                }

                // "напомни мне …": what Enter makes, and the times when none
                // was said (dogfood 2026-10-22).
                ReminderCapture {
                    id: remind
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    text: root.isNew ? titleField.text : ""
                    onSubmitRequested: root._save()
                }

                // What a new task's title will give on save (A1-4): the chips
                // quick capture shows, so "tomorrow 3pm p1" is seen being read.
                Flow {
                    objectName: "te-title-read"
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    visible: root.isNew && root._titleChips.length > 0 && !remind.active
                    spacing: Theme.spSm
                    Repeater {
                        model: root._titleChips
                        delegate: PropertyChip {
                            required property var modelData
                            objectName: "te-read-" + modelData.kind
                            outlined: true
                            small: true
                            key: modelData.key
                            value: modelData.value
                        }
                    }
                }

                // ── Status · priority · deadline: what a developer reads first ──
                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    columns: 3
                    columnSpacing: Theme.spLg
                    rowSpacing: Theme.spXs
                    FieldLabel { text: I18n.t("editor.label.status") }
                    FieldLabel { text: I18n.t("editor.label.priority") }
                    FieldLabel { text: I18n.t("editor.label.deadline") }
                    AppComboBox {
                        id: statusBox
                        objectName: "te-status"
                        // The label above is a separate Text; a screen
                        // reader only had "combo box" (APP-168).
                        Accessible.name: I18n.t("editor.label.status")
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        // Never narrower than its longest column name: "In
                        // Progress" read "In Prog…" (UX-25).
                        Layout.minimumWidth: Math.min(240, statusMetrics.widest + 2 * Theme.spLg + 24)
                        FontMetrics { id: statusMetricsFont; font.pixelSize: Theme.fsMd }
                        QtObject {
                            id: statusMetrics
                            readonly property real widest: {
                                let w = 0;
                                const names = root.statusNames();
                                for (let i = 0; i < names.length; i++)
                                    w = Math.max(w, statusMetricsFont.advanceWidth(names[i]));
                                return Math.ceil(w);
                            }
                        }
                        model: root.statusNames()
                    }
                    AppComboBox {
                        id: priBox
                        objectName: "te-priority"
                        Accessible.name: I18n.t("editor.label.priority")
                        Layout.preferredWidth: 88
                        model: ["P0", "P1", "P2", "P3"]
                        displayText: root._titlePriority.length > 0 ? root._titlePriority : currentText
                        onActivated: { root._priorityPicked = true; root._refreshTitleChips(); }
                        textColor: Theme.priorityColor(priBox.displayText)
                        textWeight: Theme.fwTitle
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 2
                        Layout.minimumWidth: 160
                        spacing: Theme.spSm
                        TextField {
                            id: deadlineField
                            objectName: "te-deadline"
                            ContextMenu.menu: TextEditMenu { editor: deadlineField }
                            Layout.fillWidth: true
                            placeholderText: I18n.t("editor.ph.deadline")
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            background: Rectangle {
                                radius: Theme.radiusMd
                                color: Theme.panel2
                                border.color: deadlineField.text.length === 0
                                    ? (deadlineField.activeFocus ? Theme.focusRing : Theme.fieldBorder)
                                    : (root._deadlinePreview && root._deadlinePreview.ok
                                        ? Theme.accent
                                        : Theme.danger)
                                border.width: deadlineField.activeFocus ? 2 : 1
                            }
                            color: Theme.text
                            placeholderTextColor: Theme.textDim
                            onTextChanged: deadlinePreviewTimer.restart()
                            onEditingFinished: {
                                if (root._deadlinePreview && root._deadlinePreview.ok &&
                                    root._deadlinePreview.start) {
                                    text = root.formatWhen(root._deadlinePreview.start,
                                                           root._deadlinePreview.hasTime);
                                }
                            }
                            ToolTip.visible: (hovered || activeFocus) && text.length > 0 &&
                                root._deadlinePreview && !root._deadlinePreview.ok
                            ToolTip.text: I18n.t("editor.tip.unrecognized")
                        }
                        Timer {
                            id: deadlinePreviewTimer
                            interval: 80
                            repeat: false
                            onTriggered: root._refreshDeadlinePreview()
                        }
                        // Calendar picker — fills the deadline field with a chosen date.
                        Rectangle {
                            id: deadlineCalBtn
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: Theme.radiusMd
                            color: deadlineCalMA.hovered ? Theme.panel3 : Theme.panel2
                            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                            border.color: Theme.border; border.width: 1
                            Rectangle {   // mini calendar glyph
                                anchors.centerIn: parent
                                width: 15; height: 14; radius: Theme.radiusXs
                                color: "transparent"
                                border.color: Theme.textMuted; border.width: 1
                                Rectangle { width: parent.width; height: 3; color: Theme.textMuted; anchors.top: parent.top }
                            }
                            ClickArea {
                                id: deadlineCalMA
                                objectName: "te-deadline-pick"
                                label: I18n.t("editor.a11y.pickDeadline")
                                onActivated: {
                                    const seed = (root._deadlinePreview && root._deadlinePreview.ok && root._deadlinePreview.start)
                                        ? root._deadlinePreview.start : null;
                                    deadlinePicker.openAt(seed, deadlineCalBtn);
                                }
                            }
                            DatePickerPopup {
                                id: deadlinePicker
                                y: parent.height + 4
                                onPicked: (value) => {
                                    deadlineField.text = root.formatDate(value);
                                    root._refreshDeadlinePreview();
                                }
                            }
                        }
                    }
                    Item { Layout.columnSpan: 2; implicitHeight: 1 }
                    // What the deadline field was read as.
                    Text {
                        id: chipLabel
                        objectName: "te-deadline-read"
                        visible: text.length > 0
                        Layout.fillWidth: true
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        font.pixelSize: Theme.fsXs
                        color: Theme.textMuted
                        text: {
                            if (!root._deadlinePreview || !root._deadlinePreview.ok ||
                                !root._deadlinePreview.start) return "";
                            const d = root._deadlinePreview.start;
                            const when = "↑ " + (root._deadlinePreview.hasTime ? I18n.fmtDateTime(d, "weekdayDayYear")
                                                                                : I18n.fmtDate(d, "weekdayDayYear"));
                            // With no plan of its own, the task is planned at its
                            // deadline too; say so, the plan field is folded away (A5-23).
                            const planned = scheduledField.text.trim().length > 0
                                            || root._titleChips.some(c => c.kind === "when");
                            return planned ? when : when + " · " + I18n.t("editor.deadline.alsoPlanned");
                        }
                    }
                }

                // ── Description ──
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    spacing: Theme.spSm
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spXs
                        FieldLabel { text: I18n.t("editor.label.desc") }
                        Item { Layout.fillWidth: true }
                        SegChip {
                            objectName: "desc-expand"
                            text: root.descExpanded ? "⤡  " + I18n.t("editor.desc.collapse")
                                                    : "⤢  " + I18n.t("editor.desc.expand")
                            active: false
                            onClicked: root.descExpanded = !root.descExpanded
                        }
                    }
                    // Grows with the text, from a few lines up to a cap; past
                    // the cap it scrolls inside itself. Expand widens the dialog
                    // and lifts the cap for writing at length.
                    ScrollView {
                        id: descScroll
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(root._descMinH,
                                                         Math.min(root._descMaxH, descField.implicitHeight))
                        clip: true
                        // The frame on the scroll, not the text: the code box of
                        // rich drawing sits under the text and over this.
                        background: FieldBg { control: descField }
                        TextArea {
                            id: descField
                            ContextMenu.menu: TextEditMenu { editor: descField }
                            objectName: "te-desc"
                            placeholderText: I18n.t("editor.ph.desc")
                            wrapMode: TextEdit.Wrap
                            font.pixelSize: Theme.fsMd
                            topPadding: Theme.spLg; bottomPadding: Theme.spLg
                            leftPadding: Theme.spLg; rightPadding: Theme.spLg
                            background: Item {}
                            color: Theme.text
                            placeholderTextColor: Theme.textDim
                            selectByMouse: true
                            // Tab leaves the description, so Details, Save and
                            // Delete can be reached from the keyboard; it used to
                            // type a tab character and trap focus here. On a
                            // list line Tab / Shift+Tab indent and outdent the
                            // item, and Ctrl+Tab always inserts indentation.
                            // Ctrl+B / I / E / K format here, as in a note.
                            Keys.onShortcutOverride: (event) => {
                                if (descMd.claimsShortcut(event.key, event.modifiers)) event.accepted = true;
                            }
                            Keys.onPressed: (event) => {
                                if (event.matches(StandardKey.Paste) && root.pasteAttachment(descField)) {
                                    event.accepted = true;
                                    return;
                                }
                                if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab) {
                                    // Enter goes on with the list, the box, the quote
                                    // (as in a note). Ctrl+Enter is the dialog's
                                    // Create, never the box toggle it is in a note.
                                    const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
                                    if (enter && (event.modifiers & ~Qt.KeypadModifier) !== Qt.NoModifier) return;
                                    descMd.setSelection(descField.selectionStart, descField.selectionEnd);
                                    if (descMd.handleKey(event.key, event.modifiers)) event.accepted = true;
                                    return;
                                }
                                const back = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier);
                                if (event.modifiers & Qt.ControlModifier) {
                                    descField.insert(descField.cursorPosition, "  ");
                                    event.accepted = true;
                                    return;
                                }
                                if (root.indentListLine(descField, back)) {
                                    event.accepted = true;
                                    return;
                                }
                                const next = descField.nextItemInFocusChain(!back);
                                if (next) next.forceActiveFocus(back ? Qt.BacktabFocusReason : Qt.TabFocusReason);
                                event.accepted = true;
                            }
                        }
                    }
                    // A pasted error this workspace has met before (APP-159).
                    SeenBeforeHint {
                        Layout.fillWidth: true
                        text: descField.text
                        excludeTaskId: root._originalId
                        onActivated: (hit) => root.seenBeforeActivated(hit)
                    }
                }

                // ── Attachments ──
                ColumnLayout {
                    objectName: "te-attachments"
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    spacing: Theme.spSm
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spSm
                        FieldLabel {
                            text: I18n.t("att.label")
                                  + (root._attachments.length > 0 ? "  " + root._attachments.length : "")
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root._attachments.length === 0
                            text: I18n.t("att.hint.task")
                            color: Theme.textDim
                            font.pixelSize: Theme.fsXs
                            // Two lines before it gives up: a longer language
                            // lost the end of the hint (APP-189).
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Item { Layout.fillWidth: root._attachments.length > 0 }
                        PillButton {
                            objectName: "te-attach"
                            text: "📎  " + I18n.t("att.button")
                            onClicked: attachDialog.open()
                            ToolTip.visible: hovered || visualFocus
                            ToolTip.delay: 400
                            ToolTip.text: I18n.t("att.button.tip")
                        }
                    }
                    AttachmentChips {
                        id: attachmentChips
                        objectName: "te-attachment-chips"
                        visible: root._attachments.length > 0
                        Layout.fillWidth: true
                        model: root._attachments
                        onRemoveRequested: (attachmentId) => root.removeAttachment(attachmentId)
                    }
                }

                // ── Waiting on a reply (APP-158): who this task waits on ──
                // Only for a saved task, and only once the heads-up is on in
                // Settings → Safety net. Applies at once, like the timer.
                RowLayout {
                    id: waitingRow
                    objectName: "te-waiting"
                    readonly property var link: AppController.waitingOn[root._originalId]
                    visible: !root.isNew && root._originalId.length > 0
                             && !!(AppController.safety && AppController.safety.waitingOn)
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    spacing: Theme.spSm
                    FieldLabel { text: I18n.t("waiting.label") }
                    Text {
                        objectName: "te-waiting-who"
                        Layout.fillWidth: true
                        text: waitingRow.link
                              ? I18n.t("waiting.since").arg(waitingRow.link.name).arg(waitingRow.link.days)
                              : I18n.t("waiting.none")
                        textFormat: Text.PlainText
                        color: waitingRow.link ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.fsSm
                        elide: Text.ElideRight
                    }
                    PillButton {
                        objectName: "te-waiting-pick"
                        text: waitingRow.link ? I18n.t("waiting.change") : I18n.t("waiting.pick")
                        onClicked: waitingPicker.open_()
                    }
                    PillButton {
                        objectName: "te-waiting-clear"
                        visible: !!waitingRow.link
                        text: I18n.t("waiting.clear")
                        onClicked: AppController.clearWaitingOn(root._originalId)
                    }
                    PersonPicker {
                        id: waitingPicker
                        pickOnly: true
                        title: I18n.t("waiting.pickTitle")
                        onPersonPicked: (personId) => AppController.setWaitingOn(root._originalId, personId)
                    }
                }

                // ── Details: everything that is not needed to pick the task up ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    implicitHeight: 1
                    color: Theme.border
                }
                Item {
                    id: detailsToggle
                    objectName: "te-details-toggle"
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    implicitHeight: detailsRow.implicitHeight + Theme.spSm
                    // On the Tab path between the description and the footer.
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: I18n.t("editor.details")
                    Keys.onSpacePressed: root.detailsOpen = !root.detailsOpen
                    Keys.onReturnPressed: root.detailsOpen = !root.detailsOpen
                    Keys.onEnterPressed: root.detailsOpen = !root.detailsOpen
                    FocusRing {
                        anchors.margins: -Theme.sp2xs
                        radius: Theme.radiusSm
                    }
                    RowLayout {
                        id: detailsRow
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spMd
                        Text {
                            text: (root.detailsOpen ? "▾  " : "▸  ") + I18n.t("editor.details")
                            color: detailsMA.containsMouse ? Theme.text : Theme.textMuted
                            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                            font.pixelSize: Theme.fsMd
                            font.weight: Theme.fwTitle
                        }
                        // What is filled in behind the fold, so a closed Details
                        // still says whether there is anything to look at.
                        Text {
                            visible: !root.detailsOpen
                            Layout.fillWidth: true
                            text: root._detailsSummary
                            color: Theme.textDim
                            font.pixelSize: Theme.fsSm
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        id: detailsMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.detailsOpen = !root.detailsOpen
                    }
                }

                GridLayout {
                    id: detailsGrid
                    visible: root.detailsOpen
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    columns: 2
                    columnSpacing: Theme.spLg
                    rowSpacing: Theme.spXs

                    FieldLabel { Layout.fillWidth: true; Layout.preferredWidth: 1; text: I18n.t("editor.label.ticketId") }
                    FieldLabel { Layout.fillWidth: true; Layout.preferredWidth: 1; text: I18n.t("editor.label.branch") }
                    TextField {
                        id: idField
                        ContextMenu.menu: TextEditMenu { editor: idField }
                        objectName: "te-id"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        // New tasks: TODO placeholder — final id is generated on save.
                        // Edit: pre-filled with the current id (still editable).
                        placeholderText: root.isNew ? I18n.t("editor.ph.ticketId")
                                                    : I18n.t("editor.ph.ticketIdEdit")
                        font.family: Theme.fontMono
                        background: FieldBg {}
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        // Auto-uppercase so a hand-typed id stays canonical (matches
                        // newTaskDraft) — but only while composing a NEW one. A synced
                        // ticket's id is lowercase by construction ("github-1234", from the
                        // provider id), and uppercasing it on open made a plain Save look
                        // like a rename to saveTask: the row was silently re-keyed to
                        // GITHUB-1234, and if that id was taken, the other task was lost.
                        onTextChanged: {
                            if (!root.isNew) return;
                            const up = text.toUpperCase();
                            if (up !== text) text = up;
                        }
                    }
                    TextField {
                        id: branchField
                        ContextMenu.menu: TextEditMenu { editor: branchField }
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        placeholderText: "fix/..."
                        font.family: Theme.fontMono
                        background: FieldBg {}
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                    }

                    FieldLabel { Layout.fillWidth: true; Layout.preferredWidth: 1; text: I18n.t("editor.label.scheduled") }
                    FieldLabel { Layout.fillWidth: true; Layout.preferredWidth: 1; text: I18n.t("editor.label.recurrence") }
                    TextField {
                        id: scheduledField
                        onTextChanged: titleReadTimer.restart()
                        ContextMenu.menu: TextEditMenu { editor: scheduledField }
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        placeholderText: I18n.t("editor.ph.deadline")
                        font.family: Theme.fontUi
                        font.features: Theme.tabularNums
                        background: FieldBg {}
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                    }
                    AppComboBox {
                        id: recurBox
                        objectName: "te-recurrence"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        readonly property var _base: ["", "every:day", "every:week", "every:weekday",
                                                      "every:mon", "every:tue", "every:wed", "every:thu", "every:fri",
                                                      "every:sat", "every:sun", "every:month", "every:2:week"]
                        // A rule the list does not name ("every:month:15", from
                        // capture) is shown as itself, so saving the editor
                        // does not quietly turn it into "none".
                        property string _extra: ""
                        // The last row, "Custom interval…" (B7), is not a rule:
                        // it opens a field over the box where the interval is
                        // typed ("every 3 weeks", "пн, ср").
                        readonly property var _vals: (_extra.length > 0 ? _base.concat([_extra]) : _base).concat(["custom"])
                        property int _prevIndex: 0
                        property bool _customOpen: false
                        property bool _customBad: false
                        function setRecurrence(r) {
                            _extra = (r.length > 0 && _base.indexOf(r) < 0) ? r : "";
                            currentIndex = Math.max(0, _vals.indexOf(r));
                            _customOpen = false;
                        }
                        function value() {
                            const v = _vals[currentIndex] || "";
                            return v === "custom" ? "" : v;
                        }
                        function _label(r) {
                            const interval = TaskDates.intervalTitle(r);
                            if (interval.length > 0) return interval;
                            const m = /^every:month:(\d+)$/.exec(r);
                            return m ? I18n.t("editor.recur.monthlyOn").arg(m[1]) : r;
                        }
                        function openCustom() {
                            recurBox.currentIndex = recurBox._prevIndex;
                            recurBox._customBad = false;
                            recurBox._customOpen = true;
                            recurCustomField.text = "";
                            Qt.callLater(recurCustomField.forceActiveFocus);
                        }
                        function applyCustom(text) {
                            const rule = AppController.recurrenceFromText(text);
                            if (rule.length === 0) { recurBox._customBad = true; return false; }
                            recurBox.setRecurrence(rule);
                            recurBox.forceActiveFocus();
                            return true;
                        }
                        onCurrentIndexChanged: { titleReadTimer.restart(); if (_vals[currentIndex] !== "custom") _prevIndex = currentIndex; }
                        onActivated: (i) => { if (recurBox._vals[i] === "custom") recurBox.openCustom(); }
                        // Down on the closed box steps the rules; it stops at
                        // the last one rather than opening the custom field.
                        Keys.onDownPressed: (e) => {
                            e.accepted = !recurBox.popup.visible && recurBox._vals[recurBox.currentIndex + 1] === "custom";
                        }
                        model: [I18n.t("editor.recur.none"), I18n.t("editor.recur.daily"),
                                I18n.t("editor.recur.weekly"), I18n.t("editor.recur.weekdays"),
                                I18n.t("editor.recur.everyMon"), I18n.t("editor.recur.everyTue"),
                                I18n.t("editor.recur.everyWed"), I18n.t("editor.recur.everyThu"),
                                I18n.t("editor.recur.everyFri"), I18n.t("editor.recur.everySat"),
                                I18n.t("editor.recur.everySun"), I18n.t("editor.recur.monthly"),
                                I18n.t("recur.biweekly")]
                               .concat(_extra.length > 0 ? [_label(_extra)] : [])
                               .concat([I18n.t("recur.custom")])
                        // Over the box while it is open: Enter sets the rule,
                        // Esc leaves the one there was.
                        TextField {
                            id: recurCustomField
                            objectName: "te-recurrence-custom"
                            visible: recurBox._customOpen
                            anchors.fill: parent
                            z: 2   // over the box's caret too
                            font.family: Theme.fontUi
                            color: Theme.text
                            placeholderText: I18n.t("recur.custom.ph")
                            placeholderTextColor: Theme.textDim
                            background: FieldBg {
                                border.color: recurBox._customBad ? Theme.danger : focused ? Theme.focusRing : Theme.fieldBorder
                            }
                            onTextChanged: recurBox._customBad = false
                            onAccepted: recurBox.applyCustom(recurCustomField.text)
                            onActiveFocusChanged: if (!activeFocus) recurBox._customOpen = false
                            Keys.onEscapePressed: (e) => { recurBox._customOpen = false; recurBox.forceActiveFocus(); e.accepted = true; }
                            Keys.onReturnPressed: (e) => { recurCustomField.accepted(); e.accepted = true; }
                            Keys.onEnterPressed: (e) => { recurCustomField.accepted(); e.accepted = true; }
                            ToolTip.visible: recurCustomField.visible && recurCustomField.activeFocus
                            ToolTip.text: recurBox._customBad ? I18n.t("recur.custom.err") : I18n.t("recur.custom.hint")
                        }
                    }

                    FieldLabel { Layout.fillWidth: true; Layout.preferredWidth: 1; text: I18n.t("editor.label.labels") }
                    FieldLabel { Layout.fillWidth: true; Layout.preferredWidth: 1; text: I18n.t("editor.label.estimate") }
                    TextField {
                        id: labelsField
                        ContextMenu.menu: TextEditMenu { editor: labelsField }
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        placeholderText: "backlog, infra"
                        background: FieldBg {}
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: Theme.spMd
                        TextField {
                            id: estimateField
                        onTextChanged: titleReadTimer.restart()
                            ContextMenu.menu: TextEditMenu { editor: estimateField }
                            Layout.fillWidth: true
                            placeholderText: "45"
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            validator: IntValidator { bottom: 0; top: 100000 }
                            background: Item {}
                            color: Theme.text
                            placeholderTextColor: Theme.textDim
                        }
                        // A toggle in the same pill style as every other button
                        // here; the stock CheckBox was a black square.
                        PillButton {
                            id: somedayBox
                            objectName: "te-someday"
                            checkable: true
                            selected: checked
                            text: (checked ? "☾  " : "☽  ") + I18n.t("editor.someday")
                            // Parking a task as "someday" files it under Backlog. Reflect
                            // that in the status box immediately; saveTask enforces it too.
                            onCheckedChanged: if (checked) {
                                const bi = root.statusList().indexOf("backlog");
                                if (bi >= 0) statusBox.currentIndex = bi;
                            }
                            ToolTip.visible: hovered || visualFocus
                            ToolTip.delay: 400
                            ToolTip.text: I18n.t("editor.someday.hint")
                        }
                    }
                }

                // What the tracker says about the issue, and its comments.
                ColumnLayout {
                    visible: root.detailsOpen && root._isTicket
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    spacing: Theme.spSm
                    FieldLabel { text: I18n.t("editor.details.tracker") }
                    // One "label value" pair per field the tracker actually gave.
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.sp2xl
                        Repeater {
                            model: root._ticketFacts
                            delegate: Row {
                                required property var modelData
                                spacing: Theme.spXs
                                Text {
                                    text: modelData.label
                                    textFormat: Text.PlainText
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fsSm
                                }
                                Text {
                                    text: modelData.value
                                    textFormat: Text.PlainText
                                    color: Theme.text
                                    font.pixelSize: Theme.fsSm
                                }
                            }
                        }
                    }
                    // Comments are read on demand and kept only while this
                    // dialog is open — heap stores none of them.
                    PillButton {
                        objectName: "te-ticket-load-comments"
                        visible: !root._commentsRequested
                        text: "❝  " + I18n.t("ticket.loadComments")
                        onClicked: {
                            root._commentsRequested = true;
                            AppController.fetchTicketComments(root._originalId || (root.draft.id || ""));
                        }
                    }
                    Text {
                        objectName: "te-ticket-comments-status"
                        visible: root._commentsRequested
                                 && (root._commentsError.length > 0 || root._comments.length === 0)
                        text: root._commentsError.length > 0 ? root._commentsError : I18n.t("ticket.noComments")
                        textFormat: Text.PlainText
                        color: Theme.textDim
                        font.pixelSize: Theme.fsSm
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: root._comments
                        delegate: ColumnLayout {
                            id: commentRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.sp2xs
                            RowLayout {
                                spacing: Theme.spSm
                                Text {
                                    text: "@" + String(modelData.author || "")
                                          + (modelData.createdAt && modelData.createdAt.getTime
                                             && !isNaN(modelData.createdAt.getTime())
                                             ? " · " + AppController.shortDate(modelData.createdAt) : "")
                                    textFormat: Text.PlainText
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fsXs
                                }
                                // Every tracker's comments can be opened where
                                // they live; Jira and GitLab had no link at all.
                                Text {
                                    objectName: "te-comment-link"
                                    visible: String(modelData.url || "").length > 0
                                    text: "↗ " + I18n.t("ticket.openComment")
                                    // Hover underlines rather than turning
                                    // to `accent`, which is 3.7:1 on heap.
                                    // light (design audit DES-25).
                                    color: Theme.accentStrong
                                    font.underline: commentLinkMA.hovered || commentLinkMA.keyboardFocused
                                    font.pixelSize: Theme.fsXs
                                    ClickArea {
                                        id: commentLinkMA
                                        role: Accessible.Link
                                        label: I18n.t("ticket.a11y.openComment").arg(String(commentRow.modelData.author || ""))
                                        onActivated: {
                                            const url = String(commentRow.modelData.url || "");
                                            if (AppController.isSafeLink(url)) Qt.openUrlExternally(url);
                                        }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData.body || "")
                                // Written by whoever commented upstream.
                                textFormat: Text.PlainText
                                color: Theme.text
                                font.pixelSize: Theme.fsSm
                                wrapMode: Text.WordWrap
                                maximumLineCount: 6
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                // ── History (APP-165): what happened to this task, newest
                // first. Folded by default; the fold says how many events.
                Rectangle {
                    visible: historyToggle.visible
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    implicitHeight: 1
                    color: Theme.border
                }
                Item {
                    id: historyToggle
                    objectName: "te-history-toggle"
                    visible: !root.isNew && root._history.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    implicitHeight: historyHead.implicitHeight + Theme.spSm
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: historyHead.text
                    Keys.onSpacePressed: root.historyOpen = !root.historyOpen
                    Keys.onReturnPressed: root.historyOpen = !root.historyOpen
                    Keys.onEnterPressed: root.historyOpen = !root.historyOpen
                    FocusRing {
                        anchors.margins: -Theme.sp2xs
                        radius: Theme.radiusSm
                    }
                    Text {
                        id: historyHead
                        objectName: "te-history-head"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: (root.historyOpen ? "▾  " : "▸  ") + I18n.t("editor.history").arg(root._history.length)
                        color: historyMA.containsMouse ? Theme.text : Theme.textMuted
                        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                        font.pixelSize: Theme.fsMd
                        font.weight: Theme.fwTitle
                    }
                    MouseArea {
                        id: historyMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.historyOpen = !root.historyOpen
                    }
                }
                ColumnLayout {
                    objectName: "te-history"
                    visible: root.historyOpen && historyToggle.visible
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
                    spacing: Theme.spXs
                    Repeater {
                        model: root.historyOpen ? root._historyRows : []
                        delegate: RowLayout {
                            id: histRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spMd
                            Text {
                                text: histRow.modelData.when
                                color: Theme.textDim
                                font.family: Theme.fontUi
                                font.features: Theme.tabularNums
                                font.pixelSize: Theme.fsXs
                            }
                            Text {
                                objectName: "te-history-line"
                                Layout.fillWidth: true
                                text: histRow.modelData.text
                                textFormat: Text.PlainText
                                color: Theme.text
                                font.pixelSize: Theme.fsSm
                                wrapMode: Text.Wrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Item { implicitHeight: 1 }
            }
        }

        // ── Footer: always on screen, however long the body gets ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        Text {
            objectName: "te-error"
            visible: root._error.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.topMargin: Theme.spMd
            text: root._error
            textFormat: Text.PlainText
            color: Theme.danger
            font.pixelSize: Theme.fsSm
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.inset; Layout.rightMargin: Theme.inset
            Layout.topMargin: Theme.spXl; Layout.bottomMargin: Theme.spXl
            spacing: Theme.spMd
            PillButton {
                objectName: "te-delete"
                visible: !root.isNew
                text: I18n.t("common.delete")
                danger: true
                onClicked: {
                    // Delete the row that was opened, keyed by the stable
                    // open-time id — NOT the live idField, which the user may
                    // have edited (Save threads _originalId for the same reason).
                    root._backToOwnProfile();
                    AppController.deleteTask(root._originalId);
                    root.close();
                }
            }
            // Archive one task without leaving the editor for a menu. Pending
            // edits are saved first, so the archived task is the edited one.
            PillButton {
                objectName: "te-archive"
                visible: !root.isNew
                text: root._archived ? I18n.t("editor.btn.unarchive") : I18n.t("editor.btn.archive")
                onClicked: {
                    const wasArchived = root._archived;
                    if (root.isDirty() && !root._commit()) return;
                    const id = idField.text.trim().length > 0 ? idField.text.trim() : root._originalId;
                    root._backToOwnProfile();
                    AppController.setArchived(id, !wasArchived);
                    root.close();
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "Ctrl+Enter"
                color: Theme.textDim
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsXs
            }
            PillButton {
                text: I18n.t("common.cancel")
                onClicked: root.close()
            }
            PillButton {
                text: root.isNew ? I18n.t("editor.btn.create") : I18n.t("editor.btn.save")
                primary: true
                onClicked: root._save()
            }
        }
    }

    // ── Unsaved changes: save, discard or keep editing ──
    Popup {
        PopupArrival { popup: discardPrompt }
        id: discardPrompt
        objectName: "te-discard-prompt"
        parent: root.contentItem
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        padding: Theme.inset
        width: Math.min(380, root.width - 2 * Theme.inset)
        background: ModalSurface {}
        Overlay.modal: ModalScrim {}
        // Bare letters decided it: typing on after Esc, a word that began
        // with d discarded the task and the rest of it hit the board's keys
        // (IDIOT-DOC-19). Only Esc, Enter and Ctrl+D answer now, not in the
        // first moments, and every other key stays here.
        property real _openedAt: 0
        onOpened: { discardPrompt._openedAt = Date.now(); promptBody.forceActiveFocus(); }
        function keep() { root._afterPrompt = null; discardPrompt.close(); titleField.forceActiveFocus(); }
        function discard() { discardPrompt.close(); root.close(); root._runAfterPrompt(); }
        // A refused save keeps the editor open and the next step waiting.
        function save() { discardPrompt.close(); if (root._save()) root._runAfterPrompt(); else root._afterPrompt = null; }
        contentItem: ColumnLayout {
            id: promptBody
            spacing: Theme.spLg
            focus: true
            Keys.onPressed: (e) => {
                if (e.key === Qt.Key_Tab || e.key === Qt.Key_Backtab) return;  // to the buttons
                e.accepted = true;
                if (e.isAutoRepeat || Date.now() - discardPrompt._openedAt < 400) return;  // the burst it opened in
                if (e.key === Qt.Key_Escape) discardPrompt.keep();
                else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) discardPrompt.save();
                else if (e.key === Qt.Key_D && (e.modifiers & Qt.ControlModifier)) discardPrompt.discard();
            }
            Text {
                Layout.fillWidth: true
                text: I18n.t("editor.dirty.title")
                color: Theme.text
                font.pixelSize: Theme.fsLg
                font.weight: Theme.fwHeading
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: I18n.t("editor.dirty.body")
                color: Theme.textMuted
                font.pixelSize: Theme.fsMd
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spMd
                PillButton {
                    objectName: "te-dirty-discard"
                    text: I18n.t("editor.dirty.discard") + "  " + AppController.keyText("Ctrl+D")
                    danger: true
                    onClicked: discardPrompt.discard()
                }
                Item { Layout.fillWidth: true }
                PillButton {
                    objectName: "te-dirty-keep"
                    text: I18n.t("editor.dirty.keep") + "  Esc"
                    onClicked: discardPrompt.keep()
                }
                PillButton {
                    objectName: "te-dirty-save"
                    text: I18n.t("editor.btn.save") + "  ↵"
                    primary: true
                    onClicked: discardPrompt.save()
                }
            }
        }
    }

    // A small toggle chip for the description's mode row.
    component SegChip: Rectangle {
        id: seg
        property string text
        property bool active: false
        signal clicked()
        radius: Theme.radiusPill
        color: active ? Theme.accentSoft : (segMA.containsMouse ? Theme.panel3 : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
        border.color: activeFocus ? Theme.focusRing : active ? Theme.withAlpha(Theme.accent, 0.5) : "transparent"
        border.width: activeFocus ? 2 : 1
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: seg.text
        Keys.onSpacePressed: seg.clicked()
        Keys.onReturnPressed: seg.clicked()
        implicitWidth: segT.implicitWidth + 16
        implicitHeight: segT.implicitHeight + 6
        Text {
            id: segT
            anchors.centerIn: parent
            text: seg.text
            color: seg.active ? Theme.accentStrong : (segMA.containsMouse ? Theme.text : Theme.textMuted)
            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
            font.pixelSize: Theme.fsSm
        }
        MouseArea {
            id: segMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: seg.clicked()
        }
    }

    // The description drawn as a note is in rich (APP-313): one text, its
    // markup shown only on the line being edited. It replaced Text / Preview.
    MdLivePreview {
        editor: descField
        headingScale: 1.3
        onInternalLinkActivated: (kind, target) => root.descLinkActivated(kind, target)
    }
    // The description's markdown keys (MdEditOps), shared with Notes.
    MarkdownEditorController {
        id: descMd
        target: descField.textDocument
        cursorPosition: descField.cursorPosition
        selectionStart: descField.selectionStart
        selectionEnd: descField.selectionEnd
        onSelectionRequested: (start, end) => {
            if (start === end) descField.cursorPosition = start;
            else descField.select(start, end);
        }
    }

    component FieldLabel: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fsSm
        font.weight: Theme.fwTitle
        topPadding: Theme.sp2xs
    }
    component FieldBg: FieldFrame {}

}
