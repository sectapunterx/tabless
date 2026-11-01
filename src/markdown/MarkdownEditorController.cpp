#include "markdown/MarkdownEditorController.h"
#include "markdown/TableOps.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <utility>

namespace heap::md {

MarkdownEditorController::MarkdownEditorController(QObject* parent) : QObject(parent) {
}

void MarkdownEditorController::setTarget(QQuickTextDocument* target) {
  if(m_target == target) {
    return;
  }
  m_target = target;
  watch(document());
  emit targetChanged();
}

void MarkdownEditorController::watch(QTextDocument* document) {
  if(m_watched == document) {
    return;
  }
  disconnect(m_watchConnection);
  m_watched = document;
  m_changedFrom = m_changedTo = -1;
  if(document != nullptr) {
    m_watchConnection = connect(document, &QTextDocument::contentsChange, this, &MarkdownEditorController::onContentsChange);
  }
}

void MarkdownEditorController::onContentsChange(int position, int removed, int added) {
  Q_UNUSED(removed);
  // Text set from code (a note opened, a sync) has no undo and is nobody's
  // edit: its numbers are left as written.
  if(m_renumbering || m_watched == nullptr || !m_watched->isUndoRedoEnabled()) {
    return;
  }
  const bool queued = m_changedFrom >= 0;
  m_changedFrom = queued ? std::min(m_changedFrom, position) : position;
  m_changedTo = std::max(m_changedTo, position + added);
  if(!queued) {
    // After the edit has finished: the editor is still inside it here.
    QMetaObject::invokeMethod(this, &MarkdownEditorController::renumberAfterEdit, Qt::QueuedConnection);
  }
}

void MarkdownEditorController::renumberAfterEdit() {
  const int from = std::exchange(m_changedFrom, -1);
  const int to = std::exchange(m_changedTo, -1);
  QTextDocument* doc = m_watched;
  // An undo or a redo puts back what was there, numbers included; counting
  // again would start a new undo step and throw the redo away.
  if(doc == nullptr || from < 0 || doc->availableRedoSteps() > 0 || doc->availableUndoSteps() == 0) {
    return;
  }
  const int last = std::max(0, doc->characterCount() - 1);
  m_renumbering = true;
  const int fromBlock = doc->findBlock(std::min(from, last)).blockNumber();
  renumberOrderedList(doc, std::min(from, last), true);
  // A paste of several lines can reach into a second list further down.
  const int end = std::min(to, last);
  if(doc->findBlock(end).blockNumber() != fromBlock) {
    renumberOrderedList(doc, end, true);
  }
  m_renumbering = false;
}

QTextDocument* MarkdownEditorController::document() const {
  return m_target != nullptr ? m_target->textDocument() : nullptr;
}

void MarkdownEditorController::setCursorPosition(int position) {
  if(m_selection.end == position && m_selection.start == position) {
    return;
  }
  // The editor reports caret and selection as three separate bindings, in an
  // order nobody controls. With text selected the caret sits at one end of
  // the selection, and collapsing it here — when the caret binding happened to
  // arrive last — turned "select, Ctrl+B" into `word****`. A caret at an end
  // of the current selection is part of it; anywhere else it is a new caret.
  if(!m_selection.isEmpty() && (position == m_selection.start || position == m_selection.end)) {
    return;
  }
  m_selection = Selection::at(position);
  emit selectionChanged();
}

void MarkdownEditorController::setSelection(int start, int end) {
  const Selection next{std::min(start, end), std::max(start, end)};
  if(next.start == m_selection.start && next.end == m_selection.end) {
    return;
  }
  m_selection = next;
  emit selectionChanged();
}

bool MarkdownEditorController::claimsShortcut(int key, int modifiers) const {
  const int mods = modifiers & ~Qt::KeypadModifier;
  if(mods == Qt::ControlModifier) {
    return key == Qt::Key_B || key == Qt::Key_I || key == Qt::Key_E || key == Qt::Key_K;
  }
  if(mods == static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)) {
    return key == Qt::Key_X || key == Qt::Key_H || key == Qt::Key_L;
  }
  return false;
}

