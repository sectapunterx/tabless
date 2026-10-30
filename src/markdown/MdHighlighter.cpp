#include "CodeHighlighter.h"

#include "markdown/LogSyntax.h"
#include "markdown/MdHighlighter.h"
#include "markdown/MdMath.h"
#include "markdown/TableText.h"

#include <QFontInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace heap::md {

namespace {

QColor colorAt(const QVariantMap& palette, const char* key, const QColor& fallback) {
  const QVariant value = palette.value(QLatin1String(key));
  if(!value.isValid()) {
    return fallback;
  }
  const auto color = value.value<QColor>();
  return color.isValid() ? color : fallback;
}

// A fence opens with three or more backticks or tildes; the info string after
// it names the language.
const QRegularExpression& fenceRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s{0,3}(`{3,}|~{3,})\\s*([A-Za-z0-9_+#.-]*)"));
  return rx;
}

const QRegularExpression& atxHeadingRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s{0,3}(#{1,6})(\\s+.*)?$"));
  return rx;
}

// A line of only = or - under text makes that text a heading. A per-line
// highlighter cannot see the text above it, but it can at least mark the
// underline, which is what the old one missed entirely.
const QRegularExpression& setextRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s{0,3}(={2,}|-{2,})\\s*$"));
  return rx;
}

const QRegularExpression& thematicBreakRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s{0,3}(?:-\\s*){3,}$|^\\s{0,3}(?:\\*\\s*){3,}$|^\\s{0,3}(?:_\\s*){3,}$"));
  return rx;
}

const QRegularExpression& listMarkerRx() {
  static const QRegularExpression rx(QStringLiteral("^(\\s*)([-*+]|\\d+[.)])(\\s+)(\\[([ xX])\\]\\s+)?"));
  return rx;
}

// "> [!note] Title": a callout's first line; the kind is group 1.
const QRegularExpression& calloutRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s*(?:>\\s?)+(\\[!([A-Za-z]+)\\][+-]?\\s*)"));
  return rx;
}

const QRegularExpression& quoteRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s*(?:>\\s?)+"));
  return rx;
}

const QRegularExpression& tableRowRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s*\\|.*\\|\\s*$"));
  return rx;
}

// The |---| line under a table's header, as decorations() reads it.
const QRegularExpression& tableSeparatorRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s*\\|?\\s*:?-+:?\\s*(\\|\\s*:?-+:?\\s*)*\\|?\\s*$"));
  return rx;
}

// How a table row was last drawn - hidden under its grid, or as source -
// so revealMoved re-highlights only the rows whose look changes (E2-1).
class TableRowLook : public QTextBlockUserData {
 public:
  explicit TableRowLook(bool hiddenLook) : hidden(hiddenLook) {
  }

  bool hidden = false;
};

// A line that is only a picture: drawn as a figure, its alt text the caption
// under it (a-09). Group 1 is the alt text, 2 the address.
const QRegularExpression& imageLineRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s*!\\[([^\\]\\n]*)\\]\\(([^)\\s]*)(?:\\s+\"[^\"]*\")?\\)\\s*$"));
  return rx;
}

// The sheet's proportions against its 15px body text (H2/Q-Knowledge,
// N/X-Oth-Knowledge): a list's gutter 20, `code` 13, a caption 12.
constexpr qreal kListGutter = 20.0 / 15.0;
constexpr qreal kTaskGutter = 1.6;  // the box (14) and the gap after it
constexpr qreal kCodeScale = 13.0 / 15.0;
constexpr qreal kCaptionScale = 12.0 / 15.0;

}  // namespace

MdHighlighter::MdHighlighter(QObject* parent) : QSyntaxHighlighter(parent) {
  rebuildFormats();
}

void MdHighlighter::setTarget(QQuickTextDocument* target) {
  if(m_target == target) {
    return;
  }
  m_target = target;
  m_tableUnit = {-1, -1};
  setDocument(target != nullptr ? target->textDocument() : nullptr);
  // Sizes measured from the document's font (`code`, captions) come now.
  rebuildFormats();
  emit targetChanged();
}

void MdHighlighter::setPalette(const QVariantMap& palette) {
  if(m_palette == palette) {
    return;
  }
  m_palette = palette;
  rebuildFormats();
  rehighlight();
  emit paletteChanged();
}

void MdHighlighter::setHeadingScale(qreal scale) {
  if(qFuzzyCompare(m_headingScale, scale)) {
    return;
  }
  m_headingScale = scale;
  rebuildFormats();
  rehighlight();
  emit headingScaleChanged();
}

void MdHighlighter::setLivePreview(bool on) {
  if(m_livePreview == on) {
    return;
  }
  m_livePreview = on;
  // Links and `code` read differently drawn than as source (a-06, b-25).
  rebuildFormats();
  rehighlight();
  emit livePreviewChanged();
}

void MdHighlighter::setRevealStart(int position) {
  if(m_revealStart == position) {
    return;
  }
  const int oldStart = m_revealStart;
  m_revealStart = position;
  revealMoved(oldStart, m_revealEnd);
  emit revealChanged();
}

void MdHighlighter::setRevealEnd(int position) {
  if(m_revealEnd == position) {
    return;
  }
  const int oldEnd = m_revealEnd;
  m_revealEnd = position;
  revealMoved(m_revealStart, oldEnd);
  emit revealChanged();
}

void MdHighlighter::setMonoFamily(const QString& family) {
  if(m_monoFamily == family) {
    return;
  }
  m_monoFamily = family;
  rebuildFormats();
  rehighlight();
  emit monoFamilyChanged();
}

void MdHighlighter::setDisplayFamily(const QString& family) {
  if(m_displayFamily == family) {
    return;
  }
  m_displayFamily = family;
  rebuildFormats();
  rehighlight();
  emit displayFamilyChanged();
}

void MdHighlighter::setTicketTitles(const QVariantMap& titles) {
  if(m_ticketTitles == titles) {
    return;
  }
  m_ticketTitles = titles;
  if(m_livePreview) {
    rehighlight();
  }
  emit ticketTitlesChanged();
}

void MdHighlighter::setMetrics(const QVariantMap& metrics) {
  if(m_metrics == metrics) {
    return;
  }
  m_metrics = metrics;
  rebuildFormats();
  rehighlight();
  emit metricsChanged();
}

qreal MdHighlighter::metric(const char* key) const {
  return m_metrics.value(QLatin1String(key)).toReal();
}

QFont MdHighlighter::bodyFont() const {
  return document() != nullptr ? document()->defaultFont() : QFont();
}

qreal MdHighlighter::bodyPx() const {
  const qreal given = metric("body");
  if(given > 0) {
    return given;
  }
  const QFont f = bodyFont();
  return f.pixelSize() > 0 ? f.pixelSize() : QFontInfo(f).pixelSize();
}

int MdHighlighter::headingPx(int level) const {
  // The sheet sets H1 at twice the text and H2 at 1.2 (30 and 18 over 15);
  // headingScale is H1's, the levels under it closer to the text, and 1.0
  // keeps an even page. Pixel sizes, not FontSizeAdjustment's coarse steps,
  // so the ratio holds at every interface scale.
  static constexpr qreal kShare[6] = {1.0, 0.2, 0.1, 0.0, 0.0, 0.0};
  const qreal ratio = 1.0 + (m_headingScale - 1.0) * kShare[std::clamp(level, 1, 6) - 1];
  return std::max(1, static_cast<int>(std::round(bodyPx() * ratio)));
}

void MdHighlighter::spaceAfter(int pos, qreal spacing) {
  if(pos < 0 || std::abs(spacing) < 0.01) {
    return;
  }
  QTextCharFormat f = format(pos);
  f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
  f.setFontLetterSpacing(spacing);
  setFormat(pos, 1, f);
}

MdHighlighter::ListIndent MdHighlighter::listIndent(const QString& line) const {
  ListIndent out;
  const auto m = listMarkerRx().match(line);
  if(!m.hasMatch()) {
    return out;
  }
  const QFontMetricsF fm(bodyFont());
  const qreal gutter = std::round(bodyPx() * kListGutter);
  const QString lead = m.captured(1);
  const QString prefix = line.mid(m.capturedStart(2), m.capturedEnd() - m.capturedStart(2));
  // Two spaces a level, as the overlay counts a bullet's depth.
  const qreal leadWidth = static_cast<qreal>(lead.size() / 2) * gutter;
  const qreal natural = fm.horizontalAdvance(prefix);
  const qreal room = std::max(m.captured(4).isEmpty() ? gutter : std::round(bodyPx() * kTaskGutter), natural);
  out.ok = true;
  out.leadEnd = static_cast<int>(m.capturedEnd(1));
  out.prefixEnd = static_cast<int>(m.capturedEnd());
  out.leadSpacing = lead.isEmpty() ? 0.0 : leadWidth - fm.horizontalAdvance(lead);
  out.prefixSpacing = room - natural;
  out.indent = leadWidth + room;
  return out;
}

QPair<QTextBlock, QTextBlock> MdHighlighter::revealUnit(const QTextBlock& block) const {
  // A table is drawn as one grid and a fence as one box: the caret on any of
  // their lines shows all of their source.
  const auto isTable = [](const QTextBlock& b) {
    return b.isValid() && tableRowRx().match(b.text()).hasMatch();
  };
  if(isTable(block)) {
    // Every row of a table asks this, so the walk over the table is kept:
    // a walk a row made a pass over a 200-row table 40 000 matches (E2-1).
    // It holds while the text does - an edit opens a new revision - and
    // through revealMoved, which edits no text but opens a revision a line.
    QTextDocument* doc = document();
    const int number = block.blockNumber();
    if(doc != nullptr && m_tableUnit.first >= 0 && number >= m_tableUnit.first && number <= m_tableUnit.second &&
       (m_textSteady || doc->revision() == m_tableUnitRevision)) {
      return {doc->findBlockByNumber(m_tableUnit.first), doc->findBlockByNumber(m_tableUnit.second)};
    }
    QTextBlock first = block;
    QTextBlock last = block;
    while(isTable(first.previous())) {
      first = first.previous();
    }
    while(isTable(last.next())) {
      last = last.next();
    }
    m_tableUnit = {first.blockNumber(), last.blockNumber()};
    m_tableUnitRevision = doc != nullptr ? doc->revision() : -1;
    return {first, last};
  }
  const auto inMath = [](const QTextBlock& b) {
    return b.isValid() && b.userState() == InMathBlock;
  };
  if(inMath(block) || inMath(block.previous())) {
    QTextBlock first = block;
    while(inMath(first.previous())) {
      first = first.previous();
    }
    QTextBlock last = block;
    while(inMath(last) && last.next().isValid()) {
      last = last.next();
    }
    return {first, last};
  }
  const auto inFence = [](const QTextBlock& b) {
    return b.isValid() && b.userState() == InFence;
  };
  const bool opens = fenceRx().match(block.text()).hasMatch() && !inFence(block.previous());
  if(inFence(block) || inFence(block.previous()) || opens) {
    QTextBlock first = block;
    while(inFence(first.previous())) {
      first = first.previous();
    }
    QTextBlock last = block;
    while(inFence(last) && last.next().isValid()) {
      last = last.next();
    }
    return {first, last};
  }
  return {block, block};
}

bool MdHighlighter::revealed(const QTextBlock& block) const {
  if(m_revealStart < 0) {
    return false;
  }
  const int from = std::min(m_revealStart, std::max(m_revealEnd, m_revealStart));
  const int to = std::max(m_revealStart, m_revealEnd);
  const auto [first, last] = revealUnit(block);
  const int start = first.position();
  const int end = last.position() + last.length() - 1;  // the line break belongs to it
  return from <= end && to >= start;
}

bool MdHighlighter::hides() const {
  return m_livePreview && !revealed(currentBlock());
}

void MdHighlighter::ghost(int start, int length) {
  if(length <= 0) {
    return;
  }
  QTextCharFormat f = format(start);
  f.merge(m_ghost);
  setFormat(start, length, f);
}

void MdHighlighter::revealMoved(int oldStart, int oldEnd) {
  if(!m_livePreview || document() == nullptr) {
    return;
  }
  // Only the lines entering or leaving the range change: a full pass on
  // every caret move would be felt in a long note.
  const auto range = [](int a, int b) {
    return std::make_pair(std::min(a, std::max(a, b)), std::max(a, b));
  };
  const auto [oa, ob] = range(oldStart, oldEnd < 0 ? oldStart : oldEnd);
  const auto [na, nb] = range(m_revealStart, m_revealEnd < 0 ? m_revealStart : m_revealEnd);
  QTextDocument* doc = document();
  // No text changes from here to the end, so what revealUnit learns of a
  // table holds for all of it.
  m_textSteady = true;
  m_tableUnit = {-1, -1};
  const auto refresh = [&](int a, int b) {
    if(a < 0) {
      return;
    }
    QTextBlock block = revealUnit(doc->findBlock(a)).first;
    const QTextBlock last = revealUnit(doc->findBlock(b)).second;
    while(block.isValid()) {
      // A table row already drawn as the new range wants it is left alone:
      // the caret moving in a table, or a keystroke in a cell, re-highlit
      // every row of it twice - 5 s a keystroke at 200 rows (E2-1).
      const auto* look = dynamic_cast<const TableRowLook*>(block.userData());
      if(look == nullptr || look->hidden == revealed(block) || !tableRowRx().match(block.text()).hasMatch()) {
        rehighlightBlock(block);
      }
      if(block == last) {
        break;
      }
      block = block.next();
    }
  };
  // One edit block around it all: the text area lays itself out again once
  // for the lines that changed, not once a line - entering a 200-row table
  // took seconds that way.
  QTextCursor batch(doc);
  batch.beginEditBlock();
  refresh(oa, ob);
  refresh(na, nb);
  batch.endEditBlock();
  m_textSteady = false;
  m_tableUnit = {-1, -1};
}

QList<QPair<int, int>> MdHighlighter::hiddenMarkup(const QString& line) {
  QList<QPair<int, int>> out;
  const auto add = [&out](qsizetype start, qsizetype length) {
    if(length > 0) {
      out.append({static_cast<int>(start), static_cast<int>(length)});
    }
  };
  // "## " of a heading.
  // A bare "#" is an empty heading, and "1. # Title" a heading in a list
  // item: their "#" is markup too, not a character of the text.
  static const QRegularExpression headingRx(QStringLiteral("^(\\s{0,3}#{1,6}(?:\\s+|$))"));
  if(const auto m = headingRx.match(line); m.hasMatch()) {
    add(m.capturedStart(1), m.capturedLength(1));
  } else if(const auto item = listMarkerRx().match(line); item.hasMatch()) {
    static const QRegularExpression itemHeadingRx(QStringLiteral("\\G#{1,6}(?:\\s+|$)"));
    if(const auto h = itemHeadingRx.match(line, item.capturedEnd()); h.hasMatch()) {
      add(h.capturedStart(), h.capturedLength());
    }
  }
  // Inline code first: what is inside backticks is not markup.
  static const QRegularExpression codeRx(QStringLiteral("(`)([^`\\n]+)(`)"));
  QList<QPair<int, int>> code;
  for(auto it = codeRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    add(m.capturedStart(1), 1);
    add(m.capturedStart(3), 1);
    code.append({static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedEnd())});
  }
  const auto inCode = [&code](qsizetype at) {
    for(const auto& [a, b] : code) {
      if(at >= a && at < b) {
        return true;
      }
    }
    return false;
  };

