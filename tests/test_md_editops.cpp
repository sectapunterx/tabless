// Markdown editing operations.
//
// These lived as JavaScript inside a Keys.onPressed handler, where the only
// way to check them was to drive the UI. They are all edge cases — what Enter
// does on an empty list item, what Tab does inside a fence, what bold does
// when the selection is already bold — which is exactly the shape of code that
// needs to be callable from a test.
//
// Each case asserts the resulting text *and* where the caret ends up. A cursor
// left in the wrong place is a bug the user feels on the very next keystroke,
// and it is invisible if only the text is checked.

#include "markdown/MdEditOps.h"

#include <QString>
#include <QTextCursor>
#include <QTextDocument>

#include <gtest/gtest.h>

#include <functional>

using namespace heap::md;

namespace {

// A document plus a caret, written so a case reads as what the user did.
class Editor {
 public:
  explicit Editor(const QString& text, int caret = 0) : m_document(text) {
    m_selection = Selection::at(caret);
  }

  Editor& select(int start, int end) {
    m_selection = Selection{start, end};
    return *this;
  }

  // Place the caret after the first occurrence of `needle`.
  Editor& caretAfter(const QString& needle) {
    const int at = text().indexOf(needle);
    m_selection = Selection::at(at < 0 ? 0 : at + static_cast<int>(needle.size()));
    return *this;
  }

  // Select the first occurrence of `needle`.
  Editor& selectText(const QString& needle) {
    const int at = text().indexOf(needle);
    m_selection = Selection{at, at + static_cast<int>(needle.size())};
    return *this;
  }

  QString text() const {
    return m_document.toPlainText();
  }

  int caret() const {
    return m_selection.caret();
  }

  QString textWithCaret() const {
    QString out = text();
    out.insert(m_selection.caret(), QStringLiteral("|"));
    return out;
  }

  QTextDocument* document() {
    return &m_document;
  }

  Selection selection() const {
    return m_selection;
  }

  void setSelection(const Selection& selection) {
    m_selection = selection;
  }

 private:
  QTextDocument m_document;
  Selection m_selection;
};

}  // namespace

// ── Inline styles ───────────────────────────────────────────────────

TEST(MdEditOpsTest, BoldWrapsTheSelection) {
  Editor editor(QStringLiteral("make this bold please"));
  editor.selectText(QStringLiteral("this bold"));
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Bold));
  EXPECT_EQ(editor.text(), QStringLiteral("make **this bold** please"));
  // The selection still covers the same words, so a second key applies to
  // what the user still thinks is selected.
  EXPECT_EQ(editor.text().mid(editor.selection().start, editor.selection().end - editor.selection().start), QStringLiteral("this bold"));
}

TEST(MdEditOpsTest, BoldOnAWrappedSelectionTakesItOff) {
  // A toggle has to come back off, or the key just piles up asterisks.
  Editor editor(QStringLiteral("make **this** plain"));
  editor.selectText(QStringLiteral("**this**"));
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Bold));
  EXPECT_EQ(editor.text(), QStringLiteral("make this plain"));
}

TEST(MdEditOpsTest, BoldRecognisesMarkersJustOutsideTheSelection) {
  // Double-clicking a bold word selects the word, not its markers.
  Editor editor(QStringLiteral("make **this** plain"));
  editor.selectText(QStringLiteral("this"));
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Bold));
  EXPECT_EQ(editor.text(), QStringLiteral("make this plain"));
}

TEST(MdEditOpsTest, BoldWithNoSelectionLeavesTheCaretInside) {
  Editor editor(QStringLiteral("type here"), 5);
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Bold));
  EXPECT_EQ(editor.text(), QStringLiteral("type ****here"));
  // Typing must continue between the markers, not after them.
  EXPECT_EQ(editor.textWithCaret(), QStringLiteral("type **|**here"));
}

TEST(MdEditOpsTest, EachStyleUsesItsOwnMarkers) {
  const struct {
    InlineStyle style;
    QString expected;
  } cases[] = {
      {InlineStyle::Italic, QStringLiteral("_word_")},
      {InlineStyle::Strikethrough, QStringLiteral("~~word~~")},
      {InlineStyle::Code, QStringLiteral("`word`")},
      {InlineStyle::Highlight, QStringLiteral("==word==")},
  };

  for(const auto& testCase : cases) {
    Editor editor(QStringLiteral("word"));
    editor.select(0, 4);
    editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), testCase.style));
    EXPECT_EQ(editor.text(), testCase.expected);
  }
}

