// CommandPalette: regression for the fuzzy-score proximity penalty pushing a
// genuine match below zero. _fuzzyScore documents "match -> >= 0, no match ->
// -1", but `score -= lastPos * 0.05` runs after the match check, so a single
// char first occurring at index >= 41 scored -0.05 and the caller's
// `if (score < 0) continue` dropped the entry even though the char is present.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "CommandPalette"
    when: windowShown
    visible: true
    width: 500
    height: 400

    Item { id: host; anchors.fill: parent }

    function make(qml) {
        const o = createTemporaryQmlObject(qml, host);
        verify(o !== null);
        return o;
    }

    function test_smoke_load() {
        const cp = make('import TodoCpp; CommandPalette { }');
        verify(cp !== null);
    }

    // A present char must never score below 0, regardless of how late it matches.
    function test_fuzzy_late_single_char_not_negative() {
        const cp = make('import TodoCpp; CommandPalette { }');
        const late = "a".repeat(41) + "z";           // only 'z' at index 41
        verify(cp._fuzzyScore("z", late) >= 0, "a present char must score >= 0");
        verify(cp._fuzzyScore("z", "a".repeat(40) + "z") >= 0);
        // A genuinely absent char must still return the negative no-match sentinel.
        verify(cp._fuzzyScore("z", "aaa") < 0, "absent char must score < 0");
    }

    // Dogfood 2026-10-30: Ctrl+K lists what Ctrl+Z can still take back,
    // newest first, with when it was done, and never as a recent.
    function test_undo_steps_are_commands() {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = "palette undo probe";
        verify(AppController.saveTask(d));
        const cp = make('import TodoCpp; CommandPalette { }');
        try {
            const undo = cp._commands().filter(c => String(c.commandId).indexOf("undo:") === 0);
            verify(undo.length >= 1);
            verify(undo.length <= cp.undoListed);
            const top = AppController.undoHistory(1)[0];
            compare(undo[0].commandId, "undo:" + top.serial);
            compare(undo[0].label, I18n.t("palette.cmd.undoEntry").arg(top.label));
            compare(undo[0].sub, I18n.fmtTime(new Date(top.at)));
            const before = AppController.appSettingsJson;
            cp._remember(undo[0]);
            compare(AppController.appSettingsJson, before, "an undo step is not a recent");
        } finally {
            AppController.deleteTask(d.id);
            AppController.clearPendingUndo();
        }
    }

    // End-to-end: an entry whose only match for the query is a late single char
    // must survive _filterAndScore, not be dropped as if it were a no-match.
    function test_filter_keeps_late_single_char_entry() {
        const cp = make('import TodoCpp; CommandPalette { }');
        cp._entries = [{ kind: "task", label: "a".repeat(41) + "z", sub: "" }];
        const out = cp._filterAndScore("z");
        compare(out.length, 1, "an entry containing the query char must be kept");
    }

    // Body-hit tier: a weak label match that _fuzzyScore clamps to 0 plus a body
    // hit (entry W) must not rank below a pure body-only hit (entry B). Both floor
    // to the body tier, so a stable sort keeps input order (W before B).
    // Pre-fix W stayed at 0 (the `if (score < 0)` rescue didn't fire) and sorted
    // below B at 5. (_filterAndScore strips scores, so we assert order, not score.)
    function test_body_hit_not_ranked_below_body_only() {
        const cp = make('import TodoCpp; CommandPalette { }');
        cp._entries = [
            { kind: "task", id: "W", label: "z" + "a".repeat(80) + "z", sub: "", body: "find zz now" },
            { kind: "task", id: "B", label: "plain label", sub: "", body: "also zz here" }
        ];
        const out = cp._filterAndScore("zz");
        compare(out.length, 2, "both body-containing entries must be kept");
        compare(out[0].id, "W", "weak-label+body must not rank below a pure body-only hit");
    }

    // ── Matching (audit TASKS-25) ────────────────────────────────────

    // Words in any order: "ingress kubernetes" finds "Kubernetes ingress".
    function test_word_order_does_not_matter() {
        const cp = make('import TodoCpp; CommandPalette { }');
        verify(cp._fuzzyScore("ingress kubernetes", "TASK-7 · Kubernetes ingress rules") >= 0);
        verify(cp._fuzzyScore("ingress kubernetes", "TASK-8 · Kubernetes egress") < 0,
               "every word has to land somewhere");
    }

    // One swapped or missing letter still finds it.
    function test_typos_are_tolerated() {
        const cp = make('import TodoCpp; CommandPalette { }');
        verify(cp._fuzzyScore("kubrenetes", "Kubernetes ingress") >= 0, "transposition");
        verify(cp._fuzzyScore("kubernets", "Kubernetes ingress") >= 0, "deletion");
        verify(cp._fuzzyScore("ingres", "Kubernetes ingress") >= 0, "prefix");
        verify(cp._fuzzyScore("xylophone", "Kubernetes ingress") < 0);
    }

    // A whole-word hit outranks a scattered one.
    function test_whole_words_rank_first() {
        const cp = make('import TodoCpp; CommandPalette { }');
        cp._entries = [
            { kind: "task", taskId: "A", label: "d-a-r-k scattered", sub: "" },
            { kind: "task", taskId: "B", label: "dark mode toggle", sub: "" }
        ];
        const out = cp._filterAndScore("dark");
        compare(out[0].taskId, "B");
    }

    // ── Commands (audit UX-20) ───────────────────────────────────────

    function _has(list, pred) {
        for (let i = 0; i < list.length; i++) if (pred(list[i])) return true;
        return false;
    }

    function test_commands_are_searchable() {
        const cp = make('import TodoCpp; CommandPalette { }');
        cp._refresh();
        const dark = cp._filterAndScore("dark");
        verify(_has(dark, function (e) { return e.kind === "command" && e.commandId === "theme.toggle"; }),
               "\"dark\" must find the theme toggle");
        const settings = cp._filterAndScore("settings appearance");
        verify(_has(settings, function (e) { return e.commandId === "settings:appearance"; }));
        const board = cp._filterAndScore("board");
        verify(_has(board, function (e) { return e.commandId === "view.board"; }));
        // Contextual keys are not offered.
        verify(!_has(cp._entries, function (e) { return e.commandId === "board.cursorDown"; }));
    }

    function test_command_activation_is_forwarded() {
        const cp = make('import TodoCpp; CommandPalette { }');
        let got = "";
        cp.commandRequested.connect(function (id) { got = id; });
        cp._activate({ kind: "command", commandId: "theme.toggle", label: "Toggle" });
        tryVerify(function () { return got === "theme.toggle"; });
    }

    // ── Recents ──────────────────────────────────────────────────────

    // KNOW-5: two notes' title rows (same profile, line 0) shared one recent
    // key, so opening "Beta" put "Alpha" in the recents.
    function test_recent_note_is_the_note_that_was_opened() {
        const saved = AppController.appSettingsJson;
        const cp = make('import TodoCpp; CommandPalette { }');
        cp._entries = [
            { kind: "note", noteId: "n-alpha", profileId: "p", line: 0, label: "Alpha note", sub: "" },
            { kind: "note", noteId: "n-beta", profileId: "p", line: 0, label: "Beta note", sub: "" },
            { kind: "docPage", pageId: "d-1", line: 0, label: "Same", sub: "" },
            { kind: "docPage", pageId: "d-2", line: 0, label: "Same", sub: "" }
        ];
        verify(cp._key(cp._entries[0]) !== cp._key(cp._entries[1]));
        verify(cp._key(cp._entries[2]) !== cp._key(cp._entries[3]));
        cp._remember(cp._entries[1]);
        const out = cp._filterAndScore("");
        AppController.appSettingsJson = saved;
        compare(out[0].noteId, "n-beta");
    }

    function test_empty_query_shows_recents_then_commands() {
        const saved = AppController.appSettingsJson;
        const cp = make('import TodoCpp; CommandPalette { }');
        cp._entries = [
            { kind: "doc", sectionId: "s1", label: "Some doc", sub: "" },
            { kind: "task", taskId: "RECENT-1", label: "RECENT-1 · a task", sub: "" },
            { kind: "command", commandId: "view.week", label: "Go to Week", sub: "Ctrl+3" }
        ];
        cp._remember(cp._entries[1]);
        const out = cp._filterAndScore("");
        AppController.appSettingsJson = saved;
        compare(out[0].taskId, "RECENT-1", "the recent entry comes first");
        verify(out[0]._recent);
        verify(!_has(out, function (e) { return e.kind === "doc"; }), "no arbitrary docs on an empty query");
        verify(_has(out, function (e) { return e.commandId === "view.week"; }));
    }

    // ── Badges and opening (audit TASKS-25) ─────────────────────────

    function test_every_kind_has_a_badge_and_a_name() {
        const cp = make('import TodoCpp; CommandPalette { }');
        const kinds = ["task", "doc", "snippet", "contact", "profile", "person", "note", "dailyNote",
                       "event", "template", "command", "setting"];
        for (const k of kinds) {
            verify(cp._kindGlyph(k) !== "?" && cp._kindGlyph(k) !== "·", k + " has no badge");
            verify(cp._kindLabel(k) !== "palette.kind." + k, k + " has no name");
        }
    }

    function test_opening_a_task_keeps_a_view_that_shows_tasks() {
        const savedView = AppController.currentView;
        const cp = make('import TodoCpp; CommandPalette { }');
        let opened = "";
        cp.openTask.connect(function (id) { opened = id; });
        AppController.currentView = "week";
        cp._activate({ kind: "task", taskId: "ANY-1", label: "ANY-1 · x" });
        tryVerify(function () { return opened === "ANY-1"; });
        compare(AppController.currentView, "week", "the palette forced the board");
        AppController.currentView = "notes";
        opened = "";
        cp._activate({ kind: "task", taskId: "ANY-1", label: "ANY-1 · x" });
        tryVerify(function () { return opened === "ANY-1"; });
        compare(AppController.currentView, "board", "notes shows no tasks; the board takes over");
        AppController.currentView = savedView;
    }

    // APP-192: every app-wide action in the catalog is a palette command, with
    // its key as the hint; the 0.6 tools are among them. (Main.runCommand
    // running each one is checked in test_palette_catalog.cpp.)
    function test_palette_covers_the_action_catalog() {
        const saved = AppController.appSettingsJson;
        let cmds = [];
        let catalog = [];
        try {
            AppController.setSafetySetting("immersion", true);
            AppController.setSafetySetting("standupDraft", true);
            const cp = make('import TodoCpp; CommandPalette { }');
            cp._refresh();
            cmds = cp._entries.filter(e => e.kind === "command");
            catalog = AppController.shortcuts.filter(c => !cp._isContextual(c.id));
        } finally {
            AppController.appSettingsJson = saved;
        }
        verify(catalog.length > 0);
        for (const c of catalog) {
            const hit = cmds.filter(e => e.commandId === c.id)[0];
            verify(hit !== undefined, c.id + " is not in the palette");
            compare(hit.sub, AppController.keyText(c.sequence || ""), c.id + " shows its key");
            verify(String(hit.label).length > 0 && hit.label !== c.id, c.id + " has a name");
        }
        for (const id of ["timeMachine.open", "standup.draft", "focus.immersion", "endOfDay.open", "log.open",
                          "recap.open", "welcome.replay"]) {
            verify(cmds.some(e => e.commandId === id), id + " missing from the palette");
        }
    }

    function _probeTask(title) {
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.title = title;
        verify(AppController.saveTask(d));
        return d.id;
    }

    // IDIOT-SHELL-8: Ctrl+K then an impatient Enter marked the card under the
    // cursor Done — the selection starts below the task's actions.
    function test_empty_line_does_not_start_on_a_task_action() {
        const id = _probeTask("palprobe context card");
        const cp = make('import TodoCpp; CommandPalette { }');
        cp.contextProvider = function () { return [id]; };
        cp.open();
        tryCompare(cp, "opened", true);
        verify(cp._matches.length > 0 && cp._matches[0].kind === "action", "no task actions on the empty line");
        const sel = cp._matches[cp._selectedIdx];
        verify(!sel || sel.kind !== "action", "the empty line starts on " + (sel && sel.label));
        cp.close();
        AppController.deleteTask(id);
        AppController.clearPendingUndo();
    }

    // PERSONA-24: a command whose name starts with the words beats a task
    // that only mentions them. PERSONA-19: a setting's description is not
    // its key (it pushed the title out).
    function test_command_named_by_the_words_comes_first() {
        const cp = make('import TodoCpp; CommandPalette { }');
        cp._refresh();
        const cmd = cp._entries.filter(e => e.kind === "command" && e.commandId === "theme.toggle")[0];
        verify(!!cmd);
        const word = String(cmd.label).split(/\s+/)[0];
        const id = _probeTask(word + " palprobe something");
        cp.openWith(word);
        tryCompare(cp, "opened", true);
        tryVerify(function () { return cp._matches.length > 0; });
        compare(cp._matches[0].kind === "command" || cp._matches[0].kind === "setting", true,
                "first row is " + cp._matches[0].kind + " " + cp._matches[0].label);
        cp.openWith(">" + I18n.t("settings.section.tasks.title"));
        tryVerify(function () { return cp._matches.some(m => m.kind === "setting"); });
        const st = cp._matches.filter(m => m.kind === "setting")[0];
        compare(st.keys, "", "a setting's description went to the key column");
        cp.close();
        AppController.deleteTask(id);
        AppController.clearPendingUndo();
    }

    // SHELL-1: ":" and a date goes to that date.
    function test_colon_and_a_date_goes_there() {
        const cp = make('import TodoCpp; CommandPalette { }');
        const was = AppController.selectedDate;
        const view = AppController.currentView;
        cp.openWith(">2031-03-12");
        tryCompare(cp, "opened", true);
        tryVerify(function () { return cp._matches.length > 0; });
        compare(cp._matches[0].commandId, "goto-date");
        cp.activateSelected();
        tryVerify(function () {
            const d = AppController.selectedDate;
            return d.getFullYear() === 2031 && d.getMonth() === 2 && d.getDate() === 12;
        }, 1000, "the date was not opened");
        AppController.selectedDate = was;
        AppController.currentView = view;
    }

    // F3-EYES-2/3: the Settings commands are the sections Settings has (no
    // "Profile" since DG-101), and About shows the version, not "%1".
    function test_settings_commands_match_the_page() {
        const cp = make('import TodoCpp; CommandPalette { }');
        cp._refresh();
        const st = cp._entries.filter(e => e.kind === "setting");
        verify(!st.some(e => e.commandId === "settings:profile"), "a removed section is offered");
        const about = st.filter(e => e.commandId === "settings:about")[0];
        verify(about !== undefined);
        verify(String(about.sub).indexOf("%1") < 0, "raw placeholder: " + about.sub);
        verify(String(about.sub).indexOf(AppController.appVersion) >= 0, about.sub);
    }

    // a-31: a meeting found by name sits under "Встречи", not "Заметки,
    // доки, люди", and its date has no locale dot ("15 окт 2026", DG-031).
    function test_a_found_meeting_has_its_own_group() {
        const was = AppController.language;
        const ev = AppController.newEventDraft(11.5, new Date(2026, 9, 15));
        ev.title = "Zebrafinch palette probe";
        ev.date = new Date(2026, 9, 15);
        AppController.saveEvent(ev);
        AppController.language = "ru";
        try {
            const cp = make('import TodoCpp; CommandPalette { }');
            cp.openWith("zebrafinch");
            tryVerify(function () { return cp._matches.some(m => m.kind === "event"); }, 2000, "the meeting is not found");
            const rows = cp._rows;
            const at = rows.findIndex(r => r.kind === "event");
            let head = "";
            for (let i = at; i >= 0; i--) if (rows[i].header) { head = rows[i].label; break; }
            compare(head, I18n.t("cmd.group.meetings"));
            const sub = String(rows[at].sub);
            verify(sub.indexOf("15 окт 2026") >= 0 && sub.indexOf("окт.") < 0, sub);
            cp.close();
        } finally {
            AppController.language = was;
            AppController.deleteEvent(ev.id);
        }
    }

    // F3-EYES-7: in the Russian UI a command is found by its English name too.
    function test_english_words_find_commands_in_russian() {
        const was = AppController.language;
        AppController.language = "ru";
        try {
            const cp = make('import TodoCpp; CommandPalette { }');
            cp.openWith("sett");
            tryVerify(function () { return cp._matches.some(m => m.kind === "setting"); }, 2000,
                      "'sett' found no settings in Russian");
            cp.close();
            cp.openWith("event log");
            tryVerify(function () { return cp._matches.some(m => m.commandId === "log.open"); }, 2000,
                      "a catalogue command is not found by its English name");
            cp.close();
        } finally {
            AppController.language = was;
        }
    }
}