  struct Pair {
    QRegularExpression rx;
    int open;
    int close;
  };

  // The delimiters of a span: the opening and closing groups are hidden, the
  // text between keeps its style.
  static const Pair pairs[] = {
      {QRegularExpression(QStringLiteral("(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)\\1")), 1, 1},
      {QRegularExpression(QStringLiteral("(?<![*\\w])(\\*)(?!\\s)([^*\\n]+?)(\\*)(?!\\w)")), 1, 3},
      {QRegularExpression(QStringLiteral("(?<![_\\w])(_)(?!\\s)([^_\\n]+?)(_)(?!\\w)")), 1, 3},
      {QRegularExpression(QStringLiteral("(~~)([^~\\n]+)(~~)")), 1, 3},
      {QRegularExpression(QStringLiteral("(==)([^=\\n]+)(==)")), 1, 3},
  };
  for(const Pair& p : pairs) {
    for(auto it = p.rx.globalMatch(line); it.hasNext();) {
      const auto m = it.next();
      if(inCode(m.capturedStart())) {
        continue;
      }
      add(m.capturedStart(p.open), m.capturedLength(p.open));
      // Group 1 doubles as the closing delimiter for ** and __.
      add(m.capturedEnd() - m.capturedLength(p.close), m.capturedLength(p.close));
    }
  }
  // [text](url): the brackets and the address go, the text stays as a link.
  static const QRegularExpression linkRx(QStringLiteral("(?<!!)(\\[)([^\\]\\n]+)(\\]\\([^)\\n]*\\))"));
  for(auto it = linkRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(inCode(m.capturedStart())) {
      continue;
    }
    add(m.capturedStart(1), 1);
    add(m.capturedStart(3), m.capturedLength(3));
  }
  // "> [!note] Title": the kind is drawn as the callout's colour, the title stays.
  if(const auto callout = calloutRx().match(line); callout.hasMatch()) {
    add(callout.capturedStart(1), callout.capturedLength(1));
  }
  // [^1] is drawn as a raised "1"; a definition line "[^1]: text" as "1 text".
  static const QRegularExpression footnoteDefRx(QStringLiteral("^(\\[\\^)[^\\]\\s]+(\\]:)"));
  const auto def = footnoteDefRx.match(line);
  if(def.hasMatch()) {
    add(def.capturedStart(1), 2);
    add(def.capturedStart(2), 2);
  }
  // <kbd>Ctrl</kbd>: the key stays, in a key's face.
  static const QRegularExpression kbdRx(QStringLiteral("(<kbd>)[^<\\n]*(</kbd>)"), QRegularExpression::CaseInsensitiveOption);
  for(auto it = kbdRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(inCode(m.capturedStart())) {
      continue;
    }
    add(m.capturedStart(1), m.capturedLength(1));
    add(m.capturedStart(2), m.capturedLength(2));
  }
  // ![alt](src): the alt text stays, read as the image's name (rich mode
  // draws no picture in the source).
  static const QRegularExpression imageRx(QStringLiteral("(!\\[)([^\\]\\n]*)(\\]\\([^)\\n]*\\))"));
  for(auto it = imageRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(inCode(m.capturedStart())) {
      continue;
    }
    add(m.capturedStart(1), 2);
    add(m.capturedStart(3), m.capturedLength(3));
  }
  // [[target]] and [[target|alias]]: what is read is the target, or the alias.
  static const QRegularExpression wikiRx(QStringLiteral("(\\[\\[)([^\\]|\\n]+\\|)?([^\\]\\n]+)(\\]\\])"));
  for(auto it = wikiRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(inCode(m.capturedStart())) {
      continue;
    }
    add(m.capturedStart(1), 2);
    if(m.capturedLength(2) > 0) {
      add(m.capturedStart(2), m.capturedLength(2));
    }
    add(m.capturedStart(4), 2);
  }
  // A backslash escape (\$, \*, \#): the backslash goes, the character stays.
  static const QRegularExpression escapeRx(QStringLiteral("\\\\[!-/:-@\\[-`{-~]"));
  for(auto it = escapeRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(!inCode(m.capturedStart())) {
      add(m.capturedStart(), 1);
    }
  }
  return out;
}