// ── Links ───────────────────────────────────────────────────────────

TEST(MdEditOpsTest, LinkPutsTheCaretInTheMissingHalf) {
  // With text selected, the target is what still has to be typed.
  Editor editor(QStringLiteral("see the docs"));
  editor.selectText(QStringLiteral("the docs"));
  editor.setSelection(insertLink(editor.document(), editor.selection()));
  EXPECT_EQ(editor.text(), QStringLiteral("see [the docs]()"));
  EXPECT_EQ(editor.textWithCaret(), QStringLiteral("see [the docs](|)"));
}

TEST(MdEditOpsTest, LinkOnASelectedUrlSwapsTheRoles) {
  // With a URL selected, it is the label that is missing.
  Editor editor(QStringLiteral("https://example.com"));
  editor.select(0, 19);
  editor.setSelection(insertLink(editor.document(), editor.selection()));
  EXPECT_EQ(editor.text(), QStringLiteral("[](https://example.com)"));
  EXPECT_EQ(editor.textWithCaret(), QStringLiteral("[|](https://example.com)"));
}

TEST(MdEditOpsTest, PastingAUrlOverTextLinksIt) {
  // Nearly always what was meant, and getting it wrong costs a retype.
  Editor editor(QStringLiteral("the docs"));
  editor.select(0, 8);
  editor.setSelection(pasteText(editor.document(), editor.selection(), QStringLiteral("https://example.com")));
  EXPECT_EQ(editor.text(), QStringLiteral("[the docs](https://example.com)"));
}

TEST(MdEditOpsTest, PastingOrdinaryTextJustInserts) {
  Editor editor(QStringLiteral("a b"));
  editor.select(0, 1);
  editor.setSelection(pasteText(editor.document(), editor.selection(), QStringLiteral("xyz")));
  EXPECT_EQ(editor.text(), QStringLiteral("xyz b"));
}

// ── Headings ────────────────────────────────────────────────────────

TEST(MdEditOpsTest, HeadingCyclesThroughEveryLevelAndBackOff) {
  Editor editor(QStringLiteral("a title"));
  const QStringList expected{
      QStringLiteral("# a title"),
      QStringLiteral("## a title"),
      QStringLiteral("### a title"),
      QStringLiteral("#### a title"),
      QStringLiteral("##### a title"),
      QStringLiteral("###### a title"),
      QStringLiteral("a title"),
  };
  for(const QString& want : expected) {
    editor.setSelection(cycleHeading(editor.document(), editor.selection()));
    EXPECT_EQ(editor.text(), want);
  }
}

TEST(MdEditOpsTest, HeadingMovesAWholeSelectionTogether) {
  Editor editor(QStringLiteral("one\ntwo\nthree"));
  editor.select(0, 13);
  editor.setSelection(cycleHeading(editor.document(), editor.selection()));
  EXPECT_EQ(editor.text(), QStringLiteral("# one\n# two\n# three"));
}

// ── Enter ───────────────────────────────────────────────────────────

TEST(MdEditOpsTest, EnterContinuesAList) {
  Editor editor(QStringLiteral("- first"));
  editor.caretAfter(QStringLiteral("- first"));
  bool handled = false;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  EXPECT_TRUE(handled);
  EXPECT_EQ(editor.textWithCaret(), QStringLiteral("- first\n- |"));
}

// Audit 2026-09-30: splitting an item left the separating space behind the
// new marker, "-  world".
TEST(MdEditOpsTest, SplittingAListItemDoesNotDoubleTheSpace) {
  Editor editor(QStringLiteral("- hello world"));
  editor.caretAfter(QStringLiteral("- hello"));
  bool handled = false;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  EXPECT_TRUE(handled);
  EXPECT_EQ(editor.textWithCaret(), QStringLiteral("- hello\n- |world"));
}

