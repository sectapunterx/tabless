// The last 0.9 round on the shell (APP-221, batch R3-ui): Tab through the
// task document reaches the plan under the text, the cheat sheet scrolls from
// the keyboard, focus mode gives the keyboard back, a toast follows the
// language, a long task title steps down and the text stops above Done, the
// date sample in Settings is in the app's format.
//
// Drives the real Main.qml with real key events.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "R3Ui"
    when: windowShown

    property var win: null
    property var seeded: []

    function initTestCase() {
        // These cases drive the in-place block editor ("live"); notes open
        // split since 0.9.2, so ask for it.
        AppController.appSettingsJson = JSON.stringify(Object.assign({}, JSON.parse(AppController.appSettingsJson || "{}"),
                                                                     { notes: { view: "live" } }));
        const comp = Qt.createComponent("qrc:/qt/qml/TodoCpp/qml/Main.qml");
        tryCompare(comp, "status", Component.Ready, 5000);
        verify(comp.status === Component.Ready, comp.errorString());
        tc.win = comp.createObject(null);
        verify(tc.win !== null);
        tc.win.width = 1456;
        tc.win.height = 939;
        AppController.resetAllShortcuts();
        wait(1200);   // splash
        tc.win.requestActivate();
        AppController.currentView = "board";
        tryVerify(function () { return tc.win.activeViewItem() !== null; }, 3000);
        wait(100);
    }

    function cleanupTestCase() {
        if (tc.win) tc.win.destroy();
        tc.win = null;
    }

    function seed(id, title, desc) {
        AppController.deleteTask(id);
        const d = AppController.newTaskDraft("todo");
        d._isNew = true;
        d.id = id;
        d.title = title;
        if (desc) d.desc = desc;
        AppController.saveTask(d);
        tc.seeded.push(id);
    }

    function init() {
        tc.seeded = [];
        closeAll();
    }

    function cleanup() {
        closeAll();
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteTask(tc.seeded[i]);
        tc.seeded = [];
        AppController.clearPendingUndo();
        tc.win.width = 1456;
        tc.win.height = 939;
        tc.win.focusActiveView();
    }

    function popups() {
        const out = [];
        const cd = tc.win.contentData;
        for (let i = 0; i < cd.length; i++) {
            const o = cd[i];
            if (o && o.opened !== undefined && o.open !== undefined) out.push(o);
        }
        return out;
    }
    function find(root, pred) {
        if (!root) return null;
        if (pred(root)) return root;
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) {
            const r = find(kids[i], pred);
            if (r) return r;
        }
        return null;
    }
    // The popups' items live on the overlay, beside the content item.
    function byName(name) {
        return find(tc.win.contentItem.parent || tc.win.contentItem, function (it) { return it.objectName === name; });
    }
    function focusName() {
        const a = tc.win.activeFocusItem;
        return a ? String(a.objectName) : "";
    }
    function focusInside(name) {
        for (let p = tc.win.activeFocusItem; p; p = p.parent)
            if (String(p.objectName) === name) return true;
        return false;
    }
    // The named item the keyboard is on, or inside (a chip's own ClickArea).
    function focusWithin() {
        for (let p = tc.win.activeFocusItem; p; p = p.parent)
            if (String(p.objectName).length > 0) return String(p.objectName);
        return "";
    }
    function closeAll() {
        const ps = popups();
        for (let i = 0; i < ps.length; i++) if (ps[i].opened) ps[i].close();
        if (AppController.immersion) AppController.stopImmersion();
        wait(150);
        AppController.clearSelection();
        tc.win.focusActiveView();
        wait(20);
    }
    function tabUntil(name, max) {
        for (let i = 0; i < max; i++) {
            keyClick(Qt.Key_Tab);
            wait(40);
            if (focusName() === name) return i + 1;
        }
        return -1;
    }

    // F2-REG-1 (A4-15): Tab walks from the title through the text to the
    // first plan item — with no description (the caret opens an empty line,
    // where Tab indented for ever) and with one.
    function test_tab_reaches_the_plan_under_the_text_data() {
        // Passing through a text that opens on a list item leaves it as it is.
        return [ { tag: "no description", desc: "" }, { tag: "a description", desc: "Some words here" },
                 { tag: "a list", desc: "- first\n- second" } ];
    }
    function test_tab_reaches_the_plan_under_the_text(data) {
        seed("R3T-1", "r3 tab probe", data.desc);
        AppController.setTaskChecklistText("R3T-1", "- [ ] one\n- [ ] two");
        tc.win.openTask("R3T-1");
        tryVerify(function () { const r = byName("cl-item-0"); return r !== null && r.visible; }, 3000);
        const title = byName("task-doc-title");
        title.forceActiveFocus();
        wait(30);
        const n = tabUntil("cl-item-0", 20);
        verify(n > 0, "Tab never reached the plan, stuck on " + focusName());
        compare(AppController.taskById("R3T-1").desc || "", data.desc, "Tab typed into the text");
        // j / d on the item it reached are the item's.
        keyClick(Qt.Key_J);
        tryCompare(tc.win.activeFocusItem, "objectName", "cl-item-1");
        // Back: Shift+Tab from the text goes to what is above it — the
        // chips, or on a narrow window the properties' line under them
        // (Pick-Narrow) — not round to the bottom of the plan.
        byName("task-doc-body").focusEditor();
        tryCompare(tc.win.activeFocusItem, "objectName", "md-block-field");
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
        wait(40);
        compare(focusWithin(), tc.win.smallScreen ? "task-doc-meta-more-area" : "task-doc-add", "Shift+Tab from the text");
        compare(AppController.taskById("R3T-1").desc || "", data.desc, "Shift+Tab changed the text");
        // On backward past the top: round to the document's end, never to
        // the board behind it.
        for (let i = 0; i < 14; i++) {
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
            wait(20);
            verify(focusInside("task-doc"), "Shift+Tab left the document for " + focusWithin());
        }
        // And forward all the way round, never out either.
        for (let i = 0; i < 30; i++) {
            keyClick(Qt.Key_Tab);
            wait(20);
            verify(focusInside("task-doc"), "Tab left the document for " + focusWithin());
        }
        // Through the plan to its add field, and on out of it: Tab there
        // typed a tab into the field for ever.
        byName("cl-item-1").forceActiveFocus();
        verify(tabUntil("cl-add", 6) > 0, "Tab never reached the add field: " + focusWithin());
        keyClick(Qt.Key_Tab);
        wait(40);
        verify(focusName() !== "cl-add", "Tab stayed in the plan's add field");
        compare(byName("cl-add").text, "", "Tab typed into the add field");
        // Backward from the add field: the last item.
        byName("cl-add").forceActiveFocus();
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
        wait(40);
        verify(focusInside("cl-item-1"), "Shift+Tab from the add field: " + focusWithin());
    }

    // Esc in the text leaves the block for the drawn text, not for the
    // plan's add field the view had last.
    function test_esc_in_the_text_stays_on_the_text() {
        seed("R3T-5", "r3 esc probe", "Some words");
        AppController.setTaskChecklistText("R3T-5", "- [ ] one");
        tc.win.openTask("R3T-5");
        tryVerify(function () { const r = byName("cl-add"); return r !== null; }, 3000);
        byName("cl-add").forceActiveFocus();
        wait(30);
        const body = byName("task-doc-body");
        body.focusEditor();
        tryCompare(tc.win.activeFocusItem, "objectName", "md-block-field");
        keyClick(Qt.Key_Escape);
        wait(40);
        verify(focusName() !== "cl-add", "Esc moved to the plan's add field");
        verify(body.documentFocused, "the drawn text does not hold the keyboard: " + focusName());
    }

    // Tab still indents a list item in the text.
    function test_tab_indents_a_list_item() {
        seed("R3T-2", "r3 indent probe", "- first\n- second");
        tc.win.openTask("R3T-2");
        const body = byName("task-doc-body");
        tryVerify(function () { return body !== null && body.visible; }, 3000);
        body.focusEditor();
        tryCompare(tc.win.activeFocusItem, "objectName", "md-block-field");
        const field = tc.win.activeFocusItem;
        field.cursorPosition = field.text.length;
        keyClick(Qt.Key_Tab);
        wait(50);
        compare(focusName(), "md-block-field", "Tab on a list item left the text");
        verify(/\n\s+- second/.test(field.text), "the item was not indented: " + JSON.stringify(field.text));
        // Shift+Tab takes the indent back; on an item with none it moves on.
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
        wait(50);
        compare(focusName(), "md-block-field");
        verify(/\n- second/.test(field.text), "not outdented: " + JSON.stringify(field.text));
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
        wait(50);
        verify(focusName() !== "md-block-field", "Shift+Tab on an item with no indent stayed in the text");
    }

    // F3-EYES-10: a long title steps its size down; the text is clipped
    // above Done instead of running on under it.
    function test_a_long_title_steps_down() {
        tc.win.width = 1280;
        tc.win.height = 720;
        seed("R3T-3", "short", "");
        seed("R3T-4", "Find out why the CSV export of the report for clients with more than ten thousand rows "
             + "cuts the last records and writes no error to the service log or the interface, check the driver buffer", "");
        tc.win.openTask("R3T-3");
        let title = null;
        tryVerify(function () { title = byName("task-doc-title"); return title !== null && title.visible && title.text === "short"; }, 3000);
        const full = title.font.pixelSize;
        tc.win.openTask("R3T-4");
        tryVerify(function () { return title.text.indexOf("CSV") > 0; }, 3000);
        wait(50);
        verify(title.font.pixelSize < full, "the long title kept " + title.font.pixelSize + "px");
        verify(title.lineCount <= 5, "still " + title.lineCount + " lines");
        verify(byName("task-doc-body").clip, "the text runs on under Done");
    }

    // F3-EYES-5: at 1280x720 the cheat sheet is longer than the window: the
    // keys scroll it from its search field.
    function test_the_cheat_sheet_scrolls_from_the_keyboard() {
        tc.win.width = 1280;
        tc.win.height = 720;
        wait(50);
        tc.win.openCheatSheet();
        const sheet = popups().filter(p => p.objectName === "key-cheat-sheet")[0];
        verify(sheet !== undefined);
        tryCompare(sheet, "opened", true);
        tryCompare(tc.win.activeFocusItem, "objectName", "key-sheet-search");
        const inSheet = function (name) { return find(sheet.contentItem, function (it) { return it.objectName === name; }); };
        const flick = inSheet("key-sheet-fade").parent;
        verify(flick.contentHeight > flick.height, "nothing to scroll at 1280x720");
        // a-33 (N/X-Keys): one dim footer line, no scroll hint of its own.
        verify(inSheet("key-sheet-scroll-hint") === null, "the footer has a scroll hint again");
        compare(inSheet("key-sheet-edit").color, Theme.textDim);
        compare(flick.contentY, 0);
        keyClick(Qt.Key_PageDown);
        tryVerify(function () { return flick.contentY > 0; }, 1000, "PgDn did not scroll");
        const y = flick.contentY;
        keyClick(Qt.Key_Down);
        tryVerify(function () { return flick.contentY > y || flick.contentY >= flick.contentHeight - flick.height - 1; }, 1000);
        keyClick(Qt.Key_End, Qt.NoModifier);   // the field's own key: no scroll
        for (let i = 0; i < 60; i++) keyClick(Qt.Key_PageDown);
        tryVerify(function () { return Math.abs(flick.contentY - (flick.contentHeight - flick.height)) < 2; }, 1000,
                  "the end of the sheet is out of reach");
        keyClick(Qt.Key_U, Qt.ControlModifier);
        tryVerify(function () { return flick.contentY < flick.contentHeight - flick.height - 1; }, 1000, "Ctrl U");
        keyClick(Qt.Key_Escape);
        tryCompare(sheet, "opened", false);
    }

    // F2-NEW-2: Esc out of focus mode hands the keyboard back where it was.
    function test_focus_mode_esc_gives_the_keyboard_back() {
        const saved = AppController.appSettingsJson;
        try {
            AppController.setSafetySetting("immersion", true);
            seed("R3T-5", "r3 focus probe", "");
            AppController.setTaskChecklistText("R3T-5", "- [ ] one");
            AppController.currentView = "today";
            wait(100);
            tc.win.focusActiveView();
            wait(50);
            const before = tc.win.activeFocusItem;
            verify(before !== null && before !== tc.win.contentItem);
            AppController.startImmersion("R3T-5");
            tryVerify(function () { return AppController.immersion; }, 1000);
            wait(100);
            // a-35 (N/X-Ntf-Focus): 13px checkboxes, the clock at 15px.
            compare(byName("immersion-check-box").width, Theme.px(13));
            compare(byName("immersion-timer").font.pixelSize, Theme.px(15));
            keyClick(Qt.Key_Escape);
            tryVerify(function () { return !AppController.immersion; }, 1000);
            tryVerify(function () { return tc.win.activeFocusItem === before; }, 1000,
                      "focus went to " + tc.win.activeFocusItem);
            // From the open document: back into it.
            tc.win.openTask("R3T-5");
            tryVerify(function () { const t = byName("task-doc-title"); return t && t.visible; }, 3000);
            wait(100);
            const inDoc = tc.win.activeFocusItem;
            AppController.startImmersion("R3T-5");
            tryVerify(function () { return AppController.immersion; }, 1000);
            wait(100);
            keyClick(Qt.Key_Escape);
            tryVerify(function () { return !AppController.immersion; }, 1000);
            tryVerify(function () { return tc.win.activeFocusItem === inDoc; }, 1000,
                      "focus went to " + tc.win.activeFocusItem);
        } finally {
            AppController.appSettingsJson = saved;
        }
    }

    // F3-EYES-19: a toast that can say it again follows the language; one
    // that cannot goes instead of staying in the old language.
    function test_a_toast_follows_the_language() {
        const was = AppController.language;
        const toast = byName("toast");
        verify(toast !== null);
        try {
            AppController.language = "ru";
            toast.clear();
            toast.showWithAction(I18n.t("update.line.whatsNew"), I18n.t("update.line.whatsNew"), 10, function () {},
                                 "info", "", true, function () {
                                     return { message: I18n.t("update.line.whatsNew"), actionLabel: I18n.t("update.line.whatsNew") };
                                 });
            compare(toast.message, I18n.dict.ru["update.line.whatsNew"]);
            AppController.language = "en";
            tryCompare(toast, "message", I18n.dict.en["update.line.whatsNew"]);
            compare(toast.actionLabel, I18n.dict.en["update.line.whatsNew"]);
            toast.clear();
            toast.show("plain words");
            compare(toast.count, 1);
            AppController.language = "ru";
            tryCompare(toast, "count", 0);
            // An Undo keeps its button, worded in the new language.
            toast.showUndo("deleted", I18n.t("undo.action"), 10, function () {}, "success", "", 0);
            compare(toast.actionLabel, I18n.dict.ru["undo.action"]);
            AppController.language = "en";
            wait(50);
            compare(toast.count, 1, "the Undo toast went with the language");
            compare(toast.actionLabel, I18n.dict.en["undo.action"]);
        } finally {
            AppController.language = was;
            toast.clear();
        }
    }

    // F3-EYES-12: the clock setting's sample date is in the app's own format.
    function test_the_date_sample_is_the_apps_format() {
        AppController.currentView = "settings";
        let row = null;
        tryVerify(function () { row = byName("settings-time-format"); return row !== null; }, 3000);
        const d = new Date();
        compare(String(row.status).split(" · ")[0], I18n.fmtDate(d, "weekdayDay"));
        AppController.currentView = "board";
    }
}
