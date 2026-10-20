#include "markdown/TableOps.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace heap::md {

namespace {

bool isTableLine(const QString& line) {
  return line.trimmed().startsWith(QLatin1Char('|'));
}

bool isSeparatorLine(const QString& line) {
  static const QRegularExpression rx(QStringLiteral("^\\s*\\|?\\s*:?-+:?\\s*(\\|\\s*:?-+:?\\s*)*\\|?\\s*$"));
  return rx.match(line).hasMatch();
}

// A |---| cell's alignment, written back the same way.
QString alignOf(const QString& cell) {
  const QString c = cell.trimmed();
  const bool left = c.startsWith(QLatin1Char(':'));
  const bool right = c.endsWith(QLatin1Char(':')) && c.size() > 1;
  if(left && right) {
    return QStringLiteral(":---:");
  }
  if(right) {
    return QStringLiteral("---:");
  }
  if(left) {
    return QStringLiteral(":---");
  }
  return QStringLiteral("---");
}

QString lineOf(const QStringList& cells) {
  QString out = QStringLiteral("|");
  for(const QString& c : cells) {
    out += QLatin1Char(' ') + c + QStringLiteral(" |");
  }
  return out;
}

// Characters from the line's start to cell `column`'s text.
int offsetOfCell(const QStringList& cells, int column) {
  int at = 2;  // "| "
  for(int k = 0; k < column && k < cells.size(); ++k) {
    at += static_cast<int>(cells.at(k).size()) + 3;  // text, " | "
  }
  return at;
}

}  // namespace

std::optional<TableOp> tableOpFromName(const QString& name) {
  if(name == QLatin1String("rowAbove")) {
    return TableOp::RowAbove;
  }
  if(name == QLatin1String("rowBelow")) {
    return TableOp::RowBelow;
  }
  if(name == QLatin1String("columnLeft")) {
    return TableOp::ColumnLeft;
  }
  if(name == QLatin1String("columnRight")) {
    return TableOp::ColumnRight;
  }
  if(name == QLatin1String("deleteRow")) {
    return TableOp::DeleteRow;
  }
  if(name == QLatin1String("deleteColumn")) {
    return TableOp::DeleteColumn;
  }
  return std::nullopt;
}