// A multi-line selection is what Tab and heading cycling act on — every line
// it touches, not just the caret's.
TEST(MdEditOpsTest, TabIndentsEverySelectedLine) {
  Editor editor(QStringLiteral("- a\n- b\n- c"));
  editor.select(0, 9);
  editor.setSelection(indentLines(editor.document(), editor.selection(), false));
  EXPECT_EQ(editor.text(), QStringLiteral("  - a\n  - b\n  - c"));
  editor.setSelection(indentLines(editor.document(), editor.selection(), true));
  EXPECT_EQ(editor.text(), QStringLiteral("- a\n- b\n- c"));
}

TEST(MdEditOpsTest, EnterCountsAnOrderedList) {
  Editor editor(QStringLiteral("3. third"));
  editor.caretAfter(QStringLiteral("3. third"));
  bool handled = false;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  EXPECT_EQ(editor.text(), QStringLiteral("3. third\n4. "));
}

TEST(MdEditOpsTest, EnterStartsTheNextTaskUnchecked) {
  // Continuing a list is not the same as repeating what was already done.
  Editor editor(QStringLiteral("- [x] done"));
  editor.caretAfter(QStringLiteral("- [x] done"));
  bool handled = false;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  EXPECT_EQ(editor.text(), QStringLiteral("- [x] done\n- [ ] "));
}

TEST(MdEditOpsTest, EnterOnAnEmptyItemEndsTheList) {
  // The way a list is finished without reaching for the mouse.
  Editor editor(QStringLiteral("- first\n- "));
  editor.caretAfter(QStringLiteral("- first\n- "));
  bool handled = false;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  EXPECT_TRUE(handled);
  EXPECT_EQ(editor.text(), QStringLiteral("- first\n"));
}

TEST(MdEditOpsTest, EnterContinuesAQuote) {
  Editor editor(QStringLiteral("> quoted"));
  editor.caretAfter(QStringLiteral("> quoted"));
  bool handled = false;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  EXPECT_EQ(editor.text(), QStringLiteral("> quoted\n> "));
}

TEST(MdEditOpsTest, EnterOnOrdinaryTextIsLeftAlone) {
  // An unhandled Enter has to fall through, or a plain newline stops working.
  Editor editor(QStringLiteral("just a sentence"));
  editor.caretAfter(QStringLiteral("sentence"));
  bool handled = true;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  EXPECT_FALSE(handled);
  EXPECT_EQ(editor.text(), QStringLiteral("just a sentence"));
}

TEST(MdEditOpsTest, EnterInsideAFenceKeepsTheIndentAndNothingElse) {
  Editor editor(QStringLiteral("```py\n    x = 1"));
  editor.caretAfter(QStringLiteral("x = 1"));
  bool handled = false;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  EXPECT_TRUE(handled);
  EXPECT_EQ(editor.text(), QStringLiteral("```py\n    x = 1\n    "));
}

// ── Indent ──────────────────────────────────────────────────────────

TEST(MdEditOpsTest, TabIndentsAndShiftTabOutdents) {
  Editor editor(QStringLiteral("- one\n- two"));
  editor.select(0, 11);
  editor.setSelection(indentLines(editor.document(), editor.selection(), false));
  EXPECT_EQ(editor.text(), QStringLiteral("  - one\n  - two"));

  editor.setSelection(indentLines(editor.document(), editor.selection(), true));
  EXPECT_EQ(editor.text(), QStringLiteral("- one\n- two"));
}

TEST(MdEditOpsTest, OutdentStopsAtTheMargin) {
  Editor editor(QStringLiteral("- already flush"));
  editor.select(0, 5);
  editor.setSelection(indentLines(editor.document(), editor.selection(), true));
  EXPECT_EQ(editor.text(), QStringLiteral("- already flush"));
}

TEST(MdEditOpsTest, TabInsideAFenceInsertsSpaces) {
  // In code, Tab means indentation — not a shifted list line.
  Editor editor(QStringLiteral("```\ncode"));
  editor.caretAfter(QStringLiteral("```\n"));
  editor.setSelection(indentLines(editor.document(), editor.selection(), false));
  EXPECT_EQ(editor.text(), QStringLiteral("```\n    code"));
}

TEST(MdEditOpsTest, IndentSkipsBlankLines) {
  Editor editor(QStringLiteral("- one\n\n- two"));
  editor.select(0, 12);
  editor.setSelection(indentLines(editor.document(), editor.selection(), false));
  EXPECT_EQ(editor.text(), QStringLiteral("  - one\n\n  - two"));
}

