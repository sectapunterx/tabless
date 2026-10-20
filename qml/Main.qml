import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import TodoCpp
import "DocsStarter.js" as DocsStarter
import "ThemePresets.js" as Presets
import "KeyRules.js" as KeyRules

ApplicationWindow {
    id: win
    // A login start (--minimized, APP-154) stays hidden in the tray; where
    // there is no tray it starts minimized instead (Component.onCompleted).
    visible: !(win._startHidden && win._minimizeToTray)
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 680
    // Every control that names no size of its own (a TextField, a combo's
    // text) reads the body size, so it follows the interface scale and the
    // system's text size with everything else (APP-183).
    font.pixelSize: Theme.fsMd

    // The OS draws the title bar and follows its own app mode, so a dark
    // heap on a light Windows sat under a white bar. Follow the theme, and
    // again once the window exists (a hidden start has no native frame yet).
    readonly property bool _frameDark: Theme.dark
    on_FrameDarkChanged: AppController.setWindowFrameDark(win, win._frameDark)
    onVisibleChanged: if (win.visible) AppController.setWindowFrameDark(win, win._frameDark)

    // ── Window geometry ───────────────────────────────────────────────
    // The window opened at a hardcoded 1440x900 in the same spot on every
    // launch, so resizing or moving it — or maximising it — was undone each
    // time the app started.
    //
    // Stored under settings.window in the same blob everything else uses.
    // SettingsView rewrites that blob wholesale but carries unknown top-level
    // keys across, so this survives a trip through the settings screen.
    property bool _geometryRestored: false
    readonly property bool _startHidden: typeof START_MINIMIZED !== "undefined" && START_MINIMIZED === true
    // A maximised window started hidden is maximised when first shown:
    // setting the visibility earlier would show it.
    property bool _maximizeOnShow: false

    // Builds the current view's loader on its first visit. Board and Notes
    // are never unloaded again. Notes and Docs are one NotesView in two
    // modes: the notes, or the pages, snippets, links and contacts.
    function activateCurrentView() {
        const v = AppController.currentView;
        if (v === "board") boardLoader.active = true;
        else if (v === "notes" || v === "docs") notesLoader.active = true;
    }
    // One fade for whichever view came on screen; a fade cut short by the
    // next switch leaves its view fully drawn, not half.
    function fadeInView(loader) {
        if (viewFade.running && viewFade.target && viewFade.target !== loader) {
            viewFade.stop();
            viewFade.target.opacity = 1;
        }
        viewFade.target = loader;
        viewFade.restart();
    }

    // Whichever view is on screen. Four loaders now hold them — three kept
    // alive, one shared — so nothing outside should have to know which.
    function activeViewItem() {
        const v = AppController.currentView;
        if (v === "board") return boardLoader.item;
        if (v === "notes" || v === "docs") return notesLoader.item;
        // The calendar lens wraps its grid (APP-264): the keys act on the grid.
        const it = viewLoader.item;
        if (it && it.objectName === "calendar-view") return it["calendarView"];
        return it;
    }

    // A #TICKET clicked in a note or doc page: the heap id, or a tracker key
    // ("PROJ-123") that a mirrored task carries.
    function openTaskById(key) {
        const t = AppController.taskById(AppController.taskIdForBranchMatch(key));
        if (t && t.id) win.showTask(t);
        else win.notice(I18n.t("notes.link.noTask").arg(key), "warning");
    }

    // A profile with no docs blob gets the starter catalogue here, so its
    // references are in Ctrl+K and in the Knowledge list (pinned ones at
    // rest, the rest under search) from the first run.
    function seedStarterDocs() {
        if ((AppController.docsState || "").length > 0) return;
        AppController.docsState = JSON.stringify({
            sections: DocsStarter.sections(I18n.lang, [Theme.mStandup, Theme.mOneone, Theme.mSync, Theme.mFocus]),
            snippets: DocsStarter.snippets(I18n.lang),
            contacts: DocsStarter.contacts(I18n.lang)
        });
    }

    // The same link rules outside Notes: a doc page's [[note]] opens the note.
    // "New task" inside the app opens the full ticket editor (APP-286);
    // the one-line quick capture stays on its own hotkey, the tray and --capture.
    function newTask(statusId) {
        taskEditor.showFor(AppController.newTaskDraft(statusId || ""));
    }

    function followMdLink(kind, target) {
        if (kind === "task") { win.openTaskById(target); return; }
        if (kind === "person") {
            const pid = AppController.personIdForHandle(target);
            if (pid.length > 0) personEditor.showFor(AppController.personById(pid));
            return;
        }
        if (kind === "note") {
            const hit = AppController.resolveNoteLink(target);
            if (hit.kind === "note" || hit.kind === "heading") {
                AppController.activeNoteId = hit.noteId;
                AppController.currentView = "notes";
                return;
            }
            const t = AppController.taskById(String(target).trim());
            if (t && t.id) { win.showTask(t); return; }
            win.notice(I18n.t("notes.link.noNote").arg(target), "warning");
        }
    }

    function _settingsObject() {
        const raw = AppController.appSettingsJson || "";
        if (!raw.length) return ({});
        try { return JSON.parse(raw) || ({}); } catch (e) { return ({}); }
    }

    // True when at least a good part of `rect` lands on some screen. A window
    // restored onto a monitor that is no longer attached would otherwise open
    // off-screen with no way to drag it back.
    function _isOnAScreen(rect) {
        const screens = Qt.application.screens;
        for (let i = 0; i < screens.length; i++) {
            const s = screens[i];
            const ix = Math.max(0, Math.min(rect.x + rect.width,  s.virtualX + s.width)  - Math.max(rect.x, s.virtualX));
            const iy = Math.max(0, Math.min(rect.y + rect.height, s.virtualY + s.height) - Math.max(rect.y, s.virtualY));
            // A title bar's worth of overlap is enough to grab the window.
            if (ix >= 120 && iy >= 40) return true;
        }
        return false;
    }

    function _restoreGeometry() {
        const g = _settingsObject().window;
        if (!g) { win._geometryRestored = true; return; }

        const w = Math.max(win.minimumWidth,  Number(g.width)  || win.width);
        const h = Math.max(win.minimumHeight, Number(g.height) || win.height);
        const x = Number(g.x), y = Number(g.y);
        if (isFinite(x) && isFinite(y) && _isOnAScreen({ x: x, y: y, width: w, height: h })) {
            win.x = x;
            win.y = y;
        }
        win.width = w;
        win.height = h;
        if (g.maximized === true) {
            if (win.visible) win.visibility = Window.Maximized;
            else win._maximizeOnShow = true;
        }
        win._geometryRestored = true;
    }

    // x/y/width/height change continuously while a window is dragged or
    // resized, so the write is debounced rather than run per frame.
    Timer {
        id: geometrySaveTimer
        interval: 400
        onTriggered: win._saveGeometry()
    }

    function _saveGeometry() {
        if (!win._geometryRestored) return;
        const maximized = win.visibility === Window.Maximized;
        const s = _settingsObject();
        // While maximised, x/y/width/height describe the maximised frame —
        // keep the last normal geometry so unmaximising after a restart does
        // not snap the window to the full screen size.
        const prev = s.window || ({});
        s.window = maximized
            ? { x: prev.x, y: prev.y, width: prev.width, height: prev.height, maximized: true }
            : { x: win.x, y: win.y, width: win.width, height: win.height, maximized: false };
        AppController.appSettingsJson = JSON.stringify(s);
    }

    onXChanged: if (win._geometryRestored) geometrySaveTimer.restart()
    onYChanged: if (win._geometryRestored) geometrySaveTimer.restart()
    // The F6 frame is drawn where its region was: a resize moves that, so
    // the frame goes (VB5-R2).
    onWidthChanged: {
        win.regionShown = "";
        if (win._geometryRestored) geometrySaveTimer.restart();
    }
    onHeightChanged: {
        win.regionShown = "";
        if (win._geometryRestored) geometrySaveTimer.restart();
    }
    onVisibilityChanged: if (win._geometryRestored) geometrySaveTimer.restart()
    // Ends with the display name, so the OS title bar does not append it a
    // second time ("heap. — Work, in one place. - heap.").
    title: I18n.t("window.title")
    color: Theme.bg
    // The stock Basic controls (combo box lists, tooltips, scroll bars, and
    // anything not drawn by hand) take their colours from the palette. Left
    // alone it was the OS palette, which on Windows meant black popups with
    // white rows whatever the theme; this feeds it the theme's tokens.
    palette {
        window: Theme.panel
        windowText: Theme.text
        base: Theme.panel2
        alternateBase: Theme.panel3
        text: Theme.text
        button: Theme.panel2
        buttonText: Theme.text
        brightText: Theme.text
        highlight: Theme.accentSoft
        highlightedText: Theme.accentStrong
        light: Theme.panel2
        midlight: Theme.panel3
        mid: Theme.border
        dark: Theme.borderStrong
        shadow: Theme.scrim
        placeholderText: Theme.textDim
        link: Theme.mdLink
        toolTipBase: Theme.toastBg
        toolTipText: Theme.toastText
    }

    property string searchText: ""
    // How long a command letter on the board waits to see whether a word is
    // being typed (KeyRouter.typingHoldMs, A4-3); tests set 0.
    property int typeAheadHoldMs: 250
    property var prioritiesFilter: ({})

    // No right panel (DG-002): the sheets have none. The day is on Today
    // and in the calendar; the people are in "Кому написать" on Today and
    // in their own dialog (PeopleDialog, palette "people.open").

    // Left sidebar: labelled (expanded) or the 56px icon rail. The choice is
    // remembered; below _sideRailMinWidth it folds to the rail on its own
    // without overwriting what was chosen, same as the right panel.
    // heap 2 (APP-258): the sidebar folds to its icons on a small window
    // (X-Oth-Small: 1280×720 is small, DG-008).
    readonly property int _sideRailMinWidth: Theme.compactWindowWidth
    property bool _sideRailWanted: _settingsObject().sideRailExpanded !== false
    property bool _sideRailOnNarrow: false
    readonly property bool sideRailExpanded: Theme.compactAt(win.width) ? _sideRailOnNarrow : _sideRailWanted
    function toggleSideRail() {
        if (Theme.compactAt(win.width)) {
            _sideRailOnNarrow = !_sideRailOnNarrow;
            return;
        }
        _sideRailWanted = !_sideRailWanted;
        const s = _settingsObject();
        s.sideRailExpanded = _sideRailWanted;
        AppController.appSettingsJson = JSON.stringify(s);
    }
    property bool showDoneTimeline: false
    property bool showArchived: false
    // Board column order. Lives on the window so it survives the board being
    // hidden, and so the filter bar and the board agree without either owning
    // the other.
    property string boardSortMode: "manual"
    // The board's sort and the list's grouping, as the header shows them
    // beside the lens tabs (APP-262/263).
    readonly property var _sortOption: {
        const ids = ["manual", "priority", "due", "updated", "title", "id"];
        const base = win.boardSortMode.endsWith("-desc") ? win.boardSortMode.slice(0, -5) : win.boardSortMode;
        return { label: I18n.t("filter.sortBy").replace(/:\s*$/, ""), current: base,
                 value: I18n.t("filter.sort." + base),
                 items: ids.map(id => ({ id: id, label: I18n.t("filter.sort." + id) })) };
    }
    property string listGroupBy: {
        const g = _settingsObject().listGroupBy;
        return ["date", "status", "priority", "profile", "month"].indexOf(g) >= 0 ? g : "date";
    }
    function setListGroupBy(g) {
        win.listGroupBy = g;
        const s = _settingsObject();
        s.listGroupBy = g;
        AppController.appSettingsJson = JSON.stringify(s);
    }
    readonly property var _groupOption: {
        const ids = ["date", "status", "priority", "profile"];
        if (win._archiveQuery) ids.push("month");
        return { label: I18n.t("list.groupBy"), current: win.listGroupEffective,
                 value: I18n.t("list.groupBy." + win.listGroupEffective),
                 items: ids.map(id => ({ id: id, label: I18n.t("list.groupBy." + id) })) };
    }
    // The archive is a condition of the query on Board and List (heap 2,
    // X-Oth-Archive-People): "is:archived" lets archived tasks through.
    readonly property bool tasksShowArchived: win.showArchived || /(^|\s)is:archived(\s|$)/i.test(win.searchText)
    // Archive is not a section of its own (DG-161, X-Oth-Archive-People): it
    // is Tasks · List with the condition "is:archived", grouped by month.
    readonly property bool _archiveQuery: /(^|\s)is:archived(\s|$)/i.test(win.searchText)
    function openArchive() {
        if (!win._archiveQuery)
            win.searchText = (win.searchText.replace(/(^|\s)is:open(?=\s|$)/gi, " ").trim() + " is:archived").trim();
        AppController.currentView = "list";
    }
    // By date an archive is one "Earlier" heap: it goes by month unless
    // another grouping was picked.
    readonly property string listGroupEffective: win._archiveQuery && win.listGroupBy === "date" ? "month" : win.listGroupBy

    // Search, priority chips, sort and the archived / done toggles survive a
    // restart (TASKS-22): they lived only on the window, so every launch
    // started from an unfiltered board. Kept in the UI settings blob, written
    // a moment after the last change rather than on every keystroke.
    property bool _filtersRestored: false
    function _restoreFilters() {
        const f = _settingsObject().filters || {};
        win.searchText = win.defaultQuery(typeof f.search === "string" ? f.search : "", f.v === 2);
        win.prioritiesFilter = (f.priorities && typeof f.priorities === "object") ? f.priorities : ({});
        win.boardSortMode = typeof f.sort === "string" && f.sort.length > 0 ? f.sort : "manual";
        win.showArchived = f.archived === true;
        win.showDoneTimeline = f.showDone === true;
        savedViewsHost.activeId = typeof f.savedView === "string" ? f.savedView : "";
        win._filtersRestored = true;
    }
    // The query a start begins from: what was saved, with "is:open" in
    // front once for filters saved before it was the default — unless the
    // saved query already says which tasks it wants by status or state.
    function defaultQuery(saved, current) {
        const q = String(saved || "").trim();
        if (current) return q;
        if (/(^|\s)-?(is|status):\S+/i.test(q) || /(^|\s)OR(\s|$)/.test(q)) return q;
        return ("is:open " + q).trim();
    }
    function _saveFiltersSoon() { if (win._filtersRestored) filterSaveTimer.restart(); }
    // The selection follows the filters: a card selected and then hidden by
    // the search left the selection with it, and Delete took it along with
    // the visible ones (TASKS-13, audit 2026-09-30). The Archive view lists
    // archived tasks whatever the toggle says; the timeline hides done ones
    // unless asked.
    function _syncSelectionFilter() {
        const v = AppController.currentView;
        const pri = [];
        for (const k in win.prioritiesFilter) if (win.prioritiesFilter[k]) pri.push(k);
        AppController.setSelectionFilter(win.searchText, pri, win.tasksShowArchived, false);
    }
    onSearchTextChanged: { _saveFiltersSoon(); _syncSelectionFilter(); }
    onPrioritiesFilterChanged: { _saveFiltersSoon(); _syncSelectionFilter(); }
    onBoardSortModeChanged: _saveFiltersSoon()
    onShowArchivedChanged: { _saveFiltersSoon(); _syncSelectionFilter(); }
    onShowDoneTimelineChanged: { _saveFiltersSoon(); _syncSelectionFilter(); }
    Connections {
        target: AppController
        function onCurrentViewChanged() { win._syncSelectionFilter(); }
    }
    Timer {
        id: filterSaveTimer
        interval: 400
        onTriggered: {
            const s = win._settingsObject();
            s.filters = { v: 2, search: win.searchText, priorities: win.prioritiesFilter, sort: win.boardSortMode,
                          archived: win.showArchived, showDone: win.showDoneTimeline,
                          savedView: savedViewsHost.activeId };
            AppController.appSettingsJson = JSON.stringify(s);
        }
    }

    // Reactive task / status counts. statusCounts is one pass over the model,
    // recomputed when the model changes; these used to be four separate full
    // scans, re-run from all four of the model's signals.
    readonly property var _counts: AppController.statusCounts
    readonly property int _activeCount:  (_counts["prog"] || 0) + (_counts["half"] || 0)
    readonly property int _blockedCount: _counts["blocked"] || 0
    readonly property int _reviewCount:  _counts["review"] || 0
    readonly property var _activePriorities: {
        const out = [];
        for (const k in win.prioritiesFilter) if (win.prioritiesFilter[k]) out.push(k);
        return out;
    }

    // A built-in theme the update retired is kept as the user's own theme
    // with the palette it had, instead of being swapped for another (APP-127).
    function _keepRetiredThemes() {
        const s = _settingsObject();
        const next = Presets.adoptRetired(s.appearance);
        if (!next) return;
        s.appearance = next;
        AppController.appSettingsJson = JSON.stringify(s);
    }

    // Every attached ToolTip in the app is one shared instance. Dressed here
    // as the popup level of elevation (APP-182) — the control style drew a
    // square grey box in the system palette, unlike any menu or drop-down.
    property Item _tipSurface: PopupSurface {}
    property Item _tipText: Text {
        text: ToolTip.toolTip ? ToolTip.toolTip.text : ""
        color: Theme.text
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        wrapMode: Text.Wrap
    }
    // An Item, since the window cannot take the ToolTip attached property.
    property Item _tipStyler: Item {
        Component.onCompleted: {
            const tt = ToolTip.toolTip;
            if (!tt) return;
            tt.background = win._tipSurface;
            tt.contentItem = win._tipText;
            tt.padding = Theme.spMd;
        }
    }

    Component.onCompleted: {
        AppController.setWindowFrameDark(win, win._frameDark);
        _keepRetiredThemes();
        _restoreGeometry();
        if (win._startHidden && !win._minimizeToTray) win.showMinimized();
        _restoreFilters();
        _syncSelectionFilter();
        win.seedStarterDocs();
        if (typeof INITIAL_VIEW !== "undefined" && INITIAL_VIEW && INITIAL_VIEW.length > 0)
            AppController.currentView = INITIAL_VIEW;
        // First run (APP-271): no tour — Today is the first-run screen
        // until the first task. Otherwise, on the first launch of a week,
        // what moved last week (WEAK PECAP).
        if (AppController.welcomeSeen)
            Qt.callLater(win._maybeShowRecap);
        // Coming from 0.7: say once what the new sidebar moved (APP-258).
        if (AppController.shellNotice.length > 0)
            Qt.callLater(win._showShellNotice);
        // A damaged data file at launch: the card with the snapshots (R2-037).
        Qt.callLater(damagedFile.showIfNeeded);
        // Once after an update to a new release line (R2-054).
        Qt.callLater(whatsNew.showIfUpdated);
        // A patch release: one quiet line, with the changelog a key away (A5-19).
        Qt.callLater(win._showUpdatedFrom);
    }
    function _showUpdatedFrom() {
        const from = AppController.takeUpdatedFrom();
        if (from.length === 0) return;
        const words = function () {
            return { message: I18n.t("update.patched").arg(from).arg(AppController.appVersion),
                     actionLabel: I18n.t("update.line.whatsNew") };
        };
        toast.showWithAction(words().message, words().actionLabel, 10, function () { whatsNew.showNow(); },
                             "info", "", true, words);
    }
    function _showShellNotice() {
        const msg = AppController.shellNotice;
        if (msg.length === 0) return;
        AppController.ackShellNotice();
        toast.showWithAction(msg, I18n.t("shell.notice.action"), 15, function () {
            win.openCheatSheet();
        });
    }

    // Close-to-tray: on platforms that have a tray icon (Windows/macOS via the
    // notification tray fallback), the window hides instead of quitting so the
    // global capture hotkeys can summon it back. The tray menu's "Quit" calls
    // QCoreApplication::quit() directly, so a real exit bypasses this. On Linux
    // there is no tray icon, so closing quits as usual.
    readonly property bool _minimizeToTray: Qt.platform.os === "windows" || Qt.platform.os === "osx"

    // Whether the X button hides to the tray: settings.system.closeToTray.
    // Unset until the first close asks, because doing it silently left people
    // thinking heap had quit while it kept running.
    function _closeToTrayPref() {
        const sys = _settingsObject().system;
        return sys ? sys.closeToTray : undefined;
    }
    function _setCloseToTray(v) {
        const s = _settingsObject();
        s.system = Object.assign({}, s.system || ({}), { closeToTray: v });
        AppController.appSettingsJson = JSON.stringify(s);
    }
    onClosing: (close) => {
        // Hiding to the tray keeps the editor as it is; a quit asks about what
        // is typed in it first, then closes again (SHELL-29).
        const quits = !win._minimizeToTray || win._closeToTrayPref() === false;
        if (quits && taskEditor.isDirty()) {
            close.accepted = false;
            taskEditor.settleThen(() => win.close());
            return;
        }
        if (!win._minimizeToTray) return;
        const pref = win._closeToTrayPref();
        if (pref === false) return;   // a real quit
        close.accepted = false;
        if (pref === undefined) {
            closeAsk.open();
            return;
        }
        win.hide();
    }

    QQC.Dialog {
        PopupArrival { popup: closeAsk }
        id: closeAsk
        objectName: "close-to-tray-ask"
        modal: true
        Overlay.modal: ModalScrim {}
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        padding: Theme.inset
        topPadding: Theme.spMd
        title: I18n.t("close.ask.title")
        background: ModalSurface {}
        // Drawn in the theme like every other dialog title; Basic's own
        // header was an unstyled bar in the palette's window colour.
        header: Text {
            text: closeAsk.title
            color: Theme.text
            font.pixelSize: Theme.fsLg
            font.weight: Theme.fwHeading
            leftPadding: Theme.inset; rightPadding: Theme.inset; topPadding: Theme.inset
            wrapMode: Text.Wrap
        }
        contentItem: Text {
            text: I18n.t("close.ask.body")
            color: Theme.textMuted
            font.pixelSize: Theme.fsMd
            wrapMode: Text.Wrap
        }
        // Inset from the dialog's edge; the buttons sat flush on the bottom.
        footer: Item {
            implicitHeight: closeAskRow.implicitHeight + Theme.inset
            RowLayout {
            id: closeAskRow
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: Theme.inset; anchors.rightMargin: Theme.inset
            spacing: Theme.spMd
            Item { Layout.fillWidth: true }
            PillButton {
                objectName: "close-ask-quit"
                text: I18n.t("close.ask.quit")
                onClicked: {
                    win._setCloseToTray(false);
                    closeAsk.close();
                    taskEditor.settleThen(() => {
                        AppController.flushSave();
                        Qt.quit();
                    });
                }
            }
            PillButton {
                objectName: "close-ask-tray"
                text: I18n.t("close.ask.tray")
                primary: true
                onClicked: {
                    win._setCloseToTray(true);
                    closeAsk.close();
                    win.hide();
                }
            }
            }
        }
    }

    // Open a side-rail popover next to its button — or close it if that same
    // button (or its shortcut) fired while the panel is already up.
    //
    // The popup is re-parented to the anchor so CloseOnPressOutsideParent means
    // "pressed outside the panel and outside the button that owns it": a press
    // anywhere else in the app dismisses it, while a press on the button itself
    // is left to the click handler below, which toggles. Positions are therefore
    // anchor-relative, but still clamped in window space so a rail button near
    // the bottom edge doesn't push the panel off-screen.
    // Everything that floats at the bottom centre — the selection bar, the
    // "continue tour" pill and the toast — used to be pinned to the same spot
    // and simply drew on top of each other (deleting a multi-selection put the
    // undo toast right over the selection bar). They stack instead.
    readonly property int _selectionBarSpace: AppController.selectionCount > 0 ? 68 : 0

    // True while anything modal-ish is up. A single-letter shortcut has to
    // stand down then: Qt only protects a focused text field, so a picker's
    // type-ahead or a read-only Text would otherwise swallow the keystroke or
    // let the shortcut fire over the dialog (HEAP-117).
    readonly property bool _overlayOpen: taskEditor.opened || AppController.immersion
        || personEditor.opened || profileEditor.opened
        || cmdPalette.opened || quickCapture.opened || quickCaptureNotes.opened || eventCapture.opened
        || hotkeys.opened || cheatSheet.opened || closeAsk.opened || goToDatePopup.opened
        || weeklyRecap.opened || standupDraft.opened || timeMachine.opened || eventLog.opened || endOfDay.opened

    // ── Keyboard scope ────────────────────────────────────────────────
    // Board and calendar keys (Return, Esc, the arrows, bare letters) are
    // application shortcuts, so Qt offered them before the focused item —
    // Return in the header search opened the card under the cursor, Down in a
    // card's context menu moved the board cursor, Enter in "New column"
    // opened the task editor. They belong to whatever holds focus unless that
    // is the view itself.
    //
    // Focus in something that takes typed text: a search box, an inline
    // rename, a breadcrumb being edited.
    readonly property bool _typing: {
        const f = win.activeFocusItem;
        return !!f && typeof f.cursorPosition === "number" && f.readOnly !== true;
    }
    // Focus inside a popup, menu or dialog — including the ones a view owns
    // (add column, WIP limit, delete confirm, a card's context menu), which
    // Main has no id for. Every popup's item lives under the window overlay.
    readonly property bool _focusInPopup: {
        for (let p = win.activeFocusItem; p; p = p.parent)
            if (p === Overlay.overlay) return true;
        return false;
    }
    // Where the keyboard goes back to when a popup closes (design audit
    // DES-1). Qt hands focus to whatever focus scope is left in the window —
    // a bare ColumnLayout after Esc in the task editor — so J/K, Return and
    // the view's other keys were dead until the next click. The last item
    // that held focus outside every popup is kept here and gets it back; if
    // it is gone (a card rebuilt by the save, a view switched away), the view
    // does. A popup that hands focus on by itself (the palette opening an
    // editor, a click on a field) is left alone: this only steps in while
    // focus still sits where Qt dropped it.
    property Item _focusHome: null
    property bool _focusWasInPopup: false
    // A popup that closed on Return leaves the rest of that key press — a
    // hurried second Enter — to the view, where board.open opened the card
    // under the cursor with the caret in its title (IDIOT-SHELL-1, the second
    // Return in a board dialog: IDIOT-TASKS-6). Return stands down for a
    // moment after focus leaves any popup.
    property real _returnGuardUntil: 0
    property bool _focusOnMenu: false
    onActiveFocusItemChanged: {
        const it = win.activeFocusItem;
        if (!it) return;
        // The region frame (APP-277) goes once the keyboard leaves the region.
        // A part of a view (Knowledge's links column) is left as the region
        // is: the frame stayed on the column after Esc (VB5-R2).
        if (win.regionShown.length > 0 && (win._regionOf(it) !== win.regionShown
                                           || (win._regionPart && !win._isWithin(it, win._regionPart))))
            win.regionShown = "";
        // Walked here, not read from _focusInPopup: that binding is stale at
        // this moment, and the popup's own item became the place to go back
        // to (IDIOT-DOC-4).
        let inPopup = false;
        for (let p = it; p; p = p.parent)
            if (p === Overlay.overlay) { inPopup = true; break; }
        // A menu holds the keyboard on its own item, a dialog in a field.
        const wasMenu = win._focusOnMenu;
        win._focusOnMenu = it.parent === Overlay.overlay;
        if (inPopup) {
            win._focusWasInPopup = true;
            // A card dragged while the palette opens does not land behind it
            // (IDIOT-TASKS-8).
            const dv = win.activeViewItem();
            if (dv && typeof dv.cancelDrag === "function") dv.cancelDrag();
            return;
        }
        if (wasMenu) {
            win._popupJustClosed = true;
            popupClosedTimer.restart();
        }
        if (win._focusWasInPopup) {
            win._focusWasInPopup = false;
            win._returnGuardUntil = Date.now() + 400;
            // Back on an empty header search is not "home" either (PERO-1):
            // returnFocusHome() sends the keyboard on to the view.
            if (it !== win._focusHome || win._isIdleSearch(it)) {
                const landed = it;
                // Qt may pass through the window's root on its way back to
                // the empty search; either place is handed on. On Linux and
                // macOS it goes on from the root, in the same key press, to
                // the root's own focus child, the view: with a document open
                // the keyboard stayed on the board behind it after Ctrl+K and
                // Esc (0.8.4 full CI). With a panel open the view is not
                // where anyone was, so that is handed on too.
                Qt.callLater(function () {
                    const f = win.activeFocusItem;
                    const fellToView = win._panelOpen && win._insideActiveView(f);
                    if ((f === landed || win._isIdleSearch(f) || fellToView) && !win._focusInPopup) win.returnFocusHome();
                });
            }
            return;
        }
        win._focusHome = it;
    }
    function _insideActiveView(it) {
        const v = win.activeViewItem();
        for (let p = it; p && v; p = p.parent)
            if (p === v) return true;
        return false;
    }
    function _isIdleSearch(it) { return !!it && it.objectName === "topbar-search" && (it.text || "").length === 0; }
    function returnFocusHome() {
        const h = win._focusHome;
        // An empty header search is not where anyone was working: it only
        // held focus because the button that opened the dialog takes none.
        // Sending focus back there put the caret and the query-syntax hint
        // over the view after every dialog, and the next click only cleared
        // it (PERO-1). The view gets it instead.
        const idleSearch = h && h.objectName === "topbar-search" && (h.text || "").length === 0;
        // A panel opened from a popup (a task picked in Ctrl+K) is where the
        // keyboard goes, not the board it was opened over (PERSONA-23).
        let inPanel = false;
        for (let p = h; p; p = p.parent)
            if (p === taskDoc || p === eventEditor) { inPanel = true; break; }
        if (win._panelOpen && !inPanel) { win.focusActiveView(); return; }
        if (h && !idleSearch && h.visible && h.enabled && h.Window.window === win) h.forceActiveFocus();
        else win.focusActiveView();
    }
    // A view switch takes the keyboard into the new view: it used to stay on
    // the hidden board, so after Ctrl+7 nothing typed reached the note and Tab
    // walked the top bar (PERA-5). A popup that still holds focus hands it on
    // when it closes (above), and the header search keeps a query being typed.
    function _focusSwitchedView() {
        if (win._focusInPopup) return;
        const f = win.activeFocusItem;
        if (f && f.visible && win._typing && !win._insideView(f)) return;
        // Already in the new view (something put it there first).
        const v = win.activeViewItem();
        for (let p = f; p && v; p = p.parent)
            if (p === v) return;
        // A view that lands the keyboard differently on arrival than on a
        // focus hand-back (Knowledge: the caret at the note's end).
        if (v && !eventEditor.opened && !taskDoc.opened && typeof v.arrive === "function") {
            v.arrive();
            return;
        }
        win.focusActiveView();
    }
    // Whether `it` sits in one of the four view loaders (any view, shown or not).
    function _insideView(it) {
        for (let p = it; p; p = p.parent)
            if (p === boardLoader || p === notesLoader || p === viewLoader) return true;
        return false;
    }
    // A modal (or dimming) popup is up somewhere — even one that did not take
    // focus, as most of the views' own confirm dialogs do not. Each puts its
    // dimmer on the overlay next to the popup items, so a visible overlay child
    // that is not a popup item is one.
    readonly property bool _dimmerShown: {
        const ov = Overlay.overlay;
        if (!ov) return false;
        const kids = ov.children;
        for (let i = 0; i < kids.length; i++) {
            const k = kids[i];
            if (k.visible && k.opacity > 0 && String(k).indexOf("QQuickPopupItem") !== 0) return true;
        }
        return false;
    }
    // …and that popup is one of the two side-rail popovers, which leave the
    // app usable behind them.
    readonly property bool _focusInPopover: {
        for (let p = win.activeFocusItem; p; p = p.parent)
            if (p === hotkeys.contentItem) return true;
        return false;
    }
    // Focus on a control outside the view that was reached with Tab — a
    // filter chip, the mini week, the day panel, the people list. Its arrows
    // and Enter are its own; the board's cursor keys wait until focus goes
    // back to the view.
    readonly property bool _focusOnControl: {
        const f = win.activeFocusItem;
        if (!f) return false;
        const v = win.activeViewItem();
        for (let p = f; p; p = p.parent) {
            // A panel handles its own Esc (IDIOT-DOC-5): a chip in the task
            // document is not a stray control of the view.
            if (p === v || p === taskDoc || p === eventEditor) return false;
            // A view's own keyboard surface (the month grid) is the view:
            // it claims the keys it uses itself and leaves the rest.
            if (p.activeFocusOnTab === true) return p.viewSurface !== true;
        }
        return false;
    }
    // A view-local key (board cursor, calendar paging, Esc on the selection,
    // Delete, the bare-letter shortcuts) stands down while any of this holds.
    readonly property bool _viewKeysBlocked: hotkeys.isCapturing || _overlayOpen || _typing || _focusInPopup || _dimmerShown
                                             || _focusOnControl || _panelOpen || splash.waiting
    // A side panel — the task document or a meeting — is open over the view.
    // The view behind it is under a scrim and takes no keys: Delete deleted
    // the open task, e archived it, Ctrl+A then Delete took every card, and
    // j/k/Space moved a cursor nobody could see (IDIOT-DOC-2/3, PERSONA-9).
    // The keys of the task itself (d, t, 1-4, s, i, y…) act on the open task.
    readonly property bool _panelOpen: taskDoc.opened || eventEditor.opened
    readonly property bool _focusInPanel: {
        for (let p = win.activeFocusItem; p; p = p.parent)
            if (p === taskDoc || p === eventEditor) return true;
        return false;
    }
    // The keys that belong to the open task while the keyboard is in its
    // document; the rest of the keymap waits until it closes.
    readonly property var _docTaskKeys: ["task.done", "task.timer", "task.priority0", "task.priority1", "task.priority2",
                                         "task.priority3", "task.schedule", "task.due", "task.copyId", "task.copyBranch",
                                         "task.copyLink", "task.openExternal", "task.rename", "task.createBranch", "task.moveProfile",
                                         "search.focus", "hotkeys.open"]
    // Global shortcuts (switch view, new task, palette, undo…) stand down
    // behind a modal: Ctrl+3 used to switch the view under an open task
    // editor, Ctrl+K opened the palette over a dialog. The side-rail
    // popovers are not modal and keep them.
    readonly property bool _modalOpen: taskEditor.opened || AppController.immersion
        || personEditor.opened || profileEditor.opened
        || cmdPalette.opened || quickCapture.opened || quickCaptureNotes.opened || eventCapture.opened || cheatSheet.opened
        || closeAsk.opened || goToDatePopup.opened || eventLog.opened || profileMoveDialog.opened
        || (_focusInPopup && !_focusInPopover) || _dimmerShown
        // The crash card waits for an answer over the whole window (A7-2).
        || splash.waiting
    readonly property bool _globalKeysOn: !hotkeys.isCapturing && !_modalOpen
    // A coach mark (CoachMarkHost) opens only now: nobody typing, no focus
    // mode, no modal or menu up. One already open stays. One about the view
    // also waits while a side panel covers it; one in the panel does not.
    readonly property bool coachMarksQuietInPanel: coachMarksAwake && !_typing && !AppController.immersion && !_modalOpen
                                                   && !_overlayOpen && !hotkeys.isCapturing && !splash.waiting
    // Never on startup: what the first pull or the start view brings waits
    // until the app has been up a while.
    property bool coachMarksAwake: false
    Timer {
        interval: 10000
        running: !win.coachMarksAwake
        onTriggered: win.coachMarksAwake = true
    }
    readonly property bool coachMarksQuiet: coachMarksQuietInPanel && !_panelOpen

    // Esc in the header search with nothing left to clear hands the keyboard
    // back to the view, so J/K and Return work again without the mouse.
    // A view that knows better where typing should go (the note editor, a
    // list's current row) says so with takeFocus().
    function focusActiveView() {
        // An open panel is where the keyboard goes back to, not the view
        // under its scrim: after any menu or popup the keys landed on the
        // board and Esc could no longer close the document (IDIOT-DOC-4/7/8).
        if (eventEditor.opened) { eventEditor.takeFocus(); return; }
        if (taskDoc.opened) { taskDoc.takeFocus(); return; }
        const v = win.activeViewItem();
        if (v && typeof v.takeFocus === "function") v.takeFocus();
        else if (v) v.forceActiveFocus();
        else win.contentItem.forceActiveFocus();
    }

    function _placePopover(pop, anchor) {
        const p = anchor.mapToItem(win.contentItem, 0, 0);
        const wantY = Math.max(8, Math.min(p.y, win.contentItem.height - pop.height - 8));
        pop.x = anchor.width + 6;
        pop.y = wantY - p.y;
    }
    function _togglePopover(pop, anchor) {
        if (pop.opened) {
            pop.close();
            return;
        }
        pop.parent = anchor;
        win._placePopover(pop, anchor);
        pop.open();
        // The Hotkeys panel sizes itself from its list's contentHeight, which is
        // still 0 on the very first open — re-clamp once it has a real height so
        // a bottom-anchored rail button can't push it off the window.
        Qt.callLater(function () { win._placePopover(pop, anchor); });
    }

    function activeCount() {
        return win._activeCount;
    }
    function scheduleMap() {
        const out = {};
        const m = AppController.events;
        const sel = AppController.selectedDate;
        for (let i = 0; i < m.rowCount(); i++) {
            const idx = m.index(i, 0);
            const date = m.data(idx, Qt.UserRole + 7);
            const taskId = m.data(idx, Qt.UserRole + 8);
            if (!taskId) continue;
            if (date && sel && date.getFullYear() === sel.getFullYear()
                    && date.getMonth() === sel.getMonth()
                    && date.getDate() === sel.getDate()) {
                out[taskId] = AppController.eventHourLabel(m.data(idx, Qt.UserRole + 4));
            }
        }
        return out;
    }
    property var _scheduleMap: scheduleMap()
    Connections {
        target: AppController.events
        function onRowsInserted() { win._scheduleMap = win.scheduleMap() }
        function onRowsRemoved()  { win._scheduleMap = win.scheduleMap() }
        function onDataChanged()  { win._scheduleMap = win.scheduleMap() }
        function onModelReset()   { win._scheduleMap = win.scheduleMap() }
    }
    // Bring the window to the foreground from any state (minimized, hidden to
    // tray, or merely unfocused). Shared by the two global-capture hotkeys and
    // the tray "Show" affordance.
    // Where a "seen this before" hint points (APP-159): the note, doc page or
    // task that already mentions the error, in its own profile.
    function openSeenBefore(hit) {
        if (!hit || !hit.id) return;
        if (hit.kind === "task") {
            // Edits to the task already open are not swapped out unasked, and
            // its profile is left only after that (PRES-1).
            taskEditor.settleThen(() => {
                if (hit.profileId && hit.profileId !== AppController.activeProfileId)
                    AppController.activeProfileId = hit.profileId;
                const t = AppController.taskById(hit.id);
                if (t && t.id) win.showTask(t);
            });
            return;
        }
        if (hit.profileId && hit.profileId !== AppController.activeProfileId)
            AppController.activeProfileId = hit.profileId;
        if (hit.kind === "note") {
            AppController.activeNoteId = hit.id;
            AppController.currentView = "notes";
        } else if (hit.kind === "docPage") {
            AppController.activeDocPageId = hit.id;
            AppController.currentView = "docs";
            notesBridge.requestedPage = hit.id;
        }
    }

    // Focus mode (APP-160) on the task in front of the user: the one open in
    // the editor, else whatever AppController picks (selection, branch).
    // The task in front of the user is the one open (document or editor),
    // else the one under the cursor, else the one whose timer runs: focus
    // mode said "no task chosen" with the cursor on a task (A1-24).
    function toggleImmersion() {
        AppController.toggleImmersion(win._immersionTaskId());
    }
    function _immersionTaskId() {
        if (taskDoc.opened && taskDoc.taskId) return taskDoc.taskId;
        if (taskEditor.opened && taskEditor._originalId) return taskEditor._originalId;
        if (AppController.selectionCount === 1) return AppController.selectedTaskIds[0];
        const cur = win._cursorTaskId();
        if (cur) return cur;
        const timer = AppController.runningTimer();
        return timer && timer.id ? String(timer.id) : "";
    }

    // The tasks a safety-net notice is about: one opens in the editor, several
    // narrow the board to them. Only what is shown changes.
    // A toast QML says itself; what went wrong or was refused also goes to
    // the event log (APP-187), as the C++ side's do.
    function notice(msg, kind) {
        toast.show(msg, kind);
        if (kind === "error" || kind === "warning") AppController.logEvent(kind, msg);
    }

    // "Show" on a sync's toast (APP-180): the board, filtered to the cards
    // that sync brought in.
    function showSyncNew() {
        AppController.currentView = "board";
        topBar.searchText = "is:new";
        win.searchText = topBar.searchText;
    }

    // An event-log entry was opened (APP-187): the task it names, the cards
    // of a sync, or the place it points at.
    function openLogEntry(entry) {
        const ids = (entry && entry.taskIds) || [];
        if (ids.length > 0) {
            const latest = AppController.syncNewTaskIds;
            if (entry.kind === "sync" && ids.length > 1 && ids.every(id => latest.indexOf(id) >= 0)) win.showSyncNew();
            else win.showSafetyTasks(ids);
            return;
        }
        if (entry && entry.route) win.runCommand(entry.route);
    }

    function showSafetyTasks(ids) {
        const list = (ids || []).filter(id => id && id.length > 0);
        if (list.length === 0) return;
        AppController.currentView = "board";
        if (list.length === 1) {
            const t = AppController.taskById(list[0]);
            if (t && t.id) win.showTask(t);
            return;
        }
        topBar.searchText = list.join(" OR ");
        win.searchText = topBar.searchText;
    }

    function _summon() {
        if (win._maximizeOnShow) {
            win._maximizeOnShow = false;
            win.showMaximized();
        }
        if (win.visibility === Window.Minimized || win.visibility === Window.Hidden || !win.visible)
            win.show();
        win.raise();
        win.requestActivate();
    }

    Connections {
        target: AppController
        function onSelectedDateChanged() { win._scheduleMap = win.scheduleMap() }
        // OS-level global hotkeys. With heap focused the popup opens in the
        // app as usual; from anywhere else only the capture window comes up —
        // the main window stays minimized, in the tray or behind other apps.
        function onQuickCaptureRequested() {
            if (win.active) quickCapture.open();
            else win._capture("task");
        }
        function onQuickCaptureNotesRequested() {
            if (win.active) quickCaptureNotes.open();
            else win._capture("note");
        }
        // Tray click / "Show heap." menu entry — just restore the window.
        function onShowWindowRequested() { win._summon(); }
        function onToast(msg, kind) { toast.show(msg, kind || "info") }
        // A sync brought new cards (APP-180): the toast names them, and
        // "Show" filters the board to them.
        function onSyncNews(msg, taskIds) {
            toast.showWithAction(msg, I18n.t("sync.show"), 10, function () { win.showSyncNew() });
        }
        // A safety-net notice (APP-157…): one toast, and "Show" takes the
        // board to the tasks it is about.
        function onSafetyNotice(kind, title, body, taskIds) {
            const msg = title.length > 0 ? title + " · " + body : body;
            // The end-of-day check opens the day's summary (APP-190).
            if (kind === "endOfDay")
                toast.showWithAction(msg, I18n.t("eod.open"), 15, function () { endOfDay.showNow() });
            else if (taskIds && taskIds.length > 0)
                toast.showWithAction(msg, I18n.t("safety.show"), 10, function () { win.showSafetyTasks(taskIds) });
            else
                toast.show(msg, "info");
        }
        // Focus mode ended: one toast with how much was held back, and a way
        // to see it. Nothing arrives unless asked for.
        function onImmersionEnded(held, minutes) {
            const msg = I18n.t("immersion.ended").arg(held);
            if (held > 0) {
                // A toast with an action is in the event log too (APP-225).
                AppController.logEvent("info", msg);
                toast.showWithAction(msg, I18n.t("immersion.showHeld"), 15, function () { AppController.releaseImmersionHeld() });
            }
            // Nothing held: nothing to say (DG-132).
        }
        function onSafetyOpenTasksRequested(taskIds) {
            win._summon();
            win.showSafetyTasks(taskIds);
        }
        // The third mouse use of an action that has a key: one quiet line,
        // never repeated for it (APP-166).
        function onShortcutHintRequested(shortcutId, sequence, label) {
            toast.show(I18n.t("hint.shortcut").arg(sequence).arg(label), "info")
        }
        // The tracker is read-only for this card (outside the filter, gone):
        // the drop was refused. Open the issue, or archive the card (APP-204).
        function onTrackerReadOnlyMove(taskId, msg, url) {
            toast.showWithActions(msg, [
                { label: I18n.t("tracker.readOnly.open"), fn: function () { if (url) Qt.openUrlExternally(url) } },
                { label: I18n.t("tracker.readOnly.archive"), fn: function () { AppController.setArchived(taskId, true) } }
            ], 10, "warning");
        }
        function onTrackerWriteAsk(taskId, key, title, tracker, providerId, from, to) {
            trackerWriteAsk.ask(taskId, key, title, tracker, providerId, from, to);
        }
        function onTrackerPushNeedsConfirm(taskId, key, title, tracker, remoteStatus, target) {
            trackerPushConfirm.ask(taskId, key, title, tracker, remoteStatus, target);
        }
        // Once, after the update that made tracker writes opt-in (APP-243).
        function onTrackerWriteNotice(msg) {
            toast.showWithAction(msg, I18n.t("tracker.writeNotice.open"), 20, function () {
                win.runCommand("settings:integrations")
            });
        }
        function onTrackerPushFailed(taskId, msg) {
            toast.showWithAction(msg, I18n.t("sync.retry"), 10, function () {
                AppController.retryTrackerPush(taskId)
            }, "error");
        }
        function onSettingsReset(msg) {
            toast.showWithAction(msg, I18n.t("undo.action"), 10, function () {
                AppController.undoSettingsReset()
            });
        }
        // "Next free window" found no room (APP-253): said as a fact, with
        // the next working day's window and this evening as choices. Nothing
        // is planned until one is pressed.
        function onFreeWindowMissing(taskId, title, date, nextDate, nextStart, lateStart) {
            const today = AppController.today;
            const isToday = date.getFullYear() === today.getFullYear() && date.getMonth() === today.getMonth()
                         && date.getDate() === today.getDate();
            const dayName = I18n.fmtDate(date, "weekdayDay");
            const msg = isToday ? I18n.t("freewin.none").arg(taskId) : I18n.t("freewin.noneOn").arg(taskId).arg(dayName);
            const acts = [];
            if (nextStart >= 0 && nextDate && nextDate.getFullYear) {
                const tomorrow = new Date(today.getFullYear(), today.getMonth(), today.getDate() + 1);
                const isTomorrow = nextDate.getFullYear() === tomorrow.getFullYear() && nextDate.getMonth() === tomorrow.getMonth()
                                && nextDate.getDate() === tomorrow.getDate();
                const label = isTomorrow ? I18n.t("freewin.tomorrowAt").arg(Theme.fmtHour(nextStart))
                                         : I18n.t("freewin.dayAt").arg(I18n.fmtDate(nextDate, "weekdayDay")).arg(Theme.fmtHour(nextStart));
                acts.push({ label: label, fn: function () { AppController.placeTaskAt(taskId, nextDate, nextStart) } });
            }
            if (lateStart >= 0) {
                const late = isToday ? I18n.t("freewin.lateToday").arg(Theme.fmtHour(lateStart))
                                     : I18n.t("freewin.lateOn").arg(dayName).arg(Theme.fmtHour(lateStart));
                acts.push({ label: late, fn: function () { AppController.placeTaskAt(taskId, date, lateStart) } });
            }
            if (acts.length > 0) toast.showWithActions(msg, acts, 12);
            else toast.show(msg, "info");
        }
        // The start of a planned task block (APP-256, opt-in): its buttons.
        // A task's own "remind me" (APP-319): open it or put it off.
        function onReminderToast(notificationId, msg, kind) {
            // "Кому написать" at its time (dogfood 2026-10-20).
            if (kind === "ping") {
                toast.showWithActions(msg, [
                    { label: I18n.t("reminder.open"), fn: function () { AppController.reminderAction(notificationId, "open") } },
                    { label: I18n.t("today.wrote"), fn: function () { AppController.reminderAction(notificationId, "wrote") } },
                    { label: I18n.t("reminder.snooze"), fn: function () { AppController.reminderAction(notificationId, "snoozeShort") } }
                ], 15);
                return;
            }
            if (kind === "remind") {
                toast.showWithActions(msg, [
                    { label: I18n.t("reminder.open"), fn: function () { AppController.reminderAction(notificationId, "open") } },
                    { label: I18n.t("reminder.snooze"), fn: function () { AppController.reminderAction(notificationId, "snoozeShort") } }
                ], 15);
                return;
            }
            toast.showWithActions(msg, [
                { label: I18n.t("reminder.open"), fn: function () { AppController.reminderAction(notificationId, "open") } },
                { label: I18n.t("reminder.snooze15"), fn: function () { AppController.reminderAction(notificationId, "snooze15") } },
                { label: I18n.t("reminder.window"), fn: function () { AppController.reminderAction(notificationId, "nextWindow") } }
            ], 15);
        }
        function onPingOpenRequested(id) { peopleDialog.showFor(id); }
        function onUndoableToast(msg, secs) {
            // Undo takes back the action this toast names — not whatever was
            // done last, which after a silent reorder is something else.
            const serial = AppController.undoSerialForToast();
            toast.showUndo(msg, I18n.t("undo.action"), secs, function () {
                AppController.undoEntry(serial)
            }, "success", AppController.shortcutText("undo"));
        }
        // No history left behind an Undo toast: it stops offering the button
        // (IDIOT-SHELL-4).
        function onPendingUndoChanged() {
            if (!AppController.hasPendingUndo) toast.dropUndo();
        }
        // "Done" with no column of that stage: a card with the two ways out
        // (APP-268, R3-137).
        function onDoneColumnMissing() {
            doneColumnCard.open();
        }
        // A newer release was found. When heap can update this copy itself
        // the action downloads it (APP-125); otherwise it opens the release page.
        // The sidebar says it in one quiet line (X/N-Ntf-OS, R2-053):
        // "0.8.1 доступна · обновить · что нового", then "готова ·
        // перезапустить". No toast; Settings → About has it too.
        function onUpdateAvailable(version, url) {
            rail.updateVersion = String(version).replace(/^v/, "");
        }
        function onUpdateReadyToInstall(version, sha256) {
            rail.updateVersion = String(version).replace(/^v/, "");
        }
    }

    GridLayout {
        anchors.fill: parent
        columns: 2
        rows: 1
        columnSpacing: 0
        rowSpacing: 0

        // The heap 2 sidebar (APP-258).
        Sidebar {
            id: rail
            Layout.row: 0; Layout.column: 0
            Layout.fillHeight: true
            expanded: win.sideRailExpanded
            firstRun: {
                const t = viewLoader.item as TodayView;
                return !!t && t.firstRun;
            }
            onToggleRequested: win.toggleSideRail()
            onNewTaskRequested: win.newTask("")
            // Every source and how it is doing (R2-048).
            onSyncStatusRequested: syncPopover.openAt(rail.profileSwitcher)
            onTimerTaskRequested: (id) => win.showTask(AppController.taskById(id))
            onPersonRequested: (id) => peopleDialog.showFor(id)
            onPersonEditRequested: (id) => personEditor.showFor(AppController.personById(id))
            onPersonAddRequested: personPicker.open_()
            onUpdateNotesRequested: AppController.openLatestRelease()
            onNewProfileRequested: profileEditor.showCreate()
            onRenameProfileRequested: {
                const list = AppController.profiles;
                const id = AppController.activeProfileId;
                for (let i = 0; i < list.length; i++) if (list[i].id === id)
                    profileEditor.showRename(list[i].id, list[i].name, list[i].color);
            }
            onDuplicateProfileRequested: {
                const list = AppController.profiles;
                const id = AppController.activeProfileId;
                for (let i = 0; i < list.length; i++) if (list[i].id === id)
                    profileEditor.showDuplicate(list[i].id, list[i].name, list[i].color);
            }
            onExportJsonRequested: {
                exportJsonDialog.currentFile = "file:///" + (
                    (AppController.activeProfileId || "profile") + ".todocpp.json"
                );
                exportJsonDialog.open();
            }
            onImportJsonRequested: importJsonDialog.open()
            onImportIcsRequested: importIcsDialog.open()
            onImportVaultRequested: importVaultDialog.open()
            onRemoveExampleRequested: win.removeExample()
            onExportVaultRequested: exportVaultDialog.open()
            onExportIcsRequested: {
                exportIcsDialog.currentFile = "file:///" + (
                    (AppController.activeProfileId || "tabless") + ".ics"
                );
                exportIcsDialog.open();
            }

            onOpenHotkeys: (anchor) => win._togglePopover(hotkeys, anchor)
            activeSavedViewId: savedViewsHost.activeView ? savedViewsHost.activeId : ""
            savedViewModified: savedViewsHost.modified
            onSavedViewActivated: (id) => savedViewsHost.toggle(id)
            onSavedViewRenameRequested: (id) => savedViewsHost.openRename(id)
            onSavedViewUpdateRequested: (id) => savedViewsHost.updateFromCurrent(id)
            onSavedViewEditRequested: (id) => savedViewsHost.openEdit(id)
            onSaveViewRequested: savedViewsHost.openSave()
        }

        // Main column: filter bar + active view
        Item {
            id: mainColumn
            objectName: "main-column"
            Layout.row: 0; Layout.column: 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // state.json unreadable / from a newer heap / not saving.
                StorageBanner { Layout.fillWidth: true }

            TopBar {
                id: topBar
                Layout.fillWidth: true
                // The tasks and knowledge sections; Today and Settings carry
                // their own titles.
                // Knowledge is one screen with its own header (APP-269,
                // DG-070): no lens tabs there.
                visible: section === "tasks"
                section: AppController.currentSection
                view: AppController.currentView
                onLensSelected: (id) => win.openLens(id)
                // Board: the sort; List: the grouping (APP-262/263).
                option: view === "board" ? win._sortOption : view === "list" ? win._groupOption : null
                onOptionPicked: (id) => {
                    if (topBar.view === "board") win.boardSortMode = id;
                    else win.setListGroupBy(id);
                }
                onZoomSelected: (id) => AppController.currentView = id
                // The header writes its own query as chips are added and
                // removed, so a binding would break on the first one: follow
                // the window's query instead (a saved view, a link, a reset).
                Component.onCompleted: topBar.searchText = win.searchText
                Connections {
                    target: win
                    function onSearchTextChanged() {
                        if (topBar.searchText !== win.searchText) topBar.searchText = win.searchText;
                    }
                }
                // Typing reaches the views once the keys pause, not per key: a
                // keystroke that swaps most rows on a 3k-task board rebinds every
                // visible card (~100 ms), and a quick "priority:p0" used to pay
                // that for each intermediate state, including "priority:p" — which
                // matches nothing. The field itself updates instantly.
                onSearchTextChanged: searchApply.restart()
                Timer {
                    id: searchApply
                    interval: 120
                    onTriggered: win.searchText = topBar.searchText
                }
                onLeaveRequested: win.focusActiveView()
                onSeenBeforeActivated: (hit) => win.openSeenBefore(hit)
                resultCount: section === "tasks" ? filterBar._fc.total : -1
                // Ctrl S in the field (A6-25): the typed filter applies first.
                onSaveViewRequested: {
                    searchApply.stop();
                    win.searchText = topBar.searchText;
                    savedViewsHost.openSave();
                }
                profileName: rail.profileSwitcher.active.name || ""
                onProfileChipClicked: rail.profileSwitcher.openMenu()
            }

                TrackerStrip {
                    id: trackerStrip
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.pagePadX
                    Layout.rightMargin: Theme.pagePadX
                    Layout.topMargin: visible ? Theme.spSm : 0
                    Layout.bottomMargin: visible ? Theme.spSm : 0
                    visible: AppController.currentSection === "tasks" && (rows.length > 0 || firstLoads.length > 0)
                    onSignInRequested: win.runCommand("settings:integrations")
                    onLogRequested: eventLog.open()
                }

                FilterBar {
                    id: filterBar
                    Layout.fillWidth: true
                    // The conditions and the count moved to the query row
                    // under the Tasks header (APP-261).
                    slim: AppController.currentSection === "tasks"
                    // Archive brings its own header and its own counter, and the
                    // fall-through label used to caption it "Docs". Board and
                    // List (APP-262/263) show it only for an applied saved
                    // view (its chip, update, save as new): their one setting
                    // (sort, grouping) sits beside the lens tabs, the archive
                    // is the query's "is:archived", saving is the header's.
                    // The calendar zooms too (DG-040): the grid starts right
                    // under the header; the archive is "is:archived" there.
                    readonly property bool _lensView: AppController.currentView === "board"
                                                      || AppController.currentView === "list"
                                                      || AppController.currentView === "day"
                                                      || AppController.currentView === "week"
                                                      || AppController.currentView === "month"
                    visible: (!_lensView || !!savedViewsHost.activeView)
                          && AppController.currentView !== "today"
                          && AppController.currentView !== "notes"
                          && AppController.currentView !== "docs"
                          && AppController.currentView !== "settings"
                    viewLabel: AppController.currentView === "day" ? I18n.t("calzoom.day")
                             : AppController.currentView === "week" ? I18n.t("siderail.week")
                             : AppController.currentView === "month" ? I18n.t("siderail.month")
                             : I18n.t("siderail.board")
                    priorities: win.prioritiesFilter
                    // Under the filters the bar itself shows (TASKS-20): the
                    // counts used to include archived tasks and ignore the
                    // search and the priority chips.
                    // The board keeps done cards in its folded Done column under the
                    // default "not done", so its count includes them: "0 tasks"
                    // over a Done column of 42 (EYE-27).
                    readonly property var _fc: AppController.filteredCounts(
                        AppController.currentView === "board" ? win.searchText.replace(/(^|\s)is:open(?=\s|$)/gi, " ").trim() : win.searchText,
                        win._activePriorities, win.tasksShowArchived, false, win._counts)
                    totalCount: _fc.total
                    activeCount: _fc.active
                    blockedCount: _fc.blocked
                    reviewCount: _fc.review
                    showArchived: win.showArchived
                    showSort: false
                    showArchivedToggle: !_lensView
                    sortMode: win.boardSortMode
                    // The weekly recap (APP-211) is in the palette
                    // (recap.open); the heap 2 board has no bar for it
                    // (APP-262, sheet H2-Board).
                    showRecap: false
                    recapUnseen: weeklyRecap.unseen
                    onRecapRequested: win.runCommand("recap.open")
                    onSortModeRequested: (mode) => win.boardSortMode = mode
                    onTogglePriority: (p) => {
                        const next = Object.assign({}, win.prioritiesFilter);
                        next[p] = !next[p];
                        win.prioritiesFilter = next;
                    }
                    onClearPriorities: win.prioritiesFilter = ({})
                    onToggleArchived: win.showArchived = !win.showArchived
                    savedViewName: savedViewsHost.activeView ? savedViewsHost.activeView.name : ""
                    savedViewModified: savedViewsHost.modified
                    onSaveViewRequested: savedViewsHost.openSave()
                    onSaveAsNewRequested: savedViewsHost.openSave()
                    onUpdateViewRequested: savedViewsHost.updateActive()
                    onLeaveViewRequested: savedViewsHost.clear()
                }
                Item {
                    id: viewArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // A view wider than its column is cut at the column, not
                    // drawn under the right panel (SCALE-3).
                    clip: true
                    // Board and Knowledge are kept alive once visited.
                    // Swapping a Loader's sourceComponent destroys the item,
                    // and these hold state the user notices losing: the
                    // board's scroll position and column focus, the note's
                    // caret, scroll and editor undo history. Everything else
                    // is cheap to rebuild and stays on the shared loader below.
                    //
                    // They load lazily — `active` is flipped on first visit —
                    // so starting on the board does not build the notes
                    // editor too.
                    Loader {
                        id: boardLoader
                        anchors.fill: parent
                        visible: AppController.currentView === "board"
                        active: false
                        sourceComponent: boardComp
                        onVisibleChanged: if (visible) win.fadeInView(boardLoader)
                    }
                    Loader {
                        id: notesLoader
                        anchors.fill: parent
                        visible: AppController.currentView === "notes" || AppController.currentView === "docs"
                        active: false
                        sourceComponent: notesComp
                        onVisibleChanged: if (visible) win.fadeInView(notesLoader)
                    }

                    Loader {
                        id: viewLoader
                        anchors.fill: parent
                        visible: !boardLoader.visible && !notesLoader.visible
                        onVisibleChanged: if (visible) win.fadeInView(viewLoader)
                        onLoaded: if (visible) win.fadeInView(viewLoader)
                        sourceComponent: {
                            if (AppController.currentView === "today") return todayComp;
                            if (AppController.currentView === "list") return listComp;
                            if (["day", "week", "month"].indexOf(AppController.currentView) >= 0) return calendarComp;
                            if (AppController.currentView === "settings") return settingsComp;
                            return null;
                        }
                    }

                    // The list (APP-263) gets the window's query from here,
                    // outside its Component, and hands its clicks back.
                    Binding { when: win._listOn; target: viewLoader.item; property: "searchText"; value: win.searchText }
                    Binding { when: win._listOn; target: viewLoader.item; property: "prioritiesFilter"; value: win.prioritiesFilter }
                    Binding { when: win._listOn; target: viewLoader.item; property: "showArchived"; value: win.tasksShowArchived }
                    Binding { when: win._listOn; target: viewLoader.item; property: "groupBy"; value: win.listGroupEffective }
                    // "show them" under an all-done list (EYE-27): the default
                    // "not done" lets go. Wired here, not in listComp, where
                    // win is out of qmllint's sight.
                    Connections {
                        target: win._listOn ? viewLoader.item : null
                        ignoreUnknownSignals: true
                        function onShowDoneRequested() { win.searchText = win.searchText.replace(/(^|\s)is:open(?=\s|$)/gi, " ").trim(); }
                    }
                    Connections {
                        target: viewLoader.item as TaskListView
                        ignoreUnknownSignals: true
                        function onTaskClicked(id) { win.showTask(AppController.taskById(id)); }
                    }

                    // Today's day hands its clicks up here, where the editors are.
                    Connections {
                        target: viewLoader.item as TodayView
                        ignoreUnknownSignals: true
                        function onEventClicked(id, occurrence) { win.openEvent(id, occurrence); }
                        function onCreateRequested(startHour, endHour, day) { win.createEventAt(startHour, endHour, day); }
                        function onTaskClicked(id) { win.openTask(id); }
                        function onOtherProfileTaskClicked(id, profileId) { win.openInProfile(id, profileId); }
                        function onOtherProfileKeys(id, profileName) { win.notice(I18n.t("today.otherProfileKeys").arg(id).arg(profileName)); }
                        function onUndatedRequested() { win.showQuery("is:undated", "list"); }
                        function onOverdueRequested() { win.showQuery("due:overdue", "list"); }
                        function onDeadlinesRequested(query) { win.showQuery(query, "list"); }
                        function onRecapRequested() { win.runCommand("recap.open"); }
                        // The first-run screen (APP-271).
                        function onFirstTaskCreated(id) { win.notice(quickCapture.headline(id)); }
                        function onConnectRequested() { win.runCommand("settings:integrations"); }
                        function onImportRequested() { importVaultDialog.open(); }
                        function onExampleRequested() { win.openExample(); }
                        function onPeopleRequested(id) { peopleDialog.showFor(id); }
                        function onPersonEditRequested(id) { personEditor.showFor(AppController.personById(id)); }
                        function onPersonAddRequested() { personPicker.open_(); }
                    }

                    // First visit to one of the kept-alive views builds it.
                    // The function lives on `win` because a Connections handler
                    // does not resolve names from the scope its parent item
                    // declares them in — calling it unqualified from there is a
                    // ReferenceError, and the two views would never activate.
                    Connections {
                        target: AppController
                        function onCurrentViewChanged() {
                            // The document belongs to the section it was
                            // opened in: another section closes it (DG-064).
                            if (AppController.currentSection !== win._docSection && taskDoc.opened) taskDoc.close();
                            // The meeting panel too: it stayed over Knowledge (IDIOT-DOC-18).
                            if (AppController.currentSection !== win._docSection && eventEditor.opened) eventEditor.close();
                            win._docSection = AppController.currentSection;
                            win.activateCurrentView();
                            Qt.callLater(win._focusSwitchedView);
                        }
                    }
                    Component.onCompleted: win.activateCurrentView()
                    // Under an open panel the view steps back and a click on
                    // it closes the panel, as on a sheet over a page: the
                    // board read as live and a click there did nothing.
                    Rectangle {
                        id: panelScrim
                        objectName: "panel-scrim"
                        readonly property bool shown: (taskDoc.opened && !taskDoc.full) || eventEditor.opened
                        // Over the whole content area, the header and the
                        // query row too, as the panel now is (APP-314): they
                        // stayed live beside it.
                        parent: mainColumn
                        anchors.fill: parent
                        z: 59
                        color: Theme.withAlpha(Theme.scrim, Theme.dark ? 0.5 : 0.28)
                        visible: opacity > 0
                        opacity: shown ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.durPop; easing.type: Theme.easeEnter } }
                        MouseArea {
                            anchors.fill: parent
                            enabled: panelScrim.shown
                            // The view under it is not live: its cards lit up
                            // and opened their details under the pointer.
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: {
                                if (eventEditor.opened) eventEditor.close();
                                if (taskDoc.opened) taskDoc.close();
                            }
                            onWheel: (wheel) => wheel.accepted = true
                        }
                    }
                    // A view switched to fades in at pop speed (APP-175):
                    // only in, never out, so a hotkey still lands at once.
                    NumberAnimation {
                        id: viewFade
                        property: "opacity"
                        from: 0; to: 1
                        duration: Theme.durPop
                        easing.type: Theme.easeEnter
                    }
                    // The task document (APP-265): a panel over the right
                    // half of the content area, or the whole width.
                    // Either way it runs the content area's full height, the
                    // Tasks header and the query row included (DG-060; the
                    // panel too since APP-314: on a small screen those rows
                    // took the height the description needed).
                    TaskDocument {
                        id: taskDoc
                        parent: mainColumn
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        // Never so narrow the meta column folds away (0.9.4).
                        width: taskDoc.full ? parent.width
                             : Math.min(parent.width, Math.max(Theme.px(820), parent.width * 0.5))
                        z: 60
                        onClosed: Qt.callLater(win.focusActiveView)
                        onInternalLinkActivated: (kind, target) => win.followMdLink(kind, target)
                        onOpenedChanged: if (taskDoc.opened) eventEditor.close()
                    }
                    // A meeting (DG-120): the same panel, saving on its own.
                    EventEditor {
                        id: eventEditor
                        // Over the scrim, which covers the content area.
                        parent: mainColumn
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: Math.min(parent.width, Theme.px(480))
                        z: 61
                        onOpenedChanged: if (eventEditor.opened) taskDoc.close()
                        onClosed: Qt.callLater(win.focusActiveView)
                        onTaskRequested: (id) => win.openTask(id)
                    }
                    SelectionBar {
                        id: selectionBar
                        restoring: win._archiveQuery
                        onCommandRequested: (id) => win.runCommand(id)
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Theme.sp2xl
                        z: 50
                    }
                }
                Component {
                    id: boardComp
                    KanbanBoard {
                        searchText: win.searchText
                        prioritiesFilter: win.prioritiesFilter
                        scheduleMap: win._scheduleMap
                        showArchived: win.tasksShowArchived
                        sortMode: win.boardSortMode
                        onTaskClicked: (id) => win.showTask(AppController.taskById(id))
                        onCreateInStatus: (s) => win.newTask(s)
                        onResetFilterRequested: win.resetTaskFilter()
                    }
                }
                Component {
                    id: todayComp
                    TodayView {}
                }
                Component {
                    id: listComp
                    TaskListView {
                        onResetFilterRequested: win.resetTaskFilter()
                    }
                }
                // The Calendar lens (APP-264): Day / Week / Month as one zoom,
                // with the "Without a date" tray. The zoom follows the view id,
                // so the grid is kept across Day <-> Week.
                Component {
                    id: calendarComp
                    CalendarView {
                        zoom: AppController.currentView
                        searchText: win.searchText
                        prioritiesFilter: win.prioritiesFilter
                        showArchived: win.tasksShowArchived
                        onTaskClicked: (id) => win.showTask(AppController.taskById(id))
                        onEventClicked: (id, occurrence) => win.openEvent(id, occurrence)
                        // An empty slot or a dragged stretch opens the editor on
                        // a draft: the meeting is named before it exists.
                        onCreateRequested: (startHour, endHour, day) => {
                            eventCapture.openAt({ date: day, start: startHour, end: endHour });
                        }
                        onDayRequested: (day) => {
                            AppController.selectedDate = day;
                            AppController.currentView = "day";
                        }
                        onTaskCaptureRequested: (text) => quickCapture.openWithText(text)
                        // A block's "Запланировать…" and s on a tray row
                        // (A1-19): the "when" field.
                        onScheduleRequested: (id) => win.scheduleFromCalendar(id)
                        onResetFilterRequested: win.resetTaskFilter()
                    }
                }
                Component {
                    id: notesComp
                    NotesView {
                        mode: AppController.currentView === "docs" ? "docs" : "notes"
                        // A palette hit in a note asks for the line its
                        // section starts on; clearing it afterwards lets the
                        // same line be requested twice.
                        jumpToLine: notesBridge.requestedLine
                        onJumpConsumed: notesBridge.requestedLine = -1
                        // A doc page or a catalogue entry found in Ctrl+K (DG-070).
                        requestedPage: notesBridge.requestedPage
                        onPageConsumed: notesBridge.requestedPage = ""
                        requestedFilter: notesBridge.requestedFilter
                        onFilterConsumed: notesBridge.requestedFilter = ""
                        onTaskRequested: (id) => win.openTaskById(id)
                        onPersonRequested: (id) => personEditor.showFor(AppController.personById(id))
                        onImportRequested: importVaultDialog.open()
                        // Its keys stand down behind a panel or a dialog (IDIOT-KNOW-4).
                        keysLive: !win._panelOpen && !win._modalOpen
                    }
                }
                Component {
                    id: settingsComp
                    SettingsView {
                        // Expose a "bus" the inner buttons can hit to open
                        // popups owned by Main (HotkeysPanel + FileDialog).
                        property var settingsBus: QtObject {
                            function openHotkeys() { rail.openHotkeys(rail.hotkeysAnchor) }
                            function exportJson()  { exportJsonDialog.open() }
                            function importJson()  { importJsonDialog.open() }
                            function openTimeMachine() { timeMachine.showNow() }
                            function openReportIssue() { reportIssue.showNow() }
                            function openWhatsNew() { return whatsNew.showNow() }
                            function openCheatSheet() { win.openCheatSheet() }
                            function exportMarkdown() { exportVaultDialog.open() }
                            // A key being rebound in place (Settings → Keys):
                            // the global shortcuts stand down as they do for
                            // the hotkeys panel.
                            function setKeyCapture(on) { hotkeys.capturingId = on ? "settings" : "" }
                        }
                    }
                }
            }
        }
    }

    TaskEditor    {
        id: taskEditor
        objectName: "task-editor"
        onSeenBeforeActivated: (hit) => win.openSeenBefore(hit)
        // Ctrl+click on a note, a task, a tag in the description.
        onDescLinkActivated: (kind, target) => win.followMdLink(kind, target)
    }
    PeopleDialog {
        id: peopleDialog
        onEditRequested: (id) => personEditor.showFor(AppController.personById(id))
        onAddRequested: personPicker.open_()
        onTaskRequested: (id) => win.openTask(id)
        onMeetingRequested: (id) => eventEditor.showForId(id)
    }
    PersonPicker  {
        id: personPicker
        onDraftRequested: (draft) => personEditor.showFor(draft)
    }
    // A new meeting from one line (DG-120); it opens in the panel once made.
    EventCapture {
        id: eventCapture
        onCreated: (id) => eventEditor.showForId(id)
    }
    PersonEditor  { id: personEditor }
    ProfileEditor { id: profileEditor }
    // After a "delete all data" reset the controller rebuilds a fresh install;
    // back to Today, which is the first-run screen again (APP-271).
    Connections {
        target: AppController
        function onFirstRunReset() {
            AppController.openSection("today");
        }
        // "С чего начать" (welcome.replay): the guide, not a tour (DG-131).
        function onWelcomeReplayRequested() {
            win.openGuide();
        }
    }
    // A Tasks filter that found nothing: "сбросить фильтр · Esc" (DG-160)
    // clears the query and the priority filter.
    // Back to the plain task list: the default "status not done" stays (the
    // reset dropped it and done tasks flooded in, IDIOT-TASKS-5), and a saved
    // view is left rather than shown as modified.
    function resetTaskFilter() {
        savedViewsHost.leave();
        win.searchText = win.defaultQuery("", false);
        win.prioritiesFilter = ({});
    }
    // Ctrl+2, g b and Esc on the board leave a saved view: only the chip's ×
    // did, so there was no keyboard way back (PERSONA-16).
    function leaveSavedView() {
        if (savedViewsHost.activeView) win.resetTaskFilter();
    }
    readonly property bool _taskFilterEmpty: {
        const v = AppController.currentSection === "tasks" ? win.activeViewItem() : null;
        return !!v && v.nothingFound === true;
    }
    // Words typed with "/" are still on the board after Enter: Esc lets go of
    // them as it does when they find nothing (A4-17). A selection keeps its
    // own Esc first; the words go before the board cursor does.
    readonly property bool _taskFilterTyped: AppController.currentSection === "tasks"
                                             && String(win.searchText || "").trim() !== win.defaultQuery("", false)
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: (win._taskFilterEmpty || win._taskFilterTyped
                  || (!!savedViewsHost.activeView && AppController.currentSection === "tasks"))
                 && !win._viewKeysBlocked && !win._focusOnControl && !AppController.immersion
                 && AppController.selectionCount === 0
        onActivated: win.resetTaskFilter()
    }
    // The guide ("С чего начать") in its reader over Settings. The welcome
    // tour is gone (DG-131): empty states and the first-run hero teach.
    function openGuide() {
        AppController.currentView = "settings";
        Qt.callLater(() => {
            const v = win.activeViewItem();
            if (v && v.openHelp)
                v.openHelp("");
        });
    }

    QuickCapturePopup {
        id: quickCapture
        // One toast for what was made (APP-266): "Created ID · when · column",
        // Open / Undo; a task the filters on screen hide says so.
        onCaptured: (title, body, taskId) => win._toastCaptured(title, taskId)
        onOpenFullRequested: (draft) => win.showTask(draft)
        onOpenTaskRequested: (id) => win.openTask(id)
        onSeenBeforeActivated: (hit) => {
            quickCapture.close();
            win.openSeenBefore(hit);
        }
    }
    QuickCaptureNotesPopup {
        id: quickCaptureNotes
        onCaptured: (title, body, taskId) => toast.show(title + " — " + body)
    }

    // The same two popups, for a capture made from outside heap. Created on
    // first use: most sessions never need a second window.
    function _capture(mode) {
        captureLoader.active = true;
        const cw = captureLoader.item as CaptureWindow;
        cw.hostScreen = win.screen;
        cw.summon(mode);
    }
    Loader {
        id: captureLoader
        active: false
        sourceComponent: CaptureWindow {
            // Nothing of heap is on screen to show it, so the confirmation is
            // an OS notification; clicking it opens the task.
            onCaptured: (title, body, taskId) => AppController.notifyCapture(taskId, title, body)
        }
    }
    // Outside the component, so `win` is in scope for qmllint (APP-159).
    Connections {
        target: captureLoader.item
        ignoreUnknownSignals: true
        function onSeenBeforeActivated(hit) {
            win._summon();
            win.openSeenBefore(hit);
        }
        // The zoom keys stand down while it has the keyboard (APP-168).
        // Window.active cannot tell for sure, so the capture window's own
        // state is read. Hiding it does not always say it went inactive,
        // hence both.
        function onActiveChanged() {
            const cw = captureLoader.item as CaptureWindow;
            win._captureActive = !!cw && cw.visible && cw.active;
        }
        function onVisibleChanged() {
            const cw = captureLoader.item as CaptureWindow;
            win._captureActive = !!cw && cw.visible && cw.active;
        }
    }

    // GitWatcher → TaskEditor bridge: TopBar "Open" button on the focus
    // banner emits openTaskRequested; route it through the same showFor()
    // path used by Kanban / Timeline / palette.
    Connections {
        target: AppController
        function onOpenTaskRequested(taskId, profileId) {
            // Also reached from a notification click while heap is in the tray.
            win._summon();
            // Edits to the task already open are not swapped out unasked
            // (TASKS-18), and the task's profile becomes the active one only
            // after that: switched first, the open editor saved into the
            // other profile (PRES-1).
            taskEditor.settleThen(() => {
                if (profileId && profileId !== AppController.activeProfileId)
                    AppController.activeProfileId = profileId;
                win.showTask(AppController.taskById(taskId));
            });
        }
        // A task menu's "Move to profile…" (A5-14).
        function onMoveToProfileRequested(ids) { win.moveToProfile(ids); }
        // "Open" on a meeting / standup reminder (APP-155): that day in the
        // week view, and the meeting itself when it is a stored event.
        function onOpenEventRequested(eventId, date) {
            win._summon();
            AppController.selectedDate = date;
            AppController.currentView = "week";
            if (eventId) eventEditor.showForId(eventId);
        }
    }

    // What a palette command does. Ids are the shortcut catalog's, so the
    // palette offers exactly what the keys do; "settings:<section>" opens a
    // Settings section, and a few have no key of their own.
    // Settings, opened on one setting: scrolled to and focused (APP-210).
    function openSettingsItem(item) {
        AppController.currentView = "settings";
        Qt.callLater(function () {
            const v = win.activeViewItem();
            if (v && v.revealItem) v.revealItem(item);
        });
    }
    // A task opens as a document (APP-265); a draft is made real first, and
    // one with no title yet still goes to the editor.
    property string _docSection: AppController.currentSection
    function showTask(t) {
        if (!t) return;
        // Made just now from a capture (Tab): the title is what was being
        // typed, so the caret stays in it.
        const fresh = !!t._isNew;
        if (t._isNew) {
            const d = Object.assign({}, t);
            if (String(d.title || "").trim().length === 0 || !AppController.saveTask(d)) {
                taskEditor.showFor(d);
                return;
            }
            t = d;
        }
        if (t.id) taskDoc.open(t.id, fresh);
    }
    // The example profile (APP-271): opened, it is the active profile and
    // Today shows its day; a second "Open the example" only switches to it.
    function openExample() {
        AppController.openExample();
        AppController.openSection("today");
        // Into the day, not the first-run input it replaced (VERIFY-B-1).
        Qt.callLater(win.focusActiveView);
    }
    // "Remove the example?" only when it was worked in; untouched, it goes.
    function removeExample() {
        const n = AppController.exampleChanges();
        if (n > 0) {
            removeExampleDialog.changes = n;
            removeExampleDialog.open();
        } else {
            AppController.removeExample();
        }
    }
    // To another profile (A5-14): the task menu, the selection bar, Ctrl K
    // and Shift M all come here. Nothing under the key, nothing happens; one
    // profile, it says why.
    function moveToProfile(ids) {
        if (!ids || ids.length === 0) return;
        if ((AppController.profiles || []).length < 2) {
            win.notice(I18n.t("moveProfile.title") + ": " + I18n.t("moveProfile.why.one"), "info");
            return;
        }
        profileMoveDialog.openFor(ids);
    }
    ProfileMoveDialog {
        id: profileMoveDialog
        onClosed: Qt.callLater(win.focusActiveView)
    }

    // On the small-dialog template (X-Dlg-Small, R4-063).
    SmallDialog {
        id: removeExampleDialog
        objectName: "remove-example-dialog"
        property int changes: 0
        title: I18n.t("example.remove.title")
        fact: I18n.t("example.remove.body") + " " + I18n.count(removeExampleDialog.changes, "example.remove.changed")
        onOpened: keepExample.forceActiveFocus()
        buttons: [
            PillButton {
                id: keepExample
                text: I18n.t("example.remove.keep")
                onClicked: removeExampleDialog.close()
            },
            PillButton {
                objectName: "remove-example-confirm"
                text: I18n.t("example.remove.confirm")
                danger: true
                onClicked: {
                    removeExampleDialog.close();
                    AppController.removeExample();
                }
            }
        ]
    }

    // A query in the Tasks search, on a lens (Today's "N without a date").
    function showQuery(q, lens) {
        topBar.searchText = q;
        win.searchText = q;
        AppController.openSection("tasks");
        win.openLens(lens);
    }
    // The day's hands, for Today and the day panel.
    function openEvent(id, occurrence) {
        // Today hands the occurrence over as its ISO date (IDIOT-CAL-1): the
        // series' row opened instead, and a change moved every past day too.
        if (typeof occurrence === "string") occurrence = win._occurrenceOf(id, occurrence);
        if (occurrence) eventEditor.showForOccurrence(occurrence);
        else eventEditor.showForId(id);
    }
    // The occurrence of meeting `id` (a single one, a series master or an
    // override) on ISO day `iso`, as the week opens it: a single meeting kept
    // in another zone carries that zone only here (A6-4); null when none.
    function _occurrenceOf(id, iso) {
        const p = String(iso).split("-");
        if (p.length !== 3) return null;
        const day = new Date(Number(p[0]), Number(p[1]) - 1, Number(p[2]));
        const list = AppController.eventOccurrences(day, day);
        for (let i = 0; i < list.length; i++) {
            const o = list[i];
            if (o.id === id || (String(o.masterId || "").length > 0 && o.masterId === id)) return o;
        }
        return null;
    }
    function createEventAt(startHour, endHour, day) {
        eventCapture.openAt({ date: day, start: startHour, end: endHour });
    }
    function openTask(id) {
        win.showTask(AppController.taskById(id));
    }
    // Another profile's task on Today (A1-17): opened in its own profile,
    // and the toast says the app went there.
    function openInProfile(id, profileId) {
        taskEditor.settleThen(() => {
            if (profileId && profileId !== AppController.activeProfileId) {
                AppController.activeProfileId = profileId;
                win.notice(I18n.t("today.openedIn").arg(AppController.profileById(profileId).name || "").arg(id));
            }
            win.showTask(AppController.taskById(id));
        });
    }
    // The tasks a key acts on: the selection, else the one under the
    // cursor or the pointer in the open view.
    function _keyTaskIds() {
        // With a panel open, never a card behind it: the open task, or none.
        if (win._panelOpen) return taskDoc.opened && win._focusInPanel ? [taskDoc.taskId] : [];
        if (AppController.selectionCount > 0) return AppController.selectedTaskIds;
        const v = win.activeViewItem();
        if (v && typeof v._actionCardId === "function") {
            const id = v._actionCardId();
            return id ? [id] : [];
        }
        if (v && v.hoveredTaskId) return [v.hoveredTaskId];
        return [];
    }
    // D (APP-268): nothing under the key, nothing happens. A second d within
    // half a second is Vim's "dd" habit, not "take it back" (keymap rule 6),
    // whichever card is under the key by then (IDIOT-TASKS-18).
    function markDone() {
        const ids = win._keyTaskIds();
        if (ids.length === 0) return;
        const key = ids.join(",");
        const now = Date.now();
        if (KeyRules.isRepeatedDone(key, now, win._lastDoneKey, win._lastDoneAt)) return;
        win._lastDoneKey = key;
        win._lastDoneAt = now;
        const v = win.activeViewItem();
        if (ids.length === 1 && AppController.currentView === "board" && v && typeof v.stepCursorOff === "function")
            v.stepCursorOff(ids[0]);
        AppController.toggleDone(ids);
    }
    // The cheat sheet ("?", Ctrl+/); changing keys is the Hotkeys panel.
    function openCheatSheet() {
        if (cheatSheet.opened) cheatSheet.close();
        else cheatSheet.open();
    }
    function _toastCaptured(title, taskId) {
        const line = taskId ? quickCapture.headline(taskId) : "";
        if (line.length === 0) {
            toast.show(title);
            return;
        }
        const serial = AppController.undoSerialForToast();
        const open = { label: I18n.t("capture.open"), fn: function () { win.openTask(taskId); } };
        const undo = { label: I18n.t("undo.action"), fn: function () { AppController.undoEntry(serial); } };
        const seen = AppController.taskInCurrentFilter(taskId) || AppController.currentSection !== "tasks";
        const msg = seen ? line : I18n.t("capture.done.hidden").arg(taskId);
        // It carries an Undo: the next action's Undo replaces it. A "Готово"
        // after three captures waited many seconds behind them (A2-28).
        toast.showWithActions(msg, [open, undo], 8, "info", 1);
    }
    // The calendar lens opens on the calendar last used (week or month).
    property string _calendarView: "week"
    Connections {
        target: AppController
        function onCurrentViewChanged() {
            const v = AppController.currentView;
            if (v === "day" || v === "week" || v === "month") win._calendarView = v;
            win._noteNav(v);
        }
    }
    function openLens(id) {
        if (id === "list") AppController.currentView = "list";
        else if (id === "calendar") AppController.currentView = win._calendarView;
        else AppController.currentView = id;
    }
    function runCommand(id) {
        // The selection bar's "Удалить": the same question as Del (A1-28).
        if (id === "selection.deleteSel") {
            win.deleteSelectedAsk();
            return;
        }
        // The calendar lens is week or month, the one used last (APP-272 g c).
        if (id === "view.calendar") {
            win.openLens("calendar");
            return;
        }
        if (id === "palette.open") {
            cmdPalette.open();
            return;
        }
        if (id.indexOf("section.") === 0) {
            if (id === "section.tasks") win.leaveSavedView();
            // Knowledge's key is the Notes row's (Docs has its own row).
            if (id === "section.knowledge") AppController.currentView = "notes";
            else AppController.openSection(id.slice(8));
            return;
        }
        if (id.indexOf("settings:") === 0) {
            const section = id.slice(9);
            AppController.currentView = "settings";
            Qt.callLater(function () {
                const v = win.activeViewItem();
                if (v && v.openSection) v.openSection(section);
            });
            return;
        }
        if (id === "view.archive") {
            win.openArchive();
            return;
        }
        // The selection's label and carry-on, off the bar since DG-026.
        if (id === "selection.label") { if (AppController.selectionCount > 0) selectionBar.openLabel(); return; }
        if (id === "task.someday") {
            const ids = win._keyTaskIds();
            if (ids.length === 0) win.notice(I18n.t("palette.cmd.someday.none"), "info");
            else AppController.carryTasks(ids, "someday");
            return;
        }
        if (id === "selection.carry") { if (AppController.selectionCount > 0) selectionBar.openCarry(); return; }
        // Off the profile menu since DG-151: the profile's import / export
        // and duplicate live in the command line.
        if (id === "profile.duplicate") { rail.duplicateProfileRequested(); return; }
        if (id === "ics.import") { rail.importIcsRequested(); return; }
        if (id === "ics.export") { rail.exportIcsRequested(); return; }
        if (id === "vault.import") { rail.importVaultRequested(); return; }
        if (id === "vault.export") { rail.exportVaultRequested(); return; }
        // Off the column menu since DG-025: the column under the board's
        // cursor (else the first one).
        if (id.indexOf("column:") === 0) {
            const b = boardLoader.item;
            if (b && b.columnCommand) b.columnCommand(id.slice(7));
            return;
        }
        if (id === "view.timeline") {
            AppController.currentView = "list";
            return;
        }
        if (id.indexOf("view.") === 0) {
            if (id === "view.board") win.leaveSavedView();
            AppController.currentView = id.slice(5);
            return;
        }
        if (id.indexOf("savedview:") === 0) {
            savedViewsHost.apply(id.slice(10));
            return;
        }
        if (id.indexOf("savedView.") === 0) {
            savedViewsHost.applyAt(Number(id.slice(10)));
            return;
        }
        // Notes actions run in the Notes view, so go there first (SHELL-1);
        // Docs is the same view and keeps them.
        if (id.indexOf("notes.") === 0) {
            if (AppController.currentView !== "docs") AppController.currentView = "notes";
            Qt.callLater(function () {
                const v = win.activeViewItem();
                if (v && v.runNotesCommand) v.runNotesCommand(id.slice(6));
            });
            return;
        }
        if (id === "savedview.save") {
            // After the palette has closed, so the dialog gets the keyboard.
            Qt.callLater(savedViewsHost.openSave);
            return;
        }
        switch (id) {
        case "task.new":             win.newTask(""); break;
        case "task.done":            win.markDone(); break;
        case "task.schedule":        win.scheduleKeyTasks(); break;
        case "task.moveProfile":     win.moveToProfile(win._keyTaskIds()); break;
        case "quick-capture":        quickCapture.open(); break;
        case "quick-capture-notes":  quickCaptureNotes.open(); break;
        case "people.open":          peopleDialog.showFor(""); break;
        case "rail.toggle":          win.toggleSideRail(); break;
        case "theme.toggle":         AppController.theme = (Theme.slot === "dark" ? "light" : "dark"); break;
        case "person.new":           personPicker.open_(); break;
        case "profile.new":          profileEditor.showCreate(); break;
        case "profile.next":         win._cycleProfile(1); break;
        case "profile.prev":         win._cycleProfile(-1); break;
        case "profile.exportMd":     AppController.copyActiveProfileMarkdownToClipboard(); break;
        case "profile.weeklyReport": AppController.copyWeeklyReportToClipboard(); break;
        case "sync.all":             AppController.syncNow(); break;
        // The Tweaks popover is gone (APP-270): its key opens Appearance.
        case "tweaks.open":          win.runCommand("settings:appearance"); break;
        case "hotkeys.open":         win.openCheatSheet(); break;
        case "region.next":          win.focusRegion(1); break;
        case "region.prev":          win.focusRegion(-1); break;
        case "hotkeys.edit":         rail.openHotkeys(rail.hotkeysAnchor); break;
        case "palette.commands":     cmdPalette.openWith(">"); break;
        case "search.focus":         win._focusSearch(); break;
        case "event.new":            eventCapture.openAt(null); break;
        case "welcome.replay":       win.openGuide(); break;
        case "recap.open":           weeklyRecap.showNow(); break;
        case "example.open":         win.openExample(); break;
        case "focus.immersion":      win.toggleImmersion(); break;
        case "standup.draft":        standupDraft.showNow(); break;
        case "timeMachine.open":     timeMachine.showNow(); break;
        case "log.open":             eventLog.showNow(); break;
        case "endOfDay.open":        endOfDay.showNow(); break;
        case "zoom.in":              win.zoomInterface(1); break;
        case "zoom.out":             win.zoomInterface(-1); break;
        case "zoom.reset":           win.zoomInterface(0); break;
        default:                     console.warn("palette: no command", id);
        }
    }

    function _cycleProfile(step) {
        const list = AppController.profiles;
        if (list.length === 0) return;
        let idx = -1;
        for (let i = 0; i < list.length; i++) if (list[i].id === AppController.activeProfileId) idx = i;
        const base = idx >= 0 ? idx : 0;
        AppController.activeProfileId = list[(base + step + list.length) % list.length].id;
    }

    // The top bar's box searches tasks. In a view that has a search of its
    // own — Docs, and Notes once it grows one — Ctrl+F used to focus that
    // task box anyway, where typing did nothing to what was on screen.
    // Duck-typed so a view picks this up by declaring focusSearch().
    function _focusSearch() {
        const view = win.activeViewItem();
        if (view && typeof view.focusSearch === "function") {
            view.focusSearch();
            return;
        }
        // Notes has no box of its own; the palette searches every note's
        // text, which is what Ctrl+F in a note is reaching for.
        if (AppController.currentView === "notes" || AppController.currentView === "docs") {
            cmdPalette.open();
            return;
        }
        topBar.focusSearch();
    }

    // Saved views: the active one, applying one, Ctrl 4… / g 1…, the name dialog.
    SavedViewsHost {
        id: savedViewsHost
        objectName: "saved-views-host"
        host: win
        onActiveIdChanged: win._saveFiltersSoon()
    }

    CommandPalette {
        id: cmdPalette
        onCommandRequested: (id) => win.runCommand(id)
        // The command line (APP-267): the task the cursor was on, the found
        // filter shown in Tasks or saved as a view, nothing found → a task.
        contextProvider: function () { return win._keyTaskIds(); }
        onQueryRequested: (query, mode) => {
            // Saving keeps the lens and the filter the user is on: Ctrl S on
            // the board threw them to the list and replaced their filter with
            // the palette's text, even when the dialog was cancelled (A3-15).
            if (mode === "save") { Qt.callLater(savedViewsHost.openSaveQuery, query); return; }
            win.showQuery(query, mode === "board" ? "board" : "list");
        }
        onCreateRequested: (text) => quickCapture.openWithText(text)
        onMoveToProfileRequested: (ids) => win.moveToProfile(ids)
        onOpenTask: (taskId) => win.showTask(AppController.taskById(taskId))
        onOpenPerson: (personId) => personEditor.showFor(AppController.personById(personId))
        // The catalogue is listed in Knowledge (DG-070): an entry opens
        // the list searched for it.
        onNavigateToDoc: (text) => notesBridge.requestedFilter = text
        onNavigateToSnippets: (text) => notesBridge.requestedFilter = text
        onNavigateToContacts: (text) => notesBridge.requestedFilter = text
        onNavigateToNoteLine: (line) => notesBridge.requestedLine = line
        onNavigateToDocPage: (pageId) => notesBridge.requestedPage = pageId
        onSearchNotes: (text) => notesBridge.requestedFilter = text
    }

    // A search hit sets the line it wants, NotesView watches and puts the
    // caret there; a doc page or a catalogue entry the same way (DG-070).
    QtObject {
        id: notesBridge
        property int requestedLine: -1
        property string requestedPage: ""
        property string requestedFilter: ""
    }

    // ── Rebindable application shortcuts ──────────────────────────────
    // Each Shortcut's sequence is bound through _kbd(id), which depends on
    // AppController.shortcuts (a Q_PROPERTY) so the binding re-evaluates on
    // shortcutsChanged — rebinding in the Hotkeys panel applies instantly.
    //
    // Only a chord with Ctrl, Alt or Meta is a Qt Shortcut's (APP-272): a bare
    // letter or a two-key sequence ("g b") is KeyRouter's below, which holds
    // it back while the person types and reads it by the physical key.
    function _kbd(id) {
        const list = AppController.shortcuts;
        for (let i = 0; i < list.length; i++)
            if (list[i].id === id) return KeyRules.isRouterSequence(list[i].sequence) ? "" : list[i].sequence;
        return "";
    }

    // ── The 0.8.0 keymap (keymap.md, APP-272) ─────────────────────────
    // Catalogue ids a Qt Shortcut item runs when bound to a Ctrl chord (here,
    // in SavedViewsHost and in NotesView). KeyRouter leaves those to it unless
    // the key came from a non-Latin layout; everything else it runs itself
    // through runShortcut().
    readonly property var _qtOwned: [
        "palette.open", "palette.open.alt", "rail.toggle", "task.new", "quick-capture",
        "quick-capture-notes", "section.today", "section.tasks", "section.knowledge", "view.board",
        "view.timeline", "view.week", "view.month", "view.docs", "view.notes", "view.settings", "view.archive",
        "theme.toggle", "person.new", "profile.new", "profile.next", "profile.prev", "profile.exportMd",
        "profile.weeklyReport", "sync.all", "focus.immersion", "timeMachine.open", "standup.draft", "recap.open",
        "endOfDay.open", "welcome.replay", "example.open", "zoom.in", "zoom.out", "zoom.reset", "tweaks.open", "log.open",
        "hotkeys.open", "undo", "redo", "search.focus", "selection.selectAll", "selection.clearSel",
        "selection.deleteSel", "cal.today", "cal.prevDay", "cal.nextDay", "cal.newEvent", "cal.prev", "cal.next", "cal.prev.alt", "cal.next.alt",
        "cal.goToDate", "cal.taskEarlier", "cal.taskLater", "cal.taskEarlierWeek", "cal.taskLaterWeek",
        "cal.taskTimeEarlier", "cal.taskTimeLater", "board.cursorDown", "board.cursorUp", "board.cursorLeft",
        "board.cursorRight", "board.open", "board.toggleSelect", "board.moveDown", "board.moveUp",
        "board.moveLeft", "board.moveRight", "board.selectDown", "board.selectUp", "board.selectColumnLeft",
        "board.selectColumnRight", "board.cardMenu", "board.archive", "board.collapseColumn", "task.done",
        "task.openExternal", "notes.next", "notes.prev", "notes.rename", "notes.toggleList",
        "notes.table.rowAbove", "notes.table.rowBelow", "notes.table.columnLeft", "notes.table.columnRight",
        "notes.table.deleteRow", "notes.table.deleteColumn",
        "savedView.1", "savedView.2", "savedView.3", "savedView.4", "savedView.5", "savedView.6",
        "savedView.7", "savedView.8", "savedView.9", "region.next", "region.prev"
    ]
    readonly property var _dayViews: ["today", "board", "list", "week", "month"]
    // Shift V: j / k grow the selection from the cursor until Esc.
    property bool _rangeMode: false
    // The last d, so a Vim-habit "dd" does not take Done back (keymap rule 6).
    property string _lastDoneKey: ""
    property real _lastDoneAt: 0
    // Ctrl O / Ctrl I: where the person was, like Vim's jumplist.
    property var _navBack: []
    property var _navFwd: []
    property string _navCur: ""
    property bool _navMoving: false
    function _noteNav(v) {
        if (win._navMoving || v === win._navCur) {
            win._navCur = v;
            return;
        }
        if (win._navCur.length > 0) {
            win._navBack.push(win._navCur);
            if (win._navBack.length > 50) win._navBack.shift();
            win._navFwd = [];
        }
        win._navCur = v;
    }
    function navStep(dir) {
        const from = dir < 0 ? win._navBack : win._navFwd;
        const to = dir < 0 ? win._navFwd : win._navBack;
        if (from.length === 0) return;
        const target = from.pop();
        to.push(AppController.currentView);
        win._navMoving = true;
        AppController.currentView = target;
        win._navMoving = false;
        win._navCur = AppController.currentView;
    }

    // Whether the action of `id` means something where the person is now.
    // A bare key stands down while anything takes typed text or a popup is
    // up; a Ctrl chord only behind a modal.
    // The focused item that gives a keymap action its own meaning (a
    // checklist row: d ticks the item, j / k walk the items), or null.
    function _keyClaimant(base) {
        for (let p = win.activeFocusItem; p; p = p.parent)
            if (p.claimedKeys !== undefined && p.claimedKeys !== null && p.claimedKeys.indexOf(base) >= 0) return p;
        return null;
    }
    // The Knowledge screens (notes and docs) with no panel over them.
    readonly property bool _inKnowledge: (AppController.currentView === "notes" || AppController.currentView === "docs")
                                         && !win._panelOpen
    function _keyLive(id) {
        if (hotkeys.isCapturing || win._captureActive) return false;
        // Deleting from the keyboard (dogfood 2026-10-19): only where the
        // Knowledge list or a document holds the keyboard and claims the key.
        // They share Del / d d with the tasks' keys, which stand down there.
        const claimedOnly = KeyRules.baseId(id);
        if (claimedOnly === "notes.delete" || claimedOnly === "editor.deleteBlock") {
            const c = win._keyClaimant(claimedOnly);
            if (!c || win._focusInPopup || win._overlayOpen || win._modalOpen) return false;
            if (KeyRules.isRouterSequence(AppController.shortcutFor(id)) && win._typing) return false;
            return typeof c.claimLive !== "function" || c.claimLive(claimedOnly);
        }
        if (KeyRules.isRouterSequence(AppController.shortcutFor(id)) && win._keyClaimant(KeyRules.baseId(id)))
            return !win._typing && !win._focusInPopup && !win._overlayOpen && !win._modalOpen;
        // F6 leaves a text field too: that is how one gets out of it.
        if (id === "region.next" || id === "region.prev") return !win._modalOpen;
        const routed = KeyRules.isRouterSequence(AppController.shortcutFor(id));
        const base = KeyRules.baseId(id);
        if (base === "board.open" && Date.now() < win._returnGuardUntil) return false;
        if (routed && win._panelOpen && win._docTaskKeys.indexOf(base) >= 0)
            return taskDoc.opened && win._focusInPanel && !win._typing && !win._focusInPopup && !win._overlayOpen
                && !hotkeys.isCapturing;
        if (routed ? win._viewKeysBlocked : !win._globalKeysOn) return false;
        const v = AppController.currentView;
        const b = win.activeViewItem();
        // Column move: only on a column header that holds the keyboard
        // (DG-133); anywhere else Ctrl+Shift+L stays the log.
        if (base === "board.columnLeft" || base === "board.columnRight")
            return !!b && typeof b.focusedHeaderStatus === "function" && b.focusedHeaderStatus().length > 0;
        // One cursor in every view that has one (APP-276): the board, the
        // list, Today, the calendar, the notes.
        // A Ctrl chord of the Classic keymap (Ctrl+E, Ctrl+arrows) is the
        // text field's while it has the keyboard, as the bare keys are.
        if (base.indexOf("board.") === 0)
            return win._cursorOn(b) && !win._viewKeysBlocked;
        if (base.indexOf("cursor.") === 0)
            return win._cursorOn(b) && !win._typing;
        // s on an empty day of the calendar: go to a date (keymap.md).
        if (base === "task.schedule" && win._keyTaskIds().length === 0)
            return ["day", "week", "month"].indexOf(v) >= 0 && !!b && b.cursorVisible === true;
        // A new note from any screen (it goes to Notes first); the rest of
        // notes.* only where the notes are.
        if (base === "notes.new") return !win._panelOpen && !win._overlayOpen && !win._modalOpen;
        if (base.indexOf("notes.") === 0) return (v === "notes" || v === "docs") && !win._panelOpen;
        // "/" on Today: the header search does not filter the day, so the
        // key does nothing rather than look like it does (IDIOT-KNOW-15).
        if (routed && base === "search.focus" && v === "today") return false;
        if (base.indexOf("savedView.") === 0)
            return Number(base.slice(10)) <= AppController.savedViews.length && !win._typing;  // IDIOT-SHELL-7
        if (base === "profile.next" || base === "profile.prev") return !win._typing;
        // No task is on a Knowledge screen: a selection made on the board
        // is out of sight there, and F2 / Del / d are the note's.
        if (base.indexOf("task.") === 0 && base !== "task.new")
            return win._keyTaskIds().length > 0 && !win._inKnowledge;
        switch (base) {
        case "cal.prev": case "cal.next":
            return ["today", "week", "month"].indexOf(v) >= 0;
        case "cal.today": case "cal.prevDay": case "cal.nextDay": case "cal.goToDate":
            return win._dayViews.indexOf(v) >= 0;
        // No view defines setZoom: the catalogue's z d did nothing while the
        // CalKey under it was shadowed (IDIOT-CAL-3). It switches, as z w does.
        case "cal.zoomDay":
            return v === "week" || v === "month";
        case "cal.newEvent":
            return !win._overlayOpen;
        case "cal.taskEarlier": case "cal.taskLater": case "cal.taskEarlierWeek": case "cal.taskLaterWeek":
        case "cal.taskTimeEarlier": case "cal.taskTimeLater":
            return win._moveKeysOn;
        case "selection.toggle": case "selection.range":
            return v === "board" || win._keyTaskIds().length > 0;
        case "selection.clearSel":
            if (!!b && b.dragActive === true) return true;
            // Nothing found and nothing selected: Esc is the empty state's
            // "reset the filter · Esc", not a cursor on a card filtered out of
            // sight (IDIOT-TASKS-5).
            if (win._taskFilterEmpty && AppController.selectionCount === 0) return false;
            return AppController.selectionCount > 0 || win._rangeMode || win._boardCursorShown();
        case "selection.deleteSel":
            return (AppController.selectionCount > 0 || win._cursorTaskId().length > 0 || win._cursorEventId().length > 0)
                && !win._inKnowledge;
        case "cal.longer": case "cal.shorter":
            return !win._viewKeysBlocked && !!b && typeof b.resizeCursor === "function";
        case "selection.selectAll":
            return ["board", "list", "week"].indexOf(v) >= 0;
        case "nav.back": return win._navBack.length > 0 && !win._typing;
        case "nav.forward": return win._navFwd.length > 0 && !win._typing;
        // A view may keep the bare key to what it shows (Knowledge: the
        // note's own edit, never a change on a screen not in sight).
        case "undo":
            if (routed && b && typeof b.undoKeyLive === "function")
                return (b.undoKeyLive() || (typeof b.undoKeyOwned === "function" && b.undoKeyOwned()))
                    && !win._overlayOpen && !win._typing;
            return AppController.hasPendingUndo && !win._overlayOpen && !win._typing;
        case "redo": return AppController.canRedo && !win._overlayOpen && !win._typing;
        case "focus.immersion": return !!(AppController.safety && AppController.safety.immersion);
        case "standup.draft": return !!(AppController.safety && AppController.safety.standupDraft);
        }
        return true;
    }
    // The board's keyboard cursor is drawn (there is something for Esc to
    // let go of), and letting go of it.
    // The cursor of the view on screen (APP-276: every view walks with the
    // same keys).
    function _cursorView() {
        return win.activeViewItem() || null;
    }
    // A view with a keyboard cursor, and no menu of its own up.
    function _cursorOn(b) {
        return !!b && typeof b.moveCursor === "function" && b.cardMenuOpen !== true;
    }
    // The task under the cursor — never the one under the pointer (Del).
    function _cursorTaskId() {
        const v = win.activeViewItem();
        return v && v.cursorVisible === true && typeof v.cursorTaskId === "string" ? v.cursorTaskId : "";
    }
    // The meeting under the cursor of a calendar view or Today, never one
    // under the pointer (Del, dogfood 2026-10-19); "" when none.
    function _cursorEventId() {
        const v = win.activeViewItem();
        if (!v || v.cursorVisible !== true || typeof v._cursorItem !== "function") return "";
        const it = v._cursorItem();
        return it && it.kind === "event" ? String(it.id || "") : "";
    }
    // Del on a meeting: one that is not a series goes at once, with Undo in
    // the toast; a series, or a calendar's read-only one, gets the meeting's
    // own menu where the view has one (the week: "this one or all", or
    // greyed with why), else a toast that says why Del does not take it and
    // what does (Today, the month: openCursorMenu only knew task cards, so
    // Del there did nothing at all - review 2026-10-19).
    function deleteKeyEvent(id) {
        const e = AppController.eventById(id);
        if (!e || !e.id) return;
        const readOnly = AppController.isSubscriptionEvent(id);
        const series = String(e.rrule || "").length > 0 || String(e.masterId || "").length > 0;
        if (series || readOnly) {
            const v = win.activeViewItem();
            if (v && typeof v.openCursorEventMenu === "function" && v.openCursorEventMenu()) return;
            win.notice(readOnly ? I18n.t("event.fromCalendar").arg(AppController.subscriptionNameOf(id))
                                : I18n.t("event.delSeries"), "info");
            return;
        }
        AppController.deleteEvent(id);
    }
    // Del: the selection, else the task under the cursor; one undo step.
    function deleteKeyTasks() {
        if (AppController.selectionCount === 0) {
            const id = win._cursorTaskId();
            if (!id) {
                const ev = win._cursorEventId();
                if (ev) win.deleteKeyEvent(ev);
                return;
            }
            AppController.setSelectedTaskIds([id]);
        }
        win.deleteSelectedAsk();
    }
    // A bulk delete asks once when it takes 10 or more tasks or any tracker
    // ticket (A1-28: Ctrl+A, Del took 121 tasks, 73 of them Jira's, with no
    // question). One task, or a few local ones, still go at once with Undo.
    // A ticket is only ever removed here: nothing is written to its tracker.
    function deleteSelectedAsk() {
        const ids = AppController.selectedTaskIds;
        const n = ids.length;
        if (n === 0) return;
        let tracked = 0;
        const providers = [];
        for (let i = 0; i < n; i++) {
            const t = AppController.taskById(ids[i]);
            if (!t || !t.externalProvider) continue;
            tracked++;
            if (providers.indexOf(t.externalProvider) < 0) providers.push(t.externalProvider);
        }
        if (n < 2 || (n < 10 && tracked === 0)) {
            AppController.deleteSelectedTasks();
            return;
        }
        bulkDeleteDialog.count = n;
        bulkDeleteDialog.tracked = tracked;
        bulkDeleteDialog.trackerName = providers.length === 1
            ? String((AppController.providerBadges[providers[0]] || {}).name || providers[0])
            : I18n.t("bulkDelete.trackers");
        bulkDeleteDialog.open();
    }
    SmallDialog {
        id: bulkDeleteDialog
        objectName: "bulk-delete-dialog"
        property int count: 0
        property int tracked: 0
        property string trackerName: ""
        title: I18n.count(bulkDeleteDialog.count, "bulkDelete.title")
               + (bulkDeleteDialog.tracked > 0
                  ? ", " + I18n.t("bulkDelete.fromTracker").arg(bulkDeleteDialog.tracked).arg(bulkDeleteDialog.trackerName)
                  : "") + "?"
        fact: (bulkDeleteDialog.tracked > 0 ? I18n.t("bulkDelete.bodyTracked") + " " : "")
              + I18n.t("bulkDelete.bodyUndo").arg(AppController.shortcutText("undo"))
        // Enter = delete (the focused, main button), Esc = cancel.
        onOpened: bulkDeleteConfirm.forceActiveFocus(Qt.TabFocusReason)
        onAccepted: bulkDeleteConfirm.clicked()
        onClosed: Qt.callLater(win.focusActiveView)
        buttons: [
            PillButton {
                objectName: "bulk-delete-cancel"
                text: I18n.t("common.cancel")
                onClicked: bulkDeleteDialog.close()
            },
            PillButton {
                id: bulkDeleteConfirm
                objectName: "bulk-delete-confirm"
                text: I18n.t("common.delete")
                danger: true
                onClicked: {
                    bulkDeleteDialog.close();
                    AppController.deleteSelectedTasks();
                }
            }
        ]
    }
    // s / Shift S (APP-278): the small field; on an empty day of the
    // calendar, s goes to a date instead.
    function openSchedule(field) {
        const ids = win._keyTaskIds();
        if (ids.length === 0) {
            if (field === "scheduled") goToDatePopup.openAt(AppController.selectedDate, win.contentItem);
            return;
        }
        schedulePopup.openFor(ids, field);
        win._placeUnderCard(schedulePopup, ids.length === 1 ? ids[0] : "");
    }
    // The visible card (or row) of a task in the active view, or null.
    function _taskItem(id) {
        const v = win.activeViewItem();
        if (!v || !id) return null;
        const stack = [v];
        while (stack.length > 0) {
            const it = stack.pop();
            if (!it.visible) continue;
            if (it !== v && it.taskId === id && it.width > 40 && it.height > 12) return it;
            const kids = it.children;
            for (let i = 0; i < kids.length; i++) stack.push(kids[i]);
        }
        return null;
    }
    // "Маленькое поле у карточки" (N-Dlg-Schedule, R3-066): right under the
    // card it was opened from, so its title stays in sight; above it when
    // there is no room below. A selection, or a card out of sight: centred.
    function _placeUnderCard(pop, id, item) {
        const host = win.contentItem;
        const card = item || win._taskItem(id);
        if (!card) {
            pop.x = Math.round((host.width - pop.width) / 2);
            pop.y = Math.round(host.height / 4);
            return;
        }
        const p = card.mapToItem(host, 0, 0);
        const gap = Theme.spXs;
        const h = pop.implicitHeight > 0 ? pop.implicitHeight : pop.height;
        let y = p.y + card.height + gap;
        if (y + h > host.height - gap) y = Math.max(gap, p.y - h - gap);
        pop.x = Math.round(Math.max(gap, Math.min(p.x, host.width - pop.width - gap)));
        pop.y = Math.round(y);
    }
    function _boardCursorShown() {
        const bi = win._cursorView();
        return !!bi && bi["cursorVisible"] === true;
    }
    function _clearBoardCursor() {
        const bi = win._cursorView();
        if (bi && typeof bi["clearCursor"] === "function") bi["clearCursor"]();
    }
    // KeyRouter's handler: run the first of `ids` that is live here.
    function _routeKey(ids, dry) {
        const list = String(ids).split(",");
        for (let i = 0; i < list.length; i++) {
            if (!win._keyLive(list[i])) continue;
            if (!dry) win.runShortcut(list[i]);
            return list[i];
        }
        return "";
    }
    function _eachKeyTask(fn) {
        const ids = win._keyTaskIds();
        for (let i = 0; i < ids.length; i++) {
            const t = AppController.taskById(ids[i]);
            if (t && t.id) fn(t);
        }
        return ids.length;
    }
    function _copyKeyTask(field) {
        const out = [];
        win._eachKeyTask(function (t) {
            const v = field === "id" ? String(t.externalKey || t.id)
                : field === "branch" ? String(t.branch || "")
                : String((t.ticket && t.ticket.url) || t.externalUrl || "");
            if (v.length > 0) out.push(v);
        });
        if (out.length === 0) {
            toast.show(I18n.t(field === "branch" ? "keys.copy.noBranch" : "keys.copy.noLink"), "warning");
            return;
        }
        AppController.copyToClipboard(out.join(field === "id" ? ", " : "\n"));
        toast.show(I18n.t("keys.copied").arg(out.join(", ")));
    }
    // Runs a catalogue action from the keyboard — every id, whichever way its
    // key came in.
    function runShortcut(id) {
        const base = KeyRules.baseId(id);
        const claimant = win._keyClaimant(base);
        if (claimant && typeof claimant.runClaimed === "function" && claimant.runClaimed(base)) return;
        const b = win.activeViewItem();
        const call = function (name) {
            if (b && typeof b[name] === "function") b[name].apply(b, Array.prototype.slice.call(arguments, 1));
        };
        switch (base) {
        case "board.cursorDown":
            if (win._rangeMode) call("extendSelection", 1); else call("moveCursor", 0, 1);
            return;
        case "board.cursorUp":
            if (win._rangeMode) call("extendSelection", -1); else call("moveCursor", 0, -1);
            return;
        case "board.cursorLeft": call("moveCursor", -1, 0); return;
        case "board.cursorRight": call("moveCursor", 1, 0); return;
        case "board.open": call("openCursor"); return;
        case "board.toggleSelect": call("toggleCursorSelection"); return;
        case "board.moveDown": call("moveCursorCard", 0, 1); return;
        case "board.moveUp": call("moveCursorCard", 0, -1); return;
        case "board.moveLeft": call("moveSelectionOrCard", -1); return;
        case "board.moveRight": call("moveSelectionOrCard", 1); return;
        case "board.selectDown": call("extendSelection", 1); return;
        case "board.selectUp": call("extendSelection", -1); return;
        case "board.selectColumnLeft": call("selectColumnAndStep", -1); return;
        case "board.selectColumnRight": call("selectColumnAndStep", 1); return;
        case "board.cardMenu": call("openCursorMenu"); return;
        case "board.archive":
            if (b && typeof b.archiveCursor === "function") b.archiveCursor();
            else win._eachKeyTask(function (t) { AppController.setArchived(t.id, true); });
            return;
        case "board.collapseColumn": call("toggleCursorColumn"); return;
        case "board.columnLeft": call("moveFocusedColumn", -1); return;
        case "board.columnRight": call("moveFocusedColumn", 1); return;
        case "cal.longer": call("resizeCursor", 1); return;
        case "cal.shorter": call("resizeCursor", -1); return;
        case "region.next": win.focusRegion(1); return;
        case "region.prev": win.focusRegion(-1); return;
        case "cursor.first": call("moveCursor", 0, -100000); return;
        case "cursor.last": call("moveCursor", 0, 100000); return;
        case "cursor.pageDown": call("moveCursor", 0, 8); return;
        case "cursor.pageUp": call("moveCursor", 0, -8); return;
        case "nav.back": win.navStep(-1); return;
        case "nav.forward": win.navStep(1); return;
        case "task.done": win.markDone(); return;
        case "task.openExternal": {
            const ids = win._keyTaskIds();
            // Never a whole multi-selection: one browser tab per card.
            if (ids.length === 1) AppController.openTaskExternal(ids[0]);
            return;
        }
        case "task.newBelow": case "task.newAbove": {
            const ids = win._keyTaskIds();
            const t = ids.length > 0 ? AppController.taskById(ids[0]) : null;
            if (t && t.status) quickCapture.openIn(t.status);
            else quickCapture.open();
            return;
        }
        case "task.rename": {
            if (win._panelOpen) { if (taskDoc.opened) taskDoc.focusTitle(); return; }
            const ids = win._keyTaskIds();
            if (ids.length > 0) win.openTask(ids[0]);
            return;
        }
        case "search.focus":
            // "/" in the document does what its hint says: a new line and
            // the insert menu, not the board's filter behind it.
            if (win._panelOpen) { if (taskDoc.opened) taskDoc.slashInsert(); return; }
            win._focusSearch();
            return;
        case "task.due": win.openSchedule("due"); return;
        case "task.schedule": win.openSchedule("scheduled"); return;
        case "task.priority0": case "task.priority1": case "task.priority2": case "task.priority3": {
            // One bulk edit, one undo step (IDIOT-TASKS-3): a step per card
            // filled the undo stack and froze on a thousand cards.
            win.priorityKeyTasks("P" + base.slice(13));
            return;
        }
        case "task.timer":
            win._eachKeyTask(function (t) {
                if (t.isTiming) AppController.stopTaskTimer(t.id); else AppController.startTaskTimer(t.id);
            });
            return;
        case "task.copyId": win._copyKeyTask("id"); return;
        case "task.copyBranch": win._copyKeyTask("branch"); return;
        case "task.copyLink": win._copyKeyTask("link"); return;
        case "task.createBranch": {
            const ids = win._keyTaskIds();
            if (ids.length > 0) AppController.createBranchForTask(ids[0]);
            return;
        }
        case "task.moveProfile": win.moveToProfile(win._keyTaskIds()); return;
        case "selection.toggle":
            if (b && typeof b.toggleCursorSelection === "function") { call("toggleCursorSelection"); return; }
            win._eachKeyTask(function (t) { AppController.toggleTaskSelection(t.id); });
            return;
        case "selection.range":
            win._rangeMode = !win._rangeMode;
            if (win._rangeMode && AppController.currentView === "board") call("toggleCursorSelection");
            return;
        case "selection.clearSel":
            // Esc mid-drag cancels the drag and nothing else (IDIOT-TASKS-8).
            if (b && typeof b.cancelDrag === "function" && b.cancelDrag()) return;
            win._rangeMode = false;
            AppController.clearSelection();
            win._clearBoardCursor();
            return;
        case "selection.deleteSel": win.deleteKeyTasks(); return;
        case "selection.selectAll": call("selectAllVisible"); return;
        case "cal.today": AppController.selectedDate = AppController.today; return;
        case "cal.prev": case "cal.next": {
            const dir = base === "cal.prev" ? -1 : 1;
            if (b && typeof b.step === "function" && AppController.currentView !== "today") { b.step(dir); return; }
            const d = AppController.selectedDate;
            AppController.selectedDate = new Date(d.getFullYear(), d.getMonth(), d.getDate() + dir);
            return;
        }
        case "cal.prevDay": case "cal.nextDay": {
            const d = AppController.selectedDate;
            AppController.selectedDate = new Date(d.getFullYear(), d.getMonth(), d.getDate() + (base === "cal.prevDay" ? -1 : 1));
            return;
        }
        case "cal.goToDate": goToDatePopup.openAt(AppController.selectedDate, win.contentItem); return;
        case "cal.zoomDay": AppController.currentView = "day"; return;
        case "cal.newEvent": {
            const day = AppController.selectedDate;
            eventCapture.openAt({ date: day });
            return;
        }
        case "cal.taskEarlier": win._moveViewTask(-1, 0); return;
        case "cal.taskLater": win._moveViewTask(1, 0); return;
        case "cal.taskEarlierWeek": win._moveViewTask(-7, 0); return;
        case "cal.taskLaterWeek": win._moveViewTask(7, 0); return;
        case "cal.taskTimeEarlier": win._moveViewTask(0, -1); return;
        case "cal.taskTimeLater": win._moveViewTask(0, 1); return;
        case "undo":
            if (taskDoc.opened && win._focusInPanel) taskDoc.undo();
            else if (b && typeof b.undoKey === "function") {
                if (!b.undoKey()) toast.show(I18n.t("notes.undoNotHere"), "info");
            }
            else AppController.undo();
            return;
        case "redo": AppController.redo(); return;
        }
        win.runCommand(base);
    }

    KeyRouter {
        id: keyRouter
        objectName: "key-router"
        window: win
        enabled: !hotkeys.isCapturing && !win._captureActive && !splash.waiting
        bindings: AppController.shortcuts
        qtOwned: win._qtOwned
        holdUnlive: boardTypeAhead.enabled
        // A card's command letter waits a moment where type-to-search is
        // live: "deploy" typed on the board searched "ploy" and marked the
        // card under the cursor done (A4-3).
        typingHoldMs: win.typeAheadHoldMs
        onTypedText: (text) => topBar.typeAhead(text)
        handler: function (ids, dry) { return win._routeKey(ids, dry); }
        // The old keys of 0.7 say once where their action went (APP-281 A4).
        onChordPressed: (chord) => { if (AppController.hasKeymapNotice()) AppController.noteKeyPressed(chord); }
        Component.onCompleted: win._navCur = AppController.currentView
    }
    // A scheme switched while a prefix waits: the waiting g of Vim means
    // nothing in Classic, and Shift V's range does not carry over.
    Connections {
        target: AppController
        function onKeymapSchemeChanged() {
            keyRouter.cancel();
            win._rangeMode = false;
        }
    }

    Shortcut {
        sequence: _kbd("palette.open")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: cmdPalette.open()
    }
    // Ctrl+P: the same, as a catalogue entry of its own (APP-279) — it can be
    // rebound or cleared like any other.
    Shortcut {
        sequence: win._kbd("palette.open.alt")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: cmdPalette.open()
    }

    Shortcut {
        sequence: _kbd("rail.toggle")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.toggleSideRail()
    }
    Shortcut {
        sequence: _kbd("task.new")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.newTask("")
    }
    Shortcut {
        sequence: _kbd("quick-capture")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: quickCapture.open()
    }
    Shortcut {
        sequence: _kbd("quick-capture-notes")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: quickCaptureNotes.open()
    }
    Shortcut {
        sequence: win._kbd("section.today")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.openSection("today")
    }
    Shortcut {
        sequence: win._kbd("section.tasks")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: { win.leaveSavedView(); AppController.openSection("tasks"); }
    }
    Shortcut {
        sequence: win._kbd("section.knowledge")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.currentView = "notes"
    }
    Shortcut {
        sequence: _kbd("view.board")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: { win.leaveSavedView(); AppController.currentView = "board"; }
    }
    Shortcut {
        sequence: _kbd("view.timeline")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.currentView = "list"
    }
    Shortcut {
        sequence: _kbd("view.week")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.currentView = "week"
    }
    Shortcut {
        sequence: _kbd("view.month")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.currentView = "month"
    }
    Shortcut {
        sequence: _kbd("view.docs")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.currentView = "docs"
    }
    Shortcut {
        sequence: _kbd("view.notes")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.currentView = "notes"
    }
    Shortcut {
        sequence: _kbd("view.settings")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.currentView = "settings"
    }
    Shortcut {
        sequence: _kbd("view.archive")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.openArchive()
    }
    Shortcut {
        sequence: _kbd("theme.toggle")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.theme = (Theme.slot === "dark" ? "light" : "dark")
    }
    Shortcut {
        sequence: _kbd("person.new")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: personPicker.open_()
    }
    Shortcut {
        sequence: _kbd("profile.new")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: profileEditor.showCreate()
    }
    // Ctrl+[ is Esc to a Vim hand, and the keymap is Vim-based: while a field
    // takes typed text it leaves the field, and neither profile key switches
    // the workspace under the caret (IDIOT-SHELL-7).
    Shortcut {
        sequence: _kbd("profile.next")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn && !win._typing
        onActivated: win._cycleProfile(1)
    }
    Shortcut {
        sequence: _kbd("profile.prev")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn && !win._typing
        onActivated: win._cycleProfile(-1)
    }
    Shortcut {
        objectName: "vim-escape"
        sequence: "Ctrl+["
        context: Qt.ApplicationShortcut
        enabled: win._typing && !win._focusInPopup && !win._modalOpen && !hotkeys.isCapturing
        onActivated: win.focusActiveView()
    }
    Shortcut {
        sequence: _kbd("profile.exportMd")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.copyActiveProfileMarkdownToClipboard()
    }
    // Focus mode (APP-160): on, and again to leave. Esc leaves it too when
    // nothing nearer would take the Esc (a selection, the board cursor, a
    // field, a dialog) — those stand the shortcut down, so the two never
    // compete for the same key.
    Shortcut {
        sequence: win._kbd("focus.immersion")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && (win._globalKeysOn || AppController.immersion)
                 && !!(AppController.safety && AppController.safety.immersion)
        onActivated: win.toggleImmersion()
    }
    // The 0.6 tools (APP-192): no key by default; live once one is bound in
    // Settings → Hotkeys, and they run what the palette runs.
    Shortcut {
        sequence: win._kbd("timeMachine.open")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.runCommand("timeMachine.open")
    }
    Shortcut {
        sequence: win._kbd("standup.draft")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn && !!(AppController.safety && AppController.safety.standupDraft)
        onActivated: win.runCommand("standup.draft")
    }
    Shortcut {
        sequence: win._kbd("recap.open")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.runCommand("recap.open")
    }
    Shortcut {
        sequence: win._kbd("endOfDay.open")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.runCommand("endOfDay.open")
    }
    Shortcut {
        sequence: win._kbd("welcome.replay")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.runCommand("welcome.replay")
    }
    Shortcut {
        sequence: win._kbd("example.open")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.runCommand("example.open")
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: AppController.immersion && !win._viewKeysBlocked && AppController.selectionCount === 0
                 && !(AppController.currentView === "board" && !!boardLoader.item && boardLoader.item["cursorVisible"] === true)
        onActivated: AppController.stopImmersion()
    }
    // Interface scale (APP-168) from the keyboard: a step of Theme.scaleSteps
    // up or down, or back to 100 %, stored where Settings → Appearance → Scale
    // stores it. Live over dialogs and in text fields too (no editor binds
    // these keys), so a modal is no reason to stand down; only the hotkey
    // recorder, which wants the key itself, and the capture window, which is
    // a window of its own, keep them.
    function zoomInterface(direction) {
        const s = AppController.stepUiScale(direction, Theme.scaleSteps);
        toast.show(I18n.t("toast.uiScale").arg(Math.round(s * 100)), "info", "uiScale");
    }
    property bool _captureActive: false
    readonly property bool _zoomKeysOn: !hotkeys.isCapturing && !win._captureActive
    // The fixed aliases match kBuiltinKeys in AppController.cpp. Where one key
    // matches two of them (Ctrl+Shift+= is also Ctrl++), Qt reports it as
    // ambiguous: that is still one press.
    Shortcut {
        context: Qt.ApplicationShortcut
        enabled: win._zoomKeysOn
        objectName: "shortcut-zoom-in"
        sequences: [win._kbd("zoom.in"), "Ctrl++", "Ctrl+Shift+=", "Ctrl+Num++"]
        onActivated: win.zoomInterface(1)
        onActivatedAmbiguously: win.zoomInterface(1)
    }
    Shortcut {
        context: Qt.ApplicationShortcut
        enabled: win._zoomKeysOn
        objectName: "shortcut-zoom-out"
        sequences: [win._kbd("zoom.out"), "Ctrl+Num+-"]
        onActivated: win.zoomInterface(-1)
        onActivatedAmbiguously: win.zoomInterface(-1)
    }
    Shortcut {
        context: Qt.ApplicationShortcut
        enabled: win._zoomKeysOn
        objectName: "shortcut-zoom-reset"
        sequences: [win._kbd("zoom.reset"), "Ctrl+Num+0"]
        onActivated: win.zoomInterface(0)
        onActivatedAmbiguously: win.zoomInterface(0)
    }
    Shortcut {
        sequence: _kbd("profile.weeklyReport")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.copyWeeklyReportToClipboard()
    }
    Shortcut {
        sequence: _kbd("sync.all")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: AppController.syncNow()
    }
    Shortcut {
        sequence: _kbd("tweaks.open")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.runCommand("settings:appearance")
    }
    // The event log (APP-187).
    Shortcut {
        objectName: "shortcut-log-open"
        sequence: win._kbd("log.open")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: eventLog.showNow()
    }
    Shortcut {
        sequence: _kbd("hotkeys.open")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn
        onActivated: win.openCheatSheet()
    }
    // `?` opens the same cheat sheet (APP-166): hotkeys.open.alt, a key
    // KeyRouter runs, so it stands down while anything takes typed text.
    Shortcut {
        sequence: _kbd("undo")
        context: Qt.ApplicationShortcut
        // Not behind an open editor or dialog: it would change what the dialog
        // is showing (and could delete the task being edited). Inside a text
        // field Ctrl+Z belongs to the field, which takes it first.
        enabled: sequence.length > 0 && win._globalKeysOn && AppController.hasPendingUndo && !win._overlayOpen
            && !(boardLoader.item && boardLoader.item.dialogOpen === true)
        // In the open document it is the document's undo (IDIOT-DOC-17).
        onActivated: if (taskDoc.opened && win._focusInPanel) taskDoc.undo(); else AppController.undo()
    }
    Shortcut {
        sequence: _kbd("redo")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && win._globalKeysOn && AppController.canRedo && !win._overlayOpen
            && !(boardLoader.item && boardLoader.item.dialogOpen === true)
        onActivated: AppController.redo()
    }
    Shortcut {
        sequence: _kbd("search.focus")
        context: Qt.ApplicationShortcut
        // Not behind a panel: the filter is under the scrim (IDIOT-DOC-4).
        enabled: sequence.length > 0 && win._globalKeysOn && !win._panelOpen
        onActivated: win._focusSearch()
    }

    Shortcut {
        sequence: _kbd("selection.selectAll")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && !win._viewKeysBlocked
            && (AppController.currentView === "board"
                || AppController.currentView === "list"
                || AppController.currentView === "day"
                || AppController.currentView === "week"
)
        onActivated: {
            const v = win.activeViewItem();
            if (v && v.selectAllVisible) v.selectAllVisible();
        }
    }
    // Esc lets go of the selection and, on the board, of the keyboard
    // cursor — last. It stays out of the way (enabled only when there is
    // something to let go of, and never while a popup, menu, editor or text
    // field holds the keyboard) so the innermost thing closes first: one Esc
    // closes the editor or the menu, the next one lets go of the selection.
    Shortcut {
        sequence: _kbd("selection.clearSel")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && !win._viewKeysBlocked
            && (AppController.selectionCount > 0 || win._boardCursorShown())
        onActivated: {
            AppController.clearSelection();
            win._clearBoardCursor();
        }
    }
    // Esc on a tabbed-to control outside the view (filter chip, mini week, day
    // panel…) hands the keyboard back to the view.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        // Not with a panel open: the panel's own Esc below takes it, and two
        // enabled Escape shortcuts are ambiguous to Qt — neither ran (VERIFY-A-1).
        enabled: win._focusOnControl && !win._typing && !win._focusInPopup && !win._overlayOpen && !win._panelOpen
                 && !hotkeys.isCapturing && !splash.waiting
        onActivated: win.focusActiveView()
    }
    // A panel is open and the keyboard is somewhere else, with nothing on
    // top: Esc brings it back into the panel, whose own Esc then closes it.
    // "Esc back" is never a dead key (IDIOT-DOC-4).
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: win._panelOpen && !win._focusInPanel && !win._typing && !win._focusInPopup && !win._overlayOpen
                 && !win._dimmerShown && !hotkeys.isCapturing && !splash.waiting
        onActivated: win.focusActiveView()
    }
    Shortcut {
        sequence: _kbd("selection.deleteSel")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && !win._viewKeysBlocked && !win._inKnowledge
            && (AppController.selectionCount > 0 || win._cursorTaskId().length > 0 || win._cursorEventId().length > 0)
        onActivated: win.deleteKeyTasks()
    }
    // Open the selected (or hovered) mirrored issue in its tracker (HEAP-117).
    // This is the first bare letter in the catalog. Qt hands a focused text
    // field the ShortcutOverride for an unmodified key, so typing "o" still
    // types it — but a read-only Text or a ComboBox's type-ahead would lose,
    // so the shortcut also stands down while any overlay is open.
    // ── Board keyboard cursor ─────────────────────────────────────────
    // The board was mouse-only: no way to move between cards, open one or
    // move one without dragging. Each of these stands down while an overlay
    // is open and off the board, exactly like task.openExternal below.
    //
    // The catalog holds the vim letter so it can be rebound; the arrow key is
    // a fixed alias alongside it, the way Ctrl+P aliases the palette.
    // A calendar view is week or month; the day panel follows the same selected
    // date, so moving it moves everything that is on screen.
    component CalKey: Shortcut {
        context: Qt.ApplicationShortcut
        enabled: sequences.length > 0 && !win._viewKeysBlocked
            && ["day", "week", "month"].indexOf(AppController.currentView) >= 0
    }

    // The day panel follows the selected date in every view it sits beside,
    // so today, go-to-date and a day at a time work from those too — they
    // used to be week/month only. They stand down like every other view key:
    // G opened go-to-date over the profile menu and a "Delete column?"
    // confirm, Alt+← moved the day from the header search (SHELL-3).
    component DayKey: Shortcut {
        context: Qt.ApplicationShortcut
        enabled: sequences.length > 0 && !win._viewKeysBlocked && !win._popupJustClosed
            && ["today", "board", "list", "day", "week", "month"].indexOf(AppController.currentView) >= 0
    }
    // The Alt of Alt+← closes an open menu on its own, and the ← that
    // followed paged the day behind it (IDIOT-KNOW-12): a day key just after
    // a menu let the keyboard go waits a moment.
    property bool _popupJustClosed: false
    Timer { id: popupClosedTimer; interval: 400; onTriggered: win._popupJustClosed = false }
    DayKey {
        sequences: [_kbd("cal.today")]
        onActivated: AppController.selectedDate = AppController.today
    }
    DayKey {
        sequences: [_kbd("cal.prevDay")]
        onActivated: {
            const d = AppController.selectedDate;
            AppController.selectedDate = new Date(d.getFullYear(), d.getMonth(), d.getDate() - 1);
        }
    }
    DayKey {
        sequences: [_kbd("cal.nextDay")]
        onActivated: {
            const d = AppController.selectedDate;
            AppController.selectedDate = new Date(d.getFullYear(), d.getMonth(), d.getDate() + 1);
        }
    }
    // A new event from the keyboard: the editor opens on the next free slot
    // of the selected day, named before it exists.
    Shortcut {
        sequences: [_kbd("cal.newEvent")]
        context: Qt.ApplicationShortcut
        // Behind a modal, a menu or a view's own dialog it stands down like
        // every other global key; it used to open over a delete confirmation
        // (SHELL-2, audit 2026-09-30). In a text field AltGrGuard keeps the
        // chord for the field (AltGr+E is € on a German layout).
        enabled: sequences.length > 0 && win._globalKeysOn && !win._overlayOpen
        onActivated: {
            const day = AppController.selectedDate;
            eventCapture.openAt({ date: day });
        }
    }
    CalKey {
        sequences: [_kbd("cal.prev")]
        onActivated: { const v = win.activeViewItem(); if (v && v.step) v.step(-1); }
    }
    CalKey {
        sequences: [_kbd("cal.next")]
        onActivated: { const v = win.activeViewItem(); if (v && v.step) v.step(1); }
    }
    // n / p, the next and the previous period, beside ] and [ (0.9.4).
    CalKey {
        sequences: [win._kbd("cal.prev.alt")]
        onActivated: { const v = win.activeViewItem(); if (v && v.step) v.step(-1); }
    }
    CalKey {
        sequences: [win._kbd("cal.next.alt")]
        onActivated: { const v = win.activeViewItem(); if (v && v.step) v.step(1); }
    }
    // The calendar lens' own keys (keymap.md, APP-264): [ ] a period, 0
    // today, z d / z w / z m the zoom. Fixed until the catalogue takes them
    // (APP-272); they stand down like every other view key.
    CalKey {
        sequences: ["["]
        onActivated: { const v = win.activeViewItem(); if (v && v.step) v.step(-1); }
    }
    CalKey {
        sequences: ["]"]
        onActivated: { const v = win.activeViewItem(); if (v && v.step) v.step(1); }
    }
    CalKey {
        sequences: ["0"]
        onActivated: AppController.selectedDate = AppController.today
    }
    CalKey {
        sequences: ["Z,D"]
        onActivated: AppController.currentView = "day"
    }
    CalKey {
        sequences: ["Z,W"]
        onActivated: AppController.currentView = "week"
    }
    CalKey {
        sequences: ["Z,M"]
        onActivated: AppController.currentView = "month"
    }
    // What a drag does in Week and Month, from the keyboard
    // (APP-249): the task that has the keyboard (or the pointer) moves a day,
    // a week, or a grid step. Ctrl+arrows are the board's own card moves;
    // these are live only in the three views that drag dates.
    readonly property bool _moveKeysOn: !win._viewKeysBlocked
        && ["day", "week", "month"].indexOf(AppController.currentView) >= 0
    function _moveViewTask(days, steps) {
        const v = win.activeViewItem();
        if (!v) return;
        if (days !== 0 && v.moveKeyTaskByDays) v.moveKeyTaskByDays(days);
        else if (steps !== 0 && v.moveKeyTaskByTime) v.moveKeyTaskByTime(steps);
    }
    Shortcut {
        sequences: [win._kbd("cal.taskEarlier")]
        context: Qt.ApplicationShortcut
        enabled: win._moveKeysOn && win._kbd("cal.taskEarlier").length > 0
        onActivated: win._moveViewTask(-1, 0)
    }
    Shortcut {
        sequences: [win._kbd("cal.taskLater")]
        context: Qt.ApplicationShortcut
        enabled: win._moveKeysOn && win._kbd("cal.taskLater").length > 0
        onActivated: win._moveViewTask(1, 0)
    }
    Shortcut {
        sequences: [win._kbd("cal.taskEarlierWeek")]
        context: Qt.ApplicationShortcut
        enabled: win._moveKeysOn && win._kbd("cal.taskEarlierWeek").length > 0
        onActivated: win._moveViewTask(-7, 0)
    }
    Shortcut {
        sequences: [win._kbd("cal.taskLaterWeek")]
        context: Qt.ApplicationShortcut
        enabled: win._moveKeysOn && win._kbd("cal.taskLaterWeek").length > 0
        onActivated: win._moveViewTask(7, 0)
    }
    Shortcut {
        sequences: [win._kbd("cal.taskTimeEarlier")]
        context: Qt.ApplicationShortcut
        enabled: win._moveKeysOn && win._kbd("cal.taskTimeEarlier").length > 0
        onActivated: win._moveViewTask(0, -1)
    }
    Shortcut {
        sequences: [win._kbd("cal.taskTimeLater")]
        context: Qt.ApplicationShortcut
        enabled: win._moveKeysOn && win._kbd("cal.taskTimeLater").length > 0
        onActivated: win._moveViewTask(0, 1)
    }
    DayKey {
        sequences: [_kbd("cal.goToDate")]
        onActivated: goToDatePopup.openAt(AppController.selectedDate, win.contentItem)
    }

    // The Monday recap: last week's column moves (WEAK PECAP). Opens itself
    // on the first launch of a week, and when the app is left running into
    // Monday; the palette opens it any time.
    // The time machine (APP-162): Settings → Data and the palette open it.
    TimeMachineDialog {
        id: timeMachine
    }

    EventLogDialog {
        id: eventLog
        onEntryActivated: (entry) => win.openLogEntry(entry)
    }

    WeeklyRecapDialog {
        id: weeklyRecap
        onTaskActivated: (id) => win.showTask(AppController.taskById(id))
        onStandupDraftRequested: standupDraft.showNow()
    }
    // The recap opens by itself only where it can be seen (APP-211): the
    // window on screen and in front, nothing open over it. A week that
    // turns while heap sits in the tray, minimised, behind other windows or
    // under the task editor waits for the next of these: the day turning
    // (midnight, resume from sleep), the window coming back to the front, an
    // overlay closing.
    function _maybeShowRecap() {
        if (!AppController.welcomeSeen) return;
        const onScreen = win.visible && win.visibility !== Window.Minimized && win.visibility !== Window.Hidden;
        weeklyRecap.showIfDue(onScreen, win.active && !win._captureActive, win._overlayOpen);
    }
    onActiveChanged: if (win.active) Qt.callLater(win._maybeShowRecap)
    on_OverlayOpenChanged: if (!win._overlayOpen) Qt.callLater(win._maybeShowRecap)
    // The standup draft (APP-170): text to edit and copy, sent nowhere.
    StandupDraftDialog { id: standupDraft }
    // "Send the status anyway?" for an issue the check found outside the
    // filter (APP-204). Cancel is the default.
    TrackerPushConfirmDialog { id: trackerPushConfirm }
    TrackerWriteAskDialog { id: trackerWriteAsk }
    // Errors & sync (0.8.1): the damaged-file card at launch, the keychain
    // card, the report form, the sync sources, what's new (R2-037…054).
    DamagedFileDialog { id: damagedFile }
    KeychainDialog { id: keychainCard }
    DoneColumnDialog {
        id: doneColumnCard
        onPickStageRequested: win.runCommand("settings:tasks")
    }
    ReportIssueDialog {
        id: reportIssue
        onNoticeRequested: (text) => toast.show(text, "success")
    }
    SyncPopover {
        id: syncPopover
        onSignInRequested: win.runCommand("settings:integrations")
    }
    WhatsNewDialog {
        id: whatsNew
        // Esc left the keyboard on the window root, and / j k did nothing
        // until a click (A3-5): the view takes it back, as after any dialog.
        onClosed: Qt.callLater(win.focusActiveView)
    }
    // The day's summary (APP-190): closed, carrying over, timers. Read-only.
    EndOfDayDialog {
        id: endOfDay
        onTaskActivated: (id) => win.showTask(AppController.taskById(id))
    }
    Connections {
        target: AppController
        function onTodayChanged() {
            win._maybeShowRecap();
        }
    }

    // ── Regions (APP-277) ──────────────────────────────────────────────
    // F6 / Shift F6 go round sidebar → content (onto the cursor) → the task
    // document → the header's filter line, instead of a
    // long Tab through every control. The region it lands in is framed until
    // the keyboard leaves it.
    property string regionShown: ""
    // The calendar's "Без даты" tray, when the view shows one: a region of
    // its own, so "pick one and press s" needs no 47 Tabs (A1-18).
    // A block's "Schedule..." and s on a tray row (A1-19): the "when" field,
    // under the tray row it came from, not at the window's corner (F2-NEW-3).
    function scheduleFromCalendar(id) {
        schedulePopup.openFor([id], "scheduled");
        const tray = win._trayItem();
        win._placeUnderCard(schedulePopup, id, tray ? tray.rowItem(id) : null);
    }

    function _trayItem() {
        // The calendar lens hands its inner grid to activeViewItem(); the
        // tray sits beside that grid, so look from the lens itself.
        const outer = viewLoader.item;
        const inner = win.activeViewItem();
        const v = outer && outer.objectName === "calendar-view" && outer["calendarView"] === inner ? outer : inner;
        if (!v) return null;
        const stack = [v];
        while (stack.length > 0) {
            const it = stack.pop();
            if (!it || !it.visible) continue;
            if (it.objectName === "undated-tray") return it.width > 0 ? it : null;
            const kids = it.children;
            for (let i = 0; i < kids.length; i++) stack.push(kids[i]);
        }
        return null;
    }
    function _regionOf(it) {
        const tray = win._trayItem();
        for (let p = it; p; p = p.parent) {
            if (tray && p === tray) return "tray";
            if (p === rail) return "sidebar";
            if (p === topBar) return "header";
            if (p === taskDoc) return "panel";
        }
        return "content";
    }
    function _regions() {
        const out = [];
        if (rail.visible && rail.width > 0) out.push("sidebar");
        out.push("content");
        if (win._trayItem()) out.push("tray");
        if (taskDoc.opened) out.push("panel");
        if (topBar.visible) out.push("header");
        return out;
    }
    function _regionItem(r) {
        return r === "sidebar" ? rail : r === "header" ? topBar
             : r === "panel" ? taskDoc : r === "tray" ? (win._trayItem() || viewArea) : viewArea;
    }
    function _firstTabStop(it) {
        const kids = it ? it.children : [];
        for (let i = 0; i < kids.length; i++) {
            const k = kids[i];
            if (!k.visible || k.enabled === false) continue;
            if (k.activeFocusOnTab === true) return k;
            const r = win._firstTabStop(k);
            if (r) return r;
        }
        return null;
    }
    function focusRegion(dir) {
        const list = win._regions();
        let i = list.indexOf(win._regionOf(win.activeFocusItem));
        if (i < 0) i = list.indexOf("content");
        // A view made of parts (Knowledge: the list, the document, the
        // links) walks them before F6 leaves it (A4-27). stepRegion() moves
        // the keyboard and returns the part, or null past the last one.
        const v = win.activeViewItem();
        const parts = !!v && typeof v.stepRegion === "function";
        if (parts && list[i] === "content" && !win._panelOpen) {
            const part = v.stepRegion(dir, false);
            if (part) { win._showRegion("content", part, true); return; }
        }
        const next = list[(i + dir + list.length) % list.length];
        if (next === "content" && parts && !win._panelOpen) {
            const part = v.stepRegion(dir, true);
            if (part) { win._showRegion("content", part, true); return; }
        }
        if (next === "content") win.focusActiveView();
        else if (next === "sidebar") rail.takeFocus();
        else if (next === "header") topBar.focusSearch();
        else {
            const stop = win._firstTabStop(win._regionItem(next));
            if (stop) stop.forceActiveFocus(Qt.TabFocusReason);
            else win._regionItem(next).forceActiveFocus(Qt.TabFocusReason);
        }
        win._showRegion(next, win._regionItem(next));
    }
    // The view part F6 framed (null for a whole region).
    property Item _regionPart: null
    function _isWithin(it, box) {
        for (let p = it; p; p = p.parent)
            if (p === box) return true;
        return false;
    }
    function _showRegion(name, r, part) {
        win._regionPart = part ? r : null;
        win.regionShown = name;
        const pos = r.mapToItem(win.contentItem, 0, 0);
        regionFrame.x = pos.x;
        regionFrame.y = pos.y;
        regionFrame.width = r.width;
        regionFrame.height = r.height;
    }
    Rectangle {
        id: regionFrame
        objectName: "region-frame"
        parent: win.contentItem
        z: 900
        // Drawn where F6 put it: a resize moves what it framed (VB5-R2).
        visible: win.regionShown.length > 0 && (!win._regionPart || win._regionPart.visible)
        color: "transparent"
        border.color: Theme.focusRing
        border.width: 1
    }
    Shortcut {
        sequences: [win._kbd("region.next")]
        context: Qt.ApplicationShortcut
        enabled: sequences.length > 0 && win._keyLive("region.next")
        onActivated: win.focusRegion(1)
    }
    Shortcut {
        sequences: [win._kbd("region.prev")]
        context: Qt.ApplicationShortcut
        enabled: sequences.length > 0 && win._keyLive("region.prev")
        onActivated: win.focusRegion(-1)
    }

    SchedulePopup {
        id: schedulePopup
        parent: win.contentItem
        // Placed by _placeUnderCard() on open.
        onPickDateRequested: (current, timed) => {
            schedDatePicker.openAt(current, win.contentItem, timed);
            // The picker takes the field's place, under the same card.
            schedDatePicker.x = Math.max(Theme.spXs, Math.min(schedulePopup.x, win.contentItem.width - schedDatePicker.width - Theme.spXs));
            schedDatePicker.y = Math.max(Theme.spXs, Math.min(schedulePopup.y, win.contentItem.height - schedDatePicker.height - Theme.spXs));
        }
        onClosed: Qt.callLater(win.returnFocusHome)
    }
    DatePickerPopup {
        id: schedDatePicker
        objectName: "schedule-date"
        withTimes: true
        onPickedAt: (value, timed) => schedulePopup.applyDate(value, timed)
    }

    // Jump straight to a day rather than paging to it. Anchored to the window
    // rather than to a view, because the view under it is swapped out.
    DatePickerPopup {
        id: goToDatePopup
        objectName: "go-to-date"
        // Centred over the window (a third of the way down, where the eye
        // is), not pinned to its top-left corner over the logo.
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 3) : 0
        onPicked: (value) => AppController.selectedDate = value
    }

    // Type to search on the board (APP-117): a letter that is not one of the
    // board's own shortcuts, typed with no text field focused, starts a search
    // in the top bar. Esc clears it; Return hands the keyboard back to the
    // board cursor, which walks what the search left.
    TypeAhead {
        id: boardTypeAhead
        // A card's menu is a popup, so _viewKeysBlocked already covers it.
        enabled: AppController.currentView === "board" && !win._viewKeysBlocked
        onTyped: (text) => topBar.typeAhead(text)
    }

    // The board's and the list's keys: not while typing, a popup or a
    // card's menu is up (its arrows and letters belong to it).
    readonly property bool _boardKeysOn: !win._viewKeysBlocked && win._cursorOn(win.activeViewItem())
    readonly property bool _boardOrList: AppController.currentView === "board" || AppController.currentView === "list"
    // The list's query comes from the window (APP-263); see the Bindings
    // beside the view loaders.
    readonly property bool _listOn: AppController.currentView === "list" && viewLoader.item !== null
    component BoardKey: Shortcut {
        context: Qt.ApplicationShortcut
        // Not while a card's menu is up: its arrows and letters belong to it.
        // The list (APP-263) walks with the same keys.
        enabled: sequences.length > 0 && win._boardKeysOn
    }

    BoardKey {
        sequences: [_kbd("board.cursorDown"), "Down"]
        onActivated: { const b = win.activeViewItem(); if (b && b.moveCursor) b.moveCursor(0, 1); }
    }
    BoardKey {
        sequences: [_kbd("board.cursorUp"), "Up"]
        onActivated: { const b = win.activeViewItem(); if (b && b.moveCursor) b.moveCursor(0, -1); }
    }
    BoardKey {
        sequences: [_kbd("board.cursorLeft"), "Left"]
        onActivated: { const b = win.activeViewItem(); if (b && b.moveCursor) b.moveCursor(-1, 0); }
    }
    BoardKey {
        sequences: [_kbd("board.cursorRight"), "Right"]
        onActivated: { const b = win.activeViewItem(); if (b && b.moveCursor) b.moveCursor(1, 0); }
    }
    BoardKey {
        sequences: [_kbd("board.open"), "Enter"]
        onActivated: { const b = win.activeViewItem(); if (b && b.openCursor) b.openCursor(); }
    }
    BoardKey {
        sequences: [_kbd("board.toggleSelect")]
        onActivated: { const b = win.activeViewItem(); if (b && b.toggleCursorSelection) b.toggleCursorSelection(); }
    }
    BoardKey {
        sequences: [win._kbd("board.moveDown")].concat(win._boardOrList ? ["Ctrl+Down"] : [])
        onActivated: { const b = win.activeViewItem(); if (b && b.moveCursorCard) b.moveCursorCard(0, 1); }
    }
    BoardKey {
        sequences: [win._kbd("board.moveUp")].concat(win._boardOrList ? ["Ctrl+Up"] : [])
        onActivated: { const b = win.activeViewItem(); if (b && b.moveCursorCard) b.moveCursorCard(0, -1); }
    }
    BoardKey {
        sequences: [win._kbd("board.moveLeft")].concat(win._boardOrList ? ["Ctrl+Left"] : [])
        onActivated: { const b = win.activeViewItem(); if (b && b.moveSelectionOrCard) b.moveSelectionOrCard(-1); }
    }
    BoardKey {
        sequences: [win._kbd("board.moveRight")].concat(win._boardOrList ? ["Ctrl+Right"] : [])
        onActivated: { const b = win.activeViewItem(); if (b && b.moveSelectionOrCard) b.moveSelectionOrCard(1); }
    }
    // Selecting from the keyboard (APP-128).
    BoardKey {
        sequences: [win._kbd("board.selectDown")]
        onActivated: { const b = win.activeViewItem(); if (b && b.extendSelection) b.extendSelection(1); }
    }
    BoardKey {
        sequences: [win._kbd("board.selectUp")]
        onActivated: { const b = win.activeViewItem(); if (b && b.extendSelection) b.extendSelection(-1); }
    }
    BoardKey {
        sequences: [win._kbd("board.selectColumnLeft")]
        onActivated: { const b = win.activeViewItem(); if (b && b.selectColumnAndStep) b.selectColumnAndStep(-1); }
    }
    BoardKey {
        sequences: [win._kbd("board.selectColumnRight")]
        onActivated: { const b = win.activeViewItem(); if (b && b.selectColumnAndStep) b.selectColumnAndStep(1); }
    }
    // The card menu, archive and fold, for the card/column the keyboard is on
    // (TASKS-32 / UX-26).
    BoardKey {
        sequences: [_kbd("board.cardMenu"), "Menu"]
        onActivated: { const b = win.activeViewItem(); if (b && b.openCursorMenu) b.openCursorMenu(); }
    }
    BoardKey {
        sequences: [_kbd("board.archive")]
        onActivated: { const b = win.activeViewItem(); if (b && b.archiveCursor) b.archiveCursor(); }
    }
    BoardKey {
        sequences: [_kbd("board.collapseColumn")]
        onActivated: { const b = win.activeViewItem(); if (b && b.toggleCursorColumn) b.toggleCursorColumn(); }
    }

    // s: the task (or the selection) into the next free slot of the selected
    // day, as the menu's "Schedule" does; 1–4: priority P0…P3 (keymap.md).
    function scheduleKeyTasks() {
        const ids = win._keyTaskIds();
        for (let i = 0; i < ids.length; i++)
            AppController.scheduleTaskAtNextFreeSlot(ids[i], AppController.selectedDate);
    }
    function priorityKeyTasks(p) {
        const ids = win._keyTaskIds();
        // The selection only when it is what the key acts on: with the panel
        // open that is the open task alone.
        if (!win._panelOpen && AppController.selectionCount > 0) { AppController.setSelectedTasksPriority(p); return; }
        if (ids.length > 0) AppController.setTaskPriority(ids[0], p);
    }
    readonly property bool _taskKeysOn: win._boardKeysOn
    // v marks the row on the list (H2-List's hint bar), as Space does.
    Shortcut {
        // The catalogue's key, not a letter of its own: a rebound
        // selection.toggle left a bare V marking rows (review 2026-10-19).
        // A bare key is the router's (_kbd gives ""), so this only carries a
        // chord.
        sequence: win._kbd("selection.toggle")
        context: Qt.ApplicationShortcut
        // Vim's own keys; Space marks it in either.
        enabled: !win._viewKeysBlocked && AppController.currentView === "list" && AppController.keymapScheme === "vim"
        onActivated: { const v = win.activeViewItem(); if (v && v.toggleCursorSelection) v.toggleCursorSelection(); }
    }

    // Done (APP-268): a bare letter, held back while typing or a popup is up.
    Shortcut {
        sequence: win._kbd("task.done")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && !win._viewKeysBlocked && !win._inKnowledge
        onActivated: win.markDone()
    }
    Shortcut {
        sequence: _kbd("task.openExternal")
        context: Qt.ApplicationShortcut
        enabled: sequence.length > 0 && !win._viewKeysBlocked
            && (AppController.currentView === "board"
                || AppController.currentView === "list"
                || AppController.currentView === "day"
                || AppController.currentView === "week")
        onActivated: {
            // One selected card is unambiguous. Otherwise act on whatever the
            // cursor is over — never on a whole multi-selection, which would
            // open a browser tab per card.
            let id = "";
            if (AppController.selectionCount === 1) {
                id = AppController.selectedTaskIds[0];
            } else if (AppController.selectionCount === 0) {
                const v = win.activeViewItem();
                if (v && v.hoveredTaskId) id = v.hoveredTaskId;
            }
            if (id) AppController.openTaskExternal(id);
        }
    }

    Connections {
        target: AppController
        function onActiveProfileChanged() {
            win.searchText = win.defaultQuery("", false);
            win.prioritiesFilter = ({});
            savedViewsHost.leave();   // views belong to the profile being left
            AppController.selectedDate = AppController.today;
            Qt.callLater(win.seedStarterDocs);
        }
    }

    // The Hotkeys popover (opened from the side rail). Re-clamped whenever
    // its height settles: on the first open it measures itself after it is
    // placed.
    HotkeysPanel {
        id: hotkeys
        onHeightChanged: if (opened && parent) win._placePopover(hotkeys, parent)
    }

    // ── Profile import / export via JSON file ──────────────────────────
    FileDialog {
        id: exportJsonDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["Tabless profile (*.json)", "All files (*)"]
        defaultSuffix: "json"
        title: I18n.t("dialog.exportProfile.title")
        onAccepted: {
            if (AppController.exportActiveProfileToFile(selectedFile))
                toast.show(I18n.t("toast.profile.exported"), "success");
            else
                win.notice(I18n.t("toast.profile.exportFail"), "error");
        }
    }
    FileDialog {
        id: importJsonDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [I18n.t("profileImport.filter"), "All files (*)"]
        title: I18n.t("dialog.importProfile.title")
        // A preview first, nothing is written before its button (R3-080).
        onAccepted: profileImportDialog.openFor(selectedFile)
    }
    ProfileImportDialog { id: profileImportDialog }

    // ── Calendar import / export via .ics ──────────────────────────────
    FileDialog {
        id: importIcsDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Calendar (*.ics)", "All files (*)"]
        title: I18n.t("dialog.importIcs.title")
        onAccepted: {
            const r = AppController.importIcs(selectedFile);
            if (r.error) {
                win.notice(r.error, "error");
                return;
            }
            // Counts, not a bare "done": a file that brought in nine events
            // and skipped one is neither a success nor a failure.
            // What was degraded is said, not only logged: an unsupported
            // rule means a series that shows as a single event.
            const warns = r.warnings || [];
            let msg = I18n.t("toast.ics.imported").arg(r.imported).arg(r.updated).arg(r.skipped);
            if (warns.length > 0)
                msg += " · " + (warns.length === 1 ? warns[0] : I18n.t("toast.ics.warnings").arg(warns.length).arg(warns[0]));
            win.notice(msg, (r.skipped > 0 || warns.length > 0) ? "warning" : "success");
            for (let i = 0; i < warns.length; i++) console.warn("[ics]", warns[i]);
        }
    }
    FileDialog {
        id: exportIcsDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["Calendar (*.ics)", "All files (*)"]
        defaultSuffix: "ics"
        title: I18n.t("dialog.exportIcs.title")
        onAccepted: {
            const ok = AppController.exportIcsToFile(selectedFile);
            win.notice(ok ? I18n.t("toast.ics.exported") : I18n.t("toast.ics.exportFail"),
                       ok ? "success" : "error");
        }
    }

    // ── Notes as a folder of .md files ─────────────────────────────────
    FileDialog {
        id: importVaultDialog
        fileMode: FileDialog.OpenFile
        // A folder picker, not a file picker: the user chooses any file in the
        // vault and heap takes the directory, because FolderDialog on Windows
        // hides the contents and people cannot tell which folder they are in.
        nameFilters: ["Markdown (*.md *.markdown)", "All files (*)"]
        title: I18n.t("dialog.importVault.title")
        // Nothing is imported from here: the folder is previewed first, so
        // what is about to change is visible before it does.
        onAccepted: vaultImportConfirm.openFor(currentFolder)
    }
    VaultImportDialog {
        id: vaultImportConfirm
        onOtherFolderRequested: importVaultDialog.open()
        onImported: (r) => {
            if (r.error) { win.notice(r.error, "error"); return; }
            const trouble = r.skipped > 0 || r.conflicts > 0;
            win.notice(I18n.t("toast.notes.importedFull")
                       .arg(r.imported).arg(r.updated).arg(r.kept).arg(r.conflicts).arg(r.skipped),
                       trouble ? "warning" : "success");
            for (let i = 0; i < r.warnings.length; i++) console.warn("[vault]", r.warnings[i]);
        }
    }
    FileDialog {
        id: exportVaultDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["All files (*)"]
        title: I18n.t("dialog.exportVault.title")
        // The typed name becomes the new folder the notes go into; nothing
        // already in the chosen directory is written over.
        onAccepted: {
            const file = String(selectedFile);
            const name = decodeURIComponent(file.substring(file.lastIndexOf("/") + 1));
            const r = AppController.exportNotesFolder(currentFolder, name);
            win.notice(r.error ? r.error
                               : I18n.t("toast.notes.exportedTo").arg(r.written).arg(r.folder),
                       r.error ? "error" : "success");
        }
    }

    // Spans the work area (the main column, beside the side rail and the
    // right panel): a wide one gets the stack in its bottom-right corner,
    // a narrow one its bottom centre (APP-225). Popups and dialogs sit on
    // the overlay above it, so a toast never covers Quick Capture or a
    // dialog's buttons.
    // The cheat sheet (APP-272): every key by area, with a search.
    // Focus mode on screen (DG-132): one task over everything.
    ImmersionView {
        id: immersionView
        anchors.fill: parent
        // Under the toasts (z 100): meetings still remind.
        z: 95
    }
    // Out of focus mode (Esc, d, the button) the keyboard goes back where it
    // came from: it stayed on the window root, where the view keys were dead
    // until another Esc (F2-NEW-2).
    Connections {
        id: immersionReturn
        target: immersionView
        property Item cameFrom: null
        function onVisibleChanged() {
            if (immersionView.visible) {
                immersionReturn.cameFrom = win.activeFocusItem;
                return;
            }
            const back = immersionReturn.cameFrom;
            immersionReturn.cameFrom = null;
            Qt.callLater(function () {
                if (immersionView.visible) return;
                if (back && back.visible && back.enabled) back.forceActiveFocus();
                else win.focusActiveView();
            });
        }
    }
    KeyCheatSheet {
        id: cheatSheet
        // "Change a key — Settings > Keys" lands there, as it says
        // (IDIOT-SHELL-15); it opened the sidebar popover.
        onEditRequested: Qt.callLater(function () { win.runCommand("settings:shortcuts"); })
    }

    // "g …": the first key of a sequence waits for its second (keymap rule 3).
    KeyPendingBar {
        id: keyPendingBar
        objectName: "key-pending-bar"
        maxWidth: mainColumn.width - 2 * Theme.spXl
        pending: keyRouter.pending
        isLive: function (id) { return win._keyLive(id); }
        x: mainColumn.x + Math.round((mainColumn.width - width) / 2)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spXl + win._selectionBarSpace
        z: 101
    }

    Toast {
        id: toast
        objectName: "toast"
        // "Журнал" on a burst of notices (a-27).
        onLogRequested: eventLog.showNow()
        x: mainColumn.x
        width: mainColumn.width
        areaWidth: mainColumn.width
        anchors.bottom: parent.bottom
        // Above the "g …" bar while it waits: it covered the toast's Undo
        // (A1-32); above the task document's Done bar too.
        anchors.bottomMargin: 24 + win._selectionBarSpace + taskDoc.toastLift
                              + (keyPendingBar.visible ? keyPendingBar.height + Theme.spMd : 0)
        z: 100
    }
    // Five or more tasks deleted at once (coach mark), over the "Tasks
    // deleted" toast: Undo now, the Time Machine later. It takes no keys, so
    // the toast's Undo works by key and by click while it is up.
    CoachMarkHost {
        id: timeMachineMark
        markId: "timeMachine"
        when: toast.count > 0
        anchorItem: toast.cardsItem
        sourceComponent: TimeMachineCoachmark {}
    }
    Connections {
        target: timeMachineMark._card
        ignoreUnknownSignals: true
        function onOpenTimeMachine() { timeMachine.showNow(); }
    }

    // Launch splash (R3-017, Review 2): leaves as soon as the window has
    // drawn its first frame; a press or a key in the meantime goes to the
    // app. Only the crash card (R3-018) waits for an answer.
    SplashScreen {
        id: splash
        objectName: "splash"
        anchors.fill: parent
        z: 9999
        onFinished: {
            if (Theme.reducedMotion) splash.visible = false;
            else splashFade.start();
            // The view takes the keyboard at launch — on the first run that
            // is the one input on screen, which looked ready and was not
            // (IDIOT-SHELL-9). A key or a dialog that got there first keeps it.
            if (!win._focusInPopup && !win._typing) win.focusActiveView();
        }
        onReportRequested: reportIssue.showNow()
        // The first frame is on screen: the scene is ready.
        Connections {
            target: win
            enabled: !splash.ready
            function onFrameSwapped() { splash.ready = true; }
        }
        // A window that is never exposed (offscreen, minimised start) still
        // gets past the splash.
        Timer { interval: 250; running: !splash.ready; onTriggered: splash.ready = true }

        NumberAnimation {
            id: splashFade
            target: splash; property: "opacity"; to: 0
            duration: Theme.durMoveOut; easing.type: Theme.easeExit
            onFinished: splash.visible = false
        }
    }
}