QVariantList MdHighlighter::decorations(const QString& text, const QVariantMap& ticketTitles) {
  QVariantList out;
  QString callout;  // the kind of the callout the quote being read opened
  const QStringList lines = text.split(QLatin1Char('\n'));
  QVector<int> starts(lines.size());
  int at = 0;
  for(int i = 0; i < lines.size(); ++i) {
    starts[i] = at;
    at += static_cast<int>(lines.at(i).size()) + 1;
  }
  const auto lineEnd = [&](int i) {
    return starts.at(i) + static_cast<int>(lines.at(i).size());
  };
  const QRegularExpression& separatorRx = tableSeparatorRx();
  const auto cellsOf = [](const QString& line) {
    QString l = line.trimmed();
    if(l.startsWith(QLatin1Char('|'))) {
      l.remove(0, 1);
    }
    if(l.endsWith(QLatin1Char('|'))) {
      l.chop(1);
    }
    QStringList cells;
    QString cur;
    for(qsizetype k = 0; k < l.size(); ++k) {
      if(l.at(k) == QLatin1Char('\\') && k + 1 < l.size() && l.at(k + 1) == QLatin1Char('|')) {
        cur += QLatin1Char('|');
        ++k;
      } else if(l.at(k) == QLatin1Char('|')) {
        cells << cur.trimmed();
        cur.clear();
      } else {
        cur += l.at(k);
      }
    }
    cells << cur.trimmed();
    return cells;
  };

  int i = 0;
  while(i < lines.size()) {
    const QString& line = lines.at(i);
    // Fenced code: one box from the opening fence to the closing one.
    if(const auto f = fenceRx().match(line); f.hasMatch()) {
      int j = i + 1;
      while(j < lines.size() && !(fenceRx().match(lines.at(j)).hasMatch() && fenceRx().match(lines.at(j)).captured(2).isEmpty())) {
        ++j;
      }
      // A fence never closed runs to the end of the note: drawn so, and
      // said so, rather than quietly swallowing everything after it.
      const bool closed = j < lines.size();
      const int last = std::min(j, static_cast<int>(lines.size()) - 1);
      const QStringList body = lines.mid(i + 1, (closed ? j : static_cast<int>(lines.size())) - i - 1);
      // ```csv / ```tsv: the grid of its records over the hidden source
      // (APP-315). A record whose quoted field runs over lines starts on its
      // first line; the lines after it are part of its row.
      if(const QChar sep = separatorForLanguage(f.captured(2), body.join(QLatin1Char('\n'))); closed && !sep.isNull() && !body.isEmpty()) {
        QVariantList rows;
        bool quoted = false;
        QString record;
        int recordLine = -1;
        for(int k = 0; k < body.size(); ++k) {
          const QString& l = body.at(k);
          if(!quoted) {
            if(recordLine >= 0) {
              const QStringList cells = parseDelimited(record, sep).value(0);
              rows.append(
                  QVariantMap{{"start", starts.at(recordLine)}, {"cells", cells}, {"header", rows.isEmpty()}, {"separator", false}});
            }
            record = l;
            recordLine = i + 1 + k;
          } else {
            record += QLatin1Char('\n') + l;
          }
          quoted = (quoted ? 1 : 0) + static_cast<int>(l.count(QLatin1Char('"'))) % 2 == 1;
        }
        if(recordLine >= 0) {
          rows.append(QVariantMap{{"start", starts.at(recordLine)},
                                  {"cells", parseDelimited(record, sep).value(0)},
                                  {"header", rows.isEmpty()},
                                  {"separator", false}});
        }
        out.append(
            QVariantMap{{"kind", "table"}, {"start", starts.at(i)}, {"end", lineEnd(last)}, {"rows", rows}, {"align", QVariantList()}});
        i = last + 1;
        continue;
      }
      out.append(QVariantMap{{"kind", "code"},
                             {"start", starts.at(i)},
                             {"end", lineEnd(last)},
                             {"bodyAt", i + 1 < lines.size() ? starts.at(i + 1) : lineEnd(i)},
                             {"language", f.captured(2)},
                             {"closed", closed},
                             {"fenced", true},
                             {"code", body.join(QLatin1Char('\n'))}});
      i = last + 1;
      continue;
    }
    // Indented code: four spaces (or a tab) after a blank line, not under a
    // list item, where the indent continues the item.
    static const QRegularExpression indentedRx(QStringLiteral("^(?: {4}|\\t)"));
    const auto blankAbove = i == 0 || lines.at(i - 1).trimmed().isEmpty();
    const auto underItem = [&]() {
      for(int k = i - 1; k >= 0; --k) {
        if(!lines.at(k).trimmed().isEmpty()) {
          return listMarkerRx().match(lines.at(k)).hasMatch() || indentedRx.match(lines.at(k)).hasMatch();
        }
      }
      return false;
    };
    if(blankAbove && indentedRx.match(line).hasMatch() && !line.trimmed().isEmpty() && !underItem()) {
      int j = i;
      int last = i;
      while(j < lines.size() && (indentedRx.match(lines.at(j)).hasMatch() || lines.at(j).trimmed().isEmpty())) {
        if(!lines.at(j).trimmed().isEmpty()) {
          last = j;
        }
        ++j;
      }
      QStringList body;
      for(int k = i; k <= last; ++k) {
        body << lines.at(k).mid(lines.at(k).startsWith(QLatin1Char('\t')) ? 1 : 4);
      }
      out.append(QVariantMap{{"kind", "code"},
                             {"start", starts.at(i)},
                             {"end", lineEnd(last)},
                             {"language", QString()},
                             {"closed", true},
                             {"fenced", false},
                             {"code", body.join(QLatin1Char('\n'))}});
      i = last + 1;
      continue;
    }
    // $$ on its own line opens display maths, to the next $$.
    if(line.trimmed() == QStringLiteral("$$")) {
      int j = i + 1;
      QStringList body;
      while(j < lines.size() && lines.at(j).trimmed() != QStringLiteral("$$")) {
        body << lines.at(j);
        ++j;
      }
      const int last = std::min(j, static_cast<int>(lines.size()) - 1);
      const MathRender math = renderMath(body.join(QLatin1Char('\n')), true);
      if(math.ok) {
        out.append(QVariantMap{{"kind", "math"},
                               {"display", true},
                               {"start", starts.at(i)},
                               {"end", lineEnd(last)},
                               {"at", starts.at(i)},
                               {"html", math.html}});
      }
      i = last + 1;
      continue;
    }
    // A table: a header row, the |---| line, then rows.
    if(tableRowRx().match(line).hasMatch() && i + 1 < lines.size() && separatorRx.match(lines.at(i + 1)).hasMatch()) {
      int j = i;
      while(j + 1 < lines.size() && tableRowRx().match(lines.at(j + 1)).hasMatch()) {
        ++j;
      }
      QVariantList rows;
      QVariantList align;
      for(const QString& cell : cellsOf(lines.at(i + 1))) {
        const bool l = cell.startsWith(QLatin1Char(':'));
        const bool r = cell.endsWith(QLatin1Char(':'));
        align << QString(l && r ? "c" : r ? "r" : l ? "l" : "");
      }
      for(int k = i; k <= j; ++k) {
        if(k == i + 1) {
          rows.append(QVariantMap{{"start", starts.at(k)}, {"separator", true}, {"cells", QStringList()}});
          continue;
        }
        // Each cell also typeset (its formulas set, its markup gone) and as
        // plain text to measure: a pasted spec table is mostly formulas.
        const QStringList cells = cellsOf(lines.at(k));
        QStringList html;
        QStringList plain;
        for(const QString& cell : cells) {
          html << cellHtml(cell, &plain);
        }
        rows.append(QVariantMap{
            {"start", starts.at(k)}, {"header", k == i}, {"separator", false}, {"cells", cells}, {"html", html}, {"plain", plain}});
      }
      out.append(QVariantMap{{"kind", "table"}, {"start", starts.at(i)}, {"end", lineEnd(j)}, {"rows", rows}, {"align", align}});
      i = j + 1;
      continue;
    }
    // "---" right under a plain line of text underlines it as a heading; under
    // a blank line, a quote, a list item or a heading it is a rule.
    const auto underText = [&]() {
      if(i == 0 || !line.trimmed().startsWith(QLatin1Char('-'))) {
        return false;
      }
      const QString& above = lines.at(i - 1);
      return !above.trimmed().isEmpty() && !quoteRx().match(above).hasMatch() && !listMarkerRx().match(above).hasMatch() &&
             !atxHeadingRx().match(above).hasMatch();
    };
    if(thematicBreakRx().match(line).hasMatch() && !underText()) {
      out.append(QVariantMap{{"kind", "rule"}, {"start", starts.at(i)}, {"end", lineEnd(i)}});
      ++i;
      continue;
    }
    // A picture on a line of its own: the figure in the room above the line,
    // the alt text under it as its caption (a-09).
    if(const auto img = imageLineRx().match(line); img.hasMatch()) {
      out.append(QVariantMap{{"kind", "image"},
                             {"start", starts.at(i)},
                             {"end", lineEnd(i)},
                             {"at", starts.at(i)},
                             {"src", img.captured(2)},
                             {"alt", img.captured(1)}});
      ++i;
      continue;
    }
    // `code` in a pill under it (a-06, b-02) and a link's underline under its
    // text (b-25): drawn, as a text format can neither pad a span nor move or
    // tint an underline.
    {
      static const QRegularExpression codeSpanRx(QStringLiteral("`[^`\\n]+`"));
      static const QRegularExpression linkTextRx(QStringLiteral("(?<!!)\\[([^\\]\\n]+)\\]\\([^)\\n]*\\)"));
      static const QRegularExpression bareUrlRx(QStringLiteral("\\bhttps?://[^\\s)\\]>]+"));
      QList<QPair<qsizetype, qsizetype>> taken;
      const auto inTaken = [&taken](qsizetype pos) {
        return std::any_of(taken.cbegin(), taken.cend(), [pos](const auto& r) {
          return pos >= r.first && pos < r.second;
        });
      };
      const auto span = [&](const QString& kind, qsizetype from, qsizetype to) {
        out.append(QVariantMap{{"kind", kind},
                               {"start", starts.at(i)},
                               {"end", lineEnd(i)},
                               {"at", starts.at(i) + static_cast<int>(from)},
                               {"to", starts.at(i) + static_cast<int>(to)}});
      };
      for(auto it = codeSpanRx.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        taken.append({m.capturedStart(), m.capturedEnd()});
        span(QStringLiteral("inlineCode"), m.capturedStart(), m.capturedEnd());
      }
      for(auto it = linkTextRx.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        if(inTaken(m.capturedStart())) {
          continue;
        }
        taken.append({m.capturedStart(), m.capturedEnd()});
        span(QStringLiteral("link"), m.capturedStart(1), m.capturedEnd(1));
      }
      for(auto it = bareUrlRx.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        if(!inTaken(m.capturedStart())) {
          span(QStringLiteral("link"), m.capturedStart(), m.capturedEnd());
        }
      }
    }
    for(const MathSpan& span : mathSpans(line)) {
      const MathRender math = renderMath(span.tex, span.display);
      if(!math.ok) {
        continue;
      }
      // A line that is nothing but $$ … $$ is a centred formula.
      const bool whole = span.display && line.trimmed().size() == span.length;
      out.append(QVariantMap{{"kind", "math"},
                             {"display", whole},
                             {"start", starts.at(i)},
                             {"end", lineEnd(i)},
                             {"at", starts.at(i) + span.start},
                             {"html", math.html}});
    }
    if(const auto q = quoteRx().match(line); !q.hasMatch() || q.capturedLength() == 0) {
      callout.clear();
    }
    for(const TicketSpan& span : footnoteSpans(line)) {
      out.append(QVariantMap{{"kind", "footnote"},
                             {"start", starts.at(i)},
                             {"end", lineEnd(i)},
                             {"at", starts.at(i) + span.start},
                             {"length", span.length},
                             {"label", span.title}});
    }
    for(const TicketSpan& span : ticketSpans(line, ticketTitles)) {
      out.append(QVariantMap{{"kind", "ticket"},
                             {"start", starts.at(i)},
                             {"end", lineEnd(i)},
                             {"at", starts.at(i) + span.start},
                             {"length", span.length},
                             {"id", span.id},
                             {"label", span.title}});
    }
    if(const auto q = quoteRx().match(line); q.hasMatch() && q.capturedLength() > 0) {
      // A callout runs from its "[!kind]" line to the end of the quote.
      const auto head = calloutRx().match(line);
      if(head.hasMatch()) {
        callout = head.captured(2).toLower();
      }
      out.append(QVariantMap{{"kind", "quote"},
                             {"start", starts.at(i)},
                             {"end", lineEnd(i)},
                             {"depth", static_cast<int>(q.captured(0).count(QLatin1Char('>')))},
                             {"callout", callout},
                             {"head", head.hasMatch()}});
    } else if(const auto m = listMarkerRx().match(line); m.hasMatch() && !m.captured(2).at(0).isDigit()) {
      if(!m.captured(4).isEmpty()) {
        out.append(QVariantMap{{"kind", "task"},
                               {"start", starts.at(i)},
                               {"end", lineEnd(i)},
                               {"text", starts.at(i) + static_cast<int>(m.capturedEnd())},
                               {"at", starts.at(i) + static_cast<int>(m.capturedStart(2))},
                               {"box", starts.at(i) + static_cast<int>(m.capturedStart(4))},
                               {"checked", m.captured(5).toLower() == QStringLiteral("x")}});
      } else {
        out.append(QVariantMap{{"kind", "bullet"},
                               {"start", starts.at(i)},
                               {"end", lineEnd(i)},
                               {"text", starts.at(i) + static_cast<int>(m.capturedEnd())},
                               {"at", starts.at(i) + static_cast<int>(m.capturedStart(2))},
                               {"depth", static_cast<int>(m.captured(1).size()) / 2}});
      }
    }
    ++i;
  }
  return out;
}

