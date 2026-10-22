// Design audit DES-6: the column header held four slots for the hover icons
// (move left / right, delete, fold) even while they were hidden, which left
// the name about 100px — "К выполнению" read "К вып…" on a 1600px window.
// The icons now lie over the end of the name, so at rest the name has the
// header to itself; and the board's scrollbar stays in view while columns
// run off to the right.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "BoardHeader"
    when: windowShown
    visible: true
    width: 1200
    height: 700

    Item { id: host; anchors.fill: parent }

    function all(root, name, acc) {
        if (!root) return acc;
        if (root.objectName === name) acc.push(root);
        const kids = root.children || [];
        for (let i = 0; i < kids.length; i++) all(kids[i], name, acc);
        return acc;
    }

    function test_column_names_are_not_cut_at_rest() {
        const board = createTemporaryQmlObject('import TodoCpp; KanbanBoard { anchors.fill: parent }', host);
        verify(board !== null);
        wait(50);
        const names = all(board, "column-name", []);
        verify(names.length > 0);
        const cut = [];
        for (let i = 0; i < names.length; i++) {
            const n = names[i];
            if (n.visible && n.truncated) cut.push(n.text);
        }
        compare(cut.length, 0, "cut column names: " + cut.join(", "));
    }

    function test_hover_icons_take_no_room_from_the_name() {
        const board = createTemporaryQmlObject('import TodoCpp; KanbanBoard { anchors.fill: parent }', host);
        wait(50);
        const icons = all(board, "column-hover-icons", []);
        verify(icons.length > 0);
        compare(icons[0].opacity, 0, "hover icons show at rest");
    }

    // Every column open: an empty one folds by itself (APP-314), and these
    // cases are about the open column's header and the board's width.
    function openEmptyColumns(board) {
        const open = {};
        for (const s of AppController.statuses) open[s.id] = true;
        board.emptyOpen = open;
        wait(0);
        return board;
    }

    function test_the_scrollbar_shows_while_columns_overflow() {
        // Too narrow for even one column and the rest as strips (a narrow
        // board folds its columns before it scrolls, BoardDrag sheet).
        const board = openEmptyColumns(createTemporaryQmlObject('import TodoCpp; KanbanBoard { width: 300; height: 500 }', host));
        wait(50);
        const flick = all(board, "board-hscroll", [])[0];
        verify(flick.contentWidth > flick.width, "setup: 300px must overflow");
        const bar = flick.ScrollBar.horizontal;
        verify(bar !== null);
        compare(bar.policy, ScrollBar.AlwaysOn);
    }
}
