#pragma once

#include <QFont>
#include <QList>
#include <QStringList>

namespace heap::md {

// How the grid a note draws over a pipe table is laid out (dogfood
// 2026-10-30): columns as wide as their text, a cell's long text wrapped
// and its row as tall as that, nothing cut short with an ellipsis. A table
// wider than the room it has gives up width from its widest columns first
// (their text wraps), each down to no less than `minColumn`; past that it is
// wider than the room and scrolls inside itself.
//
// The highlighter makes each row's hidden source line this tall, so the
// text under the table moves down as the row grows; the overlay draws the
// grid with these widths.

struct TableGridStyle {
  QFont body;
  QFont head;      // the header row's (bold)
  qreal padX = 8;  // a cell's room either side of its text
  qreal padY = 4;  // and over and under it
  qreal minColumn = 96;
  qreal room = 0;  // the width the table has; 0 or less: no limit
};

struct TableGrid {
  QList<qreal> widths;   // a column each, its padding in
  QList<qreal> heights;  // a row each (row 0 the header), padding in
  qreal width = 0;       // the widths together
  qreal room = 0;        // the style's
};

// `rows`: each row's cells as they are drawn (markup gone), the header
// first. A row with fewer cells than another is drawn with empty ones.
TableGrid layoutTableGrid(const QList<QStringList>& rows, const TableGridStyle& style);

// Column widths held to `room` in all: the widest shrink to one cap (water
// filling), none below the smaller of its own width and `minColumn`; the
// result is wider than `room` when even that does not fit.
QList<qreal> fitTableColumns(const QList<qreal>& natural, qreal room, qreal minColumn);

// The height `text` takes in `font` wrapped to `width` (at a word, or
// anywhere in a word longer than the line): a line at least.
qreal wrappedTextHeight(const QString& text, const QFont& font, qreal width);
// The width of `text` on one line. Both as Qt Quick's Text lays it out.
qreal textAdvance(const QString& text, const QFont& font);

}  // namespace heap::md
