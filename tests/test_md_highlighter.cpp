// MdHighlighter: the fenced-code path.
//
// A fenced block's language is captured on the opening fence and has to reach
// every line inside it. It used to be stored in a plain member that nothing
// ever read, so every fence was highlighted with the same union of comment
// markers: a `#include` greyed out in a ```cpp block, and the `//` of a URL
// did the same in ```python.
//
// The formats are read back off the QTextDocument rather than mocked, so these
// cases exercise the real QSyntaxHighlighter pass.

#include "markdown/MdHighlighter.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <limits>

namespace {

// The highlighter reaches its document through a QQuickTextDocument in the app.
// A test drives the QTextDocument directly, which is the same thing the
// QSyntaxHighlighter base class ends up attached to.
class Highlighted {
 public:
  explicit Highlighted(const QString& text) {
    m_highlighter.setDocument(&m_doc);
    m_doc.setPlainText(text);
    // Realising the layout is what makes QSyntaxHighlighter commit its
    // formats onto the blocks; in a headless document nothing else asks for
    // it, so both the initial pass and any later incremental one would leave
    // the block layouts empty and there would be nothing to read back.
    m_doc.documentLayout();
    m_highlighter.rehighlight();
  }

  // The format at `column` on `line`, by way of the formats the highlighter
  // left on the block.
  QTextCharFormat formatAt(int line, int column) const {
    const QTextBlock block = m_doc.findBlockByNumber(line);
    for(const QTextLayout::FormatRange& range : block.layout()->formats()) {
      if(column >= range.start && column < range.start + range.length) {
        return range.format;
      }
    }
    return {};
  }

  QColor colorAt(int line, int column) const {
    return formatAt(line, column).foreground().color();
  }

  bool isItalicAt(int line, int column) const {
    return formatAt(line, column).fontItalic();
  }

  QTextDocument* document() {
    return &m_doc;
  }

 private:
  QTextDocument m_doc;
  heap::md::MdHighlighter m_highlighter;
};

// The comment format is the only italic one the code path applies, which makes
// "was this styled as a comment?" answerable without hardcoding a palette.
bool looksLikeComment(const Highlighted& h, int line, int column) {
  return h.isItalicAt(line, column);
}

}  // namespace

TEST(MdHighlighter, HashIsNotACommentInACppFence) {
  const Highlighted h(QStringLiteral("```cpp\n#include <vector>\nint x = 1;\n```\n"));
  EXPECT_FALSE(looksLikeComment(h, 1, 0)) << "#include is a directive, not a comment";
}

TEST(MdHighlighter, SlashesAreNotACommentInAPythonFence) {
  const Highlighted h(QStringLiteral("```python\nx = a // b\n```\n"));
  EXPECT_FALSE(looksLikeComment(h, 1, 6)) << "// is floor division in Python";
}

TEST(MdHighlighter, HashIsAcommentInAPythonFence) {
  const Highlighted h(QStringLiteral("```python\nx = 1  # set it\n```\n"));
  EXPECT_TRUE(looksLikeComment(h, 1, 7));
}

TEST(MdHighlighter, SlashesAreACommentInACppFence) {
  const Highlighted h(QStringLiteral("```cpp\nint x = 1;  // set it\n```\n"));
  EXPECT_TRUE(looksLikeComment(h, 1, 12));
}

TEST(MdHighlighter, DashesAreACommentInASqlFence) {
  const Highlighted h(QStringLiteral("```sql\nselect 1;  -- why\n```\n"));
  EXPECT_TRUE(looksLikeComment(h, 1, 11));
}

TEST(MdHighlighter, JsonHasNoLineComments) {
  const Highlighted h(QStringLiteral("```json\n{\"tag\": \"#nope\"}\n```\n"));
  EXPECT_FALSE(looksLikeComment(h, 1, 9));
}

// An unlabelled fence keeps the old permissive behaviour rather than losing
// comment highlighting entirely.
TEST(MdHighlighter, AnUnlabelledFenceStillHighlightsAnyComment) {
  const Highlighted h(QStringLiteral("```\nx = 1  # set it\n```\n"));
  EXPECT_TRUE(looksLikeComment(h, 1, 7));
}

TEST(MdHighlighter, AnUnknownLanguageKeepsThePermissiveUnion) {
  const Highlighted h(QStringLiteral("```brainfuck\nx = 1  # set it\n```\n"));
  EXPECT_TRUE(looksLikeComment(h, 1, 7));
}

// A ticket's {code:java} (dogfood 2026-10-23): a known language gets the docs
// viewer's own rules, types and keywords included, not just its comments.
TEST(MdHighlighter, AJavaFenceColoursTypesAndKeywordsAsTheDocsViewerDoes) {
  const Highlighted h(QStringLiteral("```java\nBigDecimal used = new BigDecimal(\"1\");\n// why\n```\n"));
  const QColor plain = h.colorAt(1, 11);  // "used"
  EXPECT_NE(h.colorAt(1, 0), plain) << "BigDecimal is a type";
  EXPECT_EQ(h.formatAt(1, 0).fontWeight(), QFont::DemiBold);
  EXPECT_NE(h.colorAt(1, 18), plain) << "new is a keyword";
  EXPECT_NE(h.colorAt(1, 18), h.colorAt(1, 0)) << "a keyword and a type differ";
  EXPECT_TRUE(looksLikeComment(h, 2, 3));
  EXPECT_FALSE(looksLikeComment(h, 1, 0));
}

