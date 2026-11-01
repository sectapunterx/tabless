// A table edited in its grid (dogfood 2026-10-30): the caret walks and types
// one cell at a time, the raw "|" never shows and is never torn. Each case
// checks the text and where the caret lands.

#include "markdown/TableCells.h"

#include <QTextBlock>
#include <QTextDocument>

#include <gtest/gtest.h>

using namespace heap::md;

namespace {

const QString kTable = QStringLiteral("Intro\n\n| A | B |\n| --- | --- |\n| one | two |\n| x\\|y |  |\n\nAfter");

int after(const QString& text, const QString& needle) {
  return static_cast<int>(text.indexOf(needle) + needle.size());
}

// What a key does with the caret after `needle`.
struct Moved {
  QString text;
  CellMove move;
};

Moved key(const QString& text, int caret, CellKey which, bool extend = false, int anchor = -1) {
  QTextDocument doc(text);
  Moved m;
  m.move = applyCellKey(&doc, anchor < 0 ? caret : anchor, caret, which, extend);
  m.text = doc.toPlainText();
  return m;
}

}  // namespace

TEST(TableCells, ShownText) {
  EXPECT_EQ(cellDisplayText(QStringLiteral("a\\|b")), QStringLiteral("a|b"));
  EXPECT_EQ(cellSourceText(QStringLiteral("a|b\nc\td")), QStringLiteral("a\\|b c d"));
  const QString raw = QStringLiteral("x\\|y");
  EXPECT_EQ(cellDisplayOffset(raw, 1), 1);
  EXPECT_EQ(cellDisplayOffset(raw, 2), 1) << "inside the pair: before its |";
  EXPECT_EQ(cellDisplayOffset(raw, 3), 2);
  EXPECT_EQ(cellRawOffset(raw, 2), 3);
  EXPECT_EQ(cellRawOffset(raw, 9), 4);
}

TEST(TableCells, CellAtAPosition) {
  QTextDocument doc(kTable);
  const TableCell two = tableCellAt(&doc, after(kTable, QStringLiteral("| tw")));
  ASSERT_TRUE(two.ok);
  EXPECT_EQ(two.tableStart, kTable.indexOf(QStringLiteral("| A")));
  EXPECT_EQ(two.lines, 4);
  EXPECT_EQ(two.columns, 2);
  EXPECT_EQ(two.line, 2);
  EXPECT_EQ(two.column, 1);
  EXPECT_EQ(two.raw, QStringLiteral("two"));
  // Before the first pipe and after the last: the row's end cells.
  const int rowStart = static_cast<int>(kTable.indexOf(QStringLiteral("| one")));
  EXPECT_EQ(tableCellAt(&doc, rowStart).column, 0);
  EXPECT_EQ(tableCellAt(&doc, after(kTable, QStringLiteral("two |"))).column, 1);
  // The |---| line reads as the header.
  EXPECT_EQ(tableCellAt(&doc, after(kTable, QStringLiteral("| --"))).line, 0);
  // An empty cell: one space in from its bar.
  const TableCell empty = tableCellAt(&doc, after(kTable, QStringLiteral("y | ")));
  EXPECT_EQ(empty.raw, QString());
  EXPECT_EQ(empty.textStart, after(kTable, QStringLiteral("y | ")));
  EXPECT_FALSE(tableCellAt(&doc, 0).ok);
  // Not a table: no |---| line, in a fence, a row without its closing pipe.
  QTextDocument loose(QStringLiteral("| a |\n| b |"));
  EXPECT_FALSE(tableCellAt(&loose, 2).ok);
  QTextDocument fenced(QStringLiteral("```\n| a |\n|---|\n| b |\n```"));
  EXPECT_FALSE(tableCellAt(&fenced, 6).ok);
  EXPECT_TRUE(tableCell(&doc, two.tableStart, 3, 0).ok);
  EXPECT_FALSE(tableCell(&doc, after(kTable, QStringLiteral("| on")), 3, 0).ok) << "only from the header's line";
}

