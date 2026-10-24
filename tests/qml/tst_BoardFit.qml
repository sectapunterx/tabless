// A narrow board folds columns into strips by itself instead of sliding
// sideways (dogfood 2026-10-22, BoardDrag sheet). The rule is pure
// (BoardFit.fit): Done first, then empty columns, then the outermost ones
// farthest from the cursor; never a kept column (cursor, selection, the
// column a card is over); scroll only if even strips do not fit.
import QtQuick
import QtTest
import "../../qml/BoardFit.js" as BoardFit

TestCase {
    name: "BoardFit"

    // Six columns and Done, as on the sheet. min 130, strip 36, gap 16,
    // a folded Done at rest 88.
    function cols(overrides) {
        const base = [
            { id: "backlog", done: false, empty: false, folded: false },
            { id: "todo", done: false, empty: false, folded: false },
            { id: "prog", done: false, empty: false, folded: false },
            { id: "review", done: false, empty: false, folded: false },
            { id: "qa", done: false, empty: false, folded: false },
            { id: "done", done: true, empty: false, folded: false }
        ];
        for (const k in (overrides || {}))
            for (let i = 0; i < base.length; i++)
                if (base[i].id === k) Object.assign(base[i], overrides[k]);
        return base;
    }
    function run(width, columns, keep, anchor) {
        return BoardFit.fit({ width: width, gap: 16, minWidth: 130, stripWidth: 36, doneFoldedWidth: 88,
                              columns: columns, keep: keep || {}, anchor: anchor === undefined ? -1 : anchor });
    }
    function stripped(r) { return Object.keys(r.strips).sort(); }

    function test_a_board_that_fits_folds_nothing() {
        // 6 × 130 + 5 × 16 = 860
        const r = run(860, cols());
        compare(stripped(r), []);
        verify(!r.overflow);
    }

    function test_done_goes_first() {
        // 859: one px short; Done to a strip (130 → 36) is enough.
        const r = run(859, cols());
        compare(stripped(r), ["done"]);
        verify(!r.overflow);
    }

    function test_a_done_folded_at_rest_narrows_to_a_strip_first() {
        // 5 × 130 + 88 + 80 = 818
        const c = cols({ done: { folded: true } });
        compare(stripped(run(818, c)), []);
        compare(stripped(run(817, c)), ["done"]);
    }

    function test_then_the_empty_columns_the_user_opened() {
        // Done strip: 5 × 130 + 36 + 80 = 766; one more column must go.
        const r = run(765, cols({ todo: { empty: true } }));
        compare(stripped(r), ["done", "todo"]);
    }

    function test_then_the_outermost_farthest_from_the_cursor() {
        // Cursor in prog (index 2): backlog (2 away) before review (1 away).
        const keep = { prog: true };
        compare(stripped(run(765, cols(), keep, 2)), ["backlog", "done"]);
        // Two must go: backlog and qa (both 2 away); todo / review stay.
        // 3 × 130 + 3 × 36 + 80 = 578
        compare(stripped(run(578, cols(), keep, 2)), ["backlog", "done", "qa"]);
    }

    function test_never_the_kept_columns() {
        // The cursor's column, the drag target and a selected card's column
        // stay open even at a width where nothing else is left to fold.
        const keep = { backlog: true, prog: true, qa: true };
        const r = run(300, cols(), keep, 0);
        compare(stripped(r), ["done", "review", "todo"]);
        verify(r.overflow, "three open columns do not fit 300 px: the board scrolls");
    }

    function test_a_kept_done_is_not_folded() {
        const r = run(765, cols(), { done: true }, 5);
        verify(!r.strips["done"]);
        // The farthest from Done (index 5) go: backlog, then todo.
        compare(stripped(r), ["backlog", "todo"]);
    }

    function test_no_cursor_folds_from_the_edges_left_first() {
        // Anchor is the middle (2.5): backlog and done are 2.5 away; done
        // goes by its own rule, then backlog (left first on a tie with qa).
        const r = run(765, cols());
        compare(stripped(r), ["backlog", "done"]);
    }

    function test_columns_folded_by_hand_stay_as_they_are() {
        // A column folded by hand is already a strip: it is not in `strips`
        // and is counted at the strip width.
        // 4 × 130 + 36 + 36(done strip) + 80 = 672
        const c = cols({ todo: { folded: true } });
        const r = run(672, c);
        compare(stripped(r), ["done"]);
        verify(!r.strips["todo"]);
    }

    function test_overflow_only_when_even_strips_do_not_fit() {
        const keep = { prog: true };
        // One open column + 5 strips + gaps: 130 + 180 + 80 = 390.
        const fits = run(390, cols(), keep, 2);
        verify(!fits.overflow);
        compare(stripped(fits).length, 5);
        verify(run(389, cols(), keep, 2).overflow);
    }

    function test_the_last_open_column_never_folds() {
        // No cursor, nothing kept, far too narrow: one column stays open and
        // the board scrolls rather than show a row of strips.
        const r = run(100, cols());
        compare(stripped(r).length, 5);
        verify(r.overflow);
        // Only Done open, the rest folded by hand: Done stays open.
        const c = cols({ backlog: { folded: true }, todo: { folded: true }, prog: { folded: true },
                         review: { folded: true }, qa: { folded: true } });
        const d = run(100, c);
        verify(!d.strips["done"]);
        verify(d.overflow);
    }

    function test_empty_board_is_fine() {
        const r = BoardFit.fit({ width: 100, gap: 16, minWidth: 130, stripWidth: 36, doneFoldedWidth: 88,
                                 columns: [], keep: {}, anchor: -1 });
        compare(stripped(r), []);
        verify(!r.overflow);
    }
}