// The regression the block-local storage exists for: editing one line inside a
// fence re-highlights only that line, so the language has to be reachable from
// the block rather than from whatever fence was opened last.
TEST(MdHighlighter, ReHighlightingOneLineKeepsItsOwnFenceLanguage) {
  Highlighted h(QStringLiteral("```cpp\nint x = 1;\n```\n\n```python\ny = 2\n```\n"));

  // Touch a line in the FIRST fence after the second one has been parsed.
  QTextCursor cursor(h.document()->findBlockByNumber(1));
  cursor.select(QTextCursor::LineUnderCursor);
  cursor.insertText(QStringLiteral("int x = 1;  // set it"));

  EXPECT_TRUE(looksLikeComment(h, 1, 12)) << "// must still be a comment in the cpp fence";
}

TEST(MdHighlighter, ReHighlightingOneLineInTheSecondFenceUsesItsLanguage) {
  Highlighted h(QStringLiteral("```cpp\nint x = 1;\n```\n\n```python\ny = 2\n```\n"));

  QTextCursor cursor(h.document()->findBlockByNumber(5));
  cursor.select(QTextCursor::LineUnderCursor);
  cursor.insertText(QStringLiteral("y = a // b"));

  EXPECT_FALSE(looksLikeComment(h, 5, 6)) << "// is not a comment in the python fence";
}

// Deleting a fence must not leave its language behind on the lines that were
// inside it.
TEST(MdHighlighter, ProseAfterAFenceIsNotTreatedAsCode) {
  Highlighted h(QStringLiteral("```python\ny = 2\n```\nplain # text\n"));
  EXPECT_FALSE(h.colorAt(3, 0).isValid() && looksLikeComment(h, 3, 6)) << "a line outside the fence is prose, not python";
}

// ── Live preview: the markup of lines away from the caret is hidden ──

namespace {

// The hidden spans of a line as the source text they cover, joined by "|".
QString hiddenOf(const QString& line) {
  QStringList parts;
  for(const auto& [start, length] : heap::md::MdHighlighter::hiddenMarkup(line)) {
    parts << line.mid(start, length);
  }
  return parts.join(QLatin1Char('|'));
}

}  // namespace

TEST(MdHighlighterLive, HidesTheDelimitersNotTheText) {
  EXPECT_EQ(hiddenOf(QStringLiteral("## Title")), QStringLiteral("## "));
  EXPECT_EQ(hiddenOf(QStringLiteral("a **bold** b")), QStringLiteral("**|**"));
  EXPECT_EQ(hiddenOf(QStringLiteral("an *em* word")), QStringLiteral("*|*"));
  EXPECT_EQ(hiddenOf(QStringLiteral("~~gone~~")), QStringLiteral("~~|~~"));
  EXPECT_EQ(hiddenOf(QStringLiteral("see [docs](https://x.test/a)")), QStringLiteral("[|](https://x.test/a)"));
  EXPECT_EQ(hiddenOf(QStringLiteral("[[Note]] and [[Target|alias]]")), QStringLiteral("[[|]]|[[|Target||]]"));
  EXPECT_EQ(hiddenOf(QStringLiteral("run `a **b**` now")), QStringLiteral("`|`")) << "nothing inside code is markup";
  EXPECT_EQ(hiddenOf(QStringLiteral("2 * 3 * 4")), QString()) << "loose asterisks are not emphasis";
  EXPECT_EQ(hiddenOf(QStringLiteral("snake_case_name")), QString());
  EXPECT_EQ(hiddenOf(QStringLiteral("an escaped \\$x$ costs")), QStringLiteral("\\")) << "the backslash of an escape goes";
  EXPECT_EQ(hiddenOf(QStringLiteral("`a \\$ b`")), QStringLiteral("`|`"));
  EXPECT_EQ(hiddenOf(QStringLiteral("#")), QStringLiteral("#")) << "an empty heading's # is markup";
  EXPECT_EQ(hiddenOf(QStringLiteral("1. #")), QStringLiteral("#"));
  EXPECT_EQ(hiddenOf(QStringLiteral("- ## Title")), QStringLiteral("## "));
  EXPECT_EQ(hiddenOf(QStringLiteral("#tag here")), QString()) << "a tag is not a heading";
  EXPECT_EQ(hiddenOf(QStringLiteral("see ![shot.png](shot.png)")), QStringLiteral("![|](shot.png)"));
  EXPECT_EQ(hiddenOf(QStringLiteral("a footnote[^1] here")), QString()) << "ghosted whole; a raised 1 is drawn over it";
  ASSERT_EQ(heap::md::MdHighlighter::footnoteSpans(QStringLiteral("a[^1] b[^note] `[^x]`")).size(), 2);
  EXPECT_TRUE(heap::md::MdHighlighter::footnoteSpans(QStringLiteral("[^1]: the body")).isEmpty());
  EXPECT_EQ(hiddenOf(QStringLiteral("[^1]: The body")), QStringLiteral("[^|]:"));
  EXPECT_EQ(hiddenOf(QStringLiteral("press <kbd>Ctrl</kbd> now")), QStringLiteral("<kbd>|</kbd>"));
  EXPECT_EQ(hiddenOf(QStringLiteral("> [!note] A callout")), QStringLiteral("[!note] "));
}

TEST(MdHighlighterLive, ACalloutCarriesItsKindDownTheQuote) {
  QStringList kinds;
  for(const QVariant& v : heap::md::MdHighlighter::decorations(QStringLiteral("> [!Warning] Title\n> body\n\n> plain quote\n"))) {
    const QVariantMap d = v.toMap();
    if(d.value(QStringLiteral("kind")).toString() == QStringLiteral("quote")) {
      kinds << d.value(QStringLiteral("callout")).toString();
    }
  }
  EXPECT_EQ(kinds, (QStringList{QStringLiteral("warning"), QStringLiteral("warning"), QString()}));
}

