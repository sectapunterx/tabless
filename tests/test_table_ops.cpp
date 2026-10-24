// Rows and columns of a markdown table, added and removed at the caret
// (dogfood 2026-10-19). Each case checks the text and where the caret lands.

#include "markdown/TableOps.h"

#include <QTextCursor>
#include <QTextDocument>

#include <gtest/gtest.h>

using namespace heap::md;

namespace {

const QString kTable = QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n| 1 | 2 |\n| 3 | 4 |\n\nAfter");

struct Result {
  QString text;
  int caret = 0;
  bool applied = false;
};

// The op with the caret after the first `needle`.
Result run(const QString& text, const QString& needle, TableOp op) {
  QTextDocument doc(text);
  const int at = static_cast<int>(text.indexOf(needle) + needle.size());
  Result r;
  r.caret = applyTableOp(&doc, Selection::at(at), op, &r.applied).caret();
  r.text = doc.toPlainText();
  return r;
}

// What sits from the caret to the end of its cell.
QString cellAt(const Result& r) {
  const qsizetype end = r.text.indexOf(QLatin1Char('|'), r.caret);
  return r.text.mid(r.caret, end - r.caret).trimmed();
}

}  // namespace

TEST(TableOps, Cells) {
  EXPECT_EQ(tableCells(QStringLiteral("| a | b\\|c |")), (QStringList{"a", "b\\|c"}));
  EXPECT_EQ(tableCells(QStringLiteral("|  |  |")), (QStringList{"", ""}));
}

TEST(TableOps, Locate) {
  const QStringList lines = kTable.split(QLatin1Char('\n'));
  const TableLocation loc = locateTable(lines, 4, 6);  // "| 1 | 2 |", in the 2
  ASSERT_TRUE(loc.ok);
  EXPECT_EQ(loc.firstLine, 2);
  EXPECT_EQ(loc.lastLine, 5);
  EXPECT_EQ(loc.row, 2);
  EXPECT_EQ(loc.column, 1);
  EXPECT_FALSE(locateTable(lines, 0, 0).ok);
  // Pipes without a |---| line are not a table.
  EXPECT_FALSE(locateTable(QStringList{"| a |", "| b |"}, 0, 2).ok);
}

TEST(TableOps, RowBelowAndAbove) {
  Result r = run(kTable, QStringLiteral("| 1"), TableOp::RowBelow);
  ASSERT_TRUE(r.applied);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n| 1 | 2 |\n|  |  |\n| 3 | 4 |\n\nAfter"));
  EXPECT_EQ(r.text.mid(r.text.lastIndexOf(QLatin1Char('\n'), r.caret) + 1, 2), QStringLiteral("| "));
  EXPECT_EQ(r.text.indexOf(QStringLiteral("|  |  |")) + 2, r.caret);  // first cell of the new row

  r = run(kTable, QStringLiteral("| 3 | 4"), TableOp::RowAbove);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n| 1 | 2 |\n|  |  |\n| 3 | 4 |\n\nAfter"));
  EXPECT_EQ(r.text.indexOf(QStringLiteral("|  |  |")) + 5, r.caret);  // its second cell
}

TEST(TableOps, RowAboveTheHeaderMakesANewHeader) {
  const Result r = run(kTable, QStringLiteral("| A"), TableOp::RowAbove);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n|  |  |\n| :--- | ---: |\n| A | B |\n| 1 | 2 |\n| 3 | 4 |\n\nAfter"));
  EXPECT_EQ(r.caret, r.text.indexOf(QStringLiteral("|  |  |")) + 2);
}

TEST(TableOps, RowBelowTheHeaderOrItsRule) {
  for(const QString& at : {QStringLiteral("| A"), QStringLiteral("| :-")}) {
    const Result r = run(kTable, at, TableOp::RowBelow);
    EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n|  |  |\n| 1 | 2 |\n| 3 | 4 |\n\nAfter"));
  }
}