// ── Checkboxes ──────────────────────────────────────────────────────

TEST(MdEditOpsTest, TaskToggleFlipsTheBox) {
  Editor editor(QStringLiteral("- [ ] todo"));
  editor.caretAfter(QStringLiteral("todo"));
  editor.setSelection(toggleTaskAtCaret(editor.document(), editor.selection()));
  EXPECT_EQ(editor.text(), QStringLiteral("- [x] todo"));

  editor.setSelection(toggleTaskAtCaret(editor.document(), editor.selection()));
  EXPECT_EQ(editor.text(), QStringLiteral("- [ ] todo"));
}

TEST(MdEditOpsTest, TaskToggleAddsABoxToAPlainItem) {
  // Pressing the key on a plain item means "make this a task", not "do
  // nothing".
  Editor editor(QStringLiteral("- an item"));
  editor.caretAfter(QStringLiteral("item"));
  editor.setSelection(toggleTaskAtCaret(editor.document(), editor.selection()));
  EXPECT_EQ(editor.text(), QStringLiteral("- [ ] an item"));
}

TEST(MdEditOpsTest, TaskToggleLeavesNonListLinesAlone) {
  Editor editor(QStringLiteral("just a paragraph"));
  editor.caretAfter(QStringLiteral("just"));
  editor.setSelection(toggleTaskAtCaret(editor.document(), editor.selection()));
  EXPECT_EQ(editor.text(), QStringLiteral("just a paragraph"));
}

// ── Fences ──────────────────────────────────────────────────────────

TEST(MdEditOpsTest, KnowsWhenTheCaretIsInsideAFence) {
  Editor editor(QStringLiteral("text\n```\ncode\n```\nafter"));
  EXPECT_FALSE(isInsideFence(editor.document(), 0));
  EXPECT_TRUE(isInsideFence(editor.document(), editor.text().indexOf(QStringLiteral("code"))));
  EXPECT_FALSE(isInsideFence(editor.document(), editor.text().indexOf(QStringLiteral("after"))));
}

TEST(MdEditOpsTest, AnUnclosedFenceStillCounts) {
  Editor editor(QStringLiteral("```\nstill code"));
  EXPECT_TRUE(isInsideFence(editor.document(), editor.text().indexOf(QStringLiteral("still"))));
}

// ── Undo ────────────────────────────────────────────────────────────

TEST(MdEditOpsTest, EveryOperationIsASingleUndoStep) {
  // An op that lands as two steps leaves a half-applied state behind after
  // Ctrl+Z, and the reader cannot tell what happened. This is the property
  // that was got wrong once already, in the checkbox write-back.
  const QString original = QStringLiteral("- [ ] item\ntext here");

  const auto roundTrip = [&original](const std::function<Selection(QTextDocument*, Selection)>& op, Selection start) {
    QTextDocument document(original);
    document.setUndoRedoEnabled(true);
    op(&document, start);
    EXPECT_NE(document.toPlainText(), original) << "operation did nothing";
    document.undo();
    EXPECT_EQ(document.toPlainText(), original) << "one undo did not take it all back";
  };

  roundTrip(
      [](QTextDocument* d, Selection s) {
        return toggleInlineStyle(d, s, InlineStyle::Bold);
      },
      Selection{6, 10});
  roundTrip(
      [](QTextDocument* d, Selection s) {
        return heap::md::insertLink(d, s, QStringLiteral("u"));
      },
      Selection{6, 10});
  roundTrip(
      [](QTextDocument* d, Selection s) {
        return heap::md::cycleHeading(d, s);
      },
      Selection::at(12));
  roundTrip(
      [](QTextDocument* d, Selection s) {
        return toggleTaskAtCaret(d, s);
      },
      Selection::at(6));
  roundTrip(
      [](QTextDocument* d, Selection s) {
        return indentLines(d, s, false);
      },
      Selection::at(0));
  roundTrip(
      [](QTextDocument* d, Selection s) {
        bool handled = false;
        return continueLine(d, s, &handled);
      },
      Selection::at(10));
}

