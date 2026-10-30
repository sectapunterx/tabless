#pragma once

#include <QString>

class QTextDocument;

namespace heap::md {

// Markdown editing operations, as functions over a QTextDocument.
//
// These used to live as JavaScript inside NotesView.qml, where they could
// only be exercised by driving the UI. They are the kind of code that is all
// edge cases — what Enter does on an empty list item, what Tab does inside a
// fence, what bold does when the selection already is bold — so they belong
// somewhere a test can call them directly.
//
// Every op goes through a QTextCursor inside one edit block, which makes it
// one undo step. That is not a detail: an operation that lands as two steps
// leaves a half-applied state behind after Ctrl+Z, and the reader has no way
// to tell what happened.

// Where the caret and selection are, before and after an operation. Positions
// are UTF-16, the unit QTextDocument and Qt Quick's text items count in.
struct Selection {
  int start = 0;
  int end = 0;

  bool isEmpty() const {
    return start == end;
  }

  int caret() const {
    return end;
  }

  static Selection at(int position) {
    return Selection{position, position};
  }
};

// Which inline markers a toggle applies.
enum class InlineStyle {
  Bold,           // **text**
  Italic,         // _text_
  Strikethrough,  // ~~text~~
  Code,           // `text`
  Highlight,      // ==text==
};

// Wrap the selection in `style`'s markers, or unwrap it if it is already
// wrapped. With no selection the markers are inserted and the caret placed
// between them, so typing continues inside.
Selection toggleInlineStyle(QTextDocument* document, Selection selection, InlineStyle style);

// Turn the selection into a link. With text selected it becomes the label and
// the caret lands in the empty target, which is where the next keystroke
// belongs. With a URL selected, or a URL on the clipboard, the roles swap.
Selection insertLink(QTextDocument* document, Selection selection, const QString& url = {});

// Cycle the heading level of every line the selection touches: none → H1 → H2
// → … → H6 → none. Cycling rather than setting means one key reaches every
// level without a modifier per level.
Selection cycleHeading(QTextDocument* document, Selection selection);

// Continue the markdown structure of the current line: a new bullet, the next
// number, another checkbox, another quote marker. On an empty item the marker
// is removed instead, which is how a list is ended without reaching for the
// mouse. Returns the new selection, or the input unchanged when Enter should
// do its ordinary thing.
//
// `handled` says which happened: an unhandled Enter must fall through to the
// editor so a plain newline still works.
Selection continueLine(QTextDocument* document, Selection selection, bool* handled);

// Indent or outdent every list line the selection touches. Inside a fenced
// code block, indent inserts four spaces instead — Tab there means code
// indentation, not a focus change.
Selection indentLines(QTextDocument* document, Selection selection, bool outdent);

// Flip the checkbox on the line holding the caret, adding one to a plain list
// item that has none. Returns the input unchanged when the line is not a list
// item at all.
Selection toggleTaskAtCaret(QTextDocument* document, Selection selection);

// Wrap the selection in a link when `pasted` is a URL; otherwise insert it
// literally. Pasting a URL over selected text is nearly always meant as "link
// this", and getting it wrong costs a retype.
Selection pasteText(QTextDocument* document, Selection selection, const QString& pasted);

// Count the numbered list around `position` again, from its first item's
// number, so deleting, inserting or splitting an item never leaves "4. 6. 7."
// behind. Each indent level counts on its own; only items from the line at
// `position` down are rewritten. Returns whether anything changed.
//
// `joinPrevious` makes the rewrite part of the edit before it (one Ctrl+Z
// takes back both), which is how the editor calls it after a keystroke.
bool renumberOrderedList(QTextDocument* document, int position, bool joinPrevious = false);

// True when the position sits inside a fenced code block. Exposed because the
// editor asks before deciding what Tab and Enter mean.
bool isInsideFence(QTextDocument* document, int position);

}  // namespace heap::md