QString MdHighlighter::cellHtml(const QString& cell, QStringList* plainOut) {
  // decorations() runs on every keystroke and a pasted spec table is a
  // formula a cell: kept by the cell's text, a keystroke elsewhere
  // typesets nothing again (review 2026-10-19). Bounded: cleared when full.
  struct Typeset {
    QString html;
    QString plain;
  };

  static QMutex cacheLock;
  static QHash<QString, Typeset> cache;
  {
    const QMutexLocker lock(&cacheLock);
    if(const auto it = cache.constFind(cell); it != cache.constEnd()) {
      if(plainOut != nullptr) {
        *plainOut << it->plain;
      }
      return it->html;
    }
  }
  QStringList plainOne;
  const QString html = cellHtmlUncached(cell, &plainOne);
  {
    const QMutexLocker lock(&cacheLock);
    if(cache.size() >= 4096) {
      cache.clear();
    }
    cache.insert(cell, Typeset{html, plainOne.value(0)});
  }
  if(plainOut != nullptr) {
    *plainOut << plainOne.value(0);
  }
  return html;
}

QString MdHighlighter::cellHtmlUncached(const QString& cell, QStringList* plainOut) {
  // As the overlay's plain(): emphasis markers dropped, a link its label.
  static const QRegularExpression marks(QStringLiteral("\\*\\*|__|~~|==|`"));
  static const QRegularExpression link(QStringLiteral("\\[([^\\]]*)\\]\\([^)]*\\)"));
  static const QRegularExpression wiki(QStringLiteral("\\[\\[([^\\]|]*\\|)?([^\\]]*)\\]\\]"));
  QString html;
  QString plain;
  const auto addText = [&](QString t) {
    t.remove(marks);
    t.replace(link, QStringLiteral("\\1"));
    t.replace(wiki, QStringLiteral("\\2"));
    html += t.toHtmlEscaped();
    plain += t;
  };
  qsizetype at = 0;
  for(const MathSpan& span : mathSpans(cell)) {
    addText(cell.mid(at, span.start - at));
    const MathRender math = renderMath(span.tex, false);
    html += math.html;
    plain += math.plain;
    at = span.start + span.length;
  }
  addText(cell.mid(at));
  if(plainOut != nullptr) {
    *plainOut << plain;
  }
  return html;
}

QList<MdHighlighter::MathSpan> MdHighlighter::mathSpans(const QString& line) {
  QList<MathSpan> out;
  // Code spans first: nothing in them is maths.
  static const QRegularExpression codeRx(QStringLiteral("`[^`\\n]+`"));
  QList<QPair<qsizetype, qsizetype>> code;
  for(auto it = codeRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    code.append({m.capturedStart(), m.capturedEnd()});
  }
  const auto inCode = [&code](qsizetype at) {
    for(const auto& [a, b] : code) {
      if(at >= a && at < b) {
        return true;
      }
    }
    return false;
  };
  static const QRegularExpression displayRx(QStringLiteral("(?<!\\\\)\\$\\$(.+?)(?<!\\\\)\\$\\$"));
  static const QRegularExpression inlineRx(
      QStringLiteral("(?<![\\\\$])\\$(?![\\s$])((?:\\\\[^$]|[^$\\\\\\n])+?)(?<![\\s\\\\])\\$(?![$\\d])"));
  QList<QPair<qsizetype, qsizetype>> taken;
  for(auto it = displayRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(inCode(m.capturedStart())) {
      continue;
    }
    out.append({static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), m.captured(1), true});
    taken.append({m.capturedStart(), m.capturedEnd()});
  }
  for(auto it = inlineRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    bool clash = inCode(m.capturedStart());
    for(const auto& [a, b] : taken) {
      clash = clash || (m.capturedStart() < b && m.capturedEnd() > a);
    }
    if(!clash) {
      out.append({static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), m.captured(1), false});
    }
  }
  std::sort(out.begin(), out.end(), [](const MathSpan& a, const MathSpan& b) {
    return a.start < b.start;
  });
  return out;
}

namespace {

// A footnote number's size against the text's (the overlay draws it so).
constexpr qreal kFootnoteScale = 0.7;

// `code` spans of a line, as [start, end) pairs: nothing in them is markup.
QList<QPair<qsizetype, qsizetype>> codeRanges(const QString& line) {
  static const QRegularExpression codeRx(QStringLiteral("`[^`\\n]+`"));
  QList<QPair<qsizetype, qsizetype>> out;
  for(auto it = codeRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    out.append({m.capturedStart(), m.capturedEnd()});
  }
  return out;
}

bool within(const QList<QPair<qsizetype, qsizetype>>& ranges, qsizetype at) {
  return std::any_of(ranges.cbegin(), ranges.cend(), [at](const auto& r) {
    return at >= r.first && at < r.second;
  });
}

}  // namespace

QList<MdHighlighter::TicketSpan> MdHighlighter::footnoteSpans(const QString& line) {
  QList<TicketSpan> out;
  // A reference, not the definition at the start of its own line.
  static const QRegularExpression rx(QStringLiteral("\\[\\^([^\\]\\s]+)\\](?!:)"));
  const auto code = codeRanges(line);
  for(auto it = rx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(within(code, m.capturedStart())) {
      continue;
    }
    out.append({static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), m.captured(1), m.captured(1)});
  }
  return out;
}

QList<MdHighlighter::TicketSpan> MdHighlighter::ticketSpans(const QString& line, const QVariantMap& ticketTitles) {
  QList<TicketSpan> out;
  if(ticketTitles.isEmpty()) {
    return out;
  }
  // As MdHtml reads them: "#APP-1" by key, "#jira-APP-1" (the shape of a tag)
  // only when a task has that id.
  static const QRegularExpression rx(QStringLiteral("(?<![\\p{L}\\p{N}_])#([\\p{L}][\\p{L}\\p{N}_/-]*)"));
  const auto code = codeRanges(line);
  for(auto it = rx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    const QString id = m.captured(1);
    const auto known = ticketTitles.constFind(id);
    if(known == ticketTitles.constEnd() || within(code, m.capturedStart())) {
      continue;
    }
    const QString title = known->toString().simplified();
    if(title.isEmpty()) {
      continue;
    }
    out.append({static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), id, title});
  }
  return out;
}

QString MdHighlighter::linkAt(const QString& text, int pos, const QVariantMap& ticketTitles) {
  if(pos < 0 || pos > text.size()) {
    return {};
  }
  const qsizetype lineStart = text.lastIndexOf(QLatin1Char('\n'), pos - 1) + 1;
  qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), pos);
  if(lineEnd < 0) {
    lineEnd = text.size();
  }
  const QString line = text.mid(lineStart, lineEnd - lineStart);
  const qsizetype at = pos - lineStart;
  const auto code = codeRanges(line);
  if(within(code, at)) {
    return {};
  }
  const auto hit = [at](const QRegularExpressionMatch& m) {
    return at >= m.capturedStart() && at <= m.capturedEnd();
  };
  const auto encoded = [](const QString& s) {
    return QString::fromUtf8(QUrl::toPercentEncoding(s));
  };
  static const QRegularExpression wikiRx(QStringLiteral("\\[\\[([^\\]|\\n]+)(?:\\|[^\\]\\n]*)?\\]\\]"));
  for(auto it = wikiRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(hit(m)) {
      const QString target = m.captured(1).trimmed();
      return (ticketTitles.contains(target) ? QStringLiteral("heap://task/") : QStringLiteral("heap://note/")) + encoded(target);
    }
  }
  static const QRegularExpression linkRx(QStringLiteral("(?<!!)\\[[^\\]\\n]*\\]\\(([^)\\s]*)(?:\\s+\"[^\"]*\")?\\)"));
  for(auto it = linkRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(hit(m)) {
      return m.captured(1);
    }
  }
  static const QRegularExpression urlRx(QStringLiteral("<?(\\bhttps?://[^\\s)\\]>]+)>?"));
  for(auto it = urlRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(hit(m)) {
      return m.captured(1);
    }
  }
  static const QRegularExpression footnoteRx(QStringLiteral("\\[\\^([^\\]\\s]+)\\]"));
  for(auto it = footnoteRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(hit(m)) {
      return QStringLiteral("heap://fn/") + encoded(m.captured(1));
    }
  }
  static const QRegularExpression keyRx(QStringLiteral("(?<![A-Za-z0-9_])#([A-Z][A-Z0-9]*-\\d+)"));
  for(auto it = keyRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(hit(m)) {
      return QStringLiteral("heap://task/") + encoded(m.captured(1));
    }
  }
  static const QRegularExpression tagRx(QStringLiteral("(?<![\\p{L}\\p{N}_])#([\\p{L}][\\p{L}\\p{N}_/-]*)"));
  for(auto it = tagRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(hit(m)) {
      const QString id = m.captured(1);
      return (ticketTitles.contains(id) ? QStringLiteral("heap://task/") : QStringLiteral("heap://tag/")) + encoded(id);
    }
  }
  static const QRegularExpression mentionRx(QStringLiteral("(?<![\\p{L}\\p{N}_])@([\\p{L}\\p{N}_.-]+)"));
  for(auto it = mentionRx.globalMatch(line); it.hasNext();) {
    const auto m = it.next();
    if(hit(m)) {
      return QStringLiteral("heap://person/") + encoded(m.captured(1));
    }
  }
  return {};
}