TEST(TableCells, UpDownGoACell) {
  const int inTwo = after(kTable, QStringLiteral("| tw"));
  Moved m = key(kTable, inTwo, CellKey::Up);
  EXPECT_TRUE(m.move.handled);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("| B"))) << "over the |---| line, to the header's end";
  m = key(kTable, inTwo, CellKey::Down);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("y | "))) << "the empty cell under it";
  // Out of the top: the line above's end; out of the bottom: the line under.
  m = key(kTable, after(kTable, QStringLiteral("| A")), CellKey::Up);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("Intro\n")));
  m = key(kTable, after(kTable, QStringLiteral("| x")), CellKey::Down);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("|  |\n")));
  EXPECT_EQ(m.text, kTable) << "moving edits nothing";
  // A table that ends the text: Down makes the line to go to.
  const QString last = QStringLiteral("| A |\n|---|\n| 1 |");
  m = key(last, after(last, QStringLiteral("| 1")), CellKey::Down);
  EXPECT_EQ(m.text, last + QStringLiteral("\n\n"));
  EXPECT_EQ(m.move.position, m.text.size());
  EXPECT_FALSE(key(kTable, 2, CellKey::Up).move.handled) << "outside a table the key is the field's";
}

TEST(TableCells, LeftRightCrossCellsAndEscapes) {
  // Right at a cell's end: the next cell's start; at a row's end: the next row's.
  Moved m = key(kTable, after(kTable, QStringLiteral("| one")), CellKey::Right);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("one | ")));
  m = key(kTable, after(kTable, QStringLiteral("| two")), CellKey::Right);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("two |\n| ")));
  // Left at a cell's start: the cell before's end.
  m = key(kTable, after(kTable, QStringLiteral("one | ")), CellKey::Left);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("| one")));
  // "\|" is one character to the caret.
  m = key(kTable, after(kTable, QStringLiteral("| x")), CellKey::Right);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("| x\\|")));
  m = key(kTable, after(kTable, QStringLiteral("| x\\|")), CellKey::Left);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("| x")));
  // Shift stops at the cell's edge, the anchor kept.
  const int oneEnd = after(kTable, QStringLiteral("| one"));
  m = key(kTable, oneEnd, CellKey::Right, true, oneEnd - 1);
  EXPECT_EQ(m.move.anchor, oneEnd - 1);
  EXPECT_EQ(m.move.position, oneEnd);
  // Home / End: the cell's text.
  m = key(kTable, after(kTable, QStringLiteral("| tw")), CellKey::Home);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("one | ")));
  m = key(kTable, after(kTable, QStringLiteral("| t")), CellKey::End, true);
  EXPECT_EQ(m.move.anchor, after(kTable, QStringLiteral("| t")));
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("| two")));
}

TEST(TableCells, BackspaceAndDeleteKeepThePipes) {
  // At the cell's start Backspace does nothing (it took the pipe).
  Moved m = key(kTable, after(kTable, QStringLiteral("one | ")), CellKey::Backspace);
  EXPECT_TRUE(m.move.handled);
  EXPECT_EQ(m.text, kTable);
  m = key(kTable, after(kTable, QStringLiteral("| two")), CellKey::Delete);
  EXPECT_TRUE(m.move.handled);
  EXPECT_EQ(m.text, kTable);
  // Inside the text it is the field's.
  EXPECT_FALSE(key(kTable, after(kTable, QStringLiteral("| tw")), CellKey::Backspace).move.handled);
  // An escaped "|" goes whole.
  m = key(kTable, after(kTable, QStringLiteral("| x\\|")), CellKey::Backspace);
  EXPECT_TRUE(m.move.handled);
  EXPECT_TRUE(m.text.contains(QStringLiteral("| xy |")));
  // A selection over two cells: nothing.
  m = key(kTable, after(kTable, QStringLiteral("| tw")), CellKey::Delete, false, after(kTable, QStringLiteral("| o")));
  EXPECT_TRUE(m.move.handled);
  EXPECT_EQ(m.text, kTable);
  // Inside one cell: the field's.
  EXPECT_FALSE(
      key(kTable, after(kTable, QStringLiteral("| tw")), CellKey::Delete, false, after(kTable, QStringLiteral("| t"))).move.handled);
}

