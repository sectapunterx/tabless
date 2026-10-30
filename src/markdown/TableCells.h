#pragma once

#include <QString>

#include <cstdint>

class QTextDocument;

namespace heap::md {

// A table edited in its grid (dogfood 2026-10-30): the note keeps a plain
// pipe table, the grid stays drawn while the caret is in it, and the caret
// moves and types one cell at a time - the raw "|" never shows. These are
// the rules for that caret on the editor's document; the drawn cell and
// the keys are MdTableEditor's.

// A cell's text as the grid shows and types it: "\|" is "|".
QString cellDisplayText(const QString& raw);
// Text typed into a cell as the source holds it: "|" escaped, a line break
// or a tab a space (either would tear the row).
QString cellSourceText(const QString& typed);
// The same place in a cell's source and in its shown text.
int cellDisplayOffset(const QString& raw, int rawOffset);
int cellRawOffset(const QString& raw, int displayOffset);

// The cell a document position is in. A position in a cell's padding, or
// before the row's first pipe / after its last, reads as the nearest end of
// the nearest cell's text; the |---| line as the header. Only a table the
// note draws (a header with its |---| line under it), not one in a fence.
struct TableCell {
  bool ok = false;
  int tableStart = 0;  // where the header's line starts
  int tableEnd = 0;    // where the last row's line ends (before its break)
  int lines = 0;       // the table's lines, the |---| line counted
  int columns = 0;     // the header's cells
  int line = 0;        // the cell's line in the table: 0 the header, 2… the rows
  int column = 0;
  int textStart = 0;  // the cell's text in the document, padding left out;
  int textEnd = 0;    // both one space in from the left bar when it is empty
  QString raw;        // the document between textStart and textEnd
};

TableCell tableCellAt(QTextDocument* document, int position);
// Cell (line, column) of the table whose header starts at `tableStart`;
// line 1 (the |---| line) is the header's.
TableCell tableCell(QTextDocument* document, int tableStart, int line, int column);

// The keys that move the caret through the grid instead of through the
// source. Up / Down go a cell up / down (the caret at the end of its text);
// Left / Right go a character, and over a cell's edge into the next one;
// Home / End to the ends of the cell's text; Backspace at a cell's start and
// Delete at its end do nothing (they took the pipe); Escape leaves the
// table. Out of the table's top the caret goes to the line above; out of
// its bottom to the line under it, a line made (as Shift+Enter makes one)
// when there is none.
enum class CellKey : std::uint8_t { Left, Right, Up, Down, Home, End, Backspace, Delete, Escape };

// `handled` false: the key is the text field's own (the caret is not in a
// table, or Backspace deletes a selection inside one cell). `anchor` and
// `position` are the selection after it; an edit was made on `document`
// only when leaving the table needed a line under it.
struct CellMove {
  bool handled = false;
  int anchor = 0;
  int position = 0;
};

CellMove applyCellKey(QTextDocument* document, int anchor, int position, CellKey key, bool extend);

// Down from the line just above a table (up from the one just under it):
// into its first (last) row's first cell. Not handled anywhere else; the
// caller says the caret is on its line's last (first) wrapped line.
CellMove enterTable(QTextDocument* document, int position, bool down);

// Text typed with the caret in a cell. With a selection over more than one
// cell nothing is typed (handled, nothing changed): it would take the pipes
// between them. Text with a "|", a line break or a tab is put in as
// cellSourceText, one undo step. Any other text is the field's to type -
// not handled - at `position`, moved into the cell's text first when the
// caret was in its padding (`anchor` == `position` then).
CellMove typeIntoCell(QTextDocument* document, int anchor, int position, const QString& text);

// A column after the table's last, or a row under its last row, from the
// grid's "+": the caret in the new column's header cell / the new row's
// first cell, -1 when `tableStart` is no table. One undo step.
int addTableColumn(QTextDocument* document, int tableStart);
int addTableRow(QTextDocument* document, int tableStart);

}  // namespace heap::md