void MdHighlighter::ghostTo(int start, int length, qreal width) {
  if(length <= 0 || document() == nullptr) {
    return;
  }
  const QFont font = document()->defaultFont();
  const QString source = currentBlock().text().mid(start, length);
  const qreal own = QFontMetricsF(font).horizontalAdvance(source);
  QTextCharFormat f;
  f.setForeground(QColor(0, 0, 0, 0));
  f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
  f.setFontLetterSpacing((width - own) / length);
  setFormat(start, length, f);
}

void MdHighlighter::hideDrawn(const QString& text) {
  if(document() == nullptr) {
    return;
  }
  for(const MathSpan& span : mathSpans(text)) {
    const MathRender math = renderMath(span.tex, span.display);
    if(!math.ok) {
      continue;  // shown as its source, which is what is drawn instead
    }
    QTextDocument measure;
    measure.setDocumentMargin(0);
    measure.setDefaultFont(document()->defaultFont());
    measure.setHtml(math.html);
    ghostTo(span.start, span.length, measure.idealWidth());
  }
  // A footnote's number is drawn small and raised over its "[^1]".
  QFont small = document()->defaultFont();
  if(small.pixelSize() > 0) {
    small.setPixelSize(std::max(1, static_cast<int>(std::round(small.pixelSize() * kFootnoteScale))));
  } else {
    small.setPointSizeF(small.pointSizeF() * kFootnoteScale);
  }
  const QFontMetricsF smallMetrics(small);
  for(const TicketSpan& span : footnoteSpans(text)) {
    ghostTo(span.start, span.length, smallMetrics.horizontalAdvance(span.title) + 2);
  }
  // A known task is drawn over its id: what ticketTitles names it (the key,
  // in the rich editor) and the ring and padding around it (a-05).
  QFont label = bodyFont();
  const qreal labelScale = metric("ticketScale") > 0 ? metric("ticketScale") : 1.0;
  label.setPixelSize(std::max(1, static_cast<int>(std::round(bodyPx() * labelScale))));
  const QFontMetricsF metrics(label);
  for(const TicketSpan& span : ticketSpans(text, m_ticketTitles)) {
    ghostTo(span.start, span.length, metrics.horizontalAdvance(span.title) + metric("ticketChrome"));
  }
  // A link's "](address)" takes no room (b-25): collapsed to 1px glyphs, a
  // long address still left a word's gap between the link and the text.
  static const QRegularExpression linkTailRx(QStringLiteral("(?<!!)\\[[^\\]\\n]+(\\]\\([^)\\n]*\\))"));
  const auto code = codeRanges(text);
  for(auto it = linkTailRx.globalMatch(text); it.hasNext();) {
    const auto m = it.next();
    if(within(code, m.capturedStart())) {
      continue;
    }
    ghostTo(static_cast<int>(m.capturedStart(1)), static_cast<int>(m.capturedLength(1)), 0);
  }
  // A tracker's text (quiet): a bare address reads without its "https://"
  // (dogfood 2026-10-30 #10); the whole of it shows on hover.
  if(m_palette.value(QStringLiteral("quiet")).toBool()) {
    static const QRegularExpression schemeRx(QStringLiteral("(?<![\\w/(<\\[])https?://(?=[^\\s)\\]>])"),
                                             QRegularExpression::CaseInsensitiveOption);
    for(auto it = schemeRx.globalMatch(text); it.hasNext();) {
      const auto m = it.next();
      if(!within(code, m.capturedStart())) {
        ghostTo(static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), 0);
      }
    }
  }
  // `code`: its backticks become the pill's padding (a-06).
  if(const qreal pad = metric("codePad"); pad > 0) {
    static const QRegularExpression codeRx(QStringLiteral("`[^`\\n]+`"));
    for(auto it = codeRx.globalMatch(text); it.hasNext();) {
      const auto m = it.next();
      ghostTo(static_cast<int>(m.capturedStart()), 1, pad);
      ghostTo(static_cast<int>(m.capturedEnd()) - 1, 1, pad);
    }
  }
}

void MdHighlighter::ghostTableRow(int length, bool separator) {
  // A row's line is made taller (a big, narrow invisible face) so the cells
  // drawn over it have room; the |---| line under the header all but goes.
  const QFont base = document() != nullptr ? document()->defaultFont() : QFont();
  const int px = base.pixelSize() > 0 ? base.pixelSize() : QFontInfo(base).pixelSize();
  QTextCharFormat f = m_ghost;
  f.setProperty(QTextFormat::FontPixelSize, separator ? 2 : static_cast<int>(std::round(px * 1.55)));
  f.setFontStretch(1);
  setFormat(0, length, f);
}

void MdHighlighter::hideMarkup(const QString& text) {
  for(const auto& [start, length] : hiddenMarkup(text)) {
    // Kept in the text, drawn as nothing: a hairline glyph in no colour.
    setFormat(start, length, m_hidden);
  }
}

void MdHighlighter::rebuildFormats() {
  const QColor text = colorAt(m_palette, "text", QColor(0xE6, 0xE6, 0xE6));
  const QColor dim = colorAt(m_palette, "dim", QColor(0x8A, 0x8E, 0x98));
  const QColor accent = colorAt(m_palette, "accent", QColor(0x5A, 0xC8, 0xD8));
  const QColor codeColor = colorAt(m_palette, "code", text);
  const QColor codeBg = colorAt(m_palette, "codeBg", QColor());

  // Headings: the level shows in the weight here, and in the size applied per
  // line in highlightBlock. Sizing relative to the document's own font keeps
  // the editor's font setting as the baseline instead of hard-coding points.
  // 600 at every level (a-07): the 700 asked for before is not a bundled
  // weight, so Qt faked it by smearing the 400.
  for(int level = 0; level < 6; ++level) {
    m_heading[level].setFontWeight(QFont::DemiBold);
    m_heading[level].setForeground(text);
    m_heading[level].setFontFamilies(level < 3 && !m_displayFamily.isEmpty() ? QStringList{m_displayFamily} : QStringList());
  }

  m_code = QTextCharFormat();
  m_code.setForeground(codeColor);
  m_code.setFontFixedPitch(true);
  if(!m_monoFamily.isEmpty()) {
    m_code.setFontFamilies(QStringList{m_monoFamily});
  }
  if(codeBg.isValid()) {
    m_code.setBackground(codeBg);
  }
  m_codeFence = m_code;
  m_codeFence.setForeground(dim);
  // `code` in a line and <kbd>: the theme's accent, when one is given.
  m_codeInline = m_code;
  m_codeInline.setForeground(colorAt(m_palette, "inlineCode", codeColor));
  if(m_livePreview) {
    // Drawn: a pill under the span (MdLiveDecorations), the sheet's 13 over
    // 15 so mono reads the size of the text around it (a-06).
    m_codeInline.clearBackground();
    m_codeInline.setProperty(QTextFormat::FontPixelSize, std::max(1, static_cast<int>(std::round(bodyPx() * kCodeScale))));
  }

  m_quote.setForeground(dim);
  m_quote.setFontItalic(true);
  m_marker.setForeground(accent);
  m_marker.setFontWeight(QFont::DemiBold);
  // Drawn, a link reads as text with a dim underline a little under it
  // (b-25); the source keeps the accent. A tracker's text read here draws
  // it in the link's own colour (dogfood 2026-10-30 #10).
  const bool quietText = m_palette.value(QStringLiteral("quiet")).toBool();
  m_link = QTextCharFormat();
  m_link.setForeground(m_livePreview ? (quietText ? colorAt(m_palette, "link", text) : text) : accent);
  m_link.setFontUnderline(!m_livePreview);
  m_markerLive = QTextCharFormat();
  m_markerLive.setForeground(accent);
  m_caption = QTextCharFormat();
  m_caption.setForeground(dim);
  m_caption.setProperty(QTextFormat::FontPixelSize, std::max(1, static_cast<int>(std::round(bodyPx() * kCaptionScale))));
  m_emphasis.setFontItalic(true);
  // 600, the heaviest bundled face, and a step brighter than the text
  // (dogfood 2026-10-20): 700 is not bundled, so Qt smeared the 400 into a
  // bold that all but merged with the text around it.
  m_strong.setFontWeight(QFont::DemiBold);
  m_strong.setForeground(colorAt(m_palette, "strong", text));
  m_strike.setFontStrikeOut(true);
  m_strike.setForeground(dim);
  m_taskDone.setFontStrikeOut(true);
  m_taskDone.setForeground(dim);
  m_mention.setForeground(colorAt(m_palette, "mention", accent));
  // A tracker's text read here («C · тише»): a name is the text's own
  // colour, underlined, and code keeps one weight; colour is for what asks
  // for an action.
  const bool quiet = m_palette.value(QStringLiteral("quiet")).toBool();
  m_mention.setFontUnderline(quiet);
  m_ticket.setForeground(colorAt(m_palette, "ticket", accent));
  m_ticket.setFontWeight(QFont::DemiBold);
  m_tag.setForeground(colorAt(m_palette, "tag", accent));
  m_wiki.setForeground(accent);
  m_math.setForeground(colorAt(m_palette, "math", accent));
  m_math.setFontFixedPitch(true);
  m_highlight.setBackground(colorAt(m_palette, "highlightBg", QColor(0, 0, 0, 0)));
  m_frontmatter.setForeground(dim);
  m_frontmatter.setFontFixedPitch(true);
  m_rule.setForeground(dim);
  m_table.setForeground(dim);

  // Fenced code uses the same colours the docs viewer already speaks, so a
  // snippet looks the same wherever it is read.
  // On the code format, so a keyword keeps the code's face.
  m_codeKeyword = m_code;
  m_codeKeyword.setForeground(colorAt(m_palette, "keyword", accent));
  m_codeKeyword.setFontWeight(quiet ? QFont::Normal : QFont::DemiBold);
  m_codeString = m_code;
  m_codeString.setForeground(colorAt(m_palette, "string", QColor(0x9C, 0xC4, 0x8B)));
  m_codeComment = m_code;
  m_codeComment.setForeground(colorAt(m_palette, "comment", dim));
  m_codeComment.setFontItalic(true);
  m_codeNumber = m_code;
  m_codeNumber.setForeground(colorAt(m_palette, "number", QColor(0xD8, 0xC2, 0x77)));
  m_codeType = m_code;
  m_codeType.setForeground(colorAt(m_palette, "type", QColor(0x6C, 0xC4, 0xB8)));
  m_codeType.setFontWeight(quiet ? QFont::Normal : QFont::DemiBold);
  m_codeBuiltin = m_code;
  m_codeBuiltin.setForeground(colorAt(m_palette, "builtin", QColor(0xC0, 0x7A, 0xCF)));
  m_logError = m_code;
  m_logError.setForeground(colorAt(m_palette, "error", QColor(0xE5, 0x53, 0x4B)));
  m_logError.setFontWeight(QFont::DemiBold);
  m_logWarn = m_code;
  m_logWarn.setForeground(colorAt(m_palette, "warn", QColor(0xD8, 0xA7, 0x3A)));
  m_logWarn.setFontWeight(QFont::DemiBold);

  // Collapsed: a one-pixel, 1%-wide glyph in no colour. A fresh format, not
  // a merge: a heading's size adjustment kept "## " a heading's width.
  m_hidden = QTextCharFormat();
  m_hidden.setForeground(QColor(0, 0, 0, 0));
  m_hidden.setProperty(QTextFormat::FontPixelSize, 1);
  m_hidden.setFontStretch(1);
  m_ghost = QTextCharFormat();
  m_ghost.setForeground(QColor(0, 0, 0, 0));
}