TEST(TableOps, Columns) {
  Result r = run(kTable, QStringLiteral("| 1 | 2"), TableOp::ColumnRight);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |  |\n| :--- | ---: | --- |\n| 1 | 2 |  |\n| 3 | 4 |  |\n\nAfter"));
  EXPECT_EQ(cellAt(r), QString());
  r = run(kTable, QStringLiteral("| 1"), TableOp::ColumnLeft);
  // Alignment stays with its column.
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n|  | A | B |\n| --- | :--- | ---: |\n|  | 1 | 2 |\n|  | 3 | 4 |\n\nAfter"));
  r = run(kTable, QStringLiteral("| A"), TableOp::DeleteColumn);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| B |\n| ---: |\n| 2 |\n| 4 |\n\nAfter"));
  EXPECT_EQ(cellAt(r), QStringLiteral("B"));
}

TEST(TableOps, DeleteRows) {
  Result r = run(kTable, QStringLiteral("| 3"), TableOp::DeleteRow);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n| 1 | 2 |\n\nAfter"));
  EXPECT_EQ(cellAt(r), QStringLiteral("1"));
  // The header's row: the first row takes its place.
  r = run(kTable, QStringLiteral("| A"), TableOp::DeleteRow);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| 1 | 2 |\n| :--- | ---: |\n| 3 | 4 |\n\nAfter"));
  // The last row of all: the table goes, the text around it stays.
  r = run(QStringLiteral("x\n| A |\n| --- |\ny"), QStringLiteral("| A"), TableOp::DeleteRow);
  EXPECT_EQ(r.text, QStringLiteral("x\ny"));
}

// Review 2026-10-19: the last column is not "the table" — deleting it does
// nothing rather than wipe every row's text.
TEST(TableOps, DeleteTheLastColumnDoesNothing) {
  const QString oneColumn = QStringLiteral("x\n| A |\n| --- |\n| keep me |");
  const Result r = run(oneColumn, QStringLiteral("| ke"), TableOp::DeleteColumn);
  EXPECT_FALSE(r.applied);
  EXPECT_EQ(r.text, oneColumn);
}

// The |---| line is not a row: Delete row there must not put the first body
// row in the header's place.
TEST(TableOps, DeleteRowOnTheRuleDoesNothing) {
  const Result r = run(kTable, QStringLiteral("| :-"), TableOp::DeleteRow);
  EXPECT_FALSE(r.applied);
  EXPECT_EQ(r.text, kTable);
  EXPECT_FALSE(editTable(QStringList{"| A |", "| --- |", "| 1 |"}, 1, 0, TableOp::DeleteRow).ok);
}

// A table inside a list item keeps the item's indent on every line.
TEST(TableOps, IndentedTableKeepsItsIndent) {
  const QString text = QStringLiteral("- item\n\n  | A | B |\n  | --- | --- |\n  | 1 | 2 |\n");
  const Result r = run(text, QStringLiteral("| 1"), TableOp::RowBelow);
  ASSERT_TRUE(r.applied);
  EXPECT_EQ(r.text, QStringLiteral("- item\n\n  | A | B |\n  | --- | --- |\n  | 1 | 2 |\n  |  |  |\n"));
  EXPECT_EQ(r.caret, r.text.indexOf(QStringLiteral("  |  |  |")) + 4);  // first cell of the new row
  const Result c = run(text, QStringLiteral("| 1 | 2"), TableOp::DeleteColumn);
  EXPECT_EQ(c.text, QStringLiteral("- item\n\n  | A |\n  | --- |\n  | 1 |\n"));
}

// A |…| example inside a code fence is code: no op touches it, and no table
// anchor (the "/" table items, the context menu) is offered there.
TEST(TableOps, NothingInsideACodeFence) {
  const QString text = QStringLiteral("```\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\n```\nafter");
  const Result r = run(text, QStringLiteral("| 1"), TableOp::RowBelow);
  EXPECT_FALSE(r.applied);
  EXPECT_EQ(r.text, text);
  QTextDocument doc(text);
  EXPECT_EQ(tableAnchor(&doc, static_cast<int>(text.indexOf(QStringLiteral("| 1")) + 2)), -1);
  EXPECT_EQ(tableAnchor(&doc, static_cast<int>(text.indexOf(QStringLiteral("\n\n```")) + 1)), -1);
  // The same table after the fence closes is a table again.
  const QString after = text + QStringLiteral("\n| X |\n| --- |\n| 9 |");
  EXPECT_TRUE(run(after, QStringLiteral("| 9"), TableOp::RowBelow).applied);
}

