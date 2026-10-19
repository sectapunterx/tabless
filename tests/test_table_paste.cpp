// Pasting a table into a note (dogfood 2026-10-19): HTML, TSV and the glued
// text a spec page with TeX formulas writes to the clipboard.

#include "markdown/MdMath.h"
#include "markdown/TablePaste.h"

#include <gtest/gtest.h>

using namespace heap::md;

namespace {

QStringList lines(const QString& s) {
  return s.split(QLatin1Char('\n'));
}

// What the owner pasted: the plain text of a 3GPP-style parameter table,
// formulas kept as $…$ and the cells run together.
const char* const kSpecSample =
    "Name\n"
    "    \n"
    "    \n"
    "\n"
    "FormatUnitRangeDimensionsSourceDescription$\\mathrm{I}^{\\mathrm{~UeEid}}$$\\textsf{integer}$$\\left[0,...,\\infty\\right]$$1$"
    "ID16218Ue identifier.$\\mathrm{P}^\\mathrm{~AnValue}$$\\textsf{enum}$$\\left[\\textsf{ACK},\n"
    "\\textsf{NACK},\n"
    "\\textsf{DTX}\\right]$$1$ID16218AN value from DL HARQ report.$\\mathrm{F}^\\mathrm{~NewTx}$$\\textsf{boolean}$"
    "$\\left[\\textsf{True},\\textsf{False}\\right]$$1$ID16218Flag indicates the new transmission from DL HARQ report.\n";

}  // namespace

TEST(TablePasteGlued, TheSpecSampleBecomesATable) {
  const QString md = gluedTableToMarkdown(QString::fromUtf8(kSpecSample));
  ASSERT_FALSE(md.isEmpty());
  const QStringList l = lines(md);
  ASSERT_EQ(l.size(), 5);  // header, rule, three rows
  EXPECT_EQ(l.at(0), QStringLiteral("| Name | Format | Unit | Range | Dimensions | Source | Description |"));
  EXPECT_EQ(l.at(1), QStringLiteral("| --- | --- | --- | --- | --- | --- | --- |"));
  EXPECT_TRUE(l.at(2).startsWith(QStringLiteral("| $\\mathrm{I}^{\\mathrm{~UeEid}}$ | $\\textsf{integer}$ |")));
  EXPECT_TRUE(l.at(2).endsWith(QStringLiteral("| ID16218 | Ue identifier. |")));
  // A formula over three lines is one cell on one line.
  EXPECT_TRUE(l.at(3).contains(QStringLiteral("$\\left[\\textsf{ACK}, \\textsf{NACK}, \\textsf{DTX}\\right]$")));
  EXPECT_TRUE(l.at(3).endsWith(QStringLiteral("| ID16218 | AN value from DL HARQ report. |")));
  EXPECT_TRUE(l.at(4).endsWith(QStringLiteral("| ID16218 | Flag indicates the new transmission from DL HARQ report. |")));
  // Every row as wide as the header.
  for(const QString& row : l) {
    EXPECT_EQ(row.count(QLatin1Char('|')), 8) << row.toStdString();
  }
  // And it is what a paste of that text makes.
  EXPECT_EQ(tableMarkdownForPaste(QString(), QString::fromUtf8(kSpecSample)), md);
}

TEST(TablePasteGlued, TheSampleFormulasTypeset) {
  // The cells' TeX draws rather than showing as marked source.
  for(const char* tex : {"\\mathrm{I}^{\\mathrm{~UeEid}}",
                         "\\textsf{integer}",
                         "\\left[0,...,\\infty\\right]",
                         "\\left[\\textsf{ACK}, \\textsf{NACK}, \\textsf{DTX}\\right]"}) {
    const MathRender r = renderMath(QString::fromUtf8(tex));
    EXPECT_TRUE(r.ok) << tex;
  }
  EXPECT_TRUE(renderMath(QStringLiteral("\\left[0,...,\\infty\\right]")).plain.contains(QChar(0x221E)));
}

TEST(TablePasteGlued, OrdinaryTextIsLeftAlone) {
  for(const char* text : {"Plain words, nothing more.",
                          "It costs $5 and then $10 more.",
                          "echo $HOME $PATH",
                          "Price list\nThe total is $x$ for one item. Then $y$ for two.",
                          "$a$ $b$",
                          "Name\nValue $x$ only once"}) {
    EXPECT_TRUE(gluedTableToMarkdown(QString::fromUtf8(text)).isEmpty()) << text;
    EXPECT_TRUE(tableMarkdownForPaste(QString(), QString::fromUtf8(text)).isEmpty()) << text;
  }
}

TEST(TablePasteTsv, SpreadsheetRows) {
  const QString md = tsvToMarkdown(QStringLiteral("Name\tType\nid\tint\nflag|x\tbool\n"));
  EXPECT_EQ(md, QStringLiteral("| Name | Type |\n| --- | --- |\n| id | int |\n| flag\\|x | bool |"));
  // A quoted cell holds a line break, as Excel writes it.
  EXPECT_EQ(lines(tsvToMarkdown(QStringLiteral("a\tb\n\"one\ntwo\"\t3"))).at(2), QStringLiteral("| one two | 3 |"));
}

