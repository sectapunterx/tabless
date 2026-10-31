#include "markdown/MdEditOps.h"
#include "markdown/TableCells.h"
#include "markdown/TableOps.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace heap::md {

namespace {

// As the highlighter draws a table: lines between pipes, the second the
// |---| line. A "|" line without its closing pipe stays text in view, so
// the grid's keys leave it alone too.
bool isTableLine(const QString& line) {
  static const QRegularExpression rx(QStringLiteral("^\\s*\\|.*\\|\\s*$"));
  return rx.match(line).hasMatch();
}

bool isSeparatorLine(const QString& line) {
  static const QRegularExpression rx(QStringLiteral("^\\s*\\|?\\s*:?-+:?\\s*(\\|\\s*:?-+:?\\s*)*\\|?\\s*$"));
  return rx.match(line).hasMatch();
}

bool isEscapedPipe(const QString& s, qsizetype at) {
  return at >= 0 && at + 1 < s.size() && s.at(at) == QLatin1Char('\\') && s.at(at + 1) == QLatin1Char('|');
}

struct Run {
  QTextBlock first;
  QTextBlock last;
};

// The table `block` is a line of, when the note draws one there.
Run tableRun(QTextDocument* document, const QTextBlock& block) {
  if(!block.isValid() || !isTableLine(block.text()) || isInsideFence(document, block.position())) {
    return {};
  }
  QTextBlock first = block;
  while(first.previous().isValid() && isTableLine(first.previous().text())) {
    first = first.previous();
  }
  QTextBlock last = block;
  while(last.next().isValid() && isTableLine(last.next().text())) {
    last = last.next();
  }
  if(first == last || !isSeparatorLine(first.next().text())) {
    return {};
  }
  return {first, last};
}

// Cell `column` of `block` (a row of `run`), its text span filled in.
TableCell cellOf(const Run& run, const QTextBlock& block, int column) {
  TableCell c;
  const QTextBlock row = block.blockNumber() == run.first.blockNumber() + 1 ? run.first : block;
  const QString text = row.text();
  const int inRow = static_cast<int>(tableCells(text).size());
  c.ok = true;
  c.tableStart = run.first.position();
  c.tableEnd = run.last.position() + run.last.length() - 1;
  c.lines = run.last.blockNumber() - run.first.blockNumber() + 1;
  c.columns = static_cast<int>(tableCells(run.first.text()).size());
  c.line = row.blockNumber() - run.first.blockNumber();
  c.column = std::clamp(column, 0, std::max(0, std::min(c.columns, inRow) - 1));
  const auto [s, e] = cellTextSpan(text, c.column);
  c.textStart = row.position() + s;
  c.textEnd = row.position() + e;
  c.raw = text.mid(s, e - s);
  return c;
}

// The line above / under a row of the table (the |---| line is skipped).
int lineAbove(int line) {
  return line == 2 ? 0 : line - 1;
}

int lineBelow(int line) {
  return line == 0 ? 2 : line + 1;
}

CellMove at(int position) {
  return CellMove{true, position, position};
}

// Out of the table's top: the end of the line above; none, the caret stays.
CellMove leaveAbove(QTextDocument* document, const TableCell& c) {
  const QTextBlock above = document->findBlock(c.tableStart).previous();
  return at(above.isValid() ? above.position() + above.length() - 1 : c.textStart);
}

// Out of its bottom: the start of the line under it, made as Shift+Enter
// makes it (a blank line between) when the table ends the text.
CellMove leaveBelow(QTextDocument* document, const TableCell& c) {
  const QTextBlock below = document->findBlock(c.tableEnd).next();
  if(below.isValid()) {
    return at(below.position());
  }
  return at(applyTableMove(document, Selection::at(c.textEnd), TableMove::Exit).caret());
}

}  // namespace

QString cellDisplayText(const QString& raw) {
  QString out = raw;
  out.replace(QStringLiteral("\\|"), QStringLiteral("|"));
  return out;
}

QString cellSourceText(const QString& typed) {
  QString out;
  out.reserve(typed.size());
  for(const QChar ch : typed) {
    if(ch == QLatin1Char('|')) {
      out += QStringLiteral("\\|");
    } else if(ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QLatin1Char('\t') || ch == QChar::ParagraphSeparator ||
              ch == QChar::LineSeparator) {
      out += QLatin1Char(' ');
    } else {
      out += ch;
    }
  }
  return out;
}

