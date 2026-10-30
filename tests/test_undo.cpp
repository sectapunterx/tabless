// Undo / redo.
//
// Undo was a single slot with a five-second timer: the next destructive
// operation overwrote the previous one, there was no redo, and bulk move and
// bulk archive recorded nothing at all. It is a stack of recorded diffs now
// (src/undo/UndoStack.h), so these cases pin both the behaviour that already
// existed and the three things that did not: depth, redo, and the operations
// that used to be unrecoverable.
//
// Headless via offscreen QPA + AppDataLocation test mode.

#include "AppController.h"
#include "Models.h"

#include "undo/UndoStack.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVector>

#include <gtest/gtest.h>

namespace {

constexpr int kStatusRole = Qt::UserRole + 5;
constexpr int kArchivedRole = Qt::UserRole + 9;

Task makeTask(const QString& id, const QString& status) {
  Task t;
  t.id = id;
  t.title = id;
  t.priority = QStringLiteral("P2");
  t.status = status;
  t.branch = QStringLiteral("feat/x");  // lets a move to "review" pass, too
  return t;
}

}  // namespace

class UndoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->tasks()->reset({makeTask(QStringLiteral("T-1"), QStringLiteral("todo"))});
  }

  void TearDown() override {
    app_.reset();
  }

  QString statusOf(const QString& id) const {
    const int row = app_->tasks()->indexOfId(id);
    return app_->tasks()->data(app_->tasks()->index(row, 0), kStatusRole).toString();
  }

  bool archivedOf(const QString& id) const {
    const int row = app_->tasks()->indexOfId(id);
    return app_->tasks()->data(app_->tasks()->index(row, 0), kArchivedRole).toBool();
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(UndoTest, MoveTaskThenUndoRestoresStatus) {
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("prog"));
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("prog"));
  EXPECT_TRUE(app_->hasPendingUndo());

  app_->undoLastDeletion();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("todo"));
  EXPECT_FALSE(app_->hasPendingUndo());
}

TEST_F(UndoTest, ArchiveThenUndoRestores) {
  app_->setArchived(QStringLiteral("T-1"), true);
  EXPECT_TRUE(archivedOf(QStringLiteral("T-1")));
  EXPECT_TRUE(app_->hasPendingUndo());

  app_->undoLastDeletion();
  EXPECT_FALSE(archivedOf(QStringLiteral("T-1")));
}

TEST_F(UndoTest, NoOpMoveDoesNotArmUndo) {
  app_->clearPendingUndo();
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("todo"));  // already todo
  EXPECT_FALSE(app_->hasPendingUndo());
}

// ─── depth: the thing a single slot could not do ──────────────────────

TEST_F(UndoTest, EachOperationIsItsOwnUndoStep) {
  app_->clearPendingUndo();
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("prog"));
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("review"));
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("done"));
  EXPECT_EQ(app_->undoDepth(), 3) << "a later operation must not overwrite an earlier one";

  app_->undo();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("review"));
  app_->undo();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("prog"));
  app_->undo();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("todo"));
  EXPECT_FALSE(app_->hasPendingUndo());
}

TEST_F(UndoTest, UndoPastTheBottomOfTheStackIsANoop) {
  app_->clearPendingUndo();
  app_->undo();
  app_->undo();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("todo"));
}

// ─── redo ─────────────────────────────────────────────────────────────

TEST_F(UndoTest, RedoReappliesWhatUndoReversed) {
  app_->clearPendingUndo();
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("prog"));

  app_->undo();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("todo"));
  EXPECT_TRUE(app_->canRedo());

  app_->redo();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("prog"));
  EXPECT_FALSE(app_->canRedo());
  EXPECT_TRUE(app_->hasPendingUndo()) << "a redone operation is undoable again";
}

TEST_F(UndoTest, ANewOperationDropsTheRedoBranch) {
  app_->clearPendingUndo();
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("prog"));
  app_->undo();
  ASSERT_TRUE(app_->canRedo());

  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("review"));
  EXPECT_FALSE(app_->canRedo()) << "editing after an undo abandons the redone future";
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("review"));
}