TEST(TablePasteTsv, NotATable) {
  EXPECT_TRUE(tsvToMarkdown(QStringLiteral("one line\twith a tab")).isEmpty());
  EXPECT_TRUE(tsvToMarkdown(QStringLiteral("a\tb\nc\td\te")).isEmpty());  // ragged
  EXPECT_TRUE(tsvToMarkdown(QStringLiteral("no tabs\nat all")).isEmpty());
  // Code indented with tabs is code, not a table with an empty first column.
  EXPECT_TRUE(tsvToMarkdown(QStringLiteral("\tindented code\n\tmore code")).isEmpty());
}

TEST(TablePasteHtml, PlainTable) {
  const QString html = QStringLiteral(
      "<html><body><!--StartFragment--><p>Before</p><table><thead><tr><th>Name</th><th>Type</th></tr></thead>"
      "<tbody><tr><td><p>id</p></td><td>int &amp; long</td></tr><tr><td>a<br>b</td><td>x|y</td></tr></tbody></table>"
      "<p>After</p><!--EndFragment--></body></html>");
  EXPECT_EQ(htmlTablesToMarkdown(html),
            QStringLiteral("Before\n\n| Name | Type |\n| --- | --- |\n| id | int & long |\n| a b | x\\|y |\n\nAfter"));
}

TEST(TablePasteHtml, SpansTakeTheirRoom) {
  const QString html = QStringLiteral(
      "<table><tr><th colspan=\"2\">Range</th><th>Unit</th></tr>"
      "<tr><td rowspan=2>A</td><td>1</td><td>ms</td></tr>"
      "<tr><td>2</td><td>s</td></tr></table>");
  const QStringList l = lines(htmlTablesToMarkdown(html));
  ASSERT_EQ(l.size(), 4);
  EXPECT_EQ(l.at(0), QStringLiteral("| Range |  | Unit |"));
  EXPECT_EQ(l.at(2), QStringLiteral("| A | 1 | ms |"));
  EXPECT_EQ(l.at(3), QStringLiteral("|  | 2 | s |"));
}

TEST(TablePasteHtml, KatexKeepsItsTex) {
  // KaTeX: MathML with the TeX annotation, and the drawn copy hidden from
  // readers; the formula comes once, as $…$.
  const QString html = QStringLiteral(
      "<table><tr><th>Name</th><th>Format</th></tr><tr><td><span class=\"katex\"><span class=\"katex-mathml\">"
      "<math><semantics><mrow><msup><mi mathvariant=\"normal\">I</mi></msup></mrow>"
      "<annotation encoding=\"application/x-tex\">\\mathrm{I}^{\\mathrm{~UeEid}}</annotation></semantics></math></span>"
      "<span class=\"katex-html\" aria-hidden=\"true\"><span class=\"base\"><span class=\"mord\">I</span></span></span></span></td>"
      "<td><script type=\"math/tex\">\\textsf{integer}</script></td></tr></table>");
  const QStringList l = lines(htmlTablesToMarkdown(html));
  ASSERT_EQ(l.size(), 3);
  EXPECT_EQ(l.at(2), QStringLiteral("| $\\mathrm{I}^{\\mathrm{~UeEid}}$ | $\\textsf{integer}$ |"));
}

TEST(TablePasteHtml, NoTableNoConversion) {
  EXPECT_TRUE(htmlTablesToMarkdown(QStringLiteral("<p>Just <b>text</b></p>")).isEmpty());
  // A selection inside one cell is a word, not a table.
  EXPECT_TRUE(htmlTablesToMarkdown(QStringLiteral("<table><tr><td>word</td></tr></table>")).isEmpty());
  EXPECT_TRUE(tableMarkdownForPaste(QStringLiteral("<p>x</p>"), QStringLiteral("x")).isEmpty());
}

TEST(TablePasteHtml, HtmlWinsOverText) {
  const QString html = QStringLiteral("<table><tr><td>a</td><td>b</td></tr><tr><td>1</td><td>2</td></tr></table>");
  EXPECT_EQ(tableMarkdownForPaste(html, QStringLiteral("ab12")), QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 |"));
}

TEST(TablePasteHtml, BrokenHtmlDoesNotHang) {
  EXPECT_NO_THROW(htmlTablesToMarkdown(QStringLiteral("<table><tr><td>a<td>b<tr><td>c<td")));
  EXPECT_NO_THROW(htmlTablesToMarkdown(QStringLiteral("<table><math><annotation")));
  EXPECT_NO_THROW(htmlTablesToMarkdown(QStringLiteral("<table><style>td{}")));
  const QString unclosed = htmlTablesToMarkdown(QStringLiteral("<table><tr><td>a<td>b<tr><td>c<td>d"));
  EXPECT_EQ(unclosed, QStringLiteral("| a | b |\n| --- | --- |\n| c | d |"));
}