TEST(MdHighlighterLive, LinkAtSaysWhatAClickOpens) {
  using heap::md::MdHighlighter;
  const QVariantMap titles{{QStringLiteral("jira-TEL-1"), QStringLiteral("Fix it")}, {QStringLiteral("APP-2"), QStringLiteral("Two")}};
  const QString line =
      QStringLiteral("see [docs](https://x.test/a) and [[Note|alias]] and #jira-TEL-1 and #APP-2 and #tag, @ann[^1] `#jira-TEL-1`");
  const auto at = [&](const QString& piece) {
    return MdHighlighter::linkAt(line, static_cast<int>(line.indexOf(piece)) + 1, titles);
  };
  EXPECT_EQ(at(QStringLiteral("docs")), QStringLiteral("https://x.test/a"));
  EXPECT_EQ(at(QStringLiteral("Note|")), QStringLiteral("heap://note/Note"));
  EXPECT_EQ(at(QStringLiteral("#jira")), QStringLiteral("heap://task/jira-TEL-1"));
  EXPECT_EQ(at(QStringLiteral("#APP")), QStringLiteral("heap://task/APP-2"));
  EXPECT_EQ(at(QStringLiteral("#tag")), QStringLiteral("heap://tag/tag"));
  EXPECT_EQ(at(QStringLiteral("@ann")), QStringLiteral("heap://person/ann"));
  EXPECT_EQ(at(QStringLiteral("[^1]")), QStringLiteral("heap://fn/1"));
  EXPECT_EQ(at(QStringLiteral("`#jira")), QString()) << "nothing in code is a link";
  EXPECT_EQ(MdHighlighter::linkAt(QStringLiteral("go https://x.test now"), 6, {}), QStringLiteral("https://x.test"));
  EXPECT_EQ(MdHighlighter::linkAt(QStringLiteral("plain words"), 3, {}), QString());
}

TEST(MdHighlighterLive, AKnownTaskIsDrawnAsItsTitle) {
  using heap::md::MdHighlighter;
  const QVariantMap titles{{QStringLiteral("jira-TEL-1"), QStringLiteral("Fix the  thing")}};
  const auto spans = MdHighlighter::ticketSpans(QStringLiteral("ref #jira-TEL-1, #other and `#jira-TEL-1`"), titles);
  ASSERT_EQ(spans.size(), 1);
  EXPECT_EQ(spans.at(0).start, 4);
  EXPECT_EQ(spans.at(0).title, QStringLiteral("Fix the thing"));
  int tickets = 0;
  for(const QVariant& v : MdHighlighter::decorations(QStringLiteral("ref #jira-TEL-1\n"), titles)) {
    const QVariantMap d = v.toMap();
    if(d.value(QStringLiteral("kind")).toString() == QStringLiteral("ticket")) {
      ++tickets;
      EXPECT_EQ(d.value(QStringLiteral("at")).toInt(), 4);
      EXPECT_EQ(d.value(QStringLiteral("label")).toString(), QStringLiteral("Fix the thing"));
    }
  }
  EXPECT_EQ(tickets, 1);
}

TEST(MdHighlighterLive, TheCaretLineShowsItsSource) {
  QTextDocument doc;
  heap::md::MdHighlighter hl;
  hl.setDocument(&doc);
  doc.setPlainText(QStringLiteral("a **bold** one\n\nb **bold** two\n"));
  doc.documentLayout();
  hl.setLivePreview(true);
  hl.setRevealStart(0);
  hl.setRevealEnd(0);
  const auto hiddenAt = [&doc](int line, int column) {
    const QTextBlock block = doc.findBlockByNumber(line);
    for(const QTextLayout::FormatRange& range : block.layout()->formats()) {
      if(column >= range.start && column < range.start + range.length) {
        return range.format.foreground().color().alpha() == 0;
      }
    }
    return false;
  };
  EXPECT_FALSE(hiddenAt(0, 2)) << "the caret's line keeps its **";
  EXPECT_TRUE(hiddenAt(2, 2)) << "another line hides them";
  // The caret moves to the last line: the first hides, the last shows.
  const int third = doc.findBlockByNumber(2).position();
  hl.setRevealStart(third);
  hl.setRevealEnd(third);
  EXPECT_TRUE(hiddenAt(0, 2));
  EXPECT_FALSE(hiddenAt(2, 2));
  hl.setLivePreview(false);
  EXPECT_FALSE(hiddenAt(0, 2)) << "the plain source mode hides nothing";
}

TEST(MdHighlighterLive, DecorationsNameWhatIsDrawn) {
  const QString text = QStringLiteral(
      "- one\n"
      "  - [x] done\n"
      "> quote\n"
      "---\n"
      "\n"
      "```js\nx\n```\n"
      "| a | b |\n|:--|--:|\n| 1 | 2 |\n");
  const QVariantList d = heap::md::MdHighlighter::decorations(text);
  QStringList kinds;
  for(const QVariant& v : d) {
    kinds << v.toMap().value(QStringLiteral("kind")).toString();
  }
  EXPECT_EQ(kinds.join(QLatin1Char(',')), QStringLiteral("bullet,task,quote,rule,code,table"));
  const QVariantMap task = d.at(1).toMap();
  EXPECT_TRUE(task.value(QStringLiteral("checked")).toBool());
  EXPECT_EQ(text.mid(task.value(QStringLiteral("box")).toInt(), 3), QStringLiteral("[x]"));
  const QVariantMap code = d.at(4).toMap();
  EXPECT_EQ(text.mid(code.value(QStringLiteral("start")).toInt(), 5), QStringLiteral("```js"));
  EXPECT_EQ(code.value(QStringLiteral("end")).toInt(), static_cast<int>(text.indexOf(QStringLiteral("```\n|"))) + 3);
  const QVariantMap table = d.at(5).toMap();
  EXPECT_EQ(table.value(QStringLiteral("align")).toList(), (QVariantList{QStringLiteral("l"), QStringLiteral("r")}));
  const QVariantList rows = table.value(QStringLiteral("rows")).toList();
  ASSERT_EQ(rows.size(), 3);
  EXPECT_TRUE(rows.at(1).toMap().value(QStringLiteral("separator")).toBool());
  EXPECT_EQ(rows.at(2).toMap().value(QStringLiteral("cells")).toStringList(), (QStringList{QStringLiteral("1"), QStringLiteral("2")}));
  // A "---" under a line of text is a setext heading, not a rule.
  EXPECT_TRUE(heap::md::MdHighlighter::decorations(QStringLiteral("Title\n---\n")).isEmpty());
}

