pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as QQC
import QtQuick.Dialogs
import TodoCpp

// A markdown document edited where it is drawn (heap 2, APP-265/269): the
// whole document is rendered (MdView), and the block under the caret turns
// into a field holding exactly its source lines. Leave the block and it is
// drawn again; ↑ on its first line / ↓ on its last go to the block next to
// it. No Text / Preview tabs.
//
// One source of truth: a hidden text area holds the whole source, so a
// checkbox click, a block's edit and Ctrl+Z all go through one undo stack.
Item {
    id: root

    // The source. Setting it from outside replaces the document (a different
    // task, a pull from the tracker) and drops the undo history.
    property string text: ""
    property string placeholder: ""
    // The context line of the field's right-click menu (DG-152).
    property string menuTitle: ""
    property bool readOnly: false
    // How [[targets]] read (APP-269): AppController.wikiTargets(text).
    property var wikiTargets: ({})
    property bool headingRules: true
    // The Knowledge document type (MdView.noteType).
    property bool noteType: false
    // The drawn note's text column and code / table column (MdView), in
    // pixels; 0 keeps MdView's own.
    property int textColumn: 0
    property int wideColumn: 0
    property real paragraphLineHeight: 0
    // The whole source opens as one field on a click anywhere in it, the
    // caret on the clicked line (the task document): block by block, the
    // text opened in pieces and read as not editable.
    property bool wholeDocument: false
    // With wholeDocument: the note's rich mode (APP-313) - the source is
    // always the one field, its markup hidden away from the caret and drawn
    // over (MdLivePreview), so selecting, Shift+arrows and copying run
    // across lines as in Notes. Read-only, it is text to read and copy.
    property bool rich: false
    // Rich: the ticket the text is of, so a picture in it opens with the
    // ticket's other pictures (MdLivePreview.imageTaskId).
    property string imageTaskId: ""
    // The ticket's pictures as its texts show them, for the viewer's
    // order (ImageGallery.ordered): the description's, then the comments'.
    property var imageOrder: []
    // Room under the text to click for a new line; off for a short text in
    // a box (the tracker's).
    property bool tailRoom: true
    readonly property real contentHeight: view.contentHeight
    // Off, the text does not scroll inside itself: it is as tall as it is
    // drawn and a scroll around it moves it (the task's one scroll,
    // dogfood 2026-10-23 Heavy sheet), the wheel passes through.
    property bool scrolls: true
    // The picture viewer the drawn text opens (a thumbnail beside it).
    // Rich, it opens with the text's (or the ticket's) other pictures, a
    // ← / → away (owner, 2026-10-23: a thumbnail opened the one picture).
    function openImage(attId, name) {
        const lp = livePreviewLoader.item as MdLivePreview;
        if (lp && lp.openImage("attachments/" + attId)) return;
        view.openImage(attId, name);
    }
    // Back to the first line (a document reopened on the same text).
    function scrollToTop() { view.positionViewAtBeginning(); }
    // Where the text is scrolled to, and back there (Settings and back,
    // dogfood 2026-10-22 #4).
    function scrollPos() { return view.contentY - view.originY; }
    function setScrollPos(y) {
        view.contentY = view.originY + Math.max(0, Math.min(y, view.contentHeight - view.height));
    }
    // The scroll bar shows at rest while the text runs past the box (a
    // short box of read-only text, where nothing else says there is more).
    property bool scrollCue: false

    // A printable key on the drawn document opens the block the caret was
    // last in and is typed there (the Knowledge editors).
    property bool typeToEdit: false
    // Rich: the caret drawn dimmed where the text would go while the drawn
    // document holds the keyboard, so the text reads as one to type into
    // (a task's notes, dogfood 2026-10-16).
    property bool ghostCaret: false
    // A key of the open block, offered to the caller first (an autocomplete
    // list over it): accepting the event takes it.
    signal blockKey(var event)
    // Ctrl+V with a file or an image on the clipboard stores and links it.
    property bool attachOnPaste: false
    // Drawn under the last block, inside the scroll (the task document's
    // plan and hint, DG-062): the item gets `width` set to the text column.
    property Component tail: null
    readonly property Item tailItem: view.footerItem ? (view.footerItem as MdTailHost).loaded : null
    // The edited source, as each edit is taken in.
    signal edited(string text)
    // Esc with no block open: the caller goes back.
    signal escaped()
    signal internalLinkActivated(string kind, string target)

    readonly property bool editing: view.editFirst >= 0
    readonly property alias document: doc
    readonly property alias source: src
    readonly property alias editor: field

    property bool _loading: false
    onTextChanged: {
        if (root.text === src.text || root.text === root.rawText) return;
        root._leave(false);
        root._lastLine = -1;
        root._setSource(root.text);
    }
    Component.onCompleted: root._setSource(root.text)

    // The source as it was given, with only the edits applied (KNOW-5).
    // The text area normalises what it holds — CRLF to LF, a no-break space
    // to a space, U+2028 to a line break — and handing its text back
    // rewrote a whole document for a one-letter edit. `edited` carries this.
    property string rawText: ""
    property string _rawLF: ""
    property bool _crlf: false
    property string _srcSeen: ""
    function _setSource(t) {
        root._crlf = t.indexOf("\r\n") >= 0;
        root._rawLF = root._crlf ? t.replace(/\r\n/g, "\n") : t;
        root._loading = true;
        src.text = root._rawLF;
        root._loading = false;
        root._srcSeen = src.text;
        // Positions map one to one only when the normalising kept lengths.
        if (root._srcSeen.length !== root._rawLF.length) root._rawLF = root._srcSeen;
        root.rawText = root._crlf ? root._rawLF.replace(/\n/g, "\r\n") : root._rawLF;
        root._openRich();
    }
    readonly property bool _isRich: root.rich && root.wholeDocument
    on_IsRichChanged: if (root._isRich) root._openRich()
    // Rich: the whole source in the field, without taking the keyboard.
    function _openRich() {
        if (!root._isRich) return;
        const t = src.text;
        const caret = field.cursorPosition;
        view.editRow = view.count > 0 ? 0 : -1;
        view.editFirst = 0;
        view.editLast = Math.max(0, root._lineCount(t) - 1);
        root._fieldLoading = true;
        field.text = t;
        root._fieldLoading = false;
        field.cursorPosition = Math.min(caret, field.length);
    }
    // Rich: the keyboard is on the drawn text, not in the field (which is
    // always there) - after Esc, a tick, a click beside it.
    readonly property bool _drawnFocus: !root.editing || (root._isRich && !field.activeFocus)
    // The field's own edits going into the source, as opposed to an undo or
    // a tick that changed the source under it.
    property bool _committing: false
    // An edit of the text area, spliced into the raw source: only the span
    // that differs, the rest kept as it was.
    function _takeEdit() {
        const o = root._srcSeen, n = src.text;
        let p = 0;
        const max = Math.min(o.length, n.length);
        while (p < max && o.charCodeAt(p) === n.charCodeAt(p)) p++;
        let sfx = 0;
        while (sfx < max - p && o.charCodeAt(o.length - 1 - sfx) === n.charCodeAt(n.length - 1 - sfx)) sfx++;
        root._rawLF = root._rawLF.substring(0, p) + n.substring(p, n.length - sfx) + root._rawLF.substring(o.length - sfx);
        root._srcSeen = n;
        root.rawText = root._crlf ? root._rawLF.replace(/\n/g, "\r\n") : root._rawLF;
    }

    // Another text in: a document switched under the editor. Assigning
    // `text` changed nothing when the new text equalled the old *loaded*
    // one, so the edits of the last document stayed in the editor and were
    // saved into the next (IDIOT-DOC-1).
    function load(t) {
        root._leave(false);
        root._lastLine = -1;
        root._setSource(t);
        root.text = t;
    }
    // Take what is in the open block now (before a save, a switch).
    function flush() { root._commit(); }
    // The caret into the document: the block it was last in, else the
    // first block, or a new one if empty. Not into a hidden editor: Return
    // there wrote into a note nobody could see (IDIOT-KNOW-2).
    function focusEditor() {
        if (!root.visible) return;
        if (root.editing) { field.forceActiveFocus(); return; }
        root.editLine(root._lastLine >= 0 ? root._lastLine : 0, false);
    }
    // The keyboard on the drawn document, no block open: what arriving on a
    // screen does, as on every other view. The keymap's chords are keys here
    // and Return opens the text; g n, g t typed "gt" into the note (A4-4).
    // An empty document has nothing to read: the caret goes in.
    function focusDocument() {
        if (!root.visible) return;
        if (!root.readOnly && src.text.trim().length === 0) {
            root.focusEnd();
            return;
        }
        root._leave(true);
        root._holdView();
    }
    // The view is a focus scope: it handed the keyboard back to the stop
    // that had it last (a [[link]] followed with Return, the code block's
    // Copy), where g t typed "gt" into the note and Esc did nothing
    // (VB5-R1). The stop lets go, so the document itself holds it.
    function _holdView() {
        view.forceActiveFocus();
        for (let i = 0; i < 4 && root._onStop(); i++) {
            const f = root.Window.activeFocusItem;
            if (!f) break;
            f.focus = false;
            view.forceActiveFocus();
        }
    }
    // The keyboard is on a Tab stop drawn in the document (a link, a box,
    // Copy), not on the document itself.
    function _onStop() {
        for (let p = root.Window.activeFocusItem; p && p !== view; p = p.parent)
            if (p.activeFocusOnTab === true) return true;
        return false;
    }
    // The drawn document holds the keyboard (not a block, not a stop on it).
    readonly property bool documentFocused: view.activeFocus && root._drawnFocus && !root._onStop()

    // Deleting a block from the keyboard (dogfood 2026-10-19): Ctrl+Shift+K
    // in the Classic keymap while typing, d d in Vim on the drawn document
    // (the block the caret was last in). Main asks here first.
    readonly property var claimedKeys: ["editor.deleteBlock"]
    function claimLive(id) { return id === "editor.deleteBlock" && root._blockToDelete() !== null; }
    function runClaimed(id) { return id === "editor.deleteBlock" && root.deleteCurrentBlock(); }
    // The lines of the block a delete takes ({first, last} in the source),
    // or null: nothing open and nothing edited yet, or read-only text.
    function _blockToDelete() {
        if (root.readOnly || !root.visible) return null;
        const t = root._isRich ? field.text : src.text;
        if (t.length === 0) return null;
        if (root._isRich || root.wholeDocument) {
            // One field holds the text: the paragraph around the caret, as
            // far as the blank lines on either side.
            const lines = t.split("\n");
            let at = t.substring(0, Math.min(field.cursorPosition, t.length)).split("\n").length - 1;
            if (lines[at].trim().length === 0) return null;
            let first = at, last = at;
            while (first > 0 && lines[first - 1].trim().length > 0) first--;
            while (last < lines.length - 1 && lines[last + 1].trim().length > 0) last++;
            return { first: first, last: last, single: true };
        }
        if (root.editing) return { first: view.editFirst, last: view.editLast, single: false };
        if (root._lastLine < 0) return null;
        const row = doc.rowForLine(Math.min(root._lastLine, root._lineCount(t) - 1));
        if (row < 0) return null;
        return { first: doc.firstLineOfRow(row), last: doc.lastLineOfRow(row), single: false };
    }
    // Takes the block out (one undo step); the keyboard stays where it was,
    // on the block that took its place. False when there is nothing to take.
    function deleteCurrentBlock() {
        const wasEditing = root.editing && !root._isRich;
        if (wasEditing) root._commit();
        const b = root._blockToDelete();
        if (!b) return false;
        const t = b.single ? field.text : src.text;
        const total = root._lineCount(t);
        let s = root._lineStart(t, b.first);
        let e = root._lineEnd(t, b.last);
        // With its line break, and the blank lines that kept it apart from
        // the block below; the last block takes the break before it.
        if (b.last < total - 1) {
            let next = b.last + 1;
            while (next < total - 1 && t.substring(root._lineStart(t, next), root._lineEnd(t, next)).trim().length === 0) next++;
            e = root._lineStart(t, next);
        } else if (b.first > 0) {
            s = root._lineEnd(t, b.first - 1);
        }
        if (b.single) {
            field.remove(s, e);
            field.cursorPosition = Math.min(s, field.length);
            return true;
        }
        if (wasEditing) root._leave(false);
        if (!doc.replaceRange(src.textDocument, s, e, "")) src.remove(s, e);
        doc.flush();
        const line = Math.max(0, Math.min(b.first, root._lineCount(src.text) - 1));
        root._lastLine = line;
        if (wasEditing) root.editLine(line, true);
        else root._holdView();
        return true;
    }
    // Its own undo, for the keymap's bare "u" on the drawn document.
    readonly property bool canUndoHere: !root.readOnly && src.canUndo
    function undoHere() {
        if (!root.canUndoHere) return false;
        root._leave(true);
        src.undo();
        doc.flush();
        return true;
    }
    // The caret back where it was, else at the end of the text. A document
    // that ends on its heading gets a line under it: the caret at the end of
    // "# Title" made the first words typed part of the title (IDIOT-KNOW-6/9).
    function focusEnd() {
        if (!root.visible) return;
        if (root.editing) { field.forceActiveFocus(); return; }
        if (root._lastLine >= 0) { root.editLine(root._lastLine, false); return; }
        const t = src.text;
        let line = root._lineCount(t) - 1;
        while (line > 0 && t.substring(root._lineStart(t, line), root._lineEnd(t, line)).trim().length === 0) line--;
        const last = t.substring(root._lineStart(t, line), root._lineEnd(t, line));
        if (last.trim().length === 0 || /^#{1,6}\s/.test(last)) root.appendBlock();
        else root.editLine(line, false);
    }
    // Rich: the caret to the end of the text without taking the keyboard,
    // where Return or a click beside the text picks it up.
    function caretToEnd() {
        if (root._isRich) field.cursorPosition = field.length;
    }
    // A new document: its heading open with the title selected, so what is
    // typed names it; "/" or ↓ go to the line under it (PERSONA-20).
    function editTitle() {
        if (!root.visible) return;
        root.editLine(0, false);
        const m = /^(#{1,6}\s+)(.*)$/.exec(field.text.split("\n")[0]);
        if (m && m[2].length > 0) field.select(m[1].length, m[1].length + m[2].length);
    }
    // The heading's title is what is selected (editTitle's state).
    function _titleSelected() {
        if (view.editFirst !== 0 || field.selectionStart === field.selectionEnd) return false;
        const m = /^(#{1,6}\s+)(.*)$/.exec(field.text.split("\n")[0]);
        return !!m && field.selectionStart === m[1].length && field.selectionEnd === m[1].length + m[2].length;
    }
    // Enter inside a [[link]] being typed closes it instead of breaking the
    // line inside the brackets; a task's title becomes its id, as no note
    // answers to it (PERSONA-21). False when the caret is not in one.
    function _completeLink() {
        const pos = field.cursorPosition;
        const t = field.text;
        const lineStart = t.lastIndexOf("\n", pos - 1) + 1;
        const before = t.substring(lineStart, pos);
        const open = before.lastIndexOf("[[");
        if (open < 0 || before.indexOf("]]", open) >= 0) return false;
        const lineEnd = t.indexOf("\n", pos) < 0 ? t.length : t.indexOf("\n", pos);
        const closeAt = t.substring(pos, lineEnd).indexOf("]]");
        const qStart = lineStart + open + 2;
        const qEnd = closeAt >= 0 ? pos + closeAt : pos;
        const query = t.substring(qStart, qEnd).trim();
        let name = query;
        if (query.length > 0 && AppController.resolveNoteLink(query).kind === "missing") {
            const hit = AppController.matchTasks(query, 1);
            if (hit.length > 0) name = hit[0].id;
        }
        field.remove(qStart, closeAt >= 0 ? qEnd + 2 : qEnd);
        field.insert(qStart, name + "]]");
        field.cursorPosition = qStart + name.length + 2;
        return true;
    }

    // ── source helpers ──
    function _lineStart(t, line) {
        let pos = 0;
        for (let i = 0; i < line; i++) {
            const n = t.indexOf("\n", pos);
            if (n < 0) return t.length;
            pos = n + 1;
        }
        return pos;
    }
    function _lineEnd(t, line) {
        const s = root._lineStart(t, line);
        const n = t.indexOf("\n", s);
        return n < 0 ? t.length : n;
    }
    function _lineCount(t) { return t.length === 0 ? 1 : t.split("\n").length; }

    // The row a source line belongs to. A blank line after the last block
    // (a click below the text) is a new block of its own, not the paragraph
    // above: it opened that paragraph, so a "/" typed there landed in the
    // paragraph's box (R3-092). -1 = after the last row.
    function _rowFor(t, line) {
        const row = doc.rowForLine(line);
        if (row < 0 || row !== view.count - 1 || line <= doc.lastLineOfRow(row)) return row;
        return t.substring(root._lineStart(t, line), root._lineEnd(t, line)).trim().length === 0 ? -1 : row;
    }
    // Open the block that holds `line` for editing; the caret at its end, or
    // at its start with `atStart`.
    function editLine(line, atStart, hit) {
        if (root.readOnly || !root.visible) return;
        root._commit();
        const t = src.text;
        const last = Math.max(0, root._lineCount(t) - 1);
        line = Math.max(0, Math.min(line, last));
        const row = root.wholeDocument ? (view.count > 0 ? 0 : -1) : root._rowFor(t, line);
        let first = line, lastLine = line;
        if (root.wholeDocument) {
            first = 0;
            lastLine = last;
        } else if (row >= 0) {
            first = Math.max(0, doc.firstLineOfRow(row));
            lastLine = Math.max(first, doc.lastLineOfRow(row));
        }
        // Not the blank lines after the block: they part it from the next.
        while (lastLine > first && t.substring(root._lineStart(t, lastLine), root._lineEnd(t, lastLine)).trim().length === 0)
            lastLine--;
        root._lastLine = line;
        view.editRow = row;
        view.editFirst = first;
        view.editLast = lastLine;
        root._fieldLoading = true;
        field.text = t.substring(root._lineStart(t, first), root._lineEnd(t, lastLine));
        root._fieldLoading = false;
        // The caret where the click was, not at the end of the block.
        const off = hit ? root._offsetFor(hit, field.text) : -1;
        if (off >= 0)
            field.cursorPosition = Math.min(off, field.length);
        else if (root.wholeDocument)
            field.cursorPosition = atStart ? root._lineStart(t, line) : root._lineEnd(t, line);
        else
            field.cursorPosition = atStart ? 0 : field.length;
        field.forceActiveFocus();
        Qt.callLater(root._reveal);
    }
    // Where a click on the drawn text lands in the block's source: the
    // drawn text around the click, found once in the source; else the drawn
    // text up to the click walked through the source (the markup it skips is
    // what the drawing left out). -1 when there is no click to go by.
    function _offsetFor(hit: var, t: string): int {
        if (!hit || typeof hit.plain !== "string" || hit.pos < 0) return -1;
        // A table cell: its row and column in the source, then the place in
        // the cell — "1" is in many cells, so the text alone cannot say which.
        if (hit.cell >= 0 && hit.columns > 0) {
            const lines = t.split("\n");
            const row = Math.floor(hit.cell / hit.columns);
            const col = hit.cell % hit.columns;
            const li = row === 0 ? 0 : row + 1;   // under the header, the |---| line
            if (li < lines.length) {
                const line = lines[li];
                let at = 0;
                for (let i = 0; i < li; i++) at += lines[i].length + 1;
                // Cells are what the unescaped pipes part; a leading pipe opens none.
                const bars = [];
                for (let i = 0; i < line.length; i++)
                    if (line.charAt(i) === "|" && (i === 0 || line.charAt(i - 1) !== "\\")) bars.push(i);
                const lead = /^\s*\|/.test(line) ? 1 : 0;
                const from = col + lead - 1 >= 0 && col + lead - 1 < bars.length ? bars[col + lead - 1] + 1 : 0;
                const to = col + lead < bars.length ? bars[col + lead] : line.length;
                const cell = line.substring(from, to);
                const pad = cell.length - cell.replace(/^\s+/, "").length;
                return at + from + Math.min(pad + hit.pos, Math.max(pad, cell.replace(/\s+$/, "").length));
            }
        }
        const plain = hit.plain.replace(/[\u2028\u2029]/g, "\n");
        const pos = Math.min(hit.pos, plain.length);
        // The drawn text around the click, found once in the source: the
        // longest piece first, down to two characters either side of it.
        for (let k = Math.min(pos, 24); k >= 0; k = k > 3 ? Math.floor(k / 2) : k - 1) {
            for (let m = Math.min(plain.length - pos, 16); m >= 0; m = m > 3 ? Math.floor(m / 2) : m - 1) {
                if (k + m < 2) continue;
                const frag = plain.substring(pos - k, pos + m);
                const at = t.indexOf(frag);
                if (at >= 0 && t.indexOf(frag, at + 1) < 0) return at + k;
            }
        }
        // Not once anywhere: the drawn text up to the click walked through
        // the source (the markup it skips is what the drawing left out).
        let j = 0;
        for (let i = 0; i < pos && j < t.length; i++) {
            const n = t.indexOf(plain.charAt(i), j);
            if (n >= 0 && n - j < 200) j = n + 1;
        }
        return j;
    }
    // The open block grown to lines first..last, the selection kept on the
    // same source text (offsets into the whole source): Shift+click,
    // Shift+Up/Down at its edge and a second Ctrl+A select across blocks,
    // which one block at a time never could (nothing past a block could be
    // selected or copied).
    function _extendTo(first: int, last: int, selFrom: int, selTo: int) {
        if (!root.editing) return;
        root._commit();
        const t = src.text;
        first = Math.max(0, Math.min(first, view.editFirst));
        last = Math.min(root._lineCount(t) - 1, Math.max(last, view.editLast));
        while (last > first && t.substring(root._lineStart(t, last), root._lineEnd(t, last)).trim().length === 0) last--;
        const s0 = root._lineStart(t, first);
        view.editRow = root._rowFor(t, first);
        view.editFirst = first;
        view.editLast = last;
        root._fieldLoading = true;
        field.text = t.substring(s0, root._lineEnd(t, last));
        root._fieldLoading = false;
        const a = Math.max(0, Math.min(field.length, selFrom - s0));
        const b = Math.max(0, Math.min(field.length, selTo - s0));
        // select(start, end) puts the caret at `end`: the side being moved.
        if (a === b) field.cursorPosition = a;
        else field.select(a, b);
        field.forceActiveFocus();
    }
    // A Shift+click on another row: the open block grows to it, selected
    // from where the caret was to the click.
    function extendToRow(row: int, hit: var): bool {
        if (!root.editing || row < 0) return false;
        // The row came from the drawn rows; the open block's edits are in by
        // now only if they were taken in (the timer may still be running).
        root._commit();
        const t = src.text;
        const first = doc.firstLineOfRow(row), last = doc.lastLineOfRow(row);
        const anchor = root._lineStart(t, view.editFirst) + field.cursorPosition;
        const rowText = t.substring(root._lineStart(t, first), root._lineEnd(t, last));
        const off = root._offsetFor(hit, rowText);
        const target = root._lineStart(t, first) + (off >= 0 ? off : (first < view.editFirst ? 0 : rowText.length));
        root._extendTo(first, last, anchor, target);
        return true;
    }
    function editRowAt(row, atStart) {
        if (row < 0 || row >= view.count) return false;
        root.editLine(doc.firstLineOfRow(row), atStart);
        return true;
    }
    // Close the open block; with `commit`, take its text in first.
    function _leave(commit) {
        if (!root.editing) return;
        if (commit) root._commit();
        // Rich keeps its one field: leaving only takes the edits in.
        if (root._isRich) return;
        commitTimer.stop();
        view.editFirst = -1;
        view.editLast = -1;
        view.editRow = -1;
    }
    property bool _fieldLoading: false
    // The line the caret was last on, opened again when the keyboard comes
    // back from a dialog or the list (IDIOT-KNOW-6). -1: none in this text.
    property int _lastLine: -1
    // The open block's text into the source, replacing its lines.
    function _commit() {
        commitTimer.stop();
        if (!root.editing) return;
        const t = src.text;
        const s = root._lineStart(t, view.editFirst);
        const e = root._lineEnd(t, view.editLast);
        // One edit, one undo step: as a remove and an insert, Ctrl+Z took
        // back only the insert and saved the block blank (KNOW-1).
        root._committing = true;
        if (t.substring(s, e) !== field.text && !doc.replaceRange(src.textDocument, s, e, field.text)) {
            src.remove(s, e);
            src.insert(s, field.text);
        }
        root._committing = false;
        view.editLast = view.editFirst + root._lineCount(field.text) - 1;
        doc.flush();
        view.editRow = root.wholeDocument ? (view.count > 0 ? 0 : -1) : root._rowFor(src.text, view.editFirst);
    }
    function _reveal() {
        // Not scrolling itself: the scroll around it keeps the caret in view.
        if (!root.scrolls) return;
        const y = field._contentY;
        if (y < view.contentY) view.contentY = Math.max(0, y - Theme.spLg);
        else if (y + field.height > view.contentY + view.height)
            view.contentY = y + field.height - view.height + Theme.spLg;
    }
    // The "/" typed at the caret and the insert menu over it.
    function openSlashMenu() {
        if (root.readOnly || !root.editing) return;
        slashMenu.openAtCaret();
    }
    // Links to stored files, each in a paragraph of its own: at the caret of
    // the open block, else in a new block at the end. Dropped files went to
    // the top of the note, above its title (KNOW-7).
    function insertRefs(refs) {
        if (root.readOnly || refs.length === 0) return;
        if (!root.editing) root.appendBlock();
        if (!root.editing) return;
        const pos = field.cursorPosition;
        const t = field.text;
        let before = "";
        if (pos > 0 && t.charAt(pos - 1) !== "\n") before = "\n\n";
        else if (pos > 1 && t.charAt(pos - 2) !== "\n") before = "\n";
        const after = pos < t.length && t.charAt(pos) !== "\n" ? "\n\n" : "";
        field.insert(pos, before + refs + after);
        field.cursorPosition = pos + before.length + refs.length;
    }
    // A new block after the last one (a click below the text).
    function appendBlock() {
        if (root.readOnly) return;
        root._commit();
        root._leave(false);
        const t = src.text;
        if (t.length > 0 && t.trim().length > 0) {
            const tail = t.endsWith("\n\n") ? "" : (t.endsWith("\n") ? "\n" : "\n\n");
            src.insert(t.length, tail);
        }
        doc.flush();
        root.editLine(root._lineCount(src.text) - 1, false);
    }

    // Rich: the first drawn row holds the field's place once there is one.
    Connections {
        target: view
        enabled: root._isRich
        function onCountChanged() { if (root.editing) view.editRow = view.count > 0 ? 0 : -1; }
    }

    Timer {
        id: commitTimer
        interval: 400
        onTriggered: root._commit()
    }

    // The whole source; its undo stack is the document's.
    QQC.TextArea {
        id: src
        visible: false
        textFormat: TextEdit.PlainText
        onTextChanged: {
            if (root._loading) return;
            root._takeEdit();
            root.edited(root.rawText);
            // Rich: the source changed under the field (an undo, a tick).
            if (root._isRich && !root._committing) root._openRich();
        }
    }

    MdDocument {
        id: doc
        text: src.text
        allowRemoteImages: false
        imageBaseDir: AppController.dataDir + "/attachments"
        palette: Theme.mdPalette
        ticketTitles: AppController.taskTitles
        wikiTargets: root.wikiTargets
    }

    MdView {
        id: view
        objectName: "md-block-view"
        anchors.fill: parent
        interactive: root.scrolls
        headingRules: root.headingRules
        scrollCue: root.scrollCue
        noteType: root.noteType
        textColumn: root.textColumn
        wideColumn: root.wideColumn
        paragraphLineHeight: root.paragraphLineHeight
        document: doc
        editorDocument: src.textDocument
        clickToEdit: !root.readOnly
        editHeight: field.implicitHeight
        onRowClicked: (row, line, hit, extend) => {
            if (extend && root.extendToRow(row, hit)) return;
            root.editLine(line, false, hit);
        }
        onSourceRequested: (line) => root.editLine(line, false)
        onInternalLinkActivated: (kind, target) => root.internalLinkActivated(kind, target)

        // Room to click below the last block: a new block there.
        footer: MdTailHost {
            id: tailHost
            width: view.width
            height: tailLoader.y + tailLoader.height + (root.tailRoom ? Theme.px(160) : 0)
            loaded: tailLoader.item as Item
            Loader {
                id: tailLoader
                x: view.sideMargin
                y: placeholderText.visible ? placeholderText.y + placeholderText.height + Theme.spLg : Theme.spSm
                width: view.width - 2 * view.sideMargin
                active: root.tail !== null
                sourceComponent: root.tail
            }
            Text {
                id: placeholderText
                objectName: "md-block-placeholder"
                visible: src.text.trim().length === 0 && !root.editing
                x: view.sideMargin
                y: Theme.spSm
                width: view.width - 2 * view.sideMargin
                text: root.placeholder
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
            }
            // A click above the tail (the placeholder) or below it: a new
            // block. The tail's own rows take their clicks.
            Item {
                width: parent.width
                height: tailLoader.y
                TapHandler {
                    enabled: !root.readOnly
                    onTapped: root.appendBlock()
                }
            }
            Item {
                y: tailLoader.y + tailLoader.height
                width: parent.width
                height: parent.height - y
                TapHandler {
                    enabled: !root.readOnly
                    onTapped: root.appendBlock()
                }
            }
        }

        // The open block's source, laid where the block was drawn.
        QQC.TextArea {
            id: field
            objectName: "md-block-field"
            // It takes the caret on arrival now (IDIOT-KNOW-6): a named stop.
            Accessible.name: root.menuTitle.length > 0 ? root.menuTitle : root.placeholder
            // Over the view, not inside it: inside, the ListView's focus
            // scope handed the keyboard to its current row on every relayout.
            parent: root
            z: 2
            visible: root.editing
            // The row that holds the block's place, as it reports itself.
            readonly property Item _host: view.editRow >= 0 && view.editHostRow === view.editRow ? view.editHostItem : null
            // The whole document opens at its top; the host row can be
            // missing while the rows are rebuilt, and the field fell to the end.
            readonly property real _contentY: root.wholeDocument ? 0 : _host ? _host.y : (view.count > 0 && view.itemAtIndex(view.count - 1)
                                  ? view.itemAtIndex(view.count - 1).y + view.itemAtIndex(view.count - 1).height : 0)
            // One block open: the field sits where that block is drawn, at its
            // quote and list depth, in its own face — a nested item opened at
            // the left edge, code in the UI font. Several blocks: the column.
            readonly property bool _single: !root.wholeDocument && _host !== null && view.editLast <= doc.lastLineOfRow(view.editRow)
            readonly property real _indentX: _single ? view.editHostIndent : 0
            x: view.x + view.sideMargin - leftPadding + _indentX
            y: view.y + view.contentItem.y + _contentY
            width: (root.noteType ? Math.min(view.width - 2 * view.sideMargin, view.textColumn > 0 ? view.textColumn : Theme.px(680)) : view.width - 2 * view.sideMargin) + leftPadding + rightPadding - _indentX
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            readOnly: root.readOnly
            // Rich: the field is the empty document too, so it says what goes there.
            placeholderText: root._isRich ? root.placeholder : ""
            placeholderTextColor: Theme.textDim
            textFormat: TextEdit.PlainText
            color: Theme.text
            cursorDelegate: TextCaret {
                field: field
                ghost: root.ghostCaret && root._isRich && !root.readOnly && !field.activeFocus
            }
            font.family: _single && view.editHostCode ? Theme.fontMono : Theme.fontUi
            // One editor, no modes (N/X-Oth-Knowledge, R3-092): the source
            // is typed where the block was drawn, in the block's own face,
            // with no box around it.
            font.pixelSize: root.noteType ? Theme.fsLg : Theme.fsMd
            topPadding: root.noteType ? 0 : Theme.spXs
            bottomPadding: root.noteType ? Theme.px(14) : Theme.spXs
            leftPadding: root.noteType ? 0 : Theme.spSm
            rightPadding: root.noteType ? 0 : Theme.spSm
            background: Rectangle {
                radius: Theme.radiusSm
                color: root.noteType ? "transparent" : Theme.panel
                border.color: Theme.border
                border.width: root.noteType ? 0 : 1
            }
            // In a table, the menu has its rows and columns too (dogfood
            // 2026-10-20: the task document's tables had no way to grow).
            QQC.ContextMenu.menu: TextEditMenu {
                id: fieldMenu
                editor: field
                context: root.menuTitle
                taskLink: true
                onAboutToShow: fieldMenu.inTable = field.length > 0 && mdEditor.tableAnchor(field.cursorPosition) === field.cursorPosition
                onTableAction: (op) => {
                    mdEditor.setSelection(field.selectionStart, field.selectionEnd);
                    mdEditor.tableOp(op);
                    field.forceActiveFocus();
                }
            }
            // A right-click puts the caret where it was made (with nothing
            // selected), so the menu's table rows are about the cell under it.
            PointHandler {
                acceptedButtons: Qt.RightButton
                onActiveChanged: {
                    if (active && field.selectionStart === field.selectionEnd)
                        field.cursorPosition = field.positionAt(point.position.x, point.position.y);
                }
            }
            onTextChanged: if (!root._fieldLoading && root.editing) commitTimer.restart()
            // The whole document's field lies over the view, not in it: the
            // wheel over the text scrolls the view under it, or it went
            // nowhere. ScrollFeel reads this name.
            readonly property Item wheelScrollTarget: root.wholeDocument ? view : null
            // Not while the "/" menu or its file picker holds the keyboard.
            onActiveFocusChanged: if (!activeFocus && !slashMenu.opened && slashMenu.slashAt < 0) root._leave(true)

            Keys.priority: Keys.BeforeItem
            Keys.onShortcutOverride: (event) => {
                if (!root.readOnly && mdEditor.claimsShortcut(event.key, event.modifiers)) event.accepted = true;
            }
            Keys.onPressed: (event) => {
                // The caller's list over the caret (Notes' @ # [[) first.
                event.accepted = false;
                root.blockKey(event);
                if (event.accepted) return;
                const mods = event.modifiers & ~Qt.KeypadModifier;
                // Read-only text: moving, selecting and copying are the text
                // area's own; Esc lets go of it. Nothing below may edit.
                if (root.readOnly) {
                    if (event.key === Qt.Key_Escape && mods === Qt.NoModifier) {
                        root._holdView();
                        event.accepted = true;
                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && mods === Qt.NoModifier
                               && livePreviewLoader.item && (livePreviewLoader.item as MdLivePreview).openImageAtCaret()) {
                        // The caret on a picture: Enter opens it whole.
                        event.accepted = true;
                    }
                    return;
                }
                // Ctrl+Z past what the block itself can undo: the document's.
                // Past the document's too: the app's (a deleted note or page,
                // which the toast offers Ctrl+Z for, IDIOT-KNOW-10).
                if (event.matches(StandardKey.Undo) && !field.canUndo) {
                    root._commit();
                    if (src.canUndo) {
                        const line = view.editFirst;
                        root._leave(false);
                        src.undo();
                        doc.flush();
                        root.editLine(line, false);
                        event.accepted = true;
                    } else if (AppController.hasPendingUndo) {
                        AppController.undo();
                        event.accepted = true;
                    }
                    return;
                }
                if (event.matches(StandardKey.Redo) && !field.canRedo) {
                    root._commit();
                    if (src.canRedo) {
                        const line = view.editFirst;
                        root._leave(false);
                        src.redo();
                        doc.flush();
                        root.editLine(line, false);
                        event.accepted = true;
                    } else if (AppController.canRedo) {
                        AppController.redo();
                        event.accepted = true;
                    }
                    return;
                }
                // A file or a bare image on the clipboard is stored and linked
                // (the Knowledge editors); anything else pastes as text.
                if (root.attachOnPaste && event.matches(StandardKey.Paste) && AppController.clipboardHasAttachment()) {
                    const added = AppController.importClipboardAttachments();
                    root.insertRefs((added || []).map(a => a.ref).join("\n\n"));
                    event.accepted = true;
                    return;
                }
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && mods === Qt.NoModifier
                        && root._completeLink()) {
                    event.accepted = true;
                    return;
                }
                if (event.key === Qt.Key_Escape && mods === Qt.NoModifier) {
                    // First Esc leaves the block, the second the document.
                    // The drawn text holds the keyboard, not the stop the
                    // view had last (the plan's add field, F2-REG-1).
                    root._leave(true);
                    root._holdView();
                    event.accepted = true;
                    return;
                }
                // Backspace at a block's start / Delete at its end goes on into
                // the block next to it, as in any text: the line break between
                // them was out of reach, so two paragraphs could not be joined
                // and nothing could be edited past the block's edge.
                if (mods === Qt.NoModifier && field.selectionStart === field.selectionEnd && !root.wholeDocument
                        && ((event.key === Qt.Key_Backspace && field.cursorPosition === 0)
                            || (event.key === Qt.Key_Delete && field.cursorPosition === field.length))) {
                    // The rows are read from the source: the block's edits in
                    // first, or the lines below it are counted from the old text.
                    root._commit();
                    const t = src.text;
                    if (event.key === Qt.Key_Backspace && field.cursorPosition === 0 && view.editFirst > 0) {
                        let line = view.editFirst - 1;
                        while (line > 0 && t.substring(root._lineStart(t, line), root._lineEnd(t, line)).trim().length === 0) line--;
                        const row = doc.rowForLine(line);
                        const at = root._lineStart(t, view.editFirst);
                        root._extendTo(row >= 0 ? doc.firstLineOfRow(row) : line, view.editLast, at, at);
                        const c = field.cursorPosition;
                        if (c > 0) field.remove(c - 1, c);
                        event.accepted = true;
                        return;
                    }
                    const total = root._lineCount(t);
                    if (event.key === Qt.Key_Delete && field.cursorPosition === field.length && view.editLast < total - 1) {
                        let line = view.editLast + 1;
                        while (line < total - 1 && t.substring(root._lineStart(t, line), root._lineEnd(t, line)).trim().length === 0) line++;
                        const row = doc.rowForLine(line);
                        const at = root._lineEnd(t, view.editLast);
                        root._extendTo(view.editFirst, row >= 0 ? doc.lastLineOfRow(row) : line, at, at);
                        const c = field.cursorPosition;
                        if (c < field.length) field.remove(c, c + 1);
                        event.accepted = true;
                        return;
                    }
                }
                // Ctrl+A on a block already all selected: the whole document.
                if (event.matches(StandardKey.SelectAll) && !root.wholeDocument && field.length > 0
                        && field.selectionStart === 0 && field.selectionEnd === field.length) {
                    const all = root._lineCount(src.text) - 1;
                    if (view.editFirst > 0 || view.editLast < all) {
                        root._extendTo(0, all, 0, src.text.length);
                        event.accepted = true;
                        return;
                    }
                }
                // Shift+Up on the first line / Shift+Down on the last: the
                // selection runs on into the block next to it.
                if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down) && mods === Qt.ShiftModifier) {
                    const caret = field.cursorPosition;
                    const edge = event.key === Qt.Key_Up ? field.text.lastIndexOf("\n", caret - 1) < 0
                                                         : field.text.indexOf("\n", caret) < 0;
                    if (edge) root._commit();
                    const t = src.text;
                    const base = root._lineStart(t, view.editFirst);
                    const anchor = base + (caret === field.selectionStart ? field.selectionEnd : field.selectionStart);
                    if (event.key === Qt.Key_Up && field.text.lastIndexOf("\n", caret - 1) < 0 && view.editFirst > 0) {
                        let line = view.editFirst - 1;
                        while (line > 0 && t.substring(root._lineStart(t, line), root._lineEnd(t, line)).trim().length === 0) line--;
                        const row = doc.rowForLine(line);
                        const first = row >= 0 ? doc.firstLineOfRow(row) : line;
                        root._extendTo(first, view.editLast, anchor, root._lineStart(t, first));
                        event.accepted = true;
                        return;
                    }
                    const total = root._lineCount(t);
                    if (event.key === Qt.Key_Down && field.text.indexOf("\n", caret) < 0 && view.editLast < total - 1) {
                        let line = view.editLast + 1;
                        while (line < total - 1 && t.substring(root._lineStart(t, line), root._lineEnd(t, line)).trim().length === 0) line++;
                        const row = doc.rowForLine(line);
                        const last = row >= 0 ? doc.lastLineOfRow(row) : line;
                        root._extendTo(view.editFirst, last, anchor, root._lineEnd(t, last));
                        event.accepted = true;
                        return;
                    }
                }
                // The first and last line as drawn, not as typed: a long
                // paragraph wraps, and ↑/↓ inside it left the block or did
                // nothing at all instead of going up a line (dogfood
                // 2026-10-16).
                const caretY = field.positionToRectangle(field.cursorPosition).y;
                const firstLine = caretY <= field.positionToRectangle(0).y + 1;
                const lastLine = caretY >= field.positionToRectangle(field.length).y - 1;
                if (event.key === Qt.Key_Up && mods === Qt.NoModifier && firstLine) {
                    const prev = view.editFirst - 1;
                    // Nothing above: to the start of the line, as text fields do.
                    if (prev < 0) field.cursorPosition = 0;
                    if (prev >= 0) {
                        root._commit();
                        let line = prev;
                        // Over blank lines between blocks.
                        while (line > 0 && src.text.substring(root._lineStart(src.text, line), root._lineEnd(src.text, line)).trim().length === 0) line--;
                        root.editLine(line, false);
                    }
                    event.accepted = true;
                    return;
                }
                if (event.key === Qt.Key_Down && mods === Qt.NoModifier && lastLine) {
                    root._commit();
                    const total = root._lineCount(src.text);
                    let line = view.editLast + 1;
                    while (line < total - 1 && src.text.substring(root._lineStart(src.text, line), root._lineEnd(src.text, line)).trim().length === 0) line++;
                    if (line < total && line > view.editLast) root.editLine(line, true);
                    else field.cursorPosition = field.length;
                    event.accepted = true;
                    return;
                }
                // ← at a block's start and → at its end go on into the
                // block beside it, and Ctrl+Home / Ctrl+End to the start and
                // end of the whole text, as in any editor: they stopped at
                // the block's edge.
                if (!root.wholeDocument && mods === Qt.NoModifier
                        && ((event.key === Qt.Key_Left && field.cursorPosition === 0 && view.editFirst > 0)
                            || (event.key === Qt.Key_Right && field.cursorPosition === field.length))) {
                    root._commit();
                    const t = src.text;
                    const total = root._lineCount(t);
                    const back = event.key === Qt.Key_Left;
                    let line = back ? view.editFirst - 1 : view.editLast + 1;
                    while (line > 0 && line < total - 1
                           && t.substring(root._lineStart(t, line), root._lineEnd(t, line)).trim().length === 0)
                        line += back ? -1 : 1;
                    if (line >= 0 && line < total && (back || line > view.editLast)) {
                        root.editLine(line, !back);
                        event.accepted = true;
                        return;
                    }
                }
                if (!root.wholeDocument && mods === Qt.ControlModifier
                        && (event.key === Qt.Key_Home || event.key === Qt.Key_End)) {
                    root._commit();
                    if (event.key === Qt.Key_Home) root.editLine(0, true);
                    else root.editLine(Math.max(0, root._lineCount(src.text) - 1), false);
                    event.accepted = true;
                    return;
                }
                // "/" at the start of a line: insert a checklist, code, a link.
                // The "/" is typed as it is (sheet: "/" and the caret over
                // the menu); a pick replaces it, Esc leaves it.
                if (event.text === "/" && (field.cursorPosition === 0
                        || field.text.charAt(field.cursorPosition - 1) === "\n")) {
                    root.openSlashMenu();
                    event.accepted = true;
                    return;
                }
                // "/" over a new note's selected title is the insert menu on
                // the line under it, not a note called "/" (IDIOT-KNOW-9).
                if (event.text === "/" && root._titleSelected()) {
                    root.appendBlock();
                    root.openSlashMenu();
                    event.accepted = true;
                    return;
                }
                mdEditor.setSelection(field.selectionStart, field.selectionEnd);
                if (mdEditor.handleKey(event.key, event.modifiers) || mdEditor.typeText(event.text)) event.accepted = true;
            }
        }
    }
    // Rich: the field's markup hidden away from the caret and drawn over.
    Loader {
        id: livePreviewLoader
        active: root._isRich
        sourceComponent: Component {
            MdLivePreview {
                editor: field
                readOnly: root.readOnly
                imageTaskId: root.imageTaskId
                imageOrder: root.imageOrder
                // The task document reads at 1.65 (H2/Q-Task, R3-038, a-02).
                leading: root.paragraphLineHeight > 0 ? root.paragraphLineHeight : 1.65
                onInternalLinkActivated: (kind, target) => root.internalLinkActivated(kind, target)
            }
        }
    }
    // Esc on the drawn document: back to whoever opened it.
    Keys.onEscapePressed: root.escaped()
    // A letter typed with no block open goes into the text, where the caret
    // was last (or at the end), instead of nowhere (IDIOT-KNOW-6).
    // Ctrl+Z on the drawn document (after a tick) is the document's own
    // undo while it has one; it fell through to the app's and took back an
    // unseen change on the board instead (KNOW-2).
    Keys.onShortcutOverride: (event) => {
        if (root._drawnFocus && !root.readOnly && ((event.matches(StandardKey.Undo) && src.canUndo)
                                                || (event.matches(StandardKey.Redo) && src.canRedo)))
            event.accepted = true;
    }
    Keys.onPressed: (event) => {
        if (root._drawnFocus && !root.readOnly && event.matches(StandardKey.Undo) && src.canUndo) {
            src.undo();
            doc.flush();
            event.accepted = true;
            return;
        }
        if (root._drawnFocus && !root.readOnly && event.matches(StandardKey.Redo) && src.canRedo) {
            src.redo();
            doc.flush();
            event.accepted = true;
            return;
        }
        if (!root.typeToEdit || root.readOnly || root.editing || !root.visible) return;
        // A letter on a link or a button is not text for the note: g t on a
        // [[link]] wrote "gt" into it (VB5-R1).
        if (root._onStop()) return;
        const mods = event.modifiers & ~(Qt.ShiftModifier | Qt.KeypadModifier);
        const c = event.text.length > 0 ? event.text.charCodeAt(0) : 0;
        if (mods !== Qt.NoModifier || c < 32 || c === 127) return;
        root.focusEnd();
        if (!root.editing) return;
        const at = field.cursorPosition;
        if (event.text === "/" && (at === 0 || field.text.charAt(at - 1) === "\n")) root.openSlashMenu();
        else field.insert(at, event.text);
        event.accepted = true;
    }

    MarkdownEditorController {
        id: mdEditor
        target: field.textDocument
        cursorPosition: field.cursorPosition
        selectionStart: field.selectionStart
        selectionEnd: field.selectionEnd
        onSelectionRequested: (start, end) => {
            if (start === end) field.cursorPosition = start;
            else field.select(start, end);
        }
    }

    // What "/" inserts (MdSlashMenu, shared with the note's rich editor).
    MdSlashMenu {
        id: slashMenu
        field: field
        live: root.editing
        holding: fileDialog.visible
        onFileRequested: {
            fileDialog.line = view.editFirst;
            fileDialog.at = slashMenu.slashAt;
            fileDialog.open();
        }
    }

    // "Картинка или файл": stored under attachments/, linked where the "/" was.
    FileDialog {
        id: fileDialog
        property int line: -1
        property int at: -1
        fileMode: FileDialog.OpenFiles
        title: I18n.t("md.slash.file")
        onAccepted: {
            const list = [];
            for (let i = 0; i < selectedFiles.length; ++i) list.push(selectedFiles[i]);
            const added = AppController.importAttachments(list, true);
            if (!root.editing && fileDialog.line >= 0) root.editLine(fileDialog.line, false);
            slashMenu.slashAt = fileDialog.at;
            if (added && added.length > 0) {
                field.cursorPosition = Math.min(Math.max(0, fileDialog.at + 1), field.length);
                slashMenu.put(added.map(a => a.ref).join("\n\n"));
            }
        }
        onRejected: {
            slashMenu.slashAt = -1;
            if (!root.editing && fileDialog.line >= 0) root.editLine(fileDialog.line, false);
            else field.forceActiveFocus();
        }
    }

    component MdTailHost: Item {
        property Item loaded: null
    }
}