namespace {

// Carries the open fence's language down every line inside it. It cannot be a
// member of the highlighter: highlightBlock() runs for one changed line at a
// time, so a member holds whichever fence was opened last during the most
// recent full pass, not the one this line is actually inside.
class FenceLanguage : public QTextBlockUserData {
 public:
  explicit FenceLanguage(QString lang) : language(std::move(lang)) {
  }

  QString language;
};

QString fenceLanguageOf(const QTextBlock& block) {
  if(const auto* data = dynamic_cast<const FenceLanguage*>(block.userData())) {
    return data->language;
  }
  return {};
}

// Which line-comment marker a fence's language actually uses. An unknown or
// absent language keeps the permissive union of all three, which is what every
// fence used to get.
const QRegularExpression& commentRxFor(const QString& language) {
  static const QRegularExpression anyRx(QStringLiteral("(//|#|--)[^\\n]*$"));
  static const QRegularExpression hashRx(QStringLiteral("#[^\\n]*$"));
  static const QRegularExpression slashRx(QStringLiteral("//[^\\n]*$"));
  static const QRegularExpression slashHashRx(QStringLiteral("(//|#)[^\\n]*$"));
  static const QRegularExpression dashRx(QStringLiteral("--[^\\n]*$"));
  static const QRegularExpression neverRx(QStringLiteral("(?!)"));

  static const QHash<QString, const QRegularExpression*> kByLanguage = {
      {QStringLiteral("python"), &hashRx},
      {QStringLiteral("py"), &hashRx},
      {QStringLiteral("ruby"), &hashRx},
      {QStringLiteral("rb"), &hashRx},
      {QStringLiteral("sh"), &hashRx},
      {QStringLiteral("bash"), &hashRx},
      {QStringLiteral("zsh"), &hashRx},
      {QStringLiteral("shell"), &hashRx},
      {QStringLiteral("yaml"), &hashRx},
      {QStringLiteral("yml"), &hashRx},
      {QStringLiteral("toml"), &hashRx},
      {QStringLiteral("ini"), &hashRx},
      {QStringLiteral("conf"), &hashRx},
      {QStringLiteral("cmake"), &hashRx},
      {QStringLiteral("make"), &hashRx},
      {QStringLiteral("makefile"), &hashRx},
      {QStringLiteral("dockerfile"), &hashRx},
      {QStringLiteral("perl"), &hashRx},
      {QStringLiteral("r"), &hashRx},
      {QStringLiteral("nix"), &hashRx},

      {QStringLiteral("c"), &slashRx},
      {QStringLiteral("cpp"), &slashRx},
      {QStringLiteral("c++"), &slashRx},
      {QStringLiteral("cxx"), &slashRx},
      {QStringLiteral("h"), &slashRx},
      {QStringLiteral("hpp"), &slashRx},
      {QStringLiteral("java"), &slashRx},
      {QStringLiteral("js"), &slashRx},
      {QStringLiteral("javascript"), &slashRx},
      {QStringLiteral("ts"), &slashRx},
      {QStringLiteral("typescript"), &slashRx},
      {QStringLiteral("jsx"), &slashRx},
      {QStringLiteral("tsx"), &slashRx},
      {QStringLiteral("go"), &slashRx},
      {QStringLiteral("rust"), &slashRx},
      {QStringLiteral("rs"), &slashRx},
      {QStringLiteral("kotlin"), &slashRx},
      {QStringLiteral("kt"), &slashRx},
      {QStringLiteral("swift"), &slashRx},
      {QStringLiteral("cs"), &slashRx},
      {QStringLiteral("csharp"), &slashRx},
      {QStringLiteral("scala"), &slashRx},
      {QStringLiteral("dart"), &slashRx},
      {QStringLiteral("qml"), &slashRx},
      {QStringLiteral("glsl"), &slashRx},
      {QStringLiteral("groovy"), &slashRx},

      {QStringLiteral("php"), &slashHashRx},

      {QStringLiteral("sql"), &dashRx},
      {QStringLiteral("lua"), &dashRx},
      {QStringLiteral("haskell"), &dashRx},
      {QStringLiteral("hs"), &dashRx},
      {QStringLiteral("ada"), &dashRx},

      // No line comment at all — greying out a `#` in JSON is pure noise.
      {QStringLiteral("json"), &neverRx},
      {QStringLiteral("csv"), &neverRx},
      {QStringLiteral("html"), &neverRx},
      {QStringLiteral("xml"), &neverRx},
      {QStringLiteral("css"), &neverRx},
      {QStringLiteral("text"), &neverRx},
      {QStringLiteral("plain"), &neverRx},
      {QStringLiteral("txt"), &neverRx},
      {QStringLiteral("diff"), &neverRx},
  };

  const auto it = kByLanguage.constFind(language.trimmed().toLower());
  return it == kByLanguage.constEnd() ? anyRx : **it;
}

}  // namespace

void MdHighlighter::highlightCodeLine(const QString& text, const QString& language) {
  setFormat(0, static_cast<int>(text.size()), m_code);
  if(text.isEmpty()) {
    return;
  }
  // A log (APP-315): its level, time, paths and quotes, as the drawn note
  // has them (LogSyntax).
  if(isLogLanguage(language)) {
    for(const LogSpan& s : logSpans(text)) {
      const QTextCharFormat* f = &m_codeString;
      switch(s.kind) {
        case LogKind::Error:
          f = &m_logError;
          break;
        case LogKind::Warn:
          f = &m_logWarn;
          break;
        case LogKind::Quiet:
          f = &m_codeComment;
          break;
        case LogKind::Time:
          f = &m_codeNumber;
          break;
        case LogKind::Path:
          f = &m_codeKeyword;
          break;
        case LogKind::Quoted:
          break;
      }
      setFormat(s.start, s.length, *f);
    }
    return;
  }

  // A language the docs viewer knows ("java", "sql", "yaml", …): its own
  // rules (CodeHighlighter), so a snippet in a note or a ticket's
  // description is coloured as in the docs viewer.
  if(CodeHighlighter::knowsLanguage(language)) {
    for(const CodeHighlighter::Span& span : CodeHighlighter::spans(text, language)) {
      const QTextCharFormat* f = &m_codeKeyword;
      switch(span.kind) {
        case CodeHighlighter::Kind::Comment:
          f = &m_codeComment;
          break;
        case CodeHighlighter::Kind::String:
          f = &m_codeString;
          break;
        case CodeHighlighter::Kind::Keyword:
          break;
        case CodeHighlighter::Kind::Number:
          f = &m_codeNumber;
          break;
        case CodeHighlighter::Kind::Type:
          f = &m_codeType;
          break;
        case CodeHighlighter::Kind::Builtin:
          f = &m_codeBuiltin;
          break;
      }
      setFormat(span.start, span.length, *f);
    }
    return;
  }

  // No language, or one the docs viewer does not know: deliberately coarse —
  // strings, comments and numbers, plus a handful of
  // words every language in a note is likely to use. A note is not an IDE,
  // and a per-keystroke highlighter cannot afford a real lexer per language.
  static const QRegularExpression stringRx(QStringLiteral("\"(?:[^\"\\\\\\n]|\\\\.)*\"|'(?:[^'\\\\\\n]|\\\\.)*'|`(?:[^`\\\\\\n]|\\\\.)*`"));
  static const QRegularExpression numberRx(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"));
  // Comment syntax is the one thing the union gets visibly wrong: with a
  // single (//|#|--) rule, a `#include` line greys out in a ```cpp fence and
  // the `//` of a URL does the same in ```python. The fence's language narrows
  // it; an unknown or absent language keeps the permissive union.
  const QRegularExpression& commentRx = commentRxFor(language);
  static const QRegularExpression keywordRx(
      QStringLiteral("\\b(?:if|else|for|while|return|class|struct|def|function|fn|let|const|var|import|from|"
                     "include|namespace|public|private|protected|static|void|int|bool|true|false|null|nullptr|"
                     "None|True|False|async|await|try|catch|except|finally|throw|raise|new|delete|this|self)\\b"));

  auto apply = [&](const QRegularExpression& rx, const QTextCharFormat& format) {
    auto it = rx.globalMatch(text);
    while(it.hasNext()) {
      const auto match = it.next();
      setFormat(static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()), format);
    }
  };

  apply(keywordRx, m_codeKeyword);
  apply(numberRx, m_codeNumber);
  // Strings and comments come last so a keyword inside one is not recoloured,
  // and between the two, whichever starts first wins: the # in
  // `echo "#1"` is inside a string, not the start of a comment.
  qsizetype pos = 0;
  while(pos < text.size()) {
    const auto s = stringRx.match(text, pos);
    const auto c = commentRx.match(text, pos);
    const bool haveS = s.hasMatch() && s.capturedLength() > 0;
    const bool haveC = c.hasMatch() && c.capturedLength() > 0;
    if(!haveS && !haveC) {
      break;
    }
    if(haveC && (!haveS || c.capturedStart() < s.capturedStart())) {
      setFormat(static_cast<int>(c.capturedStart()), static_cast<int>(c.capturedLength()), m_codeComment);
      break;  // a line comment runs to the end
    }
    setFormat(static_cast<int>(s.capturedStart()), static_cast<int>(s.capturedLength()), m_codeString);
    pos = s.capturedEnd();
  }
}