// ── Maths in live preview (APP-312) ───────────────────────────────────

TEST(MdHighlighterLive, MathSpansReadDollarsAsMd4cDoes) {
  using heap::md::MdHighlighter;
  const auto texs = [](const QString& line) {
    QStringList out;
    for(const auto& s : MdHighlighter::mathSpans(line)) {
      out << (s.display ? QStringLiteral("D:") : QString()) + s.tex;
    }
    return out;
  };
  EXPECT_EQ(texs(QStringLiteral("so $x^2$ and $\\alpha$")), (QStringList{QStringLiteral("x^2"), QStringLiteral("\\alpha")}));
  EXPECT_TRUE(texs(QStringLiteral("costs $5 and $10 a month")).isEmpty());
  EXPECT_TRUE(texs(QStringLiteral("price $20, tax $3.50")).isEmpty());
  EXPECT_TRUE(texs(QStringLiteral("between $5 and 6$7")).isEmpty());
  EXPECT_TRUE(texs(QStringLiteral("an escaped \\$x$ dollar")).isEmpty());
  EXPECT_TRUE(texs(QStringLiteral("tax $3.50. An escaped \\$x$ is a dollar.")).isEmpty());
  EXPECT_TRUE(texs(QStringLiteral("code `$x$` here")).isEmpty());
  EXPECT_EQ(texs(QStringLiteral("$$E = mc^2$$")), QStringList{QStringLiteral("D:E = mc^2")});
}

TEST(MdHighlighterLive, DecorationsDrawMaths) {
  const QString text = QStringLiteral("so $x^2$ here\n\n$$\n\\frac{a}{b}\n$$\n\n$\\begin{x}$\n");
  QVariantList maths;
  for(const QVariant& v : heap::md::MdHighlighter::decorations(text)) {
    if(v.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("math")) {
      maths << v;
    }
  }
  ASSERT_EQ(maths.size(), 2) << "the unknown \\begin stays source, not drawn";
  const QVariantMap inl = maths.at(0).toMap();
  EXPECT_FALSE(inl.value(QStringLiteral("display")).toBool());
  EXPECT_EQ(inl.value(QStringLiteral("at")).toInt(), 3);
  EXPECT_EQ(inl.value(QStringLiteral("html")).toString(), QStringLiteral("<i>x</i><sup>2</sup>"));
  const QVariantMap block = maths.at(1).toMap();
  EXPECT_TRUE(block.value(QStringLiteral("display")).toBool());
  EXPECT_EQ(text.mid(block.value(QStringLiteral("start")).toInt(), 2), QStringLiteral("$$"));
  EXPECT_EQ(block.value(QStringLiteral("end")).toInt(), static_cast<int>(text.indexOf(QStringLiteral("$$\n\n$"))) + 2);
}

TEST(MdHighlighterLive, AFormulaKeepsItsDrawnWidthHidden) {
  QTextDocument doc;
  heap::md::MdHighlighter hl;
  hl.setDocument(&doc);
  doc.setPlainText(QStringLiteral("so $\\alpha^2$ here\n\nnext\n"));
  doc.documentLayout();
  hl.setLivePreview(true);
  hl.setRevealStart(doc.findBlockByNumber(2).position());
  hl.setRevealEnd(doc.findBlockByNumber(2).position());
  const QTextBlock block = doc.findBlockByNumber(0);
  bool spaced = false;
  for(const QTextLayout::FormatRange& r : block.layout()->formats()) {
    if(r.start == 3 && r.format.foreground().color().alpha() == 0) {
      spaced = r.format.fontLetterSpacingType() == QFont::AbsoluteSpacing && r.format.fontLetterSpacing() < 0;
    }
  }
  EXPECT_TRUE(spaced) << "the hidden source is squeezed to the drawn formula's width";
}

// ── Rich mode's layout (0.9.4 design gaps) ───────────────────────────

namespace {

// A document in rich mode with the sheet's 15px text, its pending block
// layout applied (it runs after the highlighter's pass).
class Rich {
 public:
  explicit Rich(const QString& text, qreal leading = 1.7) {
    QFont f = m_doc.defaultFont();
    f.setPixelSize(15);
    m_doc.setDefaultFont(f);
    m_hl.setDocument(&m_doc);
    m_hl.setMetrics(QVariantMap{{QStringLiteral("body"), 15},
                                {QStringLiteral("leading"), leading},
                                {QStringLiteral("codePad"), 5},
                                {QStringLiteral("ticketChrome"), 28},
                                {QStringLiteral("ticketScale"), 13.0 / 15.0},
                                {QStringLiteral("figureSpace"), 206}});
    m_hl.setLivePreview(true);
    m_doc.setPlainText(text);
    m_doc.documentLayout();
    m_hl.rehighlight();
    settle();
  }

  void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
  }

  QTextBlockFormat block(int line) const {
    return m_doc.findBlockByNumber(line).blockFormat();
  }

