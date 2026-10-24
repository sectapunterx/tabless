#include "markdown/TableImport.h"

#include <gtest/gtest.h>

using namespace heap::md;

// Dogfood 2026-10-23 #9: a table in a .txt / .log, its separator found.

TEST(TableImportTest, FindsAPipeTableInALog) {
  const QString log = QStringLiteral(
      "L2 scheduler trace, build 4411\n"
      "\n"
      "slot | ue | cce | agg\n"
      "1204 | 17 | 8 | 4\n"
      "1204 | 18 | 12 | 2\n"
      "-- end --\n");
  const TextTable t = parseTextTable(log);
  EXPECT_EQ(t.separator, TextSeparator::Pipe);
  ASSERT_TRUE(t.isTable());
  EXPECT_EQ(t.columns, 4);
  EXPECT_EQ(t.rows.size(), 3);
  EXPECT_EQ(t.rows.at(1), (QStringList{"1204", "17", "8", "4"}));
  EXPECT_EQ(t.skipped, (QStringList{"L2 scheduler trace, build 4411", "-- end --"}));
  EXPECT_EQ(t.lines, 5);
  EXPECT_EQ(t.matched, 3);
}

TEST(TableImportTest, ReadsAMarkdownTableAsItIs) {
  const TextTable t = parseTextTable(QStringLiteral("| a | b \\| c |\n|---|:--:|\n| 1 | 2 |\n"));
  EXPECT_TRUE(t.markdown);
  EXPECT_EQ(t.separator, TextSeparator::Pipe);
  ASSERT_EQ(t.rows.size(), 2);
  EXPECT_EQ(t.rows.at(0), (QStringList{"a", "b | c"}));
  EXPECT_EQ(t.rows.at(1), (QStringList{"1", "2"}));
}

TEST(TableImportTest, PrefersTheSteadySeparator) {
  // Commas in prose, semicolons as the columns.
  const QString text = QStringLiteral("name;value;note\nalpha;1;one, two\nbeta;2;three\ngamma;3;four, five, six\n");
  EXPECT_EQ(parseTextTable(text).separator, TextSeparator::Semicolon);
  EXPECT_EQ(parseTextTable(QStringLiteral("a\tb\n1\t2\n")).separator, TextSeparator::Tab);
}

TEST(TableImportTest, ColumnsAlignedWithSpaces) {
  const QString ps = QStringLiteral("PID   NAME        CPU\n412   heap.exe    3.1\n77    svchost     0.2\n");
  const TextTable t = parseTextTable(ps);
  EXPECT_EQ(t.separator, TextSeparator::Spaces);
  ASSERT_EQ(t.rows.size(), 3);
  EXPECT_EQ(t.rows.at(1), (QStringList{"412", "heap.exe", "3.1"}));
}

TEST(TableImportTest, AChosenSeparatorWins) {
  TextTableOptions o;
  o.separator = TextSeparator::Custom;
  o.custom = QLatin1Char('#');
  const TextTable t = parseTextTable(QStringLiteral("a#b#c\n1#2#3\n"), o);
  EXPECT_EQ(t.separator, TextSeparator::Custom);
  EXPECT_EQ(t.rows.at(1), (QStringList{"1", "2", "3"}));
}

TEST(TableImportTest, CollapsesRepeatsAndKeepsShortRowsPadded) {
  TextTableOptions o;
  o.separator = TextSeparator::Semicolon;
  o.collapseRepeats = true;
  const TextTable t = parseTextTable(QStringLiteral("a;;b;c\n1;2\n"), o);
  ASSERT_EQ(t.rows.size(), 2);
  EXPECT_EQ(t.rows.at(0), (QStringList{"a", "b", "c"}));
  EXPECT_EQ(t.rows.at(1), (QStringList{"1", "2", ""}));
}

TEST(TableImportTest, KeepsEveryRow) {
  QString text;
  for(int i = 0; i < 2500; ++i) {
    text += QStringLiteral("%1\t%2\n").arg(i).arg(i * 2);
  }
  const TextTable t = parseTextTable(text);
  EXPECT_EQ(t.rows.size(), 2500) << "no cut at 500 rows: one table, every row";
}

TEST(TableImportTest, PlainProseIsNoTable) {
  const TextTable t = parseTextTable(QStringLiteral("Just a note.\nNothing tabular here at all.\n"));
  EXPECT_FALSE(t.isTable());
}

TEST(TableImportTest, NamesColumnsLikeASpreadsheet) {
  EXPECT_EQ(letterHeader(3), (QStringList{"A", "B", "C"}));
  EXPECT_EQ(letterHeader(28).mid(25), (QStringList{"Z", "AA", "AB"}));
  EXPECT_EQ(textSeparatorFromName(QStringLiteral("pipe")), TextSeparator::Pipe);
  EXPECT_EQ(textSeparatorName(TextSeparator::Spaces), QStringLiteral("spaces"));
}