int cellDisplayOffset(const QString& raw, int rawOffset) {
  int shown = 0;
  for(qsizetype k = 0; k < raw.size() && k < rawOffset; ++k) {
    if(isEscapedPipe(raw, k)) {
      ++k;  // inside the pair reads as before its "|"
      if(k >= rawOffset) {
        break;
      }
    }
    ++shown;
  }
  return shown;
}

int cellRawOffset(const QString& raw, int displayOffset) {
  qsizetype k = 0;
  for(int shown = 0; k < raw.size() && shown < displayOffset; ++shown) {
    k += isEscapedPipe(raw, k) ? 2 : 1;
  }
  return static_cast<int>(std::min(k, raw.size()));
}

TableCell tableCellAt(QTextDocument* document, int position) {
  if(document == nullptr) {
    return {};
  }
  const QTextBlock block = document->findBlock(position);
  const Run run = tableRun(document, block);
  if(!run.first.isValid()) {
    return {};
  }
  QStringList lines;
  for(QTextBlock b = run.first; b.isValid(); b = b.next()) {
    lines << b.text();
    if(b == run.last) {
      break;
    }
  }
  const int line = block.blockNumber() - run.first.blockNumber();
  const TableLocation loc = locateTable(lines, line, position - block.position());
  return cellOf(run, block, loc.ok ? loc.column : 0);
}

TableCell tableCell(QTextDocument* document, int tableStart, int line, int column) {
  if(document == nullptr) {
    return {};
  }
  const QTextBlock head = document->findBlock(tableStart);
  const Run run = tableRun(document, head);
  if(!run.first.isValid() || run.first != head) {
    return {};
  }
  const int lines = run.last.blockNumber() - run.first.blockNumber() + 1;
  const QTextBlock row = document->findBlockByNumber(run.first.blockNumber() + std::clamp(line, 0, lines - 1));
  return cellOf(run, row, column);
}