  QTextCharFormat formatAt(int line, int column) const {
    const QTextBlock b = m_doc.findBlockByNumber(line);
    for(const QTextLayout::FormatRange& range : b.layout()->formats()) {
      if(column >= range.start && column < range.start + range.length) {
        return range.format;
      }
    }
    return {};
  }

  QTextDocument m_doc;
  heap::md::MdHighlighter m_hl;
};

}  // namespace

// a-07: 600 at every level, H1 twice the text and H2 1.2 times it, as
// pixel sizes (the sheet's 30 and 18 over 15).
TEST(MdHighlighterRich, HeadingsAreSemiBoldAtTheSheetsRatio) {
  Rich r(QStringLiteral("# Title\n\n## Section\n\n### Sub\n"));
  r.m_hl.setHeadingScale(2.0);
  r.settle();
  const QTextCharFormat h1 = r.formatAt(0, 3);
  const QTextCharFormat h2 = r.formatAt(2, 4);
  EXPECT_EQ(h1.fontWeight(), QFont::DemiBold);
  EXPECT_EQ(h2.fontWeight(), QFont::DemiBold);
  EXPECT_EQ(h1.property(QTextFormat::FontPixelSize).toInt(), 30);
  EXPECT_EQ(h2.property(QTextFormat::FontPixelSize).toInt(), 18);
  EXPECT_EQ(r.formatAt(4, 4).property(QTextFormat::FontPixelSize).toInt(), 17);
  EXPECT_FALSE(h1.hasProperty(QTextFormat::FontSizeAdjustment)) << "no coarse size steps";
}

// Dogfood 2026-10-20: **bold** all but merged with the text. It is the 600
// face (700 is not bundled, so Qt smeared the 400) in the palette's strong
// colour, a step brighter than the text.
TEST(MdHighlighterRich, BoldIsTheHeavyFaceInItsOwnColour) {
  Rich r(QStringLiteral("a **bold** b\n\nlast\n"));
  r.m_hl.setPalette(
      QVariantMap{{QStringLiteral("text"), QStringLiteral("#c8c8c8")}, {QStringLiteral("strong"), QStringLiteral("#ffffff")}});
  r.settle();
  const QTextCharFormat bold = r.formatAt(0, 5);
  EXPECT_EQ(bold.fontWeight(), QFont::DemiBold);
  EXPECT_EQ(bold.foreground().color(), QColor(QStringLiteral("#ffffff")));
  EXPECT_NE(r.formatAt(0, 0).fontWeight(), QFont::DemiBold) << "the text around it stays regular";
}

// a-02: the text at 1.7 (fixed pixels, as TextLeading); headings and table
// rows keep the font's own height; off rich, the document is left plain.
TEST(MdHighlighterRich, TheTextReadsAtTheSheetsLeading) {
  Rich r(QStringLiteral("# Title\n\nSome text\n\n| a | b |\n|---|---|\n| 1 | 2 |\n"));
  EXPECT_EQ(r.block(2).lineHeightType(), QTextBlockFormat::FixedHeight);
  EXPECT_DOUBLE_EQ(r.block(2).lineHeight(), 26.0);  // round(15 * 1.7)
  EXPECT_EQ(r.block(0).lineHeightType(), QTextBlockFormat::SingleHeight) << "a heading keeps its own height";
  EXPECT_EQ(r.block(4).lineHeightType(), QTextBlockFormat::SingleHeight) << "a table row is made tall by its grid";
  EXPECT_FALSE(r.m_doc.isUndoAvailable()) << "laying out a loaded text is not an edit to undo";
  r.m_hl.setLivePreview(false);
  r.settle();
  EXPECT_EQ(r.block(2).lineHeightType(), QTextBlockFormat::SingleHeight) << "the source mode is plain";
}

// a-04: a list item's text starts on a column; its wrapped lines start under
// that column (a hanging indent), drawn or shown as source.
TEST(MdHighlighterRich, AListItemHangsItsWrappedLines) {
  Rich r(QStringLiteral("- one\n  - two\n- [ ] box\nplain\n"));
  const QTextBlockFormat item = r.block(0);
  EXPECT_DOUBLE_EQ(item.leftMargin(), 20.0) << "one gutter, the sheet's 20 over 15";
  EXPECT_DOUBLE_EQ(item.textIndent(), -20.0);
  EXPECT_DOUBLE_EQ(r.block(1).leftMargin(), 40.0) << "a level deeper, a gutter further";
  EXPECT_GE(r.block(2).leftMargin(), 24.0) << "a box takes a wider marker";
  EXPECT_DOUBLE_EQ(r.block(3).leftMargin(), 0.0);
  // The marker's room is the indent: the text starts where wrapped lines do.
  const heap::md::MdHighlighter::ListIndent li = r.m_hl.listIndent(QStringLiteral("  - two"));
  ASSERT_TRUE(li.ok);
  EXPECT_EQ(li.prefixEnd, 4);
  EXPECT_DOUBLE_EQ(li.indent, 40.0);
  EXPECT_EQ(r.formatAt(1, 3).fontLetterSpacingType(), QFont::AbsoluteSpacing) << "the space after the marker takes the room";
}