TEST(TableOps, RaggedRowsComeOutEven) {
  const Result r = run(QStringLiteral("| a | b | c |\n|---|---|\n| 1 |"), QStringLiteral("| 1"), TableOp::RowBelow);
  EXPECT_EQ(r.text, QStringLiteral("| a | b | c |\n| --- | --- | --- |\n| 1 |  |  |\n|  |  |  |"));
}

TEST(TableOps, OutsideATableNothingHappens) {
  const Result r = run(kTable, QStringLiteral("Intro"), TableOp::RowBelow);
  EXPECT_FALSE(r.applied);
  EXPECT_EQ(r.text, kTable);
  EXPECT_FALSE(tableOpFromName(QStringLiteral("bogus")).has_value());
  EXPECT_EQ(tableOpFromName(QStringLiteral("deleteColumn")), TableOp::DeleteColumn);
}

TEST(TableOps, OneUndoStep) {
  QTextDocument doc(kTable);
  bool applied = false;
  applyTableOp(&doc, Selection::at(static_cast<int>(kTable.indexOf(QStringLiteral("| 1")) + 2)), TableOp::ColumnRight, &applied);
  ASSERT_TRUE(applied);
  doc.undo();
  EXPECT_EQ(doc.toPlainText(), kTable);
}

TEST(TableOps, AnchorUnderATable) {
  QTextDocument doc(kTable);
  const int inside = static_cast<int>(kTable.indexOf(QStringLiteral("| 3")) + 2);
  EXPECT_EQ(tableAnchor(&doc, inside), inside);
  const int blankUnder = static_cast<int>(kTable.indexOf(QStringLiteral("\n\nAfter")) + 1);
  EXPECT_EQ(tableAnchor(&doc, blankUnder), static_cast<int>(kTable.indexOf(QStringLiteral("| 3"))) + 1);
  EXPECT_EQ(tableAnchor(&doc, 0), -1);
  EXPECT_EQ(tableAnchor(&doc, static_cast<int>(kTable.size())), -1);
}

// ── Moving through a table from the keyboard (dogfood 2026-10-20) ──────
// Enter broke the row's line in two, which tore the table and hid the rows
// under the tear; now Enter, Tab and Shift+Tab move through the cells.

namespace {

Result move(const QString& text, const QString& needle, TableMove m) {
  QTextDocument doc(text);
  const int at = static_cast<int>(text.indexOf(needle) + needle.size());
  Result r;
  r.caret = applyTableMove(&doc, Selection::at(at), m, &r.applied).caret();
  r.text = doc.toPlainText();
  return r;
}

// The cell's text the caret is in or at the end of.
QString cellOf(const Result& r) {
  const qsizetype left = r.text.lastIndexOf(QLatin1Char('|'), r.caret - 1);
  const qsizetype right = r.text.indexOf(QLatin1Char('|'), r.caret);
  return r.text.mid(left + 1, right - left - 1).trimmed();
}

}  // namespace

TEST(TableMove, EnterGoesToTheSameColumnARowDown) {
  Result r = move(kTable, QStringLiteral("| 1 | 2"), TableMove::NextRow);
  ASSERT_TRUE(r.applied);
  EXPECT_EQ(r.text, kTable) << "a move edits nothing";
  EXPECT_EQ(cellOf(r), QStringLiteral("4"));
  EXPECT_EQ(r.text.at(r.caret - 1), QLatin1Char('4')) << "at the end of the cell's text";
  // From the header, over the |---| line, to the first row.
  r = move(kTable, QStringLiteral("| A"), TableMove::NextRow);
  EXPECT_EQ(cellOf(r), QStringLiteral("1"));
}

TEST(TableMove, EnterOnTheLastRowAddsARow) {
  const Result r = move(kTable, QStringLiteral("| 3 | 4"), TableMove::NextRow);
  ASSERT_TRUE(r.applied);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n| 1 | 2 |\n| 3 | 4 |\n|  |  |\n\nAfter"));
  EXPECT_EQ(r.caret, r.text.indexOf(QStringLiteral("|  |  |")) + 5) << "the new row, the same (second) column";
}

