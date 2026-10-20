// Notes, dogfood 2026-10-19: a table pasted from a page, a spreadsheet or a
// spec becomes a pipe table; its rows and columns are added and removed from
// the menu, the "/" menu, the palette and the keys; the text column and the
// code column have visible right edges that drag, reset and are settings.
import QtQuick
import QtQuick.Controls.Basic
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "NotesTables"
    when: windowShown
    visible: true
    width: 1600
    height: 700

    Item { id: host; anchors.fill: parent }
    // To reach the clipboard, as the app's own code blocks do.
    TextEdit { id: clip; visible: false }
    MdHighlighter { id: hl }
    Component { id: menuComp; TextEditMenu {} }

    property var made: []
    property string _settingsBefore: ""

    function initTestCase() {
        tc._settingsBefore = AppController.appSettingsJson;
    }
    function init() {
        // The profile persists between runs: start from the default columns.
        tc.setNotesPrefs({ textWidth: undefined, codeWidth: undefined, textEdge: undefined, codeEdge: undefined, view: "rich" });
    }
    function cleanup() {
        for (let i = 0; i < tc.made.length; i++) AppController.deleteNote(tc.made[i]);
        tc.made = [];
        AppController.clearPendingUndo();
    }
    function cleanupTestCase() {
        AppController.appSettingsJson = tc._settingsBefore;
    }

    function setNotesPrefs(p) {
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}"); } catch (e) { s = {}; }
        const n = Object.assign({}, s.notes || {});
        for (const k in p) {
            if (p[k] === undefined) delete n[k];
            else n[k] = p[k];
        }
        s.notes = n;
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function notesPref(key) {
        try { return (JSON.parse(AppController.appSettingsJson || "{}").notes || {})[key]; } catch (e) { return undefined; }
    }

    function make(body) {
        const id = AppController.newNote("Tables " + tc.made.length);
        tc.made.push(id);
        const nv = createTemporaryQmlObject('import TodoCpp; NotesView { anchors.fill: parent }', host);
        verify(nv !== null);
        tryVerify(function () { return nv._loadedOnce; });
        const ed = findChild(nv, "notesEditor");
        verify(ed !== null);
        ed.text = body;
        return nv;
    }
    function editorOf(nv) { return findChild(nv, "notesEditor"); }

    function copyText(t) {
        clip.text = t;
        clip.selectAll();
        clip.copy();
    }

    readonly property string table: "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nAfter"

    // ── Paste ────────────────────────────────────────────────────────

    function test_paste_of_tab_separated_rows_makes_a_table() {
        const nv = make("Before");
        const ed = editorOf(nv);
        copyText("Name\tType\nid\tint\n");
        compare(AppController.clipboardTableMarkdown(), "| Name | Type |\n| --- | --- |\n| id | int |");
        ed.forceActiveFocus();
        ed.cursorPosition = ed.text.length;
        keyClick(Qt.Key_V, Qt.ControlModifier);
        compare(ed.text, "Before\n\n| Name | Type |\n| --- | --- |\n| id | int |\n");
        // One undo step leaves the plain paste (review 2026-10-19), the next
        // takes that back too.
        ed.undo();
        compare(ed.text, "BeforeName\tType\nid\tint\n");
        ed.undo();
        compare(ed.text, "Before");
    }

    function test_code_aligned_with_tabs_pastes_as_code() {
        const nv = make("x");
        const ed = editorOf(nv);
        copyText("int\tfoo;\nchar\tbar;");
        compare(AppController.clipboardTableMarkdown(), "");
        ed.forceActiveFocus();
        ed.cursorPosition = 1;
        keyClick(Qt.Key_V, Qt.ControlModifier);
        compare(ed.text, "xint\tfoo;\nchar\tbar;");
    }

    function test_paste_of_the_spec_sample_makes_a_table() {
        const nv = make("");
        const ed = editorOf(nv);
        copyText("Name\n    \n\nFormatUnitRangeDimensionsSourceDescription$\\mathrm{I}^{\\mathrm{~UeEid}}$$\\textsf{integer}$$\\left[0,...,\\infty\\right]$$1$ID16218Ue identifier.$\\mathrm{P}^\\mathrm{~AnValue}$$\\textsf{enum}$$\\left[\\textsf{ACK},\n\\textsf{NACK},\n\\textsf{DTX}\\right]$$1$ID16218AN value from DL HARQ report.");
        ed.forceActiveFocus();
        keyClick(Qt.Key_V, Qt.ControlModifier);
        verify(ed.text.indexOf("| Name | Format | Unit | Range | Dimensions | Source | Description |") === 0, ed.text);
        compare(ed.text.split("\n").length, 5);  // header, rule, two rows, the line after
        // And it is drawn as a table, its formulas typeset.
        const decos = hl.decorations(ed.text);
        const t = decos.filter((d) => d.kind === "table");
        compare(t.length, 1);
        compare(t[0].rows.length, 4);
        verify(t[0].rows[3].plain[2].indexOf("∞") < 0);
        verify(t[0].rows[2].plain[2].indexOf("∞") >= 0, JSON.stringify(t[0].rows[2].plain));
    }

    function test_plain_text_paste_is_left_alone_and_shift_paste_is_plain() {
        const nv = make("x");
        const ed = editorOf(nv);
        copyText("just words $5 and $6");
        compare(AppController.clipboardTableMarkdown(), "");
        ed.forceActiveFocus();
        ed.cursorPosition = 1;
        keyClick(Qt.Key_V, Qt.ControlModifier);
        compare(ed.text, "xjust words $5 and $6");
        // Ctrl+Shift+V: the clipboard's text as it is, even a table.
        ed.text = "y";
        copyText("a\tb\nc\td");
        ed.cursorPosition = 1;
        keyClick(Qt.Key_V, Qt.ControlModifier | Qt.ShiftModifier);
        compare(ed.text, "ya\tb\nc\td");
    }

    function test_paste_works_in_split_and_edit() {
        for (const mode of ["split", "edit"]) {
            const nv = make("");
            nv.viewMode = mode;
            const ed = editorOf(nv);
            copyText("a\tb\n1\t2");
            ed.forceActiveFocus();
            keyClick(Qt.Key_V, Qt.ControlModifier);
            compare(ed.text, "| a | b |\n| --- | --- |\n| 1 | 2 |\n", mode);
            nv.destroy();
        }
    }

    // ── A big table, typing elsewhere (review 2026-10-19) ────────────
    // Every keystroke made new decorations, and every table cell - a
    // typeset formula each - was built again: a note with a big spec table
    // lagged. A letter typed in the prose keeps the table's cells as they are.
    function test_typing_beside_a_big_formula_table_keeps_its_cells() {
        let body = "Prose above.\n\n| Name | Format | Range | Unit |\n| --- | --- | --- | --- |\n";
        for (let r = 0; r < 50; r++)
            body += "| $\\mathrm{I}^{\\mathrm{~Ue" + r + "}}$ | $\\textsf{integer}$ | $\\left[0,...,\\infty\\right]$ | $\\frac{a_{" + r + "}}{b^{2}}$ |\n";
        const nv = make(body);
        const ed = editorOf(nv);
        let cell = null;
        tryVerify(function () { cell = findChild(nv, "md-live-table-cell"); return cell !== null && cell.text.length > 0; });
        wait(50);
        let best = 1e9, total = 0;
        for (let k = 0; k < 10; k++) {
            const t0 = Date.now();
            ed.insert(5, "x");
            wait(0);
            const dt = Date.now() - t0;
            best = Math.min(best, dt);
            total += dt;
        }
        console.log("[ perf ] keystroke beside a 50-row formula table: best " + best + " ms, mean " + (total / 10) + " ms");
        compare(findChild(nv, "md-live-table-cell"), cell, "the cells stay, not built again");
        verify(cell.text.length > 0);
        // An edit inside the table still reaches its cell.
        const at = ed.text.indexOf("| Unit |");
        ed.insert(at + 6, "Z");
        tryVerify(function () {
            return tc.liveCells(nv, []).some(function (c) { return c.text === "UnitZ"; });
        });
    }
    function liveCells(item, out) {
        for (let i = 0; i < item.children.length; i++) {
            const c = item.children[i];
            if (c.objectName === "md-live-table-cell") out.push(c);
            tc.liveCells(c, out);
        }
        return out;
    }

    // ── Rows and columns ─────────────────────────────────────────────

    function test_table_ops_from_the_view() {
        const nv = make(tc.table);
        const ed = editorOf(nv);
        ed.cursorPosition = ed.text.indexOf("| 1") + 2;
        verify(nv.caretInTable());
        verify(nv.tableOp("rowBelow"));
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n|  |  |\n\nAfter");
        verify(nv.tableOp("columnRight"));
        compare(ed.text, "Intro\n\n| A |  | B |\n| --- | --- | --- |\n| 1 |  | 2 |\n|  |  |  |\n\nAfter");
        verify(nv.tableOp("deleteColumn"));
        verify(nv.tableOp("deleteRow"));
        compare(ed.text, tc.table);
        ed.cursorPosition = 0;
        verify(!nv.caretInTable());
        verify(!nv.tableOp("rowBelow"), "outside a table nothing happens");
        compare(ed.text, tc.table);
    }

    function test_table_ops_from_the_palette_and_the_keys() {
        const nv = make(tc.table);
        const ed = editorOf(nv);
        ed.forceActiveFocus();
        ed.cursorPosition = ed.text.indexOf("| 1") + 2;
        nv.runNotesCommand("table.rowAbove");
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n|  |  |\n| 1 | 2 |\n\nAfter");
        verify(nv._kbd("notes.table.columnRight").length > 0, "the catalog binds a key");
        keyClick(Qt.Key_Right, Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier);
        compare(ed.text, "Intro\n\n| A |  | B |\n| --- | --- | --- |\n|  |  |  |\n| 1 |  | 2 |\n\nAfter");
        keyClick(Qt.Key_Backspace, Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier);
        keyClick(Qt.Key_Backspace, Qt.ControlModifier | Qt.AltModifier);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nAfter");
    }

    // A |…| example inside a code fence is code: no table ops, no table
    // rows in the "/" menu, "/" in an "empty cell" there is typed.
    function test_no_table_inside_a_code_fence() {
        const body = "```\n| A | B |\n| --- | --- |\n|  | 2 |\n```";
        const nv = make(body);
        const ed = editorOf(nv);
        ed.forceActiveFocus();
        ed.cursorPosition = ed.text.indexOf("|  |") + 2;
        verify(!nv.caretInTable());
        verify(!nv._caretInEmptyCell());
        verify(!nv.tableOp("rowBelow"));
        compare(ed.text, body);
    }

    function test_slash_in_an_empty_cell_offers_the_table() {
        const nv = make("| A | B |\n| --- | --- |\n|  | 2 |");
        const ed = editorOf(nv);
        ed.forceActiveFocus();
        ed.cursorPosition = ed.text.lastIndexOf("|  |") + 2;
        verify(nv._caretInEmptyCell());
        keyClick(Qt.Key_Slash);
        const menu = findChild(nv, "notes-slash-menu");
        tryVerify(function () { return menu.opened; });
        verify(menu.tableAt >= 0);
        // The table's rows come first while it is open in a table.
        compare(menu.currentIndex, 0);
        compare(menu.itemAt(0).objectName, "md-slash-table-rowAbove");
        const item = menu.itemAt(1);
        compare(item.objectName, "md-slash-table-rowBelow");
        item.triggered();
        tryCompare(ed, "text", "| A | B |\n| --- | --- |\n|  | 2 |\n|  |  |");
        menu.close();
        tryVerify(function () { return !menu.visible; });
        // Closed, the menu is its six rows again.
        tryCompare(menu, "count", 6);
        // In a cell with text "/" is typed.
        ed.cursorPosition = ed.text.indexOf("2") + 1;
        verify(!nv._caretInEmptyCell());
    }

    function test_text_menu_has_the_table_rows_only_in_a_table() {
        const area = createTemporaryQmlObject('import QtQuick.Controls.Basic; TextArea { text: "x" }', host);
        let asked = "";
        let pasted = 0;
        const menu = menuComp.createObject(host, { editor: area });
        verify(menu !== null);
        menu.inTable = true;
        menu.interceptPaste = true;
        menu.tableAction.connect(function (op) { asked = op; });
        menu.pasteRequested.connect(function () { pasted++; });
        menu.popup(area, 0, 0);
        tryVerify(function () { return menu.opened; });
        verify(findChild(menu, "text-edit-table-rowAbove").visible);
        findChild(menu, "text-edit-table-deleteColumn").triggered();
        compare(asked, "deleteColumn");
        findChild(menu, "text-edit-paste").triggered();
        compare(pasted, 1, "the host pastes");
        findChild(menu, "text-edit-paste-plain").triggered();
        compare(pasted, 1, "plain text never goes through the host");
        menu.close();
        tryVerify(function () { return !menu.visible; });
        menu.inTable = false;
        menu.popup(area, 0, 0);
        tryVerify(function () { return menu.opened; });
        verify(!findChild(menu, "text-edit-table-rowAbove").visible);
        menu.close();
        menu.destroy();
    }

    // ── The columns ──────────────────────────────────────────────────

    function test_columns_follow_the_settings_and_the_window() {
        const nv = make("Some text\n\n```\ncode\n```");
        const ed = editorOf(nv);
        const scroll = ed.parent.parent;  // the Flickable
        compare(nv.textWidthPref, 860);
        compare(nv.codeWidthPref, 1100);
        tryCompare(ed, "width", Math.min(scroll.width - 48, Theme.px(1100)));
        tc.setNotesPrefs({ textWidth: 500, codeWidth: 700 });
        tryCompare(ed, "width", Math.min(scroll.width - 48, Theme.px(700)));
        compare(nv._cols.text, Math.min(scroll.width - 48, Theme.px(500)));
        // Junk and out of range are held to the limits.
        tc.setNotesPrefs({ textWidth: 5, codeWidth: "lots" });
        compare(nv.textWidthPref, 320);
        compare(nv.codeWidthPref, 1100);
        tc.setNotesPrefs({ textWidth: 999999 });
        compare(nv.textWidthPref, 4000);
        // Never wider than the room there is.
        tryVerify(function () { return ed.width <= scroll.width - 48; });
        nv.width = 500;
        tryVerify(function () { return ed.width <= scroll.width - 48 && ed.width > 0; });
    }

    function test_the_edges_are_drawn_and_drag() {
        tc.setNotesPrefs({ textWidth: 600, codeWidth: 900 });
        const nv = make("Some text");
        const ed = editorOf(nv);
        const textEdge = findChild(nv, "notes-text-edge");
        const codeEdge = findChild(nv, "notes-code-edge");
        verify(textEdge !== null && codeEdge !== null);
        // Dogfood 2026-10-20: the text's line by default, none for code.
        verify(textEdge.visible);
        verify(!codeEdge.visible, "no line at the code column's edge by default");
        tc.setNotesPrefs({ codeEdge: true });
        tryVerify(function () { return codeEdge.visible; }, 1000, "turned on, a code column wider than the text has its own edge");
        tc.setNotesPrefs({ textEdge: false });
        tryVerify(function () { return !textEdge.visible; }, 1000, "turned off, the text's line goes");
        tc.setNotesPrefs({ textEdge: undefined, codeEdge: undefined });
        tryVerify(function () { return textEdge.visible && !codeEdge.visible; });
        const area = findChild(nv, "notes-text-edge-area");
        verify(area !== null);
        // The edge moves under the pointer: drag in the window's coordinates.
        const drag = function (dx) {
            const p = area.mapToItem(tc, area.width / 2, 10);
            mousePress(tc, p.x, p.y);
            mouseMove(tc, p.x + dx / 2, p.y);
            mouseMove(tc, p.x + dx, p.y);
            mouseRelease(tc, p.x + dx, p.y);
        };
        // 100px to the right: +100 on the setting (unscaled, a 10 step).
        drag(100);
        compare(tc.notesPref("textWidth"), Math.round((Theme.px(600) + 100) / Theme.scale / 10) * 10);
        compare(nv._dragText, -1);
        // Way past the left: held at the least width.
        drag(-3000);
        compare(tc.notesPref("textWidth"), 320);
        // Way past the right: held to the room on screen.
        drag(5000);
        verify(Theme.px(tc.notesPref("textWidth")) <= nv.width, "dragged past the window");
        // Double-click: back to the default.
        const at = area.mapToItem(tc, area.width / 2, 10);
        mouseDoubleClickSequence(tc, at.x, at.y);
        compare(tc.notesPref("textWidth"), undefined);
        compare(nv.textWidthPref, 860);
        verify(ed.width > 0);
    }

    function test_settings_rows_write_the_widths() {
        const sv = createTemporaryQmlObject('import TodoCpp; SettingsView { anchors.fill: parent }', host);
        tryVerify(function () { return sv._loadedOnce; });
        const row = findChild(sv, "settings-notes-text-width");
        verify(row !== null, "Settings → Notes has the text width");
        row.committed("700");
        compare(tc.notesPref("textWidth"), 700);
        row.committed("10");
        compare(tc.notesPref("textWidth"), 320, "held to the least");
        row.committed("");
        compare(tc.notesPref("textWidth"), undefined, "empty: the default");
        const code = findChild(sv, "settings-notes-code-width");
        code.committed("1500");
        compare(tc.notesPref("codeWidth"), 1500);
        // The notes view mode is kept beside them.
        compare(tc.notesPref("view"), "rich");
        // The lines at the edges: on and off by default, a choice kept.
        const textLine = findChild(sv, "settings-notes-text-edge");
        const codeLine = findChild(sv, "settings-notes-code-edge");
        verify(textLine !== null && codeLine !== null);
        verify(textLine.checked);
        verify(!codeLine.checked);
        textLine.toggled(false);
        compare(tc.notesPref("textEdge"), false);
        codeLine.toggled(true);
        compare(tc.notesPref("codeEdge"), true);
    }

    // ── Moving through a table (dogfood 2026-10-20) ──────────────────
    // Enter took the caret out of (under) the table, or tore its row in two
    // and hid the rows under the tear.

    function test_enter_and_tab_walk_the_table_and_add_rows() {
        const nv = make(tc.table);
        const ed = editorOf(nv);
        ed.forceActiveFocus();
        ed.cursorPosition = ed.text.indexOf("| 1") + 2;
        // Enter on the last row: a new row, the caret in its cell.
        keyClick(Qt.Key_Return);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n|  |  |\n\nAfter");
        keyClick(Qt.Key_X);
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Y);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n| x | y |\n\nAfter");
        // Shift+Tab back a cell, Enter down a row in the same column.
        keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
        compare(ed.text.charAt(ed.cursorPosition - 1), "x");
        ed.cursorPosition = ed.text.indexOf("| A") + 3;
        keyClick(Qt.Key_Return);
        compare(ed.text.charAt(ed.cursorPosition - 1), "1", "from the header to the first row");
        // Tab on the last cell: a new row.
        ed.cursorPosition = ed.text.indexOf("| y") + 3;
        keyClick(Qt.Key_Tab);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n| x | y |\n|  |  |\n\nAfter");
        // Enter on that empty last row: out of the table, the row gone.
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_Z);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n| x | y |\n\nz\n\nAfter");
        // Every row is drawn: the grid has the four lines of the table.
        const t = hl.decorations(ed.text).filter((d) => d.kind === "table");
        compare(t.length, 1);
        compare(t[0].rows.length, 4);
    }

    function test_shift_enter_leaves_the_table() {
        const nv = make(tc.table);
        const ed = editorOf(nv);
        ed.forceActiveFocus();
        ed.cursorPosition = ed.text.indexOf("| A") + 3;
        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        keyClick(Qt.Key_Q);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nq\n\nAfter");
        // Outside a table Enter is Enter again.
        keyClick(Qt.Key_Return);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nq\n\n\nAfter");
    }

    // Put on an empty line between two lines of text, the table is a block
    // of its own: a blank line on each side, nothing joined, nothing hidden.
    function test_a_table_put_between_lines_of_text_keeps_them_apart() {
        const nv = make("Para one\n\nPara two");
        const ed = editorOf(nv);
        ed.forceActiveFocus();
        ed.cursorPosition = ed.text.indexOf("\n\n") + 1;
        keyClick(Qt.Key_Slash);
        const menu = findChild(nv, "notes-slash-menu");
        tryVerify(function () { return menu.opened; });
        findChild(menu, "md-slash-table").triggered();
        tryCompare(ed, "text", "Para one\n\n|  |  |\n|---|---|\n|  |  |\n\nPara two");
        const t = hl.decorations(ed.text).filter((d) => d.kind === "table");
        compare(t.length, 1);
        compare(t[0].rows.length, 3);
        menu.close();
    }
}
