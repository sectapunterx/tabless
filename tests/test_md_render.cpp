// Extensions and rich-text rendering.
//
// Two layers are covered. MdExtensions re-labels blocks that are ordinary
// markdown carrying a convention — a quote that opens with "[!NOTE]", a
// paragraph that opens with "[^id]:", a paragraph that is nothing but display
// maths. Re-labelling must never disturb the source ranges, so that is checked
// too. MdHtml turns inline content into the HTML subset Qt Quick renders, and
// is the only place in the engine that emits markup.
//
// Most of the assertions here are about what must *not* come out: document
// text is always escaped, decorations never fire inside code, and no tag is
// emitted that would make Qt fetch something over the network.

#include "MdPartitionCheck.h"

#include "markdown/MdExtensions.h"
#include "markdown/MdHtml.h"
#include "markdown/MdParser.h"
#include "markdown/MdSourceMap.h"

#include <QString>

#include <gtest/gtest.h>

using namespace heap::md;

namespace {

const MdBlock* firstOfType(const MdAst& ast, BlockType type) {
  for(const MdBlock& block : ast.blocks) {
    if(block.type == type) {
      return &block;
    }
  }
  return nullptr;
}

// A palette with recognisable values, so a test can tell which rule coloured
// a run without depending on the real theme.
MdHtmlOptions testOptions() {
  MdHtmlOptions options;
  options.palette.link = QStringLiteral("#1111ff");
  options.palette.code = QStringLiteral("#222222");
  options.palette.codeBackground = QStringLiteral("#eeeeee");
  options.palette.highlightBackground = QStringLiteral("#ffff00");
  options.palette.mention = QStringLiteral("#00aa00");
  options.palette.ticket = QStringLiteral("#aa0000");
  options.palette.tag = QStringLiteral("#0000aa");
  options.palette.math = QStringLiteral("#aa00aa");
  return options;
}

// Rich text for the first block of `markdown` whose type matches.
QString renderFirst(const QString& markdown, BlockType type, MdHtmlOptions options = testOptions()) {
  const MdAst ast = parse(markdown);
  const MdBlock* block = firstOfType(ast, type);
  if(block == nullptr) {
    return QStringLiteral("<no block of that type>");
  }
  if(options.footnoteNumbers.isEmpty()) {
    options.footnoteNumbers = footnoteNumbers(ast);
  }
  return inlineHtml(ast, *block, options);
}

QString renderParagraph(const QString& markdown) {
  return renderFirst(markdown, BlockType::Paragraph);
}

}  // namespace

// ── Callouts ────────────────────────────────────────────────────────

TEST(MdExtensionsTest, RecognisesCallouts) {
  const MdAst ast = parse(QStringLiteral("> [!WARNING] Mind the gap\n> body text\n"));
  const MdBlock* callout = firstOfType(ast, BlockType::Callout);
  ASSERT_NE(callout, nullptr);
  EXPECT_EQ(callout->calloutKind, QStringLiteral("warning"));
  EXPECT_EQ(callout->calloutTitle, QStringLiteral("Mind the gap"));
  EXPECT_FALSE(callout->foldable);
}

TEST(MdExtensionsTest, CalloutWithoutATitleIsNamedAfterItsKind) {
  const MdAst ast = parse(QStringLiteral("> [!note]\n> just a body\n"));
  const MdBlock* callout = firstOfType(ast, BlockType::Callout);
  ASSERT_NE(callout, nullptr);
  EXPECT_EQ(callout->calloutKind, QStringLiteral("note"));
  EXPECT_EQ(callout->calloutTitle, QStringLiteral("note"));
}

TEST(MdExtensionsTest, CalloutFoldMarkers) {
  const MdAst folded = parse(QStringLiteral("> [!TIP]- Collapsed\n> body\n"));
  const MdBlock* a = firstOfType(folded, BlockType::Callout);
  ASSERT_NE(a, nullptr);
  EXPECT_TRUE(a->foldable);
  EXPECT_TRUE(a->startsFolded);

  const MdAst expanded = parse(QStringLiteral("> [!TIP]+ Open\n> body\n"));
  const MdBlock* b = firstOfType(expanded, BlockType::Callout);
  ASSERT_NE(b, nullptr);
  EXPECT_TRUE(b->foldable);
  EXPECT_FALSE(b->startsFolded);
}

