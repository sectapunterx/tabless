// Coverage for AppController::appendNoteEntry — the C++ side of the
// Quick-capture Notes popup (Ctrl+Shift+N). Verifies the empty-input
// guard, the first-entry vs subsequent-entry formatting branches, the
// trimming behavior, and the notesStateChanged signal contract.
//
// Runs headless via the same QApplication + offscreen QPA +
// QStandardPaths test-mode boot the selection suite uses, so the tests
// never touch the user's real state.json.

#include "AppController.h"
#include "Models.h"

#include "notes/NoteChain.h"
#include "notes/NoteLinks.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>

#include <gtest/gtest.h>

namespace {

// The timestamp header lives on its own line as "### YYYY-MM-DD HH:MM".
// Tests compare against this regex instead of hard-coding the current
// clock so the suite stays deterministic even if the minute rolls over
// mid-test.
const QRegularExpression kStampLineRe(QStringLiteral(R"(^### \d{4}-\d{2}-\d{2} \d{2}:\d{2}$)"));

}  // namespace

class NotesAppendTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    // Quick notes go to the Inbox; with it open, notesState is its body.
    app_->notes()->reset({});
    app_->newNote(QStringLiteral("Inbox"));
    app_->setNotesState(QString());
  }

  void TearDown() override {
    app_.reset();
  }

  std::unique_ptr<AppController> app_;
};

// ─── empty-input guard ───────────────────────────────────────────────

TEST_F(NotesAppendTest, EmptyTextIsNoOp) {
  app_->setNotesState(QStringLiteral("prev content"));
  QSignalSpy spy(app_.get(), &AppController::notesStateChanged);

  app_->appendNoteEntry(QString());

  EXPECT_EQ(app_->notesState(), QStringLiteral("prev content"));
  EXPECT_EQ(spy.count(), 0);
}

TEST_F(NotesAppendTest, WhitespaceOnlyTextIsNoOp) {
  app_->setNotesState(QStringLiteral("prev"));
  QSignalSpy spy(app_.get(), &AppController::notesStateChanged);

  app_->appendNoteEntry(QStringLiteral("   \n\t  \n  "));

  EXPECT_EQ(app_->notesState(), QStringLiteral("prev"));
  EXPECT_EQ(spy.count(), 0);
}

// ─── first-entry formatting ──────────────────────────────────────────

