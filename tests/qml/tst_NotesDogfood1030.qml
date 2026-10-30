// Notes, dogfood 2026-10-30: a numbered list counts again after an edit, the
// renumbering and the edit undo together (MdList), Alt ↑ / Alt ↓ to the
// neighbouring notes (NotesNav), Ctrl+F in a note's text (Find), and a
// picture on the clipboard pasted as a picture (Paste).
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "NotesDogfood1030"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Item { id: host; anchors.fill: parent }

    function make() {
        const nv = Qt.createQmlObject('import TodoCpp; NotesView { anchors.fill: parent }', host, "tst_NotesDogfood1030.make");
        verify(nv !== null);
        tryVerify(function () { return nv._loadedOnce; });
        return nv;
    }

    function editorOf(nv) {
        const ed = findChild(nv, "notesEditor");
        verify(ed !== null, "notesEditor not found");
        return ed;
    }

    function init() {
        AppController.notesState = "";
    }

    // ── MdList ───────────────────────────────────────────────────

    function test_deleting_an_item_renumbers_and_undoes_in_one_step() {
        const nv = make();
        nv.viewMode = "edit";
        const ed = editorOf(nv);
        const before = "4. Logs\n5. Restart\n6. Compare\n7. Write";
        ed.text = before;
        ed.forceActiveFocus();
        const from = before.indexOf("5.");
        ed.select(from, before.indexOf("6."));
        keyClick(Qt.Key_Backspace);
        tryCompare(ed, "text", "4. Logs\n5. Compare\n6. Write");
        ed.undo();
        compare(ed.text, before);
        nv.destroy();
    }

    function test_enter_in_the_middle_renumbers_and_undoes_in_one_step() {
        const nv = make();
        nv.viewMode = "edit";
        const ed = editorOf(nv);
        const before = "1. a\n2. b\n3. c";
        ed.text = before;
        ed.forceActiveFocus();
        ed.cursorPosition = before.indexOf("\n");
        keyClick(Qt.Key_Return);
        keyClick(Qt.Key_X);
        tryCompare(ed, "text", "1. a\n2. x\n3. b\n4. c");
        ed.undo();
        // The typed "x" goes first, then the new item with its renumbering.
        if (ed.text !== before) ed.undo();
        compare(ed.text, before);
        nv.destroy();
    }

    function test_emptying_an_item_and_joining_lines_keeps_the_count() {
        const nv = make();
        nv.viewMode = "rich";
        const ed = editorOf(nv);
        const before = "1. a\n2. x\n3. b";
        ed.text = before;
        ed.forceActiveFocus();
        ed.select(before.indexOf("2."), before.indexOf("\n3."));
        keyClick(Qt.Key_Backspace);
        tryCompare(ed, "text", "1. a\n\n2. b");
        keyClick(Qt.Key_Backspace);
        tryCompare(ed, "text", "1. a\n2. b");
        nv.destroy();
    }

    function test_undo_does_not_renumber_again() {
        const nv = make();
        nv.viewMode = "edit";
        const ed = editorOf(nv);
        const before = "1. a\n2. b\n3. c";
        ed.text = before;
        ed.forceActiveFocus();
        ed.select(before.indexOf("2."), before.indexOf("3."));
        keyClick(Qt.Key_Delete);
        tryCompare(ed, "text", "1. a\n2. c");
        ed.undo();
        compare(ed.text, before);
        wait(50);
        compare(ed.text, before);
        ed.redo();
        compare(ed.text, "1. a\n2. c");
        nv.destroy();
    }

    // ── Find ─────────────────────────────────────────────────────

    function findBarOf(nv) {
        const bar = findChild(nv, "note-find-bar");
        verify(bar !== null, "note-find-bar not found");
        return bar;
    }

    function test_ctrl_f_in_the_text_finds_in_the_note() {
        const nv = make();
        nv.viewMode = "rich";
        const ed = editorOf(nv);
        const bar = findBarOf(nv);
        const text = "Sim one\nsim two sim\nSIM three";
        ed.text = text;
        ed.forceActiveFocus();
        // The selection is the query, and its match is the current one.
        ed.select(8, 11);
        nv.focusSearch();
        verify(bar.visible);
        const field = findChild(bar, "note-find-field");
        verify(field.activeFocus, "the query field has no keyboard");
        compare(field.text, "sim");
        compare(bar.matches, [0, 8, 16, 20]);
        compare(bar.current, 1);
        tryVerify(function () { return findChild(nv, "note-find-current") !== null; }, 1000, "the current match is not tinted");
        keyClick(Qt.Key_Return);
        compare(bar.current, 2);
        keyClick(Qt.Key_Down);
        compare(bar.current, 3);
        keyClick(Qt.Key_Down);
        compare(bar.current, 0, "the last match goes round to the first");
        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        compare(bar.current, 3);
        keyClick(Qt.Key_Up);
        compare(bar.current, 2);
        // Аа: the case counts.
        bar.caseSensitive = true;
        compare(bar.matches, [8, 16]);
        bar.caseSensitive = false;
        // Typing narrows.
        keyClick(Qt.Key_End);
        keyClick(" ");
        compare(findChild(bar, "note-find-field").text, "sim ");
        compare(bar.matches, [0, 8, 20]);
        keyClick(Qt.Key_Backspace);
        bar.step(-1);
        const at = bar.matches[bar.current];
        // Esc: closed, the text has the keyboard and the match selected.
        keyClick(Qt.Key_Escape);
        verify(!bar.visible);
        verify(ed.activeFocus);
        compare(ed.selectionStart, at);
        compare(ed.selectionEnd, at + 3);
        nv.destroy();
    }

    // Acceptance 2026-10-30: a match in a table's cell is tinted in the
    // grid (which hides the text the bar tints), the current one strongly,
    // and stepping to it brings its row into view.
    function test_find_tints_matches_in_a_table_and_scrolls_to_them() {
        const nv = make();
        nv.viewMode = "rich";
        const ed = editorOf(nv);
        const bar = findBarOf(nv);
        let lines = ["Цель: прогоны Sim на стенде.", ""];
        for (let i = 0; i < 60; i++) lines.push("Line " + i + " of the note.", "");
        lines.push("| Шаг | Владелец |", "|---|---|", "| Калибровка | Смирнов |", "| Прогон Sim | я |", "", "After");
        ed.text = lines.join("\n");
        ed.forceActiveFocus();
        ed.cursorPosition = 0;
        nv.focusSearch();
        verify(bar.visible);
        keyClick("s");
        keyClick("i");
        keyClick("m");
        compare(bar.matches.length, 2);
        compare(bar.current, 0);
        // The cell with the match: tinted quietly while the current one is
        // above the table.
        let cell = null;
        tryVerify(function () {
            const all = [];
            const walk = function (it) {
                for (let i = 0; i < it.children.length; i++) {
                    const c = it.children[i];
                    if (c.objectName === "md-live-table-cell" && String(c.text).indexOf("Прогон") >= 0) all.push(c);
                    walk(c);
                }
            };
            walk(nv);
            cell = all.length > 0 ? all[0] : null;
            return cell !== null && cell.foundCount === 1;
        }, 3000, "the table's match is not found in its cell");
        verify(!cell.foundCurrent);
        verify(String(cell.text).indexOf("background-color") > 0, cell.text);
        const quiet = String(Qt.tint(cell.parent.color, Theme.highlightBg));
        const strong = String(Qt.tint(cell.parent.color, Theme.withAlpha(Theme.warning, 0.55)));
        verify(cell.text.indexOf(quiet) > 0, cell.text);
        verify(cell.text.indexOf(">Sim</span>") > 0, cell.text);
        // To the table's match: the strong tint, its row in view.
        keyClick(Qt.Key_Return);
        compare(bar.current, 1);
        tryVerify(function () { return cell.foundCurrent; });
        verify(cell.text.indexOf(strong) > 0, cell.text);
        const flick = bar.flickable;
        tryVerify(function () {
            const p = cell.mapToItem(flick, 0, 0);
            return p.y >= 0 && p.y + cell.height <= flick.height;
        }, 2000, "the table's match is not in view");
        // Closed: the cells are as they were.
        keyClick(Qt.Key_Escape);
        tryVerify(function () { return cell.foundCount === 0 && String(cell.text).indexOf("background-color") < 0; });
        nv.destroy();
    }

    function test_ctrl_f_from_the_list_is_still_the_filter() {
        const nv = make();
        const bar = findBarOf(nv);
        const ed = editorOf(nv);
        ed.text = "Sim one";
        const filter = findChild(nv, "note-filter");
        verify(filter !== null);
        filter.forceActiveFocus();
        nv.focusSearch();
        verify(!bar.visible);
        verify(filter.activeFocus);
        nv.destroy();
    }

    function test_no_matches_says_so() {
        const nv = make();
        nv.viewMode = "edit";
        const ed = editorOf(nv);
        const bar = findBarOf(nv);
        ed.text = "alpha beta";
        ed.forceActiveFocus();
        nv.focusSearch();
        keyClick(Qt.Key_Z);
        keyClick(Qt.Key_Q);
        compare(bar.matches.length, 0);
        compare(findChild(bar, "note-find-count").text, I18n.t("notes.find.none"));
        keyClick(Qt.Key_Escape);
        verify(ed.activeFocus);
        nv.destroy();
    }

    // ── Paste ────────────────────────────────────────────────────

    // A stored picture's link goes in over the selection as one edit, and
    // Ctrl+Shift+V right after puts the clipboard's text in its place.
    function test_a_pasted_picture_is_one_undo_step_and_ctrl_shift_v_swaps_it_for_text() {
        const nv = make();
        nv.viewMode = "rich";
        const ed = editorOf(nv);
        const before = "Scheme after the move: OLD";
        ed.text = before;
        ed.forceActiveFocus();
        ed.select(before.indexOf("OLD"), before.length);
        const ref = "![pasted.png](attachments/0123456789abcdef0123456789abcdef.png)";
        compare(nv.insertAttachmentRefs([{ ref: ref }]), 1);
        compare(ed.text, "Scheme after the move: \n\n" + ref + "\n");
        ed.undo();
        compare(ed.text, before, "the picture took two Ctrl+Z");

        ed.select(before.indexOf("OLD"), before.length);
        nv._pastedPictureOver = { from: ed.selectionStart, to: ed.selectionEnd };
        nv.insertAttachmentRefs([{ ref: ref }]);
        nv._pastedPictureText = ed.text;
        AppController.copyToClipboard("https://files.example/s/9f2c1/image.png");
        keyClick(Qt.Key_V, Qt.ControlModifier | Qt.ShiftModifier);
        compare(ed.text, "Scheme after the move: https://files.example/s/9f2c1/image.png");
        compare(nv._pastedPictureText, "");
        nv.destroy();
    }

    // The picture's box keeps the picture's aspect, no wider than the text
    // column, and the line above it ends above it.
    function test_a_picture_box_keeps_the_pictures_aspect() {
        const path = String(AppController.dataDir) + "/tst-dogfood-1030-figure.png";
        const src = "file://" + (path.startsWith("/") ? "" : "/") + path;
        const swatch = Qt.createQmlObject('import QtQuick; Rectangle { width: 1280; height: 610; color: "red" }', host, "swatch");
        let saved = false;
        swatch.grabToImage(function (result) { saved = result.saveToFile(path); });
        tryVerify(function () { return saved; }, 3000, "could not write the test picture");
        swatch.destroy();

        const nv = make();
        nv.viewMode = "rich";
        const ed = editorOf(nv);
        ed.text = "Scheme after the move:\n\n![scheme](" + src + ")\n\nThe next paragraph.";
        let fig = null;
        tryVerify(function () {
            fig = findChild(nv, "md-live-image");
            return fig !== null && fig.fit !== undefined;
        }, 3000, "the picture never reported its size");
        // 1280×610 into a narrower column: as wide as the column, its aspect kept.
        const preview = findChild(nv, "md-live-preview");
        const column = ed.width - preview.proseRight;
        verify(fig.width <= Math.ceil(column), "wider than the column: " + fig.width + " > " + column);
        fuzzyCompare(fig.width / fig.height, 1280 / 610, 0.02);
        // The room above the image line is the picture's own height.
        const imageLine = ed.text.indexOf("![scheme]");
        tryVerify(function () {
            const above = ed.positionToRectangle(ed.text.indexOf("move:"));
            return above.y + above.height <= fig.y + 1;
        }, 3000, "the line above runs into the picture");
        const lineTop = ed.positionToRectangle(imageLine).y;
        verify(fig.y + fig.height <= lineTop + 1, "the picture runs into its caption line");
        const next = ed.positionToRectangle(ed.text.indexOf("The next"));
        verify(next.y >= fig.y + fig.height, "the next paragraph runs into the picture");
        nv.destroy();
    }
}
