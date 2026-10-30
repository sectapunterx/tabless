#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariantMap>

class QTextDocument;
class QKeyEvent;

namespace heap::md {

// A table edited in its grid (dogfood 2026-10-30), for a rich text area
// (MdLivePreview): the caret stays in the text area's source, in the cell's
// text, and the grid draws that cell's text with the caret over the hidden
// row. This sits in front of the text area's keys - before the host's own -
// and takes the ones that would walk or type through the row's pipes
// (TableCells): arrows, Home / End, Backspace / Delete at a cell's edge,
// Esc, "|", a paste. Tab, Enter and the table's row and column keys stay
// the host's (MarkdownEditorController, TableOps), which already move by
// cells.
class MdTableEditor : public QObject {
  Q_OBJECT
  QML_ELEMENT

  // The text area (a TextArea or TextEdit).
  Q_PROPERTY(QQuickItem* editor READ editor WRITE setEditor NOTIFY editorChanged)
  // Off: the keys are all the text area's (read-only text, source modes).
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

 public:
  explicit MdTableEditor(QObject* parent = nullptr);
  ~MdTableEditor() override;

  QQuickItem* editor() const {
    return m_editor;
  }

  void setEditor(QQuickItem* editor);

  bool enabled() const {
    return m_enabled;
  }

  void setEnabled(bool on);

  // Where the caret is, for the grid to draw: {ok, tableStart, line (0 the
  // header, 2… the rows), column, text (the cell's as shown), caret,
  // selectionStart, selectionEnd (in that text; a selection over more than
  // one cell: -1 both, with fromLine / toLine the lines it runs over)}.
  Q_INVOKABLE QVariantMap caretCell() const;
  // The document position of `offset` characters into cell (line, column)'s
  // shown text; -1 for no such table. `offset` -1: the end of the text.
  Q_INVOKABLE int cellPosition(int tableStart, int line, int column, int offset) const;
  // The cell's text as shown, "" for none.
  Q_INVOKABLE QString cellText(int tableStart, int line, int column) const;
  // The grid's "+": a column after the last / a row under the last, the
  // caret in its first cell. False when there is no table there.
  Q_INVOKABLE bool addColumn(int tableStart);
  Q_INVOKABLE bool addRow(int tableStart);

 signals:
  void editorChanged();
  void enabledChanged();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  QTextDocument* document() const;
  int cursorPosition() const;
  int anchorPosition() const;
  void select(int anchor, int position);
  // Whether the key is one this takes; `apply` does it too.
  bool handleKey(QKeyEvent* key, bool apply);

  QPointer<QQuickItem> m_editor;
  bool m_enabled = true;
};

}  // namespace heap::md