QStringList tableCells(const QString& line) {
  QString l = line.trimmed();
  if(l.startsWith(QLatin1Char('|'))) {
    l.remove(0, 1);
  }
  if(l.endsWith(QLatin1Char('|')) && !l.endsWith(QLatin1String("\\|"))) {
    l.chop(1);
  }
  QStringList cells;
  QString cur;
  for(qsizetype k = 0; k < l.size(); ++k) {
    if(l.at(k) == QLatin1Char('\\') && k + 1 < l.size()) {
      cur += l.at(k);
      cur += l.at(k + 1);
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
}

TableLocation locateTable(const QStringList& lines, int line, int offset) {
  TableLocation loc;
  if(line < 0 || line >= lines.size() || !isTableLine(lines.at(line))) {
    return loc;
  }
  int first = line;
  while(first > 0 && isTableLine(lines.at(first - 1))) {
    --first;
  }
  int last = line;
  while(last + 1 < lines.size() && isTableLine(lines.at(last + 1))) {
    ++last;
  }
  if(first + 1 > last || !isSeparatorLine(lines.at(first + 1))) {
    return loc;
  }
  loc.ok = true;
  loc.firstLine = first;
  loc.lastLine = last;
  loc.row = line - first;
  // Unescaped pipes before the caret, the leading one not counted.
  const QString& l = lines.at(line);
  const int stop = std::clamp(offset, 0, static_cast<int>(l.size()));
  int pipes = 0;
  bool leading = true;
  for(int k = 0; k < stop; ++k) {
    const QChar c = l.at(k);
    if(c == QLatin1Char('\\')) {
      ++k;
      leading = false;
      continue;
    }
    if(c == QLatin1Char('|')) {
      if(leading) {
        leading = false;
        continue;
      }
      ++pipes;
    } else if(!c.isSpace()) {
      leading = false;
    }
  }
  const int columns = static_cast<int>(tableCells(lines.at(first)).size());
  loc.column = std::clamp(pipes, 0, std::max(0, columns - 1));
  return loc;
}

TableEdit editTable(const QStringList& tableLines, int row, int column, TableOp op) {
  TableEdit edit;
  if(tableLines.size() < 2) {
    return edit;
  }
  QStringList header = tableCells(tableLines.at(0));
  QStringList align;
  for(const QString& c : tableCells(tableLines.at(1))) {
    align << alignOf(c);
  }
  QList<QStringList> body;
  for(qsizetype k = 2; k < tableLines.size(); ++k) {
    body << tableCells(tableLines.at(k));
  }
  // One width for all: the widest row's.
  qsizetype columns = std::max(header.size(), align.size());
  for(const QStringList& r : body) {
    columns = std::max(columns, r.size());
  }
  header.resize(columns);
  while(align.size() < columns) {
    align << QStringLiteral("---");
  }
  for(QStringList& r : body) {
    r.resize(columns);
  }
  const int col = std::clamp(column, 0, static_cast<int>(columns) - 1);
  // The |---| line counts as the header's for adding rows; deleting it is
  // nothing (it is not a row of cells, and taking the header path there
  // would put the first row in the header's place).
  if(row == 1 && op == TableOp::DeleteRow) {
    return edit;
  }
  const int bodyRow = row >= 2 ? std::min(row - 2, static_cast<int>(body.size()) - 1) : -1;
  int caretRow = bodyRow;  // -1 header
  int caretCol = col;
  const QStringList emptyRow(columns);

  switch(op) {
    case TableOp::RowAbove:
      if(bodyRow < 0) {
        body.prepend(header);
        header = emptyRow;
        caretRow = -1;
      } else {
        body.insert(bodyRow, emptyRow);
      }
      break;
    case TableOp::RowBelow:
      body.insert(bodyRow + 1, emptyRow);
      caretRow = bodyRow + 1;
      break;
    case TableOp::DeleteRow:
      if(bodyRow < 0) {
        if(body.isEmpty()) {
          edit.ok = true;  // the only row: the table goes
          return edit;
        }
        header = body.takeFirst();
      } else {
        body.removeAt(bodyRow);
        caretRow = body.isEmpty() ? -1 : std::min(bodyRow, static_cast<int>(body.size()) - 1);
      }
      break;
    case TableOp::ColumnLeft:
    case TableOp::ColumnRight: {
      const int at = op == TableOp::ColumnLeft ? col : col + 1;
      header.insert(at, QString());
      align.insert(at, QStringLiteral("---"));
      for(QStringList& r : body) {
        r.insert(at, QString());
      }
      caretCol = at;
      break;
    }
    case TableOp::DeleteColumn:
      if(columns <= 1) {
        // The last column: nothing. Taking it would take every row's text
        // with it; a table goes by deleting its lines.
        return edit;
      }
      header.removeAt(col);
      align.removeAt(col);
      for(QStringList& r : body) {
        r.removeAt(col);
      }
      caretCol = std::min(col, static_cast<int>(header.size()) - 1);
      break;
  }

  // A table inside a list item keeps the item's indent on every line.
  const QString& head = tableLines.at(0);
  qsizetype indentSize = 0;
  while(indentSize < head.size() && head.at(indentSize).isSpace()) {
    ++indentSize;
  }
  const QString indent = head.left(indentSize);
  edit.ok = true;
  edit.lines << indent + lineOf(header) << indent + lineOf(align);
  for(const QStringList& r : body) {
    edit.lines << indent + lineOf(r);
  }
  edit.caretLine = caretRow < 0 ? 0 : caretRow + 2;
  edit.caretOffset = static_cast<int>(indentSize) + offsetOfCell(caretRow < 0 ? header : body.at(caretRow), caretCol);
  return edit;
}

Selection applyTableOp(QTextDocument* document, Selection selection, TableOp op, bool* applied) {
  if(applied != nullptr) {
    *applied = false;
  }
  if(document == nullptr) {
    return selection;
  }
  const QTextBlock caretBlock = document->findBlock(selection.caret());
  // A |…| example inside a code fence is code, not a table.
  if(!caretBlock.isValid() || !isTableLine(caretBlock.text()) || isInsideFence(document, selection.caret())) {
    return selection;
  }
  // The table's lines: the run of "|" lines around the caret.
  QTextBlock first = caretBlock;
  while(first.previous().isValid() && isTableLine(first.previous().text())) {
    first = first.previous();
  }
  QTextBlock last = caretBlock;
  while(last.next().isValid() && isTableLine(last.next().text())) {
    last = last.next();
  }
  QStringList lines;
  for(QTextBlock b = first; b.isValid(); b = b.next()) {
    lines << b.text();
    if(b == last) {
      break;
    }
  }
  const int caretLine = caretBlock.blockNumber() - first.blockNumber();
  const TableLocation loc = locateTable(lines, caretLine, selection.caret() - caretBlock.position());
  if(!loc.ok) {
    return selection;
  }
  const TableEdit edit = editTable(lines, loc.row, loc.column, op);
  if(!edit.ok) {
    return selection;
  }
  QTextCursor cursor(document);
  cursor.beginEditBlock();
  cursor.setPosition(first.position());
  cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
  int caret = first.position();
  if(edit.lines.isEmpty()) {
    // The whole table goes, with the line break after it (or before it, at
    // the end of the text).
    if(last.next().isValid()) {
      cursor.setPosition(last.next().position(), QTextCursor::KeepAnchor);
    } else if(first.previous().isValid()) {
      cursor.setPosition(last.position() + last.length() - 1);
      cursor.setPosition(first.position() - 1, QTextCursor::KeepAnchor);
      caret = first.position() - 1;
    }
    cursor.removeSelectedText();
  } else {
    cursor.insertText(edit.lines.join(QLatin1Char('\n')));
    for(int k = 0; k < edit.caretLine; ++k) {
      caret += static_cast<int>(edit.lines.at(k).size()) + 1;
    }
    caret += edit.caretOffset;
  }
  cursor.endEditBlock();
  if(applied != nullptr) {
    *applied = true;
  }
  caret = std::clamp(caret, 0, std::max(0, document->characterCount() - 1));
  return Selection::at(caret);
}

QPair<int, int> cellTextSpan(const QString& line, int column) {
  const int size = static_cast<int>(line.size());
  QList<int> bars;
  for(int k = 0; k < size; ++k) {
    if(line.at(k) == QLatin1Char('\\')) {
      ++k;
      continue;
    }
    if(line.at(k) == QLatin1Char('|')) {
      bars << k;
    }
  }
  // With a leading pipe, cell c lies between bars c and c + 1; without
  // one, between bars c - 1 and c (the first from the line's start).
  const bool lead = line.trimmed().startsWith(QLatin1Char('|'));
  const int fromBar = lead ? column : column - 1;
  const int toBar = lead ? column + 1 : column;
  int from = fromBar < 0 ? 0 : (fromBar < bars.size() ? bars.at(fromBar) + 1 : size);
  const int to = toBar < bars.size() ? bars.at(toBar) : size;
  from = std::min(from, to);
  int s = from;
  while(s < to && line.at(s).isSpace()) {
    ++s;
  }
  int e = to;
  while(e > s && line.at(e - 1).isSpace()) {
    --e;
  }
  if(s == e) {
    // An empty cell: one space in from its left bar, where "| x |" types.
    s = e = std::min(from + 1, to);
  }
  return {s, e};
}

namespace {

// The run of "|" lines around `block`, the table a move works in.
QPair<QTextBlock, QTextBlock> tableRun(const QTextBlock& block) {
  QTextBlock first = block;
  while(first.previous().isValid() && isTableLine(first.previous().text())) {
    first = first.previous();
  }
  QTextBlock last = block;
  while(last.next().isValid() && isTableLine(last.next().text())) {
    last = last.next();
  }
  return {first, last};
}

}  // namespace

Selection applyTableMove(QTextDocument* document, Selection selection, TableMove move, bool* applied) {
  if(applied != nullptr) {
    *applied = false;
  }
  if(document == nullptr) {
    return selection;
  }
  const QTextBlock caretBlock = document->findBlock(selection.caret());
  if(!caretBlock.isValid() || !isTableLine(caretBlock.text()) || isInsideFence(document, selection.caret()) ||
     document->findBlock(selection.start) != caretBlock) {
    return selection;
  }
  const auto [first, last] = tableRun(caretBlock);
  QStringList lines;
  for(QTextBlock b = first; b.isValid(); b = b.next()) {
    lines << b.text();
    if(b == last) {
      break;
    }
  }
  const TableLocation loc = locateTable(lines, caretBlock.blockNumber() - first.blockNumber(), selection.caret() - caretBlock.position());
  if(!loc.ok) {
    return selection;
  }
  if(applied != nullptr) {
    *applied = true;
  }
  const int rows = static_cast<int>(lines.size());
  const int columns = std::max(1, static_cast<int>(tableCells(lines.at(0)).size()));
  // The |---| line is the header's: moves go from there as from the header.
  int row = loc.row == 1 ? 0 : loc.row;
  int col = loc.column;
  const auto below = [](int r) {
    return r == 0 ? 2 : r + 1;
  };
  const auto above = [](int r) {
    return r == 2 ? 0 : r - 1;
  };

  // Under the table, a line of its own, kept apart from it (and from the
  // text under it) by a blank line, or the text would read as a row. `end`
  // is where the table's last row ends; the cursor's edit block is open.
  const auto leave = [document](QTextCursor& cursor, int end) {
    const QString all = document->toPlainText();
    const QString rest = all.mid(end);
    if(rest.trimmed().isEmpty()) {
      // Only blank lines under it: the second of them, made if missing.
      const int breaks = static_cast<int>(rest.count(QLatin1Char('\n')));
      if(breaks < 2) {
        cursor.setPosition(static_cast<int>(all.size()));
        cursor.insertText(QString(2 - breaks, QLatin1Char('\n')));
      }
    } else {
      // Text under it: a blank line, the caret's line, a blank line - the
      // line breaks already there counted in.
      int breaks = 0;
      while(breaks < rest.size() && rest.at(breaks) == QLatin1Char('\n')) {
        ++breaks;
      }
      if(breaks < 4) {
        cursor.setPosition(end);
        cursor.insertText(QString(4 - breaks, QLatin1Char('\n')));
      }
    }
    return Selection::at(end + 2);
  };

  if(move == TableMove::Exit) {
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    const Selection out = leave(cursor, last.position() + last.length() - 1);
    cursor.endEditBlock();
    return out;
  }

  // Enter on an empty last row, as on an empty list item: the row goes and
  // the caret leaves the table, so pressing Enter twice is the way out and
  // not an endless run of new rows. A table keeps at least one row.
  if(move == TableMove::NextRow && row >= 2 && row == rows - 1 && rows > 3) {
    const QStringList cells = tableCells(lines.at(row));
    if(std::all_of(cells.begin(), cells.end(), [](const QString& c) {
         return c.isEmpty();
       })) {
      // Positions, not blocks: an edit can drop the blocks it runs over.
      const int keepEnd = last.previous().position() + last.previous().length() - 1;
      QTextCursor cursor(document);
      cursor.beginEditBlock();
      cursor.setPosition(keepEnd);
      cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
      cursor.removeSelectedText();
      const Selection out = leave(cursor, keepEnd);
      cursor.endEditBlock();
      return out;
    }
  }

  bool addRow = false;
  switch(move) {
    case TableMove::NextCell:
      if(col + 1 < columns) {
        ++col;
      } else {
        col = 0;
        if(below(row) < rows) {
          row = below(row);
        } else {
          addRow = true;
        }
      }
      break;
    case TableMove::PreviousCell:
      if(col > 0) {
        --col;
      } else if(row > 0) {
        row = above(row);
        col = columns - 1;
      }
      break;
    case TableMove::NextRow:
      if(below(row) < rows) {
        row = below(row);
      } else {
        addRow = true;
      }
      break;
    case TableMove::AddRow:
      addRow = true;
      break;
    case TableMove::Exit:
      break;
  }

  if(addRow) {
    const TableEdit edit = editTable(lines, row, col, TableOp::RowBelow);
    if(!edit.ok) {
      return selection;
    }
    // The table's start as a position: the edit drops the blocks it replaces.
    const int start = first.position();
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    cursor.insertText(edit.lines.join(QLatin1Char('\n')));
    cursor.endEditBlock();
    int caret = start;
    for(int k = 0; k < edit.caretLine; ++k) {
      caret += static_cast<int>(edit.lines.at(k).size()) + 1;
    }
    return Selection::at(caret + cellTextSpan(edit.lines.at(edit.caretLine), col).first);
  }
  const QTextBlock target = document->findBlockByNumber(first.blockNumber() + row);
  return Selection::at(target.position() + cellTextSpan(target.text(), col).second);
}

int tableAnchor(QTextDocument* document, int position) {
  if(document == nullptr) {
    return -1;
  }
  const QTextBlock block = document->findBlock(position);
  if(!block.isValid() || isInsideFence(document, position)) {
    return -1;
  }
  const auto inTable = [](const QTextBlock& b) {
    QTextBlock first = b;
    while(first.previous().isValid() && isTableLine(first.previous().text())) {
      first = first.previous();
    }
    return first.next().isValid() && isTableLine(first.next().text()) && isSeparatorLine(first.next().text());
  };
  if(isTableLine(block.text())) {
    return inTable(block) ? position : -1;
  }
  const QTextBlock above = block.previous();
  if(block.text().trimmed().isEmpty() && above.isValid() && isTableLine(above.text()) && inTable(above)) {
    const qsizetype pipe = above.text().indexOf(QLatin1Char('|'));
    return above.position() + static_cast<int>(pipe) + 1;
  }
  return -1;
}

}  // namespace heap::md