CellMove applyCellKey(QTextDocument* document, int anchor, int position, CellKey key, bool extend) {
  const TableCell c = tableCellAt(document, position);
  if(!c.ok) {
    return {};
  }
  const TableCell a = anchor == position ? c : tableCellAt(document, anchor);
  const bool sameTable = a.ok && a.tableStart == c.tableStart;
  const bool oneCell = sameTable && a.line == c.line && a.column == c.column;
  const bool collapsed = anchor == position;
  const int p = std::clamp(position, c.textStart, c.textEnd);
  const int columnsHere = static_cast<int>(tableCells(document->findBlock(c.textStart).text()).size());
  const auto keepAnchor = [&](int to) {
    return CellMove{true, extend ? anchor : to, to};
  };

  switch(key) {
    case CellKey::Escape:
      return leaveBelow(document, c);
    case CellKey::Up:
      return c.line == 0 ? leaveAbove(document, c) : at(tableCell(document, c.tableStart, lineAbove(c.line), c.column).textEnd);
    case CellKey::Down:
      if(lineBelow(c.line) >= c.lines) {
        return leaveBelow(document, c);
      }
      return at(tableCell(document, c.tableStart, lineBelow(c.line), c.column).textEnd);
    case CellKey::Home:
      return keepAnchor(c.textStart);
    case CellKey::End:
      return keepAnchor(c.textEnd);
    case CellKey::Left:
    case CellKey::Right: {
      const bool left = key == CellKey::Left;
      if(!extend && !collapsed) {
        return at(left ? std::min(anchor, position) : std::max(anchor, position));
      }
      const QString text = document->toPlainText();
      if(left && p > c.textStart) {
        return keepAnchor(p - (isEscapedPipe(text, p - 2) ? 2 : 1));
      }
      if(!left && p < c.textEnd) {
        return keepAnchor(p + (isEscapedPipe(text, p) ? 2 : 1));
      }
      // Over the cell's edge: a selection stops there; the caret goes on
      // into the next cell, in reading order, and out of the table.
      if(extend) {
        return keepAnchor(p);
      }
      if(left) {
        if(c.column > 0) {
          return at(tableCell(document, c.tableStart, c.line, c.column - 1).textEnd);
        }
        if(c.line > 0) {
          return at(tableCell(document, c.tableStart, lineAbove(c.line), c.columns - 1).textEnd);
        }
        return leaveAbove(document, c);
      }
      if(c.column + 1 < std::min(c.columns, columnsHere)) {
        return at(tableCell(document, c.tableStart, c.line, c.column + 1).textStart);
      }
      if(lineBelow(c.line) < c.lines) {
        return at(tableCell(document, c.tableStart, lineBelow(c.line), 0).textStart);
      }
      return leaveBelow(document, c);
    }
    case CellKey::Backspace:
    case CellKey::Delete: {
      if(!collapsed) {
        // Inside one cell the selection goes as in any text; over several
        // it would take the pipes between them, so nothing. A selection out
        // of the table is the field's.
        return oneCell || !sameTable ? CellMove{} : CellMove{true, anchor, position};
      }
      const bool back = key == CellKey::Backspace;
      if(back ? position <= c.textStart : position >= c.textEnd) {
        return CellMove{true, anchor, position};
      }
      // An escaped "|" goes whole: half of it escaped the next character.
      const QString text = document->toPlainText();
      const int from = back ? position - 2 : position;
      if(isEscapedPipe(text, from) && from >= c.textStart && from + 2 <= c.textEnd) {
        QTextCursor cursor(document);
        cursor.setPosition(from);
        cursor.setPosition(from + 2, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        return at(from);
      }
      return {};
    }
  }
  return {};
}

CellMove enterTable(QTextDocument* document, int position, bool down) {
  if(document == nullptr) {
    return {};
  }
  const QTextBlock block = document->findBlock(position);
  if(!block.isValid() || isTableLine(block.text())) {
    return {};
  }
  const QTextBlock next = down ? block.next() : block.previous();
  const TableCell c = next.isValid() ? tableCellAt(document, next.position()) : TableCell{};
  if(!c.ok) {
    return {};
  }
  return at(tableCell(document, c.tableStart, down ? 0 : c.lines - 1, 0).textStart);
}

CellMove typeIntoCell(QTextDocument* document, int anchor, int position, const QString& text) {
  const TableCell c = tableCellAt(document, position);
  if(!c.ok || text.isEmpty()) {
    return {};
  }
  if(anchor != position) {
    const TableCell a = tableCellAt(document, anchor);
    const bool sameTable = a.ok && a.tableStart == c.tableStart;
    if(!sameTable) {
      return CellMove{false, anchor, position};
    }
    if(a.line != c.line || a.column != c.column) {
      return CellMove{true, anchor, position};
    }
  }
  const bool collapsed = anchor == position;
  const int p = collapsed ? std::clamp(position, c.textStart, c.textEnd) : position;
  // "|" at a cell's end types over the row's own pipe (TableOps), so a
  // row typed out raw still lands in its cells.
  if(text == QStringLiteral("|") && collapsed) {
    const QTextBlock row = document->findBlock(c.textStart);
    const QTextBlock head = document->findBlock(c.tableStart);
    QStringList lines;
    for(QTextBlock b = head; b.isValid(); b = b.next()) {
      lines << b.text();
      if(b.position() + b.length() - 1 >= c.tableEnd) {
        break;
      }
    }
    if(pipeTypeOver(lines, row.blockNumber() - head.blockNumber(), position - row.position()) >= 0) {
      return CellMove{false, anchor, position};
    }
  }
  const QString source = cellSourceText(text);
  if(source == text) {
    return CellMove{false, collapsed ? p : anchor, p};
  }
  QTextCursor cursor(document);
  cursor.beginEditBlock();
  cursor.setPosition(collapsed ? p : anchor);
  cursor.setPosition(p, QTextCursor::KeepAnchor);
  cursor.insertText(source);
  cursor.endEditBlock();
  return at(cursor.position());
}

int addTableColumn(QTextDocument* document, int tableStart) {
  const TableCell head = tableCell(document, tableStart, 0, 0);
  if(!head.ok) {
    return -1;
  }
  const TableCell last = tableCell(document, tableStart, 0, head.columns - 1);
  bool applied = false;
  const Selection out = applyTableOp(document, Selection::at(last.textStart), TableOp::ColumnRight, &applied);
  return applied ? out.caret() : -1;
}

int addTableRow(QTextDocument* document, int tableStart) {
  const TableCell head = tableCell(document, tableStart, 0, 0);
  if(!head.ok) {
    return -1;
  }
  const TableCell last = tableCell(document, tableStart, head.lines - 1, 0);
  bool applied = false;
  const Selection out = applyTableOp(document, Selection::at(last.textStart), TableOp::RowBelow, &applied);
  return applied ? out.caret() : -1;
}

}  // namespace heap::md