TEST(MdExtensionsTest, CalloutMarkerIsNotRepeatedInTheBody) {
  // The header shows the kind and title; the body must not show them again.
  const MdAst ast = parse(QStringLiteral("> [!NOTE] The title\n> body text\n"));
  const MdBlock* paragraph = firstOfType(ast, BlockType::Paragraph);
  ASSERT_NE(paragraph, nullptr);
  const QString body = inlinePlainText(ast, *paragraph);
  EXPECT_FALSE(body.contains(QStringLiteral("[!NOTE]")));
  EXPECT_FALSE(body.contains(QStringLiteral("The title")));
  EXPECT_TRUE(body.contains(QStringLiteral("body text")));
}

TEST(MdExtensionsTest, OrdinaryQuotesAreLeftAlone) {
  const MdAst ast = parse(QStringLiteral("> just a quotation\n"));
  EXPECT_EQ(firstOfType(ast, BlockType::Callout), nullptr);
  EXPECT_NE(firstOfType(ast, BlockType::Quote), nullptr);
}

// ── Footnotes and maths ─────────────────────────────────────────────

TEST(MdExtensionsTest, RecognisesFootnoteDefinitions) {
  const MdAst ast = parse(QStringLiteral("Text[^why].\n\n[^why]: Because of the thing.\n"));
  const MdBlock* definition = firstOfType(ast, BlockType::FootnoteDef);
  ASSERT_NE(definition, nullptr);
  EXPECT_EQ(definition->footnoteId, QStringLiteral("why"));
  EXPECT_TRUE(inlinePlainText(ast, *definition).contains(QStringLiteral("Because of the thing")));
  EXPECT_FALSE(inlinePlainText(ast, *definition).contains(QStringLiteral("[^why]:")));
}

TEST(MdExtensionsTest, NumbersFootnotesByFirstReference) {
  // Numbering follows the reader, not the order the definitions were written.
  const MdAst ast = parse(QStringLiteral("First[^b] then[^a] again[^b].\n\n[^a]: A\n\n[^b]: B\n"));
  const QHash<QString, int> numbers = footnoteNumbers(ast);
  EXPECT_EQ(numbers.value(QStringLiteral("b")), 1);
  EXPECT_EQ(numbers.value(QStringLiteral("a")), 2);
}

TEST(MdExtensionsTest, PromotesADisplayMathParagraph) {
  const MdAst ast = parse(QStringLiteral("text\n\n$$\nE = mc^2\n$$\n\nmore\n"));
  EXPECT_NE(firstOfType(ast, BlockType::MathBlock), nullptr);
}

TEST(MdExtensionsTest, NamesDiagramLanguages) {
  EXPECT_TRUE(isDiagramLanguage(QStringLiteral("mermaid")));
  EXPECT_TRUE(isDiagramLanguage(QStringLiteral("  Mermaid  ")));
  EXPECT_FALSE(isDiagramLanguage(QStringLiteral("cpp")));
  EXPECT_FALSE(isDiagramLanguage(QString()));
}

TEST(MdExtensionsTest, RelabellingDoesNotDisturbTheSourceRanges) {
  // Extensions only rename blocks. If one ever added or reordered a block, the
  // partition would break and every write-back by range with it.
  const QStringList documents{
      QStringLiteral("> [!NOTE] Title\n> body\n\nafter\n"),
      QStringLiteral("ref[^a]\n\n[^a]: definition\n\nafter\n"),
      QStringLiteral("$$\nx\n$$\n\nafter\n"),
      QStringLiteral("> [!TIP]-\n> folded\n\n> plain quote\n\n[^x]: d\n"),
  };
  for(const QString& document : documents) {
    EXPECT_TRUE(heap::md::test::checkPartition(document).isEmpty()) << document.toStdString();
  }
}

