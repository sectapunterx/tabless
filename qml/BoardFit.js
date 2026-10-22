.pragma library

// Which columns a narrow board folds into strips by itself (dogfood
// 2026-10-22, BoardDrag sheet): the board does not slide sideways, its
// columns give way instead. Pure, so the order is testable without a board.
//
// o.width            the board's width
// o.gap              the space between two columns
// o.minWidth         the narrowest an open column gets
// o.stripWidth       a folded column, a strip with its name run down it
// o.doneFoldedWidth  a folded Done at rest: wider, it says "Show"
// o.columns          [{ id, done, empty, folded }], left to right; `empty`:
//                    no card of it on screen (a filter may hide them); `folded`
//                    is the column's own fold (by hand, an empty column,
//                    Done at rest), which this never undoes
// o.keep             { id: true } for the columns that never fold here: the
//                    cursor's, the selection's, the one a card is over, the
//                    one a card came from, the one the user opened
// o.anchor           the cursor's column index, or -1
//
// Returns { strips: { id: true }, overflow: bool }: the columns shown as
// strips on top of their own fold (a folded Done narrows to a strip too),
// and whether even that does not fit, so the board scrolls after all.
//
// The order: Done first, then the empty columns the user opened, then the
// outermost open columns, farthest from the cursor first (from the middle
// of the board when there is none; on a tie the left one goes first). The
// last open column never folds.
function fit(o) {
    const cols = o.columns || [];
    const keep = o.keep || {};
    const strips = {};
    const n = cols.length;
    if (n === 0) return { strips: strips, overflow: false };

    function widthOf(c) {
        if (strips[c.id]) return o.stripWidth;
        if (c.folded) return c.done ? o.doneFoldedWidth : o.stripWidth;
        return o.minWidth;
    }
    function total() {
        let w = Math.max(0, n - 1) * o.gap;
        for (let i = 0; i < n; i++) w += widthOf(cols[i]);
        return w;
    }
    function fits() { return total() <= o.width; }
    if (fits()) return { strips: strips, overflow: false };
    // The board keeps one column open, whatever the width: a row of strips
    // and nothing to read helps nobody, so then it scrolls.
    function openCount() {
        let k = 0;
        for (let i = 0; i < n; i++) if (!cols[i].folded && !strips[cols[i].id]) k++;
        return k;
    }
    function fold(c) {
        if (!c.folded && openCount() <= 1) return;
        strips[c.id] = true;
    }

    // 1. Done, open or folded wide.
    for (let i = 0; i < n && !fits(); i++) {
        const c = cols[i];
        if (c.done && !keep[c.id]) fold(c);
    }
    // 2. Empty columns: none of their cards on screen, or none at all.
    for (let i = 0; i < n && !fits(); i++) {
        const c = cols[i];
        if (!c.done && !c.folded && c.empty && !keep[c.id]) fold(c);
    }
    // 3. The outermost open ones, farthest from the cursor first.
    if (!fits()) {
        const anchor = o.anchor >= 0 ? o.anchor : (n - 1) / 2;
        const order = [];
        for (let i = 0; i < n; i++) {
            const c = cols[i];
            if (!c.done && !c.folded && !strips[c.id] && !keep[c.id]) order.push(i);
        }
        order.sort(function (a, b) {
            const d = Math.abs(b - anchor) - Math.abs(a - anchor);
            return d !== 0 ? d : a - b;
        });
        for (let k = 0; k < order.length && !fits(); k++) fold(cols[order[k]]);
    }
    return { strips: strips, overflow: !fits() };
}