// a-09: a line that is only a picture gets the picture's room above it and
// its alt text is the caption; a-06 / b-25: `code` and links are drawn.
TEST(MdHighlighterRich, PicturesCodeAndLinksAreDrawn) {
  const QString text = QStringLiteral("![Retries](attachments/a.png)\n\nkey `login` and [RFC](https://x.test) or https://y.test\n");
  Rich r(text);
  EXPECT_DOUBLE_EQ(r.block(0).topMargin(), 206.0);
  QStringList kinds;
  QVariantMap image;
  QVariantMap code;
  for(const QVariant& v : heap::md::MdHighlighter::decorations(text)) {
    const QVariantMap d = v.toMap();
    kinds << d.value(QStringLiteral("kind")).toString();
    if(kinds.last() == QStringLiteral("image")) {
      image = d;
    } else if(kinds.last() == QStringLiteral("inlineCode")) {
      code = d;
    }
  }
  EXPECT_EQ(kinds.join(QLatin1Char(',')), QStringLiteral("image,inlineCode,link,link"));
  EXPECT_EQ(image.value(QStringLiteral("src")).toString(), QStringLiteral("attachments/a.png"));
  EXPECT_EQ(image.value(QStringLiteral("alt")).toString(), QStringLiteral("Retries"));
  EXPECT_EQ(text.mid(code.value(QStringLiteral("at")).toInt(),
                     code.value(QStringLiteral("to")).toInt() - code.value(QStringLiteral("at")).toInt()),
            QStringLiteral("`login`"));
  // `code` in the text's colour, no flat square behind it: the pill is drawn.
  const QTextCharFormat c = r.formatAt(2, 6);
  EXPECT_FALSE(c.hasProperty(QTextFormat::BackgroundBrush));
  EXPECT_EQ(c.property(QTextFormat::FontPixelSize).toInt(), 13);
  // A link is not underlined by the font: its dim line is drawn lower.
  EXPECT_FALSE(r.formatAt(2, 18).fontUnderline());
  // The caption is small.
  EXPECT_EQ(r.formatAt(0, 3).property(QTextFormat::FontPixelSize).toInt(), 12);
}

// a-05: a task's id is hidden as wide as its pill: the key and the room for
// the ring and the padding.
TEST(MdHighlighterRich, ATaskIdIsAsWideAsItsPill) {
  Rich r(QStringLiteral("bug in #APP-101 here\n\nnext\n"));
  r.m_hl.setTicketTitles(QVariantMap{{QStringLiteral("APP-101"), QStringLiteral("APP-101")}});
  r.m_hl.setRevealStart(r.m_doc.findBlockByNumber(2).position());
  r.m_hl.setRevealEnd(r.m_doc.findBlockByNumber(2).position());
  const QTextCharFormat f = r.formatAt(0, 8);
  ASSERT_EQ(f.fontLetterSpacingType(), QFont::AbsoluteSpacing);
  QFont key = r.m_doc.defaultFont();
  key.setPixelSize(13);
  const qreal want = QFontMetricsF(key).horizontalAdvance(QStringLiteral("APP-101")) + 28;
  const qreal own = QFontMetricsF(r.m_doc.defaultFont()).horizontalAdvance(QStringLiteral("#APP-101"));
  EXPECT_NEAR(own + f.fontLetterSpacing() * 8, want, 0.5);
}

// Notes' columns (dogfood 2026-10-19): prose stops at the text column, code
// and tables run on to the code column - in rich and in the source modes.
TEST(MdHighlighterRich, ProseAndCodeKeepTheirOwnColumns) {
  const QString text = QStringLiteral("Some text\n\n```cpp\nint x = 1;\n```\n\n| a | b |\n|---|---|\n\n    indented\n");
  for(const bool rich : {true, false}) {
    Rich r(text);
    r.m_hl.setLivePreview(rich);
    QVariantMap m = r.m_hl.metrics();
    m.insert(QStringLiteral("proseRight"), 240);
    m.insert(QStringLiteral("wideRight"), 0);
    r.m_hl.setMetrics(m);
    r.settle();
    EXPECT_DOUBLE_EQ(r.block(0).rightMargin(), 240.0) << "prose, rich=" << rich;
    EXPECT_DOUBLE_EQ(r.block(2).rightMargin(), 0.0) << "the opening fence";
    EXPECT_DOUBLE_EQ(r.block(3).rightMargin(), 0.0) << "code";
    EXPECT_DOUBLE_EQ(r.block(4).rightMargin(), 0.0) << "the closing fence";
    EXPECT_DOUBLE_EQ(r.block(6).rightMargin(), 0.0) << "a table row";
    EXPECT_DOUBLE_EQ(r.block(9).rightMargin(), 0.0) << "indented code";
    EXPECT_FALSE(r.m_doc.isUndoAvailable()) << "a column change is not an edit to undo";
  }
  // A code column narrower than the text: the other way round.
  Rich r(text);
  QVariantMap m = r.m_hl.metrics();
  m.insert(QStringLiteral("proseRight"), 0);
  m.insert(QStringLiteral("wideRight"), 100);
  r.m_hl.setMetrics(m);
  r.settle();
  EXPECT_DOUBLE_EQ(r.block(0).rightMargin(), 0.0);
  EXPECT_DOUBLE_EQ(r.block(3).rightMargin(), 100.0);
}

// A table's cells are sent typeset: a pasted spec table is mostly formulas.
TEST(MdHighlighterLive, TableCellsCarryTheirFormulasTypeset) {
  const QString text = QStringLiteral("| Name | Range |\n| --- | --- |\n| $\\infty$ **x** | [a](u) |\n");
  QVariantMap table;
  for(const QVariant& v : heap::md::MdHighlighter::decorations(text)) {
    if(v.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("table")) {
      table = v.toMap();
    }
  }
  const QVariantList rows = table.value(QStringLiteral("rows")).toList();
  ASSERT_EQ(rows.size(), 3);
  const QVariantMap row = rows.at(2).toMap();
  const QStringList html = row.value(QStringLiteral("html")).toStringList();
  const QStringList plain = row.value(QStringLiteral("plain")).toStringList();
  ASSERT_EQ(html.size(), 2);
  EXPECT_EQ(plain.at(0), QString(QChar(0x221E)) + QStringLiteral(" x"));
  EXPECT_FALSE(html.at(0).contains(QLatin1Char('$')));
  EXPECT_EQ(plain.at(1), QStringLiteral("a"));
}