TEST_F(NotesAppendTest, FirstEntryEmitsHeadingThenBody) {
  ASSERT_TRUE(app_->notesState().isEmpty());
  QSignalSpy spy(app_.get(), &AppController::notesStateChanged);

  app_->appendNoteEntry(QStringLiteral("hello world"));

  const QStringList lines = app_->notesState().split(QLatin1Char('\n'));
  ASSERT_EQ(lines.size(), 3);
  EXPECT_TRUE(kStampLineRe.match(lines[0]).hasMatch()) << "first line should be '### YYYY-MM-DD HH:MM', got: " << lines[0].toStdString();
  EXPECT_EQ(lines[1], QString());
  EXPECT_EQ(lines[2], QStringLiteral("hello world"));
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(NotesAppendTest, FirstEntryWhenStateIsWhitespaceOnly) {
  // Whitespace-only previous state must behave like empty — no separator
  // and no leading blank lines.
  app_->setNotesState(QStringLiteral("   \n\n  \n"));
  QSignalSpy spy(app_.get(), &AppController::notesStateChanged);

  app_->appendNoteEntry(QStringLiteral("body"));

  EXPECT_FALSE(app_->notesState().contains(QStringLiteral("----")));
  EXPECT_TRUE(app_->notesState().endsWith(QStringLiteral("\n\nbody")));
  EXPECT_EQ(spy.count(), 1);
}

// ─── subsequent entries ──────────────────────────────────────────────

TEST_F(NotesAppendTest, SecondEntryInsertsSeparatorAndHeading) {
  app_->setNotesState(QStringLiteral("first note"));
  QSignalSpy spy(app_.get(), &AppController::notesStateChanged);

  app_->appendNoteEntry(QStringLiteral("second note"));

  // Expected layout, one element per "\n"-delimited line:
  //
  //   first note
  //                       ← blank (HR needs preceding blank line)
  //   ----                ← HR (NotesHighlighter HR rule)
  //   ### YYYY-MM-DD HH:MM
  //                       ← blank (heading body separator)
  //   second note
  const QString out = app_->notesState();
  const QStringList lines = out.split(QLatin1Char('\n'));

  ASSERT_EQ(lines.size(), 6);
  EXPECT_EQ(lines[0], QStringLiteral("first note"));
  EXPECT_EQ(lines[1], QString());
  EXPECT_EQ(lines[2], QStringLiteral("----"));
  EXPECT_TRUE(kStampLineRe.match(lines[3]).hasMatch()) << "expected stamp heading on line 4, got: " << lines[3].toStdString();
  EXPECT_EQ(lines[4], QString());
  EXPECT_EQ(lines[5], QStringLiteral("second note"));
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(NotesAppendTest, TrailingNewlinesAreCollapsedBeforeSeparator) {
  // Existing text with stray trailing newlines must not produce more
  // than two blank lines between the old body and the "----" separator.
  app_->setNotesState(QStringLiteral("old body\n\n\n\n"));

  app_->appendNoteEntry(QStringLiteral("new body"));

  // The contract is exactly two blank lines before the separator.
  EXPECT_TRUE(app_->notesState().contains(QStringLiteral("old body\n\n----\n")));
  EXPECT_FALSE(app_->notesState().contains(QStringLiteral("\n\n\n----")));
}

TEST_F(NotesAppendTest, BodyIsTrimmed) {
  app_->appendNoteEntry(QStringLiteral("\n\n  padded note  \n\n"));

  EXPECT_TRUE(app_->notesState().endsWith(QStringLiteral("padded note")));
  EXPECT_FALSE(app_->notesState().endsWith(QStringLiteral("\n")));
}

TEST_F(NotesAppendTest, MultilineBodyIsPreserved) {
  app_->appendNoteEntry(QStringLiteral("line one\nline two\nline three"));

  EXPECT_TRUE(app_->notesState().contains(QStringLiteral("line one\nline two\nline three")));
}

TEST_F(NotesAppendTest, ManyEntriesProduceOneSeparatorPerBoundary) {
  app_->appendNoteEntry(QStringLiteral("a"));
  app_->appendNoteEntry(QStringLiteral("b"));
  app_->appendNoteEntry(QStringLiteral("c"));

  const int separators = app_->notesState().count(QStringLiteral("\n----\n"));
  EXPECT_EQ(separators, 2) << "three entries should have exactly two '----' separators";
}

// ─── signal contract ─────────────────────────────────────────────────

TEST_F(NotesAppendTest, EmitsNotesStateChangedExactlyOncePerAppend) {
  QSignalSpy spy(app_.get(), &AppController::notesStateChanged);

  app_->appendNoteEntry(QStringLiteral("one"));
  app_->appendNoteEntry(QStringLiteral("two"));

  EXPECT_EQ(spy.count(), 2);
}

TEST_F(NotesAppendTest, NoSignalWhenInputRejected) {
  QSignalSpy spy(app_.get(), &AppController::notesStateChanged);

  app_->appendNoteEntry(QString());
  app_->appendNoteEntry(QStringLiteral("   "));
  app_->appendNoteEntry(QStringLiteral("\n\t\n"));

  EXPECT_EQ(spy.count(), 0);
}

// ─── Wiki-links / backlinks (HEAP-79) ─────────────────────────────────

TEST(NoteLinks, CollectsHeadingsDeduped) {
  const QString md = QStringLiteral("# Alpha\n\ntext\n## Beta\n### Alpha\n#### \n");
  const QStringList h = heap::notes::collectHeadings(md);
  ASSERT_EQ(h.size(), 2);  // "Alpha" deduped, empty heading skipped
  EXPECT_EQ(h.at(0), QString("Alpha"));
  EXPECT_EQ(h.at(1), QString("Beta"));
}

TEST(NoteLinks, BacklinksGroupByTargetWithResolution) {
  const QString md = QStringLiteral("# Alpha\n\nsee [[Beta]] here\n\n## Beta\n\nrefers to [[Alpha]] and [[Ghost]]\n");
  const QVariantList bl = heap::notes::collectBacklinks(md);
  // Targets: Alpha, Beta, Ghost (sorted, case-insensitive).
  ASSERT_EQ(bl.size(), 3);
  const QVariantMap alpha = bl.at(0).toMap();
  EXPECT_EQ(alpha.value("target").toString(), QString("Alpha"));
  EXPECT_TRUE(alpha.value("resolved").toBool());  // heading "# Alpha" exists
  ASSERT_EQ(alpha.value("refs").toList().size(), 1);
  EXPECT_EQ(alpha.value("refs").toList().at(0).toMap().value("line").toInt(), 7);

  const QVariantMap ghost = bl.at(2).toMap();
  EXPECT_EQ(ghost.value("target").toString(), QString("Ghost"));
  EXPECT_FALSE(ghost.value("resolved").toBool());  // no such heading
}

TEST(NoteLinks, HeadingOffsetFindsCaseInsensitive) {
  const QString md = QStringLiteral("# Alpha\n\n## Beta gamma\n");
  const int off = heap::notes::headingOffset(md, QStringLiteral("beta gamma"));
  EXPECT_EQ(md.mid(off, 12), QString("## Beta gamm"));
  EXPECT_EQ(heap::notes::headingOffset(md, QStringLiteral("nope")), -1);
}

// ─── headless boot (mirrors test_selection.cpp) ──────────────────────

// ─── Many notes instead of one blob (schema v8) ───────────────────────
//
// The delicate part is not the CRUD, it is the alias. `notesState` is what the
// editor binds to and what appendNoteEntry() writes, and it is now the body of
// whichever note is open. Anywhere those two drift apart, an edit survives
// until the next switch and then disappears.

class NotesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->notes()->reset({});
    app_->setNotesState(QString());
  }

  void TearDown() override {
    app_.reset();
  }

  int count() const {
    return app_->notes()->rowCount();
  }

  QString titleOf(const QString& id) const {
    const int row = app_->notes()->indexOfId(id);
    return row >= 0 ? app_->notes()->items().at(row).title : QString();
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(NotesTest, ANewNoteIsCreatedAndOpened) {
  const QString id = app_->newNote(QStringLiteral("Standup"));

  EXPECT_EQ(count(), 1);
  EXPECT_EQ(app_->activeNoteId(), id);
  EXPECT_EQ(titleOf(id), QStringLiteral("Standup"));
}

// A note with no name cannot be found in a list.
TEST_F(NotesTest, ANewNoteWithNoTitleStillHasOne) {
  const QString id = app_->newNote();

  EXPECT_FALSE(titleOf(id).isEmpty());
}

// It opens saying what it is rather than as an empty page.
TEST_F(NotesTest, ANewNoteStartsWithItsHeading) {
  const QString id = app_->newNote(QStringLiteral("Standup"));

  EXPECT_TRUE(app_->noteBody(id).contains(QStringLiteral("# Standup")));
}

// ── The alias ──

TEST_F(NotesTest, NotesStateIsTheActiveNotesBody) {
  const QString id = app_->newNote(QStringLiteral("One"));

  app_->setNotesState(QStringLiteral("typed into the editor"));

  EXPECT_EQ(app_->noteBody(id), QStringLiteral("typed into the editor"));
}

// The failure this file exists for: type, switch away, switch back, and find
// what you typed still there.
TEST_F(NotesTest, TypingThenSwitchingKeepsWhatWasTyped) {
  const QString first = app_->newNote(QStringLiteral("One"));
  const QString second = app_->newNote(QStringLiteral("Two"));

  app_->setActiveNoteId(first);
  app_->setNotesState(QStringLiteral("first body"));
  app_->setActiveNoteId(second);
  app_->setNotesState(QStringLiteral("second body"));
  app_->setActiveNoteId(first);

  EXPECT_EQ(app_->notesState(), QStringLiteral("first body"));
  EXPECT_EQ(app_->noteBody(second), QStringLiteral("second body"));
}

TEST_F(NotesTest, SwitchingNotesReplacesWhatTheEditorShows) {
  const QString first = app_->newNote(QStringLiteral("One"));
  app_->setNotesState(QStringLiteral("first"));
  const QString second = app_->newNote(QStringLiteral("Two"));
  app_->setNotesState(QStringLiteral("second"));

  app_->setActiveNoteId(first);

  EXPECT_EQ(app_->notesState(), QStringLiteral("first"));
}

TEST_F(NotesTest, SwitchingNotesTellsTheEditorToReload) {
  const QString first = app_->newNote(QStringLiteral("One"));
  const QString second = app_->newNote(QStringLiteral("Two"));
  QSignalSpy spy(app_.get(), &AppController::notesStateChanged);

  app_->setActiveNoteId(first);

  EXPECT_GE(spy.count(), 1);
  EXPECT_NE(app_->activeNoteId(), second);
}

// Writing a body straight to a note the user is not looking at must not
// silently change what the editor shows.
TEST_F(NotesTest, WritingToAnInactiveNoteLeavesTheEditorAlone) {
  const QString first = app_->newNote(QStringLiteral("One"));
  const QString second = app_->newNote(QStringLiteral("Two"));
  app_->setNotesState(QStringLiteral("second body"));

  app_->setNoteBody(first, QStringLiteral("written behind the scenes"));

  EXPECT_EQ(app_->notesState(), QStringLiteral("second body"));
  EXPECT_EQ(app_->noteBody(first), QStringLiteral("written behind the scenes"));
}

// QuickCapture appends to notesState; it has to land in a real note.
TEST_F(NotesTest, AppendingAnEntryReachesTheActiveNote) {
  const QString id = app_->newNote(QStringLiteral("Inbox"));
  app_->setNotesState(QString());

  app_->appendNoteEntry(QStringLiteral("something captured"));

  EXPECT_TRUE(app_->noteBody(id).contains(QStringLiteral("something captured")));
}

// APP-320 (owner, 2026-10-16): a quick note goes on in the last note when that
// one was made or written to less than an hour ago under the same label, and
// starts a note of its own after a longer pause or under another label. The
// open note stays open either way.
TEST_F(NotesTest, AQuickNoteGoesOnInTheLastNoteWithinTheHour) {
  const QDateTime t0(QDate(2026, 10, 16), QTime(10, 0));
  const QString first = app_->appendNoteEntryAt(QStringLiteral("standup notes\nmore"), QString(), t0);
  ASSERT_FALSE(first.isEmpty());
  EXPECT_EQ(titleOf(first), QStringLiteral("standup notes")) << "named by its first words";
  EXPECT_EQ(app_->quickNoteTargetAt(QString(), t0.addSecs(59 * 60)), titleOf(first));
  EXPECT_EQ(app_->appendNoteEntryAt(QStringLiteral("second"), QString(), t0.addSecs(59 * 60)), first);
  EXPECT_EQ(app_->noteBody(first).count(QStringLiteral("----")), 1);
  // The hour counts from the last entry, not the first.
  EXPECT_EQ(app_->appendNoteEntryAt(QStringLiteral("third"), QString(), t0.addSecs(110 * 60)), first);
  // A pause of an hour or more: a new note.
  EXPECT_TRUE(app_->quickNoteTargetAt(QString(), t0.addSecs(170 * 60)).isEmpty());
  const QString later = app_->appendNoteEntryAt(QStringLiteral("after lunch"), QString(), t0.addSecs(170 * 60));
  EXPECT_NE(later, first);
  EXPECT_EQ(app_->notes()->rowCount(), 2);
}

TEST_F(NotesTest, AnotherLabelStartsANoteAndTheLabelIsATag) {
  const QDateTime t0(QDate(2026, 10, 16), QTime(10, 0));
  const QString work = app_->appendNoteEntryAt(QStringLiteral("deploy at 5"), QStringLiteral("работа"), t0);
  const QString hr = app_->appendNoteEntryAt(QStringLiteral("vacation form"), QStringLiteral("#HR"), t0.addSecs(60));
  EXPECT_NE(hr, work);
  EXPECT_TRUE(app_->noteBody(hr).contains(QStringLiteral("#hr")));
  EXPECT_EQ(app_->appendNoteEntryAt(QStringLiteral("and the certificate"), QStringLiteral("hr"), t0.addSecs(120)), hr);
  // The label filter finds the tag exactly: "#hr", not "#hrm".
  const QStringList found = app_->notesMatching(QStringLiteral("#hr"));
  EXPECT_TRUE(found.contains(hr));
  EXPECT_FALSE(found.contains(work));
  EXPECT_TRUE(app_->noteLabels().contains(QStringLiteral("hr")));
}

// A note made by hand starts the hour too ("quick capture or not").
TEST_F(NotesTest, ANoteMadeByHandTakesTheNextQuickNote) {
  const QString open = app_->newNote(QStringLiteral("Design"));
  app_->appendNoteEntry(QStringLiteral("captured right after"));
  EXPECT_TRUE(app_->noteBody(open).contains(QStringLiteral("captured right after")));
  EXPECT_FALSE(app_->noteBody(open).contains(QStringLiteral("----"))) << "no rule under the note's own heading";
  EXPECT_EQ(app_->activeNoteId(), open);
}

// R2-069: the quick note's draft and its task are kept in settings until
// saved, so a quit or a crash does not take them.
TEST_F(NotesTest, TheQuickNoteDraftIsKeptUntilCleared) {
  EXPECT_TRUE(app_->quickNoteDraft().isEmpty());
  app_->setQuickNoteDraft(QStringLiteral("half a thought"), QStringLiteral("APP-101"));
  QVariantMap d = app_->quickNoteDraft();
  EXPECT_EQ(d.value(QStringLiteral("text")).toString(), QStringLiteral("half a thought"));
  EXPECT_EQ(d.value(QStringLiteral("attachId")).toString(), QStringLiteral("APP-101"));
  app_->setQuickNoteDraft(QStringLiteral("  "), QString());
  EXPECT_TRUE(app_->quickNoteDraft().isEmpty());
}

// ── Deleting ──

TEST_F(NotesTest, DeletingANoteRemovesIt) {
  const QString id = app_->newNote(QStringLiteral("One"));

  app_->deleteNote(id);

  EXPECT_EQ(count(), 0);
}

// An empty editor after a delete reads as the rest of the notes having gone
// too, so the selection lands on a neighbour.
TEST_F(NotesTest, DeletingTheOpenNoteOpensANeighbour) {
  const QString first = app_->newNote(QStringLiteral("One"));
  const QString second = app_->newNote(QStringLiteral("Two"));
  app_->setActiveNoteId(second);

  app_->deleteNote(second);

  EXPECT_EQ(app_->activeNoteId(), first);
  EXPECT_EQ(app_->notesState(), app_->noteBody(first));
}

TEST_F(NotesTest, DeletingTheLastNoteLeavesNothingOpen) {
  const QString id = app_->newNote(QStringLiteral("Only"));

  app_->deleteNote(id);

  EXPECT_TRUE(app_->activeNoteId().isEmpty());
  EXPECT_TRUE(app_->notesState().isEmpty());
}

TEST_F(NotesTest, DeletingANoteThatIsNotOpenLeavesTheEditorAlone) {
  const QString first = app_->newNote(QStringLiteral("One"));
  const QString second = app_->newNote(QStringLiteral("Two"));
  app_->setNotesState(QStringLiteral("second body"));

  app_->deleteNote(first);

  EXPECT_EQ(app_->activeNoteId(), second);
  EXPECT_EQ(app_->notesState(), QStringLiteral("second body"));
}

TEST_F(NotesTest, DeletingSomethingThatIsNotThereDoesNothing) {
  app_->newNote(QStringLiteral("One"));

  app_->deleteNote(QStringLiteral("nobody"));

  EXPECT_EQ(count(), 1);
}

// ── Renaming, pinning, filing ──

TEST_F(NotesTest, RenamingChangesTheTitle) {
  const QString id = app_->newNote(QStringLiteral("Old"));

  app_->renameNote(id, QStringLiteral("New"));

  EXPECT_EQ(titleOf(id), QStringLiteral("New"));
}

// A blank name would make the note unfindable, so it is refused rather than
// applied.
TEST_F(NotesTest, RenamingToNothingIsRefused) {
  const QString id = app_->newNote(QStringLiteral("Keep"));

  app_->renameNote(id, QStringLiteral("   "));

  EXPECT_EQ(titleOf(id), QStringLiteral("Keep"));
}

TEST_F(NotesTest, PinningIsRemembered) {
  const QString id = app_->newNote(QStringLiteral("One"));

  app_->setNotePinned(id, true);

  const int row = app_->notes()->indexOfId(id);
  EXPECT_TRUE(app_->notes()->items().at(row).pinned);
}

TEST_F(NotesTest, AFolderIsRemembered) {
  const QString id = app_->newNote(QStringLiteral("One"));

  app_->moveNoteToFolder(id, QStringLiteral("meetings/2026"));

  const int row = app_->notes()->indexOfId(id);
  EXPECT_EQ(app_->notes()->items().at(row).folder, QStringLiteral("meetings/2026"));
}

// "/meetings/" and "meetings" are one folder, not two — they have to be, or
// the tree grows a duplicate branch nobody typed.
TEST_F(NotesTest, AFolderPathIsNormalised) {
  const QString id = app_->newNote(QStringLiteral("One"));

  app_->moveNoteToFolder(id, QStringLiteral("/meetings/"));

  const int row = app_->notes()->indexOfId(id);
  EXPECT_EQ(app_->notes()->items().at(row).folder, QStringLiteral("meetings"));
}

TEST_F(NotesTest, FoldersAreListedOnceAndSorted) {
  app_->moveNoteToFolder(app_->newNote(QStringLiteral("a")), QStringLiteral("zeta"));
  app_->moveNoteToFolder(app_->newNote(QStringLiteral("b")), QStringLiteral("alpha"));
  app_->moveNoteToFolder(app_->newNote(QStringLiteral("c")), QStringLiteral("alpha"));

  EXPECT_EQ(app_->noteFolders(), (QStringList{QStringLiteral("alpha"), QStringLiteral("zeta")}));
}

TEST_F(NotesTest, ANoteWithNoFolderIsNotAFolder) {
  app_->newNote(QStringLiteral("loose"));

  EXPECT_TRUE(app_->noteFolders().isEmpty());
}

// ── The model ──

// The list carries no bodies: fifty notes would be fifty documents crossing the
// QML boundary on every repaint.
TEST_F(NotesTest, TheModelDoesNotCarryBodies) {
  app_->newNote(QStringLiteral("One"));

  EXPECT_LT(app_->notes()->roleOf(QStringLiteral("body")), 0);
  EXPECT_GE(app_->notes()->roleOf(QStringLiteral("title")), 0);
}

// It does carry a preview, so a row can say what the note is about.
TEST_F(NotesTest, TheModelCarriesAnExcerpt) {
  const QString id = app_->newNote(QStringLiteral("Standup"));
  app_->setNoteBody(id, QStringLiteral("# Standup\n\nthe first real line\n"));

  const int role = app_->notes()->roleOf(QStringLiteral("excerpt"));
  ASSERT_GE(role, 0);
  const int row = app_->notes()->indexOfId(id);
  EXPECT_EQ(app_->notes()->data(app_->notes()->index(row, 0), role).toString(), QStringLiteral("the first real line"));
}

// Repeating the heading back tells the reader nothing they cannot already see.
TEST_F(NotesTest, TheExcerptSkipsTheHeading) {
  const QString id = app_->newNote(QStringLiteral("Title"));
  app_->setNoteBody(id, QStringLiteral("# Title\n\n## Sub\n\nbody text"));

  const int role = app_->notes()->roleOf(QStringLiteral("excerpt"));
  const int row = app_->notes()->indexOfId(id);
  const QString excerpt = app_->notes()->data(app_->notes()->index(row, 0), role).toString();

  EXPECT_NE(excerpt, QStringLiteral("Title"));
  EXPECT_FALSE(excerpt.startsWith(QLatin1Char('#')));
}

// A link to a note whose name has an escaped | reads as the whole name, and a
// labelled one as its label, not as the half after the escaped bar (KNOW-6).
TEST_F(NotesTest, TheExcerptReadsEscapedLinkNamesWhole) {
  const QString id = app_->newNote(QStringLiteral("Links"));
  app_->setNoteBody(id, QStringLiteral("# Links\n\nsee [[A\\|B options]] and [[C\\# basics|intro]]\n"));

  const int role = app_->notes()->roleOf(QStringLiteral("excerpt"));
  const int row = app_->notes()->indexOfId(id);
  EXPECT_EQ(app_->notes()->data(app_->notes()->index(row, 0), role).toString(), QStringLiteral("see A|B options and intro"));
}

// ─── Notes as a folder of .md files ──────────────────────────────────
//
// MdVault.h covers the naming and the frontmatter without a filesystem; these
// go through the disk, because the parts that break there are the walk and the
// second import.

class NotesVaultTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    // Both halves: emptying the model alone leaves the loaded profile's text
    // in notesState with no note behind it, which the next switch adopts.
    app_->notes()->reset({});
    app_->setNotesState(QString());
    ASSERT_TRUE(dir_.isValid());
  }

  void TearDown() override {
    app_.reset();
  }

  QUrl vaultUrl() const {
    return QUrl::fromLocalFile(dir_.path());
  }

  void writeFile(const QString& relative, const QString& contents) const {
    const QString full = dir_.path() + QLatin1Char('/') + relative;
    QDir().mkpath(QFileInfo(full).absolutePath());
    QFile f(full);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(contents.toUtf8());
  }

  QTemporaryDir dir_;
  std::unique_ptr<AppController> app_;
};

