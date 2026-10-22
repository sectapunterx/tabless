// Dragging a card on the board and the narrow board (dogfood 2026-10-22,
// BoardDrag sheet):
//  - the card in the hand keeps scale 1.03 and gains a shadow;
//  - under the manual order the target column opens an empty slot the
//    card's height where it would land, and the cards below move aside;
//    under a sort there is no slot, only the column is marked;
//  - the place the card came from is a dim slot while the card is over its
//    own column and closes when it leaves; Esc, a drop outside, and rapid
//    passes between columns leave nothing moved;
//  - a narrow board folds columns into strips instead of scrolling, never
//    the cursor's; a strip opens under a card held on it for 0.4 s, and on
//    a click.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "BoardDrag"
    when: windowShown
    visible: true
    width: 1300
    height: 700

    Item { id: host; width: 1200; height: 700 }

    property var seeded: []
    readonly property string probe: "dragslotprobe"

    // Seeds `perColumn[c]` cards into the c-th status; returns their ids per
    // column, in the column's (manual) order: new cards land on top.
    function seed(perColumn) {
        const statuses = AppController.statuses;
        const out = [];
        for (let c = 0; c < perColumn.length; c++) {
            const ids = [];
            for (let i = 0; i < perColumn[c]; i++) {
                const d = AppController.newTaskDraft(statuses[c].id);
                d._isNew = true;
                d.id = "DSL-" + c + "-" + i;
                d.title = tc.probe + " card " + c + "." + i;
                AppController.saveTask(d);
                tc.seeded.push(d.id);
                ids.unshift(d.id);
            }
            out.push(ids);
        }
        return out;
    }

    function cleanup() {
        for (let i = 0; i < tc.seeded.length; i++) AppController.deleteTask(tc.seeded[i]);
        AppController.clearPendingUndo();
        AppController.clearSelection();
        tc.seeded = [];
    }

    function makeBoard(w) {
        const b = createTemporaryQmlObject('import TodoCpp; KanbanBoard { height: 700 }', host);
        verify(b !== null);
        b.width = w || 1200;
        b.searchText = tc.probe;
        waitForRendering(b);
        return b;
    }

    // The column items, left to right.
    function columns(item, out) {
        out = out || [];
        for (let i = 0; i < item.children.length; i++) {
            const c = item.children[i];
            if (typeof c.autoStrip === "boolean" && typeof c.statusId === "string") out.push(c);
            else columns(c, out);
        }
        return out;
    }
    function column(b, statusId) {
        const cs = columns(b);
        for (let i = 0; i < cs.length; i++) if (cs[i].statusId === statusId) return cs[i];
        return null;
    }
    function named(item, name) {
        for (let i = 0; i < item.children.length; i++) {
            const c = item.children[i];
            if (c.objectName === name) return c;
            const f = named(c, name);
            if (f) return f;
        }
        return null;
    }
    function cardIn(col, id) {
        const list = col.taskList;
        for (let i = 0; i < list.count; i++) {
            const it = list.itemAtIndex(i);
            if (it && it.taskId === id) return it;
        }
        return null;
    }
    function statusOf(id) { return AppController.taskById(id).status; }

    // Press on `card` and move the pointer (in steps) to `to`, in board
    // coordinates; the pointer stays down.
    function pickUp(b, card) {
        const from = card.mapToItem(b, card.width / 2, 20);
        mousePress(b, from.x, from.y);
        mouseMove(b, from.x, from.y + 8);
        mouseMove(b, from.x, from.y + 2);
        return from;
    }
    function moveTo(b, from, to, steps) {
        const n = steps || 8;
        for (let i = 1; i <= n; i++)
            mouseMove(b, from.x + (to.x - from.x) * i / n, from.y + (to.y - from.y) * i / n);
        return to;
    }
    function shiftsOf(col) {
        const out = [];
        const list = col.taskList;
        for (let i = 0; i < list.count; i++) {
            const it = list.itemAtIndex(i);
            if (it && !it._lifted) out.push(it.slotShift);
        }
        return out;
    }

    function test_lifted_card_has_scale_and_shadow() {
        const ids = seed([1, 1]);
        const b = makeBoard();
        const c0 = column(b, AppController.statuses[0].id);
        tryVerify(() => cardIn(c0, ids[0][0]) !== null);
        const card = cardIn(c0, ids[0][0]);
        const shadow = named(card, "tc-lift-shadow");
        verify(shadow !== null);
        verify(!shadow.active, "a card at rest carries no shadow");
        const from = pickUp(b, card);
        tryVerify(() => card._lifted, 1000);
        verify(shadow.active, "the lifted card has no shadow");
        tryCompare(card, "scale", 1.03, 1000);
        compare(card.rotation, 0, "no tilt");
        mouseRelease(b, from.x, from.y + 2);
        tryVerify(() => !shadow.active, 1000);
    }

    function test_manual_sort_opens_a_slot_where_the_card_lands() {
        const ids = seed([3, 1]);
        const b = makeBoard();
        compare(b.sortMode, "manual");
        const c0 = column(b, AppController.statuses[0].id);
        const c1 = column(b, AppController.statuses[1].id);
        tryVerify(() => cardIn(c0, ids[0][2]) !== null && cardIn(c1, ids[1][0]) !== null);
        const target = cardIn(c0, ids[0][1]);
        const below = cardIn(c0, ids[0][2]);
        const above = cardIn(c0, ids[0][0]);
        const card = cardIn(c1, ids[1][0]);
        const slot = named(c0.taskList, "drop-slot");
        verify(slot !== null);
        verify(!slot.visible);

        const from = pickUp(b, card);
        // Just above the middle of the second card: the slot opens above it.
        const to = target.mapToItem(b, target.width / 2, target.height / 2 - 6);
        moveTo(b, from, to);
        tryVerify(() => c0.slotOn, 1000, "no slot under the manual order");
        tryCompare(slot, "opacity", 1, 1000);
        tryCompare(slot, "y", target.y, 1000, "the slot is not where the card would land");
        compare(slot.height, card.height, "the slot is not the card's height");
        compare(above.slotShift, 0, "a card above the slot moved");
        const step = card.height + c0.taskList.spacing;
        compare(target.slotShift, step, "the card under the slot did not move aside");
        compare(below.slotShift, step);

        mouseRelease(b, to.x, to.y);
        tryVerify(() => statusOf(ids[1][0]) === AppController.statuses[0].id, 2000, "the drop did not move the card");
        compare(c0.taskFilter.ids(), [ids[0][0], ids[1][0], ids[0][1], ids[0][2]],
                "the card did not land where the slot was");
        tryVerify(() => shiftsOf(c0).every(s => s === 0), 1000, "cards stayed moved aside after the drop");
        tryVerify(() => !c0.slotOn && !slot.visible, 1000, "the slot stayed after the drop");
    }

    function test_several_selected_cards_open_one_slot_and_land_there() {
        const ids = seed([2, 2]);
        const b = makeBoard();
        const c0 = column(b, AppController.statuses[0].id);
        const c1 = column(b, AppController.statuses[1].id);
        tryVerify(() => cardIn(c0, ids[0][1]) !== null && cardIn(c1, ids[1][1]) !== null);
        AppController.setSelectedTaskIds([ids[1][0], ids[1][1]]);
        const card = cardIn(c1, ids[1][0]);
        const target = cardIn(c0, ids[0][1]);
        const from = pickUp(b, card);
        const to = target.mapToItem(b, target.width / 2, 4);
        moveTo(b, from, to);
        tryVerify(() => c0.slotOn, 1000);
        compare(c0.taskFilter.ids().length, 2);
        const shownSlots = () => {
            const out = [];
            (function walk(it) {
                for (let i = 0; i < it.children.length; i++) {
                    const ch = it.children[i];
                    if (ch.objectName === "drop-slot" && ch.opacity > 0) out.push(ch);
                    walk(ch);
                }
            })(b);
            return out;
        };
        tryVerify(() => shownSlots().length === 1, 1000, "not one slot for one drag of several cards");
        compare(shownSlots()[0].height, card.height);
        mouseRelease(b, to.x, to.y);
        tryVerify(() => statusOf(ids[1][0]) === AppController.statuses[0].id
                  && statusOf(ids[1][1]) === AppController.statuses[0].id, 2000, "the selection did not move");
        const got = c0.taskFilter.ids();
        compare(got[0], ids[0][0], "the cards did not land at the slot");
        compare(got[got.length - 1], ids[0][1], "the cards did not land at the slot");
    }

    function test_priority_sort_has_no_slot_only_the_column_mark() {
        const ids = seed([2, 1]);
        const b = makeBoard();
        b.sortMode = "priority";
        const c0 = column(b, AppController.statuses[0].id);
        const c1 = column(b, AppController.statuses[1].id);
        tryVerify(() => cardIn(c0, ids[0][1]) !== null && cardIn(c1, ids[1][0]) !== null);
        const target = cardIn(c0, ids[0][1]);
        const card = cardIn(c1, ids[1][0]);
        const slot = named(c0.taskList, "drop-slot");
        const from = pickUp(b, card);
        const to = target.mapToItem(b, target.width / 2, 4);
        moveTo(b, from, to);
        tryVerify(() => c0.dragOver, 1000, "the column is not marked under the card");
        wait(200);
        verify(!c0.slotOn, "a slot under a sort: the sort decides the place");
        compare(slot.opacity, 0);
        verify(shiftsOf(c0).every(s => s === 0), "cards moved aside under a sort");
        verify(named(c0, "column-drop-frame").visible, "no column outline under the card");
        mouseRelease(b, to.x, to.y);
        tryVerify(() => statusOf(ids[1][0]) === AppController.statuses[0].id, 2000);
    }

    function test_origin_slot_dims_in_own_column_and_closes_outside() {
        const ids = seed([3, 1]);
        const b = makeBoard();
        const c0 = column(b, AppController.statuses[0].id);
        const c1 = column(b, AppController.statuses[1].id);
        tryVerify(() => cardIn(c0, ids[0][2]) !== null && cardIn(c1, ids[1][0]) !== null);
        const card = cardIn(c0, ids[0][0]);
        const next = cardIn(c0, ids[0][1]);
        const origin = named(c0.taskList, "drop-origin");
        verify(origin !== null);

        const from = pickUp(b, card);
        tryVerify(() => c0.isOrigin && c0.dragOver, 1000);
        // Over its own place: no second slot, its own place is the slot.
        verify(!c0.slotOn, "a slot opened next to the card's own place");
        tryVerify(() => origin.visible && origin.opacity > 0, 1000, "the origin slot is gone over its own column");
        compare(next.slotShift, 0);

        // Out over the next column: the origin closes and the cards close up.
        const over = c1.mapToItem(b, c1.width / 2, c1.height - 60);
        const at = moveTo(b, from, over);
        tryVerify(() => !c0.dragOver && c0.originCollapsed, 1000);
        tryCompare(origin, "opacity", 0, 1000);
        tryCompare(next, "slotShift", -(card.height + c0.taskList.spacing), 1000);

        // Back over its own column, lower down: the origin is a dim slot again
        // and a slot opens where the card would land.
        const low = cardIn(c0, ids[0][2]);
        const back = low.mapToItem(b, low.width / 2, low.height - 4);
        moveTo(b, at, back);
        tryVerify(() => c0.dragOver && c0.slotOn, 1000);
        tryVerify(() => origin.opacity > 0 && origin.opacity < 1, 1000, "the origin is not the dim slot");
        compare(next.slotShift, 0, "a card above the slot moved");

        // Esc: everything goes back, nothing moved.
        verify(b.cancelDrag(), "there was a drag to cancel");
        // The grab goes on the next turn of the event loop (as after a real
        // Esc, long before the button comes up).
        tryVerify(() => !b.dragActive, 1000, "Esc did not let go of the card");
        mouseRelease(b, back.x, back.y);
        tryVerify(() => shiftsOf(c0).every(s => s === 0), 1000, "cards stayed moved aside after Esc");
        verify(!c0.slotOn);
        compare(c0.taskFilter.ids(), ids[0], "Esc reordered the column");
        compare(statusOf(ids[0][0]), AppController.statuses[0].id);
    }

    // The cards of a column as drawn, top to bottom: each one the list's
    // spacing under the one above, no closer (overlap) and no further.
    function overlapsIn(col) {
        const list = col.taskList;
        const boxes = [];
        for (let i = 0; i < list.count; i++) {
            const it = list.itemAtIndex(i);
            if (!it || !it.visible) continue;
            // In the list's own frame, with the slot's shift and not the
            // lifted scale settling back.
            const top = it.y + it.slotShift;
            boxes.push({ id: it.taskId, top: top, bottom: top + it.height, parentOk: it.parent === list.contentItem });
        }
        boxes.sort((a, c) => a.top - c.top);
        const out = [];
        for (let i = 0; i < boxes.length; i++) {
            if (!boxes[i].parentOk) out.push(boxes[i].id + " is not back in its list");
            if (i > 0 && Math.abs(boxes[i].top - boxes[i - 1].bottom - list.spacing) > 0.5)
                out.push(boxes[i - 1].id + " [" + boxes[i - 1].top + "," + boxes[i - 1].bottom + "] over " + boxes[i].id + " at " + boxes[i].top);
        }
        return out;
    }

    // Dogfood 2026-10-22: a card taken into another column and let go of
    // with Esc came back, but it and the card under it overlapped (one's
    // background gone) until something laid the column out again. The top
    // card, open under the pointer, closes as it is lifted; the list laid
    // the rest out from it while it sat in the drag layer, and nothing laid
    // them out again once it was back.
    function test_esc_in_another_column_leaves_the_origin_laid_out() {
        const ids = seed([3, 1]);
        for (let c = 0; c < ids.length; c++) {
            for (const id of ids[c]) {
                const t = AppController.taskById(id);
                // A checklist at rest; open, the description's line too.
                t.desc = "PR #4 the first line of the description\n- [ ] one\n- [x] two";
                AppController.saveTask(t);
            }
        }
        const b = makeBoard();
        const c0 = column(b, AppController.statuses[0].id);
        const c1 = column(b, AppController.statuses[1].id);
        tryVerify(() => cardIn(c0, ids[0][2]) !== null && cardIn(c1, ids[1][0]) !== null);
        const card = cardIn(c0, ids[0][0]);
        const rest = card.height;
        // The pointer rests on it (no hover offscreen): it opens.
        card._hoverRested = true;
        tryVerify(() => card.height > rest, 1000, "the card did not open");
        wait(Theme.durMove + 50);
        compare(overlapsIn(c0), [], "the column is not laid out before the drag");

        const from = pickUp(b, card);
        tryVerify(() => card._lifted, 1000);
        const over = c1.mapToItem(b, c1.width / 2, c1.height - 60);
        moveTo(b, from, over);
        tryVerify(() => !c0.dragOver && c0.originCollapsed, 1000);
        wait(Theme.durSlot + 50);
        // Esc, the pointer far from the card.
        card._hoverRested = false;
        verify(b.cancelDrag(), "there was a drag to cancel");
        tryVerify(() => !b.dragActive, 1000, "Esc did not let go of the card");
        tryVerify(() => shiftsOf(c0).every(s => s === 0), 1000, "cards stayed moved aside after Esc");
        compare(overlapsIn(c0), [], "the column is not laid out once the card is back");
        mouseRelease(b, over.x, over.y);
        // And it stays so once everything has settled.
        wait(Theme.durMove + 100);
        compare(overlapsIn(c0), [], "the column fell apart after settling");
        compare(c0.taskFilter.ids(), ids[0], "Esc reordered the column");
    }

    function test_rapid_passes_and_a_drop_outside_leave_nothing_moved() {
        const ids = seed([2, 2]);
        const b = makeBoard(1000);
        const c0 = column(b, AppController.statuses[0].id);
        const c1 = column(b, AppController.statuses[1].id);
        tryVerify(() => cardIn(c0, ids[0][1]) !== null && cardIn(c1, ids[1][1]) !== null);
        const card = cardIn(c0, ids[0][0]);
        let at = pickUp(b, card);
        const p0 = c0.mapToItem(b, c0.width / 2, 80);
        const p1 = c1.mapToItem(b, c1.width / 2, 80);
        for (let k = 0; k < 6; k++) at = moveTo(b, at, k % 2 ? p0 : p1, 2);
        // Off the board (it is 1000 wide in a 1300 window).
        at = moveTo(b, at, Qt.point(1250, 300), 4);
        tryVerify(() => !c0.dragOver && !c1.dragOver, 1000);
        mouseRelease(b, at.x, at.y);
        tryVerify(() => !b.dragActive, 1000);
        tryVerify(() => shiftsOf(c0).every(s => s === 0) && shiftsOf(c1).every(s => s === 0), 1000,
                  "cards stayed moved aside after a drop outside");
        compare(c0.taskFilter.ids(), ids[0]);
        compare(c1.taskFilter.ids(), ids[1]);
    }

    function test_resize_mid_drag_keeps_the_drag() {
        const ids = seed([2, 1]);
        const b = makeBoard();
        const c0 = column(b, AppController.statuses[0].id);
        const c1 = column(b, AppController.statuses[1].id);
        tryVerify(() => cardIn(c0, ids[0][1]) !== null && cardIn(c1, ids[1][0]) !== null);
        const card = cardIn(c1, ids[1][0]);
        let at = pickUp(b, card);
        b.width = 900;
        waitForRendering(b);
        verify(b.dragActive, "a resize dropped the card");
        const target = cardIn(c0, ids[0][0]);
        const to = target.mapToItem(b, target.width / 2, 4);
        moveTo(b, at, to);
        tryVerify(() => c0.slotOn, 1000);
        mouseRelease(b, to.x, to.y);
        tryVerify(() => statusOf(ids[1][0]) === AppController.statuses[0].id, 2000);
        compare(c0.taskFilter.ids()[0], ids[1][0], "the card did not land at the top slot");
    }

    // ── Narrow board ──────────────────────────────────────────────────
    function nonDone() {
        const out = [];
        const sts = AppController.statuses;
        for (let i = 0; i < sts.length; i++)
            if (AppController.statusCategory(sts[i].id) !== "done") out.push(i);
        return out;
    }
    // A board width with room for `open` columns at their least width and
    // strips for the rest, the "+ column" tile and the page's margins.
    function widthFor(b, open) {
        const hs = named(b, "board-hscroll");
        const n = AppController.statuses.length;
        return (b.width - hs.width) + b.tileRoom + open * b.minColumnWidth + (n - open) * b.foldedWidth
            + (n - 1) * b.columnGap + 4;
    }
    function seedAllOpen() {
        const per = [];
        const nd = nonDone();
        for (let i = 0; i < AppController.statuses.length; i++) per.push(nd.indexOf(i) >= 0 ? 1 : 0);
        return seed(per);
    }

    function test_narrow_board_folds_columns_instead_of_scrolling() {
        const ids = seedAllOpen();
        const nd = nonDone();
        verify(nd.length >= 4, "setup: the profile needs four open columns");
        // Cursor in the second open column.
        const cursorCol = nd[1];
        const b = makeBoard(1200);
        b.cursorTaskId = ids[cursorCol][0];
        const hs = named(b, "board-hscroll");
        tryVerify(() => columns(b).every(c => !c.autoStrip), 1000, "a wide board folded a column");

        b.width = widthFor(b, 2);
        waitForRendering(b);
        const cs = columns(b);
        tryVerify(() => cs.some(c => c.autoStrip), 1000, "a narrow board folded nothing");
        verify(!column(b, AppController.statuses[cursorCol].id).autoStrip, "the cursor's column was folded");
        verify(!column(b, AppController.statuses[cursorCol].id).folded);
        tryVerify(() => hs.contentWidth <= hs.width + 1, 1000,
                  "the board still scrolls sideways: " + hs.contentWidth + " > " + hs.width);
        for (let i = 0; i < cs.length; i++)
            if (cs[i].autoStrip) compare(cs[i].width, b.foldedWidth, "a folded column is not a strip");

        // Not saved: only folds by hand are remembered.
        let s = {};
        try { s = JSON.parse(AppController.appSettingsJson || "{}") || {}; } catch (e) { s = {}; }
        const saved = Array.isArray(s.boardCollapsed) ? s.boardCollapsed : [];
        for (let i = 0; i < cs.length; i++)
            if (cs[i].autoStrip) verify(saved.indexOf(cs[i].statusId) < 0, "an automatic fold was saved");

        // Wide again: everything opens.
        b.width = 1200;
        tryVerify(() => columns(b).every(c => !c.autoStrip), 1000);
    }

    function test_click_on_a_strip_opens_it_and_another_gives_way() {
        const ids = seedAllOpen();
        const nd = nonDone();
        const b = makeBoard(1200);
        b.width = widthFor(b, 2);
        b.cursorTaskId = ids[nd[1]][0];
        const cs = columns(b);
        let strip = null;
        tryVerify(() => { strip = cs.find(c => c.autoStrip && !c.isDone); return strip !== null; }, 1000,
                  "setup: no open column folded on a narrow board");
        const before = cs.filter(c => c.autoStrip).length;
        const area = named(strip, "column-expand");
        verify(area !== null);
        mouseClick(area, area.width / 2, 40);
        tryVerify(() => !strip.folded, 1000, "a click on the strip did not open it");
        compare(cs.filter(c => c.autoStrip).length, before, "nothing gave way");
        const hs = named(b, "board-hscroll");
        tryVerify(() => hs.contentWidth <= hs.width + 1, 1000);
        verify(!column(b, AppController.statuses[nd[1]].id).folded, "the cursor's column folded to make room");
    }

    function test_a_card_held_on_a_strip_opens_it_after_a_moment() {
        const ids = seedAllOpen();
        const nd = nonDone();
        const b = makeBoard(1200);
        b.width = widthFor(b, 2);
        b.cursorTaskId = ids[nd[1]][0];
        const cs = columns(b);
        let strip = null;
        tryVerify(() => { strip = cs.find(c => c.autoStrip && !c.isDone); return strip !== null; }, 1000);
        const src = column(b, AppController.statuses[nd[1]].id);
        tryVerify(() => cardIn(src, ids[nd[1]][0]) !== null);
        const card = cardIn(src, ids[nd[1]][0]);
        let at = pickUp(b, card);
        const over = strip.mapToItem(b, strip.width / 2, 120);
        at = moveTo(b, at, over);
        tryVerify(() => strip.dragOver, 1000, "the strip does not take the card");
        wait(150);
        verify(strip.folded, "the strip opened at once, not after a moment");
        tryVerify(() => !strip.folded, 1500, "the strip did not open under the held card");
        verify(src.isOrigin && !src.folded, "the column the card came from folded under it");
        // Into the opened column, then let go there.
        const into = strip.mapToItem(b, strip.width / 2, strip.height - 40);
        at = moveTo(b, at, into, 3);
        tryVerify(() => strip.dragOver, 1000);
        mouseRelease(b, at.x, at.y);
        tryVerify(() => statusOf(ids[nd[1]][0]) === strip.statusId, 2000, "the drop did not land in the opened column");
    }
}
