#pragma once

#include "markdown/MdEditOps.h"

#include <QPair>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

class QTextDocument;

namespace heap::md {

// Rows and columns of a markdown pipe table, added and removed where the
// caret is (dogfood 2026-10-19). A table here is what the note draws as one:
// consecutive lines that start with "|", the second its |---| line. Every op
// rewrites the table's lines with the same number of cells in every row,
// keeps the alignment markers of the columns it does not touch, and leaves
// the caret in the cell the op is about.

enum class TableOp : std::uint8_t {
  RowAbove,
  RowBelow,
  ColumnLeft,
  ColumnRight,
  DeleteRow,
  DeleteColumn,
};

// "rowAbove", "rowBelow", "columnLeft", "columnRight", "deleteRow",
// "deleteColumn"; nothing for any other name.
std::optional<TableOp> tableOpFromName(const QString& name);

// The cells of one table line, "\|" kept as written: "| a | b\|c |" is
// {"a", "b\|c"}.
QStringList tableCells(const QString& line);

// Where line `line`, character `offset` of `lines` sits in a table.
struct TableLocation {
  bool ok = false;
  int firstLine = 0;  // the header's line
  int lastLine = 0;   // the last row's line
  int row = 0;        // line within the table: 0 the header, 1 the |---| line
  int column = 0;     // cell, from 0
};

TableLocation locateTable(const QStringList& lines, int line, int offset);

// The table's lines after `op` at (row, column). Empty `lines` with ok set
// means the op removed the whole table (its only row went). Deleting the
// last column, or "the row" on the |---| line, is no op (ok false). The
// rebuilt lines keep the first line's indent (a table in a list item).
struct TableEdit {
  bool ok = false;
  QStringList lines;
  int caretLine = 0;    // within `lines`
  int caretOffset = 0;  // characters into that line: the cell's text
};

TableEdit editTable(const QStringList& tableLines, int row, int column, TableOp op);

// The same on the editor's document, as one undo step. `applied` says
// whether the caret was in a table at all.
Selection applyTableOp(QTextDocument* document, Selection selection, TableOp op, bool* applied = nullptr);

// Moving through a table from the keyboard, as Notion, Obsidian and Typora
// do (dogfood 2026-10-20): Enter broke the row's line in two, which tore the
// table and hid the rows under the tear.
enum class TableMove : std::uint8_t {
  NextCell,      // Tab: the next cell; on the last cell, a new row
  PreviousCell,  // Shift+Tab: the cell before (stays on the first one)
  NextRow,       // Enter: the same column a row down; on the last row, a new row
  AddRow,        // Ctrl+Enter: a new row under this one, same column
  Exit,          // Shift+Enter: a line of text under the table
};

// Where cell `column` of a table line holds its text: [first, last) of the
// text without its padding; both inside the cell when it is empty.
QPair<int, int> cellTextSpan(const QString& line, int column);

// The move on the editor's document, one undo step when it edits. `applied`
// is false - and nothing moved - when the caret is not in a table (or a
// selection runs over more than one line, where Tab indents).
Selection applyTableMove(QTextDocument* document, Selection selection, TableMove move, bool* applied = nullptr);

// A position inside the table a table op at `position` works on: the
// position itself inside a table; on a blank line right under a table, its
// last row's first cell; -1 otherwise.
int tableAnchor(QTextDocument* document, int position);

}  // namespace heap::md
