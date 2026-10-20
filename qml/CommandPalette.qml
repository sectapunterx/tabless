// The command line (APP-267, sheets Q-Command / H2-Command): Ctrl+K, Ctrl+P,
// ":" — search, filter and act in one line. What is typed is read in the
// language quick capture speaks ("заблок p0 до пт оформ"): a finished
// condition becomes a chip, the rest searches. Results come in groups —
// the tasks found, what to do with them, what to do with this filter, the
// task the cursor was on, the commands, and notes, docs and people — each
// action with its key. ">" (or ":") leaves only the commands.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp
import "PaletteMatch.js" as Match
import "KeyRules.js" as KeyRules
import "SettingsIndex.js" as SettingsIdx

Popup {
    PopupArrival { popup: root }
    id: root
    objectName: "command-line"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0
    // H2-Command: a 700 px line at y 100 with the syntax card 20 px beside
    // it; Q-Command: 620 px, centred, at y 130, no card (DG-082, DG-083).
    readonly property bool _quiet: !Style.fills
    readonly property bool _cardShown: root.syntaxShown && !root._quiet
    readonly property int _lineW: root._quiet ? Theme.px(620) : Theme.px(700)
    readonly property int _cardW: Theme.px(320)
    readonly property real _avail: Overlay.overlay ? Overlay.overlay.width - 2 * Theme.sp3xl : 960
    readonly property bool _cardFits: root._cardShown && root._avail >= root._lineW + root._cardW + Theme.sp2xl
    width: Math.min(root._avail, root._lineW + (root._cardFits ? root._cardW + Theme.sp2xl : 0))
    x: Overlay.overlay ? Math.round((Overlay.overlay.width - root.width) / 2) : 0
    y: root._quiet ? Theme.px(130) : Theme.px(100)
    height: root.contentItem ? root.contentItem.implicitHeight : Theme.px(520)
    // The results scroll past this; the line grows with them up to it.
    readonly property real _listMax: Math.max(Theme.px(160), Math.min(Theme.px(480),
        (Overlay.overlay ? Overlay.overlay.height : 900) - root.y - Theme.px(160)))

    // Dimmed backdrop so the underlying app stays visible behind the popup.
    Overlay.modal: ModalScrim {}

    // Catalogue entries are found in the Knowledge list (DG-070): the text
    // to search it for.
    signal navigateToDoc(string text)
    signal navigateToSnippets(string text)
    signal navigateToContacts(string text)
    signal openTask(string taskId)
    signal openPerson(string personId)
    // A note hit carries the line its section starts on, so opening it lands
    // the reader where the words were rather than at the top of the note.
    signal navigateToNoteLine(int line)
    signal navigateToDocPage(string pageId)
    // Words met only in notes' bodies: the notes list, filtered by them (A4-8).
    signal searchNotes(string text)

    // A command the palette cannot run itself (it needs a popup Main owns):
    // Main.runCommand(id) handles it. Ids are the shortcut catalog's
    // ("view.board", "task.new", "theme.toggle"…) plus "settings:<section>"
    // and a palette-only one ("event.new").
    signal commandRequested(string id)
    // This filter in Tasks: on the board, as a list, or saved as a view
    // (mode "board" | "list" | "save").
    signal queryRequested(string query, string mode)
    // "Nothing found · create «…»": the task input with the words in it.
    signal createRequested(string text)
    // "Перенести в профиль…" for the task(s) the line opened on (A5-14).
    signal moveToProfileRequested(var ids)

    // function () → the ids of the task(s) the cursor or selection was on
    // when the line opened (Main's _keyTaskIds): the "For the selected task"
    // group acts on them.
    property var contextProvider: null
    property var _context: []

    property var _entries: []           // cached full list
    property var _rows: []              // what the list shows: headers and entries
    property var _matches: []           // the entries of _rows, in order — what ↑↓ walk
    property int _selectedIdx: 0

    // Conditions already turned into chips: [{kind, clause, value, words}].
    property var chips: []
    // The parse of the field, for the "Tab →" hint and the results.
    property var _parse: ({ tokens: [], text: "", tasks: [], total: 0, query: "" })
    property bool _sync: false

    // The syntax card beside the results (hideable, remembered).
    readonly property bool syntaxShown: {
        const s = root._settingsObj();
        return !(s.commandLine && s.commandLine.syntax === false);
    }
    function toggleSyntax() {
        const s = root._settingsObj();
        s.commandLine = Object.assign({}, s.commandLine || {}, { syntax: !root.syntaxShown });
        AppController.appSettingsJson = JSON.stringify(s);
    }

    // Views a task editor can open over without losing the reader's place.
    readonly property var _taskViews: ["board", "list", "week", "month"]

    // Catalog actions that only mean something on a surface, with a cursor or
    // a selection — offered by the palette they would do nothing. A second
    // key of an action (".alt") is the same command.
    readonly property var _contextual: ["palette.open", "palette.commands", "task.openExternal", "undo", "redo"]
    function _isContextual(id) {
        // savedView.N is offered by name instead ("View: Urgent"), below.
        return _contextual.indexOf(id) >= 0 || id.indexOf("board.") === 0 || id.indexOf("savedView.") === 0
            || id.indexOf("cal.") === 0 || id.indexOf("selection.") === 0 || id.indexOf("cursor.") === 0
            || id.indexOf("nav.") === 0 || /\.alt\d*$/.test(id)
            || (id.indexOf("task.") === 0 && id !== "task.new")
            // Notes are reached by "Перейти в «Заметки»" (section.knowledge);
            // Docs, the other sidebar row, by "Перейти в «Доки»" (A6-27).
            || id === "view.notes"
            // Deleting is the key's on the list or document that holds the
            // keyboard, not a command over whatever was open last.
            || id.indexOf("notes.delete") === 0 || id.indexOf("editor.") === 0;
    }

    // Catalog actions whose palette name says more than their hotkey label.
    // Words a command is looked for by that are not in its name (A5-5): a
    // panicked "восстановить" / "backup" found nothing but "create a task".
    readonly property var _paletteKeywords: ({
        "timeMachine.open": "palette.kw.timeMachine"
    })
    function _keywordsFor(id) {
        return _paletteKeywords[id] ? I18n.t(_paletteKeywords[id]) : (id === "settings:data" ? I18n.t("palette.kw.data") : "");
    }

    readonly property var _paletteLabels: ({
        "timeMachine.open": "palette.cmd.timeMachine",
        "standup.draft":    "palette.cmd.standupDraft",
        "recap.open":       "palette.cmd.weeklyRecap",
        "endOfDay.open":    "palette.cmd.endOfDay",
        "welcome.replay":   "palette.cmd.replayTour"
    })

    // The sections Settings has, from its own catalogue: a hand-kept list
    // still offered "Profile · Name, role, team", gone since DG-101 (F3-EYES-2).
    readonly property var _settingsSections: SettingsIdx.SECTIONS.map(s => s.id)
    // The English name of a command beside the Russian one: engineers type
    // "sett", "backup" whatever the UI language (F3-EYES-7).
    function _withEnglish(words, enLabel) {
        if (I18n.lang === "en" || !enLabel) return words || "";
        return ((words || "") + " " + enLabel).trim();
    }
    function _enOf(key) { return root._withEnglish("", I18n.dict.en[key] || ""); }

    // Commands: every app-wide action in the shortcut catalog (so the palette
    // and the keys never disagree on what exists or what it is called), each
    // Settings section, and a few that have no key of their own.
    function _commands() {
        const out = [];
        const list = AppController.shortcuts;
        for (let i = 0; i < list.length; i++) {
            const c = list[i];
            if (_isContextual(c.id)) continue;
            // Focus mode (APP-160) is offered once Settings → Safety net
            // turns it on, and says which way it goes.
            if (c.id === "focus.immersion") {
                if (!(AppController.safety && AppController.safety.immersion)) continue;
                out.push({ kind: "command", commandId: c.id, sub: AppController.keyText(c.sequence || ""), body: c.description || "",
                           label: AppController.immersion ? I18n.t("palette.cmd.immersionOff") : c.label });
                continue;
            }
            // The standup draft (APP-170), once Settings → Safety net turns it on.
            if (c.id === "standup.draft" && !(AppController.safety && AppController.safety.standupDraft)) continue;
            // The 0.6 tools read clearer in the palette with what they do.
            const label = _paletteLabels[c.id] ? I18n.t(_paletteLabels[c.id]) : c.label;
            out.push({ kind: "command", commandId: c.id, label: label, sub: AppController.keyText(c.sequence || ""),
                       body: c.description || "",
                       keywords: root._withEnglish(root._keywordsFor(c.id),
                                                   _paletteLabels[c.id] ? I18n.dict.en[_paletteLabels[c.id]] : c.labelEn) });
        }
        // A new event at 9:00 of the selected day; the catalog's key for a
        // new event (at the next free slot) is the nearest thing to a hint.
        out.push({ kind: "command", commandId: "event.new", label: I18n.t("palette.cmd.newEvent"), keywords: root._enOf("palette.cmd.newEvent"),
                   sub: AppController.shortcutText("cal.newEvent") });
        // Saved views: one command per view, by name, plus saving the current
        // filters as one.
        const views = AppController.savedViews;
        for (let v = 0; v < views.length; v++) {
            out.push({ kind: "command", commandId: "savedview:" + views[v].id,
                       label: I18n.t("palette.cmd.savedView").arg(views[v].name),
                       sub: v < 9 ? AppController.shortcutText("savedView." + (v + 1)) : "",
                       body: views[v].query });
        }
        out.push({ kind: "command", commandId: "savedview.save", label: I18n.t("palette.cmd.saveView"), sub: "",
                   keywords: root._enOf("palette.cmd.saveView") });
        // What left the selection bar, the profile menu and the column menu
        // for the sheets (DG-025, DG-026, DG-151): still one command away.
        // "когда-нибудь" / "someday" / "отложить" find parking the task in
        // front of the user (A1-15); Main says when there is none.
        out.push({ kind: "command", commandId: "task.someday", label: I18n.t("palette.cmd.someday"), sub: "",
                   body: I18n.t("palette.cmd.someday.words"), aliases: I18n.t("palette.cmd.someday.words") });
        if (AppController.selectionCount > 0) {
            out.push({ kind: "command", commandId: "selection.label", label: I18n.t("palette.cmd.selLabel"), sub: "" });
            out.push({ kind: "command", commandId: "selection.carry", label: I18n.t("palette.cmd.selCarry"), sub: "" });
        }
        for (const pc of ["profile.duplicate", "ics.import", "ics.export", "vault.import", "vault.export"])
            out.push({ kind: "command", commandId: pc, label: I18n.t("palette.cmd." + pc), sub: "",
                       keywords: root._enOf("palette.cmd." + pc) });
        if (AppController.currentView === "board")
            for (const cc of ["color", "archive", "doing"])
                out.push({ kind: "command", commandId: "column:" + cc, label: I18n.t("palette.cmd.column." + cc), sub: "",
                           keywords: root._enOf("palette.cmd.column." + cc) });
        // Integrations health (APP-164) lives at the top of that section.
        out.push({ kind: "setting", commandId: "settings:integrations", label: I18n.t("palette.cmd.integrationsHealth"),
                   sub: I18n.t("health.hint"), keywords: root._enOf("palette.cmd.integrationsHealth") });
        for (let j = 0; j < _settingsSections.length; j++) {
            const id = _settingsSections[j];
            // "Стабильная 0.9.5" / "Feature-сборка · …": which build, as the
            // page's header has it (F3-EYES-3).
            const sub = I18n.t("settings.section." + id + ".sub");
            const enTitle = I18n.dict.en["settings.section." + id + ".title"] || "";
            out.push({ kind: "setting", commandId: "settings:" + id,
                       label: I18n.t("palette.cmd.settings").arg(I18n.t("settings.section." + id + ".title")),
                       sub: id === "about" ? sub.replace("%1", Brand.buildLine()) : sub,
                       keywords: root._withEnglish(root._keywordsFor("settings:" + id),
                                                   enTitle ? I18n.dict.en["palette.cmd.settings"].arg(enTitle) : "") });
        }
        return out;
    }

    // ── Recents ──────────────────────────────────────────────────────
    // An empty query used to list whatever came first (the docs catalogue);
    // it shows what was opened last now, then the commands. Kept in the
    // settings blob (paletteRecents), newest first, eight at most.
    readonly property int _recentMax: 8
    function _key(e) {
        if (!e) return "";
        const id = e.commandId || e.taskId || e.personId || e.templateName || e.eventId
                // Which note and which heading in it: profile + line alone
                // made every note's title row the same key (KNOW-5).
                || (e.kind === "note" ? (e.profileId || "") + "#" + (e.noteId || "") + "#" + (e.line || 0) : "")
                || (e.kind === "docPage" ? (e.pageId || "") + "#" + (e.line || 0) : "")
                || (e.kind === "doc" ? (e.sectionId || "") + "#" + e.label : "")
                || (e.kind === "profile" ? e.profileId : "")
                || e.label;
        return e.kind + ":" + id;
    }
    function _settingsObj() {
        const raw = AppController.appSettingsJson || "";
        if (!raw.length) return ({});
        try { return JSON.parse(raw) || ({}); } catch (err) { return ({}); }
    }
    function _recentKeys() {
        const r = _settingsObj().paletteRecents;
        return Array.isArray(r) ? r.filter(function (k) { return typeof k === "string"; }) : [];
    }
    function _remember(entry) {
        const k = _key(entry);
        if (!k.length) return;
        const s = _settingsObj();
        const keys = _recentKeys().filter(function (x) { return x !== k; });
        keys.unshift(k);
        s.paletteRecents = keys.slice(0, _recentMax);
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function _recents() {
        const keys = _recentKeys();
        const byKey = {};
        for (let i = 0; i < _entries.length; i++) {
            const k = _key(_entries[i]);
            if (!byKey[k]) byKey[k] = _entries[i];
        }
        const out = [];
        for (let j = 0; j < keys.length; j++) {
            const e = byKey[keys[j]];
            if (e) out.push(Object.assign({}, e, { _recent: true }));
        }
        return out;
    }

    function _refresh() {
        _entries = AppController.commandPaletteEntries().concat(_commands());
        _matches = _filterAndScore("");
        _selectedIdx = 0;
    }

    // Word-order independent, typo tolerant (PaletteMatch.js). Returns -1 for
    // no match, otherwise >= 0.
    function _fuzzyScore(q, s) {
        return Match.score(q, s);
    }

    function _escapeHtml(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    // Build a StyledText snippet: ~30 chars of context on each side of the first
    // occurrence of `q` in `body`, with the match emphasised. "" if not present.
    function _snippetFor(body, q) {
        if (!body || q.length < 2) return "";
        const idx = Match.fold(body).indexOf(Match.fold(q));
        if (idx < 0) return "";
        const start = Math.max(0, idx - 30);
        const end = Math.min(body.length, idx + q.length + 40);
        const frag = body.substring(start, end).replace(/\s+/g, " ").trim();
        const mi = Match.fold(frag).indexOf(Match.fold(q));
        const lead = start > 0 ? "…" : "";
        const tail = end < body.length ? "…" : "";
        if (mi < 0) return lead + _escapeHtml(frag) + tail;
        return lead + _escapeHtml(frag.substring(0, mi))
             + "<b>" + _escapeHtml(frag.substring(mi, mi + q.length)) + "</b>"
             + _escapeHtml(frag.substring(mi + q.length)) + tail;
    }

    function _filterAndScore(q) {
        const trimmed = (q || "").trim();
        if (trimmed.length === 0) {
            // Recents, then every command — something to act on, never an
            // arbitrary slice of the docs.
            const rec = _recents();
            const seen = {};
            for (let r = 0; r < rec.length; r++) seen[_key(rec[r])] = true;
            const out0 = rec.slice();
            for (let i = 0; i < _entries.length; i++) {
                const e = _entries[i];
                if ((e.kind === "command" || e.kind === "setting") && !seen[_key(e)]) out0.push(e);
            }
            return out0.slice(0, 80).map(function (e) { const c = Object.assign({}, e); c._snippet = ""; return c; });
        }
        const out = [];
        const firstWord = trimmed.split(/\s+/)[0];
        const ql = trimmed.toLowerCase();
        for (let i = 0; i < _entries.length; i++) {
            const e = _entries[i];
            let score = _fuzzyScore(trimmed, e.label + " " + (e.sub || ""));
            // A command's other names: whole words only, no typo tolerance.
            if (e.keywords && Match.bodyHit(trimmed, e.keywords)) score = Math.max(score, 60);
            // The words themselves in the label beat a scattered subsequence:
            // "windows" should rank "Release › Windows" above "Release".
            if (ql.length >= 2 && score >= 0 && e.label.toLowerCase().indexOf(ql) >= 0) score += 20;
            // A person is found as an @-mention finds them: by a login made of
            // the name ("r.losev" for Роман Лосев), by the name in the other
            // script ("roman losev"). Exact login ~ 180, a word prefix 100.
            if (e.kind === "person" || e.kind === "contact") {
                const pr = AppController.personMatchRank(trimmed, e.label, e.personId || "");
                if (pr > 0) score = Math.max(score, pr * 2);
            }
            let snippet = "";
            // Full-text (HEAP-80): every word in the body, with a context
            // snippet. A body hit floors the entry at tier 5, below head matches.
            if (trimmed.length >= 2 && e.body && Match.bodyHit(trimmed, e.body)) {
                snippet = _snippetFor(e.body, trimmed.toLowerCase().indexOf(" ") < 0 ? trimmed : firstWord);
                score = Math.max(score, 5);
            }
            if (score < 0) continue;
            // tiny boost so an entry in the active profile floats up
            const profBonus = (e.profileId === AppController.activeProfileId) ? 1 : 0;
            out.push({ entry: e, score: score + profBonus, snippet: snippet, bodyOnly: score <= 5 && snippet.length > 0 });
        }
        out.sort(function (a, b) { return b.score - a.score; });
        return out.slice(0, 80).map(function (x) {
            const c = Object.assign({}, x.entry);
            c._snippet = x.snippet;
            c._bodyOnly = x.bodyOnly;
            return c;
        });
    }

    function _activate(entry) {
        if (!entry) return;
        _remember(entry);
        const switchProfile = (entry.profileId && entry.profileId !== AppController.activeProfileId);
        if (switchProfile) AppController.activeProfileId = entry.profileId;
        Qt.callLater(function () {
            if (entry.kind === "profile") {
                // already switched above
            } else if (entry.commandId === "goto-date") {
                // The calendar on that day; Today and the calendar views
                // just move to it.
                AppController.selectedDate = entry.date;
                if (["today", "week", "month"].indexOf(AppController.currentView) < 0)
                    AppController.currentView = "week";
            } else if (entry.kind === "command" || entry.kind === "setting") {
                // Closed first: a command often opens a popup of its own.
                root.close();
                root.commandRequested(entry.commandId);
                return;
            } else if (entry.kind === "task") {
                // The editor opens over any view that shows tasks; only a view
                // with no tasks on it (notes, docs, settings) gives way to the
                // board. It used to jump to the board from anywhere.
                if (root._taskViews.indexOf(AppController.currentView) < 0)
                    AppController.currentView = "board";
                root.openTask(entry.taskId);
            } else if (entry.kind === "doc") {
                AppController.currentView = "docs";
                root.navigateToDoc(entry.title || entry.label);
            } else if (entry.kind === "snippet") {
                AppController.currentView = "docs";
                root.navigateToSnippets(entry.label);
            } else if (entry.kind === "contact") {
                AppController.currentView = "docs";
                root.navigateToContacts(entry.label);
            } else if (entry.kind === "person") {
                root.openPerson(entry.personId);
            } else if (entry.kind === "note") {
                // Any note of the profile, not just the open one.
                if (entry.noteId) AppController.activeNoteId = entry.noteId;
                AppController.currentView = "notes";
                root.navigateToNoteLine(entry.line !== undefined ? entry.line : 0);
            } else if (entry.kind === "docPage") {
                AppController.activeDocPageId = entry.pageId;
                AppController.currentView = "docs";
                root.navigateToDocPage(entry.pageId);
            } else if (entry.kind === "dailyNote") {
                AppController.currentView = "notes";
                AppController.openDailyNote();
            } else if (entry.kind === "event") {
                // The week the event is in, selected on the day it falls on —
                // landing on the month would leave the reader to find it again.
                if (entry.eventDate) AppController.selectedDate = entry.eventDate;
                AppController.currentView = "week";
            } else if (entry.kind === "template") {
                if (root._taskViews.indexOf(AppController.currentView) < 0)
                    AppController.currentView = "board";
                AppController.createTaskFromTemplate(entry.templateName);
            }
            root.close();
        });
    }

    // Badge glyph and the localized kind name, for every kind the palette
    // lists; an unknown kind used to render "?".
    function _kindGlyph(kind) {
        switch (kind) {
            case "task":      return "T";
            case "doc":       return "D";
            case "snippet":   return "S";
            case "contact":   return "C";
            case "profile":   return "●";
            case "person":    return "P";
            case "note":      return "N";
            case "docPage":   return "¶";
            case "dailyNote": return "☼";
            case "event":     return "E";
            case "template":  return "✚";
            case "command":   return "›";
            case "setting":   return "⚙";
        }
        return "·";
    }
    function _kindLabel(kind) {
        const k = "palette.kind." + kind;
        const t = I18n.t(k);
        return t === k ? kind : t;
    }

    // "сегодня", "завтра", "вчера", else "9 окт" ("" for no date).
    function _dayWord(d) {
        if (!(d && d.getTime && !isNaN(d.getTime()))) return "";
        const a = new Date(d.getFullYear(), d.getMonth(), d.getDate());
        const n = new Date();
        const k = Math.round((a - new Date(n.getFullYear(), n.getMonth(), n.getDate())) / 86400000);
        if (k === 0) return I18n.t("common.today").toLowerCase();
        if (k === 1) return I18n.t("common.tomorrow").toLowerCase();
        if (k === -1) return I18n.t("common.yesterday").toLowerCase();
        return I18n.fmtDate(d, "dayMonth");
    }
    // A key as the sheets write it: Enter is ↵, one letter is lower case.
    function _k(keys) {
        const t = String(keys || "");
        if (t.length === 1) return t.toLowerCase();
        return t.replace(/\b(Enter|Return)\b/g, "↵");
    }
    // A chip's signal (bold only): blocked / P0 red, P1 amber.
    function chipTone(c) {
        if (!Style.urgency) return "transparent";
        const v = String(c.clause || "").toLowerCase();
        if (/status:blocked|priority:p0/.test(v)) return Theme.danger;
        if (/priority:p1/.test(v)) return Theme.warning;
        return "transparent";
    }

    // ── The command line ─────────────────────────────────────────────
    function openWith(text) {
        root._initial = text || "";
        if (!root.opened) { root.open(); return; }
        // Already open: the line takes the text as if it had opened with it.
        root._sync = true;
        root.chips = [];
        searchField.text = root._initial;
        root._initial = "";
        root._sync = false;
        if (!root.commitTokens(true)) root._rebuild();
        searchField.cursorPosition = searchField.text.length;
    }
    // Beside a query, a result is one whose name holds every word typed
    // (DG-081): the typo-tolerant match stays for "> commands" and for the
    // empty line, where it cannot bury the tasks found.
    function _nameHit(words, e) {
        const label = Match.fold(e.label || "");
        const ws = Match.fold(words).split(/\s+/).filter(w => w.length > 0);
        if (ws.length === 0) return false;
        if ((e.kind === "person" || e.kind === "contact")
            && AppController.personMatchRank(String(words), e.label, e.personId || "") > 0) return true;
        if (e.keywords && Match.bodyHit(String(words), e.keywords)) return true;
        // A command may carry other names for itself ("потом", "park" for
        // → Someday): those count as its name, not as a body hit (A1-15).
        const aka = String(e.aliases || "").toLowerCase();
        return ws.every(w => label.indexOf(w) >= 0 || aka.indexOf(w) >= 0);
    }
    property string _initial: ""

    // The chips' clauses, then the field: what the task search runs.
    function _fullText() {
        return root.chips.map(c => c.clause).concat([searchField.text]).join(" ").trim();
    }
    function _commandsOnly() {
        return root.chips.length === 0 && searchField.text.trim().indexOf(">") === 0;
    }
    // A finished condition ("заблок ", "p0 ") moves out of the field into a
    // chip; Tab does it for the word still being typed.
    function commitTokens(all) {
        if (root._commandsOnly()) return false;
        const raw = searchField.text;
        if (!all && !/\s$/.test(raw)) return false;
        const p = AppController.commandLine(raw, 0);
        if (!p.tokens || p.tokens.length === 0) return false;
        root._sync = true;
        root.chips = root.chips.concat(p.tokens);
        searchField.text = p.text.length > 0 ? p.text + (all ? "" : " ") : "";
        root._sync = false;
        root._rebuild();
        return true;
    }
    function removeChip(i) {
        const list = root.chips.slice();
        list.splice(i, 1);
        root.chips = list;
        root._rebuild();
    }
    // The words a chip shows: a key for the field and the value.
    function chipKey(c) {
        const keys = { status: "status", priority: "priority", tag: "label", due: "due", when: "when", ticket: "ticket" };
        if (keys[c.kind]) return I18n.t("query.key." + keys[c.kind]);
        const at = String(c.clause).indexOf(":");
        return at > 0 ? String(c.clause).slice(0, at) : I18n.t("query.key.other");
    }
    function chipValue(c) {
        if (c.kind === "clause") {
            const at = String(c.clause).indexOf(":");
            return at > 0 ? String(c.clause).slice(at + 1) : c.clause;
        }
        // Quiet writes values in lower case ("заблокировано"), P0 stays.
        return root._quiet && c.kind === "status" ? String(c.value).toLowerCase() : c.value;
    }

    // The task actions, for the task the line opened on or for what it found.
    // A column's name as the target of "→": the default "В работе" reads
    // "В работу" there (H2-Command, R3-106); a column the user named stays
    // as written.
    function _toStatus(name) {
        return String(name) === I18n.t("cmd.status.progName") ? I18n.t("cmd.status.progTo") : String(name);
    }
    function _taskAction(action, ids, labelKey, keyId, extra) {
        return Object.assign({ kind: "action", action: action, ids: ids, label: I18n.t(labelKey),
                               keys: keyId ? AppController.shortcutText(keyId) : "", sub: "" }, extra || {});
    }
    function _contextActions(filter) {
        const ids = root._context || [];
        if (ids.length === 0) return [];
        const t = AppController.taskById(ids[0]);
        if (!t || !t.id) return [];
        const done = AppController.statusCategory(t.status) === "done";
        const url = String((t.ticket && t.ticket.url) || t.externalUrl || "");
        const out = [
            root._taskAction("done", ids, done ? "taskmenu.reopen" : "taskmenu.done", "task.done"),
            root._taskAction("schedule", ids, "taskmenu.schedule", "task.schedule"),
            root._taskAction("due", ids, "taskmenu.due", "task.due"),
            root._taskAction("timer", ids, t.isTiming ? "taskcard.stopTimer" : "taskcard.startTimer", "task.timer"),
            root._taskAction("copyId", ids, "taskcard.copyId", "task.copyId"),
            root._taskAction("branch", ids, "taskcard.createBranch", "task.createBranch"),
            root._taskAction("archive", ids, t.archived ? "taskcard.unarchive" : "taskcard.archive", "board.archive")
        ];
        if (url.length > 0) out.splice(5, 0, root._taskAction("external", ids, "cmd.openInTracker", "task.openExternal"));
        if ((AppController.profiles || []).length > 1)
            out.push(root._taskAction("moveProfile", ids, "taskmenu.moveProfile", "task.moveProfile"));
        const q = String(filter || "").trim();
        return q.length === 0 ? out : out.filter(a => Match.score(q, a.label) >= 0);
    }
    // A task command asked for by its exact name with no task to act on:
    // shown, off, with the reason (APP-267 corner case).
    function _unavailable(q) {
        const query = String(q || "").trim().toLowerCase();
        if (query.length < 3 || (root._context || []).length > 0) return [];
        const out = [];
        const list = AppController.shortcuts;
        for (let i = 0; i < list.length; i++) {
            const c = list[i];
            if (c.id.indexOf("task.") !== 0 || c.id === "task.new" || /\.alt\d*$/.test(c.id)) continue;
            if (String(c.label).toLowerCase() !== query) continue;
            out.push({ kind: "action", action: "none", ids: [], label: c.label, keys: AppController.shortcutText(c.id),
                       sub: "", off: true, note: I18n.t("cmd.why.noTask") });
        }
        return out;
    }

    // A command's `sub` is its key; a setting's is a description. Both went
    // to the key column, where a setting's sentence pushed its title out to
    // nothing (PERSONA-19).
    function _cmdRow(e) {
        return Object.assign({}, e, { keys: e.kind === "command" ? (e.sub || "") : "",
                                      sub: e.kind === "command" ? "" : (e.sub || "") });
    }

    function _rebuild() {
        const raw = searchField.text;
        const rows = [];
        const header = function (label) { rows.push({ header: true, label: label }); };
        const free = raw.trim();
        if (root._commandsOnly()) {
            const q = free.slice(1).trim();
            root._parse = { tokens: [], text: q, tasks: [], total: 0, query: "" };
            const cmds = (q.length > 0 ? root._filterAndScore(q) : root._entries)
                .filter(e => e.kind === "command" || e.kind === "setting");
            const extra = root._unavailable(q);
            // ":" and a date goes to that date (HOTKEYS.md): ":fri",
            // ":20.10", ":завтра" found a setting or nothing (SHELL-1).
            const when = q.length > 0 ? AppController.parseDateTime(q, new Date()) : null;
            if (when && when.ok && when.start && !isNaN(when.start.getTime()))
                extra.unshift({ kind: "command", commandId: "goto-date", date: when.start, keys: "", sub: "",
                                label: I18n.t("palette.cmd.goToDate").arg(I18n.fmtDate(when.start, "weekdayDay")) });
            if (cmds.length + extra.length > 0) header(I18n.t("cmd.group.commands"));
            for (const e of extra) rows.push(e);
            for (const e of cmds) rows.push(root._cmdRow(e));
        } else if (free.length === 0 && root.chips.length === 0) {
            root._parse = { tokens: [], text: "", tasks: [], total: 0, query: "" };
            const ctx = root._contextActions("");
            if (ctx.length > 0) {
                header(I18n.t("cmd.group.selected").arg(root._context.length === 1 ? root._context[0] : root._context.length));
                for (const a of ctx) rows.push(a);
            }
            const rest = root._filterAndScore("");
            if (rest.length > 0) header(I18n.t("cmd.group.recent"));
            for (const e of rest) rows.push(root._cmdRow(e));
        } else {
            const p = AppController.commandLine(root._fullText(), 50);
            root._parse = p;
            const words = p.text;
            const scored = words.length > 0 ? root._filterAndScore(words) : [];
            const isCmd = e => e.kind === "command" || e.kind === "setting";
            let cmds = scored.filter(e => isCmd(e) && root._nameHit(words, e)).slice(0, 8);
            // A command named by the whole line, even when its words also read
            // as a filter: "end of day" is a command, not "due today" (A2-32).
            const lineText = root.chips.map(c => String(c.words || "")).concat([free]).join(" ").trim();
            if (lineText !== words && lineText.length >= 2) {
                const byLine = root._filterAndScore(lineText).filter(e => isCmd(e) && root._nameHit(lineText, e));
                const have = {};
                for (const e of cmds) have[e.commandId] = true;
                cmds = byLine.filter(e => !have[e.commandId]).concat(cmds).slice(0, 8);
            }
            // A command whose name starts with what was typed is what was
            // asked for: "save" opened a task that mentions saving, the
            // command "Save current view" third (PERSONA-24).
            const lw = Match.fold(words).trim();
            const lf = Match.fold(lineText);
            const leadCmds = cmds.filter(e => (lw.length >= 2 && Match.fold(e.label).indexOf(lw) === 0)
                                           || (lf.length >= 2 && Match.fold(e.label).indexOf(lf) === 0)
                                           || (e.keywords && Match.bodyHit(lineText, e.keywords)));
            if (leadCmds.length > 0) {
                header(I18n.t("cmd.group.commands"));
                for (const e of leadCmds) rows.push(root._cmdRow(e));
            }
            // The tasks found.
            const found = [];
            for (const t of p.tasks) {
                // The row ends with when it is due or planned (H2-Command).
                const full = AppController.taskById(t.id);
                const when = full && full.id ? root._dayWord(full.dueAt || full.scheduledAt) : "";
                found.push({ kind: "task", taskId: t.id, label: (t.key || t.id) + " · " + t.title,
                             sub: root._quiet ? "" : (t.archived ? I18n.t("cmd.archived") : when),
                             archived: t.archived, category: t.category, keys: "" });
            }
            if (found.length > 0) {
                header(root._quiet ? I18n.t(p.total === 1 ? "cmd.group.task" : "cmd.group.tasksQuiet")
                                   : I18n.t("cmd.group.tasks").arg(p.total));
                for (const e of found) rows.push(e);
                if (p.total > found.length)
                    rows.push({ kind: "more", label: I18n.t("cmd.more").arg(p.total - found.length),
                                keys: AppController.keyText("Ctrl+Return"), sub: "" });
                // What to do with them.
                const open = found.filter(e => e.category !== "done" && !e.archived).map(e => e.taskId);
                const acts = [];
                if (open.length > 0)
                    acts.push(root._taskAction("doneFound", open, "cmd.doneFound", "task.done",
                                               { sub: root._quiet ? "" : I18n.count(open.length, "cmd.n.tasks") }));
                const allBlocked = found.length > 0 && found.every(e => e.category === "blocked");
                const prog = AppController.statuses.find(st => AppController.statusCategory(st.id) === "prog");
                if (allBlocked && prog)
                    acts.push({ kind: "action", action: "unblock", ids: found.map(e => e.taskId), status: prog.id,
                                label: root._quiet ? I18n.t("cmd.unblockQuiet") : I18n.t("cmd.unblock").arg(root._toStatus(prog.name)), keys: "", sub: "" });
                if (acts.length > 0) {
                    header(root._quiet ? I18n.t(found.length === 1 ? "cmd.group.withIt" : "cmd.group.withThem")
                                       : I18n.t("cmd.group.withFound"));
                    for (const a of acts) rows.push(a);
                }
            }
            // What to do with this filter.
            const q = p.query;
            if (q.length > 0 && (root.chips.length > 0 || p.tokens.length > 0)) {
                header(I18n.t("cmd.group.withFilter"));
                // The parse already holds the chips' clauses: each once.
                const seen = {};
                const name = root.chips.concat(p.tokens).filter(c => !seen[c.clause] && (seen[c.clause] = true))
                    .map(c => root.chipValue(c)).join(" · ");
                rows.push({ kind: "action", action: "saveView", query: q,
                            label: root._quiet ? I18n.t("cmd.saveViewQuiet") : I18n.t("cmd.saveView").arg(name),
                            keys: AppController.keyText("Ctrl+S"), sub: root._quiet ? "" : I18n.t("cmd.saveView.sub") });
                // Bold opens it as a board (the list is Ctrl ↵ in the footer);
                // quiet, with no footer keys, offers the list.
                if (root._quiet)
                    rows.push({ kind: "action", action: "openList", query: q, label: I18n.t("cmd.openList"),
                                keys: AppController.keyText("Ctrl+Return"), sub: "" });
                else
                    rows.push({ kind: "action", action: "openBoard", query: q, label: I18n.t("cmd.openBoard"),
                                keys: AppController.shortcutText("view.board"), sub: "" });
            }
            // The task the line opened on, then the commands, then the rest.
            const ctx = root._contextActions(words);
            if (ctx.length > 0) {
                header(I18n.t("cmd.group.selected").arg(root._context.length === 1 ? root._context[0] : root._context.length));
                for (const a of ctx) rows.push(a);
            }
            const restCmds = cmds.filter(e => leadCmds.indexOf(e) < 0);
            const extra = root._unavailable(words);
            if (restCmds.length + extra.length > 0) header(I18n.t("cmd.group.commands"));
            for (const e of extra) rows.push(e);
            for (const e of restCmds) rows.push(root._cmdRow(e));
            const shownTasks = {};
            for (const e of found) shownTasks[e.taskId] = true;
            // Only what matches by name (DG-081): a word met somewhere in a
            // doc's body is not a result of the line.
            const other = scored.filter(e => e.kind !== "command" && e.kind !== "setting" && !e._bodyOnly && root._nameHit(words, e)
                                             && !(e.kind === "task" && (shownTasks[e.taskId]
                                                                        || e.profileId === AppController.activeProfileId)))
                .slice(0, 20);
            // Another profile's tasks under a header that says so, not under
            // "Notes, docs, people" (A1-16).
            const elsewhere = other.filter(e => e.kind === "task");
            // Meetings under their own header, not "Notes, docs, people"
            // (a-31).
            const meetings = other.filter(e => e.kind === "event");
            const rest = other.filter(e => e.kind !== "task" && e.kind !== "event");
            // Words met only inside notes: the full text is one Enter away in
            // the notes list (DG-081), not a junk task (A4-8). It sits in the
            // same group as a note found by name: one header, not two.
            const inBodies = scored.filter(e => e._bodyOnly && (e.kind === "note" || e.kind === "docPage")).length;
            const typed = raw.trim();
            const offerNotes = inBodies > 0 && words.length > 0 && found.length === 0;
            if (rest.length > 0 || offerNotes) header(I18n.t("cmd.group.other"));
            for (const e of rest) rows.push(Object.assign({}, e, { keys: "" }));
            if (offerNotes) {
                rows.push({ kind: "action", action: "searchNotes", text: words, keys: "", sub: "",
                            label: I18n.t("cmd.searchNotes").arg(words).arg(inBodies) });
                rows.push({ kind: "action", action: "create", text: typed, keys: "", sub: "",
                            label: I18n.t("cmd.createRow").arg(typed) });
            }
            if (meetings.length > 0) header(I18n.t("cmd.group.meetings"));
            for (const e of meetings) rows.push(Object.assign({}, e, { keys: "" }));
            if (elsewhere.length > 0) header(I18n.t("cmd.group.otherProfiles"));
            for (const e of elsewhere) rows.push(Object.assign({}, e, { keys: "" }));
            // Nothing found: the same keys in the other keyboard layout
            // ("gjxbybnm" is "починить") before offering a task (A4-20).
            if (rows.length === 0 && root.chips.length === 0) {
                const swapped = Match.swapLayout(typed);
                if (swapped.length > 0) {
                    const p2 = AppController.commandLine(swapped, 20);
                    const alt = [];
                    for (const t of p2.tasks)
                        alt.push({ kind: "task", taskId: t.id, label: (t.key || t.id) + " · " + t.title, sub: "",
                                   archived: t.archived, category: t.category, keys: "" });
                    const altWords = p2.text || "";
                    if (altWords.length > 0)
                        for (const e of root._filterAndScore(altWords))
                            if (!e._bodyOnly && root._nameHit(altWords, e) && !(e.kind === "task")) alt.push(Object.assign({}, e, { keys: "" }));
                    if (alt.length > 0) {
                        header(I18n.t("cmd.group.otherLayout").arg(swapped));
                        for (const e of alt.slice(0, 20)) rows.push(e);
                        rows.push({ kind: "action", action: "create", text: typed, keys: "", sub: "",
                                    label: I18n.t("cmd.createRow").arg(typed) });
                    }
                }
            }
            // Nothing at all: make it a task.
            if (rows.length === 0)
                rows.push({ kind: "create", label: I18n.t("cmd.create").arg(typed), text: typed,
                            keys: AppController.keyText("Return"), sub: "" });
        }
        const matches = [];
        for (const r of rows) {
            if (r.header) continue;
            r._idx = matches.length;
            matches.push(r);
        }
        root._rows = rows;
        root._matches = matches;
        // On an empty line the actions for the task under the cursor come
        // first, and an impatient Enter marked it Done (IDIOT-SHELL-8): the
        // selection starts below them, on a recent or a command.
        const ctxCount = free.length === 0 && root.chips.length === 0 && !root._commandsOnly()
                         ? matches.filter(m => m.kind === "action").length : 0;
        root._selectedIdx = ctxCount > 0 ? (ctxCount < matches.length ? ctxCount : -1) : 0;
    }

    function _move(step) {
        if (root._matches.length === 0) return;
        let i = root._selectedIdx;
        for (let n = 0; n < root._matches.length; n++) {
            i = Math.max(0, Math.min(root._matches.length - 1, i + step));
            if (!root._matches[i].off) break;
        }
        root._selectedIdx = i;
    }

    function _runAction(e) {
        const ids = e.ids || [];
        switch (e.action) {
        case "done": case "doneFound": AppController.toggleDone(ids); break;
        case "unblock": for (const id of ids) AppController.moveTaskTo(id, e.status, ""); break;
        case "schedule": for (const id of ids) AppController.scheduleTaskAtNextFreeSlot(id, AppController.selectedDate); break;
        case "due": if (ids.length > 0) root.openTask(ids[0]); break;
        case "timer":
            for (const id of ids) {
                const t = AppController.taskById(id);
                if (t && t.isTiming) AppController.stopTaskTimer(id); else AppController.startTaskTimer(id);
            }
            break;
        case "copyId": AppController.copyToClipboard(ids.join(", ")); break;
        case "external": if (ids.length > 0) AppController.openTaskExternal(ids[0]); break;
        case "branch": if (ids.length > 0) AppController.createBranchForTask(ids[0]); break;
        case "archive":
            for (const id of ids) {
                const t = AppController.taskById(id);
                AppController.setArchived(id, !(t && t.archived));
            }
            break;
        case "saveView": root.queryRequested(e.query, "save"); break;
        case "openBoard": root.queryRequested(e.query, "board"); break;
        case "openList": root.queryRequested(e.query, "list"); break;
        case "searchNotes":
            AppController.currentView = "notes";
            root.searchNotes(e.text);
            break;
        case "create": root.createRequested(e.text); break;
        case "moveProfile": root.moveToProfileRequested(ids); break;
        }
    }

    function activateSelected() {
        const e = root._matches[root._selectedIdx];
        if (!e || e.off) return;
        if (e.kind === "action") {
            root.close();
            root._runAction(e);
            return;
        }
        if (e.kind === "more") {
            root.close();
            root.queryRequested(root._parse.query, "list");
            return;
        }
        if (e.kind === "create") {
            root.close();
            root.createRequested(e.text);
            return;
        }
        root._activate(e);
    }
    // Ctrl+Enter: everything found, in Tasks as a list.
    function showAllAsList() {
        const q = root._parse.query || "";
        if (q.length === 0) return;
        root.close();
        root.queryRequested(q, "list");
    }

    onAboutToShow: {
        root._context = root.contextProvider ? root.contextProvider() : [];
        root._entries = AppController.commandPaletteEntries().concat(root._commands());
        root._sync = true;
        root.chips = [];
        searchField.text = root._initial;
        root._initial = "";
        root._sync = false;
        // A query handed in whole ("статус:заблок p0 оформ") shows as chips
        // the way typing it would (DG-080).
        if (!root.commitTokens(true)) root._rebuild();
        Qt.callLater(function () {
            searchField.forceActiveFocus();
            searchField.cursorPosition = searchField.text.length;
        });
    }

    // Ctrl+K again closes it (APP-267).
    Shortcut {
        sequences: [AppController.shortcuts.length >= 0 ? AppController.shortcutFor("palette.open") : "",
                    AppController.shortcutFor("palette.open.alt")]
        context: Qt.WindowShortcut
        enabled: root.opened
        onActivated: root.close()
        onActivatedAmbiguously: root.close()
    }

    background: Item {}

    contentItem: RowLayout {
        spacing: Theme.sp2xl

        Item {
            Layout.preferredWidth: root._lineW
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            implicitHeight: lineCol.implicitHeight
            Layout.preferredHeight: lineCol.implicitHeight
            ModalSurface { anchors.fill: parent }

        ColumnLayout {
            id: lineCol
            anchors.fill: parent
            spacing: 0

            // The line: › then the chips, then the field.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(Theme.px(52), inputFlow.implicitHeight + 2 * Theme.spXl)
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 1; color: Theme.border
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: root._quiet ? Theme.px(18) : Theme.sp2xl
                    anchors.rightMargin: Theme.sp2xl
                    spacing: Theme.spMd
                    Icon {
                        objectName: "cmd-prompt"
                        visible: !root._quiet || root._commandsOnly()
                        name: "chevron-right"
                        size: Theme.iconSize - 2
                        color: Theme.textMuted
                    }
                    Flow {
                        id: inputFlow
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Theme.spSm
                        Repeater {
                            id: chipRep
                            model: root.chips
                            delegate: PropertyChip {
                                id: chip
                                required property var modelData
                                required property int index
                                objectName: "cmd-chip-" + chip.index
                                small: !root._quiet
                                removable: true
                                // Plain chips; × only under the pointer (sheet H2/Q-Command, R4-078).
                                removeOnHover: true
                                key: root.chipKey(chip.modelData)
                                value: root.chipValue(chip.modelData)
                                tone: root.chipTone(chip.modelData)
                                onRemoved: root.removeChip(chip.index)
                            }
                        }
                        TextField {
                            id: searchField
                            objectName: "cmd-field"
                            ContextMenu.menu: TextEditMenu { editor: searchField }
                            // The rest of the chips' row, or a row of its own.
                            readonly property Item _lastChip: chipRep.count > 0 ? chipRep.itemAt(chipRep.count - 1) : null
                            readonly property real _after: _lastChip ? _lastChip.x + _lastChip.width + inputFlow.spacing : 0
                            width: inputFlow.width - _after >= Theme.px(200) ? inputFlow.width - _after : inputFlow.width
                            height: root._quiet ? Theme.chipH : Theme.chipHSmall
                            topPadding: 0; bottomPadding: 0
                            leftPadding: Theme.sp2xs
                            verticalAlignment: TextInput.AlignVCenter
                            placeholderText: root.chips.length > 0 ? "" : I18n.t("cmd.placeholder")
                            background: Item {}
                            color: Theme.text
                            placeholderTextColor: Theme.textDim
                            font.pixelSize: root._quiet ? Theme.typeStep(1) : Theme.typeStep(2)
                            selectByMouse: true
                            onTextChanged: {
                                if (root._sync) return;
                                if (!root.commitTokens(false)) root._rebuild();
                            }
                            Keys.onDownPressed: root._move(1)
                            Keys.onUpPressed: root._move(-1)
                            Keys.onTabPressed: (event) => {
                                root.commitTokens(true);
                                event.accepted = true;
                            }
                            Keys.onPressed: (event) => {
                                const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
                                if (enter && (event.modifiers & Qt.ControlModifier)) {
                                    root.showAllAsList();
                                    event.accepted = true;
                                } else if (enter) {
                                    root.activateSelected();
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier)) {
                                    const q = root._parse.query || "";
                                    if (q.length > 0) {
                                        root.close();
                                        root.queryRequested(q, "save");
                                    }
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_Backspace && searchField.text.length === 0
                                           && root.chips.length > 0) {
                                    root.removeChip(root.chips.length - 1);
                                    event.accepted = true;
                                }
                            }
                        }
                    }
                }
            }

            // Results, in groups.
            ListView {
                id: resultsView
                objectName: "cmd-results"
                Layout.fillWidth: true
                Layout.preferredHeight: root._matches.length === 0 ? Theme.px(120)
                                      : Math.min(resultsView.contentHeight + resultsView.topMargin + resultsView.bottomMargin,
                                                 root._listMax)
                clip: true
                model: root._rows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThinScrollBar {}
                topMargin: Theme.spSm
                bottomMargin: Theme.spSm
                Connections {
                    target: root
                    function on_SelectedIdxChanged() {
                        const e = root._matches[root._selectedIdx];
                        if (!e) return;
                        const i = root._rows.indexOf(e);
                        if (i >= 0) resultsView.positionViewAtIndex(i, ListView.Contain);
                    }
                }

                delegate: Item {
                    id: rowItem
                    required property var modelData
                    readonly property bool selected: !rowItem.modelData.header && rowItem.modelData._idx === root._selectedIdx
                    // Nothing found: two centred lines, not a row (R3-135).
                    readonly property bool isCreate: rowItem.modelData.kind === "create"
                    width: ListView.view.width
                    height: rowItem.modelData.header ? headText.implicitHeight + Theme.spLg + Theme.spXs
                          : rowItem.isCreate ? Theme.px(120) - 2 * Theme.spSm
                          : Math.max(Theme.px(36), rowLabel.implicitHeight + 2 * Theme.spMd)
                    Column {
                        objectName: "cmd-nothing"
                        visible: rowItem.isCreate
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.sp2xl
                        spacing: Theme.spXs
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: rowItem.isCreate ? I18n.t("cmd.nothingFor").arg(rowItem.modelData.text) : ""
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            color: Theme.textMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsMd
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: rowItem.isCreate ? I18n.t("cmd.createLine").arg(AppController.keyText("Return")).arg(rowItem.modelData.text) : ""
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            color: createCA.hovered ? Theme.textMuted : Theme.textDim
                            Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsMd
                            ClickArea {
                                id: createCA
                                label: parent.text
                                onActivated: {
                                    root._selectedIdx = rowItem.modelData._idx;
                                    root.activateSelected();
                                }
                            }
                        }
                    }

                    Text {
                        id: headText
                        visible: rowItem.modelData.header === true
                        anchors.left: parent.left; anchors.leftMargin: Theme.sp2xl
                        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.spXs
                        text: rowItem.modelData.header ? rowItem.modelData.label : ""
                        color: root._quiet ? Theme.textDim : Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    Rectangle {
                        visible: !rowItem.modelData.header && !rowItem.isCreate
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spSm; anchors.rightMargin: Theme.spSm
                        radius: Theme.radiusMd
                        color: rowItem.selected ? Theme.rowHighlight : "transparent"
                        // The selected row's marker (bold): a bright line on
                        // its left edge, 3:1 against the panel.
                        Rectangle {
                            objectName: "palette-row-marker"
                            visible: rowItem.selected && !root._quiet
                            anchors.left: parent.left
                            anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: 2
                            color: Theme.focusRing
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                            spacing: Theme.spMd
                            Text {
                                id: rowLabel
                                Layout.fillWidth: true
                                text: rowItem.modelData.label || ""
                                textFormat: Text.PlainText
                                color: rowItem.modelData.off || rowItem.modelData.archived ? Theme.textDim
                                     : rowItem.selected || !root._quiet ? Theme.text : Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsMd
                                font.weight: rowItem.modelData.kind === "task" && !root._quiet ? Theme.fwTitle : Theme.fwBody
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: text.length > 0
                                Layout.maximumWidth: Theme.px(260)
                                text: rowItem.modelData.note || (rowItem.modelData._snippet ? "" : (rowItem.modelData.sub || ""))
                                color: Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: !!rowItem.modelData.kind && rowItem.modelData.kind !== "task"
                                         && rowItem.modelData.kind !== "action" && rowItem.modelData.kind !== "command"
                                         && rowItem.modelData.kind !== "more" && rowItem.modelData.kind !== "create"
                                text: rowItem.modelData.kind ? root._kindLabel(rowItem.modelData.kind) : ""
                                color: Theme.textDim
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsXs
                            }
                            // The key column: right-aligned, as wide as "Ctrl S".
                            Text {
                                Layout.minimumWidth: root._quiet ? 0 : Theme.px(56)
                                horizontalAlignment: Text.AlignRight
                                text: root._k(rowItem.modelData.keys
                                              || (rowItem.modelData.kind === "task" && (rowItem.selected || !root._quiet)
                                                  ? AppController.keyText("Return") : ""))
                                visible: text.length > 0 || !root._quiet
                                color: root._quiet ? Theme.textDim : Theme.textMuted
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fsXs
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: if (!rowItem.modelData.off) root._selectedIdx = rowItem.modelData._idx
                            onClicked: {
                                root._selectedIdx = rowItem.modelData._idx;
                                root.activateSelected();
                            }
                        }
                    }
                }

                // Empty state: nothing typed yet, or nothing found.
                EmptyState {
                    objectName: "palette-empty"
                    visible: root._matches.length === 0
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.sp3xl, Theme.px(380))
                    compact: true
                    title: searchField.text.length === 0 ? I18n.t("palette.empty.start") : I18n.t("palette.empty.miss")
                    line: searchField.text.length === 0 ? "" : I18n.t("palette.empty.missHint")
                }
            }

            // The footer. Bold: the keys (↵ выполнить · Tab · Ctrl ↵);
            // quiet: the language in one line. Tab's offer replaces both.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: footText.implicitHeight + (root._quiet ? Theme.spMd + Theme.spLg : 2 * Theme.spMd)
                Rectangle {
                    visible: !root._quiet
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    height: 1; color: Theme.border
                }
                Text {
                    id: footText
                    objectName: "cmd-hints"
                    anchors.left: parent.left; anchors.leftMargin: root._quiet ? Theme.px(18) : Theme.sp2xl
                    anchors.right: parent.right; anchors.rightMargin: Theme.sp2xl
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: root._quiet ? -Theme.spXs : 0
                    readonly property var pending: root._parse.tokens
                        ? root._parse.tokens.filter(t => !root.chips.some(c => c.clause === t.clause)) : []
                    function k(key) {
                        return "<b><font face=\"" + Theme.fontMono + "\" color=\"" + Theme.text + "\">" + key + "</font></b>";
                    }
                    textFormat: pending.length > 0 ? Text.PlainText : Text.StyledText
                    text: pending.length > 0
                          ? I18n.t("cmd.tabHint").arg(pending.map(t => root.chipKey(t) + " " + root.chipValue(t)).join(" · "))
                          : root._quiet ? I18n.t("cmd.quietHint").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                          : footText.k("↵") + " " + I18n.t("cmd.hint.run") + "&nbsp;&nbsp;&nbsp;&nbsp;"
                            + footText.k("Tab") + " " + I18n.t("cmd.hint.tab") + "&nbsp;&nbsp;&nbsp;&nbsp;"
                            + footText.k(root._k(AppController.keyText("Ctrl+Return"))) + " " + I18n.t("cmd.hint.list")
                    color: root._quiet ? Theme.textDim : Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                    elide: Text.ElideRight
                }
            }
        }
        }

        // "Один язык везде": its own card beside the line (bold only).
        Item {
            objectName: "cmd-syntax"
            visible: root._cardFits
            Layout.preferredWidth: root._cardW
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: synCol.implicitHeight + 2 * Theme.sp2xl
            Rectangle {
                anchors.fill: parent
                color: Theme.panel2
                radius: Theme.modalRadius
                border.width: 1
                border.color: Theme.border
            }
            ColumnLayout {
                id: synCol
                anchors.fill: parent
                anchors.margins: Theme.sp2xl
                spacing: 0
                Text {
                    text: I18n.t("cmd.syntax.title")
                    color: Theme.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsMd
                    font.weight: Theme.fwHeading
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spXs
                    Layout.bottomMargin: Theme.spLg
                    text: I18n.t("cmd.syntax.intro")
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
                Repeater {
                    model: ["priority", "when", "due", "label", "status", "ticket", "commands"]
                    delegate: Item {
                        id: syn
                        required property string modelData
                        Layout.fillWidth: true
                        implicitHeight: synRow.implicitHeight + 2 * Theme.spSm
                        RowLayout {
                            id: synRow
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spLg
                            Text {
                                Layout.preferredWidth: Theme.px(120)
                                text: I18n.t("cmd.syntax." + syn.modelData + ".example")
                                color: Theme.text
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fsSm
                            }
                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("cmd.syntax." + syn.modelData)
                                wrapMode: Text.WordWrap
                                color: Theme.textMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSm
                            }
                        }
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.border
                            opacity: 0.6
                        }
                    }
                }
            }
        }
    }
}