// Review 2026-10-19: decorations() runs on every keystroke in rich mode and
// typeset every formula of every table each time - a note with a big spec
// table lagged. A keystroke in the prose above it now re-typesets nothing:
// the cells' HTML is kept by their text. The printed numbers are the signal;
// the budget only catches it getting categorically slower again.
TEST(MdHighlighterLive, ABigFormulaTableIsCheapOnTheNextKeystroke) {
  QString text = QStringLiteral("Prose above.\n\n| Name | Format | Range | Unit |\n| --- | --- | --- | --- |\n");
  for(int r = 0; r < 50; ++r) {
    text += QStringLiteral(
                "| $\\mathrm{I}^{\\mathrm{~Ue%1}}$ | $\\textsf{integer}$ | $\\left[0,...,\\infty\\right]$ | "
                "$\\frac{a_{%1}}{b^{2}}$ |\n")
                .arg(r);
  }
  QElapsedTimer timer;
  timer.start();
  const QVariantList first = heap::md::MdHighlighter::decorations(text);
  const qint64 firstUs = timer.nsecsElapsed() / 1000;
  // The next keystrokes: a letter typed in the prose, 20 times.
  qint64 bestUs = std::numeric_limits<qint64>::max();
  for(int k = 0; k < 20; ++k) {
    const QString typed = QStringLiteral("Prose above") + QString(k + 1, QLatin1Char('x')) + text.mid(11);
    timer.restart();
    const QVariantList again = heap::md::MdHighlighter::decorations(typed);
    bestUs = std::min(bestUs, timer.nsecsElapsed() / 1000);
    ASSERT_EQ(again.size(), first.size());
  }
  std::printf("[ perf ] 50-row formula table: first %lld us, next keystroke best %lld us\n",
              static_cast<long long>(firstUs),
              static_cast<long long>(bestUs));
  EXPECT_LT(bestUs, 50000) << "a keystroke over a 50-row formula table";
}

namespace {

// Counts the lines the highlighter is asked to draw again.
class CountingHighlighter : public heap::md::MdHighlighter {
 public:
  int calls = 0;

 protected:
  void highlightBlock(const QString& text) override {
    ++calls;
    MdHighlighter::highlightBlock(text);
  }
};

bool rowHidden(const QTextDocument& doc, int line) {
  const QTextBlock block = doc.findBlockByNumber(line);
  for(const QTextLayout::FormatRange& range : block.layout()->formats()) {
    if(2 >= range.start && 2 < range.start + range.length) {
      return range.format.foreground().color().alpha() == 0;
    }
  }
  return false;
}

}  // namespace

// E2-1: with the caret in a table every keystroke and every caret move
// re-highlighted the whole table twice, each row walking the whole table
// again - 5 s a keystroke at 200 rows in rich mode. Counted, not timed: the
// rows whose look does not change are not drawn again.
TEST(MdHighlighterLive, TheCaretInABigTableRedrawsOnlyWhatChanges) {
  QString text = QStringLiteral("# P\n\n| A | B | C |\n| --- | --- | --- |\n");
  for(int r = 0; r < 200; ++r) {
    text += QStringLiteral("| r%1 | value %1 | w%1 |\n").arg(r);
  }
  text += QStringLiteral("\nEnd.\n");
  QTextDocument doc;
  CountingHighlighter hl;
  hl.setDocument(&doc);
  doc.setPlainText(text);
  doc.documentLayout();
  hl.setLivePreview(true);
  const int row = 104;  // the 100th row of data
  ASSERT_TRUE(rowHidden(doc, row)) << "away from the caret the grid is drawn over it";

  // Into the table: all of it shows its source.
  const auto caretTo = [&hl](int pos) {
    hl.setRevealStart(pos);
    hl.setRevealEnd(pos);
  };
  const int at = doc.findBlockByNumber(row).position() + 3;
  caretTo(at);
  EXPECT_FALSE(rowHidden(doc, 2));
  EXPECT_FALSE(rowHidden(doc, row));
  EXPECT_FALSE(rowHidden(doc, 203));

  // Along a row and down a row: nothing changes, nothing is drawn again.
  hl.calls = 0;
  caretTo(at + 1);
  caretTo(doc.findBlockByNumber(row + 1).position() + 3);
  EXPECT_EQ(hl.calls, 0);

  // A keystroke in a cell: its line, and none of the 200 others.
  hl.calls = 0;
  QTextCursor cursor(&doc);
  cursor.setPosition(at + 1);
  cursor.insertText(QStringLiteral("y"));
  caretTo(at + 2);
  EXPECT_LE(hl.calls, 2);
  EXPECT_FALSE(rowHidden(doc, row));
  EXPECT_FALSE(rowHidden(doc, 50));

  // Out of it again: every row hidden under the grid.
  caretTo(0);
  EXPECT_TRUE(rowHidden(doc, 2));
  EXPECT_TRUE(rowHidden(doc, row));
  EXPECT_TRUE(rowHidden(doc, 203));
}

