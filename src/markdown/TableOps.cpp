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
  // The |---| line counts as the header's.
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
        edit.ok = true;
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

  edit.ok = true;
  edit.lines << lineOf(header) << lineOf(align);
  for(const QStringList& r : body) {
    edit.lines << lineOf(r);
  }
  edit.caretLine = caretRow < 0 ? 0 : caretRow + 2;
  edit.caretOffset = offsetOfCell(caretRow < 0 ? header : body.at(caretRow), caretCol);
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
  if(!caretBlock.isValid() || !isTableLine(caretBlock.text())) {
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

int tableAnchor(QTextDocument* document, int position) {
  if(document == nullptr) {
    return -1;
  }
  const QTextBlock block = document->findBlock(position);
  if(!block.isValid()) {
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