void MdHighlighter::highlightInline(const QString& text) {
  struct Rule {
    const QRegularExpression* pattern;
    const QTextCharFormat* format;
    int group;
  };

  static const QRegularExpression codeRx(QStringLiteral("`[^`\\n]+`"));
  static const QRegularExpression strongRx(QStringLiteral("\\*\\*[^*\\n]+\\*\\*|__[^_\\n]+__"));
  static const QRegularExpression emphasisRx(QStringLiteral("(?<![*\\w])\\*(?!\\s)[^*\\n]+\\*(?!\\w)|(?<![_\\w])_(?!\\s)[^_\\n]+_(?!\\w)"));
  static const QRegularExpression strikeRx(QStringLiteral("~~[^~\\n]+~~"));
  static const QRegularExpression highlightRx(QStringLiteral("==[^=\\n]+=="));
  static const QRegularExpression linkRx(QStringLiteral("!?\\[[^\\]\\n]*\\]\\([^)\\n]*\\)"));
  static const QRegularExpression wikiRx(QStringLiteral("\\[\\[[^\\]\\n]+\\]\\]"));
  static const QRegularExpression urlRx(QStringLiteral("\\bhttps?://[^\\s)\\]]+"));
  // Names and tags in any script: "@Олег_Т" is as much a mention as "@oleg".
  static const QRegularExpression mentionRx(QStringLiteral("(?<![\\p{L}\\p{N}_])@[\\p{L}\\p{N}_.-]+"));
  static const QRegularExpression ticketRx(QStringLiteral("(?<![A-Za-z0-9_])#[A-Z][A-Z0-9]*-\\d+"));
  static const QRegularExpression tagRx(QStringLiteral("(?<![\\p{L}\\p{N}_])#[\\p{L}][\\p{L}\\p{N}_/-]*"));
  static const QRegularExpression footnoteRx(QStringLiteral("\\[\\^[^\\]\\s]+\\]"));
  static const QRegularExpression kbdRx(QStringLiteral("<kbd>([^<\\n]*)</kbd>"), QRegularExpression::CaseInsensitiveOption);

  const Rule rules[] = {
      {&strongRx, &m_strong, 0},
      {&emphasisRx, &m_emphasis, 0},
      {&strikeRx, &m_strike, 0},
      {&highlightRx, &m_highlight, 0},
      {&wikiRx, &m_wiki, 0},
      {&linkRx, &m_link, 0},
      {&urlRx, &m_link, 0},
      {&ticketRx, &m_ticket, 0},
      {&tagRx, &m_tag, 0},
      {&mentionRx, &m_mention, 0},
      {&footnoteRx, &m_wiki, 0},
      {&kbdRx, &m_codeInline, 1},
  };

  for(const Rule& rule : rules) {
    auto it = rule.pattern->globalMatch(text);
    while(it.hasNext()) {
      const auto match = it.next();
      setFormat(static_cast<int>(match.capturedStart(rule.group)), static_cast<int>(match.capturedLength(rule.group)), *rule.format);
    }
  }

  for(const MathSpan& span : mathSpans(text)) {
    setFormat(span.start, span.length, m_math);
  }

  // Inline code last: what is inside backticks is not markdown, so nothing
  // else should have coloured it. "#tag" in a snippet is a comment.
  auto it = codeRx.globalMatch(text);
  while(it.hasNext()) {
    const auto match = it.next();
    setFormat(static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()), m_codeInline);
  }
}

void MdHighlighter::scheduleBlockLook() {
  if(m_lookPending || m_applyingLook) {
    return;
  }
  m_lookPending = true;
  // After the pass that called this: a block format set from inside
  // highlightBlock would edit the document the highlighter is walking.
  QMetaObject::invokeMethod(this, &MdHighlighter::applyBlockLook, Qt::QueuedConnection);
}

void MdHighlighter::applyBlockLook() {
  m_lookPending = false;
  QTextDocument* doc = document();
  if(doc == nullptr || m_applyingLook) {
    return;
  }

  struct Look {
    bool fixed = false;
    qreal height = 0;
    qreal indent = 0;
    qreal top = 0;
    qreal right = 0;
  };

  const bool on = m_livePreview && metric("leading") > 0;
  const qreal lineHeight = std::round(bodyPx() * metric("leading"));
  // What a line needs: the leading (a-02) for text, code and lists, a list
  // item's hanging indent (a-04), the picture's room (a-09). Tables, csv
  // grids and headings keep the font's own height: a grid's row is made
  // tall by its glyphs, and a fixed height would pile a heading's line
  // over the next.
  // The text column and the code column (Notes, dogfood 2026-10-19): the
  // editor is as wide as the wider; prose stops short of it by proseRight,
  // code, tables and csv grids by wideRight. In every mode, not just rich.
  const qreal proseRight = std::max<qreal>(0, metric("proseRight"));
  const qreal wideRight = std::max<qreal>(0, metric("wideRight"));
  const auto rightOf = [&](const QTextBlock& b) {
    const int state = b.userState();
    const int prev = b.previous().isValid() ? b.previous().userState() : static_cast<int>(Normal);
    const bool wide = state == InFence || prev == InFence || state == InIndentedCode || state == InTable;
    return wide ? wideRight : proseRight;
  };
  const auto lookOfText = [&](const QTextBlock& b) {
    Look look;
    if(!on) {
      return look;
    }
    const int state = b.userState();
    const int prev = b.previous().isValid() ? b.previous().userState() : static_cast<int>(Normal);
    const QString t = b.text();
    if(state == InFrontmatter || prev == InFrontmatter) {
      return look;
    }
    look.fixed = true;
    look.height = lineHeight;
    if(state == InFence || prev == InFence) {
      const QString language = fenceLanguageOf(state == InFence ? b : b.previous());
      if(!separatorForLanguage(language, QStringLiteral("a,b")).isNull()) {
        look.fixed = false;
      }
      return look;
    }
    if(state == InMathBlock || prev == InMathBlock || state == InIndentedCode) {
      return look;
    }
    qsizetype k = 0;
    while(k < t.size() && t.at(k).isSpace()) {
      ++k;
    }
    if(k == t.size()) {
      return look;
    }
    const QChar c = t.at(k);
    if(c == QLatin1Char('#') && atxHeadingRx().match(t).hasMatch()) {
      look.fixed = false;
    } else if(state == InTable) {
      look.fixed = false;
    } else if(const auto img = c == QLatin1Char('!') ? imageLineRx().match(t) : QRegularExpressionMatch(); img.hasMatch()) {
      look.fixed = false;
      // As tall as the picture drawn there once its size is known (its own
      // aspect, no wider than the column, dogfood 2026-10-30), so the lines
      // under it start under it; the stand-in's height until then.
      const qreal fit =
          m_metrics.value(QStringLiteral("figures")).toMap().value(img.captured(2)).toMap().value(QStringLiteral("h")).toReal();
      look.top = fit > 0 ? fit + metric("figureGap") : metric("figureSpace");
    } else if((c == QLatin1Char('=') || c == QLatin1Char('-')) && setextRx().match(t).hasMatch() && b.previous().isValid() &&
              !b.previous().text().trimmed().isEmpty()) {
      look.fixed = false;
    } else if((c == QLatin1Char('-') || c == QLatin1Char('*') || c == QLatin1Char('+') || c.isDigit()) &&
              !thematicBreakRx().match(t).hasMatch()) {
      if(const ListIndent li = listIndent(t); li.ok) {
        look.indent = li.indent;
      }
    }
    return look;
  };
  const auto lookOf = [&](const QTextBlock& b) {
    Look look = lookOfText(b);
    look.right = rightOf(b);
    return look;
  };
  const auto same = [](const QTextBlockFormat& f, const Look& w) {
    const auto eq = [](qreal a, qreal b) {
      return std::abs(a - b) < 0.01;
    };
    const bool fixed = f.lineHeightType() == QTextBlockFormat::FixedHeight;
    if(fixed != w.fixed || (!fixed && f.lineHeightType() != QTextBlockFormat::SingleHeight)) {
      return false;
    }
    return (!fixed || eq(f.lineHeight(), w.height)) && eq(f.leftMargin(), w.indent) && eq(f.textIndent(), -w.indent) &&
           eq(f.topMargin(), w.top) && eq(f.rightMargin(), w.right);
  };

  m_applyingLook = true;
  // Text set from code (setPlainText) leaves no undo history: the layout
  // must not become the first thing Ctrl+Z takes back. After an edit it
  // joins that edit, so one Ctrl+Z takes back both (as TextLeading does).
  const bool fresh = !doc->isUndoAvailable();
  bool changed = false;
  QTextCursor cursor(doc);
  for(QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
    const Look want = lookOf(b);
    if(same(b.blockFormat(), want)) {
      continue;
    }
    if(!changed) {
      cursor.joinPreviousEditBlock();
      changed = true;
    }
    QTextBlockFormat f = b.blockFormat();
    f.setLineHeight(want.fixed ? want.height : 0, want.fixed ? QTextBlockFormat::FixedHeight : QTextBlockFormat::SingleHeight);
    f.setLeftMargin(want.indent);
    f.setTextIndent(-want.indent);
    f.setTopMargin(want.top);
    f.setRightMargin(want.right);
    cursor.setPosition(b.position());
    cursor.setBlockFormat(f);
  }
  if(changed) {
    cursor.endEditBlock();
    if(fresh) {
      doc->clearUndoRedoStacks();
    }
  }
  m_applyingLook = false;
}