// The look a row was drawn with is checked, not assumed: a line typed into
// a row of the table under it joins that table, and the whole of it shows.
TEST(MdHighlighterLive, ALineThatJoinsATableRevealsAllOfIt) {
  QTextDocument doc;
  heap::md::MdHighlighter hl;
  hl.setDocument(&doc);
  doc.setPlainText(QStringLiteral("| a | b |\n| --- | --- |\n| c | d |\nx\n"));
  doc.documentLayout();
  hl.setLivePreview(true);
  const int x = doc.findBlockByNumber(3).position();
  hl.setRevealStart(x);
  hl.setRevealEnd(x);
  ASSERT_TRUE(rowHidden(doc, 0));
  QTextCursor cursor(&doc);
  cursor.setPosition(x);
  cursor.insertText(QStringLiteral("| "));
  cursor.movePosition(QTextCursor::EndOfBlock);
  cursor.insertText(QStringLiteral(" |"));
  hl.setRevealStart(cursor.position());
  hl.setRevealEnd(cursor.position());
  EXPECT_FALSE(rowHidden(doc, 0));
  EXPECT_FALSE(rowHidden(doc, 2));
}

// Dogfood 2026-10-20: a table torn by Enter (or put into a line of text) hid
// every "|" line under the tear - hidden as table rows, with no grid drawn
// over them, as the grid only starts at a header with its |---| line. A
// line is hidden as a row only where decorations() draws that row; the rest
// stays in view as text, in notes saved that way too.
namespace {

// Every line hidden under a grid lies in a table decorations() draws, and
// every row of a drawn table is hidden under it.
QString checkRowsRender(const QTextDocument& doc) {
  QList<QPair<int, int>> tables;
  for(const QVariant& v : heap::md::MdHighlighter::decorations(doc.toPlainText())) {
    const QVariantMap d = v.toMap();
    if(d.value(QStringLiteral("kind")).toString() == QStringLiteral("table")) {
      tables.append({d.value(QStringLiteral("start")).toInt(), d.value(QStringLiteral("end")).toInt()});
    }
  }
  for(QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
    if(b.text().trimmed().isEmpty() || b.text().trimmed() == QStringLiteral("end")) {
      continue;
    }
    const bool drawn = std::any_of(tables.begin(), tables.end(), [&b](const QPair<int, int>& t) {
      return b.position() >= t.first && b.position() <= t.second;
    });
    if(drawn != rowHidden(doc, b.blockNumber())) {
      return QStringLiteral("line %1 \"%2\": %3").arg(b.blockNumber()).arg(b.text(), drawn ? "drawn but shown" : "hidden, not drawn");
    }
  }
  return {};
}

struct LiveDoc {
  QTextDocument doc;
  heap::md::MdHighlighter hl;

  explicit LiveDoc(const QString& text) {
    hl.setDocument(&doc);
    doc.setPlainText(text + QStringLiteral("\nend"));
    doc.documentLayout();
    hl.setLivePreview(true);
    caretToEnd();
  }

  void caretToEnd() {
    const int end = static_cast<int>(doc.characterCount()) - 1;
    hl.setRevealStart(end);
    hl.setRevealEnd(end);
    QCoreApplication::processEvents();
  }
};

}  // namespace

TEST(MdHighlighterLive, EveryRowOfABrokenTableRenders) {
  const QStringList texts = {
      // Torn in a row by Enter: the rows under the tear are text.
      QStringLiteral("| a | b |\n| --- | --- |\n| c \n| d | e |\n| f | g |\n"),
      // Put into a line of text: the text before it took the header's place.
      QStringLiteral("Intro |  |  |\n|---|---|\n|  |  | outro\n| x | y |\n"),
      // Pipes and no |---| line.
      QStringLiteral("| a |\n| b |\n"),
      // Right under a line of text, with no blank line: still a table.
      QStringLiteral("Some text\n| a | b |\n| --- | --- |\n| c | d |\n"),
      QStringLiteral("| a | b |\n| --- | --- |\n| c | d |\nright under\n| e | f |\n"),
  };
  for(const QString& text : texts) {
    LiveDoc live(text);
    EXPECT_EQ(checkRowsRender(live.doc), QString()) << qPrintable(text);
  }
  // A well-formed table is still drawn whole.
  LiveDoc good(QStringLiteral("| a | b |\n| --- | --- |\n| c | d |\n| e | f |\n"));
  EXPECT_TRUE(rowHidden(good.doc, 0));
  EXPECT_TRUE(rowHidden(good.doc, 3));
}

TEST(MdHighlighterLive, TearingATableShowsTheRowsUnderTheTear) {
  LiveDoc live(QStringLiteral("| a | b |\n| --- | --- |\n| c | d |\n| e | f |\n| g | h |\n"));
  ASSERT_TRUE(rowHidden(live.doc, 4));
  // Enter in the middle of "| c | d |", as the old Enter did.
  QTextCursor cursor(&live.doc);
  cursor.setPosition(live.doc.findBlockByNumber(2).position() + 4);
  cursor.insertText(QStringLiteral("\n"));
  live.caretToEnd();
  EXPECT_EQ(checkRowsRender(live.doc), QString());
  EXPECT_FALSE(rowHidden(live.doc, 5)) << "| g | h | is text now, in view";
  // Mended again: the whole table is drawn.
  cursor.deletePreviousChar();
  live.caretToEnd();
  EXPECT_EQ(checkRowsRender(live.doc), QString());
  EXPECT_TRUE(rowHidden(live.doc, 4));
  // The |---| line typed under a line of pipes makes it a header, after the fact.
  LiveDoc late(QStringLiteral("| a | b |\n| c | d |\n"));
  EXPECT_FALSE(rowHidden(late.doc, 0));
  QTextCursor c2(&late.doc);
  c2.setPosition(late.doc.findBlockByNumber(1).position());
  c2.insertText(QStringLiteral("| --- | --- |\n"));
  late.caretToEnd();
  EXPECT_EQ(checkRowsRender(late.doc), QString());
  EXPECT_TRUE(rowHidden(late.doc, 0));
  EXPECT_TRUE(rowHidden(late.doc, 2));
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
