#pragma once

#include <qqmlregistration.h>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QVariantList>
#include <QVariantMap>

namespace heap::md {

// Syntax highlighting for the notes editor.
//
// Replaces NotesHighlighter, which ran about twenty regular expressions
// against each line on its own. A line is not enough context for markdown:
// that highlighter could not tell a heading from a shell comment inside a
// fence, could not see a setext heading at all, and gave every heading level
// the same size, so a document's structure did not read at a glance.
//
// This one keeps a small state machine across lines — fences, frontmatter,
// display maths — and sizes headings by level. Inside a fenced block it hands
// the line to the same language rules the docs viewer uses, so a code block in
// a note looks like code rather than like tinted prose.
//
// It is deliberately lexical rather than AST-driven. A highlighter runs per
// line on every keystroke; a parse per keystroke of a large note would be felt
// as lag, and the AST is already available to everything that needs real
// structure.
class MdHighlighter : public QSyntaxHighlighter {
  Q_OBJECT
  QML_ELEMENT

  Q_PROPERTY(QQuickTextDocument* target READ target WRITE setTarget NOTIFY targetChanged)
  // Theme colours, by the same names NotesHighlighter used so the QML that
  // supplies them does not have to change.
  Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged)
  // Relative size of an H1, with the levels below it scaled down toward 1.0.
  // 1.0 turns the effect off for anyone who would rather have an even page.
  Q_PROPERTY(qreal headingScale READ headingScale WRITE setHeadingScale NOTIFY headingScaleChanged)
  // Live preview (the notes editor, as Obsidian has it): the markup of a line
  // the caret is not on is hidden - "**", "# ", "[", "](url)", "[[" - so the
  // note reads as drawn while staying one plain text to select, copy and edit
  // across lines. The lines between revealStart and revealEnd (the caret, or
  // the selection) show their source.
  Q_PROPERTY(bool livePreview READ livePreview WRITE setLivePreview NOTIFY livePreviewChanged)
  Q_PROPERTY(int revealStart READ revealStart WRITE setRevealStart NOTIFY revealChanged)
  Q_PROPERTY(int revealEnd READ revealEnd WRITE setRevealEnd NOTIFY revealChanged)
  // The font code is set in (fences and spans): the editor's own face is
  // the UI font in rich mode, where code in it read as prose ("<<" as a «).
  Q_PROPERTY(QString monoFamily READ monoFamily WRITE setMonoFamily NOTIFY monoFamilyChanged)
  // The headings' face (Theme.fontDisplay) for the top three levels in rich
  // mode, as the drawn note sets them; empty keeps the editor's own.
  Q_PROPERTY(QString displayFamily READ displayFamily WRITE setDisplayFamily NOTIFY displayFamilyChanged)
  // Task id -> title (AppController.taskTitles): a "#jira-APP-1" or "#APP-1"
  // that names a known task is drawn as its title in rich mode, as the
  // drawn note has it.
  Q_PROPERTY(QVariantMap ticketTitles READ ticketTitles WRITE setTicketTitles NOTIFY ticketTitlesChanged)
  // What rich mode lays out around the text, in pixels of the editor (the
  // overlay draws with the same numbers, so what is hidden and what is drawn
  // over it stay the same width):
  //   body          the editor's font size (headings are sized from it, a-07)
  //   leading       line height as a multiple of body; 0 = the font's own (a-02)
  //   codePad       room either side of `code`, where its pill is drawn (a-06)
  //   ticketChrome  a task pill's width beyond its key (ring, gap, padding, a-05)
  //   ticketScale   the key's size against body
  //   figureSpace   room above an image line for the picture (a-09)
  // Line height, list indents and the picture's room are block formats, so
  // they are set on the document itself, without an undo step of their own.
  Q_PROPERTY(QVariantMap metrics READ metrics WRITE setMetrics NOTIFY metricsChanged)

 public:
  explicit MdHighlighter(QObject* parent = nullptr);

  QQuickTextDocument* target() const {
    return m_target;
  }

  void setTarget(QQuickTextDocument* target);

  QVariantMap palette() const {
    return m_palette;
  }

  void setPalette(const QVariantMap& palette);

  qreal headingScale() const {
    return m_headingScale;
  }

  void setHeadingScale(qreal scale);

  bool livePreview() const {
    return m_livePreview;
  }

  void setLivePreview(bool on);

  int revealStart() const {
    return m_revealStart;
  }

  void setRevealStart(int position);

  int revealEnd() const {
    return m_revealEnd;
  }