TEST(TableMove, EnterOnAnEmptyLastRowLeavesTheTable) {
  const QString text = QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |\n|  |  |\nAfter");
  const Result r = move(text, QStringLiteral("|  |"), TableMove::NextRow);
  ASSERT_TRUE(r.applied);
  EXPECT_EQ(r.text, QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |\n\n\n\nAfter"));
  EXPECT_EQ(r.caret, r.text.indexOf(QStringLiteral("| 1 | 2 |")) + 11) << "on a line of its own, a blank line on each side";
  // The only row of a table stays: Enter there adds one.
  const Result only = move(QStringLiteral("| A |\n| --- |\n|  |"), QStringLiteral("|  "), TableMove::NextRow);
  EXPECT_EQ(only.text, QStringLiteral("| A |\n| --- |\n|  |\n|  |"));
}

TEST(TableMove, TabWalksTheCellsAndAddsARowAtTheEnd) {
  Result r = move(kTable, QStringLiteral("| 1"), TableMove::NextCell);
  EXPECT_EQ(cellOf(r), QStringLiteral("2"));
  r = move(kTable, QStringLiteral("| 1 | 2"), TableMove::NextCell);
  EXPECT_EQ(cellOf(r), QStringLiteral("3")) << "the last cell of a row: the next row's first";
  r = move(kTable, QStringLiteral("| 3 | 4"), TableMove::NextCell);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n| 1 | 2 |\n| 3 | 4 |\n|  |  |\n\nAfter"));
  EXPECT_EQ(r.caret, r.text.indexOf(QStringLiteral("|  |  |")) + 2) << "the new row's first cell";
  r = move(kTable, QStringLiteral("| 3"), TableMove::PreviousCell);
  EXPECT_EQ(cellOf(r), QStringLiteral("2"));
  r = move(kTable, QStringLiteral("| 1"), TableMove::PreviousCell);
  EXPECT_EQ(cellOf(r), QStringLiteral("B")) << "over the |---| line to the header";
  r = move(kTable, QStringLiteral("| A"), TableMove::PreviousCell);
  ASSERT_TRUE(r.applied);
  EXPECT_EQ(cellOf(r), QStringLiteral("A")) << "the first cell stays";
}

TEST(TableMove, AddRowAndExit) {
  Result r = move(kTable, QStringLiteral("| 1"), TableMove::AddRow);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n| 1 | 2 |\n|  |  |\n| 3 | 4 |\n\nAfter"));
  r = move(kTable, QStringLiteral("| 1"), TableMove::Exit);
  EXPECT_EQ(r.text, QStringLiteral("Intro\n\n| A | B |\n| :--- | ---: |\n| 1 | 2 |\n| 3 | 4 |\n\n\n\nAfter"));
  EXPECT_EQ(r.caret, r.text.indexOf(QStringLiteral("| 3 | 4 |")) + 11);
  // At the end of the text: a blank line, then the caret.
  r = move(QStringLiteral("| A |\n| --- |\n| 1 |"), QStringLiteral("| 1"), TableMove::Exit);
  EXPECT_EQ(r.text, QStringLiteral("| A |\n| --- |\n| 1 |\n\n"));
  EXPECT_EQ(r.caret, r.text.size());
}

TEST(TableMove, OutsideATableItIsNotAMove) {
  for(const TableMove m : {TableMove::NextRow, TableMove::NextCell, TableMove::PreviousCell, TableMove::AddRow, TableMove::Exit}) {
    EXPECT_FALSE(move(kTable, QStringLiteral("Intro"), m).applied);
    EXPECT_FALSE(move(QStringLiteral("| a |\n| b |"), QStringLiteral("| a"), m).applied) << "pipes with no |---| line";
    EXPECT_FALSE(move(QStringLiteral("```\n| A |\n| --- |\n| 1 |\n```"), QStringLiteral("| 1"), m).applied);
  }
}

TEST(TableMove, OneUndoStep) {
  QTextDocument doc(kTable);
  bool applied = false;
  applyTableMove(&doc, Selection::at(static_cast<int>(kTable.indexOf(QStringLiteral("| 3 | 4")) + 7)), TableMove::NextRow, &applied);
  ASSERT_TRUE(applied);
  ASSERT_NE(doc.toPlainText(), kTable);
  doc.undo();
  EXPECT_EQ(doc.toPlainText(), kTable);
}

// ── "|" types over the row's own pipes (dogfood 2026-10-20) ───────────

namespace {

// Types `keys` into `doc` one character at a time, as the editor does: Enter
// moves through the table (or breaks the line), "|" may type over a pipe.
int typeInto(QTextDocument& doc, int caret, const QString& keys) {
  for(const QChar c : keys) {
    if(c == QLatin1Char('\n')) {
      bool applied = false;
      const Selection s = applyTableMove(&doc, Selection::at(caret), TableMove::NextRow, &applied);
      if(applied) {
        caret = s.caret();
        continue;
      }
    } else if(c == QLatin1Char('|')) {
      bool applied = false;
      const Selection s = applyPipeTypeOver(&doc, Selection::at(caret), &applied);
      if(applied) {
        caret = s.caret();
        continue;
      }
    }
    QTextCursor cursor(&doc);
    cursor.setPosition(caret);
    cursor.insertText(QString(c));
    caret = cursor.position();
  }
  return caret;
}

}  // namespace

TEST(PipeTypeOver, TheRawTableTypedCharByCharStaysTwoColumns) {
  QTextDocument doc;
  const QString keys = QStringLiteral("| Name | Value |\n| --- | --- |\n| build | me |\n| a \\| b | c |\n");
  typeInto(doc, 0, keys);
  const QStringList lines = doc.toPlainText().split(QLatin1Char('\n'));
  ASSERT_EQ(lines.size(), 5) << doc.toPlainText().toStdString();
  EXPECT_EQ(tableCells(lines.at(0)), (QStringList{QStringLiteral("Name"), QStringLiteral("Value")}));
  EXPECT_EQ(lines.at(2), QStringLiteral("| build | me |"));
  // An escaped pipe stays inside its cell.
  EXPECT_EQ(tableCells(lines.at(3)), (QStringList{QStringLiteral("a \\| b"), QStringLiteral("c")}));
  // Enter after the last typed row: the next, empty row.
  EXPECT_EQ(tableCells(lines.at(4)), (QStringList{QString(), QString()}));
}

TEST(PipeTypeOver, Rules) {
  const QStringList t{QStringLiteral("| A | B |"), QStringLiteral("| --- | --- |"), QStringLiteral("|  |  |"), QStringLiteral("| x |  |")};
  // The first cell, empty: swallowed in place.
  EXPECT_EQ(pipeTypeOver(t, 2, 1), 1);
  EXPECT_EQ(pipeTypeOver(t, 2, 2), 2);
  // Spaces up to the next pipe: past it.
  EXPECT_EQ(pipeTypeOver(t, 3, 4), 5);
  EXPECT_EQ(pipeTypeOver(t, 3, 6), 8);
  // Text before the next pipe, the end of the row, the |---| line, a "\":
  // typed as usual.
  EXPECT_EQ(pipeTypeOver(t, 3, 2), -1);
  EXPECT_EQ(pipeTypeOver(t, 3, 8), -1);
  EXPECT_EQ(pipeTypeOver(t, 1, 5), -1);
  EXPECT_EQ(pipeTypeOver({QStringLiteral("| A |"), QStringLiteral("| --- |"), QStringLiteral("| x\\ |")}, 2, 4), -1);
  // No table (no |---| line): nothing to type over.
  EXPECT_EQ(pipeTypeOver({QStringLiteral("|  |  |")}, 0, 2), -1);
}

TEST(PipeTypeOver, NotWithASelectionOrInAFence) {
  QTextDocument doc(QStringLiteral("| A | B |\n| --- | --- |\n|  |  |"));
  const int at = static_cast<int>(doc.toPlainText().lastIndexOf(QStringLiteral("|  |")) + 2);
  bool applied = false;
  applyPipeTypeOver(&doc, Selection{at - 1, at}, &applied);
  EXPECT_FALSE(applied);
  applyPipeTypeOver(&doc, Selection::at(at), &applied);
  EXPECT_TRUE(applied);
  QTextDocument fenced(QStringLiteral("```\n| A | B |\n| --- | --- |\n|  |  |\n```"));
  applyPipeTypeOver(&fenced, Selection::at(static_cast<int>(fenced.toPlainText().lastIndexOf(QStringLiteral("|  |")) + 2)), &applied);
  EXPECT_FALSE(applied);
}