TEST_F(NotesVaultTest, AFolderOfMarkdownBecomesNotes) {
  writeFile(QStringLiteral("Standup.md"), QStringLiteral("body one"));
  writeFile(QStringLiteral("meetings/Retro.md"), QStringLiteral("body two"));

  const QVariantMap r = app_->importNotesFolder(vaultUrl());

  EXPECT_EQ(r.value(QStringLiteral("imported")).toInt(), 2);
  EXPECT_EQ(app_->notes()->rowCount(), 2);
}

TEST_F(NotesVaultTest, ADirectoryBecomesAFolder) {
  writeFile(QStringLiteral("meetings/2026/Retro.md"), QStringLiteral("body"));

  app_->importNotesFolder(vaultUrl());

  ASSERT_EQ(app_->notes()->rowCount(), 1);
  EXPECT_EQ(app_->notes()->items().at(0).folder, QStringLiteral("meetings/2026"));
}

// A vault is edited elsewhere, so importing it again is the normal case — and
// it has to update rather than double every note.
TEST_F(NotesVaultTest, ImportingTheSameVaultTwiceUpdatesRatherThanDoubles) {
  writeFile(QStringLiteral("Standup.md"), QStringLiteral("first"));
  app_->importNotesFolder(vaultUrl());

  writeFile(QStringLiteral("Standup.md"), QStringLiteral("second"));
  const QVariantMap r = app_->importNotesFolder(vaultUrl());

  EXPECT_EQ(app_->notes()->rowCount(), 1);
  EXPECT_EQ(r.value(QStringLiteral("updated")).toInt(), 1);
  EXPECT_EQ(app_->notes()->items().at(0).body, QStringLiteral("second"));
}