  void setRevealEnd(int position);

  QString monoFamily() const {
    return m_monoFamily;
  }

  void setMonoFamily(const QString& family);

  QString displayFamily() const {
    return m_displayFamily;
  }

  void setDisplayFamily(const QString& family);

  QVariantMap ticketTitles() const {
    return m_ticketTitles;
  }

  void setTicketTitles(const QVariantMap& titles);

  QVariantMap metrics() const {
    return m_metrics;
  }

  void setMetrics(const QVariantMap& metrics);

  // The markup ranges of one line that live preview hides, as (start, length)
  // pairs. Public for the tests; the highlighter applies them itself.
  static QList<QPair<int, int>> hiddenMarkup(const QString& line);

  // What live preview draws over the hidden source, one map per element:
  // {kind, start, end} with start/end the document positions of the lines
  // it covers (a reveal range touching them shows the source instead), and
  //   bullet: at (the marker)            task: at (the "["), checked
  //   quote: depth                       rule: -
  //   code: language                     table: rows [{start, cells, header}],
  //                                             align ["l"|"c"|"r"|""]
  // Fenced code and tables are revealed as a whole, the rest line by line.
  //   ticket: at, length, id, label (ticketTitles' value for the id, drawn
  //           over it: the rich editor passes the keys, AppController.taskKeys)
  //   footnote: at, length, label (the id, drawn small and raised)
  //   bullet / task also: text (where the item's text starts)
  //   code also: bodyAt (the first line under the fence)
  //   inlineCode: at, to (the backticks included)
  //   link: at, to (the text of a [link](url), or a bare address)
  //   image: at, src, alt (a line that is only ![alt](src))
  Q_INVOKABLE static QVariantList decorations(const QString& text, const QVariantMap& ticketTitles = {});

  // What a click at pos opens, in MdView's link form: "heap://task/<id>",
  // "heap://note/<target>", "heap://fn/<id>", "heap://tag/<t>",
  // "heap://person/<p>", an address, or "" when it is on none.
  Q_INVOKABLE static QString linkAt(const QString& text, int pos, const QVariantMap& ticketTitles = {});

  // References to known tasks in one line (APP-313): "#jira-TEL-1" by its
  // id, "#TEL-1" by its key; not inside code.
  struct TicketSpan {
    int start = 0;
    int length = 0;
    QString id;
    QString title;
  };

  static QList<TicketSpan> ticketSpans(const QString& line, const QVariantMap& ticketTitles);
  // Footnote references ("[^1]", not the "[^1]:" that defines one), drawn as
  // a small raised number; `title` is the id.
  static QList<TicketSpan> footnoteSpans(const QString& line);

  // The maths spans of one line, as md4c reads them (APP-312): $ opens
  // only before a non-space, closes only after one and not before a digit,
  // so "$5 and $10" is money; \$ is a dollar; nothing inside `code` counts.
  // $$ … $$ on one line is display maths.
  struct MathSpan {
    int start = 0;   // the opening $
    int length = 0;  // through the closing $
    QString tex;
    bool display = false;
  };

  static QList<MathSpan> mathSpans(const QString& line);
  // A table cell as rich text, its formulas typeset and its inline markup
  // gone; its plain text appended to `plainOut`.
  static QString cellHtml(const QString& cell, QStringList* plainOut);
  static QString cellHtmlUncached(const QString& cell, QStringList* plainOut);

  // A list item's marker and the room it takes in rich mode (a-04): the
  // nesting spaces are `depth` gutters wide and the marker with its space
  // one more (wider for a task's box), so the text starts on a fixed column
  // and a wrapped line, indented by `indent`, starts under it.
  struct ListIndent {
    bool ok = false;
    int leadEnd = 0;    // after the nesting spaces
    int prefixEnd = 0;  // where the item's text starts
    qreal leadSpacing = 0;
    qreal prefixSpacing = 0;
    qreal indent = 0;
  };

  ListIndent listIndent(const QString& line) const;

 signals:
  void targetChanged();
  void paletteChanged();
  void headingScaleChanged();
  void livePreviewChanged();
  void revealChanged();
  void monoFamilyChanged();
  void displayFamilyChanged();
  void ticketTitlesChanged();
  void metricsChanged();

 protected:
  void highlightBlock(const QString& text) override;

