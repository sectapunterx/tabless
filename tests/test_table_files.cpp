#include "markdown/LogSyntax.h"
#include "markdown/TableOps.h"
#include "markdown/TableText.h"
#include "markdown/Xlsx.h"

#include <QtEndian>

#include <gtest/gtest.h>

using namespace heap::md;

TEST(TableTextTest, ReadsCsvAsRfc4180Does) {
  const auto rows = parseDelimited(QStringLiteral("a,b,c\r\n1,\"two, too\",\"say \"\"hi\"\"\"\n\"multi\nline\",,x\n\n"), QLatin1Char(','));
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows.at(0), (QStringList{"a", "b", "c"}));
  EXPECT_EQ(rows.at(1), (QStringList{"1", "two, too", "say \"hi\""}));
  EXPECT_EQ(rows.at(2), (QStringList{"multi\nline", "", "x"}));
  EXPECT_TRUE(parseDelimited(QString(), QLatin1Char(',')).isEmpty());
  EXPECT_EQ(parseDelimited(QStringLiteral("a\tb"), QLatin1Char('\t')).first(), (QStringList{"a", "b"}));
}

TEST(TableTextTest, GuessesTheSeparator) {
  EXPECT_EQ(guessSeparator(QStringLiteral("a;b;c\n1;2,5;3\n")), QLatin1Char(';')) << "a decimal comma is not the separator";
  EXPECT_EQ(guessSeparator(QStringLiteral("a,b\n1,2\n")), QLatin1Char(','));
  EXPECT_EQ(guessSeparator(QStringLiteral("a\tb\n1\t2\n")), QLatin1Char('\t'));
  EXPECT_EQ(guessSeparator(QStringLiteral("\"x;y\",b\n1,2\n")), QLatin1Char(',')) << "inside quotes does not count";
  EXPECT_EQ(separatorForLanguage(QStringLiteral("TSV"), QString()), QLatin1Char('\t'));
  EXPECT_TRUE(separatorForLanguage(QStringLiteral("js"), QStringLiteral("a,b")).isNull());
}

TEST(TableTextTest, MakesAMarkdownTable) {
  const QList<QStringList> rows{{"name", "note"}, {"a|b", "two\nlines"}, {"short"}};
  EXPECT_EQ(markdownTable(rows), QStringLiteral("| name | note |\n| --- | --- |\n| a\\|b | two lines |\n| short |  |"));
  EXPECT_EQ(markdownTable(rows, 1), QStringLiteral("| name | note |\n| --- | --- |\n| a\\|b | two lines |"));
  EXPECT_EQ(markdownTable({{"abcdef"}}, 0, 3), QStringLiteral("| abc… |\n| --- |"));
}

// Review 2026-10-19: a cell that already holds "\|" must not come out as
// "\\|" (an escaped backslash, then a pipe that splits the row in GFM).
TEST(TableTextTest, AnEscapedPipeStaysOneCell) {
  const QList<QStringList> rows{{"h1", "h2"}, {"a\\|b", "c"}, {"x\\\\|y", "z"}, {"p|q", "r"}};
  const QString md = markdownTable(rows);
  const QStringList lines = md.split(QLatin1Char('\n'));
  ASSERT_EQ(lines.size(), 5);
  EXPECT_EQ(lines.at(2), QStringLiteral("| a\\|b | c |"));
  EXPECT_EQ(lines.at(3), QStringLiteral("| x\\\\\\|y | z |")) << "an even run of backslashes still gets its pipe escaped";
  EXPECT_EQ(lines.at(4), QStringLiteral("| p\\|q | r |"));
  // Read back, every row is two cells.
  for(qsizetype k = 2; k < lines.size(); ++k) {
    EXPECT_EQ(tableCells(lines.at(k)).size(), 2) << lines.at(k).toStdString();
  }
  // And a second pass over its own output changes nothing.
  QList<QStringList> again;
  for(const QString& l : lines) {
    if(!l.contains(QLatin1String("---"))) {
      again << tableCells(l);
    }
  }
  EXPECT_EQ(markdownTable(again), md);
}

TEST(LogSyntaxTest, ReadsLevelsTimesAndPaths) {
  const QString line = QStringLiteral("2026-10-14 21:05:03.123 ERROR src/lte/model.cc:754 \"not found\" WARN +0.10s INFO");
  QList<LogKind> kinds;
  QStringList texts;
  for(const LogSpan& s : logSpans(line)) {
    kinds << s.kind;
    texts << line.mid(s.start, s.length);
  }
  EXPECT_EQ(texts, (QStringList{"2026-10-14 21:05:03.123", "ERROR", "src/lte/model.cc:754", "\"not found\"", "WARN", "+0.10s", "INFO"}));
  EXPECT_EQ(kinds,
            (QList<LogKind>{LogKind::Time, LogKind::Error, LogKind::Path, LogKind::Quoted, LogKind::Warn, LogKind::Time, LogKind::Quiet}));
  EXPECT_TRUE(isLogLanguage(QStringLiteral("Log")));
  EXPECT_FALSE(isLogLanguage(QStringLiteral("logic")));
}