TEST_F(UndoTest, DeleteThenUndoThenRedo) {
  app_->clearPendingUndo();
  app_->deleteTask(QStringLiteral("T-1"));
  EXPECT_EQ(app_->tasks()->rowCount(), 0);

  app_->undo();
  ASSERT_EQ(app_->tasks()->rowCount(), 1);
  EXPECT_EQ(app_->tasks()->items().at(0).id, QStringLiteral("T-1"));

  app_->redo();
  EXPECT_EQ(app_->tasks()->rowCount(), 0);
}

// ─── the operations that recorded nothing before ──────────────────────

TEST_F(UndoTest, BulkMoveIsUndoable) {
  app_->tasks()->reset({makeTask(QStringLiteral("T-1"), QStringLiteral("todo")),
                        makeTask(QStringLiteral("T-2"), QStringLiteral("todo")),
                        makeTask(QStringLiteral("T-3"), QStringLiteral("backlog"))});
  app_->clearPendingUndo();
  app_->setSelectedTaskIds({QStringLiteral("T-1"), QStringLiteral("T-2"), QStringLiteral("T-3")});

  app_->moveSelectedTasksToStatus(QStringLiteral("prog"));
  ASSERT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("prog"));
  ASSERT_EQ(statusOf(QStringLiteral("T-3")), QStringLiteral("prog"));

  app_->undo();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("todo"));
  EXPECT_EQ(statusOf(QStringLiteral("T-2")), QStringLiteral("todo"));
  EXPECT_EQ(statusOf(QStringLiteral("T-3")), QStringLiteral("backlog")) << "each task goes back to its own column, not to a shared one";
}

TEST_F(UndoTest, BulkArchiveIsUndoable) {
  app_->tasks()->reset({makeTask(QStringLiteral("T-1"), QStringLiteral("todo")), makeTask(QStringLiteral("T-2"), QStringLiteral("todo"))});
  app_->clearPendingUndo();
  app_->setSelectedTaskIds({QStringLiteral("T-1"), QStringLiteral("T-2")});

  app_->setSelectedTasksArchived(true);
  ASSERT_TRUE(archivedOf(QStringLiteral("T-1")));

  app_->undo();
  EXPECT_FALSE(archivedOf(QStringLiteral("T-1")));
  EXPECT_FALSE(archivedOf(QStringLiteral("T-2")));
}

// A restored task keeps its row, so undoing a delete does not silently
// reorder the column it came from.
TEST_F(UndoTest, UndoRestoresATaskToItsOriginalRow) {
  app_->tasks()->reset({makeTask(QStringLiteral("T-1"), QStringLiteral("todo")),
                        makeTask(QStringLiteral("T-2"), QStringLiteral("todo")),
                        makeTask(QStringLiteral("T-3"), QStringLiteral("todo"))});
  app_->clearPendingUndo();

  app_->deleteTask(QStringLiteral("T-2"));
  app_->undo();

  ASSERT_EQ(app_->tasks()->rowCount(), 3);
  EXPECT_EQ(app_->tasks()->items().at(1).id, QStringLiteral("T-2"));
}

// The stack is bounded, and it is the oldest entry that falls off.
TEST_F(UndoTest, TheStackIsCappedAtItsMaximumDepth) {
  app_->clearPendingUndo();
  const int cap = heap::undo::UndoStack::kMaxDepth;
  for(int i = 0; i < cap + 20; ++i) {
    app_->moveTask(QStringLiteral("T-1"), (i % 2 == 0) ? QStringLiteral("prog") : QStringLiteral("todo"));
  }
  EXPECT_EQ(app_->undoDepth(), cap);
}