TEST(MdEditOpsTest, OperationsRefuseANullDocument) {
  const Selection start = Selection::at(3);
  EXPECT_EQ(toggleInlineStyle(nullptr, start, InlineStyle::Bold).caret(), 3);
  EXPECT_EQ(heap::md::insertLink(nullptr, start, QString()).caret(), 3);
  EXPECT_EQ(heap::md::cycleHeading(nullptr, start).caret(), 3);
  EXPECT_EQ(toggleTaskAtCaret(nullptr, start).caret(), 3);
  EXPECT_EQ(indentLines(nullptr, start, false).caret(), 3);
  EXPECT_FALSE(isInsideFence(nullptr, 0));
  bool handled = true;
  EXPECT_EQ(continueLine(nullptr, start, &handled).caret(), 3);
  EXPECT_FALSE(handled);
}

// ── KNOW-19 (audit 2026-09-30): several selected lines ──

TEST(MdEditOpsTest, Know19_BoldOverListLinesWrapsEachItem) {
  Editor editor(QStringLiteral("- a\n- b"));
  editor.select(0, static_cast<int>(editor.text().size()));
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Bold));
  EXPECT_EQ(editor.text(), QStringLiteral("- **a**\n- **b**"));
  // A toggle: the same key takes it back off.
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Bold));
  EXPECT_EQ(editor.text(), QStringLiteral("- a\n- b"));
}

TEST(MdEditOpsTest, Know19_ItalicSkipsHeadingHashesQuotesAndBlankLines) {
  Editor editor(QStringLiteral("# H\ntext\n\n> quoted\n- [ ] task"));
  editor.select(0, static_cast<int>(editor.text().size()));
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Italic));
  EXPECT_EQ(editor.text(), QStringLiteral("# _H_\n_text_\n\n> _quoted_\n- [ ] _task_"));
}

TEST(MdEditOpsTest, Know19_APartialSelectionWrapsOnlyWhatIsSelected) {
  Editor editor(QStringLiteral("one two\nthree four"));
  editor.select(4, 13);  // "two\nthree"
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Bold));
  EXPECT_EQ(editor.text(), QStringLiteral("one **two**\n**three** four"));
}

TEST(MdEditOpsTest, Know19_CodeFenceLinesAreLeftAlone) {
  Editor editor(QStringLiteral("a\n```\ncode\n```\nb"));
  editor.select(0, static_cast<int>(editor.text().size()));
  editor.setSelection(toggleInlineStyle(editor.document(), editor.selection(), InlineStyle::Bold));
  EXPECT_EQ(editor.text(), QStringLiteral("**a**\n```\ncode\n```\n**b**"));
}

// ── Numbered lists count again after an edit (dogfood 2026-10-30) ──

TEST(MdEditOpsTest, RenumberAfterDeletingAnItemClosesTheGap) {
  Editor editor(QStringLiteral("4. Logs\n6. Compare\n7. Write"));
  EXPECT_TRUE(renumberOrderedList(editor.document(), static_cast<int>(editor.text().indexOf(QStringLiteral("6.")))));
  EXPECT_EQ(editor.text(), QStringLiteral("4. Logs\n5. Compare\n6. Write"));
}

TEST(MdEditOpsTest, RenumberAfterInsertingInTheMiddleShiftsTheRestDown) {
  Editor editor(QStringLiteral("1. a\n2. b\n3. new\n3. c\n4. d"));
  EXPECT_TRUE(renumberOrderedList(editor.document(), static_cast<int>(editor.text().indexOf(QStringLiteral("3. new")))));
  EXPECT_EQ(editor.text(), QStringLiteral("1. a\n2. b\n3. new\n4. c\n5. d"));
}

TEST(MdEditOpsTest, RenumberAfterEnterSplitsAnItem) {
  Editor editor(QStringLiteral("1. one two\n2. three"));
  editor.caretAfter(QStringLiteral("one"));
  bool handled = false;
  editor.setSelection(continueLine(editor.document(), editor.selection(), &handled));
  ASSERT_TRUE(handled);
  EXPECT_EQ(editor.text(), QStringLiteral("1. one\n2. two\n2. three"));
  EXPECT_TRUE(renumberOrderedList(editor.document(), editor.caret()));
  EXPECT_EQ(editor.text(), QStringLiteral("1. one\n2. two\n3. three"));
}

