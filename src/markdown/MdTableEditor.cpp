#include "markdown/MdTableEditor.h"
#include "markdown/TableCells.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>

namespace heap::md {

MdTableEditor::MdTableEditor(QObject* parent) : QObject(parent) {
}

MdTableEditor::~MdTableEditor() {
  if(m_editor != nullptr) {
    m_editor->removeEventFilter(this);
  }
}

void MdTableEditor::setEditor(QQuickItem* editor) {
  if(m_editor == editor) {
    return;
  }
  if(m_editor != nullptr) {
    m_editor->removeEventFilter(this);
  }
  m_editor = editor;
  if(m_editor != nullptr) {
    m_editor->installEventFilter(this);
  }
  emit editorChanged();
}

void MdTableEditor::setEnabled(bool on) {
  if(m_enabled == on) {
    return;
  }
  m_enabled = on;
  emit enabledChanged();
}

QTextDocument* MdTableEditor::document() const {
  if(m_editor == nullptr) {
    return nullptr;
  }
  auto* doc = m_editor->property("textDocument").value<QQuickTextDocument*>();
  return doc != nullptr ? doc->textDocument() : nullptr;
}

int MdTableEditor::cursorPosition() const {
  return m_editor != nullptr ? m_editor->property("cursorPosition").toInt() : 0;
}

int MdTableEditor::anchorPosition() const {
  if(m_editor == nullptr) {
    return 0;
  }
  const int start = m_editor->property("selectionStart").toInt();
  const int end = m_editor->property("selectionEnd").toInt();
  const int caret = cursorPosition();
  if(start == end) {
    return caret;
  }
  return caret == start ? end : start;
}

void MdTableEditor::select(int anchor, int position) {
  if(m_editor == nullptr) {
    return;
  }
  if(anchor == position) {
    m_editor->setProperty("cursorPosition", position);
  } else {
    QMetaObject::invokeMethod(m_editor, "select", Q_ARG(int, anchor), Q_ARG(int, position));
  }
}

QVariantMap MdTableEditor::caretCell() const {
  QTextDocument* doc = document();
  if(doc == nullptr) {
    return {{"ok", false}};
  }
  const int position = cursorPosition();
  const int anchor = anchorPosition();
  const TableCell c = tableCellAt(doc, position);
  if(!c.ok) {
    return {{"ok", false}};
  }
  const TableCell a = anchor == position ? c : tableCellAt(doc, anchor);
  const auto shown = [&c](int pos) {
    return cellDisplayOffset(c.raw, std::clamp(pos, c.textStart, c.textEnd) - c.textStart);
  };
  QVariantMap out{{"ok", true},
                  {"tableStart", c.tableStart},
                  {"line", c.line},
                  {"column", c.column},
                  {"text", cellDisplayText(c.raw)},
                  {"caret", shown(position)}};
  if(a.ok && a.tableStart == c.tableStart && a.line == c.line && a.column == c.column) {
    out.insert(QStringLiteral("selectionStart"), shown(std::min(anchor, position)));
    out.insert(QStringLiteral("selectionEnd"), shown(std::max(anchor, position)));
    out.insert(QStringLiteral("fromLine"), c.line);
    out.insert(QStringLiteral("toLine"), c.line);
  } else {
    // Over several cells: the rows it runs over, out of the table too.
    const int other = a.ok && a.tableStart == c.tableStart ? a.line : (anchor < c.tableStart ? 0 : c.lines - 1);
    out.insert(QStringLiteral("selectionStart"), -1);
    out.insert(QStringLiteral("selectionEnd"), -1);
    out.insert(QStringLiteral("fromLine"), std::min(other, c.line));
    out.insert(QStringLiteral("toLine"), std::max(other, c.line));
  }
  return out;
}

int MdTableEditor::cellPosition(int tableStart, int line, int column, int offset) const {
  const TableCell c = tableCell(document(), tableStart, line, column);
  if(!c.ok) {
    return -1;
  }
  return c.textStart + (offset < 0 ? static_cast<int>(c.raw.size()) : cellRawOffset(c.raw, offset));
}

QString MdTableEditor::cellText(int tableStart, int line, int column) const {
  const TableCell c = tableCell(document(), tableStart, line, column);
  return c.ok ? cellDisplayText(c.raw) : QString();
}

bool MdTableEditor::addColumn(int tableStart) {
  const int at = addTableColumn(document(), tableStart);
  if(at < 0) {
    return false;
  }
  select(at, at);
  m_editor->forceActiveFocus();
  return true;
}

bool MdTableEditor::addRow(int tableStart) {
  const int at = addTableRow(document(), tableStart);
  if(at < 0) {
    return false;
  }
  select(at, at);
  m_editor->forceActiveFocus();
  return true;
}

bool MdTableEditor::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_editor && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
    auto* key = static_cast<QKeyEvent*>(event);
    if(handleKey(key, event->type() == QEvent::KeyPress)) {
      event->accept();
      return true;
    }
  }
  return QObject::eventFilter(watched, event);
}