// Obsidian keeps its settings in .obsidian and its deletions in .trash.
TEST_F(NotesVaultTest, ObsidiansOwnDirectoriesAreNotImported) {
  writeFile(QStringLiteral("Real.md"), QStringLiteral("body"));
  writeFile(QStringLiteral(".obsidian/workspace.md"), QStringLiteral("config"));
  writeFile(QStringLiteral(".trash/Deleted.md"), QStringLiteral("gone"));

  app_->importNotesFolder(vaultUrl());

  ASSERT_EQ(app_->notes()->rowCount(), 1);
  EXPECT_EQ(app_->notes()->items().at(0).title, QStringLiteral("Real"));
}

TEST_F(NotesVaultTest, NonMarkdownFilesAreNotImported) {
  writeFile(QStringLiteral("Real.md"), QStringLiteral("body"));
  writeFile(QStringLiteral("image.png"), QStringLiteral("not markdown"));
  writeFile(QStringLiteral("notes.txt"), QStringLiteral("also not"));

  app_->importNotesFolder(vaultUrl());

  EXPECT_EQ(app_->notes()->rowCount(), 1);
}

TEST_F(NotesVaultTest, AMissingFolderIsReportedRatherThanCrashing) {
  const QVariantMap r = app_->importNotesFolder(QUrl::fromLocalFile(dir_.path() + QStringLiteral("/nope")));

  EXPECT_FALSE(r.value(QStringLiteral("error")).toString().isEmpty());
  EXPECT_EQ(app_->notes()->rowCount(), 0);
}