bool MarkdownEditorController::handleKey(int key, int modifiers) {
  const int mods = modifiers & ~Qt::KeypadModifier;
  // In a table, Enter, Tab and their Shift / Ctrl forms move through the
  // cells and add rows (TableOps::TableMove); everywhere else they are what
  // they always were.
  if(key == Qt::Key_Return || key == Qt::Key_Enter) {
    if(mods == Qt::NoModifier && tableMove(TableMove::NextRow)) {
      return true;
    }
    if(mods == Qt::ShiftModifier && tableMove(TableMove::Exit)) {
      return true;
    }
    if(mods == Qt::ControlModifier) {
      if(!tableMove(TableMove::AddRow)) {
        toggleTask();
      }
      return true;
    }
    return mods == Qt::NoModifier && handleReturn();
  }
  if((key == Qt::Key_Backspace || key == Qt::Key_Delete) && mods == Qt::NoModifier) {
    return deleteAcrossLines(key == Qt::Key_Delete);
  }
  if(key == Qt::Key_Tab && mods == Qt::NoModifier) {
    if(!tableMove(TableMove::NextCell)) {
      indent();
    }
    return true;
  }
  if(key == Qt::Key_Backtab || (key == Qt::Key_Tab && mods == Qt::ShiftModifier)) {
    if(!tableMove(TableMove::PreviousCell)) {
      outdent();
    }
    return true;
  }
  if(mods == Qt::ControlModifier) {
    switch(key) {
      case Qt::Key_B:
        toggleBold();
        return true;
      case Qt::Key_I:
        toggleItalic();
        return true;
      case Qt::Key_E:
        toggleCode();
        return true;
      case Qt::Key_K:
        insertLink();
        return true;
      default:
        break;
    }
  }
  if(mods == static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier)) {
    switch(key) {
      case Qt::Key_X:
        toggleStrikethrough();
        return true;
      case Qt::Key_H:
        toggleHighlight();
        return true;
      case Qt::Key_L:
        cycleHeading();
        return true;
      default:
        break;
    }
  }
  return false;
}

void MarkdownEditorController::setSelectionStart(int position) {
  if(m_selection.start == position) {
    return;
  }
  m_selection.start = position;
  emit selectionChanged();
}

void MarkdownEditorController::setSelectionEnd(int position) {
  if(m_selection.end == position) {
    return;
  }
  m_selection.end = position;
  emit selectionChanged();
}

void MarkdownEditorController::apply(const Selection& result) {
  m_selection = result;
  emit selectionChanged();
  // Only the editor can move its own cursor, so the new selection is asked
  // for rather than set.
  emit selectionRequested(result.start, result.end);
}

void MarkdownEditorController::toggleBold() {
  apply(toggleInlineStyle(document(), m_selection, InlineStyle::Bold));
}

void MarkdownEditorController::toggleItalic() {
  apply(toggleInlineStyle(document(), m_selection, InlineStyle::Italic));
}

void MarkdownEditorController::toggleStrikethrough() {
  apply(toggleInlineStyle(document(), m_selection, InlineStyle::Strikethrough));
}

void MarkdownEditorController::toggleCode() {
  apply(toggleInlineStyle(document(), m_selection, InlineStyle::Code));
}

void MarkdownEditorController::toggleHighlight() {
  apply(toggleInlineStyle(document(), m_selection, InlineStyle::Highlight));
}

void MarkdownEditorController::insertLink(const QString& url) {
  apply(heap::md::insertLink(document(), m_selection, url));
}

void MarkdownEditorController::cycleHeading() {
  apply(heap::md::cycleHeading(document(), m_selection));
}

void MarkdownEditorController::toggleTask() {
  apply(toggleTaskAtCaret(document(), m_selection));
}

