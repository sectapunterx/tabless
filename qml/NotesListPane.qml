pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as QQC
import QtQuick.Dialogs
import TodoCpp

// The list of notes, beside the one being edited.
//
// Notes were one document per profile, so this pane had nothing to show and did
// not exist. Now that a profile holds many, the editor needs something that
// says which one is open and lets another be reached without a search.
//
// Pinned notes first, then folders, then loose notes — the order somebody
// scanning for a note actually uses, rather than the order they were created.
Rectangle {
    id: root

    property string filter: ""
    // "notes": the notes alone. "docs": the pages, snippets, links and
    // contacts (Knowledge is two screens, DG-070 revised).
    property string mode: "notes"
    readonly property bool docs: root.mode === "docs"
    onModeChanged: rebuildNow()
    // The doc page open in the document, so its row reads as the open one.
    property string openPageId: ""

    // Bumped by the model so the grouping rebuilds.
    property int rev: 0

    signal noteActivated(string id)
    // A note's "Экспорт в .md" (R2-062): where to, then one file.
    function exportNote(id, title) {
        noteExportDialog.noteId = id;
        noteExportDialog.currentFile = "file:///" + (title.replace(/[\\/:*?"<>|]/g, " ").trim() || "note") + ".md";
        noteExportDialog.open();
    }
    FileDialog {
        id: noteExportDialog
        property string noteId: ""
        fileMode: FileDialog.SaveFile
        nameFilters: ["Markdown (*.md)", "All files (*)"]
        defaultSuffix: "md"
        title: I18n.t("notes.menu.export")
        onAccepted: AppController.showToast(AppController.exportNoteToFile(noteExportDialog.noteId, selectedFile)
                                         ? I18n.t("notes.export.done") : I18n.t("notes.export.fail"))
    }
    // After "+": the view puts the cursor in the new note.
    signal noteCreated(string id)
    // Return on the list: the caret into the document on screen
    // (IDIOT-KNOW-7/8).
    signal editRequested()
    // Esc on an empty filter: back to the note, drawn, not into its text
    // (A4-6).
    signal documentRequested()
    // "Import Markdown…" under the empty list (A4-25).
    signal importRequested()
    // Knowledge (APP-269): Docs live in the same list — the reference links
    // pinned at the top, the doc pages and the snippets below the notes.
    signal refActivated(string url)
    signal pageActivated(string id)
    // A snippet is shown in the document pane (APP-290), its code to read;
    // a link has nothing inside but its address, so it opens.
    signal itemShown(var row)
    // "snippet:<idx>": the row the document shows.
    property string shownKey: ""
    signal contactActivated(string handle)

    // The Docs catalogue, read as it is stored: nothing is moved or copied,
    // so nothing of it can be lost on the way to Knowledge.
    // What a row holds, copied — a double click on any row (APP-290): a
    // page's text, a snippet's code, a link's address, a contact's handle.
    function copyItem(r) {
        if (!r) return;
        let text = "", what = "";
        if (r.kind === "page") { text = AppController.docPageBody(r.id); what = r.name || r.title || ""; }
        else if (r.kind === "snippet") { text = String(r.code || ""); what = r.title || r.lang || ""; }
        else if (r.kind === "ref") { text = String(r.url || ""); what = text; }
        else if (r.kind === "contact") { text = String(r.handle || ""); what = text; }
        if (text.length === 0) return;
        AppController.copyToClipboard(text);
        AppController.showToast(I18n.t("docs.toast.copied").arg(what));
    }
    function editItem(kind, data) { docDialog.openFor(kind, data); }

    function _docs() {
        const raw = AppController.docsState || "";
        if (!raw.length) return ({ sections: [], snippets: [], contacts: [] });
        try { return JSON.parse(raw) || ({}); } catch (e) { return ({}); }
    }
    // "RFC 9110" → tag "RFC", number "9110"; a ref without a number is its tag.
    // Only a short code is a tag: "OpenAPI 3.1" was cut to "Open" (R3-089);
    // a longer word names its kind by its "API" ending, else "REF".
    function _splitRef(ref) {
        const m = /^(\S+)\s+(.+)$/.exec(String(ref || "").trim());
        const r = m ? { tag: m[1], rest: m[2] } : { tag: String(ref || "").trim(), rest: "" };
        if (r.tag.length > 4) r.tag = /api$/i.test(r.tag) ? "API" : "REF";
        return r;
    }

    // Pin or unpin a reference of the catalogue (DG-073): only pinned ones
    // stand in Закреплено at rest; the rest come up in the search and Ctrl+K.
    // The blob is rewritten as read, with only this one flag changed.
    function setRefPinned(sectionIdx, itemIdx, pinned) {
        const raw = AppController.docsState || "";
        if (!raw.length) return;
        let d;
        try { d = JSON.parse(raw); } catch (e) { return; }
        const sec = (d.sections || [])[sectionIdx];
        const it = sec && (sec.items || [])[itemIdx];
        if (!it) return;
        if (pinned) it.pinned = true;
        else delete it.pinned;
        AppController.docsState = JSON.stringify(d);
    }

    // Snippets are kept in the same catalogue blob; `idx` is the place in its
    // `snippets`, -1 for a new one at the end. Undoable, like the old Docs.
    function _docsForWrite() {
        const raw = AppController.docsState || "";
        let d = ({});
        if (raw.length) {
            try { d = JSON.parse(raw) || ({}); } catch (e) { return null; }
        }
        if (!Array.isArray(d.sections)) d.sections = [];
        if (!Array.isArray(d.snippets)) d.snippets = [];
        if (!Array.isArray(d.contacts)) d.contacts = [];
        return d;
    }
    function _writeDocs(d, toastKey, name) {
        const label = I18n.t(toastKey).arg(name);
        AppController.setDocsStateUndoable(JSON.stringify(d), label);
        AppController.showToast(label);
    }
    function saveSnippet(idx, title, lang, code) {
        const d = root._docsForWrite();
        if (!d) return;
        const isNew = idx < 0 || idx >= d.snippets.length;
        const sn = isNew ? ({}) : d.snippets[idx];
        sn.title = title;
        sn.lang = lang;
        sn.code = code;
        if (isNew) d.snippets.push(sn);
        root._writeDocs(d, isNew ? "docs.toast.snippet.created" : "docs.toast.snippet.saved", title);
    }
    function deleteSnippet(idx) {
        const d = root._docsForWrite();
        if (!d || idx < 0 || idx >= d.snippets.length) return;
        const title = String(d.snippets[idx].title || "");
        d.snippets.splice(idx, 1);
        root._writeDocs(d, "docs.toast.snippet.deleted", title);
    }
    // A link of the catalogue: `sec`/`item` place it, sec < 0 is a new one,
    // filed in the first section (made if there is none).
    function saveRef(sec, item, title, ref, url) {
        const d = root._docsForWrite();
        if (!d) return;
        let it = null;
        if (sec >= 0 && d.sections[sec] && (d.sections[sec].items || [])[item]) {
            it = d.sections[sec].items[item];
        } else {
            if (d.sections.length === 0)
                d.sections.push({ id: "links", title: I18n.t("knowledge.refs"), items: [] });
            if (!Array.isArray(d.sections[0].items)) d.sections[0].items = [];
            it = ({});
            d.sections[0].items.push(it);
        }
        const isNew = sec < 0;
        it.title = title;
        it.ref = ref;
        it.url = url;
        root._writeDocs(d, isNew ? "docs.toast.created" : "docs.toast.saved", title || url);
    }
    function deleteRef(sec, item) {
        const d = root._docsForWrite();
        if (!d || !d.sections[sec] || !(d.sections[sec].items || [])[item]) return;
        const it = d.sections[sec].items[item];
        d.sections[sec].items.splice(item, 1);
        root._writeDocs(d, "docs.toast.deleted", String(it.title || it.url || ""));
    }
    function saveContact(idx, name, role, handle) {
        const d = root._docsForWrite();
        if (!d) return;
        const isNew = idx < 0 || idx >= d.contacts.length;
        const c = isNew ? ({}) : d.contacts[idx];
        c.name = name;
        c.role = role;
        c.mattermost = handle;
        if (isNew) d.contacts.push(c);
        root._writeDocs(d, isNew ? "docs.toast.contact.added" : "docs.toast.contact.saved", name);
    }
    function deleteContact(idx) {
        const d = root._docsForWrite();
        if (!d || idx < 0 || idx >= d.contacts.length) return;
        const name = String(d.contacts[idx].name || "");
        d.contacts.splice(idx, 1);
        root._writeDocs(d, "docs.toast.contact.deleted", name);
    }
    // "+" in Docs, Ctrl+N there: a page, opened and named.
    function newPage() {
        const id = AppController.newDocPage();
        root.pageActivated(id);
        pageRenamePopup.openFor(id, "");
    }

    color: Theme.bg
    implicitWidth: 260

    function _data(idx, name) {
        const m = AppController.notes;
        const r = m.roleOf(name);
        return r < 0 ? undefined : m.data(idx, r);
    }

    // Flat rows with a `kind`, so one ListView can draw both headers and notes
    // and the keyboard can walk them without a second structure to keep in step.
    function buildRows() {
        const m = AppController.notes;
        const needle = root.filter.trim();
        // The whole body, not the excerpt: a word from the middle of a note
        // has to find it. Asked of the controller, which holds the bodies.
        const hits = needle.length > 0 ? AppController.notesMatching(needle) : null;
        const allowed = ({});
        if (hits) for (let h = 0; h < hits.length; h++) allowed[hits[h]] = true;

        const pinned = [];
        const byFolder = ({});
        const loose = [];
        const rId = m.roleOf("id"), rTitle = m.roleOf("title"), rFolder = m.roleOf("folder");
        const rPinned = m.roleOf("pinned"), rExcerpt = m.roleOf("excerpt"), rCreated = m.roleOf("created");
        for (let i = 0; i < m.rowCount(); i++) {
            const idx = m.index(i, 0);
            const id = String(m.data(idx, rId));
            if (hits && !allowed[id]) continue;
            const note = {
                id:      id,
                title:   String(m.data(idx, rTitle) || ""),
                folder:  String(m.data(idx, rFolder) || ""),
                pinned:  !!m.data(idx, rPinned),
                excerpt: String(m.data(idx, rExcerpt) || ""),
                created: new Date(m.data(idx, rCreated) || 0).getTime() || 0
            };
            if (note.pinned) pinned.push(note);
            else if (note.folder.length > 0) {
                if (!byFolder[note.folder]) byFolder[note.folder] = [];
                byFolder[note.folder].push(note);
            } else loose.push(note);
        }

        const byTitle = function (a, b) {
            return a.title.toLowerCase() < b.title.toLowerCase() ? -1
                 : a.title.toLowerCase() > b.title.toLowerCase() ? 1 : 0;
        };
        // Notes newest first (sheet H2-Knowledge, DG-073): by when they were
        // made, so a note being typed in does not jump to the top.
        const newest = function (a, b) { return b.created - a.created || byTitle(a, b); };
        pinned.sort(byTitle);
        loose.sort(newest);

        // Docs: the reference links, the pages, the snippets (APP-269).
        const q = needle.toLowerCase();
        const has = (s) => q.length === 0 || String(s || "").toLowerCase().indexOf(q) >= 0;
        const docs = root._docs();
        // Pinned references stand in Закреплено (DG-073), the others under
        // Ссылки.
        const refs = [], otherRefs = [];
        const secs = docs.sections || [];
        for (let si = 0; si < secs.length; si++) {
            const items = (secs[si] && secs[si].items) || [];
            for (let ii = 0; ii < items.length; ii++) {
                const it = items[ii];
                if (!it || !(it.url || "").length) continue;
                const pinnedRef = it.pinned === true;
                if (!has(it.title) && !has(it.ref) && !has(it.desc) && !has(it.url)) continue;
                const r = root._splitRef(it.ref);
                const title = String(it.title || it.ref || it.url);
                (pinnedRef ? refs : otherRefs).push({
                    kind: "ref", tag: r.tag, rest: r.rest, name: title,
                    title: title + (r.rest.length ? " · " + r.rest : ""),
                    url: String(it.url), sec: si, item: ii, pinned: pinnedRef,
                    refTitle: String(it.title || ""), ref: String(it.ref || ""),
                    desc: String(it.desc || "")
                });
            }
        }
        const contacts = [];
        const cs = docs.contacts || [];
        for (let ci = 0; ci < cs.length; ci++) {
            const c = cs[ci];
            if (!c || (!has(c.name) && !has(c.role) && !has(c.channel) && !has(c.mattermost))) continue;
            const handle = String(c.mattermost || c.channel || c.name || "");
            contacts.push({ kind: "contact", tag: "", idx: ci, title: String(c.name || "") + (c.role ? " · " + c.role : ""),
                            handle: handle, name: String(c.name || ""), role: String(c.role || ""),
                            mm: String(c.mattermost || c.channel || "") });
        }
        const noteTitles = ({});
        for (const n of pinned.concat(loose)) noteTitles[n.title.toLowerCase()] = true;
        for (const f in byFolder) for (const n of byFolder[f]) noteTitles[n.title.toLowerCase()] = true;
        const pages = [];
        const dp = AppController.docPages;
        if (dp) {
            const rpId = dp.roleOf("id"), rpTitle = dp.roleOf("title");
            for (let i = 0; i < dp.rowCount(); i++) {
                const idx = dp.index(i, 0);
                const t = String(dp.data(idx, rpTitle) || "");
                if (!has(t)) continue;
                // A page named like a note says where it is from.
                const shown = noteTitles[t.toLowerCase()] ? t + " " + I18n.t("knowledge.fromDocs") : t;
                pages.push({ kind: "page", id: String(dp.data(idx, rpId)), title: shown, name: t });
            }
        }
        const snippets = [];
        const allSnippets = docs.snippets || [];
        for (let si = 0; si < allSnippets.length; si++) {
            const sn = allSnippets[si];
            if (!sn || (!has(sn.title) && !has(sn.code))) continue;
            snippets.push({ kind: "snippet", idx: si, tag: String(sn.lang || "txt"), lang: String(sn.lang || ""),
                            title: String(sn.title || ""), code: String(sn.code || "") });
        }

        const rows = [];
        // mode, not docs: this runs from onModeChanged, before docs is new.
        if (root.mode !== "docs") {
            if (pinned.length > 0) {
                rows.push({ kind: "header", label: I18n.t("notes.pinned") });
                for (const n of pinned) rows.push({ kind: "note", note: n, inPinnedSection: true });
            }
            const folders = Object.keys(byFolder).sort();
            for (const f of folders) {
                rows.push({ kind: "header", label: f, folder: f });
                byFolder[f].sort(newest);
                for (const n of byFolder[f]) rows.push({ kind: "note", note: n });
            }
            if (loose.length > 0) {
                rows.push({ kind: "header", label: I18n.t("notes.other") });
                for (const n of loose) rows.push({ kind: "note", note: n });
            }
            if (rows.length > 0) rows[0].first = true;
            return rows;
        }
        if (refs.length > 0) {
            rows.push({ kind: "header", label: I18n.t("notes.pinned") });
            for (const r of refs) rows.push(r);
        }
        if (pages.length > 0) {
            rows.push({ kind: "header", label: I18n.t("knowledge.pages") });
            for (const p of pages) rows.push(p);
        }
        if (snippets.length > 0) {
            rows.push({ kind: "header", label: I18n.t("knowledge.snippets") });
            for (const sn of snippets) rows.push(sn);
        }
        if (otherRefs.length > 0) {
            rows.push({ kind: "header", label: I18n.t("knowledge.refs") });
            for (const r of otherRefs) rows.push(r);
        }
        if (contacts.length > 0) {
            rows.push({ kind: "header", label: I18n.t("knowledge.contacts") });
            for (const c of contacts) rows.push(c);
        }
        // The first header sits closer to the search than the others to
        // the group above them.
        if (rows.length > 0 && rows[0].kind === "header") rows[0].first = true;
        return rows;
    }

    // Rebuilt on purpose rather than as a binding. As a binding it re-ran for
    // every row the model inserted — six hundred notes created in a loop meant
    // six hundred full rebuilds — and every rebuild handed the ListView a new
    // model, which threw the reader back to the top after each 250 ms save.
    // Now model changes are coalesced into one rebuild per event-loop turn, a
    // rebuild that changes nothing is not handed to the view at all, and one
    // that does keeps the scroll position.
    property var rows: []
    property string _rowsKey: ""
    property bool _rebuildQueued: false
    function rebuildNow() {
        root._rebuildQueued = false;
        const next = root.buildRows();
        const key = JSON.stringify(next);
        if (key === root._rowsKey) return;
        const y = list.contentY;
        root._rowsKey = key;
        root.rows = next;
        list.contentY = Math.max(0, Math.min(y, list.contentHeight - list.height));
    }
    function scheduleRebuild() {
        if (root._rebuildQueued) return;
        root._rebuildQueued = true;
        Qt.callLater(root.rebuildNow);
    }
    onFilterChanged: rebuildNow()
    Component.onCompleted: rebuildNow()
    Connections {
        target: I18n
        function onLangChanged() { root.rebuildNow() }
    }

    // The filter, set from outside: a #tag clicked in a note lists the notes
    // carrying it.
    function setFilter(text) {
        filterField.text = text;
        root.filter = text;
    }
    // F2: the rename dialog for the open note, wherever the focus is.
    function renameActive() {
        const m = AppController.notes;
        const row = m.indexOfId(AppController.activeNoteId);
        if (row < 0) return;
        const idx = m.index(row, 0);
        renamePopup.openFor(AppController.activeNoteId, String(m.data(idx, m.roleOf("title")) || ""),
                            String(m.data(idx, m.roleOf("folder")) || ""));
    }
    // F2 over a doc page renames the page, not the note behind it
    // (IDIOT-KNOW-4).
    function renamePage(id) {
        const m = AppController.docPages;
        const row = m.indexOfId(id);
        if (row < 0) return;
        pageRenamePopup.openFor(id, String(m.data(m.index(row, 0), m.roleOf("title")) || ""));
    }
    function takeFocus() { list.forceActiveFocus(); }
    // What the keyboard's delete would take: the doc page open in the
    // document ("page"), else the open note ("note"), else nothing ("").
    function deleteTargetKind() {
        if (root.openPageId.length > 0) return AppController.docPages.indexOfId(root.openPageId) >= 0 ? "page" : "";
        if (root.docs) return "";
        return AppController.activeNoteId.length > 0 && AppController.notes.indexOfId(AppController.activeNoteId) >= 0 ? "note" : "";
    }
    // Deletes it; the list keeps the keyboard on the row that takes its place.
    function deleteCurrent() {
        const kind = root.deleteTargetKind();
        if (kind === "") return;
        root._reclaim = true;
        if (kind === "page") AppController.deleteDocPage(root.openPageId);
        else AppController.deleteNote(AppController.activeNoteId);
        Qt.callLater(root.reclaimFocus);
    }
    // Set when a row's own action (delete) took the row the keyboard was
    // on: the list keeps the keyboard, whoever asks for it next.
    property bool _reclaim: false
    function reclaimFocus() {
        if (!root._reclaim) return false;
        root._reclaim = false;
        if (!root.visible) return false;
        root.focusList();
        return true;
    }
    function focusFilter() {
        filterField.forceActiveFocus();
        filterField.selectAll();
    }
    // Esc in the editor lands here, on the open note's row, so ↑/↓ walk the
    // notes from where the reader was.
    function focusList() {
        list.forceActiveFocus(Qt.OtherFocusReason);
        root._activeRowItem();
    }

    // The ids on screen, in order, so a caller can step through them.
    function visibleIds() {
        if (root._rebuildQueued) root.rebuildNow();
        const out = [];
        for (let i = 0; i < root.rows.length; i++)
            if (root.rows[i].kind === "note") out.push(root.rows[i].note.id);
        return out;
    }

    // The list row of the open note, scrolled into view — the keyboard's
    // way into that row's menu.
    function _activeRowItem() {
        if (root._rebuildQueued) root.rebuildNow();
        for (let i = 0; i < root.rows.length; i++) {
            const r = root.rows[i];
            if (r.kind !== "note" || r.note.id !== AppController.activeNoteId) continue;
            list.positionViewAtIndex(i, ListView.Contain);
            const loader = list.itemAtIndex(i) as Loader;
            return loader ? loader.item : null;
        }
        return null;
    }

    function _openFirstHit() {
        const ids = root.visibleIds();
        if (ids.length === 0) return;
        if (ids.indexOf(AppController.activeNoteId) < 0 || root.openPageId.length > 0) root.noteActivated(ids[0]);
        root.editRequested();
    }

    // Open the note `delta` places from the open one. Headers are skipped
    // because they are not somewhere the selection can land.
    function step(delta) {
        const ids = root.visibleIds();
        if (ids.length === 0) return;
        const at = ids.indexOf(AppController.activeNoteId);
        const next = at < 0 ? 0 : Math.max(0, Math.min(ids.length - 1, at + delta));
        root.noteActivated(ids[next]);
    }

    Connections {
        target: AppController
        function onDocsStateChanged() { root.scheduleRebuild() }
    }
    Connections {
        target: AppController.docPages
        function onRowsInserted() { root.scheduleRebuild() }
        function onRowsRemoved()  { root.scheduleRebuild() }
        function onDataChanged()  { root.scheduleRebuild() }
        function onModelReset()   { root.scheduleRebuild() }
    }
    // The labels in notes (APP-320), as chips that filter by the tag: read
    // a moment after the notes change, never from the row build.
    property int _tagRev: 0
    readonly property var tagChips: root._tagRev >= 0 && !root.docs ? AppController.noteTagCounts().slice(0, 12) : []
    Timer { id: tagTimer; interval: 400; onTriggered: root._tagRev++ }
    Connections {
        target: AppController.notes
        function onRowsInserted() { tagTimer.restart() }
        function onRowsRemoved() { tagTimer.restart() }
        function onDataChanged() { tagTimer.restart() }
        function onModelReset() { tagTimer.restart() }
    }
    Connections {
        target: AppController
        function onNotesStateChanged() { tagTimer.restart() }
    }
    Connections {
        target: AppController.notes
        function onRowsInserted() { root.scheduleRebuild() }
        function onRowsRemoved()  { root.scheduleRebuild() }
        function onDataChanged()  { root.scheduleRebuild() }
        function onModelReset()   { root.scheduleRebuild() }
    }
    Rectangle {
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 1; color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spLg
        anchors.rightMargin: Theme.spLg
        spacing: Theme.spMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spSm
            Layout.topMargin: Style.quiet ? Theme.px(18) : Theme.spXs
            Text {
                objectName: "knowledge-title"
                text: I18n.t(root.docs ? "siderail.docs" : "siderail.notes")
                color: Theme.text
                font.family: Theme.fontDisplay
                font.pixelSize: Style.quiet ? Theme.fsXl : Theme.px(18)  // sheet H2-Knowledge: 18px (R4-066)
                font.weight: Theme.fwHeading
                Layout.fillWidth: true
            }
            // Bold: a framed "+"; quiet: the mark alone (sheets H2/Q-Knowledge).
            Rectangle {
                objectName: "note-new"
                width: Theme.px(26); height: Theme.px(26); radius: Theme.radiusSm
                color: newMA.hovered ? Theme.panel3 : (Style.quiet ? "transparent" : Theme.panel2)
                Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                border.color: Style.quiet ? "transparent" : Theme.border
                border.width: 1
                PlusGlyph {
                    anchors.centerIn: parent
                    size: Theme.px(9)
                    color: Style.quiet ? Theme.textMuted : Theme.text
                }
                // Notes: "+" is a new note. Docs: a page, a snippet, a link
                // or a contact.
                ClickArea {
                    id: newMA
                    objectName: "notes-new"
                    label: I18n.t(root.docs ? "knowledge.new" : "notes.newNote")
                    shortcutId: "notes.new"
                    onActivated: {
                        if (root.docs) {
                            newMenu.popup(newMA, 0, newMA.height);
                            return;
                        }
                        const id = AppController.newNote();
                        root.noteActivated(id);
                        root.noteCreated(id);
                    }
                }
                AppMenu {
                    id: newMenu
                    AppMenuItem {
                        objectName: "knowledge-new-page"
                        text: I18n.t("knowledge.new.page")
                        onTriggered: root.newPage()
                    }
                    AppMenuItem {
                        objectName: "knowledge-new-snippet"
                        text: I18n.t("knowledge.new.snippet")
                        onTriggered: docDialog.openFor("snippet", ({ idx: -1 }))
                    }
                    AppMenuItem {
                        objectName: "knowledge-new-ref"
                        text: I18n.t("knowledge.new.ref")
                        onTriggered: docDialog.openFor("ref", ({ sec: -1, item: -1 }))
                    }
                    AppMenuItem {
                        objectName: "knowledge-new-contact"
                        text: I18n.t("knowledge.new.contact")
                        onTriggered: docDialog.openFor("contact", ({ idx: -1 }))
                    }
                }
            }
        }

        QQC.TextField {
            id: filterField
            QQC.ContextMenu.menu: TextEditMenu { editor: filterField }
            objectName: "note-filter"
            Layout.fillWidth: true
            placeholderText: Style.quiet ? I18n.t("notes.filter.short") : I18n.t(root.docs ? "knowledge.docs.filter" : "notes.filter")
            placeholderTextColor: Theme.textDim
            color: Theme.text
            font.pixelSize: Theme.fsSm
            leftPadding: Style.quiet ? 0 : Theme.spMd
            // Quiet: a line under the words, no box.
            background: Item {
                implicitHeight: Theme.px(30)
                FieldFrame { anchors.fill: parent; visible: !Style.quiet }
                Rectangle {
                    visible: Style.quiet
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 1
                    color: filterField.activeFocus ? Theme.accent : Theme.border
                }
            }
            onTextChanged: root.filter = text
            // Esc clears, then goes back to the note; Return opens the first
            // hit to write in, ↓ walks the hits (IDIOT-KNOW-7).
            Keys.onEscapePressed: (event) => {
                if (filterField.text.length > 0) root.setFilter("");
                else root.documentRequested();
                event.accepted = true;
            }
            Keys.onReturnPressed: (event) => { root._openFirstHit(); event.accepted = true; }
            Keys.onEnterPressed: (event) => { root._openFirstHit(); event.accepted = true; }
            Keys.onDownPressed: (event) => { root.focusList(); event.accepted = true; }
        }

        // "#личное  #hr  #работа": a click shows the notes with that label,
        // a second one shows all again (APP-320).
        Flow {
            objectName: "notes-tag-chips"
            visible: root.tagChips.length > 0
            Layout.fillWidth: true
            spacing: Theme.spXs
            Repeater {
                model: root.tagChips
                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    readonly property bool active: root.filter.trim().toLowerCase() === "#" + chip.modelData.id
                    objectName: "notes-tag-" + chip.modelData.id
                    implicitWidth: chipText.implicitWidth + 2 * Theme.spSm
                    implicitHeight: Theme.px(22)
                    radius: height / 2
                    color: chip.active ? Theme.accentSoft : chipCA.hovered ? Theme.panel3 : "transparent"
                    border.width: 1
                    border.color: chip.active ? Theme.accent : Theme.border
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: "#" + chip.modelData.id
                        color: chip.active ? Theme.accentStrong : Theme.textMuted
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsXs
                    }
                    ClickArea {
                        id: chipCA
                        label: chipText.text
                        tip: I18n.count(chip.modelData.count, "notes.tag.count")
                        checkable: true
                        checked: chip.active
                        onActivated: root.setFilter(chip.active ? "" : "#" + chip.modelData.id)
                    }
                }
            }
        }

        EmptyState {
            objectName: "notes-empty"
            // No notes, even with pinned links and snippets listed (R3-134).
            visible: root.rows.length === 0
            Layout.fillWidth: true
            Layout.topMargin: Theme.sp2xl
            title: root.filter.length > 0 ? I18n.t("notes.noMatches")
                 : I18n.t(root.docs ? "knowledge.docs.empty" : "notes.empty")
            line: root.filter.length > 0 ? I18n.t("notes.noMatches.hint")
                : root.docs ? I18n.t("knowledge.docs.empty.line")
                : I18n.t("notes.empty.line").arg(AppController.shortcutText("notes.new"))
            // An action, not words in a sentence (A4-25).
            Text {
                id: importLink
                objectName: "notes-empty-import"
                visible: !root.docs && root.filter.length === 0
                anchors.horizontalCenter: parent.horizontalCenter
                text: I18n.t("notes.empty.import")
                color: importCA.hovered ? Theme.text : Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsMd
                font.underline: true
                ClickArea {
                    id: importCA
                    label: importLink.text
                    role: Accessible.Link
                    showTip: false
                    onActivated: root.importRequested()
                }
            }
        }

        ListView {
            id: list
            objectName: "note-list"
            Accessible.name: I18n.t("siderail.notes")
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.sp2xs
            model: root.rows
            QQC.ScrollBar.vertical: QQC.ScrollBar { policy: QQC.ScrollBar.AsNeeded }
            // Keyboard (design audit DES-3): Tab lands on the list, ↑/↓ open
            // the next note, the menu key (or Shift+F10) opens the open
            // note's menu — pin and delete had no keyboard path.
            activeFocusOnTab: true
            // The view's own keyboard surface, not a stray control (Main's
            // _focusOnControl): g t, ?, :, / and u work here as on any view
            // (A4-5) - u brings back a note just deleted from this list (A4-9).
            readonly property bool viewSurface: true
            Accessible.role: Accessible.List
            // Deleting from the keyboard (dogfood 2026-10-19): Del in the
            // Classic keymap, d d (and Del) in Vim — Main asks here first.
            // The note or page the list is on goes, with Undo in the toast.
            readonly property var claimedKeys: ["notes.delete"]
            function claimLive(id) { return id === "notes.delete" && root.deleteTargetKind().length > 0; }
            function runClaimed(id) {
                if (id !== "notes.delete") return false;
                root.deleteCurrent();
                return true;
            }
            Keys.onUpPressed: { root.step(-1); Qt.callLater(root._activeRowItem); }
            Keys.onDownPressed: { root.step(1); Qt.callLater(root._activeRowItem); }
            // Esc on the list is as far out as Esc goes in Knowledge: it
            // stays here. It went on into the note's text, so Esc, Esc, Esc
            // ended up typing again (A4-6).
            Keys.onShortcutOverride: (event) => {
                if (event.key === Qt.Key_Escape && (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier) event.accepted = true;
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape && (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier) {
                    event.accepted = true;
                    return;
                }
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    const r = root._activeRowItem();
                    if (r) r.openMenu();
                    event.accepted = true;
                    return;
                }
                // The row menu says "Open ↵"; j / k are ↓ / ↑ as in every
                // list with a cursor (IDIOT-KNOW-8).
                const mods = event.modifiers & ~Qt.KeypadModifier;
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && mods === Qt.NoModifier) {
                    root.editRequested();
                    event.accepted = true;
                } else if (AppController.keyMatches("board.cursorDown", event.key, event.modifiers, event.text, event.nativeScanCode)
                           || AppController.keyMatches("board.cursorUp", event.key, event.modifiers, event.text, event.nativeScanCode)) {
                    // The keymap's own down / up (j / k in Vim), as a cursor
                    // does in every list; arrows always (above).
                    root.step(AppController.keyMatches("board.cursorDown", event.key, event.modifiers, event.text,
                                                       event.nativeScanCode) ? 1 : -1);
                    Qt.callLater(root._activeRowItem);
                    event.accepted = true;
                }
            }

            delegate: Loader {
                required property var modelData
                width: list.width
                sourceComponent: modelData.kind === "header" ? headerRow
                               : modelData.kind === "note" ? noteRow : docRow
                onLoaded: item.rowData = modelData
            }

            Component {
                id: headerRow
                Item {
                    id: headerItem
                    property var rowData: ({})
                    height: headerItem.rowData.first ? Theme.px(30) : Theme.px(40)
                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.spMd
                        anchors.bottomMargin: Theme.spSm
                        text: headerItem.rowData.label || ""
                        color: Theme.textDim
                        font.pixelSize: Theme.fsXs
                        elide: Text.ElideRight
                    }
                    // A folder header is the folder: right-click renames it or
                    // removes it (its notes move up a level, none are lost).
                    MouseArea {
                        anchors.fill: parent
                        enabled: (headerItem.rowData.folder || "").length > 0
                        acceptedButtons: Qt.RightButton
                        onClicked: folderMenu.popup()
                    }
                    AppMenu {
                        id: folderMenu
                        AppMenuItem {
                            text: I18n.t("notes.folder.rename")
                            onTriggered: folderPopup.openFor(headerItem.rowData.folder)
                        }
                        AppMenuSeparator {}
                        AppMenuItem {
                            text: I18n.t("notes.folder.remove")
                            danger: true
                            onTriggered: AppController.removeNoteFolder(headerItem.rowData.folder)
                        }
                    }
                }
            }

            // A reference link, a doc page or a snippet: a small tag and a title.
            Component {
                id: docRow
                Rectangle {
                    id: drow
                    property var rowData: ({})
                    objectName: "knowledge-" + (drow.rowData.kind || "") + "-row"
                    height: Theme.px(30)
                    radius: Theme.radiusMd
                    color: drowCA.hovered ? Theme.panel2 : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    readonly property bool current: {
                        const r = drow.rowData;
                        if (r.kind === "page") return r.id === root.openPageId;
                        if (r.kind === "snippet") return root.shownKey === "snippet:" + r.idx;
                        return false;
                    }
                    // Quiet draws no tag chip: a reference reads "RFC 9110 · title".
                    readonly property string shownTitle: {
                        const r = drow.rowData;
                        if (Style.quiet && r.kind === "ref")
                            return (r.rest || "").length > 0 ? r.tag + " " + r.rest + " · " + r.name : r.name;
                        return r.title || "";
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                        spacing: Theme.spMd
                        Rectangle {
                            visible: !Style.quiet && (drow.rowData.tag || "").length > 0
                            implicitWidth: tagTxt.implicitWidth + 2 * Theme.spXs
                            implicitHeight: tagTxt.implicitHeight + Theme.sp2xs
                            radius: Theme.radiusSm
                            color: "transparent"
                            border.color: Theme.border
                            border.width: 1
                            Text {
                                id: tagTxt
                                anchors.centerIn: parent
                                text: String(drow.rowData.tag || "")
                                color: Theme.textDim
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fsXs
                            }
                        }
                        Text {
                            id: drowTitle
                            Layout.fillWidth: true
                            text: drow.shownTitle
                            color: Theme.text
                            font.pixelSize: Theme.fsSm
                            font.weight: drow.current ? Theme.fwHeading : Theme.fwBody
                            elide: Text.ElideRight
                            CursorBar {
                                shown: drow.current
                                anchors.left: parent.left
                                anchors.top: parent.bottom
                            }
                        }
                    }
                    // A click opens what has an inside (a page, a snippet) in
                    // the document, opens a link, copies a contact's handle; a
                    // double click copies what the row holds. A link waits out
                    // the double-click time, so copying it does not open it.
                    ClickArea {
                        id: drowCA
                        label: drow.rowData.title || ""
                        // The name is written on the row: a tip only when it is
                        // cut. It hung over "Pinned" on arrival (EYE-19).
                        showTip: drowTitle.truncated
                        role: Accessible.Link
                        onActivated: {
                            const r = drow.rowData;
                            if (r.kind === "snippet") root.itemShown(r);
                            else if (r.kind === "page") root.pageActivated(r.id);
                            else if (r.kind === "contact") root.contactActivated(r.handle);
                            else if (r.kind === "ref") refOpen.restart();
                        }
                        onDoubleActivated: {
                            refOpen.stop();
                            // The contact's first click copied it already.
                            if (drow.rowData.kind !== "contact") root.copyItem(drow.rowData);
                        }
                    }
                    Timer {
                        id: refOpen
                        interval: Qt.styleHints.mouseDoubleClickInterval
                        onTriggered: root.refActivated(drow.rowData.url)
                    }
                    // Right-click: pin a reference, rename or delete a page,
                    // edit or delete a snippet, a link or a contact.
                    MouseArea {
                        anchors.fill: parent
                        enabled: drow.rowData.kind !== undefined
                        acceptedButtons: Qt.RightButton
                        onClicked: docMenu.popup()
                    }
                    AppMenu {
                        id: docMenu
                        AppMenuItem {
                            objectName: "knowledge-item-open"
                            visible: drow.rowData.kind === "ref"
                            height: visible ? implicitHeight : 0
                            text: I18n.t("md.link.web.open")
                            onTriggered: root.refActivated(drow.rowData.url)
                        }
                        AppMenuItem {
                            objectName: "knowledge-item-copy"
                            visible: drow.rowData.kind !== undefined
                            height: visible ? implicitHeight : 0
                            text: I18n.t("docs.copy")
                            onTriggered: root.copyItem(drow.rowData)
                        }
                        AppMenuItem {
                            objectName: "knowledge-ref-pin"
                            visible: drow.rowData.kind === "ref"
                            height: visible ? implicitHeight : 0
                            text: drow.rowData.pinned ? I18n.t("notes.unpin") : I18n.t("notes.pin")
                            onTriggered: root.setRefPinned(drow.rowData.sec, drow.rowData.item, !drow.rowData.pinned)
                        }
                        AppMenuItem {
                            visible: drow.rowData.kind === "page"
                            height: visible ? implicitHeight : 0
                            text: I18n.t("notes.rename")
                            onTriggered: pageRenamePopup.openFor(drow.rowData.id, drow.rowData.name || "")
                        }
                        AppMenuItem {
                            visible: drow.rowData.kind === "page"
                            height: visible ? implicitHeight : 0
                            text: I18n.t("common.delete")
                            danger: true
                            onTriggered: AppController.deleteDocPage(drow.rowData.id)
                        }
                        AppMenuItem {
                            objectName: "knowledge-item-edit"
                            visible: drow.rowData.kind !== "page"
                            height: visible ? implicitHeight : 0
                            text: I18n.t("knowledge.edit")
                            onTriggered: docDialog.openFor(drow.rowData.kind, drow.rowData)
                        }
                        AppMenuItem {
                            objectName: "knowledge-item-delete"
                            visible: drow.rowData.kind !== "page"
                            height: visible ? implicitHeight : 0
                            text: I18n.t("common.delete")
                            danger: true
                            onTriggered: {
                                const r = drow.rowData;
                                if (r.kind === "snippet") root.deleteSnippet(r.idx);
                                else if (r.kind === "ref") root.deleteRef(r.sec, r.item);
                                else if (r.kind === "contact") root.deleteContact(r.idx);
                            }
                        }
                    }
                }
            }

            Component {
                id: noteRow
                Rectangle {
                    id: row
                    property var rowData: ({})
                    readonly property var note: rowData.note || ({})
                    objectName: "note-row-" + (row.note.id || "")
                    height: Theme.px(30)
                    radius: Theme.radiusMd
                    // A doc page open in the document is the current row instead.
                    readonly property bool current: row.note.id === AppController.activeNoteId && root.openPageId.length === 0
                    color: rowMA.containsMouse ? Theme.panel2 : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durTap; easing.type: Theme.easeEnter } }
                    Accessible.role: Accessible.ListItem
                    Accessible.name: row.note.title || ""
                    readonly property alias menu: rowMenu
                    // The row sits in a Loader, the ListView's delegate.
                    readonly property bool listFocused: !!row.parent && !!row.parent.ListView.view
                                                        && row.parent.ListView.view.activeFocus
                    function openMenu() { rowMenu.popup(row, Theme.spXl, row.height / 2); }
                    FocusRing {
                        anchors.margins: 0
                        radius: row.radius
                        visible: row.current && row.listFocused
                        z: 10
                    }


                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spMd; anchors.rightMargin: Theme.spMd
                        anchors.topMargin: Theme.spXs; anchors.bottomMargin: Theme.spXs
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spXs
                            Text {
                                id: noteTitleTxt
                                text: row.note.title || ""
                                color: Theme.text
                                font.pixelSize: Theme.fsSm
                                font.weight: row.current ? Theme.fwHeading : Theme.fwBody
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                CursorBar {
                                    shown: row.current
                                    anchors.left: parent.left
                                    anchors.top: parent.bottom
                                }
                            }
                            // A pin is only worth drawing where it is not
                            // already implied by the section it is under.
                            Text {
                                visible: row.note.pinned && row.rowData.inPinnedSection !== true
                                text: "•"
                                color: Theme.accent
                                font.pixelSize: Theme.fsMd
                            }
                        }
                        Text {
                            text: row.note.excerpt || ""
                            // textMuted on the selected row: textDim on the 14%
                            // accent tint fell to 3.3:1 (design audit DES-21).
                            color: row.current ? Theme.textMuted : Theme.textDim
                            font.pixelSize: Theme.fsXs
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            // One line per note in Knowledge (sheet H2-Knowledge).
                            visible: false
                        }
                    }

                    MouseArea {
                        id: rowMA
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                        // APP-116: drag a note onto another to merge it in.
                        // The ghost moves, not the row, so the list stays put.
                        drag.target: dragGhost
                        drag.threshold: Theme.spMd
                        onPressed: (mouse) => {
                            if (mouse.button !== Qt.LeftButton) return;
                            const p = row.mapToItem(root, 0, 0);
                            dragGhost.x = p.x; dragGhost.y = p.y;
                            dragGhost.width = row.width;
                            dragGhost.noteId = row.note.id;
                            dragGhost.title = row.note.title || "";
                        }
                        onReleased: {
                            if (dragGhost.dragging) dragGhost.Drag.drop();
                            dragGhost.dragging = false;
                            dragGhost.noteId = "";
                            dragGhost.overTitle = "";
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) rowMenu.popup();
                            else root.noteActivated(row.note.id);
                        }
                    }
                    Connections {
                        target: rowMA.drag
                        function onActiveChanged() {
                            if (rowMA.drag.active) dragGhost.dragging = true;
                        }
                    }
                    DropArea {
                        id: rowDrop
                        objectName: "note-drop-" + (row.note.id || "")
                        anchors.fill: parent
                        keys: ["heap-note"]
                        onEntered: (drag) => {
                            drag.accepted = drag.source === dragGhost && dragGhost.noteId !== row.note.id;
                            if (drag.accepted) dragGhost.overTitle = row.note.title || "";
                        }
                        onExited: if (dragGhost.overTitle === (row.note.title || "")) dragGhost.overTitle = ""
                        onDropped: (drop) => {
                            if (drop.source !== dragGhost || dragGhost.noteId === row.note.id) return;
                            drop.accept();
                            AppController.mergeNotes(dragGhost.noteId, row.note.id);
                        }
                    }
                    // Where a drop would land: outlined, and the ghost says so.
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Theme.accentSoft
                        border.color: Theme.accent
                        border.width: 1
                        visible: rowDrop.containsDrag && dragGhost.noteId !== row.note.id
                        z: 9
                    }

                    // The note's menu (R2-062, sheet X/N-Menus-Other). Merging
                    // stays on drag and drop: a row dropped on another.
                    AppMenu {
                        id: rowMenu
                        objectName: "note-row-menu"
                        AppMenuHeader {
                            text: I18n.t("notes.menu.header").arg(row.note.title || "")
                        }
                        AppMenuItem {
                            objectName: "note-menu-open"
                            text: I18n.t("notes.menu.open")
                            keyText: "↵"
                            onTriggered: root.noteActivated(row.note.id)
                        }
                        AppMenuItem {
                            objectName: "note-menu-pin"
                            text: row.note.pinned ? I18n.t("notes.unpin") : I18n.t("notes.pin")
                            onTriggered: AppController.setNotePinned(row.note.id, !row.note.pinned)
                        }
                        AppMenuItem {
                            objectName: "note-menu-rename"
                            text: I18n.t("notes.menu.rename")
                            shortcutId: "notes.rename"
                            onTriggered: renamePopup.openFor(row.note.id, row.note.title, row.note.folder)
                        }
                        AppMenuItem {
                            objectName: "note-menu-copy-link"
                            text: I18n.t("notes.menu.copyLink")
                            onTriggered: AppController.copyToClipboard("[[" + (row.note.title || "") + "]]")
                        }
                        AppMenuItem {
                            objectName: "note-menu-export"
                            text: I18n.t("notes.menu.export")
                            onTriggered: root.exportNote(row.note.id, row.note.title || "")
                        }
                        AppMenuSeparator {}
                        AppMenuItem {
                            objectName: "note-menu-delete"
                            text: I18n.t("common.delete")
                            danger: true
                            // The keyboard stays on the list: it fell into the
                            // next note's text, where u was typed instead of
                            // bringing the note back (A4-9).
                            onTriggered: {
                                root._reclaim = true;
                                AppController.deleteNote(row.note.id);
                                Qt.callLater(root.reclaimFocus);
                            }
                        }
                    }
                }
            }
        }
    }

    // What a note drag carries: the note's id, drawn as a chip under the
    // pointer. Dropped on another row, that row merges it in (APP-116).
    Rectangle {
        id: dragGhost
        objectName: "note-drag-ghost"
        property string noteId: ""
        property string title: ""
        // Set by the row being dragged while its drag is live.
        property bool dragging: false
        // The note under the pointer that would take this one in.
        property string overTitle: ""
        visible: dragging
        height: 32
        z: 100
        radius: Theme.radiusMd
        color: Theme.panel3
        border.color: Theme.borderStrong
        opacity: 0.95
        Drag.active: dragging
        Drag.keys: ["heap-note"]
        Drag.source: dragGhost
        Drag.hotSpot.x: Theme.spXl
        Drag.hotSpot.y: height / 2
        Text {
            anchors.fill: parent
            anchors.leftMargin: Theme.spXl; anchors.rightMargin: Theme.spMd
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: dragGhost.overTitle.length > 0 ? I18n.t("notes.dropToMerge").arg(dragGhost.overTitle) : dragGhost.title
            color: Theme.text
            font.pixelSize: Theme.fsSm
            font.weight: Theme.fwTitle
        }
    }

    QQC.Dialog {
        PopupArrival { popup: folderPopup }
        id: folderPopup
        objectName: "note-folder-rename"
        property string folder: ""
        modal: true
        QQC.Overlay.modal: ModalScrim {}
        anchors.centerIn: QQC.Overlay.overlay
        parent: QQC.Overlay.overlay
        padding: Theme.inset
        width: 380
        title: I18n.t("notes.folder.rename")

        function openFor(folder) {
            folderPopup.folder = folder;
            folderName.text = folder;
            folderPopup.open();
            folderName.forceActiveFocus();
            folderName.selectAll();
        }
        function commit() {
            AppController.renameNoteFolder(folderPopup.folder, folderName.text);
            folderPopup.close();
        }

        background: ModalSurface {}
        contentItem: QQC.TextField {
            id: folderName
            objectName: "note-folder-name"
            color: Theme.text
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsSm
            background: FieldFrame {}
            onAccepted: folderPopup.commit()
        }
        footer: RowLayout {
            spacing: Theme.spMd
            Layout.margins: Theme.sp2xl
            Item { Layout.fillWidth: true }
            PillButton { text: I18n.t("common.cancel"); onClicked: folderPopup.close() }
            PillButton { text: I18n.t("editor.btn.save"); primary: true; onClicked: folderPopup.commit() }
        }
    }

    // A page keeps its name and body; renaming is all the list offers.
    QQC.Dialog {
        PopupArrival { popup: pageRenamePopup }
        id: pageRenamePopup
        objectName: "knowledge-page-rename"
        property string pageId: ""
        modal: true
        QQC.Overlay.modal: ModalScrim {}
        anchors.centerIn: QQC.Overlay.overlay
        parent: QQC.Overlay.overlay
        padding: Theme.inset
        width: 380
        title: I18n.t("notes.rename")
        function openFor(id, title) {
            pageRenamePopup.pageId = id;
            pageName.text = title;
            pageRenamePopup.open();
            pageName.forceActiveFocus();
            pageName.selectAll();
        }
        function commit() {
            if (pageName.text.trim().length > 0) AppController.renameDocPage(pageRenamePopup.pageId, pageName.text.trim());
            pageRenamePopup.close();
        }
        background: ModalSurface {}
        contentItem: QQC.TextField {
            id: pageName
            color: Theme.text
            background: FieldFrame {}
            onAccepted: pageRenamePopup.commit()
        }
        footer: RowLayout {
            spacing: Theme.spMd
            Layout.margins: Theme.sp2xl
            Item { Layout.fillWidth: true }
            PillButton { text: I18n.t("common.cancel"); onClicked: pageRenamePopup.close() }
            PillButton { text: I18n.t("editor.btn.save"); primary: true; onClicked: pageRenamePopup.commit() }
        }
    }

    // A snippet (title, language, code), a link (title, ref, URL) or a
    // contact (name, role, Mattermost handle): new or edited, one dialog.
    QQC.Dialog {
        PopupArrival { popup: docDialog }
        id: docDialog
        objectName: "knowledge-doc-dialog"
        property string kind: "snippet"
        property var at: ({})
        readonly property bool isNew: docDialog.kind === "ref" ? !(docDialog.at.sec >= 0) : !(docDialog.at.idx >= 0)
        modal: true
        QQC.Overlay.modal: ModalScrim {}
        anchors.centerIn: QQC.Overlay.overlay
        parent: QQC.Overlay.overlay
        padding: Theme.inset
        width: 520
        title: I18n.t("knowledge." + docDialog.kind + (docDialog.isNew ? ".new" : ".edit"))
        function openFor(kind, data) {
            docDialog.kind = kind;
            docDialog.at = data;
            const v = (k) => String(data[k] || "");
            if (kind === "snippet") {
                f1.text = v("title"); f2.text = v("lang"); f3.text = ""; codeArea.text = v("code");
            } else if (kind === "ref") {
                f1.text = v("refTitle"); f2.text = v("ref"); f3.text = v("url"); codeArea.text = "";
            } else {
                f1.text = v("name"); f2.text = v("role"); f3.text = v("mm"); codeArea.text = "";
            }
            docDialog.open();
            f1.forceActiveFocus();
            f1.selectAll();
        }
        readonly property bool canSave: docDialog.kind === "snippet"
                                        ? f1.text.trim().length > 0 || codeArea.text.trim().length > 0
                                        : docDialog.kind === "ref" ? f3.text.trim().length > 0
                                        : f1.text.trim().length > 0
        function commit() {
            if (!docDialog.canSave) return;
            const a = docDialog.at;
            if (docDialog.kind === "snippet") {
                const title = f1.text.trim().length > 0 ? f1.text.trim()
                                                        : codeArea.text.trim().split("\n")[0].slice(0, 60);
                root.saveSnippet(docDialog.isNew ? -1 : a.idx, title, f2.text.trim(), codeArea.text);
            } else if (docDialog.kind === "ref") {
                root.saveRef(docDialog.isNew ? -1 : a.sec, docDialog.isNew ? -1 : a.item,
                             f1.text.trim(), f2.text.trim(), f3.text.trim());
            } else {
                root.saveContact(docDialog.isNew ? -1 : a.idx, f1.text.trim(), f2.text.trim(), f3.text.trim());
            }
            docDialog.close();
        }
        background: ModalSurface {}
        contentItem: ColumnLayout {
            spacing: Theme.spMd
            QQC.TextField {
                id: f1
                objectName: "knowledge-doc-f1"
                QQC.ContextMenu.menu: TextEditMenu { editor: f1 }
                Layout.fillWidth: true
                placeholderText: I18n.t(docDialog.kind === "contact" ? "docs.field.name" : "docs.field.title")
                placeholderTextColor: Theme.textDim
                color: Theme.text
                background: FieldFrame {}
                onAccepted: f2.forceActiveFocus()
            }
            QQC.TextField {
                id: f2
                objectName: "knowledge-doc-f2"
                QQC.ContextMenu.menu: TextEditMenu { editor: f2 }
                Layout.fillWidth: true
                placeholderText: I18n.t(docDialog.kind === "snippet" ? "docs.field.language"
                                      : docDialog.kind === "ref" ? "docs.field.ref" : "docs.field.role")
                placeholderTextColor: Theme.textDim
                color: Theme.text
                font.family: docDialog.kind === "contact" ? Theme.fontUi : Theme.fontMono
                background: FieldFrame {}
                onAccepted: docDialog.kind === "snippet" ? codeArea.forceActiveFocus() : f3.forceActiveFocus()
            }
            QQC.TextField {
                id: f3
                objectName: "knowledge-doc-f3"
                visible: docDialog.kind !== "snippet"
                QQC.ContextMenu.menu: TextEditMenu { editor: f3 }
                Layout.fillWidth: true
                placeholderText: I18n.t(docDialog.kind === "ref" ? "docs.field.url" : "docs.field.mmHandle")
                placeholderTextColor: Theme.textDim
                color: Theme.text
                background: FieldFrame {}
                onAccepted: docDialog.commit()
            }
            QQC.ScrollView {
                visible: docDialog.kind === "snippet"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.px(200)
                QQC.TextArea {
                    id: codeArea
                    objectName: "knowledge-doc-code"
                    QQC.ContextMenu.menu: TextEditMenu { editor: codeArea }
                    placeholderText: I18n.t("docs.field.code")
                    placeholderTextColor: Theme.textDim
                    color: Theme.text
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsSm
                    wrapMode: TextEdit.NoWrap
                    background: FieldFrame {}
                    // Enter is a new line in code; Ctrl+Enter saves.
                    Keys.onPressed: (e) => {
                        if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && (e.modifiers & Qt.ControlModifier)) {
                            docDialog.commit();
                            e.accepted = true;
                        }
                    }
                }
            }
        }
        footer: RowLayout {
            spacing: Theme.spMd
            Layout.margins: Theme.sp2xl
            Item { Layout.fillWidth: true }
            PillButton { text: I18n.t("common.cancel"); onClicked: docDialog.close() }
            PillButton {
                objectName: "knowledge-doc-save"
                text: I18n.t("editor.btn.save")
                primary: true
                enabled: docDialog.canSave
                onClicked: docDialog.commit()
            }
        }
    }

    // A small muted label over a dialog field (X-Dlg-Small): the rename
    // dialog's two fields were bare, the folder one read as a name (A4-11).
    component FieldLabel: Text {
        Layout.bottomMargin: -Theme.spSm
        color: Theme.textMuted
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
    }

    // Rename and re-file in one place: they are the same edit as far as the
    // reader is concerned — what this note is called and where it lives.
    QQC.Dialog {
        PopupArrival { popup: renamePopup }
        id: renamePopup
        objectName: "note-rename"
        property string noteId: ""
        modal: true
        QQC.Overlay.modal: ModalScrim {}
        anchors.centerIn: QQC.Overlay.overlay
        parent: QQC.Overlay.overlay
        padding: Theme.inset
        width: 380
        // The menu item's "…" is not the dialog's (A4-11).
        title: I18n.t("notes.rename.title")

        // Why Save did nothing: an empty name, a name taken (IDIOT-KNOW-3/13).
        property string error: ""
        // The same name in another folder is allowed, but [[name]] then has
        // two notes to open: said while it is typed, not found out later
        // (A4-10). Which one a link opens: its own folder's, else the first.
        readonly property string elsewhere: {
            const t = titleField.text.trim().toLowerCase();
            if (t.length === 0 || renamePopup.error.length > 0) return "";
            const folder = folderField.text.trim();
            const m = AppController.notes;
            const rT = m.roleOf("title"), rF = m.roleOf("folder"), rI = m.roleOf("id");
            for (let i = 0; i < m.rowCount(); i++) {
                const idx = m.index(i, 0);
                if (String(m.data(idx, rI)) === renamePopup.noteId) continue;
                if (String(m.data(idx, rT) || "").trim().toLowerCase() !== t) continue;
                const f = String(m.data(idx, rF) || "");
                if (f !== folder) return f.length > 0 ? f : I18n.t("notes.rename.noFolder");
            }
            return "";
        }
        function openFor(id, title, folder) {
            renamePopup.noteId = id;
            titleField.text = title;
            folderField.text = folder;
            renamePopup.error = "";
            renamePopup.open();
            titleField.forceActiveFocus();
            titleField.selectAll();
        }

        // The dialog stays open with a word why instead of closing on a
        // name that was not taken.
        function commit() {
            const title = titleField.text.trim();
            if (title.length === 0) {
                renamePopup.error = I18n.t("notes.rename.err.empty");
            } else if (AppController.noteTitleTaken(title, folderField.text.trim(), renamePopup.noteId)) {
                renamePopup.error = I18n.t("notes.rename.err.taken").arg(title);
            } else {
                AppController.renameNote(renamePopup.noteId, title);
                AppController.moveNoteToFolder(renamePopup.noteId, folderField.text);
                renamePopup.close();
                return;
            }
            titleField.forceActiveFocus();
            titleField.selectAll();
        }

        background: ModalSurface {}

        contentItem: ColumnLayout {
            spacing: Theme.spMd
            FieldLabel { text: I18n.t("notes.rename.name") }
            QQC.TextField {
                id: titleField
                QQC.ContextMenu.menu: TextEditMenu { editor: titleField }
                Accessible.name: I18n.t("notes.rename.name")
                objectName: "note-rename-title"
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                color: Theme.text
                background: FieldFrame {}
                onAccepted: renamePopup.commit()
                onTextChanged: renamePopup.error = ""
            }
            Text {
                objectName: "note-rename-error"
                visible: renamePopup.error.length > 0
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                text: renamePopup.error
                color: Theme.danger
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                wrapMode: Text.Wrap
            }
            Text {
                objectName: "note-rename-elsewhere"
                visible: renamePopup.elsewhere.length > 0
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                text: I18n.t("notes.rename.warn.elsewhere").arg(titleField.text.trim()).arg(renamePopup.elsewhere)
                color: Theme.warning
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
                wrapMode: Text.Wrap
            }
            FieldLabel { text: I18n.t("notes.rename.folder") }
            QQC.TextField {
                id: folderField
                QQC.ContextMenu.menu: TextEditMenu { editor: folderField }
                Accessible.name: I18n.t("notes.rename.folder")
                objectName: "note-rename-folder"
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                placeholderText: I18n.t("notes.folderPlaceholder")
                placeholderTextColor: Theme.textDim
                color: Theme.text
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSm
                background: FieldFrame {}
                onAccepted: renamePopup.commit()
            }
        }

        footer: RowLayout {
            spacing: Theme.spMd
            Layout.margins: Theme.sp2xl
            Item { Layout.fillWidth: true }
            PillButton { text: I18n.t("common.cancel"); onClicked: renamePopup.close() }
            PillButton { text: I18n.t("editor.btn.save"); primary: true; onClicked: renamePopup.commit() }
        }
    }
}