// Dogfood 2026-10-30: undo has no time limit, and the palette lists what it
// would take back, newest first, each with a label and when it was done.
TEST_F(UndoTest, TheHistoryListsUndoableStepsNewestFirst) {
  app_->clearPendingUndo();
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("prog"));
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("review"));
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("done"));
  const QVariantList all = app_->undoHistory(10);
  ASSERT_EQ(all.size(), 3);
  const QVariantList two = app_->undoHistory(2);
  ASSERT_EQ(two.size(), 2);
  EXPECT_EQ(two.at(0).toMap().value(QStringLiteral("serial")), app_->undoSerialForToast()) << "the top first";
  for(const QVariant& v : all) {
    EXPECT_FALSE(v.toMap().value(QStringLiteral("label")).toString().isEmpty());
    EXPECT_TRUE(v.toMap().value(QStringLiteral("at")).toDateTime().isValid());
  }
  // A step taken back is not offered; picking one takes back that step.
  app_->undo();
  EXPECT_EQ(app_->undoHistory(10).size(), 2);
  EXPECT_TRUE(app_->undoEntry(app_->undoHistory(10).at(0).toMap().value(QStringLiteral("serial")).toDouble()));
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("prog"));
  EXPECT_EQ(app_->undoHistory(0).size(), 0);
}

// Notes' Ctrl+Z past the note's own history takes back the app's step
// quietly and hands its label on, for the toast with Redo.
TEST_F(UndoTest, UndoPastADocumentSaysWhatItTookBack) {
  app_->clearPendingUndo();
  app_->moveTask(QStringLiteral("T-1"), QStringLiteral("prog"));
  const QString label = app_->undoHistory(1).at(0).toMap().value(QStringLiteral("label")).toString();
  QStringList toasts;
  QStringList past;
  QObject::connect(app_.get(), &AppController::toast, [&](const QString& m, const QString&) {
    toasts << m;
  });
  QObject::connect(app_.get(), &AppController::undonePastDocument, [&](const QString& l) {
    past << l;
  });
  app_->undoPastDocument();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("todo"));
  EXPECT_EQ(past, QStringList{label});
  EXPECT_FALSE(toasts.contains(label)) << "the plain toast is not said twice";
  app_->undoPastDocument();
  EXPECT_EQ(past.size(), 1) << "nothing left to take back: nothing said";
  app_->redo();
  EXPECT_EQ(statusOf(QStringLiteral("T-1")), QStringLiteral("prog"));
}

TEST(UndoStackHistory, UndoableIsNewestFirstAndSkipsTheRedoTail) {
  heap::undo::UndoStack s;
  for(int i = 1; i <= 4; ++i) {
    heap::undo::Entry e;
    e.serial = i;
    e.label = QString::number(i);
    e.statusesTouched = true;
    s.push(e);
  }
  s.takeUndo();
  const QVector<const heap::undo::Entry*> u = s.undoable(2);
  ASSERT_EQ(u.size(), 2);
  EXPECT_EQ(u.at(0)->serial, 3u);
  EXPECT_EQ(u.at(1)->serial, 2u);
  EXPECT_EQ(s.undoable(10).size(), 3);
}

// Replaying a recorded text edit on a later version of the text (KNOW-1): the
// stretch that changed is found again, and anything typed elsewhere stays.
TEST(UndoRebase, ATextEditIsReplayedOnTheTextAsItIsNow) {
  bool ok = false;
  // A rename's heading, with a paragraph typed under it since.
  EXPECT_EQ(
      heap::undo::rebaseTextEdit(QStringLiteral("# Final\n\nimportant"), QStringLiteral("# Final\n\n"), QStringLiteral("# Draft\n\n"), &ok),
      QStringLiteral("# Draft\n\nimportant"));
  EXPECT_TRUE(ok);
  // A link retargeted mid-text, with typing on both sides of it.
  EXPECT_EQ(heap::undo::rebaseTextEdit(QStringLiteral("new top\nsee [[New]] here\nnew end"),
                                       QStringLiteral("see [[New]] here"),
                                       QStringLiteral("see [[Old]] here"),
                                       &ok),
            QStringLiteral("new top\nsee [[Old]] here\nnew end"));
  EXPECT_TRUE(ok);
  // The stretch itself was retyped: the text is left alone and says so.
  EXPECT_EQ(heap::undo::rebaseTextEdit(
                QStringLiteral("# Something else\n\nx"), QStringLiteral("# Final\n\n"), QStringLiteral("# Draft\n\n"), &ok),
            QStringLiteral("# Something else\n\nx"));
  EXPECT_FALSE(ok);
}

