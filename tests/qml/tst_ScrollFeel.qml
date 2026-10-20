// The wheel and the middle button (dogfood 2026-10-20: "scrolling is too
// fast — make it like other programs; let the middle button scroll").
//
// ScrollFeel, installed on the application by main() and by the test
// runner, moves every view one notch = the system's lines per notch of one
// text line; the innermost view that can move takes the wheel and keeps it
// to the end of a gesture; a middle click starts browser-like autoscroll
// that any key, Esc or click stops.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "ScrollFeel"
    when: windowShown
    visible: true
    width: 1000
    height: 700

    Item { id: host; anchors.fill: parent }

    property var seededTasks: []
    property var seededNotes: []
    readonly property string probe: "scrollfeelprobe"

    function notch() { return Qt.styleHints.wheelScrollLines * Theme.scrollLine; }

    function init() {
        ScrollFeel.stopAutoscroll();
        // Each case starts a gesture of its own.
        wait(450);
    }
    function cleanup() {
        ScrollFeel.stopAutoscroll();
        for (let i = 0; i < tc.seededTasks.length; i++) AppController.deleteTask(tc.seededTasks[i]);
        for (let i = 0; i < tc.seededNotes.length; i++) AppController.deleteNote(tc.seededNotes[i]);
        tc.seededTasks = [];
        tc.seededNotes = [];
        AppController.clearPendingUndo();
    }

    Component {
        id: listComp
        ListView {
            width: 300
            height: 400
            model: 200
            delegate: Rectangle {
                required property int index
                width: ListView.view.width
                height: 24
                color: index % 2 ? Theme.panel : Theme.bg
            }
        }
    }

    // A list inside a page that scrolls too.
    Component {
        id: nestedComp
        Flickable {
            id: page
            property alias inner: innerList
            width: 400
            height: 400
            contentHeight: 1200
            ListView {
                id: innerList
                y: 50
                width: 300
                height: 200
                model: 30
                delegate: Rectangle {
                    required property int index
                    width: ListView.view.width
                    height: 24
                    color: index % 2 ? Theme.panel : Theme.bg
                }
            }
        }
    }

    // ── The wheel ─────────────────────────────────────────────────────

    function test_a_notch_is_three_lines_not_a_flick() {
        verify(Theme.scrollLine > 0);
        compare(ScrollFeel.lineHeight, Theme.scrollLine, "Theme did not hand its line to ScrollFeel");
        compare(ScrollFeel.notchPixels(), tc.notch());
        verify(tc.notch() < 360, "a notch is still the old triple step");
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        mouseWheel(list, 100, 100, 0, -120);
        tryCompare(list, "contentY", tc.notch(), 1000);
        wait(300);
        compare(list.contentY, tc.notch(), "the view kept going after the notch: a flick, not a step");
        // And back up.
        mouseWheel(list, 100, 100, 0, 120);
        tryCompare(list, "contentY", 0, 1000);
    }

    function test_notches_in_a_row_add_up() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        for (let i = 0; i < 3; i++) mouseWheel(list, 100, 100, 0, -120);
        tryCompare(list, "contentY", 3 * tc.notch(), 1000);
    }

    function test_a_fine_wheel_moves_in_proportion_at_once() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        // An eighth of a notch: a high-resolution wheel or a touchpad.
        mouseWheel(list, 100, 100, 0, -15);
        compare(list.contentY, tc.notch() / 8, "a fine step must follow 1:1, without a glide");
    }

    function test_the_wheel_stops_at_the_end() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        for (let i = 0; i < 200; i++) mouseWheel(list, 100, 100, 0, -1200);
        const end = list.contentHeight - list.height;
        tryCompare(list, "contentY", end, 2000);
    }

    function test_inner_view_running_out_keeps_the_gesture() {
        const page = createTemporaryObject(nestedComp, host);
        waitForRendering(page);
        const inner = page.inner;
        verify(inner.contentHeight > inner.height);
        // Far more than the inner list has, in one quick gesture.
        for (let i = 0; i < 30; i++) mouseWheel(inner, 100, 100, 0, -240);
        tryVerify(() => inner.contentY >= inner.contentHeight - inner.height - 1, 2000);
        wait(200);
        compare(page.contentY, 0, "the page slid when the inner list ran out mid-gesture");
        // A new gesture over a list already at its end goes to the page.
        wait(500);
        mouseWheel(inner, 100, 100, 0, -120);
        tryCompare(page, "contentY", tc.notch(), 1000);
    }

    function test_a_gesture_begun_on_the_page_stays_on_it() {
        const page = createTemporaryObject(nestedComp, host);
        waitForRendering(page);
        // Over the page, beside the inner list.
        mouseWheel(page, 350, 380, 0, -120);
        tryCompare(page, "contentY", tc.notch(), 1000);
        // The inner list slid under the pointer; the gesture stays with the page.
        const p = page.inner.mapFromItem(page, 100, 100);
        mouseWheel(page.inner, p.x, Math.max(10, p.y), 0, -120);
        tryCompare(page, "contentY", 2 * tc.notch(), 1000);
        compare(page.inner.contentY, 0);
    }

    function test_board_column_moves_a_notch() {
        const statuses = AppController.statuses;
        for (let i = 0; i < 40; i++) {
            const d = AppController.newTaskDraft(statuses[0].id);
            d._isNew = true;
            d.id = "SCRL-" + i;
            d.title = tc.probe + " card " + i;
            AppController.saveTask(d);
            tc.seededTasks.push(d.id);
        }
        const b = createTemporaryQmlObject('import TodoCpp; KanbanBoard { width: 600; height: 700 }', host);
        b.searchText = tc.probe;
        waitForRendering(b);
        const lists = [];
        (function walk(item) {
            for (let i = 0; i < item.children.length; i++) {
                const c = item.children[i];
                if (c.objectName === "column-list") lists.push(c);
                walk(c);
            }
        })(b);
        lists.sort((a, c) => a.mapToItem(b, 0, 0).x - c.mapToItem(b, 0, 0).x);
        const list = lists[0];
        tryVerify(() => list.contentHeight > list.height + tc.notch(), 2000, "the column must overflow");
        const start = list.contentY;
        mouseWheel(list, list.width / 2, list.height / 2, 0, -120);
        tryCompare(list, "contentY", start + tc.notch(), 1000);
        const hs = findChild(b, "board-hscroll");
        compare(hs.contentX, 0, "the board slid sideways");
        // Shift+wheel is for the board.
        wait(500);
        if (hs.contentWidth > hs.width) {
            mouseWheel(list, list.width / 2, list.height / 2, 0, -120, Qt.NoButton, Qt.ShiftModifier);
            tryVerify(() => hs.contentX > 0, 1000, "Shift+wheel did not scroll the board");
        }
        b.destroy();
    }

    function test_notes_editor_moves_a_notch() {
        let body = "";
        for (let i = 1; i <= 150; i++) body += "Line " + i + " of a long note\n";
        const id = AppController.newNote("scroll probe");
        AppController.setNoteBody(id, body);
        tc.seededNotes.push(id);
        AppController.activeNoteId = id;
        const nv = createTemporaryQmlObject('import TodoCpp; NotesView { width: 900; height: 600 }', host);
        tryVerify(() => nv._loadedOnce, 2000);
        nv.viewMode = "edit";
        const ed = findChild(nv, "notesEditor");
        tryVerify(() => ed.visible && ed.text.length > 0, 2000);
        let flick = ed.parent;
        while (flick && flick.contentY === undefined) flick = flick.parent;
        verify(flick !== null);
        tryVerify(() => flick.contentHeight > flick.height + tc.notch(), 2000, "the note must overflow");
        flick.contentY = 0;
        mouseWheel(ed, 60, 60, 0, -120);
        tryCompare(flick, "contentY", tc.notch(), 1000);
        nv.destroy();
    }

    // ── Middle-click autoscroll ───────────────────────────────────────

    function test_middle_click_autoscrolls_until_a_key() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        mouseClick(list, 150, 150, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling, "a middle click over a list did not start autoscroll");
        // Inside the dead zone nothing moves.
        mouseMove(list, 150, 156);
        wait(150);
        compare(list.contentY, 0);
        // Further down, it scrolls down by itself, frame after frame.
        mouseMove(list, 150, 260);
        tryVerify(() => list.contentY > 20, 2000, "autoscroll did not move the list");
        const a = list.contentY;
        tryVerify(() => list.contentY > a + 10, 2000, "autoscroll stopped on its own");
        // Esc stops it, and nothing else sees the key.
        keyClick(Qt.Key_Escape);
        verify(!ScrollFeel.autoscrolling, "Esc did not stop autoscroll");
        const b = list.contentY;
        wait(200);
        compare(list.contentY, b, "the list kept moving after Esc");
    }

    function test_any_key_stops_and_is_swallowed() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        const sink = createTemporaryQmlObject('import QtQuick; Item { property int got: 0; focus: true; Keys.onPressed: got++ }', host);
        sink.forceActiveFocus();
        mouseClick(list, 150, 150, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling);
        keyClick(Qt.Key_J);
        verify(!ScrollFeel.autoscrolling, "a key did not stop autoscroll");
        compare(sink.got, 0, "the key that stopped autoscroll also acted");
        keyClick(Qt.Key_J);
        compare(sink.got, 1, "keys after the stop must work again");
    }

    function test_second_middle_click_or_left_click_stops() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        mouseClick(list, 150, 150, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling);
        mouseClick(list, 150, 150, Qt.MiddleButton);
        verify(!ScrollFeel.autoscrolling, "a second middle click did not stop autoscroll");
        wait(450);
        mouseClick(list, 150, 150, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling);
        mouseClick(list, 150, 150, Qt.LeftButton);
        verify(!ScrollFeel.autoscrolling, "a left click did not stop autoscroll");
    }

    function test_hold_drag_release_stops_on_release() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        mousePress(list, 150, 150, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling);
        mouseMove(list, 150, 260, -1, Qt.MiddleButton);
        tryVerify(() => list.contentY > 10, 2000);
        mouseRelease(list, 150, 260, Qt.MiddleButton);
        verify(!ScrollFeel.autoscrolling, "letting go after a drag did not stop autoscroll");
    }

    function test_marker_shows_where_it_started() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        mouseClick(list, 150, 150, Qt.MiddleButton);
        const root = list.Window.contentItem;
        let marker = null;
        for (let i = 0; i < root.children.length; i++)
            if (root.children[i].objectName === "autoscroll-marker") marker = root.children[i];
        verify(marker !== null, "no origin marker");
        verify(marker.canY && !marker.canX);
        const centre = marker.mapToItem(list, marker.width / 2, marker.height / 2);
        fuzzyCompare(centre.x, 150, 1);
        fuzzyCompare(centre.y, 150, 1);
        keyClick(Qt.Key_Escape);
        wait(50);
        let left = false;
        for (let i = 0; i < root.children.length; i++)
            if (root.children[i].objectName === "autoscroll-marker") left = true;
        verify(!left, "the marker stayed after the stop");
    }

    function test_middle_button_with_its_own_meaning_keeps_it() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        const own = createTemporaryQmlObject('import QtQuick; MouseArea { width: 100; height: 100; acceptedButtons: Qt.MiddleButton; property int got: 0; onClicked: got++ }', list.contentItem);
        mouseClick(own, 50, 50, Qt.MiddleButton);
        verify(!ScrollFeel.autoscrolling, "autoscroll took a middle click that had its own use");
        compare(own.got, 1);
    }

    function test_nothing_to_scroll_no_autoscroll() {
        const shortList = createTemporaryQmlObject('import QtQuick; ListView { width: 200; height: 400; model: 3; delegate: Item { width: 200; height: 20 } }', host);
        waitForRendering(shortList);
        mouseClick(shortList, 100, 100, Qt.MiddleButton);
        verify(!ScrollFeel.autoscrolling);
    }

    // ── Middle over things that only take clicks (dogfood 2026-10-20) ──
    // A real middle click on a board card did nothing: a HoverHandler or a
    // TapHandler widens its item to every button, and anything that "took"
    // the middle button made autoscroll stand aside. Only an item that uses
    // the middle button keeps it now.

    function markerShown(item) {
        const root = item.Window.contentItem;
        for (let i = 0; i < root.children.length; i++)
            if (root.children[i].objectName === "autoscroll-marker" && root.children[i].visible) return true;
        return false;
    }

    function boardWithCards() {
        const statuses = AppController.statuses;
        for (let i = 0; i < 40; i++) {
            const d = AppController.newTaskDraft(statuses[0].id);
            d._isNew = true;
            d.id = "SCRM-" + i;
            d.title = tc.probe + " middle " + i;
            AppController.saveTask(d);
            tc.seededTasks.push(d.id);
        }
        const b = createTemporaryQmlObject('import TodoCpp; KanbanBoard { width: 600; height: 700 }', host);
        b.searchText = tc.probe;
        waitForRendering(b);
        return b;
    }

    function findAll(item, pred, out) {
        for (let i = 0; i < item.children.length; i++) {
            const c = item.children[i];
            if (pred(c)) out.push(c);
            tc.findAll(c, pred, out);
        }
        return out;
    }

    function test_middle_over_a_board_card_autoscrolls_its_column() {
        const b = tc.boardWithCards();
        const lists = tc.findAll(b, (c) => c.objectName === "column-list", []);
        lists.sort((a, c) => a.mapToItem(b, 0, 0).x - c.mapToItem(b, 0, 0).x);
        const list = lists[0];
        tryVerify(() => list.contentHeight > list.height * 2, 2000, "the column must overflow");
        let card = null;
        tryVerify(() => {
            const cards = tc.findAll(list.contentItem, (c) => c.task !== undefined && c.task && String(c.task.id).indexOf("SCRM-") === 0 && c.visible && c.height > 0, []);
            card = cards.length > 0 ? cards[0] : null;
            return card !== null;
        }, 2000, "no card in the column");
        mousePress(card, card.width / 2, card.height / 2, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling, "a middle press on a board card did not start autoscroll");
        verify(tc.markerShown(card), "no marker over the card");
        mouseRelease(card, card.width / 2, card.height / 2, Qt.MiddleButton);
        const start = list.contentY;
        mouseMove(card, card.width / 2, card.height / 2 + 150);
        tryVerify(() => list.contentY > start + 10, 2000, "the column did not move");
        keyClick(Qt.Key_Escape);
        verify(!ScrollFeel.autoscrolling);
        b.destroy();
    }

    function test_middle_over_a_note_line_autoscrolls_the_note() {
        let body = "";
        for (let i = 1; i <= 150; i++) body += "Line " + i + " of a long note\n";
        const id = AppController.newNote("middle probe");
        AppController.setNoteBody(id, body);
        tc.seededNotes.push(id);
        AppController.activeNoteId = id;
        const nv = createTemporaryQmlObject('import TodoCpp; NotesView { width: 900; height: 600 }', host);
        tryVerify(() => nv._loadedOnce, 2000);
        nv.viewMode = "edit";
        const ed = findChild(nv, "notesEditor");
        tryVerify(() => ed.visible && ed.text.length > 0, 2000);
        mousePress(ed, 60, 60, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling, "a middle press on a note's text did not start autoscroll");
        verify(tc.markerShown(ed));
        mouseRelease(ed, 60, 60, Qt.MiddleButton);
        keyClick(Qt.Key_Escape);
        nv.destroy();
    }

    // A row that takes left and right clicks, with a hover and a tap handler
    // on it, and a pane that eats every button: none of them uses the middle.
    Component {
        id: rowsComp
        ListView {
            width: 300
            height: 400
            model: 200
            delegate: Rectangle {
                required property int index
                width: ListView.view.width
                height: 28
                color: index % 2 ? Theme.panel : Theme.bg
                HoverHandler {}
                TapHandler {}
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
            }
        }
    }

    function test_middle_over_a_list_row_autoscrolls() {
        const list = createTemporaryObject(rowsComp, host);
        waitForRendering(list);
        mousePress(list, 150, 150, Qt.MiddleButton);
        verify(ScrollFeel.autoscrolling, "a middle press on a list row did not start autoscroll");
        verify(tc.markerShown(list));
        mouseRelease(list, 150, 150, Qt.MiddleButton);
    }

    // A link that opens in the background on the middle button keeps it.
    function test_a_middle_tap_handler_keeps_the_middle_button() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        const link = createTemporaryQmlObject('import QtQuick; Item { width: 100; height: 100; property int got: 0; HoverHandler {} TapHandler { acceptedButtons: Qt.MiddleButton; onTapped: parent.got++ } }', list.contentItem);
        mouseClick(link, 50, 50, Qt.MiddleButton);
        verify(!ScrollFeel.autoscrolling, "autoscroll took a middle click from a link");
        compare(link.got, 1);
    }

    // ── The long wheel the autoscroll hint counts ──────────────────────

    SignalSpy { id: longSpy; target: ScrollFeel; signalName: "longWheelScroll" }

    function test_a_long_wheel_gesture_is_reported_once() {
        const list = createTemporaryObject(listComp, host);
        waitForRendering(list);
        longSpy.clear();
        // Under three screens: not a long one.
        const notches = Math.ceil(3 * list.height / tc.notch());
        for (let i = 0; i < notches - 2; i++) mouseWheel(list, 100, 100, 0, -120);
        compare(longSpy.count, 0);
        // Past three screens in the same gesture: once, however far it goes.
        for (let i = 0; i < 12; i++) mouseWheel(list, 100, 100, 0, -120);
        compare(longSpy.count, 1);
        // A new gesture starts counting again.
        wait(500);
        mouseWheel(list, 100, 100, 0, -120);
        compare(longSpy.count, 1);
    }
}
