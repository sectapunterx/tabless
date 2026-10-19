// Rows and columns of a markdown table, added and removed at the caret
// (dogfood 2026-10-19). Each case checks the text and where the caret lands.

#include "markdown/TableOps.h"

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
  r = run(QStringLiteral("x\n| A |\n| --- |"), QStringLiteral("| A"), TableOp::DeleteColumn);
  EXPECT_EQ(r.text, QStringLiteral("x"));
  EXPECT_EQ(r.caret, 1);
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