TEST(TableCells, EscapeLeavesUnderTheTable) {
  Moved m = key(kTable, after(kTable, QStringLiteral("| tw")), CellKey::Escape);
  EXPECT_EQ(m.move.position, after(kTable, QStringLiteral("|  |\n")));
  EXPECT_EQ(m.text, kTable);
}

TEST(TableCells, TypingEscapesThePipe) {
  QTextDocument doc(kTable);
  const int inOne = after(kTable, QStringLiteral("| on"));
  CellMove m = typeIntoCell(&doc, inOne, inOne, QStringLiteral("|"));
  EXPECT_TRUE(m.handled);
  EXPECT_TRUE(doc.toPlainText().contains(QStringLiteral("| on\\|e | two |")));
  EXPECT_EQ(m.position, inOne + 2);
  // A pasted line break or tab: a space; the row stays one line.
  m = typeIntoCell(&doc, m.position, m.position, QStringLiteral("a\nb\tc"));
  EXPECT_TRUE(doc.toPlainText().contains(QStringLiteral("| on\\|a b ce | two |")));
  // Plain text is the field's to type; in the padding, moved into the text.
  const QString t = doc.toPlainText();
  m = typeIntoCell(&doc, after(t, QStringLiteral("| tw")), after(t, QStringLiteral("| tw")), QStringLiteral("z"));
  EXPECT_FALSE(m.handled);
  EXPECT_EQ(m.position, after(t, QStringLiteral("| tw")));
  const int padding = after(t, QStringLiteral("two"));  // the space before the pipe
  m = typeIntoCell(&doc, padding + 1, padding + 1, QStringLiteral("z"));
  EXPECT_FALSE(m.handled);
  EXPECT_EQ(m.position, padding);
  // Over two cells: nothing is typed.
  m = typeIntoCell(&doc, after(t, QStringLiteral("| o")), after(t, QStringLiteral("| tw")), QStringLiteral("z"));
  EXPECT_TRUE(m.handled);
  EXPECT_EQ(doc.toPlainText(), t);
}

// "|" at a cell's end still types over the row's pipe (TableOps), so a row
// typed out raw lands in its cells.
TEST(TableCells, APipeAtTheEndOfACellTypesOver) {
  QTextDocument doc(QStringLiteral("| A | B |\n| --- | --- |\n| build |  |"));
  const int end = after(doc.toPlainText(), QStringLiteral("| build"));
  EXPECT_FALSE(typeIntoCell(&doc, end, end, QStringLiteral("|")).handled);
  EXPECT_EQ(doc.toPlainText(), QStringLiteral("| A | B |\n| --- | --- |\n| build |  |"));
}

TEST(TableCells, EnteringFromAboveAndBelow) {
  QTextDocument doc(kTable);
  CellMove m = enterTable(&doc, after(kTable, QStringLiteral("Intro\n")), true);
  EXPECT_TRUE(m.handled);
  EXPECT_EQ(m.position, after(kTable, QStringLiteral("| ")));
  m = enterTable(&doc, after(kTable, QStringLiteral("|  |\n")), false);
  EXPECT_EQ(m.position, after(kTable, QStringLiteral("| x")) - 1);
  EXPECT_FALSE(enterTable(&doc, 0, true).handled) << "Intro: the line under it is blank, not the table";
}

TEST(TableCells, AddAColumnAndARowAtTheEdge) {
  QTextDocument doc(QStringLiteral("| A | B |\n| --- | ---: |\n| 1 | 2 |"));
  int caret = addTableColumn(&doc, 0);
  EXPECT_EQ(doc.toPlainText(), QStringLiteral("| A | B |  |\n| --- | ---: | --- |\n| 1 | 2 |  |"));
  EXPECT_EQ(caret, after(doc.toPlainText(), QStringLiteral("| B | ")));
  caret = addTableRow(&doc, 0);
  EXPECT_EQ(doc.toPlainText(), QStringLiteral("| A | B |  |\n| --- | ---: | --- |\n| 1 | 2 |  |\n|  |  |  |"));
  EXPECT_EQ(caret, doc.toPlainText().lastIndexOf(QLatin1Char('\n')) + 3);
  EXPECT_EQ(addTableRow(&doc, doc.findBlockByNumber(2).position()), -1) << "not a table's start";
}
