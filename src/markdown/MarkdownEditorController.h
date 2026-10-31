#pragma once

#include "markdown/MdEditOps.h"
#include "markdown/TableOps.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickTextDocument>

class QTextDocument;

namespace heap::md {

// The editor's keyboard behaviour, in one place QML can call.
//
// NotesView used to carry this as a few hundred lines of JavaScript in a
// Keys.onPressed handler: what Enter does on a list item, what Tab does inside
// a fence, and nothing at all for bold or italic. Moving it here means the
// rules can be tested directly, and it leaves the QML handler short enough to
// read in one go.
//
// Each operation is one undo step, because each goes through a QTextCursor
// inside a single edit block — see MdEditOps.
class MarkdownEditorController : public QObject {
  Q_OBJECT
  QML_ELEMENT

  // The editor's document. Everything here is a no-op until this is set.
  // Typed QObject* for QML: qmllint on CI (Qt 6.9) has no type for
  // QQuickTextDocument and counts every binding to it as a finding.
  Q_PROPERTY(QObject* target READ targetObject WRITE setTargetObject NOTIFY targetChanged)
  // Caret and selection, kept in step with the editor's own. The controller
  // writes back to these after an operation, and the editor binds them to its
  // cursorPosition and selection.
  Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY selectionChanged)
  Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart NOTIFY selectionChanged)
  Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd NOTIFY selectionChanged)

 public:
  explicit MarkdownEditorController(QObject* parent = nullptr);

  QQuickTextDocument* target() const {
    return m_target;
  }

  void setTarget(QQuickTextDocument* target);

  QObject* targetObject() const {
    return m_target;
  }

  void setTargetObject(QObject* t) {
    setTarget(qobject_cast<QQuickTextDocument*>(t));
  }

  int cursorPosition() const {
    return m_selection.end;
  }

  void setCursorPosition(int position);

  int selectionStart() const {
    return m_selection.start;
  }

  void setSelectionStart(int position);

  int selectionEnd() const {
    return m_selection.end;
  }

  void setSelectionEnd(int position);

  // Caret and selection in one call, for an editor that wants to be sure the
  // controller sees exactly what it shows before an operation.
  Q_INVOKABLE void setSelection(int start, int end);

  // The keyboard, shared by every markdown editor (notes and doc pages), so
  // the two cannot drift apart again.
  //
  // `claimsShortcut` answers ShortcutOverride: true for the formatting keys,
  // which must beat the app-wide shortcuts (Ctrl+K is the command palette
  // everywhere else) while the editor has focus. `handleKey` performs Enter
  // continuation, Tab/Shift+Tab, Ctrl+Enter and the formatting keys, and says
  // whether it did, so an unhandled key still reaches the text field.
  Q_INVOKABLE bool claimsShortcut(int key, int modifiers) const;
  Q_INVOKABLE bool handleKey(int key, int modifiers);
  // Printable text about to be typed: true when the controller took it
  // instead (a "|" typing over a table row's own pipe, TableOps::
  // pipeTypeOver), so the field must not insert it.
  Q_INVOKABLE bool typeText(const QString& text);

  // ── Operations, each one undo step ──────────────────────────────
  Q_INVOKABLE void toggleBold();
  Q_INVOKABLE void toggleItalic();
  Q_INVOKABLE void toggleStrikethrough();
  Q_INVOKABLE void toggleCode();
  Q_INVOKABLE void toggleHighlight();
  Q_INVOKABLE void insertLink(const QString& url = {});
  Q_INVOKABLE void cycleHeading();
  Q_INVOKABLE void toggleTask();
  Q_INVOKABLE void indent();
  Q_INVOKABLE void outdent();
  Q_INVOKABLE void paste(const QString& text);

  // Enter, with markdown continuation. Returns true when it did something, so
  // the QML handler knows whether to let the key through: an unhandled Enter
  // must still insert a plain newline.
  Q_INVOKABLE bool handleReturn();

  // True when the caret sits inside a fenced code block. The view asks so it
  // can tell a code Tab from a list Tab.
  Q_INVOKABLE bool insideFence() const;

  // A table's rows and columns where the caret is (TableOps): "rowAbove",
  // "rowBelow", "columnLeft", "columnRight", "deleteRow", "deleteColumn".
  // False, and nothing changed, when the caret is not in a table.
  Q_INVOKABLE bool tableOp(const QString& op);
  // Where a table op at `position` would work (TableOps::tableAnchor); -1
  // for no table there.
  Q_INVOKABLE int tableAnchor(int position) const;

  // Replace [start, end) with `text` as one undo step and put the caret after
  // it: a pasted picture's link over the selection is one Ctrl+Z, not two.
  Q_INVOKABLE void replaceRange(int start, int end, const QString& text);

 signals:
  void targetChanged();
  void selectionChanged();
  // Emitted after an operation, with where the caret and selection should now
  // be. The editor applies it: only the editor can move its own cursor.
  void selectionRequested(int start, int end);

 private:
  QTextDocument* document() const;
  void apply(const Selection& result);
  // A move through the table at the caret; false when it is not in one.
  bool tableMove(TableMove move);
  // Backspace / Delete that join two lines of a numbered list, done here in
  // one edit block so the renumbering after it joins the same undo step.
  bool deleteAcrossLines(bool forward);

  // Numbered lists count again after every edit (renumberOrderedList). The
  // document says where it changed; the count runs once the edit is done.
  void watch(QTextDocument* document);
  void onContentsChange(int position, int removed, int added);
  void renumberAfterEdit();

  QQuickTextDocument* m_target = nullptr;
  QPointer<QTextDocument> m_watched;
  QMetaObject::Connection m_watchConnection;
  Selection m_selection;
  // The span the last edit touched, until the renumbering has looked at it.
  int m_changedFrom = -1;
  int m_changedTo = -1;
  bool m_renumbering = false;
};

}  // namespace heap::md
