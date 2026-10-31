#include "markdown/MdEditOps.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <utility>

namespace heap::md {

namespace {

struct Markers {
  QString open;
  QString close;

  // QString reports sizes as qsizetype while a QTextDocument is addressed in
  // ints; converting once here keeps the arithmetic below readable.
  int openSize() const {
    return static_cast<int>(open.size());
  }

  int closeSize() const {
    return static_cast<int>(close.size());
  }
};

Markers markersFor(InlineStyle style) {
  switch(style) {
    case InlineStyle::Bold:
      return {QStringLiteral("**"), QStringLiteral("**")};
    case InlineStyle::Italic:
      return {QStringLiteral("_"), QStringLiteral("_")};
    case InlineStyle::Strikethrough:
      return {QStringLiteral("~~"), QStringLiteral("~~")};
    case InlineStyle::Code:
      return {QStringLiteral("`"), QStringLiteral("`")};
    case InlineStyle::Highlight:
      return {QStringLiteral("=="), QStringLiteral("==")};
  }
  return {};
}

// "- ", "* ", "+ ", "1. ", "> ", with any leading indent, and the checkbox if
// there is one. Kept as one expression so every op agrees on what a list line
// looks like.
const QRegularExpression& lineStructureRx() {
  static const QRegularExpression rx(
      QStringLiteral("^([ \\t]*)"                    // 1: indent
                     "(?:([-*+]|\\d+[.)])[ \\t]+)?"  // 2: list marker
                     "(?:\\[([ xX])\\][ \\t]+)?"     // 3: checkbox state
                     "(.*)$"));                      // 4: the rest
  return rx;
}

const QRegularExpression& quoteRx() {
  static const QRegularExpression rx(QStringLiteral("^([ \\t]*(?:>[ \\t]*)+)(.*)$"));
  return rx;
}

const QRegularExpression& headingRx() {
  static const QRegularExpression rx(QStringLiteral("^([ \\t]*)(#{1,6})[ \\t]+(.*)$"));
  return rx;
}

const QRegularExpression& urlRx() {
  static const QRegularExpression rx(QStringLiteral("^\\s*(?:https?://|mailto:|file://)\\S+\\s*$"));
  return rx;
}

QString lineTextAt(const QTextDocument* document, int position) {
  return document->findBlock(position).text();
}

int lineStartAt(const QTextDocument* document, int position) {
  return document->findBlock(position).position();
}

// Every block the selection touches, as a closed range of block numbers.
void blockRange(const QTextDocument* document, Selection selection, int* first, int* last) {
  const int lo = std::min(selection.start, selection.end);
  const int hi = std::max(selection.start, selection.end);
  *first = document->findBlock(lo).blockNumber();
  *last = document->findBlock(hi).blockNumber();
}

QTextCursor cursorFor(QTextDocument* document, Selection selection) {
  QTextCursor cursor(document);
  cursor.setPosition(std::min(selection.start, selection.end));
  cursor.setPosition(std::max(selection.start, selection.end), QTextCursor::KeepAnchor);
  return cursor;
}

}  // namespace

bool isInsideFence(QTextDocument* document, int position) {
  if(document == nullptr) {
    return false;
  }
  // Count fence openings above the caret: an odd number means one is still
  // open. Cheap, and it does not need a parse for a decision made per
  // keystroke.
  const int target = document->findBlock(position).blockNumber();
  int fences = 0;
  for(QTextBlock block = document->firstBlock(); block.isValid() && block.blockNumber() < target; block = block.next()) {
    const QString trimmed = block.text().trimmed();
    if(trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"))) {
      ++fences;
    }
  }
  return (fences % 2) == 1;
}

namespace {

// The part of a line that inline markers may wrap: past any quote, list
// marker, checkbox or heading hashes, and without the blanks at either end.
// "**- a**" is not a bold list item but a paragraph that starts with a dash.
void lineContent(const QString& line, int* begin, int* end) {
  int at = 0;
  const QRegularExpressionMatch quote = quoteRx().match(line);
  if(quote.hasMatch()) {
    at = static_cast<int>(quote.capturedLength(1));
  }
  const QString rest = line.mid(at);
  const QRegularExpressionMatch heading = headingRx().match(rest);
  if(heading.hasMatch()) {
    at += static_cast<int>(heading.capturedStart(3));
  } else {
    const QRegularExpressionMatch item = lineStructureRx().match(rest);
    if(item.hasMatch()) {
      at += static_cast<int>(item.capturedStart(4));
    }
  }
  int stop = static_cast<int>(line.size());
  while(at < stop && line.at(at).isSpace()) {
    ++at;
  }
  while(stop > at && line.at(stop - 1).isSpace()) {
    --stop;
  }
  *begin = at;
  *end = stop;
}

// A selection over several lines: the markers go round the selected part of
// each line's content, the way the Help promises — "- a\n- b" becomes
// "- **a**\n- **b**", not "**- a\n- b**", which no renderer reads as bold.
// When every such part is already wrapped, they are all unwrapped instead.
Selection toggleInlineStylePerLine(QTextDocument* document, int lo, int hi, const Markers& markers) {
  struct Segment {
    int start;
    int end;
  };

  QVector<Segment> segments;
  bool allWrapped = true;
  int firstBlock = 0;
  int lastBlock = 0;
  blockRange(document, Selection{lo, hi}, &firstBlock, &lastBlock);
  // Code fences and what is inside them are code, not prose to emphasise.
  // Counted the way isInsideFence() counts them.
  bool inFence = isInsideFence(document, document->findBlockByNumber(firstBlock).position());
  for(int b = firstBlock; b <= lastBlock; ++b) {
    const QTextBlock block = document->findBlockByNumber(b);
    const QString line = block.text();
    const QString trimmed = line.trimmed();
    if(trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"))) {
      inFence = !inFence;
      continue;
    }
    if(inFence) {
      continue;
    }
    int begin = 0;
    int end = 0;
    lineContent(line, &begin, &end);
    int start = std::max(lo, block.position() + begin);
    const int stop = std::min(hi, block.position() + end);
    while(start < stop && line.at(start - block.position()).isSpace()) {
      ++start;
    }
    int finish = stop;
    while(finish > start && line.at(finish - 1 - block.position()).isSpace()) {
      --finish;
    }
    if(start >= finish) {
      continue;
    }
    const QString part = line.mid(start - block.position(), finish - start);
    const bool wrapped =
        part.size() >= markers.openSize() + markers.closeSize() && part.startsWith(markers.open) && part.endsWith(markers.close);
    allWrapped = allWrapped && wrapped;
    segments.append({start, finish});
  }
  if(segments.isEmpty()) {
    return Selection{lo, hi};
  }

  QTextCursor cursor(document);
  cursor.beginEditBlock();
  int delta = 0;
  // Bottom up, so the positions above stay where they were.
  for(qsizetype i = segments.size() - 1; i >= 0; --i) {
    const Segment& s = segments.at(i);
    if(allWrapped) {
      cursor.setPosition(s.end - markers.closeSize());
      cursor.setPosition(s.end, QTextCursor::KeepAnchor);
      cursor.removeSelectedText();
      cursor.setPosition(s.start);
      cursor.setPosition(s.start + markers.openSize(), QTextCursor::KeepAnchor);
      cursor.removeSelectedText();
      delta -= markers.openSize() + markers.closeSize();
    } else {
      cursor.setPosition(s.end);
      cursor.insertText(markers.close);
      cursor.setPosition(s.start);
      cursor.insertText(markers.open);
      delta += markers.openSize() + markers.closeSize();
    }
  }
  cursor.endEditBlock();
  return Selection{lo, hi + delta};
}

}  // namespace

Selection toggleInlineStyle(QTextDocument* document, Selection selection, InlineStyle style) {
  if(document == nullptr) {
    return selection;
  }
  const Markers markers = markersFor(style);
  const int lo = std::min(selection.start, selection.end);
  const int hi = std::max(selection.start, selection.end);
  if(document->findBlock(lo).blockNumber() != document->findBlock(hi).blockNumber()) {
    return toggleInlineStylePerLine(document, lo, hi, markers);
  }
  const QString text = document->toPlainText();

  QTextCursor cursor(document);
  cursor.beginEditBlock();

  // Already wrapped, either inside the selection or just outside it? Then the
  // toggle takes the markers off, which is what makes it a toggle rather than
  // a way to pile up asterisks.
  const QString selected = text.mid(lo, hi - lo);
  if(selected.startsWith(markers.open) && selected.endsWith(markers.close) && selected.size() >= markers.openSize() + markers.closeSize()) {
    cursor.setPosition(lo);
    cursor.setPosition(hi, QTextCursor::KeepAnchor);
    cursor.insertText(selected.mid(markers.openSize(), static_cast<int>(selected.size()) - markers.openSize() - markers.closeSize()));
    cursor.endEditBlock();
    return Selection{lo, hi - markers.openSize() - markers.closeSize()};
  }

  const bool wrappedOutside = lo >= markers.openSize() && text.mid(lo - markers.openSize(), markers.openSize()) == markers.open &&
                              text.mid(hi, markers.closeSize()) == markers.close;
  if(wrappedOutside) {
    cursor.setPosition(hi);
    cursor.setPosition(hi + markers.closeSize(), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.setPosition(lo - markers.openSize());
    cursor.setPosition(lo, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    return Selection{lo - markers.openSize(), hi - markers.openSize()};
  }

  cursor.setPosition(hi);
  cursor.insertText(markers.close);
  cursor.setPosition(lo);
  cursor.insertText(markers.open);
  cursor.endEditBlock();

  if(selection.isEmpty()) {
    // No selection: put the caret between the markers so typing continues
    // inside them.
    const int inside = lo + markers.openSize();
    return Selection::at(inside);
  }
  return Selection{lo + markers.openSize(), hi + markers.openSize()};
}

Selection insertLink(QTextDocument* document, Selection selection, const QString& url) {
  if(document == nullptr) {
    return selection;
  }
  const int lo = std::min(selection.start, selection.end);
  const int hi = std::max(selection.start, selection.end);
  const QString selected = document->toPlainText().mid(lo, hi - lo);

  // Whichever of the two we have decides which half the caret lands in: the
  // missing one.
  const bool selectionIsUrl = urlRx().match(selected).hasMatch();
  const QString label = selectionIsUrl ? QString() : selected;
  const QString target = selectionIsUrl ? selected.trimmed() : url;

  const QString replacement = QStringLiteral("[%1](%2)").arg(label, target);

  QTextCursor cursor = cursorFor(document, selection);
  cursor.beginEditBlock();
  cursor.insertText(replacement);
  cursor.endEditBlock();

  if(label.isEmpty()) {
    return Selection::at(lo + 1);  // inside the empty label
  }
  if(target.isEmpty()) {
    return Selection::at(lo + static_cast<int>(label.size()) + 3);  // inside the empty target
  }
  return Selection::at(lo + static_cast<int>(replacement.size()));
}

Selection cycleHeading(QTextDocument* document, Selection selection) {
  if(document == nullptr) {
    return selection;
  }
  int first = 0;
  int last = 0;
  blockRange(document, selection, &first, &last);

  // The first line decides the level for all of them, so a selection moves as
  // one rather than each line cycling independently.
  const QString firstLine = document->findBlockByNumber(first).text();
  const auto match = headingRx().match(firstLine);
  const int currentLevel = match.hasMatch() ? match.captured(2).size() : 0;
  const int nextLevel = (currentLevel + 1) % 7;

  QTextCursor cursor(document);
  cursor.beginEditBlock();
  int delta = 0;
  for(int number = first; number <= last; ++number) {
    const QTextBlock block = document->findBlockByNumber(number);
    if(!block.isValid()) {
      continue;
    }
    const auto lineMatch = headingRx().match(block.text());
    const QString indent = lineMatch.hasMatch() ? lineMatch.captured(1) : QString();
    const QString body = lineMatch.hasMatch() ? lineMatch.captured(3) : block.text().trimmed();
    const QString replacement = nextLevel == 0 ? indent + body : indent + QString(nextLevel, QLatin1Char('#')) + QLatin1Char(' ') + body;

    cursor.setPosition(block.position());
    cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);
    delta += static_cast<int>(replacement.size() - block.text().size());
  }
  cursor.endEditBlock();
  return Selection{std::min(selection.start, selection.end), std::max(selection.start, selection.end) + delta};
}

Selection continueLine(QTextDocument* document, Selection selection, bool* handled) {
  if(handled != nullptr) {
    *handled = false;
  }
  if(document == nullptr) {
    return selection;
  }
  const int caret = selection.caret();
  const QString line = lineTextAt(document, caret);
  const int lineStart = lineStartAt(document, caret);
  const QString before = line.left(caret - lineStart);

  QTextCursor cursor(document);

  // Inside a fence, Enter keeps the current indentation and nothing else:
  // code continues, markdown structure does not apply.
  if(isInsideFence(document, caret)) {
    const auto indentMatch = QRegularExpression(QStringLiteral("^([ \\t]*)")).match(before);
    const QString indent = indentMatch.captured(1);
    if(indent.isEmpty()) {
      return selection;
    }
    cursor.setPosition(caret);
    cursor.beginEditBlock();
    cursor.insertText(QStringLiteral("\n") + indent);
    cursor.endEditBlock();
    if(handled != nullptr) {
      *handled = true;
    }
    return Selection::at(caret + 1 + static_cast<int>(indent.size()));
  }

  const auto quote = quoteRx().match(before);
  const auto structure = lineStructureRx().match(before);

  QString continuation;
  bool emptyItem = false;

  if(structure.hasMatch() && !structure.captured(2).isEmpty()) {
    const QString indent = structure.captured(1);
    const QString marker = structure.captured(2);
    const QString checkbox = structure.captured(3);
    emptyItem = structure.captured(4).trimmed().isEmpty();

    QString nextMarker = marker;
    // An ordered list keeps counting rather than repeating its first number.
    const auto ordered = QRegularExpression(QStringLiteral("^(\\d+)([.)])$")).match(marker);
    if(ordered.hasMatch()) {
      nextMarker = QString::number(ordered.captured(1).toInt() + 1) + ordered.captured(2);
    }
    continuation = indent + nextMarker + QLatin1Char(' ');
    if(!checkbox.isEmpty()) {
      // A new item starts unchecked: continuing a list is not the same as
      // repeating what was already done.
      continuation += QStringLiteral("[ ] ");
    }
  } else if(quote.hasMatch() && !quote.captured(1).trimmed().isEmpty()) {
    emptyItem = quote.captured(2).trimmed().isEmpty();
    continuation = quote.captured(1);
  } else {
    return selection;
  }

  cursor.beginEditBlock();
  if(emptyItem) {
    // Enter on an empty item ends the list instead of adding another one.
    cursor.setPosition(lineStart);
    cursor.setPosition(lineStart + static_cast<int>(before.size()), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    if(handled != nullptr) {
      *handled = true;
    }
    return Selection::at(lineStart);
  }

  // Splitting "- hello| world" must give "- world", not "-  world": the space
  // that separated the words would otherwise follow the new marker's own.
  const QString rest = line.mid(caret - lineStart);
  int gap = 0;
  while(gap < static_cast<int>(rest.size()) && (rest.at(gap) == u' ' || rest.at(gap) == u'\t')) {
    ++gap;
  }
  if(gap > 0 && gap < static_cast<int>(rest.size())) {
    cursor.setPosition(caret);
    cursor.setPosition(caret + gap, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
  }
  cursor.setPosition(caret);
  cursor.insertText(QStringLiteral("\n") + continuation);
  cursor.endEditBlock();
  if(handled != nullptr) {
    *handled = true;
  }
  return Selection::at(caret + 1 + static_cast<int>(continuation.size()));
}

Selection indentLines(QTextDocument* document, Selection selection, bool outdent) {
  if(document == nullptr) {
    return selection;
  }
  const int lo = std::min(selection.start, selection.end);

  // Inside a fence Tab is code indentation, so it inserts spaces at the caret
  // rather than shifting the line.
  if(!outdent && selection.isEmpty() && isInsideFence(document, lo)) {
    QTextCursor cursor(document);
    cursor.setPosition(lo);
    cursor.beginEditBlock();
    cursor.insertText(QStringLiteral("    "));
    cursor.endEditBlock();
    return Selection::at(lo + 4);
  }

  int first = 0;
  int last = 0;
  blockRange(document, selection, &first, &last);

  QTextCursor cursor(document);
  cursor.beginEditBlock();
  int firstDelta = 0;
  int totalDelta = 0;
  for(int number = first; number <= last; ++number) {
    const QTextBlock block = document->findBlockByNumber(number);
    if(!block.isValid() || block.text().trimmed().isEmpty()) {
      continue;
    }
    int delta = 0;
    if(outdent) {
      const QString text = block.text();
      int remove = 0;
      while(remove < 2 && remove < static_cast<int>(text.size()) && (text.at(remove) == u' ' || text.at(remove) == u'\t')) {
        ++remove;
      }
      if(remove == 0) {
        continue;
      }
      cursor.setPosition(block.position());
      cursor.setPosition(block.position() + remove, QTextCursor::KeepAnchor);
      cursor.removeSelectedText();
      delta = -remove;
    } else {
      cursor.setPosition(block.position());
      cursor.insertText(QStringLiteral("  "));
      delta = 2;
    }
    if(number == first) {
      firstDelta = delta;
    }
    totalDelta += delta;
  }
  cursor.endEditBlock();
  return Selection{std::max(0, lo + firstDelta), std::max(0, std::max(selection.start, selection.end) + totalDelta)};
}

Selection toggleTaskAtCaret(QTextDocument* document, Selection selection) {
  if(document == nullptr) {
    return selection;
  }
  const QTextBlock block = document->findBlock(selection.caret());
  if(!block.isValid()) {
    return selection;
  }
  const auto match = lineStructureRx().match(block.text());
  if(!match.hasMatch() || match.captured(2).isEmpty()) {
    return selection;  // not a list item, so there is nothing to tick
  }

  QTextCursor cursor(document);
  cursor.beginEditBlock();
  if(match.captured(3).isEmpty()) {
    // A plain item gains a box rather than doing nothing: that is what the
    // key was pressed for.
    const int insertAt = block.position() + match.capturedEnd(2) + 1;
    cursor.setPosition(insertAt);
    cursor.insertText(QStringLiteral("[ ] "));
    cursor.endEditBlock();
    return Selection::at(selection.caret() + 4);
  }
  const int markAt = block.position() + match.capturedStart(3);
  cursor.setPosition(markAt);
  cursor.setPosition(markAt + 1, QTextCursor::KeepAnchor);
  cursor.insertText(match.captured(3) == QStringLiteral(" ") ? QStringLiteral("x") : QStringLiteral(" "));
  cursor.endEditBlock();
  return selection;
}

Selection pasteText(QTextDocument* document, Selection selection, const QString& pasted) {
  if(document == nullptr) {
    return selection;
  }
  // A URL pasted over selected text is nearly always meant as "link this".
  if(!selection.isEmpty() && urlRx().match(pasted).hasMatch()) {
    return insertLink(document, selection, pasted.trimmed());
  }

  QTextCursor cursor = cursorFor(document, selection);
  cursor.beginEditBlock();
  cursor.insertText(pasted);
  cursor.endEditBlock();
  return Selection::at(std::min(selection.start, selection.end) + static_cast<int>(pasted.size()));
}

namespace {

// What a line is to a list: an item (numbered or not), a line inside an
// item (indented), a gap, or something that ends the list.
struct ListLine {
  enum Kind { Ordered, Bullet, Inside, Blank, Other };

  Kind kind = Other;
  int indent = 0;     // in columns, a tab as four
  int number = 0;     // Ordered: the number written
  int numberAt = 0;   // Ordered: where the digits start in the line
  int numberLen = 0;  // Ordered: how many digits
  QChar delimiter;    // Ordered: '.' or ')'
};

ListLine classifyListLine(const QString& text) {
  static const QRegularExpression orderedRx(QStringLiteral("^([ \\t]*)(\\d{1,9})([.)])(?=[ \\t]|$)"));
  static const QRegularExpression bulletRx(QStringLiteral("^([ \\t]*)[-*+](?=[ \\t]|$)"));
  const auto columns = [](const QString& indent) {
    int width = 0;
    for(const QChar ch : indent) {
      width += ch == u'\t' ? 4 : 1;
    }
    return width;
  };
  ListLine line;
  if(text.trimmed().isEmpty()) {
    line.kind = ListLine::Blank;
    return line;
  }
  if(const auto m = orderedRx.match(text); m.hasMatch()) {
    line.kind = ListLine::Ordered;
    line.indent = columns(m.captured(1));
    line.number = m.captured(2).toInt();
    line.numberAt = static_cast<int>(m.capturedStart(2));
    line.numberLen = static_cast<int>(m.capturedLength(2));
    line.delimiter = m.captured(3).at(0);
    return line;
  }
  if(const auto m = bulletRx.match(text); m.hasMatch()) {
    line.kind = ListLine::Bullet;
    line.indent = columns(m.captured(1));
    return line;
  }
  if(text.at(0) == u' ' || text.at(0) == u'\t') {
    int k = 0;
    while(k < text.size() && (text.at(k) == u' ' || text.at(k) == u'\t')) {
      ++k;
    }
    line.kind = ListLine::Inside;
    line.indent = columns(text.left(k));
    return line;
  }
  return line;
}

}  // namespace

bool renumberOrderedList(QTextDocument* document, int position, bool joinPrevious) {
  if(document == nullptr || isInsideFence(document, position)) {
    return false;
  }
  const QTextBlock at = document->findBlock(position);
  if(!at.isValid()) {
    return false;
  }
  // The run of list lines around the edit: items, lines inside them and the
  // gaps between them. Anything else (a paragraph, a heading, a fence) ends it.
  const auto inList = [](const QTextBlock& b) {
    return classifyListLine(b.text()).kind != ListLine::Other;
  };
  if(!inList(at)) {
    return false;
  }
  QTextBlock first = at;
  while(first.previous().isValid() && inList(first.previous())) {
    first = first.previous();
  }

  // Each indent level counts on its own: a nested list starts again from its
  // own first number and the outer one picks up where it left off.
  struct Level {
    int indent = 0;
    bool ordered = false;
    QChar delimiter;
    int counter = 0;
  };

  QList<Level> levels;

  struct Fix {
    int block = 0;
    int at = 0;
    int length = 0;
    QString number;
  };

  QList<Fix> fixes;
  bool gap = false;
  for(QTextBlock b = first; b.isValid() && inList(b); b = b.next()) {
    const ListLine line = classifyListLine(b.text());
    if(line.kind == ListLine::Blank) {
      gap = true;
      continue;
    }
    const bool afterGap = std::exchange(gap, false);
    if(line.kind == ListLine::Inside) {
      // Text inside an item at or left of a nested list's marker ends it.
      while(!levels.isEmpty() && levels.last().indent >= line.indent) {
        levels.removeLast();
      }
      continue;
    }
    while(!levels.isEmpty() && levels.last().indent > line.indent) {
      levels.removeLast();
    }
    const bool sameLevel = !levels.isEmpty() && levels.last().indent == line.indent;
    if(line.kind == ListLine::Bullet) {
      const Level bullet{line.indent, false, QChar(), 0};
      if(sameLevel) {
        levels.last() = bullet;
      } else {
        levels.append(bullet);
      }
      continue;
    }
    // A "1." after an empty line is a new list, not the old one's next item:
    // two lists a blank line apart stay two lists.
    const bool restarts = afterGap && line.number == 1;
    const bool continues = sameLevel && levels.last().ordered && levels.last().delimiter == line.delimiter && !restarts;
    if(!continues) {
      // The first item sets where the list counts from.
      const Level start{line.indent, true, line.delimiter, line.number};
      if(sameLevel) {
        levels.last() = start;
      } else {
        levels.append(start);
      }
      continue;
    }
    // Only from the edited line down: what is above it was not touched, and
    // the count goes on from the number it actually shows.
    if(b.blockNumber() < at.blockNumber()) {
      levels.last().counter = line.number;
      continue;
    }
    const int expected = levels.last().counter + 1;
    levels.last().counter = expected;
    if(line.number != expected) {
      fixes.append(Fix{b.blockNumber(), line.numberAt, line.numberLen, QString::number(expected)});
    }
  }
  if(fixes.isEmpty()) {
    return false;
  }

  QTextCursor cursor(document);
  if(joinPrevious) {
    cursor.joinPreviousEditBlock();
  } else {
    cursor.beginEditBlock();
  }
  for(const Fix& fix : fixes) {
    const QTextBlock block = document->findBlockByNumber(fix.block);
    cursor.setPosition(block.position() + fix.at);
    cursor.setPosition(block.position() + fix.at + fix.length, QTextCursor::KeepAnchor);
    cursor.insertText(fix.number);
  }
  cursor.endEditBlock();
  return true;
}

}  // namespace heap::md