// ── Escaping ────────────────────────────────────────────────────────

TEST(MdHtmlTest, EscapesDocumentText) {
  // A note is not a web page. Anything that looks like markup must read as
  // the characters the author typed.
  // Kept inline: a line *starting* with "<script>" is an HTML block in
  // CommonMark, which is a different path and covered separately below.
  const QString html = renderParagraph(QStringLiteral("text <script>alert(1)</script> & \"quotes\"\n"));
  EXPECT_FALSE(html.contains(QStringLiteral("<script>")));
  EXPECT_TRUE(html.contains(QStringLiteral("&lt;script&gt;")));
  EXPECT_TRUE(html.contains(QStringLiteral("&amp;")));
  EXPECT_TRUE(html.contains(QStringLiteral("&quot;")));
}

TEST(MdHtmlTest, ShowsRawInlineHtmlRatherThanInterpretingIt) {
  const QString html = renderParagraph(QStringLiteral("text <b>not bold</b> more\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("&lt;b&gt;")));
}

TEST(MdHtmlTest, EscapesLinkTargets) {
  const QString html = renderParagraph(QStringLiteral("[label](https://example.com/?a=1&b=2)\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("&amp;b=2")));
  EXPECT_FALSE(html.contains(QStringLiteral("?a=1&b=2")));
}

// ── Emphasis, code, links ───────────────────────────────────────────

TEST(MdHtmlTest, RendersBasicMarkup) {
  const QString html = renderParagraph(QStringLiteral("**bold** _em_ ~~gone~~ `code`\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("<b style=\"font-weight:600\">bold</b>"))) << "600: 700 is not a bundled face";
  EXPECT_TRUE(html.contains(QStringLiteral("<i>em</i>")));
  EXPECT_TRUE(html.contains(QStringLiteral("<s>gone</s>")));
  EXPECT_TRUE(html.contains(QStringLiteral("<code>code</code>")));
}

TEST(MdHtmlTest, RendersLinksAndWikiLinks) {
  const QString link = renderParagraph(QStringLiteral("[text](https://example.com)\n"));
  EXPECT_TRUE(link.contains(QStringLiteral("href=\"https://example.com\"")));
  EXPECT_TRUE(link.contains(QStringLiteral("#1111ff")));

  // A wiki link resolves inside heap, so it gets an internal scheme the UI
  // routes rather than a URL the browser would be handed.
  const QString wiki = renderParagraph(QStringLiteral("see [[Some heading]]\n"));
  EXPECT_TRUE(wiki.contains(QStringLiteral("heap://note/")));
  EXPECT_FALSE(wiki.contains(QStringLiteral("href=\"Some heading\"")));
}

// ── Decorations ─────────────────────────────────────────────────────

TEST(MdHtmlTest, RendersHeapDecorations) {
  const QString html = renderParagraph(QStringLiteral("==note== @alice #HEAP-123 #topic\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("#ffff00")));  // highlight
  EXPECT_TRUE(html.contains(QStringLiteral("heap://person/alice")));
  EXPECT_TRUE(html.contains(QStringLiteral("heap://task/HEAP-123")));
  EXPECT_TRUE(html.contains(QStringLiteral("heap://tag/topic")));
}

TEST(MdHtmlTest, TicketReferenceWinsOverTag) {
  // "#HEAP-1" must not read as the tag "HEAP" followed by "-1".
  const QString html = renderParagraph(QStringLiteral("#HEAP-1\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("heap://task/HEAP-1")));
  EXPECT_FALSE(html.contains(QStringLiteral("heap://tag/")));
}

// APP-121: a known ticket reads as its title, escaped like any document
// text; the link still carries the id. An unknown one stays "#ID".
TEST(MdHtmlTest, TicketReferenceShowsItsTitle) {
  MdHtmlOptions options = testOptions();
  options.ticketTitles.insert(QStringLiteral("HEAP-7"), QStringLiteral("Fix <login>\n  flow"));
  const QString html = renderFirst(QStringLiteral("see #HEAP-7 and #HEAP-8\n"), BlockType::Paragraph, options);
  EXPECT_TRUE(html.contains(QStringLiteral("heap://task/HEAP-7")));
  EXPECT_TRUE(html.contains(QStringLiteral(">Fix &lt;login&gt; flow</a>")));
  EXPECT_FALSE(html.contains(QStringLiteral(">#HEAP-7<")));
  EXPECT_TRUE(html.contains(QStringLiteral(">#HEAP-8</a>")));
}

TEST(MdHtmlTest, DecorationsDoNotFireInsideCode) {
  // In a shell snippet "#tag" is a comment and "@host" is not a person.
  const QString html = renderParagraph(QStringLiteral("`ssh @host  # notatag`\n"));
  EXPECT_FALSE(html.contains(QStringLiteral("heap://person/")));
  EXPECT_FALSE(html.contains(QStringLiteral("heap://tag/")));
}

TEST(MdHtmlTest, DecorationsDoNotFireMidWord) {
  const QString html = renderParagraph(QStringLiteral("email a@b.com and c#d\n"));
  EXPECT_FALSE(html.contains(QStringLiteral("heap://person/")));
  EXPECT_FALSE(html.contains(QStringLiteral("heap://tag/d")));
}

TEST(MdHtmlTest, FootnoteReferencesBecomeNumberedSuperscripts) {
  const QString html = renderParagraph(QStringLiteral("Claim[^src].\n\n[^src]: Source\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("<sup>")));
  EXPECT_TRUE(html.contains(QStringLiteral("heap://fn/src")));
  EXPECT_TRUE(html.contains(QStringLiteral(">1<")));
}

TEST(MdHtmlTest, UndefinedFootnoteShowsItsIdRatherThanAMadeUpNumber) {
  const QString html = renderParagraph(QStringLiteral("Claim[^missing].\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("missing")));
}

// ── Images and egress ───────────────────────────────────────────────

TEST(MdHtmlTest, DoesNotEmitRemoteImagesByDefault) {
  // heap says it makes no network requests of its own. An <img> with an http
  // source would make one, to a host chosen by whoever wrote the note, every
  // time the note is opened.
  const QString html = renderParagraph(QStringLiteral("![alt](https://tracker.example/pixel.png)\n"));
  EXPECT_FALSE(html.contains(QStringLiteral("<img")));
  EXPECT_TRUE(html.contains(QStringLiteral("href=\"https://tracker.example/pixel.png\"")));
  EXPECT_TRUE(html.contains(QStringLiteral("alt")));
}

TEST(MdHtmlTest, EmitsRemoteImagesOnceAllowed) {
  MdHtmlOptions options = testOptions();
  options.allowRemoteImages = true;
  const QString html = renderFirst(QStringLiteral("![alt](https://example.com/x.png)\n"), BlockType::Paragraph, options);
  EXPECT_TRUE(html.contains(QStringLiteral("<img")));
}

TEST(MdHtmlTest, LocalImagesAlwaysRender) {
  // An attachment is a file the author already has; nothing leaves the machine.
  // Relative to the base directory, as a file URL the view can load: handed
  // over as-is it resolved against the QML file (qrc:) and never displayed.
  MdHtmlOptions options = testOptions();
  options.imageBaseDir = QStringLiteral("C:/data/attachments");
  const QString html = renderFirst(QStringLiteral("![diagram](shots/abc.png)\n"), BlockType::Paragraph, options);
  EXPECT_TRUE(html.contains(QStringLiteral("<img src=\"file:///C:/data/attachments/shots/abc.png\""))) << html.toStdString();
}

// Audit 2026-09-30 KNOW-25: local images never displayed.
TEST(MdHtmlTest, ResolveImage_LocalDrivePathsAndFileUrlsAreLoadable) {
  EXPECT_EQ(resolveImage(QStringLiteral("C:\\shots\\x.png"), {}).url, QStringLiteral("file:///C:/shots/x.png"));
  EXPECT_FALSE(resolveImage(QStringLiteral("C:/shots/x.png"), {}).blocked);
  EXPECT_EQ(resolveImage(QStringLiteral("file:///C:/shots/x.png"), {}).url, QStringLiteral("file:///C:/shots/x.png"));
  EXPECT_TRUE(resolveImage(QStringLiteral("https://x/y.png"), {}).remote);
}

TEST(MdHtmlTest, ResolveImage_SharesAndHostsStayBlocked) {
  EXPECT_TRUE(resolveImage(QStringLiteral("\\\\host\\share\\x.png"), {}).blocked);
  EXPECT_TRUE(resolveImage(QStringLiteral("//host/x.png"), {}).blocked);
  EXPECT_TRUE(resolveImage(QStringLiteral("file://host/share/x.png"), {}).blocked);
  EXPECT_TRUE(resolveImage(QStringLiteral("smb://host/x.png"), {}).blocked);
}

TEST(MdHtmlTest, ResolveImage_RelativePathsNeedABaseAndCannotClimbOut) {
  EXPECT_TRUE(resolveImage(QStringLiteral("a/x.png"), {}).url.isEmpty());
  EXPECT_TRUE(resolveImage(QStringLiteral("../../x.png"), QStringLiteral("C:/data/attachments")).url.isEmpty());
  EXPECT_EQ(resolveImage(QStringLiteral("a/x.png"), QStringLiteral("C:/data/attachments")).url,
            QStringLiteral("file:///C:/data/attachments/a/x.png"));
}

// ── Maths and plain text ────────────────────────────────────────────

TEST(MdHtmlTest, TypesetsMathInItsOwnColour) {
  // MdMath (APP-312): set, in the maths colour; what it cannot set stays its
  // source rather than being dropped or drawn wrongly.
  const QString html = renderParagraph(QStringLiteral("inline $x^2$ here\n"));
  EXPECT_TRUE(html.contains(QStringLiteral("#aa00aa")));
  EXPECT_TRUE(html.contains(QStringLiteral("<i>x</i><sup>2</sup>"))) << html.toStdString();
  const QString unknown = renderParagraph(QStringLiteral("inline $\\begin{x}$ here\n"));
  EXPECT_TRUE(unknown.contains(QStringLiteral("\\begin{x}"))) << unknown.toStdString();
}

TEST(MdHtmlTest, PlainTextDropsMarkup) {
  const MdAst ast = parse(QStringLiteral("**bold** and [a link](https://x) and `code`\n"));
  const MdBlock* paragraph = firstOfType(ast, BlockType::Paragraph);
  ASSERT_NE(paragraph, nullptr);
  const QString plain = inlinePlainText(ast, *paragraph);
  EXPECT_EQ(plain, QStringLiteral("bold and a link and code"));
}

TEST(MdHtmlTest, HandlesEmptyAndDegenerateInput) {
  EXPECT_NO_FATAL_FAILURE(renderParagraph(QString()));
  EXPECT_NO_FATAL_FAILURE(renderParagraph(QStringLiteral("==\n")));
  EXPECT_NO_FATAL_FAILURE(renderParagraph(QStringLiteral("@ # [^] ==x\n")));
  EXPECT_NO_FATAL_FAILURE(renderParagraph(QStringLiteral("![](  )\n")));
}

// ── Sources and links that reach outside the note (audit S3) ─────────

TEST(MdHtml, IsLocalSource_FileUrlWithHost_IsNotLocal) {
  EXPECT_FALSE(isLocalSource(QStringLiteral("file://host/share/x.png")));
  EXPECT_FALSE(isLocalSource(QStringLiteral("file:///C:/Users/me/x.png")));
  EXPECT_FALSE(isLocalSource(QStringLiteral("\\\\host\\share\\x.png")));
  EXPECT_FALSE(isLocalSource(QStringLiteral("C:\\Users\\me\\x.png")));
  EXPECT_FALSE(isLocalSource(QStringLiteral("//host/x.png")));
  EXPECT_FALSE(isLocalSource(QStringLiteral("/etc/x.png")));
}

TEST(MdHtml, IsLocalSource_RelativePathOrEmbedded_IsLocal) {
  EXPECT_TRUE(isLocalSource(QStringLiteral("attachments/x.png")));
  EXPECT_TRUE(isLocalSource(QStringLiteral("../img/x.png")));
  EXPECT_TRUE(isLocalSource(QStringLiteral("qrc:/brand/logo.png")));
  EXPECT_TRUE(isLocalSource(QStringLiteral("data:image/png;base64,AAAA")));
}

TEST(MdHtml, InlineHtml_FileImageEvenWithRemoteAllowed_RendersAsLink) {
  MdHtmlOptions options = testOptions();
  options.allowRemoteImages = true;
  const QString html = renderFirst(QStringLiteral("![x](file://host/share/x.png)\n"), BlockType::Paragraph, options);
  EXPECT_FALSE(html.contains(QStringLiteral("<img")));
}

TEST(MdHtml, IsSafeLink_WebAndMail_AreSafe) {
  EXPECT_TRUE(isSafeLink(QStringLiteral("https://example.com")));
  EXPECT_TRUE(isSafeLink(QStringLiteral("HTTP://example.com")));
  EXPECT_TRUE(isSafeLink(QStringLiteral("mailto:a@b.c")));
  EXPECT_TRUE(isSafeLink(QStringLiteral("heap://note/abc")));
}

TEST(MdHtml, IsSafeLink_LocalProgramOrSystemScheme_IsNotSafe) {
  EXPECT_FALSE(isSafeLink(QStringLiteral("file:///C:/Windows/System32/calc.exe")));
  EXPECT_FALSE(isSafeLink(QStringLiteral("ms-settings:privacy")));
  EXPECT_FALSE(isSafeLink(QStringLiteral("javascript:alert(1)")));
  EXPECT_FALSE(isSafeLink(QStringLiteral("smb://host/share")));
}

// Knowledge (APP-269): [[APP-1]] that is a task reads as a chip linked to the
// task; a target nothing answers to is grey and says so, still a link.
TEST(MdRender, WikiTaskIsAChipAndMissingSaysSo) {
  MdHtmlOptions options = testOptions();
  options.wikiTargets.insert(QStringLiteral("APP-1"), {QStringLiteral("task"), QStringLiteral("x APP-1")});
  options.wikiTargets.insert(QStringLiteral("Gone"), {QStringLiteral("missing"), QStringLiteral("Gone - no such note")});
  const QString html = renderFirst(QStringLiteral("See [[APP-1]] and [[Gone]]."), BlockType::Paragraph, options);
  EXPECT_TRUE(html.contains(QStringLiteral("heap://task/APP-1"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("background-color:#eeeeee"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("no such note"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("heap://note/Gone"))) << html.toStdString();
}

// OWN-1: the # autocomplete inserts a tracker task's own id, provider first
// ("#jira-APP-123"). A known one reads as its title and opens the task; an
// id no task has stays the tag it looks like; "#APP-1" is unchanged.
TEST(MdRender, TrackerTaskMentionReadsAsItsTitle) {
  MdHtmlOptions options = testOptions();
  options.ticketTitles.insert(QStringLiteral("jira-APP-123"), QStringLiteral("Billing export"));
  options.ticketTitles.insert(QStringLiteral("github-17"), QStringLiteral("Crash on start"));
  const QString html = renderFirst(QStringLiteral("see #jira-APP-123, #github-17 and #jira-APP-999\n"), BlockType::Paragraph, options);
  EXPECT_TRUE(html.contains(QStringLiteral("heap://task/jira-APP-123"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral(">Billing export</a>"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("heap://task/github-17"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral(">Crash on start</a>"))) << html.toStdString();
  EXPECT_FALSE(html.contains(QStringLiteral("heap://task/jira-APP-999"))) << html.toStdString();
  EXPECT_TRUE(html.contains(QStringLiteral("heap://tag/jira-APP-999"))) << html.toStdString();
}

// A2-35: a link to a note or a task is underlined, whatever its colour; a
// tag or a person is not.
TEST(MdRender, InternalLinksAreUnderlined) {
  MdHtmlOptions options = testOptions();
  options.ticketTitles.insert(QStringLiteral("APP-1"), QStringLiteral("Task one"));
  options.wikiTargets.insert(QStringLiteral("APP-2"), {QStringLiteral("task"), QStringLiteral("APP-2 Task two")});
  const QString html = renderFirst(QStringLiteral("[[Glossary]] [[APP-2]] #APP-1 #topic @alice\n"), BlockType::Paragraph, options);
  const auto styleOf = [&html](const QString& href) {
    const int at = html.indexOf(href);
    const int style = html.indexOf(QStringLiteral("style=\""), at);
    return html.mid(style, html.indexOf(QChar('"'), style + 7) - style);
  };
  EXPECT_TRUE(styleOf(QStringLiteral("heap://note/Glossary")).contains(QStringLiteral("text-decoration:underline"))) << html.toStdString();
  EXPECT_TRUE(styleOf(QStringLiteral("heap://task/APP-2")).contains(QStringLiteral("text-decoration:underline"))) << html.toStdString();
  EXPECT_TRUE(styleOf(QStringLiteral("heap://task/APP-1")).contains(QStringLiteral("text-decoration:underline"))) << html.toStdString();
  EXPECT_TRUE(styleOf(QStringLiteral("heap://tag/topic")).contains(QStringLiteral("text-decoration:none"))) << html.toStdString();
  EXPECT_TRUE(styleOf(QStringLiteral("heap://person/alice")).contains(QStringLiteral("text-decoration:none"))) << html.toStdString();
}

// ── Maths typeset (APP-312) ────────────────────────────────────────────

#include "markdown/MdMath.h"

TEST(MdMathTest, SetsWhatNotesHold) {
  using heap::md::renderMath;
  EXPECT_EQ(renderMath(QStringLiteral("x^2")).html, QStringLiteral("<i>x</i><sup>2</sup>"));
  EXPECT_EQ(renderMath(QStringLiteral("\\alpha_i")).html, QStringLiteral("α<sub><i>i</i></sub>"));
  EXPECT_EQ(renderMath(QStringLiteral("a \\le b")).plain, QStringLiteral("a ≤ b"));
  EXPECT_EQ(renderMath(QStringLiteral("\\frac{a+b}{2}")).plain, QStringLiteral("(a + b)/2"));
  EXPECT_EQ(renderMath(QStringLiteral("\\sqrt{x}")).plain, QStringLiteral("√x"));
  EXPECT_EQ(renderMath(QStringLiteral("\\mathbb{R}^n")).plain, QStringLiteral("ℝ^n"));
  EXPECT_EQ(renderMath(QStringLiteral("\\sin \\theta")).plain, QStringLiteral("sinθ"));
  EXPECT_EQ(renderMath(QStringLiteral("\\text{if } x")).plain, QStringLiteral("if x"));
  EXPECT_EQ(renderMath(QStringLiteral("E = mc^2")).plain, QStringLiteral("E = mc^2"));
  EXPECT_EQ(renderMath(QStringLiteral("12.5x")).html, QStringLiteral("12.5<i>x</i>"));
  EXPECT_TRUE(renderMath(QStringLiteral("\\sum_{i=1}^{n} i")).ok);
}

TEST(MdMathTest, WhatItCannotSetStaysSource) {
  using heap::md::renderMath;
  for(const char* tex : {"\\begin{matrix} a & b \\end{matrix}", "\\unknowncommand x", "{a", "a}", "x^", "a & b"}) {
    const auto r = renderMath(QString::fromUtf8(tex));
    EXPECT_FALSE(r.ok) << tex;
    EXPECT_TRUE(r.html.startsWith(QStringLiteral("<code>"))) << tex;
  }
  EXPECT_EQ(renderMath(QStringLiteral("a < b")).html, QStringLiteral("<i>a</i> &lt; <i>b</i>")) << "html escaped";
}
