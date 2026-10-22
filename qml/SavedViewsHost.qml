// The window's side of saved views: which one is active, whether the filters
// have moved away from it, and applying one. The views themselves are
// AppController.savedViews (per profile, undoable); the filters a view
// restores live on the window (Main.qml: searchText, prioritiesFilter,
// boardSortMode, showArchived, showDoneTimeline), so applying one is here.
//
// Kept out of Main.qml, which is long enough: Main makes one of these, hands it
// itself as `host`, and routes the filter bar, the sidebar and the palette to
// it.
import QtQuick
import TodoCpp

Item {
    id: root
    // Main.qml's window. Read and written: the filter properties above,
    // _activePriorities, _kbd(), _globalKeysOn.
    property var host: null

    // The view the filters were last set from. Cleared when it is deleted, on
    // a profile switch, and by "leave view".
    property string activeId: ""

    readonly property var activeView: {
        const list = AppController.savedViews;
        for (let i = 0; i < list.length; i++)
            if (list[i].id === root.activeId) return list[i];
        return null;
    }

    // The filter state as a saved view stores it. Off a task view (Notes,
    // Docs, Settings) the active view's own view stands in, so walking over to
    // Notes and back does not mark it modified.
    function currentState() {
        if (!root.host) return ({});
        const cur = AppController.currentView;
        const taskView = ["board", "list", "week", "month"].indexOf(cur) >= 0;
        return {
            query: root.uniqueClauses(root.host.searchText),
            priorities: root.host._activePriorities,
            sort: root.host.boardSortMode,
            archived: root.host.showArchived,
            showDone: root.host.showDoneTimeline,
            // A view saved on the 0.8.0 timeline or archive is the list now:
            // being there is not a change to it.
            view: taskView ? (root.activeView && cur === "list"
                              && (root.activeView.view === "timeline" || root.activeView.view === "archive")
                              ? root.activeView.view : cur)
                           : (root.activeView ? root.activeView.view : "board")
        };
    }

    // A clause said twice is said once: a view was stored as "is:open
    // priority:p1 priority:p1" (PERSONA-17). Words and OR queries stay as
    // typed.
    function uniqueClauses(q) {
        const toks = String(q || "").match(/"[^"]*"|\S+/g) || [];
        if (toks.indexOf("OR") >= 0 || toks.indexOf("|") >= 0) return String(q || "");
        const seen = {};
        return toks.filter(t => {
            if (t.indexOf(":") <= 0) return true;
            const k = t.toLowerCase();
            if (seen[k]) return false;
            seen[k] = true;
            return true;
        }).join(" ");
    }

    readonly property bool modified: {
        if (!root.activeView || !root.host) return false;
        // currentState() reads the window's filters, so the binding follows
        // them; activeView follows AppController.savedViews.
        return AppController.savedViewDiffers(root.activeId, root.currentState());
    }

    // The query a view puts in the filter. A view saved on the archive of
    // 0.8.0 opens on the list (DG-161/162) and keeps its "is:archived".
    function appliedQuery(v) {
        return v.view === "archive" && !/(^|\s)is:archived(\s|$)/i.test(v.query)
            ? (v.query + " is:archived").trim() : v.query;
    }
    // While the filter is exactly the active view's query, a `status:` on a
    // column deleted since matches nothing — on the board, in the list and in
    // the sidebar count alike (IDIOT-TASKS-11). A typed query stays tolerant.
    Binding {
        target: AppController
        property: "strictQuery"
        value: root.activeView ? root.appliedQuery(root.activeView) : ""
    }

    function apply(id) {
        const list = AppController.savedViews;
        let v = null;
        for (let i = 0; i < list.length; i++) if (list[i].id === id) v = list[i];
        if (!v || !root.host) return false;
        const chips = {};
        for (let j = 0; j < v.priorities.length; j++) chips[v.priorities[j]] = true;
        // A view saved on the archive or the timeline of 0.8.0 opens on the
        // list (DG-161/162).
        root.host.searchText = root.appliedQuery(v);
        root.host.prioritiesFilter = chips;
        root.host.boardSortMode = v.sort;
        root.host.showArchived = v.archived;
        root.host.showDoneTimeline = v.showDone;
        AppController.currentView = v.view;
        root.activeId = id;
        return true;
    }

    // A click on the view being followed takes it off: the filters go back
    // to none, as they were before it was picked. Only while it is on
    // screen as saved and unchanged: from Today its key turned it off and
    // showed nothing (A1-27); there, and after the filter moved (A6-24), the
    // key opens it.
    function toggle(id) {
        if (id !== "" && id === root.activeId && !root.modified && root.shownAsSaved()) { root.clear(); return true; }
        return root.apply(id);
    }
    // The active view is what the screen shows: a task view of its kind,
    // with its filters untouched.
    function shownAsSaved() {
        if (!root.activeView) return false;
        if (["board", "list", "week", "month"].indexOf(AppController.currentView) < 0) return false;
        return !root.modified && root.currentState().view === root.activeView.view;
    }

    // The Nth view's key (its catalogue key, g 1…): the Nth view in the
    // sidebar, 1-based; pressed again while it is shown, off.
    function applyAt(n) {
        const list = AppController.savedViews;
        if (n < 1 || n > list.length) return false;
        return root.toggle(list[n - 1].id);
    }

    // What a view would capture, one line: the dialog shows it under the name.
    function describe(state) {
        const parts = [];
        if (state.query && state.query.length > 0) parts.push(state.query);
        if (state.priorities && state.priorities.length > 0) parts.push(state.priorities.join(","));
        if (state.archived) parts.push(I18n.t("filter.archived"));
        if (state.view === "board" && state.sort && state.sort !== "manual")
            parts.push(I18n.t("filter.sortBy") + " " + I18n.t("filter.sort." + state.sort));
        parts.push(I18n.t("siderail." + state.view));
        return parts.join(" · ");
    }

    // A name to start from: the query when there is one, else the chips, else
    // the view's own name — something the user can accept with one Enter.
    // A query as a name (X-Dlg-Small, R3-077): "статус:заблокировано p0" →
    // "Заблокировано · P0" — the values, not the keys.
    function readableQuery(raw) {
        const parts = String(raw).trim().match(/(?:[^\s"]+:)?"[^"]*"|\S+/g) || [];
        const words = parts.map(w => {
            let v = w.indexOf(":") > 0 && !/^https?:/i.test(w) ? w.slice(w.indexOf(":") + 1) : w;
            v = v.replace(/^"|"$/g, "");
            // "is:open" in the language of the UI: "Open · P1" in a Russian
            // one was English (PERSONA-18).
            if (/^is:/i.test(w)) {
                const said = I18n.t("query.is." + v.toLowerCase());
                if (said.indexOf("query.") !== 0) v = said;
            }
            return /^p[0-3]$/i.test(v) ? v.toUpperCase() : v;
        }).filter(v => v.length > 0);
        const out = words.join(" · ");
        return out.length > 0 ? out.charAt(0).toUpperCase() + out.slice(1) : "";
    }
    function suggestName(state) {
        const q = root.readableQuery(state.query || "");
        if (q.length > 0) return q.length > 40 ? q.slice(0, 39) + "…" : q;
        if (state.priorities && state.priorities.length > 0) return state.priorities.join(" · ");
        return I18n.t("siderail." + state.view);
    }

    function openSave() {
        const s = root.currentState();
        nameDialog.targetId = "";
        nameDialog.paletteQuery = false;
        nameDialog.openFor("save", root.suggestName(s), s.query || "", AppController.savedViews.length + 1);
    }
    // Ctrl S in the command line saves what it says, not the filter on
    // screen, and leaves that filter alone, saved or cancelled (A3-15).
    function openSaveQuery(query) {
        const s = root.currentState();
        s.query = String(query || "").trim();
        s.priorities = [];
        nameDialog.targetId = "";
        nameDialog.paletteQuery = true;
        nameDialog.openFor("save", root.suggestName(s), s.query, AppController.savedViews.length + 1);
    }
    function openRename(id) {
        const v = AppController.savedView(id);
        if (!v.id) return;
        nameDialog.targetId = id;
        nameDialog.openFor("rename", v.name, v.query || "", 0);
    }
    // "Изменить запрос…" (X-Menus-Other, DG-150): the name and the query
    // of a view in one dialog; the rest of the view stays as saved.
    function openEdit(id) {
        const v = AppController.savedView(id);
        if (!v.id) return;
        nameDialog.targetId = id;
        nameDialog.openFor("edit", v.name, v.query || "", 0);
    }
    function updateActive() {
        if (root.activeView) AppController.updateSavedView(root.activeId, root.currentState());
    }
    function updateFromCurrent(id) {
        if (AppController.updateSavedView(id, root.currentState())) root.activeId = id;
    }
    function leave() { root.activeId = ""; }
    // Off the view and its filters: the "×" on the chip and a second click.
    function clear() {
        root.activeId = "";
        if (!root.host) return;
        root.host.resetTaskFilter();
        root.host.showArchived = false;
        root.host.showDoneTimeline = false;
    }

    SavedViewNameDialog {
        id: nameDialog
        property string targetId: ""
        // Opened from the command line: the view is its query alone and the
        // filters on screen stay as they are.
        property bool paletteQuery: false
        onNamed: (name) => {
            if (nameDialog.mode === "rename") {
                AppController.renameSavedView(nameDialog.targetId, name);
                return;
            }
            if (nameDialog.mode === "edit") {
                const v = AppController.savedView(nameDialog.targetId);
                if (!v.id) return;
                AppController.renameSavedView(v.id, name);
                AppController.updateSavedView(v.id, { query: nameDialog.query.trim(), priorities: v.priorities, sort: v.sort,
                                                      archived: v.archived, showDone: v.showDone, view: v.view });
                if (root.activeId === v.id) root.apply(v.id);
                return;
            }
            const st = root.currentState();
            st.query = nameDialog.query.trim();
            if (nameDialog.paletteQuery) {
                st.priorities = [];
                st.archived = false;
                st.showDone = false;
                AppController.saveView(name, st);
                return;
            }
            const id = AppController.saveView(name, st);
            if (id.length > 0) root.activeId = id;
        }
    }
    readonly property alias nameDialog: nameDialog

    // savedView.1 … savedView.9 (Ctrl 6… for the first four; g 1… is KeyRouter's).
    // An inline component does not see this file's ids, so each key is handed
    // the host it works for.
    component ViewKey: Shortcut {
        id: vk
        property int n: 1
        property var owner: null
        sequence: vk.owner && vk.owner.host ? vk.owner.host._kbd("savedView." + vk.n) : ""
        context: Qt.ApplicationShortcut
        // Not while a field takes typed text: a saved view replaced the
        // filter being typed in the header (IDIOT-SHELL-7).
        enabled: String(vk.sequence).length > 0 && !!vk.owner && !!vk.owner.host && vk.owner.host._globalKeysOn
                 && !vk.owner.host._typing && vk.n <= AppController.savedViews.length
        onActivated: vk.owner.applyAt(vk.n)
    }
    ViewKey { n: 1; owner: root }
    ViewKey { n: 2; owner: root }
    ViewKey { n: 3; owner: root }
    ViewKey { n: 4; owner: root }
    ViewKey { n: 5; owner: root }
    ViewKey { n: 6; owner: root }
    ViewKey { n: 7; owner: root }
    ViewKey { n: 8; owner: root }
    ViewKey { n: 9; owner: root }
}