// Only the fields the entry changed move; the rest of the note is as it is now.
TEST(UndoRebase, ANoteEditMovesOnlyTheFieldsItChanged) {
  Note before;
  before.id = QStringLiteral("n");
  before.title = QStringLiteral("N");
  before.body = QStringLiteral("# N\n\n");
  Note after = before;
  after.pinned = true;
  Note now = after;
  now.body = QStringLiteral("# N\n\ntyped");
  now.folder = QStringLiteral("moved by hand");
  bool ok = false;
  const Note undone = heap::undo::mergeNoteEdit(now, after, before, &ok);
  EXPECT_TRUE(ok);
  EXPECT_FALSE(undone.pinned);
  EXPECT_EQ(undone.body, now.body);
  EXPECT_EQ(undone.folder, now.folder);
}

// The Docs blob: entries go back in by id, next to the neighbour they had, and
// edits to other entries stay (KNOW-2).
TEST(UndoRebase, ADocsDeletionIsUndoneById) {
  const QString before = QStringLiteral(R"({"sections":[{"id":"s","items":[{"id":"a"},{"id":"b"},{"id":"c"}]}],"snippets":[]})");
  const QString after = QStringLiteral(R"({"sections":[{"id":"s","items":[{"id":"a"},{"id":"c"}]}],"snippets":[]})");
  const QString now = QStringLiteral(R"({"sections":[{"id":"s","items":[{"id":"a","t":"edited"},{"id":"c"},{"id":"d"}]}],"snippets":[]})");
  bool ok = false;
  const QString undone = heap::undo::rebaseDocsState(now, after, before, &ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(QJsonDocument::fromJson(undone.toUtf8()),
            QJsonDocument::fromJson(
                R"({"sections":[{"id":"s","items":[{"id":"a","t":"edited"},{"id":"b"},{"id":"c"},{"id":"d"}]}],"snippets":[]})"));
  const QString redone = heap::undo::rebaseDocsState(undone, before, after, &ok);
  EXPECT_EQ(QJsonDocument::fromJson(redone.toUtf8()), QJsonDocument::fromJson(now.toUtf8()));
}

// A contact has no id: linking it to a Person inside an undo step and a
// Mattermost sync changing it again afterwards still leave one contact, and
// the undo takes back only the link.
TEST(UndoRebase, AnEditedDocsContactIsUndoneInPlace) {
  const QString before = QStringLiteral(R"({"contacts":[{"name":"Ann","mattermost":"@ann","role":"dev"}]})");
  const QString after = QStringLiteral(R"({"contacts":[{"name":"Ann","mattermost":"@ann","role":"dev","personId":"ann"}]})");
  const QString now =
      QStringLiteral(R"({"contacts":[{"name":"Ann","mattermost":"@ann","role":"lead","personId":"ann","mm":{"role":"lead"}}]})");
  bool ok = false;
  const QString undone = heap::undo::rebaseDocsState(now, after, before, &ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(QJsonDocument::fromJson(undone.toUtf8()),
            QJsonDocument::fromJson(R"({"contacts":[{"name":"Ann","mattermost":"@ann","role":"lead","mm":{"role":"lead"}}]})"));
  const QString redone = heap::undo::rebaseDocsState(undone, before, after, &ok);
  EXPECT_EQ(QJsonDocument::fromJson(redone.toUtf8()), QJsonDocument::fromJson(now.toUtf8()));
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