namespace {

// A zip of `files`, every other one deflated: what Excel writes, in small.
QByteArray makeZip(const QList<QPair<QString, QByteArray>>& files) {
  QByteArray out;
  QByteArray directory;
  const auto le16 = [](QByteArray* b, quint16 v) {
    char buf[2];
    qToLittleEndian(v, buf);
    b->append(buf, 2);
  };
  const auto le32 = [](QByteArray* b, quint32 v) {
    char buf[4];
    qToLittleEndian(v, buf);
    b->append(buf, 4);
  };
  int i = 0;
  for(const auto& [name, data] : files) {
    const bool deflate = (i++ % 2) == 1;
    // qCompress: 4-byte size, a 2-byte zlib header, the raw deflate, a 4-byte checksum.
    const QByteArray packed = deflate ? qCompress(data).mid(6).chopped(4) : data;
    const QByteArray n = name.toUtf8();
    const auto offset = static_cast<quint32>(out.size());
    le32(&out, 0x04034b50U);
    le16(&out, 20);
    le16(&out, 0);
    le16(&out, deflate ? 8 : 0);
    le32(&out, 0);
    le32(&out, 0);  // crc: not checked
    le32(&out, static_cast<quint32>(packed.size()));
    le32(&out, static_cast<quint32>(data.size()));
    le16(&out, static_cast<quint16>(n.size()));
    le16(&out, 0);
    out += n + packed;
    le32(&directory, 0x02014b50U);
    le16(&directory, 20);
    le16(&directory, 20);
    le16(&directory, 0);
    le16(&directory, deflate ? 8 : 0);
    le32(&directory, 0);
    le32(&directory, 0);
    le32(&directory, static_cast<quint32>(packed.size()));
    le32(&directory, static_cast<quint32>(data.size()));
    le16(&directory, static_cast<quint16>(n.size()));
    le16(&directory, 0);
    le16(&directory, 0);
    le16(&directory, 0);
    le16(&directory, 0);
    le32(&directory, 0);
    le32(&directory, offset);
    directory += n;
  }
  const auto dirOffset = static_cast<quint32>(out.size());
  out += directory;
  le32(&out, 0x06054b50U);
  le16(&out, 0);
  le16(&out, 0);
  le16(&out, static_cast<quint16>(files.size()));
  le16(&out, static_cast<quint16>(files.size()));
  le32(&out, static_cast<quint32>(directory.size()));
  le32(&out, dirOffset);
  le16(&out, 0);
  return out;
}

}  // namespace

TEST(XlsxTest, ReadsSheetsAndCells) {
  const QByteArray workbook =
      R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>)"
      R"(<sheet name="Числа" sheetId="1" r:id="rId1"/><sheet name="Second" sheetId="2" r:id="rId2"/></sheets></workbook>)";
  const QByteArray rels = R"(<Relationships><Relationship Id="rId1" Target="worksheets/sheet1.xml"/>)"
                          R"(<Relationship Id="rId2" Target="/xl/worksheets/sheet2.xml"/></Relationships>)";
  const QByteArray shared =
      R"(<sst><si><t>name</t></si><si><r><t>Ал</t></r><r><t>ёна</t></r></si><si><t>x</t><rPh><t>skip</t></rPh></si></sst>)";
  const QByteArray styles =
      R"(<styleSheet><numFmts><numFmt numFmtId="164" formatCode="d\.mmm;@"/></numFmts>)"
      R"(<cellXfs><xf numFmtId="0"/><xf numFmtId="14"/><xf numFmtId="164"/><xf numFmtId="2"/></cellXfs></styleSheet>)";
  const QByteArray sheet1 = R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="C1" t="s"><v>2</v></c>)"
                            R"(<c r="D1" s="1"><v>45698</v></c><c r="E1" s="2"><v>45698.5</v></c><c r="F1" s="3"><v>12.5</v></c></row>)"
                            R"(<row r="3"><c r="A3" t="s"><v>1</v></c><c r="B3"><v>42.5</v></c><c r="C3" t="b"><v>1</v></c></row>)"
                            R"(</sheetData></worksheet>)";
  const QByteArray sheet2 = R"(<worksheet><sheetData><row><c t="inlineStr"><is><t>inline</t></is></c></row></sheetData></worksheet>)";
  const QByteArray zip = makeZip({{"xl/workbook.xml", workbook},
                                  {"xl/_rels/workbook.xml.rels", rels},
                                  {"xl/sharedStrings.xml", shared},
                                  {"xl/styles.xml", styles},
                                  {"xl/worksheets/sheet1.xml", sheet1},
                                  {"xl/worksheets/sheet2.xml", sheet2}});
  QString error;
  const auto sheets = readXlsx(zip, 0, &error);
  ASSERT_EQ(sheets.size(), 2) << error.toStdString();
  EXPECT_EQ(sheets.at(0).name, QStringLiteral("Числа"));
  ASSERT_EQ(sheets.at(0).rows.size(), 3);
  EXPECT_EQ(sheets.at(0).rows.at(0), (QStringList{"name", "", "x", "2025-02-10", "2025-02-10 12:00", "12.5"})) << "dates by their style";
  EXPECT_EQ(sheets.at(0).rows.at(1), (QStringList{"", "", "", "", "", ""})) << "an empty row keeps its place";
  EXPECT_EQ(sheets.at(0).rows.at(2), (QStringList{"Алёна", "42.5", "TRUE", "", "", ""}));
  EXPECT_EQ(sheets.at(1).rows, (QList<QStringList>{{"inline"}}));
  EXPECT_EQ(readXlsx(zip, 1).at(0).rows.size(), 1) << "maxRows";
  EXPECT_TRUE(readXlsx(QByteArray("not a zip"), 0, &error).isEmpty());
  EXPECT_FALSE(error.isEmpty());
  EXPECT_EQ(xlsxColumn(QStringLiteral("AA10")), 26);
}