TEST(MdEditOpsTest, RenumberCountsNestedLevelsSeparately) {
  Editor editor(QStringLiteral("1. a\n   1. x\n   3. y\n2. b\n5. c"));
  EXPECT_TRUE(renumberOrderedList(editor.document(), 0));
  EXPECT_EQ(editor.text(), QStringLiteral("1. a\n   1. x\n   2. y\n2. b\n3. c"));
}

TEST(MdEditOpsTest, RenumberKeepsTheFirstNumberAndTheDelimiter) {
  Editor editor(QStringLiteral("7) a\n9) b\n10) c"));
  EXPECT_TRUE(renumberOrderedList(editor.document(), 0));
  EXPECT_EQ(editor.text(), QStringLiteral("7) a\n8) b\n9) c"));
}

TEST(MdEditOpsTest, RenumberRewritesOnlyFromTheEditedLineDown) {
  Editor editor(QStringLiteral("1. a\n5. b\n6. c\n9. d"));
  // The edit was on "6. c": "5. b" above it is left as written, and the
  // count goes on from it.
  EXPECT_TRUE(renumberOrderedList(editor.document(), static_cast<int>(editor.text().indexOf(QStringLiteral("6.")))));
  EXPECT_EQ(editor.text(), QStringLiteral("1. a\n5. b\n6. c\n7. d"));
}

TEST(MdEditOpsTest, RenumberStopsAtTheEndOfTheList) {
  Editor editor(QStringLiteral("1. a\n3. b\n\nText\n\n7. other\n9. list"));
  EXPECT_TRUE(renumberOrderedList(editor.document(), 0));
  EXPECT_EQ(editor.text(), QStringLiteral("1. a\n2. b\n\nText\n\n7. other\n9. list"));
}

TEST(MdEditOpsTest, RenumberTreatsAOneAfterABlankLineAsANewList) {
  Editor editor(QStringLiteral("1. a\n2. b\n\n1. x\n3. y"));
  EXPECT_TRUE(renumberOrderedList(editor.document(), static_cast<int>(editor.text().indexOf(QStringLiteral("3. y")))));
  EXPECT_EQ(editor.text(), QStringLiteral("1. a\n2. b\n\n1. x\n2. y"));
  // A loose list (items a blank line apart) is still one list.
  Editor loose(QStringLiteral("1. a\n\n3. b"));
  EXPECT_TRUE(renumberOrderedList(loose.document(), 0));
  EXPECT_EQ(loose.text(), QStringLiteral("1. a\n\n2. b"));
}

TEST(MdEditOpsTest, RenumberLeavesBulletsFencesAndCorrectListsAlone) {
  Editor bullets(QStringLiteral("- a\n- b"));
  EXPECT_FALSE(renumberOrderedList(bullets.document(), 0));
  Editor fence(QStringLiteral("```\n1. a\n3. b\n```"));
  EXPECT_FALSE(renumberOrderedList(fence.document(), static_cast<int>(fence.text().indexOf(QStringLiteral("3.")))));
  EXPECT_EQ(fence.text(), QStringLiteral("```\n1. a\n3. b\n```"));
  Editor fine(QStringLiteral("1. a\n2. b"));
  EXPECT_FALSE(renumberOrderedList(fine.document(), 0));
}

TEST(MdEditOpsTest, RenumberJoinedToTheEditIsOneUndoStep) {
  Editor editor(QStringLiteral("1. a\n2. b\n3. c"));
  QTextCursor cursor(editor.document());
  cursor.beginEditBlock();
  cursor.setPosition(static_cast<int>(editor.text().indexOf(QStringLiteral("2."))));
  cursor.setPosition(static_cast<int>(editor.text().indexOf(QStringLiteral("3."))), QTextCursor::KeepAnchor);
  cursor.removeSelectedText();
  cursor.endEditBlock();
  ASSERT_EQ(editor.text(), QStringLiteral("1. a\n3. c"));
  EXPECT_TRUE(renumberOrderedList(editor.document(), static_cast<int>(editor.text().indexOf(QStringLiteral("3."))), true));
  EXPECT_EQ(editor.text(), QStringLiteral("1. a\n2. c"));
  editor.document()->undo();
  EXPECT_EQ(editor.text(), QStringLiteral("1. a\n2. b\n3. c"));
}
