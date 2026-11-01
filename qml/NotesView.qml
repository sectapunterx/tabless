// Notes view — single per-profile markdown canvas with @mention / #ticket
// autocomplete. Mirrors the DocsView layout (header bar + scrollable body)
// and the docsState round-trip pattern.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import TodoCpp
import "Mention.js" as Mention
import "NotesWidth.js" as NotesWidth

Item {
    id: root

    // ── Mode: "rich" | "split" | "edit" | "live" ────────────────────
    // "rich" (the default, as Obsidian's live preview): one editor holding
    // the whole note, its markup hidden on every line the caret is not on, so
    // it reads as drawn and is still one text to select, copy and edit across
    // lines. "split": the source beside the drawn note (0.9.2). "edit": the
    // source alone. Ctrl+Shift+M steps rich -> split -> edit. "live", the
    // in-place block editor (APP-269), is only opened on request (tests).
    // Stored as settings.notes.view; the old settings.notes.mode is read only
    // for "source".
    property string viewMode: "rich"

    // The note's text without its title: past a "# " heading on the first
    // line and the blank lines under it. {start: 0, end} when there is none.
    function _bodyRange(t: string): var {
        const first = t.indexOf("\n");
        if (first < 0 || !/^#\s+\S/.test(t.substring(0, first))) return { start: 0, end: t.length };
        let at = first + 1;
        while (at < t.length && t.charAt(at) === "\n") at++;
        return { start: at, end: t.length };
    }

    function _readViewMode() {
        const raw = AppController.appSettingsJson || "";
        if (!raw.length) return "rich";
        try {
            const s = JSON.parse(raw);
            const n = (s && s.notes) || {};
            if (n.view === "rich" || n.view === "edit" || n.view === "split" || n.view === "live") return n.view;
            if (n.mode === "source") return "edit";
        } catch (e) {}
        return "rich";
    }
    function _writeViewMode(mode) {
        const raw = AppController.appSettingsJson || "";
        let s = {};
        if (raw.length) {
            try { s = JSON.parse(raw); } catch (e) { s = {}; }
        }
        s.notes = Object.assign({}, s.notes || {}, { view: mode });
        AppController.appSettingsJson = JSON.stringify(s);
    }
    onViewModeChanged: if (_loadedOnce) _writeViewMode(viewMode)

    // Set by the command palette when a search hit lands in this note; the
    // caret goes to that line and the property is handed back for clearing.
    property int jumpToLine: -1
    signal jumpConsumed()

    // From Ctrl+K (DG-070): a doc page to open in the document, a catalogue
    // entry (reference, snippet, contact) to find in the list. Handed back
    // for clearing, like jumpToLine.
    property string requestedPage: ""
    signal pageConsumed()
    onRequestedPageChanged: _consumePage()
    function _consumePage() {
        if (requestedPage.length === 0) return;
        root.openPage(requestedPage);
        Qt.callLater(root.pageConsumed);
    }
    // A filter left behind showed "No matches" beside an open note on the
    // way back (IDIOT-KNOW-7).
    onVisibleChanged: if (!visible && requestedFilter.length === 0) notesList.setFilter("")
    property string requestedFilter: ""
    signal filterConsumed()
    onRequestedFilterChanged: _consumeFilter()
    function _consumeFilter() {
        if (requestedFilter.length === 0) return;
        if (!root._listShown) root._listPref = "shown";
        notesList.setFilter(requestedFilter);
        Qt.callLater(root.filterConsumed);
    }
    onJumpToLineChanged: _consumeJump()
    function _consumeJump() {
        if (jumpToLine < 0) return;
        _jumpToOffset(mdDocument.positionForLine(jumpToLine));
        // Later, not from inside the change handler: the caller clears the
        // line it bound this to, which re-entered the binding while it was
        // still being evaluated and logged a binding loop.
        Qt.callLater(root.jumpConsumed);
    }

    // ── State for autocomplete popup ─────────────────────────────────
    property string acTrigger: ""        // "@" or "#" or ""
    property int    acTriggerPos: -1     // index of trigger char in editor.text
    property string acFilter: ""
    property var    acMatches: []
    property int    acSelected: 0
    // The field the list serves: the source editor, or the open block of
    // the drawn note — where nothing came up at all (KNOW-8).
    property var    _acField: editor

    // ── Links pane (HEAP-79) ─────────────────────────────────────────
    // Two lists: the notes that link here (what "backlinks" means — it used to
    // show this note's own links under that name, and flag a link to an
    // existing note as broken because no heading here carried its name), and
    // this note's own links, each resolved against every note.
    //
    // Both are computed after typing pauses, not on every keystroke: over a
    // 3 MB note the live binding cost seconds per character. Empty (and
    // uncomputed) while the pane is hidden.
    // Shown beside the note when there is room (sheet H2-Knowledge).
    property bool showBacklinks: root.width > Theme.px(1000)
    property var  _incoming: []
    // APP-269: the tasks the note names, the tasks that link here, the
    // note's links out of Tabless.
    property var  _taskRefs: []
    property var  _taskLinks: []
    property var  _external: []
    function _externalLinks(text) {
        const out = [], seen = {};
        // [label](url "title") reads "label · title" in the column.
        const rx = /\[([^\]\n]+)\]\((https?:[^)\s]+)(?:\s+"([^"\n]*)")?\)|(https?:\/\/[^\s)>\]]+)/g;
        let m;
        while ((m = rx.exec(text)) !== null && out.length < 50) {
            const url = m[2] || m[4];
            if (seen[url]) continue;
            seen[url] = true;
            out.push({ url: url, label: m[1] || url, title: m[3] || "" });
        }
        return out;
    }
    // Something to list beside the note (or under it, when narrow).
    readonly property bool _hasLinks: root._taskRefs.length + root._incoming.length + root._taskLinks.length
                                      + (Style.quiet ? 0 : root._external.length) > 0
    function _refreshLinks() {
        // Beside the note when there is room, under its text when there is
        // not (A4-12): computed whenever a note is on screen.
        if (!root._noteShown) {
            root._incoming = []; root._taskRefs = []; root._taskLinks = []; root._external = [];
            return;
        }
        root._incoming = AppController.backlinksToNote(AppController.activeNoteId);
        root._taskRefs = AppController.noteTaskRefs(editor.text);
        root._taskLinks = AppController.tasksLinkingToNote(AppController.activeNoteId);
        root._external = root._externalLinks(editor.text);
    }
    // How each [[target]] reads in the drawn note: a task chip, "no such".
    property var _wiki: ({})
    onShowBacklinksChanged: _refreshLinks()
    Timer { id: linksTimer; interval: 400; onTriggered: root._refreshLinks() }

    // The header's counts, on the same pause.
    property var _stats: ({ lines: 0, mentions: 0, tickets: 0 })
    Timer {
        id: statsTimer
        interval: 300
        onTriggered: {
            root._stats = AppController.noteStats(editor.text);
            root._wiki = AppController.wikiTargets(editor.text);
            root._refreshAttachments();
        }
    }

    // ── Attachments ──────────────────────────────────────────────────
    // A note's files are the ones its text links ("attachments/<id>"), so the
    // chips are read from the text and removing one takes the link out.
    property var _noteAttachments: []
    function _refreshAttachments() {
        root._noteAttachments = editor.text.indexOf("attachments/") >= 0
            ? AppController.markdownAttachments(editor.text) : [];
    }
    // Puts markdown links to stored files at the caret, each in a paragraph of
    // its own so an image renders as an image. One edit: one Ctrl+Z.
    function insertAttachmentRefs(added) {
        if (!added || added.length === 0) return 0;
        const refs = added.map(a => a.ref).join("\n\n");
        // Into the document on screen, not the hidden source field, whose
        // caret sat at 0: files landed above the title (KNOW-7).
        if (root.pageId.length > 0) {
            pageEditor.insertRefs(refs);
            return added.length;
        }
        if (root.viewMode === "live") {
            liveEditor.insertRefs(refs);
            liveEditor.flush();
            root._refreshAttachments();
            return added.length;
        }
        // Over the selection, as one edit: one Ctrl+Z takes the picture
        // back out (it was a remove and an insert, two).
        const from = editor.selectionStart;
        const to = editor.selectionEnd;
        const text = editor.text;
        let before = "";
        if (from > 0 && text.charAt(from - 1) !== "\n") before = "\n\n";
        else if (from > 1 && text.charAt(from - 2) !== "\n") before = "\n";
        const after = (to < text.length && text.charAt(to) !== "\n") ? "\n\n" : "\n";
        mdEditor.replaceRange(from, to, before + refs + after);
        root._refreshAttachments();
        return added.length;
    }
    function attachUrls(urls) {
        if (!urls || urls.length === 0) return 0;
        const list = [];
        for (let i = 0; i < urls.length; ++i) list.push(urls[i]);
        return root.insertAttachmentRefs(AppController.importAttachments(list, true));
    }
    // Ctrl+V with a file or a picture on the clipboard. A picture wins over
    // the text a browser or a messenger puts beside it (dogfood 2026-10-30);
    // the toast says Ctrl+Shift+V is the text instead.
    // The note's text right after such a paste, and what the picture went
    // over: while the text is still that, Ctrl+Shift+V swaps it for the text.
    property string _pastedPictureText: ""
    property var _pastedPictureOver: ({ from: 0, to: 0 })
    function pasteAttachment() {
        if (!AppController.clipboardHasAttachment()) return false;
        const withText = AppController.clipboardPlainText().trim().length > 0;
        const over = { from: editor.selectionStart, to: editor.selectionEnd };
        const added = root.insertAttachmentRefs(AppController.importClipboardAttachments());
        root._pastedPictureText = added > 0 && withText && root.pageId.length === 0 && root.viewMode !== "live" ? editor.text : "";
        root._pastedPictureOver = over;
        if (root._pastedPictureText.length > 0) AppController.showToast(I18n.t("notes.paste.image"));
        return true;
    }
    // Ctrl+Shift+V: the clipboard's text, in place of a picture just pasted.
    function pasteAsText() {
        if (root._pastedPictureText.length > 0 && root._pastedPictureText === editor.text) {
            editor.undo();
            editor.select(root._pastedPictureOver.from, root._pastedPictureOver.to);
        }
        root._pastedPictureText = "";
        editor.paste();
    }
    // Ctrl+V with a table on the clipboard - a page's <table>, a
    // spreadsheet's rows, a spec page's glued cells - pastes it as a pipe
    // table, a block of its own (dogfood 2026-10-19). The clipboard's own
    // text goes in first and the table replaces it as a second undo step:
    // one Ctrl+Z after a table you did not want leaves the plain paste
    // (review 2026-10-19). Ctrl+Shift+V is "paste as plain text" outright.
    function pasteTable() {
        if (!root._noteShown || root.viewMode === "live" || editor.readOnly) return false;
        const md = AppController.clipboardTableMarkdown();
        if (!md.length) return false;
        const plain = AppController.clipboardPlainText();
        if (plain.length) {
            const s = editor.selectionStart, e = editor.selectionEnd;
            const before = editor.length;
            mdEditor.setSelection(s, e);
            mdEditor.paste(plain);
            // What went in, as the document took it (line breaks and all).
            editor.select(s, s + editor.length - before + (e - s));
        }
        root.insertTableMarkdown(md);
        return true;
    }
    function insertTableMarkdown(md: string) {
        const s = editor.selectionStart, e = editor.selectionEnd;
        const t = editor.text;
        // A blank line above and below unless the text already has one: a
        // table right under a line of text reads as that paragraph's pipes.
        const lead = s === 0 ? "" : t.charAt(s - 1) !== "\n" ? "\n\n" : s > 1 && t.charAt(s - 2) !== "\n" ? "\n" : "";
        const tail = e >= t.length ? "\n" : t.charAt(e) !== "\n" ? "\n\n" : e + 1 < t.length && t.charAt(e + 1) !== "\n" ? "\n" : "";
        mdEditor.setSelection(s, e);
        mdEditor.paste(lead + md + tail);
        editor.cursorPosition = s + lead.length + md.length;
    }
    // A table's rows and columns at the caret (TableOps): from the context
    // menu, the "/" menu, the palette and the notes.table.* keys.
    function tableOp(op: string): bool {
        if (!root._noteShown || root.viewMode === "live" || editor.readOnly) return false;
        mdEditor.setSelection(editor.selectionStart, editor.selectionEnd);
        const ok = mdEditor.tableOp(op);
        if (ok) editor.forceActiveFocus();
        return ok;
    }
    function caretInTable(): bool {
        return root._noteShown && root.viewMode !== "live" && mdEditor.tableAnchor(editor.cursorPosition) === editor.cursorPosition
               && editor.text.length > 0;
    }
    // The caret alone in an empty cell of a table: "/" there is the menu.
    function _caretInEmptyCell(): bool {
        if (editor.selectionStart !== editor.selectionEnd) return false;
        const t = editor.text, at = editor.cursorPosition;
        const ls = t.lastIndexOf("\n", at - 1) + 1;
        let le = t.indexOf("\n", at);
        if (le < 0) le = t.length;
        const line = t.substring(ls, le), off = at - ls;
        if (off <= 0 || !/^\s*\|.*\|\s*$/.test(line)) return false;
        const l = line.lastIndexOf("|", off - 1), r = line.indexOf("|", off);
        return l >= 0 && r >= off && line.substring(l + 1, r).trim().length === 0 && mdEditor.tableAnchor(at) >= 0;
    }

    // ── The text column and the code column (dogfood 2026-10-19) ─────
    // settings.notes.textWidth / codeWidth, unscaled; the right edge of each
    // is drawn as a guide and can be dragged (double-click: the default).
    // Settings → Notes has them as numbers too.
    readonly property int textWidthPref: NotesWidth.read(AppController.appSettingsJson, "textWidth", NotesWidth.TEXT_DEFAULT)
    readonly property int codeWidthPref: NotesWidth.read(AppController.appSettingsJson, "codeWidth", NotesWidth.CODE_DEFAULT)
    // The guide lines at those edges (dogfood 2026-10-20): the text's on
    // unless turned off, the code's and tables' off unless turned on.
    readonly property bool textEdgeOn: NotesWidth.readFlag(AppController.appSettingsJson, "textEdge", NotesWidth.TEXT_EDGE_DEFAULT)
    readonly property bool codeEdgeOn: NotesWidth.readFlag(AppController.appSettingsJson, "codeEdge", NotesWidth.CODE_EDGE_DEFAULT)
    // While an edge is dragged: its width so far, -1 when none is.
    property int _dragText: -1
    property int _dragCode: -1
    readonly property var _cols: NotesWidth.columns(notesScroll.width - 48,
                                                    Theme.px(root._dragText >= 0 ? root._dragText : root.textWidthPref),
                                                    Theme.px(root._dragCode >= 0 ? root._dragCode : root.codeWidthPref))
    function writeNotesPref(key: string, value: var) {
        const raw = AppController.appSettingsJson || "";
        let s = {};
        if (raw.length) {
            try { s = JSON.parse(raw); } catch (e) { s = {}; }
        }
        const n = Object.assign({}, s.notes || {});
        if (value === undefined) delete n[key];
        else n[key] = value;
        s.notes = n;
        AppController.appSettingsJson = JSON.stringify(s);
    }
    // Whether the character at `i` is backslash-escaped: an odd run of
    // backslashes before it.
    function _isEscapedAt(text, i) {
        let n = 0;
        while (i - n - 1 >= 0 && text.charAt(i - n - 1) === "\\") n++;
        return n % 2 === 1;
    }
    // Takes every link to the file out of the note. The file stays in the
    // attachments folder (Settings → Data cleans up what nothing uses).
    function removeAttachmentRefs(attachmentId) {
        const needle = "attachments/" + attachmentId;
        const text = editor.text;
        const spans = [];
        let at = text.indexOf(needle);
        while (at >= 0) {
            const lineStart = text.lastIndexOf("\n", at - 1) + 1;
            const open = text.lastIndexOf("](", at);
            // The label's own "[" — not one escaped inside it: a file called
            // a[1].png is labelled "a\[1\].png", and stopping at "\[" left
            // "![a\" behind in the note.
            let first = open >= lineStart ? text.lastIndexOf("[", open) : -1;
            while (first > lineStart && root._isEscapedAt(text, first))
                first = text.lastIndexOf("[", first - 1);
            const close = text.indexOf(")", at);
            if (first < lineStart || close < 0) { at = text.indexOf(needle, at + needle.length); continue; }
            if (first > lineStart && text.charAt(first - 1) === "!") first--;
            let last = close + 1;
            if (text.charAt(last) === "\n") last++;
            spans.push([first, last]);
            at = text.indexOf(needle, last);
        }
        for (let i = spans.length - 1; i >= 0; --i) editor.remove(spans[i][0], spans[i][1]);
        root._refreshAttachments();
        return spans.length;
    }
    // The caret alone on an empty line of the note.
    function _caretOnEmptyLine() {
        if (editor.selectionStart !== editor.selectionEnd) return false;
        const at = editor.cursorPosition;
        const t = editor.text;
        return (at === 0 || t.charAt(at - 1) === "\n") && (at === t.length || t.charAt(at) === "\n");
    }
    // What "/" inserts in the rich editor (a-03); a file is picked and
    // linked where the "/" was.
    MdSlashMenu {
        id: noteSlash
        objectName: "notes-slash-menu"
        field: editor
        onFileRequested: {
            noteSlash.dropSlash();
            attachDialog.open();
        }
        onTableRequested: (op, at) => {
            editor.cursorPosition = Math.max(0, Math.min(at, editor.length));
            root.tableOp(op);
        }
    }
    FileDialog {
        id: attachDialog
        fileMode: FileDialog.OpenFiles
        title: I18n.t("att.dialog.title")
        onAccepted: root.attachUrls(selectedFiles)
    }
    Shortcut {
        sequence: "Ctrl+Shift+A"
        enabled: editor.activeFocus
        onActivated: attachDialog.open()
    }

    // ── Notes → Импорт (dogfood 2026-10-23 #9) ───────────────────────────
    // A table from a file or the clipboard into a new note or under the
    // cursor; a .md / .txt as a note as it is.
    readonly property bool _canInsertTable: root._noteShown && root.viewMode !== "live" && !editor.readOnly
                                            && AppController.activeNoteId.length > 0
    function importTable(what: string) {
        if (what === "clipboard") tableImport.openFor("clipboard", root._canInsertTable);
        else if (what === "text") importTextDialog.open();
        else importTableDialog.open();
    }
    function _putImported(md: string, intoCurrent: bool, title: string) {
        if (intoCurrent) {
            root.insertTableMarkdown(md);
            editor.forceActiveFocus();
            return;
        }
        root._flushPending();
        const id = AppController.newNote(title);
        AppController.setNoteBody(id, md);
        root.pageId = "";
        root.shownDoc = null;
        AppController.activeNoteId = id;
    }
    FileDialog {
        id: importTableDialog
        objectName: "notes-import-table-file"
        fileMode: FileDialog.OpenFile
        title: I18n.t("tableImport.title")
        nameFilters: ["Tables (*.xlsx *.csv *.tsv *.txt *.log)", "All files (*)"]
        onAccepted: tableImport.openFor(selectedFile, root._canInsertTable)
    }
    FileDialog {
        id: importTextDialog
        objectName: "notes-import-text-file"
        fileMode: FileDialog.OpenFile
        title: I18n.t("notes.import.asText")
        nameFilters: ["Markdown / text (*.md *.markdown *.txt *.log)", "All files (*)"]
        onAccepted: {
            const md = AppController.tableImportMarkdown(selectedFile, ({ asText: true }));
            const name = String(selectedFile).replace(/^.*[\/]/, "").replace(/\.[^.]*$/, "");
            root._putImported(md, false, decodeURIComponent(name));
        }
    }
    TableImportDialog {
        id: tableImport
        onImported: (md, intoCurrent, title) => root._putImported(md, intoCurrent, title)
    }
    // Files dropped anywhere on Notes are linked into the open note.
    DropArea {
        id: noteFileDrop
        objectName: "notes-file-drop"
        anchors.fill: parent
        z: 50
        keys: ["text/uri-list"]
        onEntered: (drag) => { drag.accepted = drag.hasUrls; }
        onDropped: (drop) => {
            if (drop.hasUrls && root.attachUrls(drop.urls) > 0) drop.accept(Qt.CopyAction);
        }
    }
    Rectangle {
        visible: noteFileDrop.containsDrag
        anchors.fill: parent
        z: 49
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
    }

    // What the header calls the open note. m.data() is a call, not something
    // a binding can watch, so it is refreshed when the note or the list moves.
    property string _activeTitle: ""
    property var _activeUpdated: null
    function _refreshTitle() {
        const m = AppController.notes;
        const row = m.indexOfId(AppController.activeNoteId);
        root._activeTitle = row >= 0 ? String(m.data(m.index(row, 0), m.roleOf("title")) || "") : "";
        root._activeUpdated = row >= 0 ? m.data(m.index(row, 0), m.roleOf("updated")) : null;
    }
    Connections {
        target: AppController.notes
        function onDataChanged() { root._refreshTitle() }
        function onModelReset() { root._refreshTitle() }
        function onRowsRemoved() { root._refreshTitle() }
    }
    Connections {
        target: AppController
        function onActiveNoteChanged() {
            if (!root.docs) root.pageId = "";
            root._refreshTitle();
            findBar.dismiss();
        }
    }

    // Ctrl+F in the note's text: find in this note, over its top right.
    NoteFindBar {
        id: findBar
        parent: notesScroll
        anchors.top: parent ? parent.top : undefined
        anchors.right: parent ? parent.right : undefined
        anchors.topMargin: Theme.spMd
        anchors.rightMargin: Theme.spXl
        editor: editor
        flickable: notesScroll
    }
    readonly property bool _findOn: root._noteShown && root.viewMode !== "live"
    on_FindOnChanged: if (!root._findOn) findBar.dismiss()

    // "notes" or "docs" (Main binds it to the view): the same screen shows
    // the notes, or the pages, snippets, links and contacts. Each keeps what
    // it had open: back in Docs, the page left there is open again.
    property string mode: "notes"
    readonly property bool docs: root.mode === "docs"
    property string _docsPage: ""
    onModeChanged: {
        root._flushPending();
        root.shownDoc = null;
        // mode, not docs: the binding may not have caught up yet.
        if (root.mode === "docs") {
            root.pageId = root._docsPage;
        } else {
            root._docsPage = root.pageId;
            root.pageId = "";
        }
    }
    // The note on screen: in Notes, while no page is open.
    readonly property bool _noteShown: !root.docs && root.pageId.length === 0

    // ── A page of the Docs catalogue, open in the document ───────────
    // Empty: the active note is shown (Notes) or the empty line (Docs).
    // Set from the list or from Ctrl+K.
    property string pageId: ""
    // The keyboard goes with the page: it stayed on the hidden note, and
    // Return then typing wrote into that note (IDIOT-KNOW-2).
    function openPage(id) {
        root._flushPending();
        root.shownDoc = null;
        root.pageId = id;
        Qt.callLater(pageEditor.focusEditor);
    }
    // ── A snippet, shown in the document (APP-290) ───────────────────
    // Where it sits in the Docs data; what is shown is read from that live,
    // so an edit shows at once and a delete clears the pane.
    property var shownDoc: null
    readonly property string shownKey: root.shownDoc ? "snippet:" + root.shownDoc.idx : ""
    readonly property var shownDocData: {
        const at = root.shownDoc;
        if (!at || !root.docs) return null;
        let d = ({});
        try { d = JSON.parse(AppController.docsState || "{}") || ({}); } catch (e) { return null; }
        const sn = (d.snippets || [])[at.idx];
        if (!sn) return null;
        return { kind: "snippet", idx: at.idx, title: String(sn.title || ""), lang: String(sn.lang || ""),
                 code: String(sn.code || "") };
    }
    function showDoc(r) {
        root._flushPending();
        root.pageId = "";
        root.shownDoc = { kind: "snippet", idx: r.idx };
    }
    property var _pageUpdated: null
    function _refreshPage() {
        const m = AppController.docPages;
        const row = root.pageId.length > 0 ? m.indexOfId(root.pageId) : -1;
        root._pageUpdated = row >= 0 ? m.data(m.index(row, 0), m.roleOf("updated")) : null;
        if (root.pageId.length > 0 && row < 0) root.pageId = "";
    }
    onPageIdChanged: { _refreshPage(); linksTimer.restart(); }
    Connections {
        target: AppController.docPages
        function onDataChanged() { root._refreshPage() }
        function onModelReset() { root._refreshPage() }
        function onRowsRemoved() { root._refreshPage() }
    }

    // ── The line above the title (DG-071) ────────────────────────────
    // "Заметка · изменена 5 мин назад" (bold), "изменена 5 мин назад"
    // (quiet). Pure: the clock is passed in, so a test can pin it.
    function agoText(when, now) {
        if (!when) return "";
        const t = new Date(when).getTime();
        if (isNaN(t)) return "";
        const min = Math.floor((now.getTime() - t) / 60000);
        if (min < 1) return I18n.t("know.ago.now");
        if (min < 60) return I18n.t("know.ago.min").arg(min);
        const hours = Math.floor(min / 60);
        if (hours < 24) return I18n.t("know.ago.hours").arg(hours);
        const days = Math.floor(hours / 24);
        if (days === 1) return I18n.t("know.ago.yesterday");
        return I18n.t("know.ago.date").arg(I18n.fmtDate(new Date(t)));
    }
    property date _now: new Date()
    Timer { interval: 30000; running: root.visible; repeat: true; onTriggered: root._now = new Date() }
    readonly property string _metaLine: {
        const page = root.pageId.length > 0;
        if (root.docs && !page) return "";
        // No note at all: no lonely "Note" over an empty pane (A4-25).
        if (!page && AppController.activeNoteId.length === 0) return "";
        const ago = root.agoText(page ? root._pageUpdated : root._activeUpdated, root._now);
        const edited = ago.length > 0 ? I18n.t("know.edited").arg(ago) : "";
        // The keyboard on the drawn document, not in its text: the line
        // says how to write (A4-4).
        const reading = page ? pageEditor.liveEditor.documentFocused
                             : root.viewMode === "live" && liveEditor.documentFocused;
        const hint = reading ? I18n.t("know.hint.write") : "";
        let out = edited;
        if (!Style.quiet) {
            const kind = I18n.t(page ? "know.kind.page" : "know.kind.note");
            out = edited.length > 0 ? kind + " · " + edited : kind;
        }
        return hint.length === 0 ? out : out.length > 0 ? out + " · " + hint : hint;
    }
    // Where the document starts: the sheet's 44 px from the list, of which
    // the drawn note's own side margin is 24.
    readonly property int _mdSide: 24
    readonly property int _docInset: Style.quiet ? Theme.px(4) : Theme.px(20)

    // The list of notes beside the editor. "auto" shows it when there is room
    // for both; the toggle (and Ctrl+Alt+L) pins it open or shut. At 1440 px
    // with the right panel open it used to be hidden with no way back, which
    // left no way to reach another note at all.
    property string _listPref: "auto"
    readonly property bool _listShown: _listPref === "auto" ? root.width > 560 : _listPref === "shown"
    function toggleList() { root._listPref = root._listShown ? "hidden" : "shown"; }

    // Same lookup Main uses, so a rebinding in Settings → Hotkeys applies here.
    function _kbd(id) {
        const list = AppController.shortcuts;
        for (let i = 0; i < list.length; i++)
            if (list[i].id === id) return list[i].sequence;
        return "";
    }

    // The notes.* catalog actions, for the command palette: the same thing the
    // keys bound below do (SHELL-1). `cmd` is the id without "notes.".
    function runNotesCommand(cmd) {
        // notes.table.rowAbove …: the table under the caret.
        if (cmd.indexOf("table.") === 0) {
            root.tableOp(cmd.slice(6));
            return;
        }
        switch (cmd) {
        case "new":        root.newNoteAndEdit(); break;
        case "next":       notesList.step(1); break;
        case "prev":       notesList.step(-1); break;
        case "rename":     root.renameShown(); break;
        case "toggleList": root.toggleList(); break;
        // From the palette there is no Ctrl to hold: straight to the note.
        case "recent":     root._openRecent(1); break;
        case "recentBack": root._openRecent(-1); break;
        case "close":      root.closeNote(); break;
        default:           console.warn("notes: no command", cmd);
        }
    }

    // The keys that take focus out of the editor (SHELL-18). True when the
    // key was one of them and focus has moved.
    function _leaveEditorKey(field: Item, key: int, modifiers: int): bool {
        const mods = modifiers & ~Qt.KeypadModifier;
        if (key === Qt.Key_Escape && mods === Qt.NoModifier) {
            if (root._listShown) {
                notesList.focusList();
            } else {
                const prev = field.nextItemInFocusChain(false);
                if (prev) prev.forceActiveFocus(Qt.BacktabFocusReason);
            }
            return true;
        }
        const ctrlTab = (key === Qt.Key_Tab || key === Qt.Key_Backtab) && (mods & Qt.ControlModifier) !== 0;
        const f6 = key === Qt.Key_F6 && (mods & ~Qt.ShiftModifier) === Qt.NoModifier;
        if (!ctrlTab && !f6) return false;
        AppController.noteFeatureUsed("notesRegions");
        const back = key === Qt.Key_Backtab || (mods & Qt.ShiftModifier) !== 0;
        // Ctrl+Tab is the recent notes (dogfood 2026-10-20), here too when
        // the key reaches the text before the Shortcut; F6 still moves on.
        if (ctrlTab && !root.docs && root._kbd("notes.recent") === "Ctrl+Tab") {
            root.switchRecent(back ? -1 : 1);
            return true;
        }
        const next = field.nextItemInFocusChain(!back);
        if (next) next.forceActiveFocus(back ? Qt.BacktabFocusReason : Qt.TabFocusReason);
        return true;
    }

    // ── Recent notes (dogfood 2026-10-20) ───────────────────────────
    // Ctrl+Tab / Ctrl+Shift+Tab: the switcher, held like a browser's.
    function switchRecent(direction: int) {
        if (root.docs) return;
        const seq = root._kbd(direction < 0 ? "notes.recentBack" : "notes.recent");
        let mods = 0;
        if (/(^|\+)Ctrl\+/.test(seq)) mods |= Qt.ControlModifier;
        if (/(^|\+)Alt\+/.test(seq)) mods |= Qt.AltModifier;
        if (/(^|\+)Meta\+/.test(seq)) mods |= Qt.MetaModifier;
        noteSwitcher.start(direction, mods);
    }
    // The next or previous note in the list's order (Ctrl+PgDn / PgUp, Alt
    // ↓ / ↑). From the text the keyboard stays in the text: the draft is
    // saved on the way out (onAboutToChangeActiveNote) and the caret goes
    // back to where it was in the note last time.
    property var _caretByNote: ({})
    function stepNote(delta: int) {
        const inText = editor.activeFocus || liveEditor.editor.activeFocus;
        notesList.step(delta);
        if (!inText || root.pageId.length > 0) return;
        if (root.viewMode === "live") {
            liveEditor.focusEnd();
            return;
        }
        const at = root._caretByNote[AppController.activeNoteId];
        editor.forceActiveFocus();
        editor.cursorPosition = at === undefined ? editor.length : Math.min(at, editor.length);
    }
    // "Alt ↑" / "Alt ↓" beside the neighbouring notes while Alt alone is held
    // (a moment, so Alt+Tab does not flash them); AltGr is Ctrl+Alt and types.
    property bool _altHeld: false
    readonly property bool _windowActive: Window.active
    on_WindowActiveChanged: if (!root._windowActive) root._altHeld = false
    Timer {
        id: altHintDelay
        interval: 250
        onTriggered: root._altHeld = true
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Alt && (event.modifiers & ~(Qt.AltModifier | Qt.KeypadModifier)) === 0) {
            if (!event.isAutoRepeat && !root._altHeld) altHintDelay.restart();
        } else if (!(event.modifiers & Qt.AltModifier)) {
            altHintDelay.stop();
            root._altHeld = false;
        }
    }
    Keys.onReleased: (event) => {
        if (event.key !== Qt.Key_Alt) return;
        altHintDelay.stop();
        root._altHeld = false;
    }
    function _rememberCaret() {
        if (root._loadedNoteId.length > 0 && root.viewMode !== "live") root._caretByNote[root._loadedNoteId] = editor.cursorPosition;
    }
    function _openRecent(direction: int) {
        const ids = AppController.recentNoteIds();
        if (ids.length < 2) return;
        root._openNoteFromKeys(direction < 0 ? ids[ids.length - 1] : ids[1]);
    }
    function _openNoteFromKeys(id: string) {
        root._flushPending();
        AppController.activeNoteId = id;
        root._focusEditorAtEnd();
    }
    // Ctrl+W: the note closes as a tab does — back to the one opened before.
    // An empty new note goes with it; a note with text stays in the list.
    function closeNote() {
        if (root.docs) return;
        const cur = AppController.activeNoteId;
        const ids = AppController.recentNoteIds();
        let prev = "";
        for (let i = 0; i < ids.length; i++) {
            if (ids[i] !== cur) { prev = ids[i]; break; }
        }
        if (prev.length === 0 && cur.length === 0) return;
        root._openNoteFromKeys(prev);
    }

    // F2: the document on screen, a page or the note (IDIOT-KNOW-4). A page
    // can be open on either screen.
    function renameShown() {
        if (root.pageId.length > 0) notesList.renamePage(root.pageId);
        else if (AppController.activeNoteId.length > 0) notesList.renameActive();
    }

    // Ctrl+N: a note in Notes, a page in Docs.
    function newNoteAndEdit() {
        root._flushPending();
        if (root.docs) {
            notesList.newPage();
            return;
        }
        AppController.newNote();
        root._focusNewNote();
    }
    // A new note opens on its title, selected: what is typed names it, and
    // the caret after "# Untitled note" made it "Untitled noteStandup"
    // (PERSONA-20, IDIOT-KNOW-9).
    function _focusNewNote() {
        Qt.callLater(function () {
            if (root.viewMode === "live") {
                // An empty note (dogfood 2026-10-20): its first line, open.
                if (AppController.notesState.length === 0) liveEditor.editLine(0, false);
                else liveEditor.editTitle();
                return;
            }
            editor.forceActiveFocus();
            const m = /^(#{1,6}\s+)(.*)$/.exec(editor.text.split("\n")[0]);
            if (m && m[2].length > 0) editor.select(m[1].length, m[1].length + m[2].length);
            else editor.cursorPosition = editor.length;
        });
    }
    // The document on screen: an open page, else the note (IDIOT-KNOW-2).
    function _focusEditorAtEnd() {
        Qt.callLater(function () {
            if (root.pageId.length > 0) {
                pageEditor.focusEditor();
                return;
            }
            if (root.viewMode === "live") {
                liveEditor.focusEnd();
                return;
            }
            editor.forceActiveFocus();
            editor.cursorPosition = editor.length;
        });
    }
    // An edit made in the live view, into the source field: only what
    // differs, so the field keeps its undo history and the save follows.
    function _applyFromLive(next) {
        const old = editor.text;
        if (old === next) return;
        let p = 0;
        const max = Math.min(old.length, next.length);
        while (p < max && old.charCodeAt(p) === next.charCodeAt(p)) p++;
        let s = 0;
        while (s < max - p && old.charCodeAt(old.length - 1 - s) === next.charCodeAt(next.length - 1 - s)) s++;
        const inserted = next.substring(p, next.length - s);
        // One edit: as a remove then an insert, the drawn note was handed
        // the text in between, reloaded it and lost its undo — a tick could
        // not be taken back (KNOW-2).
        if (mdDocument.replaceRange(editor.textDocument, p, old.length - s, inserted)) return;
        if (old.length - s > p) editor.remove(p, old.length - s);
        if (inserted.length > 0) editor.insert(p, inserted);
    }
    // Where the keyboard goes when the view is switched to (PERA-5): the
    // editor, caret where it was left; in preview there is nothing to type
    // into, so the notes list.
    // Onto the drawn document, as every other view and the task document
    // take the keyboard: the keymap's chords are keys there and ↵ opens the
    // text. In the text, g n then g t wrote "gt" into the note (A4-4). A
    // letter that is no key still lands in the note (IDIOT-KNOW-6), and a
    // bare "u" never reaches a change made on another screen (IDIOT-KNOW-5,
    // undoKeyLive). An open page is the document on screen.
    // Arriving on Knowledge from another screen: the caret at the end of the
    // note, ready to type on (dogfood 2026-10-16); a Ctrl K hit that jumps
    // to a line keeps its line.
    function arrive() {
        // Going to Notes opened a new, empty note: the caret goes in it, not
        // back to the list the keyboard was on last time (dogfood 2026-10-20).
        // The drawn (live) note keeps the keyboard on the document, where a
        // letter typed writes into it and the keymap's chords stay keys.
        if (root._noteShown && root.viewMode !== "live" && AppController.isFreshNote(AppController.activeNoteId)) {
            notesList.reclaimFocus();
            root._focusEditorAtEnd();
            return;
        }
        root.takeFocus();
        if (root.jumpToLine >= 0 || root.pageId.length > 0) return;
        if (editor.activeFocus) editor.cursorPosition = editor.length;
    }
    function takeFocus() {
        if (notesList.reclaimFocus()) return;
        if (root.pageId.length > 0) pageEditor.focusDocument();
        else if (root.viewMode === "live" && root._noteShown) liveEditor.focusDocument();
        else if (root.viewMode !== "preview" && root._noteShown) editor.forceActiveFocus();
        else if (root._listShown) notesList.takeFocus();
        else root.forceActiveFocus();
    }
    // The drawn editor on screen (a page's or the note's), or null.
    function _shownLive() {
        if (root.pageId.length > 0) return pageEditor.liveEditor;
        if (root.viewMode === "live" && root._noteShown) return liveEditor;
        return null;
    }
    // The keymap's bare "u" (Main asks): on the drawn document the
    // document's own edit first; past that, only what was done to notes and
    // pages. A change made on a screen not in sight is Ctrl+Z's, which says
    // what it took back (IDIOT-KNOW-5).
    function undoKeyLive() {
        const ed = root._shownLive();
        if (ed && ed.documentFocused && ed.canUndoHere) return true;
        return AppController.hasPendingUndo && AppController.undoIsKnowledgeOnly();
    }
    // On the drawn note the bare key is always the note's: with nothing of
    // its own to take back and a change elsewhere on top, "u" was typed into
    // the note as a letter (VB5-R1). It stays a key, and says so.
    function undoKeyOwned() {
        const ed = root._shownLive();
        return !!ed && ed.documentFocused && !ed.readOnly;
    }
    // False when nothing was taken back (Main says why). `ctrlZ`: Ctrl+Z
    // itself, which past the note's own edits takes back the app's last step
    // wherever it was made, and says so with a Redo (dogfood 2026-10-30);
    // the bare "u" keeps to notes and pages.
    function undoKey(ctrlZ) {
        const ed = root._shownLive();
        if (ed && ed.documentFocused && ed.undoHere()) return true;
        if (ctrlZ === true) {
            if (!AppController.hasPendingUndo) return false;
            AppController.undoPastDocument();
            return true;
        }
        if (!AppController.hasPendingUndo || !AppController.undoIsKnowledgeOnly()) return false;
        AppController.undo();
        return true;
    }
    // A row of the links column followed: the note opens and the keyboard
    // goes on to it. Done here, not in the row: the row is rebuilt for the
    // note opened and its own code had no `root` left to call (VB5-R3).
    function _openLinkedNote(noteId) {
        root._flushPending();
        AppController.activeNoteId = noteId;
        Qt.callLater(root.takeFocus);
    }
    // F6 walks the parts of the screen (Main asks, A4-27): the list, the
    // document, the links beside it. `entering`: F6 has just come in from
    // the sidebar or the header — the first part, the last going back.
    // Returns the part that took the keyboard, or null where F6 leaves.
    function _firstStop(it) {
        if (!it || !it.visible || it.enabled === false) return null;
        if (it.activeFocusOnTab === true) return it;
        const kids = it.children;
        for (let i = 0; i < kids.length; i++) {
            const r = root._firstStop(kids[i]);
            if (r) return r;
        }
        return null;
    }
    function _regionParts() {
        const out = [];
        if (root._listShown) out.push(notesList);
        out.push(docColumn);
        if (linksPane.visible && root._firstStop(linksPane)) out.push(linksPane);
        return out;
    }
    function stepRegion(dir, entering) {
        const parts = root._regionParts();
        let i;
        if (entering) {
            i = dir > 0 ? 0 : parts.length - 1;
        } else {
            let at = -1;
            for (let p = root.Window.activeFocusItem; p && at < 0; p = p.parent)
                at = parts.indexOf(p);
            if (at < 0) return null;
            i = at + dir;
            if (i < 0 || i >= parts.length) return null;
        }
        const part = parts[i];
        if (part === notesList) notesList.focusList();
        else if (part === linksPane) root._firstStop(linksPane).forceActiveFocus(Qt.TabFocusReason);
        else root.takeFocus();
        return part;
    }
    // The cursor keys (APP-276): j / k (↑ ↓) walk the list of notes, Enter
    // opens the note to write in. Only while the focus is not in the text,
    // where a letter is a letter.
    function moveCursor(dx, dy) {
        if (dy === 0 || !root._listShown) return;
        notesList.step(dy);
        notesList.focusList();
    }
    function openCursor() { root._focusEditorAtEnd(); }
    // m / the Menu key (the keymap's card menu): the open note's menu in
    // the list — pin, rename, delete from the keyboard.
    function openCursorMenu() {
        if (root.docs || !root._listShown) return;
        notesList.focusList();
        const r = notesList._activeRowItem();
        if (r) r.openMenu();
    }

    // Ctrl+F in Notes filters the notes, not the task search in the top bar;
    // with the keyboard in the note's text it finds in the note (dogfood
    // 2026-10-30).
    function focusSearch() {
        if (root._noteShown && root.viewMode !== "live" && (editor.activeFocus || findBar.hasKeys)) {
            findBar.open();
            return;
        }
        if (!root._listShown) root._listPref = "shown";
        notesList.focusFilter();
    }

    // ── Scroll sync between the two panes ────────────────────────────
    // Which pane last moved under the reader's hand. The follower's own
    // movement re-enters here, so the leader is held for a moment to stop the
    // two from chasing each other.
    property string _syncOwner: ""
    Timer {
        id: syncRelease
        interval: 150
        onTriggered: root._syncOwner = ""
    }

    function _syncFrom(who) {
        if (root.viewMode !== "split") return;
        if (root._syncOwner !== "" && root._syncOwner !== who) return;
        root._syncOwner = who;
        syncRelease.restart();
        Qt.callLater(root._applySync, who);
    }

    function _applySync(who) {
        if (root.viewMode !== "split") return;
        if (who === "editor") {
            // Top visible line of the editor → the row that holds it.
            const line = editor.text.substring(0, editor.positionAt(0, notesScroll.contentY))
                               .split("\n").length - 1;
            const row = mdDocument.rowForLine(line);
            if (row >= 0) preview.positionViewAtIndex(row, ListView.Beginning);
        } else {
            const row = preview.indexAt(1, preview.contentY + 1);
            if (row < 0) return;
            const line = mdDocument.firstLineOfRow(row);
            if (line < 0) return;
            const rect = editor.positionToRectangle(mdDocument.positionForLine(line));
            notesScroll.contentY = Math.max(0, Math.min(rect.y,
                Math.max(0, notesScroll.contentHeight - notesScroll.height)));
        }
    }

    function _fuzzyScore(q, s) {
        if (q.length === 0) return 0;
        const ql = q.toLowerCase();
        const sl = s.toLowerCase();
        let i = 0, j = 0, score = 0, lastPos = -2;
        while (i < ql.length && j < sl.length) {
            if (ql[i] === sl[j]) {
                score += (lastPos + 1 === j ? 10 : 2);
                lastPos = j;
                i++;
            }
            j++;
        }
        if (i < ql.length) return -1;
        score -= lastPos * 0.05;
        // Clamp: a late single-char match must not go negative and be dropped by
        // the caller's `if (sc < 0) continue` as though the char were absent.
        return Math.max(0, score);
    }

    // Ranked already, the way every @-mention box finds people: the id, a
    // login made of the name ("r.losev"), the name's words in either script.
    function _peopleEntries(filter) {
        const hits = AppController.matchPeople(filter, 8);
        const out = [];
        for (let i = 0; i < hits.length; i++) {
            out.push({ kind: "person", id: hits[i].id, label: hits[i].name,
                       sub: hits[i].role || "", color: hits[i].color });
        }
        return out;
    }
    function _linkTaskEntries(filter) {
        const hits = AppController.matchTasks(filter, 4);
        const out = [];
        for (let i = 0; i < hits.length; i++)
            out.push({ kind: "task", id: hits[i].id, label: hits[i].title, sub: hits[i].id, color: Theme.accent });
        return out;
    }
    function _taskEntries() {
        const out = [];
        const m = AppController.tasks;
        for (let i = 0; i < m.rowCount(); i++) {
            const idx = m.index(i, 0);
            out.push({
                kind: "task",
                id:    m.data(idx, Qt.UserRole + 1),
                label: m.data(idx, Qt.UserRole + 2),
                sub:   m.data(idx, Qt.UserRole + 4) || "",
                color: Theme.accent
            });
        }
        return out;
    }

    // [[ offers the notes first — linking another note is what [[ is for now
    // that a profile has many — then this note's own headings. It offered
    // only the headings.
    function _headingEntries() {
        const out = [];
        const m = AppController.notes;
        const rTitle = m.roleOf("title"), rFolder = m.roleOf("folder"), rId = m.roleOf("id");
        for (let i = 0; i < m.rowCount(); i++) {
            const idx = m.index(i, 0);
            if (String(m.data(idx, rId)) === AppController.activeNoteId) continue;
            out.push({ kind: "note", id: "", label: String(m.data(idx, rTitle) || ""),
                       sub: String(m.data(idx, rFolder) || ""), color: Theme.accent });
        }
        // From the document's own outline, already parsed: asking for the
        // headings of editor.text parsed the whole note on every keystroke
        // typed inside [[ ]], 150 ms a key at 3 MB.
        // Fresh for an ordinary note; a huge one keeps its last parse.
        if (mdDocument.lastParseMs < 30) mdDocument.flush();
        const hs = mdDocument.outline;
        const seen = ({});
        for (let i = 0; i < hs.length; i++) {
            const h = hs[i].text;
            if (!h || seen[h]) continue;
            seen[h] = true;
            out.push({ kind: "heading", id: "", label: h, sub: "", color: Theme.accent });
        }
        return out;
    }

    // A character that can be part of an @name or #tag: letters of any
    // script, digits, and _ . - — the ASCII-only test made Cyrillic names
    // impossible to mention from the editor.
    function _isNameChar(ch) {
        if (!ch) return false;
        if (/[0-9_.\-]/.test(ch)) return true;
        return ch.toLowerCase() !== ch.toUpperCase();
    }

    function _rebuildMatches() {
        if (acTrigger === "@") {
            acMatches = _peopleEntries(acFilter);
            acSelected = 0;
            return;
        }
        // [[ offers tasks too, after the notes: "/ Link to a task" put an
        // empty [[]] with nothing to pick from (PERSONA-21).
        const source = acTrigger === "#"  ? _taskEntries()
                     : acTrigger === "[[" ? _headingEntries().concat(acFilter.trim().length > 0 ? _linkTaskEntries(acFilter) : [])
                     : [];
        const scored = [];
        for (let i = 0; i < source.length; i++) {
            const e = source[i];
            const hay = (acTrigger === "#" || e.kind === "task") ? (e.id + " " + e.label) : e.label;
            const sc = _fuzzyScore(acFilter, hay);
            if (sc < 0) continue;
            scored.push({ entry: e, score: sc });
        }
        scored.sort(function (a, b) { return b.score - a.score; });
        acMatches = scored.slice(0, 8).map(function (x) { return x.entry; });
        acSelected = 0;
    }

    // ↑ ↓ Enter Tab Esc while the list is up, for whichever field it serves.
    function _acKey(event) {
        if (!acPopup.opened || acMatches.length === 0) return false;
        if (event.key === Qt.Key_Down) {
            root.acSelected = Math.min(root.acMatches.length - 1, root.acSelected + 1);
            return true;
        }
        if (event.key === Qt.Key_Up) {
            root.acSelected = Math.max(0, root.acSelected - 1);
            return true;
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab) {
            root._commitAutocomplete();
            return true;
        }
        if (event.key === Qt.Key_Escape) {
            root._hideAutocomplete();
            return true;
        }
        return false;
    }

    function _hideAutocomplete() {
        acTrigger = "";
        acTriggerPos = -1;
        acFilter = "";
        acMatches = [];
        acPopup.close();
    }

    function _isTriggerContextChar(ch) {
        // Allow @ / # only at word-boundaries (start of text, after whitespace
        // or after common separators).
        if (!ch) return true;
        return /[\s.,;:!?()\[\]{}]/.test(ch);
    }

    function _openAcPopup() {
        if (acMatches.length === 0) { acPopup.close(); return; }
        const cr = root._acField.cursorRectangle;
        const p  = root._acField.mapToItem(root, cr.x, cr.y + cr.height);
        acPopup.x = Math.min(p.x, root.width - acPopup.width - 8);
        acPopup.y = Math.max(0, Math.min(p.y + 4, root.height - acPopup.height - 8));
        if (!acPopup.opened) acPopup.open();
    }

    function _detectAutocomplete(field) {
        root._acField = field || editor;
        if (!root._acField.activeFocus && root._acField !== editor) { _hideAutocomplete(); return; }
        const pos = root._acField.cursorPosition;
        if (pos <= 0) { _hideAutocomplete(); return; }
        // Only the text just before the caret. Reading `editor.text` here
        // copied the whole note on every keystroke and every caret move.
        const base = Math.max(0, pos - 512);
        const txt = root._acField.getText(base, pos);
        const end = txt.length;

        // [[wiki-link]] trigger (HEAP-79) — filter may contain spaces, so scan
        // to the nearest "[[" on the current line rather than a single word.
        const lineStart = txt.lastIndexOf("\n", end - 1) + 1;
        const before = txt.substring(lineStart, end);
        const openIdx = before.lastIndexOf("[[");
        if (openIdx >= 0 && before.substring(openIdx + 2).indexOf("]]") < 0) {
            acTrigger    = "[[";
            acTriggerPos = base + lineStart + openIdx;   // index of the first '['
            acFilter     = before.substring(openIdx + 2);
            _rebuildMatches();
            _openAcPopup();
            return;
        }

        let i = end - 1;
        while (i >= 0 && end - i < 64 && root._isNameChar(txt[i])) i--;
        // "@roman losev": one space inside a person's query, once a word
        // follows it ("@Roman_Losev " as inserted still ends the mention).
        // The list closes again when the two words stop matching anyone.
        if (i > 0 && i < end - 1 && txt[i] === " ") {
            let j = i - 1;
            while (j >= 0 && end - j < 64 && root._isNameChar(txt[j])) j--;
            if (j >= 0 && j < i - 1 && txt[j] === "@") i = j;
        }
        if (i < 0) { _hideAutocomplete(); return; }
        const ch = txt[i];
        if (ch !== "@" && ch !== "#") { _hideAutocomplete(); return; }
        if (i > 0 && !_isTriggerContextChar(txt[i - 1])) {
            _hideAutocomplete();
            return;
        }
        acTrigger    = ch;
        acTriggerPos = base + i;
        acFilter     = txt.substring(i + 1, end);
        _rebuildMatches();
        if (acMatches.length === 0) {
            acPopup.close();
            return;
        }
        _openAcPopup();
    }

    function _slugifyName(s) { return (s || "").replace(/\s+/g, "_"); }
    // A note title or heading as it goes between [[ ]]: "#" and "|" would
    // start a heading and a label, so they are escaped, as is a backslash
    // that would escape what follows it. NoteGraph.h escapeLinkName is the
    // same rule; resolveLink reads it back.
    function _escapeLinkName(s) {
        return (s || "").replace(/\\(?=[!-\/:-@\[-`{-~]|$)/g, "\\\\").replace(/[#|]/g, "\\$&");
    }

    function _commitAutocomplete() {
        if (acMatches.length === 0 || acSelected < 0 || acSelected >= acMatches.length) {
            _hideAutocomplete();
            return;
        }
        const e = acMatches[acSelected];
        const f = root._acField;
        const insert = (acTrigger === "@")  ? "@" + _slugifyName(e.label) + " "
                     : (acTrigger === "[[") ? "[[" + (e.kind === "task" ? e.id : root._escapeLinkName(e.label)) + "]] "
                     : "#" + e.id + " ";
        let pos = f.cursorPosition;
        // The "]]" the "/" menu put after the caret is part of the link.
        if (acTrigger === "[[" && f.getText(pos, pos + 2) === "]]") pos += 2;
        // Replace the "@filter" / "#filter" span in place (remove + insert) so
        // the caret stays at the edit point instead of resetting to 0. (HEAP-65)
        Mention.commit(f, acTriggerPos, pos, insert);
        _hideAutocomplete();
    }

    // "/today", "/fri" typed as a word of its own becomes an ISO date. Only
    // as a word of its own: "http://x.com/now" and "src/mon/main.c" are a URL
    // and a path, and rewriting them on Enter mangled both.
    function _expandSlashDate() {
        const pos = editor.cursorPosition;
        const txt = editor.text;
        const lineStart = txt.lastIndexOf("\n", pos - 1) + 1;
        const m = txt.substring(lineStart, pos).match(/(^|[\s(\[{])\/([A-Za-zА-Яа-яЁё]+)$/);
        if (!m) return false;
        const word = m[2];
        const parsed = AppController.parseDateTime(word, new Date());
        if (!parsed || !parsed.ok || !parsed.start) return false;
        const d = parsed.start;
        const iso = d.getFullYear() + "-" +
                    String(d.getMonth() + 1).padStart(2, "0") + "-" +
                    String(d.getDate()).padStart(2, "0");
        const slashPos = pos - (word.length + 1);
        editor.remove(slashPos, pos);
        editor.insert(slashPos, iso);
        return true;
    }

    // Ctrl+Z / Ctrl+Shift+Z when the text field has nothing of its own left to
    // undo: hand the key to the app's undo, which holds note deletes and
    // imports. Returns true when it did.
    function _globalUndoFallback(field, event) {
        if (event.matches(StandardKey.Undo) && !field.canUndo && AppController.hasPendingUndo) {
            AppController.undoPastDocument();
            event.accepted = true;
            return true;
        }
        if (event.matches(StandardKey.Redo) && !field.canRedo && AppController.canRedo) {
            AppController.redo();
            event.accepted = true;
            return true;
        }
        return false;
    }

    // Move the caret to `off` and scroll the editor so it is visible.
    function _jumpToOffset(off) {
        if (off < 0) return;
        // Live: the block of that line opens, caret at its start. The hidden
        // source field took the focus, and what was typed went in unseen,
        // breaking the heading (KNOW-3).
        if (root.viewMode === "live") {
            root.pageId = "";
            // At the end of the line, past a heading's "## ": at its start the
            // first letter typed broke the heading (KNOW-3).
            liveEditor.editLine(editor.text.substring(0, off).split("\n").length - 1, false);
            return;
        }
        if (root.viewMode === "preview") root.viewMode = "split";
        editor.forceActiveFocus();
        editor.cursorPosition = off;
        const cr = editor.positionToRectangle(off);
        const maxY = Math.max(0, notesScroll.contentHeight - notesScroll.height);
        notesScroll.contentY = Math.max(0, Math.min(cr.y - 40, maxY));
    }
    function _jumpToHeading(name) {
        _jumpToOffset(AppController.noteHeadingOffset(editor.text, name));
    }
    function _jumpToLine(lineNum) {
        const lines = editor.text.split("\n");
        let off = 0;
        for (let i = 0; i < lineNum - 1 && i < lines.length; i++) off += lines[i].length + 1;
        _jumpToOffset(off);
    }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── The list (DG-070) ─────────────────────────────────────
        // Which notes there are, and which one is open. Runs the full height
        // with its own "Knowledge +" on top (sheet H2-Knowledge); folds away
        // on a narrow window, where the note needs the width more.
        NotesListPane {
            id: notesList
            objectName: "notes-list-pane"
            visible: root._listShown
            Layout.preferredWidth: visible ? Math.min(Theme.px(260), root.width * 0.35) : 0
            Layout.fillHeight: true
            mode: root.mode
            openPageId: root.pageId
            shownKey: root.shownKey
            stepHints: root._altHeld && root._keysOn && !root.docs
            onNoteActivated: (id) => {
                // Flush first: the editor debounces its saves, so the last
                // keystrokes are still only in the text field here and
                // switching would drop them.
                root._flushPending();
                root.pageId = "";
                root.shownDoc = null;
                AppController.activeNoteId = id;
            }
            // "+" makes a note to write in: the cursor goes there.
            onNoteCreated: (id) => root._focusNewNote()
            // Return on the list (IDIOT-KNOW-8); Esc on an empty filter goes
            // back to the note drawn (IDIOT-KNOW-7, A4-6).
            onEditRequested: root._focusEditorAtEnd()
            onDocumentRequested: root.takeFocus()
            onImportRequested: root.importRequested()
            onTableImportRequested: (what) => root.importTable(what)
            // A page or a snippet opens here in the document, a reference
            // under the link rules; a contact's handle is copied.
            onRefActivated: (url) => preview.openExternal(url)
            onPageActivated: (id) => root.openPage(id)
            onItemShown: (r) => root.showDoc(r)
            onContactActivated: (handle) => {
                AppController.copyToClipboard(handle);
                AppController.showToast(I18n.t("docs.toast.copied").arg(handle));
            }
        }

        // ── The document (DG-071) ─────────────────────────────────
        ColumnLayout {
            id: docColumn
            objectName: "notes-document"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // What this is and how fresh, above the title — no toolbar.
            // The files, the list, the source and the links stay on their
            // keys and in Ctrl+K.
            Text {
                objectName: "notes-meta"
                Layout.fillWidth: true
                Layout.leftMargin: root._docInset + root._mdSide
                Layout.rightMargin: root._docInset
                Layout.topMargin: Style.quiet ? Theme.px(40) : Theme.px(26)
                // 18px down to the title (sheets H2/Q-Knowledge, R4-067).
                Layout.bottomMargin: Theme.spXs + Theme.sp2xl
                text: root._metaLine
                color: Theme.textDim
                font.pixelSize: Theme.fsSm
                elide: Text.ElideRight
            }
            // ── The note's files ──────────────────────────────────────
            Rectangle {
                objectName: "notes-attachments"
                visible: root._noteAttachments.length > 0 && root._noteShown
                Layout.fillWidth: true
                Layout.preferredHeight: noteChips.implicitHeight + 2 * Theme.spSm
                color: Theme.panel
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.border }
                AttachmentChips {
                    id: noteChips
                    objectName: "notes-attachment-chips"
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.inset; anchors.rightMargin: Theme.inset
                    model: root._noteAttachments
                    onRemoveRequested: (attachmentId) => root.removeAttachmentRefs(attachmentId)
                }
            }

            // ── Editor + preview body ─────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: root._docInset
                spacing: 0


                // Editor pane — visible in edit + split modes.
                Flickable {
                    id: notesScroll
                    visible: (root.viewMode === "rich" || root.viewMode === "edit" || root.viewMode === "split") && root._noteShown
                    // Halves of equal weight in split: one fill alone took
                    // the whole row and left the other at zero.
                    Layout.preferredWidth: 1
                    // Scroll sync: the editor leads while the reader scrolls it.
                    onContentYChanged: if (root.viewMode === "split") root._syncFrom("editor")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: editor.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ThinScrollBar {}

                    // The wheel is ScrollFeel's, as in every view (it moved
                    // 3x the notch here, dogfood 2026-10-20).

                    // Rich (live preview): what is drawn over the hidden source -
                    // the code box under the text, bullets, boxes, bars, rules
                    // and tables over it - and the source colours in split/edit.
                    MdLivePreview {
                        id: livePreview
                        editor: editor
                        rich: root.viewMode === "rich"
                        // The note's text at 1.7, as the drawn note (a-02, R3-083).
                        leading: 1.7
                        // The text column and the code column (dogfood 2026-10-19).
                        proseRight: root._cols.editor - root._cols.text
                        wideRight: root._cols.editor - root._cols.code
                        // Find in the note: a table's cells tint their matches
                        // (the bar's tints are under the grid there).
                        findMatches: findBar.visible ? findBar.matches : []
                        findQuery: findBar.visible ? findBar.query : ""
                        findCaseSensitive: findBar.caseSensitive
                        findCurrent: findBar.current
                        onInternalLinkActivated: (kind, target) => root._followLink(kind, target)
                    }

                    TextArea {
                        id: editor
                        ContextMenu.menu: TextEditMenu {
                            id: noteTextMenu
                            editor: editor
                            interceptPaste: true
                            onPasteRequested: {
                                if (!root.pasteAttachment() && !root.pasteTable()) editor.paste();
                            }
                            onAboutToShow: noteTextMenu.inTable = root.caretInTable()
                            onTableAction: (op) => root.tableOp(op)
                        }
                        objectName: "notesEditor"
                        x: 24; y: 16
                        // As wide as the wider of the text column and the code
                        // column, in every mode; prose stops at its own edge
                        // (the highlighter's block margins).
                        width: root._cols.editor
                        // Always fill at least the visible viewport so the user can
                        // click anywhere on the canvas to start typing; grow with
                        // content otherwise.
                        height: Math.max(notesScroll.height - 32, implicitHeight + 16)
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        placeholderText: I18n.t("notes.placeholderBody")
                        placeholderTextColor: Theme.textDim
                        color: Theme.text
                        cursorDelegate: TextCaret { field: editor }
                        // The note's own face in rich, where it reads as drawn;
                        // the source in mono in split and edit.
                        font.family: root.viewMode === "rich" ? Theme.fontUi : Theme.fontMono
                        font.pixelSize: root.viewMode === "rich" ? Theme.fsLg : Theme.fsMd
                        background: Item {}
                        onTextChanged: {
                            root._scheduleSave();
                            if (editor.activeFocus) root._detectAutocomplete();
                            statsTimer.restart();
                            linksTimer.restart();
                        }
                        onCursorPositionChanged: if (editor.activeFocus) root._detectAutocomplete()
                        Keys.priority: Keys.BeforeItem

                        // Qt delivers shortcut events before key presses, so a
                        // global Ctrl+K would open the command palette and this
                        // field would never see the key. Claiming these while the
                        // editor has focus is what makes them editor-scoped: they
                        // still mean what they always did everywhere else.
                        Keys.onShortcutOverride: (event) => {
                            if (mdEditor.claimsShortcut(event.key, event.modifiers)) event.accepted = true;
                        }

                        Keys.onPressed: (event) => {
                            // Autocomplete navigation (when popup is open) takes
                            // priority over markdown continuation.
                            if (root._acKey(event)) { event.accepted = true; return; }

                            // Tab indents here, so it cannot also be the way out
                            // (SHELL-18). Esc steps out to the list of notes;
                            // Ctrl+Tab / Ctrl+Shift+Tab and F6 / Shift+F6 move on
                            // along the focus chain, as in any editor that keeps
                            // Tab for itself.
                            if (root._leaveEditorKey(editor, event.key, event.modifiers)) {
                                event.accepted = true;
                                return;
                            }

                            // "/" on an empty line: the insert menu, as in the
                            // task document (a-03). Anywhere else "/" is
                            // typed, so "/завтра" still expands to a date.
                            // In an empty table cell, or on the line under a
                            // table, it also has the table's rows and columns.
                            if (root.viewMode === "rich" && event.text === "/" && (root._caretOnEmptyLine() || root._caretInEmptyCell())) {
                                noteSlash.tableAt = mdEditor.tableAnchor(editor.cursorPosition);
                                noteSlash.openAtCaret();
                                event.accepted = true;
                                return;
                            }

                            // Ctrl+A in rich takes the note's text, not its title
                            // (the "# " heading on the first line); pressed again
                            // over that, everything.
                            if (root.viewMode === "rich" && event.matches(StandardKey.SelectAll)) {
                                const r = root._bodyRange(editor.text);
                                if (r.start > 0 && !(editor.selectionStart === r.start && editor.selectionEnd === r.end)) {
                                    editor.select(r.start, r.end);
                                    event.accepted = true;
                                    return;
                                }
                            }

                            // A file or a picture on the clipboard is attached
                            // and linked; a table becomes a pipe table; anything
                            // else pastes as text, as Ctrl+Shift+V always does.
                            // Right after a picture pasted with text beside it,
                            // Ctrl+Shift+V puts the text in its place.
                            if (event.key === Qt.Key_V && (event.modifiers & ~Qt.KeypadModifier) === (Qt.ControlModifier | Qt.ShiftModifier)) {
                                root.pasteAsText();
                                event.accepted = true;
                                return;
                            }
                            if (event.matches(StandardKey.Paste) && (root.pasteAttachment() || root.pasteTable())) {
                                event.accepted = true;
                                return;
                            }

                            // Ctrl+Z with nothing left to undo in the text means the
                            // last thing done to the notes themselves — a delete, an
                            // import. The field is always focused here, so without
                            // this the app-wide undo was unreachable from Notes.
                            if (root._globalUndoFallback(editor, event)) return;

                            // Slash-commands — /today, /tomorrow, /завтра, etc.
                            // Tab or Enter replaces "/<word>" with a parsed ISO
                            // date when the chrono parser recognises the word.
                            if (event.key === Qt.Key_Tab
                                || event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter)
                            {
                                if (root._expandSlashDate() && event.key === Qt.Key_Tab) {
                                    event.accepted = true;
                                    return;
                                }
                                // Enter still ends the line after expanding: the
                                // reader pressed it to get a new one.
                            }

                            // Everything below is markdown editing, which lives
                            // in C++ so it can be tested without driving the UI,
                            // and is shared with the doc page editor. See
                            // MdEditOps: each operation is one undo step.
                            mdEditor.setSelection(editor.selectionStart, editor.selectionEnd);
                            if (mdEditor.handleKey(event.key, event.modifiers) || mdEditor.typeText(event.text)) event.accepted = true;
                        }
                        // A right-click puts the caret where it was made (with
                        // nothing selected), so the menu's table rows are about
                        // the table under the pointer.
                        PointHandler {
                            acceptedButtons: Qt.RightButton
                            onActiveChanged: {
                                if (active && editor.selectionStart === editor.selectionEnd)
                                    editor.cursorPosition = editor.positionAt(point.position.x, point.position.y);
                            }
                        }
                    }

                    // The right edges of the text column and the code column:
                    // drawn, and dragged to set them (dogfood 2026-10-19).
                    // Over the viewport, not the text, so they stay in view.
                    NoteColumnEdge {
                        id: codeEdge
                        objectName: "notes-code-edge"
                        viewport: notesScroll
                        originX: editor.x
                        // One line when the two columns end together.
                        visible: root.codeEdgeOn && root._noteShown && root.viewMode !== "live" && Math.abs(root._cols.code - root._cols.text) >= 2
                        edgeX: editor.x + root._cols.code
                        label: I18n.t("notes.edge.code")
                        prefWidth: root.codeWidthPref
                        onDragTo: (v) => root._dragCode = v
                        onDragDone: (v) => { root.writeNotesPref("codeWidth", v); root._dragCode = -1; }
                        onResetRequested: { root.writeNotesPref("codeWidth", undefined); root._dragCode = -1; }
                        dim: true
                    }
                    NoteColumnEdge {
                        id: textEdge
                        objectName: "notes-text-edge"
                        viewport: notesScroll
                        originX: editor.x
                        visible: root.textEdgeOn && root._noteShown && root.viewMode !== "live"
                        edgeX: editor.x + root._cols.text
                        label: I18n.t("notes.edge.text")
                        prefWidth: root.textWidthPref
                        onDragTo: (v) => root._dragText = v
                        onDragDone: (v) => { root.writeNotesPref("textWidth", v); root._dragText = -1; }
                        onResetRequested: { root.writeNotesPref("textWidth", undefined); root._dragText = -1; }
                    }
                }
                // Vertical divider — only in split mode.
                Rectangle {
                    visible: root.viewMode === "split" && root._noteShown
                    Layout.preferredWidth: 1
                    Layout.maximumWidth: 1
                    Layout.fillHeight: true
                    color: Theme.border
                }

                // Preview pane — visible in preview + split modes.
                //
                // Draws the parsed document row by row. It replaces a read-only
                // TextArea with textFormat: MarkdownText, which handed the whole
                // note to Qt and gave nothing back — no say in how an element
                // looked, and no way to ask which lines produced it.
                MdView {
                    id: preview
                    objectName: "notesPreview"
                    // The drawn note beside the source (split). Text in it can
                    // be selected and copied; a double-click puts the caret on
                    // the source line it came from.
                    visible: root.viewMode === "split" && root._noteShown
                    Layout.preferredWidth: 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    document: mdDocument
                    editorDocument: editor.textDocument
                    // The same columns as the source beside it.
                    textColumn: Theme.px(root._dragText >= 0 ? root._dragText : root.textWidthPref)
                    wideColumn: Theme.px(root._dragCode >= 0 ? root._dragCode : root.codeWidthPref)

                    // The checkbox write already went through the editor's own
                    // document; this only keeps the caret where it was.
                    onTaskToggled: {
                        const caret = editor.cursorPosition;
                        editor.cursorPosition = caret;
                    }

                    // Clicking a rendered block puts the caret on the line that
                    // produced it, and shows the editor if it was hidden.
                    onSourceRequested: (line) => {
                        if (root.viewMode === "preview") root.viewMode = "split";
                        editor.forceActiveFocus();
                        editor.cursorPosition = mdDocument.positionForLine(line);
                    }

                    onInternalLinkActivated: (kind, target) => root._followLink(kind, target)

                    Text {
                        anchors.centerIn: parent
                        visible: preview.count === 0
                        text: I18n.t("notes.preview.empty")
                        color: Theme.textDim
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsMd
                    }

                    // ── Scroll sync (split mode) ──────────────────────────
                    // Both panes show the same document, so they should show the
                    // same part of it. Whichever pane the reader is scrolling
                    // leads; the other follows and its own movement is ignored
                    // for a moment, or the two would push each other.
                    onContentYChanged: if (root.viewMode === "split") root._syncFrom("preview")
                }

                // The note drawn, the block under the caret edited in place.
                MdBlockEditor {
                    id: liveEditor
                    objectName: "notes-live"
                    visible: root.viewMode === "live" && root._noteShown
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: editor.text
                    placeholder: I18n.t("notes.placeholderBody")
                    wikiTargets: root._wiki
                    headingRules: false
                    noteType: true
                    textColumn: Theme.px(root.textWidthPref)
                    wideColumn: Theme.px(root.codeWidthPref)
                    typeToEdit: true
                    attachOnPaste: true
                    // No room beside the note: its links under the text.
                    tail: !root.showBacklinks && root._hasLinks ? inlineLinks : null
                    onBlockKey: (event) => { if (root._acKey(event)) event.accepted = true; }
                    onEdited: (t) => root._applyFromLive(t)
                    onInternalLinkActivated: (kind, target) => root._followLink(kind, target)
                    onEscaped: if (root._listShown) notesList.focusList()
                }
                // @ # [[ in the open block (KNOW-8).
                Connections {
                    target: liveEditor.editor
                    function onTextChanged() { if (liveEditor.editor.activeFocus) root._detectAutocomplete(liveEditor.editor) }
                    function onCursorPositionChanged() { if (liveEditor.editor.activeFocus) root._detectAutocomplete(liveEditor.editor) }
                    function onActiveFocusChanged() { if (!liveEditor.editor.activeFocus && root._acField === liveEditor.editor) root._hideAutocomplete() }
                }


                // A page from the former Docs catalogue, drawn and edited in
                // place like a note (DG-070): pages stay where the list shows
                // them instead of in a second screen.
                // Docs with no page open: what to do, in one line.
                EmptyState {
                    objectName: "docs-no-page"
                    visible: root.docs && root.pageId.length === 0 && !root.shownDocData
                    // A short block in the middle, not one line to the
                    // window's edge (EYES-8).
                    Layout.fillWidth: true
                    Layout.maximumWidth: Theme.px(420)
                    Layout.leftMargin: Theme.sp2xl
                    Layout.rightMargin: Theme.sp2xl
                    Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
                    Layout.topMargin: Theme.px(80)
                    title: I18n.t("knowledge.docs.noPage")
                    line: I18n.t("knowledge.docs.noPage.line")
                }
                // A snippet's code, to read and select; copy and edit beside
                // the title.
                ColumnLayout {
                    id: docItemView
                    objectName: "docs-item-view"
                    readonly property var d: root.shownDocData
                    readonly property string tag: d ? d.lang : ""
                    visible: root.docs && root.pageId.length === 0 && !!d
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: Theme.spLg
                    spacing: Theme.spMd
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spMd
                        Text {
                            objectName: "docs-item-title"
                            Layout.fillWidth: true
                            text: docItemView.d ? (docItemView.d.title || docItemView.d.lang) : ""
                            color: Theme.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsLg
                            font.weight: Theme.fwHeading
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: docItemView.tag.length > 0
                            text: docItemView.tag
                            color: Theme.textDim
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsXs
                        }
                        PillButton {
                            objectName: "docs-item-copy"
                            text: I18n.t("docs.copy")
                            onClicked: notesList.copyItem(docItemView.d)
                        }
                        PillButton {
                            objectName: "docs-item-edit"
                            text: I18n.t("knowledge.edit")
                            onClicked: notesList.editItem(docItemView.d.kind, docItemView.d)
                        }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        background: FieldFrame {}
                        TextEdit {
                            objectName: "docs-item-text"
                            readOnly: true
                            selectByMouse: true
                            selectByKeyboard: true
                            padding: Theme.spMd
                            textFormat: TextEdit.PlainText
                            text: docItemView.d ? docItemView.d.code : ""
                            color: Theme.text
                            selectionColor: Theme.accentSoft
                            selectedTextColor: Theme.text
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsSm
                            wrapMode: TextEdit.NoWrap
                        }
                    }
                }
                MdEditorPane {
                    id: pageEditor
                    objectName: "knowledge-page"
                    visible: root.pageId.length > 0
                    bare: true
                    // A doc page as a note: source and drawn page side by side.
                    mode: root.viewMode === "edit" ? "edit" : root.viewMode === "live" ? "live" : "split"
                    pageId: root.pageId
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onInternalLinkActivated: (kind, target) => root._followLink(kind, target)
                }
            }
        }

        // ── The note's links (DG-072) ─────────────────────────────
        // Beside the note when there is room: the tasks it names, what links
        // here, its links out of Tabless. Sheet order: bold leads with the
        // tasks, quiet with the backlinks; an empty group is not drawn.
        Item {
            id: linksPane
            objectName: "notes-links-pane"
            visible: root.showBacklinks && root._noteShown
            Layout.preferredWidth: Theme.px(220)
            Layout.fillHeight: true
            Flickable {
                anchors.fill: parent
                anchors.topMargin: Style.quiet ? Theme.px(112) : Theme.px(60)
                anchors.rightMargin: Theme.sp2xl
                clip: true
                contentHeight: linksCol.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThinScrollBar {}
                ColumnLayout {
                    id: linksCol
                    width: parent.width
                    spacing: 0

                    Loader {
                        Layout.fillWidth: true
                        active: Style.quiet
                        visible: active
                        sourceComponent: backlinksGroup
                    }
                    Loader {
                        Layout.fillWidth: true
                        sourceComponent: tasksGroup
                    }
                    Loader {
                        Layout.fillWidth: true
                        active: !Style.quiet
                        visible: active
                        sourceComponent: backlinksGroup
                    }
                    // Q-Knowledge has no external-links group (R3-091).
                    Loader {
                        Layout.fillWidth: true
                        active: !Style.quiet
                        visible: active
                        sourceComponent: externalGroup
                    }
                }
            }
        }
    }

    // ── The groups of the links column (DG-072) ──────────────────────
    component LinksHeader: Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.spLg
        color: Theme.textDim
        // Sheet: 12px, 600 in bold, regular in quiet (R3-088).
        font.pixelSize: Theme.fsSm
        font.weight: Style.quiet ? Theme.fwBody : Theme.fwHeading
        elide: Text.ElideRight
    }
    component LinksItem: Text {
        id: linkItem
        property string tip: ""
        signal activated()
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.spLg
        textFormat: Text.StyledText
        color: Theme.text
        font.pixelSize: Theme.fsMd
        font.weight: Style.quiet ? Theme.fwBody : Theme.fwTitle
        font.underline: linkCA.hovered
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        ClickArea {
            id: linkCA
            label: linkItem.tip.length > 0 ? linkItem.tip : linkItem.text
            showTip: false
            role: Accessible.Link
            onActivated: linkItem.activated()
        }
    }

    // Below 1000 px there is no room beside the note: the same groups sit
    // under its text instead of vanishing with no trace (A4-12).
    Component {
        id: inlineLinks
        ColumnLayout {
            objectName: "notes-links-inline"
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: Theme.sp2xl
                Layout.bottomMargin: Theme.sp2xl
                color: Theme.border
            }
            Loader { Layout.fillWidth: true; sourceComponent: tasksGroup }
            Loader { Layout.fillWidth: true; sourceComponent: backlinksGroup }
            Loader {
                Layout.fillWidth: true
                active: !Style.quiet
                visible: active
                sourceComponent: externalGroup
            }
        }
    }

    // "APP-101 · В работе": the tasks this note names, with their column now.
    Component {
        id: tasksGroup
        ColumnLayout {
            spacing: 0
            visible: root._taskRefs.length > 0
            LinksHeader { text: I18n.t("notes.tasksInNote") }
            Repeater {
                model: root._taskRefs
                delegate: LinksItem {
                    required property var modelData
                    objectName: "note-task-" + modelData.id
                    readonly property string status: Style.quiet ? String(modelData.statusName).toLowerCase()
                                                                 : String(modelData.statusName)
                    // Bold: the ID bold, the column dimmed (H2-Knowledge, R3-088).
                    text: (Style.quiet ? modelData.id : "<b>" + modelData.id + "</b>")
                          + "<font color=\"" + (Style.quiet ? Theme.textMuted : Theme.textDim) + "\"> · " + status
                          + (modelData.profileName.length > 0 ? " · " + modelData.profileName : "") + "</font>"
                    tip: modelData.id + " " + modelData.title
                    maximumLineCount: 1
                    onActivated: root.taskRequested(modelData.id)
                }
            }
            Item { Layout.preferredHeight: Theme.sp2xl }
        }
    }

    // What links here: other notes, and tasks that name this note [[Note]].
    Component {
        id: backlinksGroup
        ColumnLayout {
            spacing: 0
            visible: root._incoming.length > 0 || root._taskLinks.length > 0
            LinksHeader { text: I18n.t("notes.backlinks") }
            Repeater {
                model: root._incoming
                delegate: LinksItem {
                    required property var modelData
                    objectName: "incoming-" + modelData.noteId
                    text: modelData.title
                    tip: modelData.title + " — " + modelData.text
                    // The keyboard goes on to the note opened (A4-7).
                    onActivated: root._openLinkedNote(modelData.noteId)
                }
            }
            Repeater {
                model: root._taskLinks
                delegate: LinksItem {
                    required property var modelData
                    objectName: "note-linked-task-" + modelData.id
                    text: I18n.t("notes.linkedTask").arg(modelData.id)
                    tip: text + " " + modelData.title
                    onActivated: root.taskRequested(modelData.id)
                }
            }
            Item { Layout.preferredHeight: Theme.sp2xl }
        }
    }

    // Links out of Tabless, opened under the usual rules.
    Component {
        id: externalGroup
        ColumnLayout {
            spacing: 0
            visible: root._external.length > 0
            LinksHeader { text: I18n.t("notes.external") }
            Repeater {
                model: root._external
                delegate: LinksItem {
                    required property var modelData
                    textFormat: Text.PlainText
                    text: modelData.title.length > 0 ? modelData.label + " · " + modelData.title : modelData.label
                    tip: modelData.url
                    onActivated: preview.openExternal(modelData.url)
                }
            }
        }
    }

    // Ctrl+Shift+M — cycle edit → split → preview → edit.
    //
    // Was Ctrl+Shift+P, which is also profile.new in the global shortcut
    // catalog: both fired, and which one won depended on where focus was.
    Shortcut {
        sequence: "Ctrl+Shift+M"
        context: Qt.WindowShortcut
        enabled: root.visible
        onActivated: {
            root.viewMode = root.viewMode === "rich" ? "split" : root.viewMode === "split" ? "edit" : "rich";
        }
    }

    // Notes navigation from the keyboard. step() existed and nothing was bound
    // to it; a new note needed the mouse. Rebindable in Settings → Hotkeys.
    // Not behind the task document or a dialog: there they switched and
    // renamed a note nobody could see (IDIOT-KNOW-4). Main sets keysLive.
    property bool keysLive: true
    readonly property bool _keysOn: root.visible && root.keysLive
    // "notes.new" (Ctrl+Alt+N) is KeyRouter's, from any screen: it goes to
    // Notes and starts one there (runCommand). As a Shortcut here it only
    // worked once Notes was already open (dogfood 2026-10-16).
    Shortcut {
        sequence: root._kbd("notes.next")
        context: Qt.WindowShortcut
        enabled: root._keysOn && sequence.length > 0
        onActivated: root.stepNote(1)
    }
    Shortcut {
        sequence: root._kbd("notes.prev")
        context: Qt.WindowShortcut
        enabled: root._keysOn && sequence.length > 0
        onActivated: root.stepNote(-1)
    }
    // Alt ↓ / Alt ↑, the same from anywhere in Notes and from the text too
    // (dogfood 2026-10-30): the arrows with Alt mean nothing in the editor.
    Shortcut {
        objectName: "notes-next-alt-shortcut"
        sequence: root._kbd("notes.next.alt")
        context: Qt.WindowShortcut
        enabled: root._keysOn && !root.docs && !root._focusInPopup && sequence.length > 0
        autoRepeat: true
        onActivated: root.stepNote(1)
    }
    Shortcut {
        objectName: "notes-prev-alt-shortcut"
        sequence: root._kbd("notes.prev.alt")
        context: Qt.WindowShortcut
        enabled: root._keysOn && !root.docs && !root._focusInPopup && sequence.length > 0
        autoRepeat: true
        onActivated: root.stepNote(-1)
    }
    // Ctrl+Tab and Ctrl+W (dogfood 2026-10-20). A Shortcut, so the editor's
    // Tab (indent, the next table cell) never sees the Ctrl form. Not while
    // a popup of the note (the "/" menu, a suggestion list) has the keys:
    // the switch would happen behind it.
    readonly property bool _focusInPopup: {
        for (let p = root.Window.activeFocusItem; p; p = p.parent)
            if (p === Overlay.overlay) return true;
        return false;
    }
    readonly property bool _recentKeysOn: root._keysOn && !root.docs && !noteSwitcher.opened && !root._focusInPopup
    readonly property NoteSwitcher switcher: noteSwitcher
    NoteSwitcher {
        id: noteSwitcher
        onChosen: (noteId) => root._openNoteFromKeys(noteId)
    }
    Shortcut {
        objectName: "notes-recent-shortcut"
        sequence: root._kbd("notes.recent")
        context: Qt.WindowShortcut
        enabled: root._recentKeysOn && sequence.length > 0
        onActivated: root.switchRecent(1)
    }
    Shortcut {
        sequence: root._kbd("notes.recentBack")
        context: Qt.WindowShortcut
        enabled: root._recentKeysOn && sequence.length > 0
        onActivated: root.switchRecent(-1)
    }
    Shortcut {
        sequence: root._kbd("notes.close")
        context: Qt.WindowShortcut
        enabled: root._recentKeysOn && sequence.length > 0
        onActivated: root.closeNote()
    }
    Shortcut {
        sequence: root._kbd("notes.rename")
        context: Qt.WindowShortcut
        enabled: root._keysOn && sequence.length > 0 && (AppController.activeNoteId.length > 0 || root.pageId.length > 0)
        onActivated: root.renameShown()
    }
    Shortcut {
        sequence: root._kbd("notes.toggleList")
        context: Qt.WindowShortcut
        enabled: root._keysOn && sequence.length > 0
        onActivated: root.toggleList()
    }
    // A table's rows and columns (dogfood 2026-10-19): only while the note's
    // text has the keyboard, and only in a table; elsewhere the key is the
    // field's own.
    readonly property bool _tableKeysOn: root._keysOn && editor.activeFocus && root.viewMode !== "live"
    Shortcut {
        sequence: root._kbd("notes.table.rowAbove")
        context: Qt.WindowShortcut
        enabled: root._tableKeysOn && sequence.length > 0
        onActivated: root.tableOp("rowAbove")
    }
    Shortcut {
        sequence: root._kbd("notes.table.rowBelow")
        context: Qt.WindowShortcut
        enabled: root._tableKeysOn && sequence.length > 0
        onActivated: root.tableOp("rowBelow")
    }
    Shortcut {
        sequence: root._kbd("notes.table.columnLeft")
        context: Qt.WindowShortcut
        enabled: root._tableKeysOn && sequence.length > 0
        onActivated: root.tableOp("columnLeft")
    }
    Shortcut {
        sequence: root._kbd("notes.table.columnRight")
        context: Qt.WindowShortcut
        enabled: root._tableKeysOn && sequence.length > 0
        onActivated: root.tableOp("columnRight")
    }
    Shortcut {
        sequence: root._kbd("notes.table.deleteRow")
        context: Qt.WindowShortcut
        enabled: root._tableKeysOn && sequence.length > 0
        onActivated: root.tableOp("deleteRow")
    }
    Shortcut {
        sequence: root._kbd("notes.table.deleteColumn")
        context: Qt.WindowShortcut
        enabled: root._tableKeysOn && sequence.length > 0
        onActivated: root.tableOp("deleteColumn")
    }

    // ── Autocomplete popup ───────────────────────────────────────
    Popup {
        PopupArrival { popup: acPopup; dialog: false }
        id: acPopup
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        padding: 0
        // Wide enough for full tracker keys like "ra-TEL-258998" next to a
        // readable title (dogfood 2026-10-23 #3).
        width: Theme.px(440)
        height: Math.min(8, Math.max(1, acMatches.length)) * 38 + 8

        // One key column for every task row: as wide as the longest key.
        readonly property string longestKey: {
            let longest = "";
            for (const m of root.acMatches)
                if (m.kind === "task" && String(m.id).length > longest.length)
                    longest = String(m.id);
            return longest;
        }
        readonly property real keyColW: Math.ceil(acKeyMetrics.advanceWidth) + 2 * Theme.spSm
        TextMetrics {
            id: acKeyMetrics
            text: acPopup.longestKey
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsXs
            font.weight: Theme.fwTitle
        }

        background: PopupSurface {}

        contentItem: ListView {
            id: acList
            anchors.fill: parent
            anchors.margins: Theme.spXs
            clip: true
            interactive: false
            model: acMatches
            spacing: 0
            delegate: Rectangle {
                id: acRow
                required property var modelData
                required property int index
                readonly property bool selected: index === root.acSelected
                width: ListView.view.width
                height: 36
                radius: Theme.radiusSm
                // rowHighlight and a focusRing bar, like a menu row: panel2 on
                // the popup's panel was 1.05:1 (design audit DES-21).
                color: acRow.selected ? Theme.rowHighlight : "transparent"
                Rectangle {
                    visible: acRow.selected
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: parent.height - 2 * Theme.spXs
                    radius: Theme.radiusXs
                    color: Theme.focusRing
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                    spacing: Theme.spMd

                    Rectangle {
                        visible: modelData.kind === "person"
                        Layout.preferredWidth: Theme.px(22)
                        Layout.preferredHeight: Theme.px(22)
                        radius: width / 2
                        color: modelData.color || Theme.accent
                        Text {
                            anchors.centerIn: parent
                            text: {
                                const parts = (modelData.label || "").split(/\s+/);
                                return (parts[0] ? parts[0][0] : "") + (parts[1] ? parts[1][0] : "");
                            }
                            color: Theme.textOnAccent
                            font.family: Theme.fontUi
                            font.features: Theme.tabularNums
                            font.pixelSize: Theme.fsXs
                            font.weight: Theme.fwTitle
                        }
                    }
                    // Layout.* sizes: a plain width is ignored in a RowLayout,
                    // which collapsed the badge and let the key spill over the title.
                    Rectangle {
                        visible: modelData.kind === "task"
                        Layout.preferredWidth: acPopup.keyColW
                        Layout.preferredHeight: Theme.px(18)
                        radius: Theme.radiusSm
                        color: "transparent"
                        border.color: Theme.accent; border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: modelData.id
                            color: Theme.accentStrong
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsXs
                            font.weight: Theme.fwTitle
                        }
                    }
                    Rectangle {
                        visible: modelData.kind === "heading"
                        Layout.preferredWidth: Theme.px(22)
                        Layout.preferredHeight: Theme.px(18)
                        radius: Theme.radiusSm
                        color: "transparent"
                        border.color: Theme.heading; border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "⌗"
                            color: Theme.heading
                            font.pixelSize: Theme.fsSm
                            font.weight: Theme.fwTitle
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: modelData.label
                            color: Theme.text
                            font.pixelSize: Theme.fsMd
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: (modelData.sub || "").length > 0
                            text: modelData.sub || ""
                            color: Theme.textMuted
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsXs
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.acSelected = index
                    onClicked: { root.acSelected = index; root._commitAutocomplete(); }
                }
            }
        }
    }

    // ── Persistence (mirror DocsView's _persisting / _reloading) ────
    property bool _loadedOnce: false
    property bool _persisting: false
    property bool _reloading:  false
    property string _savedAgo: ""

    Timer {
        id: persistTimer
        interval: 250
        repeat: false
        onTriggered: root._persistNow()
    }
    Timer {
        id: savedAgoTimer
        interval: 5000
        repeat: false
        onTriggered: root._savedAgo = ""
    }

    function _scheduleSave() {
        if (!_loadedOnce || _reloading) return;
        persistTimer.restart();
    }
    function _persistNow() {
        if (!_loadedOnce || _reloading) return;
        _persisting = true;
        AppController.notesState = editor.text;
        _persisting = false;
        _savedAgo = I18n.t("notes.saved");
        savedAgoTimer.restart();
    }
    // Following a [[link]] or a #TICKET from the preview.
    //
    // A [[target]] used to mean a heading in this document, because a profile
    // had one document. It now means a note first and a heading second, and
    // when it means neither it is an offer to write the note somebody clearly
    // expected to exist.
    signal taskRequested(string taskId)
    signal personRequested(string personId)
    // "Import Markdown" under an empty list (A4-25): Main owns the picker.
    signal importRequested()

    function _followLink(kind, target) {
        // A ticket in a note is a task: opening it is the whole reason for
        // writing #HEAP-12 rather than the title. MdHtml calls it "task".
        if (kind === "task" || kind === "ticket") {
            root.taskRequested(target);
            return;
        }
        if (kind === "person") {
            const pid = AppController.personIdForHandle(target);
            if (pid.length > 0) root.personRequested(pid);
            return;
        }
        // A #tag lists the notes that carry it.
        if (kind === "tag") {
            notesList.setFilter("#" + target);
            return;
        }
        if (kind !== "note") return;
        const hit = AppController.resolveNoteLink(target);
        // Followed from the keyboard (a link's Tab stop, A4-7): the keyboard
        // goes on to the note opened, not a link of the one left.
        const f = root.Window.activeFocusItem;
        const fromKeys = !!f && f.objectName === "mdLinkStop";
        if (hit.kind === "note") {
            root._flushPending();
            AppController.activeNoteId = hit.noteId;
            if (fromKeys) Qt.callLater(root.takeFocus);
            return;
        }
        if (hit.kind === "heading") {
            // [[Note#Heading]] can name another note; open it first.
            if (hit.noteId && hit.noteId !== AppController.activeNoteId) {
                root._flushPending();
                AppController.activeNoteId = hit.noteId;
            }
            const off = AppController.noteHeadingOffset(editor.text, hit.heading);
            if (off >= 0) root._jumpToOffset(off);
            return;
        }
        // [[APP-101]] with no note of that name is the task of that id.
        const t = AppController.taskById(String(target).trim());
        if (t && t.id) {
            root.taskRequested(t.id);
            return;
        }
        missingLinkPopup.openFor(hit.title);
    }

    property string _loadedNoteId: ""
    function _loadFromController() {
        _reloading = true;
        const next = AppController.notesState || "";
        if (root._loadedNoteId === AppController.activeNoteId && root._loadedNoteId.length > 0 && editor.length > 0) {
            // The same note, changed from outside — a quick-capture append, an
            // import. Replace only what differs, so the caret stays where the
            // reader left it and Ctrl+Z still has the history (and can take the
            // outside change back too). Setting `text` reset both.
            const old = editor.text;
            if (old !== next) {
                let p = 0;
                const max = Math.min(old.length, next.length);
                while (p < max && old.charCodeAt(p) === next.charCodeAt(p)) p++;
                let s = 0;
                while (s < max - p && old.charCodeAt(old.length - 1 - s) === next.charCodeAt(next.length - 1 - s)) s++;
                const caret = editor.cursorPosition;
                if (old.length - s > p) editor.remove(p, old.length - s);
                const inserted = next.substring(p, next.length - s);
                if (inserted.length > 0) editor.insert(p, inserted);
                // Typing at the end of the note stays at the end; anywhere
                // else the caret keeps its place in the text it was in.
                if (caret <= p) editor.cursorPosition = caret;
            }
        } else {
            editor.text = next;
        }
        // "Load images" is consent for the note it was given in, not for the
        // next one: an imported note must not ping its tracker pixel on open
        // just because another note's images were allowed (KNOW-18).
        if (root._loadedNoteId !== AppController.activeNoteId) mdDocument.allowRemoteImages = false;
        root._loadedNoteId = AppController.activeNoteId;
        _reloading = false;
        root._refreshTitle();
        root._stats = AppController.noteStats(editor.text);
        root._wiki = AppController.wikiTargets(editor.text);
        root._refreshLinks();
        root._refreshAttachments();
    }
    // Markdown editing, in C++ so the rules can be tested directly rather
    // than only by driving the UI.
    MarkdownEditorController {
        id: mdEditor
        target: editor.textDocument
        cursorPosition: editor.cursorPosition
        selectionStart: editor.selectionStart
        selectionEnd: editor.selectionEnd
        // Only the editor can move its own cursor, so the controller asks.
        onSelectionRequested: (start, end) => {
            if (start === end) editor.cursorPosition = start;
            else editor.select(start, end);
        }
    }

    // Parses once per change and serves every question about the document:
    // the rendered rows, the outline, and which row a line belongs to.
    MdDocument {
        id: mdDocument
        text: editor.text
        allowRemoteImages: false
        // Relative image paths resolve here; absolute local paths work anywhere.
        imageBaseDir: AppController.dataDir + "/attachments"
        palette: Theme.mdPalette
        ticketTitles: AppController.taskTitles
        // No preview on screen, no parse per pause; lookups still parse.
        live: preview.visible
    }

    // A pending edit is only in the editor until the 250 ms debounce fires.
    // Main.qml swaps views through a Loader, so this item is destroyed on
    // every view switch — without a flush the last keystrokes are lost.
    function _flushPending() {
        liveEditor.flush();
        // The open page's last words too: a profile switch flushes through
        // here (IDIOT-KNOW-1).
        pageEditor.flush();
        if (!persistTimer.running) return;
        persistTimer.stop();
        _persistNow();
    }

    Component.onCompleted: {
        viewMode = _readViewMode();
        _loadFromController();
        _loadedOnce = true;
        // A palette hit that opened Notes for the first time: the binding's
        // first value does not fire the change handler.
        if (jumpToLine >= 0) Qt.callLater(root._consumeJump);
        if (requestedPage.length > 0) Qt.callLater(root._consumePage);
        if (requestedFilter.length > 0) Qt.callLater(root._consumeFilter);
    }
    Component.onDestruction: _flushPending()
    // Quit is not covered by onDestruction: the engine tears down its root
    // objects and the AppController singleton in an unspecified order, and
    // ~AppController's own flushSave() may already have run by then. Flush
    // here, while both are alive, and push the debounced state.json write.
    Connections {
        target: Qt.application
        function onAboutToQuit() {
            root._flushPending();
            AppController.flushSave();
        }
    }
    // A broken link is usually a note somebody meant to write, so the offer to
    // write it is the useful thing to do with the click.
    Dialog {
        PopupArrival { popup: missingLinkPopup }
        id: missingLinkPopup
        objectName: "missing-link"
        property string wanted: ""
        modal: true
        Overlay.modal: ModalScrim {}
        anchors.centerIn: Overlay.overlay
        parent: Overlay.overlay
        padding: Theme.inset
        width: 400
        title: I18n.t("notes.link.missingTitle")

        function openFor(target) {
            missingLinkPopup.wanted = target;
            missingLinkPopup.open();
            missingBody.forceActiveFocus();
        }
        function create() {
            missingLinkPopup.close();
            root._flushPending();
            AppController.createNoteForLink(missingLinkPopup.wanted);
        }

        background: ModalSurface {}

        // Enter writes the note, as the primary button says; it needed the
        // mouse (KNOW-9).
        contentItem: Text {
            id: missingBody
            focus: true
            Keys.onReturnPressed: missingLinkPopup.create()
            Keys.onEnterPressed: missingLinkPopup.create()
            text: I18n.t("notes.link.missingBody").arg(missingLinkPopup.wanted)
            color: Theme.textMuted
            font.pixelSize: Theme.fsMd
            wrapMode: Text.Wrap
        }

        footer: RowLayout {
            spacing: Theme.spMd
            Layout.margins: Theme.sp2xl
            Item { Layout.fillWidth: true }
            PillButton { text: I18n.t("common.cancel"); onClicked: missingLinkPopup.close() }
            PillButton {
                objectName: "missing-link-create"
                text: I18n.t("notes.link.create")
                primary: true
                onClicked: missingLinkPopup.create()
            }
        }
    }

    Connections {
        target: AppController
        // Before the active note changes, while the old one is still active:
        // the debounced keystrokes go into the note they were typed in, not the
        // one about to be opened. Without this a "+" pressed inside the 250 ms
        // window moved the draft into the new note and emptied the old one.
        function onAboutToChangeActiveNote() {
            root._rememberCaret();
            root._flushPending();
        }
        // An export or a search is about to read the whole profile.
        function onFlushEditorsRequested() { root._flushPending() }
        function onNotesStateChanged() {
            if (!root._loadedOnce || root._persisting) return;
            root._loadFromController();
            root._hideAutocomplete();
        }
    }
}