TEST_F(NotesVaultTest, AnEmptyFolderImportsNothing) {
  const QVariantMap r = app_->importNotesFolder(vaultUrl());

  EXPECT_EQ(r.value(QStringLiteral("imported")).toInt(), 0);
  EXPECT_TRUE(r.value(QStringLiteral("error")).toString().isEmpty());
}

TEST_F(NotesVaultTest, ExportWritesAFilePerNote) {
  app_->newNote(QStringLiteral("One"));
  app_->newNote(QStringLiteral("Two"));

  const QVariantMap r = app_->exportNotesFolder(vaultUrl(), QStringLiteral("out"));

  EXPECT_EQ(r.value(QStringLiteral("written")).toInt(), 2);
  EXPECT_TRUE(QFile::exists(dir_.path() + QStringLiteral("/out/One.md")));
}

TEST_F(NotesVaultTest, ExportCreatesTheDirectoryForAFolder) {
  const QString id = app_->newNote(QStringLiteral("Retro"));
  app_->moveNoteToFolder(id, QStringLiteral("meetings/2026"));

  app_->exportNotesFolder(vaultUrl(), QStringLiteral("out"));

  EXPECT_TRUE(QFile::exists(dir_.path() + QStringLiteral("/out/meetings/2026/Retro.md")));
}

// The whole point: out to a folder, back in, and nothing has moved.
TEST_F(NotesVaultTest, NotesSurviveAnExportAndReimport) {
  const QString id = app_->newNote(QStringLiteral("Standup"));
  app_->moveNoteToFolder(id, QStringLiteral("meetings"));
  app_->setNoteBody(id, QStringLiteral("# Standup\n\nthe body\n"));
  app_->setNotePinned(id, true);

  const QString out = app_->exportNotesFolder(vaultUrl()).value(QStringLiteral("folder")).toString();
  app_->notes()->reset({});
  app_->setNotesState(QString());
  app_->importNotesFolder(QUrl::fromLocalFile(out));

  ASSERT_EQ(app_->notes()->rowCount(), 1);
  const Note back = app_->notes()->items().at(0);
  EXPECT_EQ(back.title, QStringLiteral("Standup"));
  EXPECT_EQ(back.folder, QStringLiteral("meetings"));
  EXPECT_EQ(back.body, QStringLiteral("# Standup\n\nthe body\n"));
  EXPECT_TRUE(back.pinned);
}

// ─── Links between notes, through the controller ─────────────────────
//
// NoteGraph.h covers the resolution rules; these cover what AppController adds:
// which note the link was written in, and what happens when it points nowhere.

class NoteLinkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->notes()->reset({});
    app_->setNotesState(QString());
  }

  void TearDown() override {
    app_.reset();
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(NoteLinkTest, ALinkResolvesToANote) {
  const QString target = app_->newNote(QStringLiteral("Standup"));
  app_->newNote(QStringLiteral("Diary"));

  const QVariantMap hit = app_->resolveNoteLink(QStringLiteral("Standup"));

  EXPECT_EQ(hit.value(QStringLiteral("kind")).toString(), QStringLiteral("note"));
  EXPECT_EQ(hit.value(QStringLiteral("noteId")).toString(), target);
}

// A heading resolves only inside the note the link was written in, which means
// the answer depends on which note is open.
TEST_F(NoteLinkTest, AHeadingResolvesOnlyInTheOpenNote) {
  const QString withHeading = app_->newNote(QStringLiteral("A"));
  app_->setNoteBody(withHeading, QStringLiteral("## Risks\n\nbody"));
  const QString other = app_->newNote(QStringLiteral("B"));

  app_->setActiveNoteId(other);
  EXPECT_EQ(app_->resolveNoteLink(QStringLiteral("Risks")).value(QStringLiteral("kind")).toString(), QStringLiteral("missing"));

  app_->setActiveNoteId(withHeading);
  EXPECT_EQ(app_->resolveNoteLink(QStringLiteral("Risks")).value(QStringLiteral("kind")).toString(), QStringLiteral("heading"));
}

TEST_F(NoteLinkTest, AnUnknownTargetIsReportedAsMissingWithItsName) {
  app_->newNote(QStringLiteral("Diary"));

  const QVariantMap hit = app_->resolveNoteLink(QStringLiteral("Ghost"));

  EXPECT_EQ(hit.value(QStringLiteral("kind")).toString(), QStringLiteral("missing"));
  EXPECT_EQ(hit.value(QStringLiteral("title")).toString(), QStringLiteral("Ghost"));
}

TEST_F(NoteLinkTest, BacklinksAreReportedAcrossNotes) {
  const QString target = app_->newNote(QStringLiteral("Standup"));
  const QString other = app_->newNote(QStringLiteral("Diary"));
  app_->setNoteBody(other, QStringLiteral("see [[Standup]]"));

  const QVariantList back = app_->backlinksToNote(target);

  ASSERT_EQ(back.size(), 1);
  EXPECT_EQ(back.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Diary"));
}

TEST_F(NoteLinkTest, UnresolvedLinksInTheOpenNoteAreListed) {
  const QString id = app_->newNote(QStringLiteral("Diary"));
  app_->setNotesState(QStringLiteral("see [[Ghost]] and [[Diary]]"));

  EXPECT_EQ(app_->unresolvedNoteLinks(), QStringList{QStringLiteral("Ghost")});
}

// A broken link is usually a note somebody meant to write.
TEST_F(NoteLinkTest, CreatingANoteForALinkMakesItAndOpensIt) {
  app_->newNote(QStringLiteral("Diary"));

  const QString id = app_->createNoteForLink(QStringLiteral("Ghost"));

  EXPECT_FALSE(id.isEmpty());
  EXPECT_EQ(app_->activeNoteId(), id);
  EXPECT_EQ(app_->resolveNoteLink(QStringLiteral("Ghost")).value(QStringLiteral("kind")).toString(), QStringLiteral("note"));
}

// A second note with the same name would leave the link ambiguous forever.
TEST_F(NoteLinkTest, CreatingANoteForALinkThatExistsOpensItInstead) {
  const QString existing = app_->newNote(QStringLiteral("Standup"));
  app_->newNote(QStringLiteral("Diary"));
  const int before = app_->notes()->rowCount();

  const QString id = app_->createNoteForLink(QStringLiteral("Standup"));

  EXPECT_EQ(id, existing);
  EXPECT_EQ(app_->notes()->rowCount(), before);
}

// A note created from a link belongs with its neighbours, not at the root.
TEST_F(NoteLinkTest, ANoteCreatedFromALinkIsFiledBesideTheNoteThatAskedForIt) {
  const QString from = app_->newNote(QStringLiteral("Diary"));
  app_->moveNoteToFolder(from, QStringLiteral("journal"));
  app_->setActiveNoteId(from);

  const QString made = app_->createNoteForLink(QStringLiteral("Ghost"));

  const int row = app_->notes()->indexOfId(made);
  EXPECT_EQ(app_->notes()->items().at(row).folder, QStringLiteral("journal"));
}

TEST_F(NoteLinkTest, CreatingANoteForAnEmptyLinkDoesNothing) {
  const int before = app_->notes()->rowCount();

  EXPECT_TRUE(app_->createNoteForLink(QStringLiteral("   ")).isEmpty());
  EXPECT_EQ(app_->notes()->rowCount(), before);
}

// ── The daily note ──

TEST_F(NoteLinkTest, TheDailyNoteIsCreatedOnFirstUse) {
  const QString id = app_->openDailyNote();

  EXPECT_FALSE(id.isEmpty());
  EXPECT_EQ(app_->activeNoteId(), id);
  const int row = app_->notes()->indexOfId(id);
  EXPECT_EQ(app_->notes()->items().at(row).title, QDate::currentDate().toString(Qt::ISODate));
}

// Asking twice in one day is the normal case: it must open the same note, not
// start a second one.
TEST_F(NoteLinkTest, TheDailyNoteIsReusedWithinADay) {
  const QString first = app_->openDailyNote();
  const int after = app_->notes()->rowCount();

  const QString second = app_->openDailyNote();

  EXPECT_EQ(second, first);
  EXPECT_EQ(app_->notes()->rowCount(), after);
}

// ISO, so the folder sorts chronologically in every list and on disk.
TEST_F(NoteLinkTest, TheDailyNoteIsFiledUnderDaily) {
  const QString id = app_->openDailyNote();

  const int row = app_->notes()->indexOfId(id);
  EXPECT_EQ(app_->notes()->items().at(row).folder, QStringLiteral("daily"));
}

// ─── Text with no note behind it ─────────────────────────────────────
//
// The bug these exist for, as reported: a note written with Ctrl+Shift+N, or
// typed into the editor without pressing "+", was shown in the editor, listed
// nowhere, and gone after a restart; and pressing "+" afterwards overwrote it.
//
// Two causes. Text that arrived with no note open lived only in `notesState`,
// which since notes became a list is no longer written to disk. And "+" asked
// for a new note before the editor's 250 ms debounce had flushed, so the switch
// replaced text that had not been saved anywhere yet.

class OrphanNoteTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->notes()->reset({});
    app_->setNotesState(QString());
  }

  void TearDown() override {
    app_.reset();
  }

  // The note holding `needle`, from whatever controller is asked.
  static QString noteHolding(AppController* app, const QString& needle) {
    for(const Note& n : app->notes()->items()) {
      if(n.body.contains(needle)) {
        return n.id;
      }
    }
    return {};
  }

  QString titleOf(const QString& id) const {
    const int row = app_->notes()->indexOfId(id);
    return row >= 0 ? app_->notes()->items().at(row).title : QString();
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(OrphanNoteTest, TypingWithNoNoteOpenMakesANote) {
  ASSERT_EQ(app_->notes()->rowCount(), 0);

  app_->setNotesState(QStringLiteral("typed straight into the editor"));

  ASSERT_EQ(app_->notes()->rowCount(), 1) << "the text has to be listed, not only shown";
  const QString id = app_->notes()->items().at(0).id;
  EXPECT_EQ(app_->activeNoteId(), id);
  EXPECT_EQ(app_->noteBody(id), QStringLiteral("typed straight into the editor"));
}

// An empty editor is not a note; making one per keystroke-then-delete would
// fill the list with blanks.
TEST_F(OrphanNoteTest, EmptyTextWithNoNoteOpenMakesNothing) {
  app_->setNotesState(QStringLiteral("   \n  "));

  EXPECT_EQ(app_->notes()->rowCount(), 0);
}

TEST_F(OrphanNoteTest, AdoptedTextKeepsBeingWrittenToTheSameNote) {
  app_->setNotesState(QStringLiteral("first draft"));
  const QString id = app_->activeNoteId();

  app_->setNotesState(QStringLiteral("first draft, and more"));

  EXPECT_EQ(app_->notes()->rowCount(), 1) << "one note, not one per save";
  EXPECT_EQ(app_->noteBody(id), QStringLiteral("first draft, and more"));
}

TEST_F(OrphanNoteTest, QuickCaptureWithNoNoteOpenOpensItsNote) {
  app_->appendNoteEntry(QStringLiteral("captured from anywhere"));

  ASSERT_EQ(app_->notes()->rowCount(), 1);
  const QString id = app_->activeNoteId();
  EXPECT_EQ(titleOf(id), QStringLiteral("captured from anywhere"));
  EXPECT_TRUE(app_->noteBody(id).contains(QStringLiteral("captured from anywhere")));
}

// A second capture in the same hour goes on in the same note.
TEST_F(OrphanNoteTest, QuickCaptureReusesItsNoteWithinTheHour) {
  app_->appendNoteEntry(QStringLiteral("first capture"));
  const QString note = app_->activeNoteId();
  app_->setActiveNoteId(QString());

  app_->appendNoteEntry(QStringLiteral("second capture"));

  EXPECT_EQ(app_->notes()->rowCount(), 1);
  EXPECT_TRUE(app_->noteBody(note).contains(QStringLiteral("first capture")));
  EXPECT_TRUE(app_->noteBody(note).contains(QStringLiteral("second capture")));
}

// A note open and written in for over an hour: the capture starts its own
// note; the open one keeps its text and stays open.
TEST_F(OrphanNoteTest, QuickCaptureAfterTheHourLeavesTheOpenNote) {
  const QDateTime t0(QDate(2026, 10, 16), QTime(9, 0));
  const QString open = app_->appendNoteEntryAt(QStringLiteral("Standup"), QString(), t0);
  app_->setActiveNoteId(open);

  app_->appendNoteEntryAt(QStringLiteral("captured while Standup is open"), QString(), t0.addSecs(2 * 3600));

  EXPECT_FALSE(app_->noteBody(open).contains(QStringLiteral("captured while Standup is open")));
  EXPECT_EQ(app_->activeNoteId(), open);
  EXPECT_EQ(app_->notes()->rowCount(), 2);
}

// The second half of the report: "+" after writing erased what was written.
TEST_F(OrphanNoteTest, PressingPlusAfterWritingKeepsWhatWasWritten) {
  app_->setNotesState(QStringLiteral("written before pressing plus"));
  const QString first = app_->activeNoteId();

  const QString second = app_->newNote();

  EXPECT_NE(first, second);
  EXPECT_EQ(app_->notes()->rowCount(), 2);
  EXPECT_EQ(app_->noteBody(first), QStringLiteral("written before pressing plus"));
  EXPECT_EQ(app_->activeNoteId(), second);
}

// The race: "+" inside the editor's debounce window. The editor flushes on
// aboutToChangeActiveNote, and whatever it writes then has to land in the note
// being left — before this, it landed in the new note and the old one lost it.
TEST_F(OrphanNoteTest, KeystrokesFlushedBeforeASwitchLandInTheNoteBeingLeft) {
  const QString first = app_->newNote(QStringLiteral("First"));
  QObject::connect(app_.get(), &AppController::aboutToChangeActiveNote, app_.get(), [this]() {
    app_->setNotesState(QStringLiteral("late keystrokes"));
  });

  const QString second = app_->newNote(QStringLiteral("Second"));

  EXPECT_EQ(app_->noteBody(first), QStringLiteral("late keystrokes"));
  EXPECT_TRUE(app_->noteBody(second).startsWith(QStringLiteral("# Second")));
}

// Same race with no note open at all: the flush adopts the text into a note of
// its own, and the switch then goes on to the new one.
TEST_F(OrphanNoteTest, AFlushWithNoNoteOpenIsAdoptedBeforeTheSwitch) {
  bool flushed = false;
  QObject::connect(app_.get(), &AppController::aboutToChangeActiveNote, app_.get(), [this, &flushed]() {
    if(!flushed) {
      flushed = true;
      app_->setNotesState(QStringLiteral("typed then plus, fast"));
    }
  });

  const QString made = app_->newNote(QStringLiteral("New"));

  EXPECT_EQ(app_->activeNoteId(), made);
  EXPECT_FALSE(noteHolding(app_.get(), QStringLiteral("typed then plus, fast")).isEmpty());
  EXPECT_NE(noteHolding(app_.get(), QStringLiteral("typed then plus, fast")), made);
}

TEST_F(OrphanNoteTest, SwitchingNotesAsksTheEditorToFlushFirst) {
  const QString a = app_->newNote(QStringLiteral("A"));
  const QString b = app_->newNote(QStringLiteral("B"));
  QSignalSpy spy(app_.get(), &AppController::aboutToChangeActiveNote);

  app_->setActiveNoteId(a);

  EXPECT_EQ(spy.count(), 1);
  EXPECT_NE(a, b);
}

TEST_F(OrphanNoteTest, DeletingANoteAsksTheEditorToFlushFirst) {
  const QString a = app_->newNote(QStringLiteral("A"));
  QSignalSpy spy(app_.get(), &AppController::aboutToChangeActiveNote);

  app_->deleteNote(a);

  EXPECT_GE(spy.count(), 1);
}

// Switching to the note already open is not a switch.
TEST_F(OrphanNoteTest, ReopeningTheOpenNoteDoesNotFlush) {
  const QString a = app_->newNote(QStringLiteral("A"));
  QSignalSpy spy(app_.get(), &AppController::aboutToChangeActiveNote);

  app_->setActiveNoteId(a);

  EXPECT_EQ(spy.count(), 0);
}

// ── Through the disk ──
//
// The only test of "it is still there after a restart" that means anything:
// save, throw the controller away, load a fresh one from the same file.

TEST_F(OrphanNoteTest, TextTypedWithNoNoteOpenSurvivesARestart) {
  const QString needle = QStringLiteral("orphan-restart-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
  app_->setNotesState(needle);
  app_->flushSave();
  app_.reset();

  AppController reloaded;

  EXPECT_FALSE(noteHolding(&reloaded, needle).isEmpty()) << "typed text must be on disk, inside a note";
}

TEST_F(OrphanNoteTest, QuickCaptureSurvivesARestart) {
  const QString needle = QStringLiteral("capture-restart-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
  app_->appendNoteEntry(needle);
  app_->flushSave();
  app_.reset();

  AppController reloaded;

  EXPECT_FALSE(noteHolding(&reloaded, needle).isEmpty()) << "a captured note must be on disk, inside a note";
}

TEST_F(OrphanNoteTest, BothNotesSurviveARestartAfterPlus) {
  const QString tag = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  const QString firstText = QStringLiteral("before-plus-%1").arg(tag);
  const QString secondText = QStringLiteral("after-plus-%1").arg(tag);
  app_->setNotesState(firstText);
  app_->newNote();
  app_->setNotesState(secondText);
  app_->flushSave();
  app_.reset();

  AppController reloaded;

  const QString first = noteHolding(&reloaded, firstText);
  const QString second = noteHolding(&reloaded, secondText);
  EXPECT_FALSE(first.isEmpty());
  EXPECT_FALSE(second.isEmpty());
  EXPECT_NE(first, second);
}

// ── Going to Notes opens a new note (dogfood 2026-10-20) ──
//
// An empty one, named by its first line; left empty, it goes again, so
// visiting Notes ten times does not leave ten "Untitled note"s.

TEST_F(OrphanNoteTest, GoingToNotesOpensAnEmptyNoteAndLeavingDropsIt) {
  app_->setCurrentView(QStringLiteral("board"));
  const QString old = app_->newNote(QStringLiteral("Old"));
  QCoreApplication::processEvents();
  const int before = app_->notes()->rowCount();

  app_->setCurrentView(QStringLiteral("notes"));
  const QString fresh = app_->activeNoteId();
  EXPECT_NE(fresh, old);
  EXPECT_TRUE(app_->isFreshNote(fresh));
  EXPECT_TRUE(app_->notesState().isEmpty());
  EXPECT_EQ(app_->notes()->rowCount(), before + 1);

  app_->setCurrentView(QStringLiteral("board"));
  EXPECT_EQ(app_->notes()->rowCount(), before);
  EXPECT_LT(app_->notes()->indexOfId(fresh), 0);
  EXPECT_EQ(app_->activeNoteId(), old) << "back on the note opened before";
}

TEST_F(OrphanNoteTest, GoingToNotesTwiceLeavesOneEmptyNote) {
  app_->setCurrentView(QStringLiteral("board"));
  const int before = app_->notes()->rowCount();
  app_->setCurrentView(QStringLiteral("notes"));
  app_->setCurrentView(QStringLiteral("notes"));
  app_->setCurrentView(QStringLiteral("today"));
  app_->setCurrentView(QStringLiteral("notes"));
  EXPECT_EQ(app_->notes()->rowCount(), before + 1);
  // Ctrl+N over it is the same note.
  const QString fresh = app_->activeNoteId();
  EXPECT_EQ(app_->newNote(), fresh);
  EXPECT_EQ(app_->notes()->rowCount(), before + 1);
}

TEST_F(OrphanNoteTest, TypedThenLeftAtOnceKeepsTheNote) {
  app_->setCurrentView(QStringLiteral("board"));
  app_->setCurrentView(QStringLiteral("notes"));
  const QString fresh = app_->activeNoteId();
  // The editor's debounce has not run: its text arrives on the flush.
  const auto c = QObject::connect(app_.get(), &AppController::aboutToChangeActiveNote, app_.get(), [this] {
    app_->setNotesState(QStringLiteral("Standup notes\nship it"));
  });
  app_->setCurrentView(QStringLiteral("board"));
  QObject::disconnect(c);

  ASSERT_GE(app_->notes()->indexOfId(fresh), 0) << "typed text was dropped";
  EXPECT_EQ(titleOf(fresh), QStringLiteral("Standup notes"));
  EXPECT_EQ(app_->noteBody(fresh), QStringLiteral("Standup notes\nship it"));
}

TEST_F(OrphanNoteTest, TypedAndWipedIsEmptyAgain) {
  app_->setCurrentView(QStringLiteral("board"));
  const int before = app_->notes()->rowCount();
  app_->setCurrentView(QStringLiteral("notes"));
  app_->setNotesState(QStringLiteral("Hel"));
  app_->setNotesState(QStringLiteral("\n"));
  app_->setCurrentView(QStringLiteral("board"));
  EXPECT_EQ(app_->notes()->rowCount(), before);
}

TEST_F(OrphanNoteTest, ANoteAskedForOnTheWayIsWhatNotesOpens) {
  app_->setCurrentView(QStringLiteral("board"));
  const QString a = app_->newNote(QStringLiteral("Linked"));
  app_->setActiveNoteId(QString());
  QCoreApplication::processEvents();
  const int before = app_->notes()->rowCount();
  // A [[link]] or a search hit: the note, then the view.
  app_->setActiveNoteId(a);
  app_->setCurrentView(QStringLiteral("notes"));
  EXPECT_EQ(app_->activeNoteId(), a);
  EXPECT_EQ(app_->notes()->rowCount(), before);
}

TEST_F(OrphanNoteTest, ANamedEmptyNoteStays) {
  app_->setCurrentView(QStringLiteral("board"));
  app_->setCurrentView(QStringLiteral("notes"));
  const QString fresh = app_->activeNoteId();
  ASSERT_TRUE(app_->renameNote(fresh, QStringLiteral("Ideas %1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(6))));
  app_->setCurrentView(QStringLiteral("board"));
  EXPECT_GE(app_->notes()->indexOfId(fresh), 0);
}

TEST_F(OrphanNoteTest, TheEmptyNoteIsNotWrittenToDisk) {
  const QString tag = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  app_->setCurrentView(QStringLiteral("board"));
  const QString kept = app_->newNote(QStringLiteral("Kept %1").arg(tag));
  QCoreApplication::processEvents();
  app_->setCurrentView(QStringLiteral("notes"));
  const QString fresh = app_->activeNoteId();
  ASSERT_TRUE(app_->isFreshNote(fresh));
  app_->flushSave();
  app_.reset();

  AppController reloaded;
  EXPECT_LT(reloaded.notes()->indexOfId(fresh), 0) << "quit with the empty note open left it in the list";
  EXPECT_GE(reloaded.notes()->indexOfId(kept), 0);
  EXPECT_EQ(reloaded.activeNoteId(), kept);
}

TEST_F(OrphanNoteTest, TypedThenQuitIsOnDisk) {
  const QString needle = QStringLiteral("typed-then-quit-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
  app_->setCurrentView(QStringLiteral("board"));
  app_->setCurrentView(QStringLiteral("notes"));
  app_->setNotesState(needle);
  app_->flushSave();
  app_.reset();

  AppController reloaded;
  EXPECT_FALSE(noteHolding(&reloaded, needle).isEmpty());
}

// Ctrl+Tab's list: the notes opened last, the open one first.
TEST_F(OrphanNoteTest, RecentNotesAreTheOpenedOnesLatestFirst) {
  const QString a = app_->newNote(QStringLiteral("A"));
  const QString b = app_->newNote(QStringLiteral("B"));
  const QString c = app_->newNote(QStringLiteral("C"));
  app_->setActiveNoteId(a);
  QStringList ids = app_->recentNoteIds();
  ASSERT_GE(ids.size(), 3);
  EXPECT_EQ(ids.mid(0, 3), (QStringList{a, c, b}));
  app_->setActiveNoteId(c);
  ids = app_->recentNoteIds();
  EXPECT_EQ(ids.mid(0, 2), (QStringList{c, a})) << "a tap of Ctrl+Tab goes back and forth between two";
  const QVariantList rows = app_->recentNotes(2);
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(rows.at(1).toMap().value("title").toString(), QStringLiteral("A"));
}

TEST_F(OrphanNoteTest, ADroppedEmptyNoteIsNotRecent) {
  app_->setCurrentView(QStringLiteral("board"));
  const QString a = app_->newNote(QStringLiteral("A"));
  QCoreApplication::processEvents();
  app_->setCurrentView(QStringLiteral("notes"));
  const QString fresh = app_->activeNoteId();
  app_->setActiveNoteId(a);
  EXPECT_FALSE(app_->recentNoteIds().contains(fresh));
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");

  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir scratch;
  scratch.setAutoRemove(true);
  qputenv("XDG_CONFIG_HOME", scratch.path().toUtf8());
  qputenv("XDG_DATA_HOME", scratch.path().toUtf8());

  QApplication qapp(argc, argv);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// The rule itself (APP-320), without a clock.
TEST(NoteChain, AnHourUnderOneLabel) {
  const QDateTime t0(QDate(2026, 10, 16), QTime(10, 0));
  EXPECT_TRUE(heap::notes::chainAppends(t0, QString(), QString(), t0.addSecs(59 * 60)));
  EXPECT_FALSE(heap::notes::chainAppends(t0, QString(), QString(), t0.addSecs(60 * 60)));
  EXPECT_FALSE(heap::notes::chainAppends(t0, QStringLiteral("hr"), QString(), t0.addSecs(60)));
  EXPECT_TRUE(heap::notes::chainAppends(t0, QStringLiteral("hr"), QStringLiteral("#HR"), t0.addSecs(60)));
  EXPECT_FALSE(heap::notes::chainAppends(QDateTime(), QString(), QString(), t0));
  EXPECT_FALSE(heap::notes::chainAppends(t0, QString(), QString(), t0.addSecs(-60))) << "a clock set back";
}

TEST(NoteChain, TagsAndTitles) {
  EXPECT_EQ(heap::notes::noteTags(QStringLiteral("#HR form, #личное and #HEAP-12 #hr")),
            (QStringList{QStringLiteral("hr"), QStringLiteral("личное")}));
  EXPECT_EQ(heap::notes::titleFromEntry(QStringLiteral("\n- [ ] buy milk #личное\nmore")), QStringLiteral("buy milk"));
  EXPECT_EQ(heap::notes::titleFromEntry(QStringLiteral("## Heading")), QStringLiteral("Heading"));
  const QString longLine = QStringLiteral("word ").repeated(30);
  EXPECT_LE(heap::notes::titleFromEntry(longLine).size(), 61);
  EXPECT_TRUE(heap::notes::titleFromEntry(longLine).endsWith(QStringLiteral("…")));
}