void MarkdownEditorController::indent() {
  apply(indentLines(document(), m_selection, false));
}

void MarkdownEditorController::outdent() {
  apply(indentLines(document(), m_selection, true));
}

void MarkdownEditorController::paste(const QString& text) {
  apply(pasteText(document(), m_selection, text));
}

void MarkdownEditorController::replaceRange(int start, int end, const QString& text) {
  QTextDocument* doc = document();
  if(doc == nullptr) {
    return;
  }
  const int last = std::max(0, doc->characterCount() - 1);
  const int lo = std::clamp(std::min(start, end), 0, last);
  const int hi = std::clamp(std::max(start, end), 0, last);
  QTextCursor cursor(doc);
  cursor.beginEditBlock();
  cursor.setPosition(lo);
  cursor.setPosition(hi, QTextCursor::KeepAnchor);
  cursor.insertText(text);
  cursor.endEditBlock();
  apply(Selection::at(lo + static_cast<int>(text.size())));
}

bool MarkdownEditorController::deleteAcrossLines(bool forward) {
  QTextDocument* doc = document();
  if(doc == nullptr) {
    return false;
  }
  const int lo = std::min(m_selection.start, m_selection.end);
  const int hi = std::max(m_selection.start, m_selection.end);
  int from = lo;
  int to = hi;
  if(lo == hi) {
    // Only a line break: inside a line the field's own delete is kept, so
    // a word deleted letter by letter still takes back in one Ctrl+Z.
    const QTextBlock block = doc->findBlock(lo);
    if(forward ? lo != block.position() + block.length() - 1 || !block.next().isValid() : lo != block.position() || lo == 0) {
      return false;
    }
    from = forward ? lo : lo - 1;
    to = from + 1;
  }
  // Only around a numbered list: everywhere else the field does it. A
  // selection inside one line counts too: emptying an item ("Shift+Home,
  // Backspace") changes the numbers below it.
  const auto ordered = [](const QTextBlock& b) {
    static const QRegularExpression rx(QStringLiteral("^[ \\t]*\\d{1,9}[.)](?:[ \\t]|$)"));
    return b.isValid() && rx.match(b.text()).hasMatch();
  };
  const QTextBlock top = doc->findBlock(from);
  const QTextBlock bottom = doc->findBlock(to);
  if(!ordered(top) && !ordered(bottom) && !ordered(bottom.next())) {
    return false;
  }
  QTextCursor cursor(doc);
  cursor.beginEditBlock();
  cursor.setPosition(from);
  cursor.setPosition(to, QTextCursor::KeepAnchor);
  cursor.removeSelectedText();
  cursor.endEditBlock();
  apply(Selection::at(from));
  return true;
}

bool MarkdownEditorController::handleReturn() {
  bool handled = false;
  const Selection result = continueLine(document(), m_selection, &handled);
  if(handled) {
    apply(result);
  }
  return handled;
}

bool MarkdownEditorController::insideFence() const {
  return isInsideFence(document(), m_selection.caret());
}

bool MarkdownEditorController::tableOp(const QString& op) {
  const auto which = tableOpFromName(op);
  if(!which) {
    return false;
  }
  bool applied = false;
  const Selection result = applyTableOp(document(), m_selection, *which, &applied);
  if(applied) {
    apply(result);
  }
  return applied;
}

bool MarkdownEditorController::typeText(const QString& text) {
  if(text != QStringLiteral("|")) {
    return false;
  }
  bool applied = false;
  const Selection result = applyPipeTypeOver(document(), m_selection, &applied);
  if(applied) {
    apply(result);
  }
  return applied;
}

bool MarkdownEditorController::tableMove(TableMove move) {
  bool applied = false;
  const Selection result = applyTableMove(document(), m_selection, move, &applied);
  if(applied) {
    apply(result);
  }
  return applied;
}

int MarkdownEditorController::tableAnchor(int position) const {
  return heap::md::tableAnchor(document(), position);
}

}  // namespace heap::md