bool MdTableEditor::handleKey(QKeyEvent* key, bool apply) {
  if(!m_enabled || m_editor == nullptr || m_editor->property("readOnly").toBool()) {
    return false;
  }
  QTextDocument* doc = document();
  if(doc == nullptr) {
    return false;
  }
  const int position = cursorPosition();
  const int anchor = anchorPosition();
  const auto mods = key->modifiers() & ~Qt::KeypadModifier;
  const bool plain = mods == Qt::NoModifier;
  const bool shift = mods == Qt::ShiftModifier;
  const bool upDown = key->key() == Qt::Key_Up || key->key() == Qt::Key_Down;
  const TableCell c = tableCellAt(doc, position);

  // Down from the last wrapped line over a table (up from the first under
  // one): into its first cell, not onto the hidden row's end.
  if(!c.ok) {
    if(!plain || !upDown || anchor != position) {
      return false;
    }
    const bool down = key->key() == Qt::Key_Down;
    const QTextBlock block = doc->findBlock(position);
    if(const QTextLayout* layout = block.layout(); layout != nullptr && layout->lineCount() > 1) {
      const int line = layout->lineForTextPosition(position - block.position()).lineNumber();
      if(line != (down ? layout->lineCount() - 1 : 0)) {
        return false;
      }
    }
    const CellMove move = enterTable(doc, position, down);
    if(move.handled && apply) {
      select(move.anchor, move.position);
    }
    return move.handled;
  }

  const auto run = [&](CellKey which, bool extend) {
    if(!apply) {
      return true;
    }
    const CellMove move = applyCellKey(doc, anchor, position, which, extend);
    if(move.handled) {
      select(move.anchor, move.position);
    }
    return move.handled;
  };
  switch(key->key()) {
    case Qt::Key_Escape:
      return plain && run(CellKey::Escape, false);
    case Qt::Key_Up:
      return plain && run(CellKey::Up, false);
    case Qt::Key_Down:
      return plain && run(CellKey::Down, false);
    case Qt::Key_Left:
      return (plain || shift) && run(CellKey::Left, shift);
    case Qt::Key_Right:
      return (plain || shift) && run(CellKey::Right, shift);
    case Qt::Key_Home:
      return (plain || shift) && run(CellKey::Home, shift);
    case Qt::Key_End:
      return (plain || shift) && run(CellKey::End, shift);
    case Qt::Key_Backspace:
      return plain && run(CellKey::Backspace, false);
    case Qt::Key_Delete:
      return plain && run(CellKey::Delete, false);
    default:
      break;
  }
  if(!apply) {
    return false;
  }
  const TableCell a = anchor == position ? c : tableCellAt(doc, anchor);
  const bool manyCells = a.ok && a.tableStart == c.tableStart && (a.line != c.line || a.column != c.column);
  // Over several cells Enter would put a line break where their pipes
  // were, a cut would take the pipes: Enter does nothing, a cut copies.
  if(manyCells && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
    return true;
  }
  if(manyCells && key->matches(QKeySequence::Cut)) {
    QMetaObject::invokeMethod(m_editor, "copy");
    return true;
  }
  QString text;
  if(key->matches(QKeySequence::Paste)) {
    // A file or a picture is the host's to attach (a link it puts in).
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if(mime == nullptr || mime->hasImage() || mime->hasUrls() || !mime->hasText()) {
      return false;
    }
    text = mime->text();
  } else if(const bool control = (mods & Qt::ControlModifier) != 0 && (mods & Qt::AltModifier) == 0;
            !control && !key->text().isEmpty() && key->text().at(0).isPrint()) {
    // AltGr (Ctrl+Alt on Windows) types too.
    text = key->text();
  } else {
    return false;
  }
  const CellMove move = typeIntoCell(doc, anchor, position, text);
  if(move.handled || move.anchor != anchor || move.position != position) {
    select(move.anchor, move.position);
  }
  return move.handled;
}

}  // namespace heap::md