 private:
  // What a line can be in the middle of. previousBlockState carries it.
  enum BlockState {
    Normal = 0,
    InFence,
    InFrontmatter,
    InMathBlock,
    InIndentedCode,
    // A row of a table decorations() draws: its header (a "|" line with the
    // |---| line under it) and the "|" lines that follow. A "|" line in no
    // such table is text and stays in view (dogfood 2026-10-20: a row torn
    // by Enter hid every row under it).
    InTable,
  };

  void rebuildFormats();
  void highlightInline(const QString& text);
  void highlightCodeLine(const QString& text, const QString& language);
  // Whether the line holds the caret or part of the selection.
  bool revealed(const QTextBlock& block) const;
  // Re-highlights the lines whose revealed state changes when the reveal
  // range moves from [oldStart, oldEnd] to the current one.
  void revealMoved(int oldStart, int oldEnd);
  void hideMarkup(const QString& text);
  // A table row's (or a ```csv record's) line under its drawn grid.
  void ghostTableRow(int length, bool separator);
  void ghost(int start, int length);
  // Invisible like ghost(), with the letter spacing that makes the span as
  // wide as `width`: a formula drawn over its source takes its own room,
  // and the line does not jump when it is drawn or shown.
  void ghostTo(int start, int length, qreal width);
  // Formulas and task references: invisible, as wide as what is drawn over them.
  void hideDrawn(const QString& text);
  // The first and last line of the table or fence a line belongs to; the
  // line itself when it is in neither.
  QPair<QTextBlock, QTextBlock> revealUnit(const QTextBlock& block) const;
  bool hides() const;
  // The editor's font, at metrics' body size when one is given.
  QFont bodyFont() const;
  qreal bodyPx() const;
  qreal metric(const char* key) const;
  // A heading's size against body (a-07): H1 headingScale, H2 1.2, H3 1.1.
  int headingPx(int level) const;
  // The extra width `spacing` added after the one character at `pos`.
  void spaceAfter(int pos, qreal spacing);
  // Line height, list indents and picture room on every block that needs
  // them (or none, off rich): one pass, after the highlighter has run.
  void scheduleBlockLook();
  void applyBlockLook();

  QQuickTextDocument* m_target = nullptr;
  QVariantMap m_palette;
  qreal m_headingScale = 1.45;
  bool m_livePreview = false;
  int m_revealStart = -1;
  int m_revealEnd = -1;
  QString m_monoFamily;
  QString m_displayFamily;
  QVariantMap m_ticketTitles;
  QVariantMap m_metrics;
  // The table revealUnit walked last (first and last block numbers), kept
  // while the text is unchanged: the document's revision it was walked at,
  // or the whole of revealMoved (m_textSteady), which edits no text.
  mutable QPair<int, int> m_tableUnit{-1, -1};
  mutable int m_tableUnitRevision = -1;
  bool m_textSteady = false;
  bool m_lookPending = false;
  bool m_applyingLook = false;
  QTextCharFormat m_hidden;  // collapsed: "## ", "**", "[", "](url)"
  QTextCharFormat m_ghost;   // invisible, keeping its width: what a drawn element sits on

  QTextCharFormat m_heading[6];
  QTextCharFormat m_code;
  QTextCharFormat m_codeFence;
  QTextCharFormat m_quote;
  QTextCharFormat m_marker;
  QTextCharFormat m_link;
  QTextCharFormat m_emphasis;
  QTextCharFormat m_strong;
  QTextCharFormat m_strike;
  QTextCharFormat m_taskDone;
  QTextCharFormat m_mention;
  QTextCharFormat m_ticket;
  QTextCharFormat m_tag;
  QTextCharFormat m_wiki;
  QTextCharFormat m_math;
  QTextCharFormat m_highlight;
  QTextCharFormat m_frontmatter;
  QTextCharFormat m_rule;
  QTextCharFormat m_table;
  QTextCharFormat m_codeInline;
  QTextCharFormat m_markerLive;  // a list marker in rich: its width is measured, so no weight
  QTextCharFormat m_caption;     // an image line's alt text, under the picture

  // Language rules for whatever fence is currently open. The language itself
  // is not a member: it travels per block (see kCommentStyleFor in the .cpp),
  // because highlightBlock is called for one changed line at a time and a
  // member would hold whichever fence was opened last, not this line's.
  QTextCharFormat m_codeKeyword;
  QTextCharFormat m_codeString;
  QTextCharFormat m_codeComment;
  QTextCharFormat m_codeNumber;
  QTextCharFormat m_codeType;
  QTextCharFormat m_codeBuiltin;
  QTextCharFormat m_logError;
  QTextCharFormat m_logWarn;
};

}  // namespace heap::md
