// Dogfood 2026-10-30: a table in a note is edited in its grid. The grid
// stays with the caret in it (one cell outlined, its source in it, the raw
// "|" never shown); long text wraps and the row grows; a table too wide for
// its column scrolls inside itself; "+" adds a column or a row; the arrows,
// Esc and "|" go by cells. The note keeps a plain pipe table.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "NotesTableGrid"
    when: windowShown
    visible: true
    width: 1200
    height: 800

    Item { id: host; anchors.fill: parent }

    property var made: []
    property string _settingsBefore: ""

    function initTestCase() {
        tc._settingsBefore = AppController.appSettingsJson;
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}"); } catch (e) { s = {}; }
        s.notes = Object.assign({}, s.notes || {}, { view: "rich" });
        AppController.appSettingsJson = JSON.stringify(s);
    }
    function cleanup() {
        for (let i = 0; i < tc.made.length; i++) AppController.deleteNote(tc.made[i]);
        tc.made = [];
        AppController.clearPendingUndo();
    }
    function cleanupTestCase() {
        AppController.appSettingsJson = tc._settingsBefore;
    }

    function make(body) {
        const id = AppController.newNote("Grid " + tc.made.length);
        tc.made.push(id);
        const nv = createTemporaryQmlObject('import TodoCpp; NotesView { anchors.fill: parent }', host);
        tryVerify(function () { return nv._loadedOnce; });
        const ed = findChild(nv, "notesEditor");
        ed.text = body;
        ed.forceActiveFocus();
        return nv;
    }
    function all(item, name, out) {
        for (let i = 0; i < item.children.length; i++) {
            const c = item.children[i];
            if (c.objectName === name) out.push(c);
            tc.all(c, name, out);
        }
        return out;
    }
    // The drawn cell showing `text` (not the one being edited).
    function cell(root, text) {
        let found = null;
        tryVerify(function () {
            found = tc.all(root, "md-live-table-cell", []).find(function (c) { return c.visible && c.text === text; }) || null;
            return found !== null;
        }, 3000, "a drawn cell reads " + text);
        return found;
    }
    function edited(root) {
        const e = tc.all(root, "md-live-table-edit", []);
        return e.length > 0 ? e[0] : null;
    }
    function clickCell(root, text) {
        const c = tc.cell(root, text);
        mouseClick(c, c.width - 1, c.height / 2);
        return c;
    }

    readonly property string table: "Intro\n\n| A | B |\n| --- | --- |\n| one | two |\n| x | y |\n\nAfter"

    function test_a_click_edits_the_cell_in_place() {
        const nv = make(tc.table);
        const ed = findChild(nv, "notesEditor");
        clickCell(nv, "two");
        tryVerify(function () { return tc.edited(nv) !== null; });
        const edit = tc.edited(nv);
        compare(edit.text, "two", "the cell, not the row's source");
        verify(edit.text.indexOf("|") < 0);
        compare(ed.cursorPosition, ed.text.indexOf("two") + 3, "the caret where it was clicked: the cell's end");
        verify(findChild(nv, "md-live-table-caret").visible);
        // The other cells stay drawn: the grid is there with the caret in it.
        verify(tc.cell(nv, "one").visible);
        verify(tc.cell(nv, "A").visible);
        // Typing goes into the cell, the file stays a pipe table.
        keyClick(Qt.Key_S);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| one | twos |\n| x | y |\n\nAfter");
        tryCompare(edit, "text", "twos");
        // A "|" inside a cell is written escaped and shown as typed.
        keyClick(Qt.Key_Left);
        keyClick("|");
        compare(ed.text.split("\n")[4], "| one | two\\|s |");
        tryCompare(tc.edited(nv), "text", "two|s");
        // Shift+Home selects to the cell's start, shown in the cell.
        keyClick(Qt.Key_Home, Qt.ShiftModifier);
        compare(ed.selectedText, "two\\|");
        tryCompare(tc.edited(nv), "selectedText", "two|");
        // Typing over a selection inside one cell replaces it.
        keyClick(Qt.Key_Q);
        compare(ed.text.split("\n")[4], "| one | qs |");
    }

    function test_the_arrows_and_esc_go_by_cells() {
        const nv = make(tc.table);
        const ed = findChild(nv, "notesEditor");
        clickCell(nv, "one");
        // Right at the cell's end: the next cell; Down: the cell under it.
        keyClick(Qt.Key_Right);
        compare(ed.cursorPosition, ed.text.indexOf("two"));
        keyClick(Qt.Key_Down);
        compare(ed.cursorPosition, ed.text.indexOf("| y") + 3);
        tryCompare(tc.edited(nv), "text", "y");
        // Backspace at a cell's start takes nothing (it took the pipe).
        keyClick(Qt.Key_Home);
        keyClick(Qt.Key_Backspace);
        compare(ed.text, tc.table);
        // Up twice: over the |---| line into the header.
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Up);
        tryCompare(tc.edited(nv), "text", "B");
        // Esc: out under the table, the grid with no cell outlined.
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Escape);
        compare(ed.cursorPosition, ed.text.indexOf("\n\nAfter") + 1);
        verify(ed.activeFocus, "Esc left the table, not the note");
        tryVerify(function () { return tc.edited(nv) === null; });
        // Down from the line above the table: its first cell.
        ed.cursorPosition = ed.text.indexOf("\n\n| A") + 1;
        keyClick(Qt.Key_Down);
        compare(ed.cursorPosition, ed.text.indexOf("| A") + 2);
        tryCompare(tc.edited(nv), "text", "A");
    }

    // Tab, Enter and the row keys still go as before (TableOps).
    function test_tab_enter_and_the_row_keys_in_the_grid() {
        const nv = make(tc.table);
        const ed = findChild(nv, "notesEditor");
        clickCell(nv, "y");
        keyClick(Qt.Key_Tab);
        compare(ed.text, "Intro\n\n| A | B |\n| --- | --- |\n| one | two |\n| x | y |\n|  |  |\n\nAfter");
        keyClick(Qt.Key_Z);
        tryCompare(tc.edited(nv), "text", "z");
        // Ctrl+Alt+Up (notes.table.rowAbove): a row above, the caret in it.
        keyClick(Qt.Key_Up, Qt.ControlModifier | Qt.AltModifier);
        compare(ed.text.split("\n").slice(4, 8).join("\n"), "| one | two |\n| x | y |\n|  |  |\n| z |  |");
    }

    function test_long_text_wraps_and_the_row_grows() {
        const long = "Fails on the nightly run, waiting for the logs from DevOps before the stand moves again; "
                     + "after the move the stand needs its addresses written into the config once more";
        const nv = make("| Step | Owner | Comment |\n| --- | --- | --- |\n| Calibrate | Smirnov | " + long + " |\n| Sim | me | short |\n\nAfter");
        const ed = findChild(nv, "notesEditor");
        const wrapped = tc.cell(nv, long);
        const short = tc.cell(nv, "short");
        // Wider than the editor's column: the long cell wraps.
        tryVerify(function () { return wrapped.contentHeight > 1.5 * short.contentHeight; }, 3000, "wrapped at " + ed.width + " cell " + wrapped.width + " text " + wrapped.implicitWidth + " grid " + JSON.stringify(findChild(nv, "md-live-table").lay));
        compare(wrapped.elide, Text.ElideNone);
        verify(wrapped.truncated === false);
        // Its row is as tall as its text, the text under the table moved down.
        tryVerify(function () { return wrapped.parent.height >= wrapped.contentHeight + 2 * Theme.spSm; }, 3000,
                  "the row holds the wrapped text: " + wrapped.parent.height + " / " + wrapped.contentHeight);
        verify(wrapped.parent.height > short.parent.height || wrapped.lineCount === 1);
        const after = ed.positionToRectangle(ed.text.indexOf("After"));
        const grid = findChild(nv, "md-live-table");
        verify(after.y >= grid.y + grid.height - 1, "the text under the table starts under its last row");
        // A short column keeps its own width.
        verify(tc.cell(nv, "me").parent.width < wrapped.parent.width);
    }

    // Acceptance 2026-10-30: the |---| line under the header is the grid's
    // line, not a blank band between the header and the first row - in the
    // text area too (the line under it starts right under the header).
    function test_no_band_under_the_header_data() {
        return [
            { tag: "spaced rule", body: tc.table, head: "A", first: "one" },
            { tag: "tight rule, wrapped cells, last in the note",
              body: "# Синк\n\nЦель: прогоны Sim.\n\n1. Собрать логи\n2. Сравнить\n\n| Шаг | Владелец | Комментарий |\n|---|---|---|\n"
                    + "| Калибровка стенда | Смирнов | Падает на ночном прогоне, ждём логи от DevOps, потом повторяем прогон целиком и смотрим ещё раз |\n"
                    + "| Прогон Sim | я | После переезда стенда нужно перепрописать IP в конфиге |\n",
              head: "Шаг", first: "Калибровка стенда" },
            { tag: "a wrapped row under a one-line header",
              body: "Intro\n\n| A | B | C |\n| --- | --- | --- |\n| one | two | Fails on the nightly run, waiting for the logs from "
                    + "DevOps before the stand moves again, and then once more after the move, and then again |\n| x | y | z |\n\nAfter",
              head: "A", first: "one" }
        ];
    }
    function test_no_band_under_the_header(row) {
        const nv = make(row.body);
        const ed = findChild(nv, "notesEditor");
        const head = tc.cell(nv, row.head);
        const first = tc.cell(nv, row.first);
        const grid = findChild(nv, "md-live-table");
        const gap = function () {
            const a = head.parent.mapToItem(grid, 0, 0), b = first.parent.mapToItem(grid, 0, 0);
            return b.y - (a.y + head.parent.height);
        };
        // Settled: the grid measured, the rows' lines as tall as its rows.
        tryVerify(function () { return (grid.lay.heights || []).length === grid.rows.length; }, 3000);
        wait(200);
        verify(Math.abs(gap()) <= 1, "a band of " + gap() + " px under the header: " + JSON.stringify(grid.lay));
        // The header's box is its text's height and the hairline, no more.
        const h = grid.lay.heights;
        compare(Math.round(head.parent.height), Math.round(h[0] + h[1]));
        // Every row starts where the one above ends, and the text under the
        // table under its last row.
        for (let i = 3; i < grid.rows.length; i++)
            compare(Math.round(grid.rowY(i)), Math.round(grid.rowY(i - 1) + grid.rowH(i - 1)), "row " + i);
        const after = ed.text.indexOf("After");
        if (after >= 0) verify(ed.positionToRectangle(after).y >= grid.y + grid.height - 1, "the text under the table overlaps it");
    }

    function test_a_wide_table_scrolls_inside_itself() {
        let head = "|", rule = "|", row = "|";
        for (let c = 0; c < 14; c++) {
            head += " Column number " + c + " |";
            rule += " --- |";
            row += " value " + c + " |";
        }
        const nv = make("Intro\n\n" + head + "\n" + rule + "\n" + row + "\n\nAfter");
        const ed = findChild(nv, "notesEditor");
        const grid = findChild(nv, "md-live-table");
        tryVerify(function () { return grid.maxScroll > 0; }, 3000, "wider than its column");
        verify(grid.width <= ed.width, "the grid's view stays in the column");
        verify(findChild(nv, "md-live-table-scroll").visible);
        compare(grid.scrollX, 0);
        // The caret in the last cell scrolls it into view.
        clickCell(nv, "value 0");
        tryCompare(tc.edited(nv), "text", "value 0");
        for (let k = 0; k < 13; k++) keyClick(Qt.Key_Tab);
        tryCompare(tc.edited(nv), "text", "value 13");
        tryVerify(function () { return grid.scrollX > 0; });
        compare(ed.text.split("\n").length, 7, "Tab walked the cells, no row added");
    }

    function test_plus_adds_a_column_and_a_row() {
        const nv = make(tc.table);
        const ed = findChild(nv, "notesEditor");
        clickCell(nv, "one");
        const addColumn = findChild(nv, "md-live-table-add-column");
        const addRow = findChild(nv, "md-live-table-add-row");
        tryVerify(function () { return addColumn.visible && addRow.visible; }, 3000, "shown with the caret in the table");
        mouseClick(addColumn);
        compare(ed.text, "Intro\n\n| A | B |  |\n| --- | --- | --- |\n| one | two |  |\n| x | y |  |\n\nAfter");
        compare(ed.cursorPosition, ed.text.indexOf("| B | ") + 6, "the caret in the new column's header");
        verify(ed.activeFocus);
        mouseClick(addRow);
        compare(ed.text, "Intro\n\n| A | B |  |\n| --- | --- | --- |\n| one | two |  |\n| x | y |  |\n|  |  |  |\n\nAfter");
        compare(ed.cursorPosition, ed.text.indexOf("|  |  |  |") + 2, "the caret in the new row's first cell");
        // Undo takes each back.
        ed.undo();
        compare(ed.text.split("\n").length, 8);
    }

    // The task document's rich text (MdBlockEditor, MdLivePreview) edits
    // its tables in the grid too.
    function test_the_task_document_edits_in_the_grid() {
        const be = createTemporaryQmlObject('import TodoCpp; MdBlockEditor { width: 900; height: 500; rich: true; wholeDocument: true; noteType: true }', host);
        be.text = "Plan\n\n| Step | Who |\n| --- | --- |\n| build | me |\n";
        wait(50);
        const field = be.editor;
        tryVerify(function () { return field.visible; });
        clickCell(be, "build");
        verify(field.cursorPosition === field.text.indexOf("build") + 5, "clicked: " + field.cursorPosition);
        tryVerify(function () { return field.activeFocus; });
        tryCompare(tc.edited(be), "text", "build");
        keyClick(Qt.Key_S);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_X);
        tryCompare(field, "text", "Plan\n\n| Step | Who |\n| --- | --- |\n| builds | xme |\n");
    }
}