void MdHighlighter::highlightBlock(const QString& text) {
  const int previous = previousBlockState();
  const int length = static_cast<int>(text.size());
  scheduleBlockLook();

  // A header is a header by the line under it: this line turning into or
  // out of a |---| line changes what the line above is, and a highlighter
  // only goes on downwards. The line above is done again, once this pass ends.
  if(const QTextBlock above = currentBlock().previous(); above.isValid() && (above.userState() == Normal || above.userState() == InTable) &&
                                                         tableRowRx().match(above.text()).hasMatch() &&
                                                         above.previous().userState() != InTable) {
    const bool header = tableSeparatorRx().match(text).hasMatch();
    if(header != (above.userState() == InTable)) {
      QMetaObject::invokeMethod(
          this,
          [this, number = above.blockNumber()]() {
            if(document() != nullptr) {
              const QTextBlock b = document()->findBlockByNumber(number);
              if(b.isValid()) {
                rehighlightBlock(b);
              }
            }
          },
          Qt::QueuedConnection);
    }
  }

  // ── Multi-line state ────────────────────────────────────────────
  // A line on its own cannot tell code from prose, which is what the old
  // per-line highlighter got wrong.
  if(previous == InFrontmatter) {
    setFormat(0, length, m_frontmatter);
    setCurrentBlockState(text.trimmed() == QStringLiteral("---") || text.trimmed() == QStringLiteral("...") ? Normal : InFrontmatter);
    return;
  }
  if(currentBlock().blockNumber() == 0 && text.trimmed() == QStringLiteral("---")) {
    setFormat(0, length, m_frontmatter);
    setCurrentBlockState(InFrontmatter);
    return;
  }

  if(previous == InFence) {
    // The language was captured on the opening fence and relayed line by line;
    // reading it from the previous block is what lets a re-highlight of a
    // single line in the middle of a fence still know what it is inside.
    const QString language = fenceLanguageOf(currentBlock().previous());
    const auto fence = fenceRx().match(text);
    if(fence.hasMatch() && fence.captured(2).isEmpty()) {
      setFormat(0, length, m_codeFence);
      setCurrentBlockUserData(nullptr);
      setCurrentBlockState(Normal);
      if(hides()) {
        // Under a ```csv grid nothing of the fence shows, its box included.
        if(!separatorForLanguage(language, QStringLiteral("a,b")).isNull()) {
          setFormat(0, length, m_ghost);
        } else {
          ghost(0, length);
        }
      }
      return;
    }
    setCurrentBlockUserData(new FenceLanguage(language));
    setCurrentBlockState(InFence);
    if(hides() && !separatorForLanguage(language, QStringLiteral("a,b")).isNull()) {
      ghostTableRow(length, false);  // a ```csv record: the grid is drawn over it
      return;
    }
    highlightCodeLine(text, language);
    return;
  }

  if(previous == InMathBlock) {
    setFormat(0, length, m_math);
    setCurrentBlockState(text.trimmed() == QStringLiteral("$$") ? Normal : InMathBlock);
    if(hides()) {
      ghost(0, length);  // the formula is drawn over its lines
    }
    return;
  }

  const auto fence = fenceRx().match(text);
  if(fence.hasMatch()) {
    setFormat(0, length, m_codeFence);
    // The opening fence carries the language for every line that follows.
    setCurrentBlockUserData(new FenceLanguage(fence.captured(2)));
    setCurrentBlockState(InFence);
    if(hides()) {
      if(!separatorForLanguage(fence.captured(2), QStringLiteral("a,b")).isNull()) {
        setFormat(0, length, m_ghost);
      } else {
        ghost(0, length);
      }
    }
    return;
  }
  if(text.trimmed() == QStringLiteral("$$")) {
    setFormat(0, length, m_math);
    setCurrentBlockUserData(nullptr);
    setCurrentBlockState(InMathBlock);
    if(hides()) {
      ghost(0, length);
    }
    return;
  }

  // A line that is no longer inside a fence must not keep the language it had
  // when it was, or deleting a fence would leave the stale value behind for
  // whatever re-highlight reads this block next.
  setCurrentBlockUserData(nullptr);
  setCurrentBlockState(Normal);

  // Indented code: four spaces (or a tab) after a blank line or more such
  // code, and not under a list item, where the indent continues the item.
  // Set in the code's face, as decorations() draws its box.
  static const QRegularExpression indentedRx(QStringLiteral("^(?: {4}|\\t)\\S"));
  if(indentedRx.match(text).hasMatch()) {
    bool code = previous == InIndentedCode;
    if(!code && currentBlock().previous().isValid() && currentBlock().previous().text().trimmed().isEmpty()) {
      code = true;
      for(QTextBlock b = currentBlock().previous(); b.isValid(); b = b.previous()) {
        if(!b.text().trimmed().isEmpty()) {
          code = !listMarkerRx().match(b.text()).hasMatch() && !indentedRx.match(b.text()).hasMatch();
          break;
        }
      }
    }
    if(code) {
      setFormat(0, length, m_code);
      setCurrentBlockState(InIndentedCode);
      return;
    }
  }
  // A blank line inside indented code keeps it open for the next line.
  if(previous == InIndentedCode && text.trimmed().isEmpty()) {
    setCurrentBlockState(InIndentedCode);
    return;
  }

  // ── Line-level structure ────────────────────────────────────────
  const auto heading = atxHeadingRx().match(text);
  if(heading.hasMatch()) {
    const int level = static_cast<int>(heading.captured(1).size());
    QTextCharFormat format = m_heading[std::clamp(level, 1, 6) - 1];
    // Sized from the editor's own font, so its size setting stays the
    // baseline (a-07).
    format.setProperty(QTextFormat::FontPixelSize, headingPx(level));
    setFormat(0, length, format);
    highlightInline(text);
    if(m_livePreview && !revealed(currentBlock())) {
      hideMarkup(text);
      hideDrawn(text);
    }
    return;
  }

  const QString above = currentBlock().blockNumber() > 0 ? currentBlock().previous().text() : QString();
  if(setextRx().match(text).hasMatch() && !above.trimmed().isEmpty() && !quoteRx().match(above).hasMatch() &&
     !listMarkerRx().match(above).hasMatch() && !atxHeadingRx().match(above).hasMatch()) {
    // The underline of a setext heading. The old highlighter saw nothing here
    // at all, so such a heading looked like ordinary text with a rule under it.
    setFormat(0, length, m_heading[0]);
    return;
  }

  if(thematicBreakRx().match(text).hasMatch()) {
    setFormat(0, length, m_rule);
    if(hides()) {
      ghost(0, length);
    }
    return;
  }

  // Drawn as a table only where decorations() draws one: the rows under a
  // header, or a header with its |---| line under it. Any other "|" line is
  // text: hidden, nothing was drawn over it and the line was lost.
  const bool tableRow =
      tableRowRx().match(text).hasMatch() && (previous == InTable || tableSeparatorRx().match(currentBlock().next().text()).hasMatch());
  if(tableRow) {
    setCurrentBlockState(InTable);
    setFormat(0, length, m_table);
    highlightInline(text);
    const bool hidden = hides();
    // How it was drawn, for revealMoved to leave it be while that holds.
    setCurrentBlockUserData(new TableRowLook(hidden));
    if(hidden) {
      // The grid is drawn over it. A row's line is made taller (a big, narrow
      // invisible face) so the cells have room; the |---| line under the
      // header all but goes, being part of the header's box.
      static const QRegularExpression separatorRx(QStringLiteral("^\\s*\\|?(\\s*:?-+:?\\s*\\|)+\\s*(:?-+:?)?\\s*$"));
      ghostTableRow(length, separatorRx.match(text).hasMatch());
    }
    return;
  }

  const auto quote = quoteRx().match(text);
  if(quote.hasMatch() && quote.capturedLength() > 0) {
    // A callout ("> [!note] Title" and the quote under it) reads as a box of
    // plain text with a bold title, not as a quotation.
    const auto head = calloutRx().match(text);
    bool inCallout = head.hasMatch();
    for(QTextBlock b = currentBlock().previous(); !inCallout && b.isValid(); b = b.previous()) {
      const auto q = quoteRx().match(b.text());
      if(!q.hasMatch() || q.capturedLength() == 0) {
        break;
      }
      inCallout = calloutRx().match(b.text()).hasMatch();
    }
    if(inCallout) {
      QTextCharFormat body;
      body.setForeground(colorAt(m_palette, "text", QColor(0xE6, 0xE6, 0xE6)));
      setFormat(0, length, body);
      if(head.hasMatch()) {
        QTextCharFormat title = body;
        title.setFontWeight(QFont::DemiBold);
        setFormat(static_cast<int>(head.capturedEnd()), length - static_cast<int>(head.capturedEnd()), title);
      }
    } else {
      setFormat(0, length, m_quote);
    }
    setFormat(0, static_cast<int>(quote.capturedLength()), m_marker);
    highlightInline(text);
    if(hides()) {
      hideMarkup(text);
      hideDrawn(text);
      ghost(0, static_cast<int>(quote.capturedLength()));  // the bar is drawn there
    }
    return;
  }

  const auto list = listMarkerRx().match(text);
  // Rich measures a marker to lay the item's text on its column (a-04), so
  // it keeps the text's weight there.
  const QTextCharFormat& marker = m_livePreview ? m_markerLive : m_marker;
  if(list.hasMatch()) {
    setFormat(static_cast<int>(list.capturedStart(2)), static_cast<int>(list.capturedLength(2)), marker);
    if(!list.captured(4).isEmpty()) {
      setFormat(static_cast<int>(list.capturedStart(4)), static_cast<int>(list.capturedLength(4)), marker);
      // A finished task reads as finished, the way it does in the rendered
      // view.
      if(list.captured(5).toLower() == QStringLiteral("x")) {
        setFormat(static_cast<int>(list.capturedEnd(4)), length - static_cast<int>(list.capturedEnd(4)), m_taskDone);
      }
    }
  }

  highlightInline(text);
  // A picture's alt text is its caption, small and dim under it (a-09).
  const auto image = m_livePreview ? imageLineRx().match(text) : QRegularExpressionMatch();
  if(image.hasMatch() && image.capturedLength(1) > 0) {
    setFormat(static_cast<int>(image.capturedStart(1)), static_cast<int>(image.capturedLength(1)), m_caption);
  }
  if(hides()) {
    hideMarkup(text);
    hideDrawn(text);
    // The bullet or the box is drawn over its marker, which keeps its width.
    if(list.hasMatch() && !list.captured(2).at(0).isDigit()) {
      const int to = list.captured(4).isEmpty() ? static_cast<int>(list.capturedEnd(2)) : static_cast<int>(list.capturedEnd(4)) - 1;
      ghost(static_cast<int>(list.capturedStart(2)), to - static_cast<int>(list.capturedStart(2)));
    }
  }
  // The item's text on a fixed column, drawn or shown as source, so the
  // hanging indent its lines wrap to fits either way (a-04): the room goes
  // after the last space of the nesting and of the marker.
  if(m_livePreview && list.hasMatch()) {
    const ListIndent li = listIndent(text);
    if(li.leadEnd > 0) {
      spaceAfter(li.leadEnd - 1, li.leadSpacing);
    }
    spaceAfter(li.prefixEnd - 1, li.prefixSpacing);
  }
}

}  // namespace heap::md
