// View navigation + status-column focus (HEAP-69).
//
// The sidebar Blocked / Code Review buttons call focusStatusColumn(id): it must
// switch the active view to "board" and record the focused status so the board
// can scroll to + highlight that column. Also covers countByStatus, which feeds
// the sidebar count badges.
//
// Headless via offscreen QPA + AppDataLocation test mode.

#include "AppController.h"
#include "Models.h"
#include "StateSerializer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

Task mk(const QString& id, const QString& status) {
  Task t;
  t.id = id;
  t.title = id;
  t.priority = QStringLiteral("P2");
  t.status = status;
  return t;
}

}  // namespace

class ViewFocusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->tasks()->reset({
        mk(QStringLiteral("A"), QStringLiteral("todo")),
        mk(QStringLiteral("B"), QStringLiteral("blocked")),
        mk(QStringLiteral("C"), QStringLiteral("blocked")),
        mk(QStringLiteral("D"), QStringLiteral("review")),
    });
  }

  void TearDown() override {
    app_.reset();
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(ViewFocusTest, FocusStatusColumnSwitchesToBoardAndRecordsStatus) {
  app_->setCurrentView(QStringLiteral("notes"));
  ASSERT_EQ(app_->currentView(), QStringLiteral("notes"));

  QSignalSpy viewSpy(app_.get(), &AppController::currentViewChanged);
  QSignalSpy focusSpy(app_.get(), &AppController::focusedStatusChanged);

  app_->focusStatusColumn(QStringLiteral("blocked"));

  EXPECT_EQ(app_->currentView(), QStringLiteral("board"));
  EXPECT_EQ(app_->focusedStatus(), QStringLiteral("blocked"));
  EXPECT_EQ(viewSpy.count(), 1);  // notes -> board
  EXPECT_GE(focusSpy.count(), 1);
}

TEST_F(ViewFocusTest, RepeatFocusOnSameStatusReEmits) {
  app_->focusStatusColumn(QStringLiteral("review"));  // from default "board"
  EXPECT_EQ(app_->currentView(), QStringLiteral("board"));
  EXPECT_EQ(app_->focusedStatus(), QStringLiteral("review"));

  // A repeat click on the already-focused status must re-emit so the board
  // re-runs its scroll/pulse.
  QSignalSpy focusSpy(app_.get(), &AppController::focusedStatusChanged);
  app_->focusStatusColumn(QStringLiteral("review"));
  EXPECT_EQ(focusSpy.count(), 1);
  EXPECT_EQ(app_->focusedStatus(), QStringLiteral("review"));
}

TEST_F(ViewFocusTest, CountByStatusFeedsBadges) {
  EXPECT_EQ(app_->countByStatus(QStringLiteral("blocked")), 2);
  EXPECT_EQ(app_->countByStatus(QStringLiteral("review")), 1);
  EXPECT_EQ(app_->countByStatus(QStringLiteral("todo")), 1);
  EXPECT_EQ(app_->countByStatus(QStringLiteral("nonexistent")), 0);
}

// ─── view shortcuts ───────────────────────────────────────────────────
// Ctrl+1..8 are counted off the side rail, so they have to be in rail order.
// They used to skip Month (which had no shortcut at all) and then run
// docs/notes/settings out of order, so the number a user counted off the rail
// opened a different view.

namespace {

QString sequenceOf(AppController* app, const QString& id) {
  for(const QVariant& v : app->shortcuts()) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() == id) {
      return m.value(QStringLiteral("sequence")).toString();
    }
  }
  return {};
}

}  // namespace

// heap 2 (APP-258): Ctrl+1..3 are the sidebar's sections, Ctrl+, Settings;
// the lenses have the keymap's g b / g l, the calendar zoom z w / z m
// (APP-272); Docs is g d like its sidebar neighbours (A7-10); the other
// views have no key of their own (still bindable).
TEST_F(ViewFocusTest, SectionShortcutsFollowTheSidebar) {
  app_->setKeymapScheme(QStringLiteral("vim"));  // the Vim keymap's keys
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("section.today")), QStringLiteral("Ctrl+1"));
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("section.tasks")), QStringLiteral("Ctrl+2"));
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("section.knowledge")), QStringLiteral("Ctrl+3"));
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("view.settings")), QStringLiteral("Ctrl+,"));
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("view.board")), QStringLiteral("G, B"));
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("view.timeline")), QStringLiteral("G, L"));
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("view.week")), QStringLiteral("Z, W"));
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("view.month")), QStringLiteral("Z, M"));
  EXPECT_EQ(sequenceOf(app_.get(), QStringLiteral("view.docs")), QStringLiteral("G, D"));
  for(const char* v : {"view.archive", "view.notes"}) {
    EXPECT_TRUE(sequenceOf(app_.get(), QString::fromLatin1(v)).isEmpty()) << v;
  }
  EXPECT_TRUE(sequenceOf(app_.get(), QStringLiteral("tweaks.open")).isEmpty()) << "Ctrl+, is Settings now";
}

// Each section opens on the view it was left on; the first time, on its
// default. A view outside the section is never its "last view".
TEST_F(ViewFocusTest, SectionsRememberTheirLastView) {
  app_->setCurrentView(QStringLiteral("today"));
  EXPECT_EQ(app_->currentSection(), QStringLiteral("today"));
  app_->openSection(QStringLiteral("tasks"));
  EXPECT_EQ(app_->currentView(), QStringLiteral("board"));
  app_->setCurrentView(QStringLiteral("month"));
  EXPECT_EQ(app_->currentSection(), QStringLiteral("tasks"));
  app_->openSection(QStringLiteral("knowledge"));
  EXPECT_EQ(app_->currentView(), QStringLiteral("notes"));
  app_->setCurrentView(QStringLiteral("docs"));
  app_->openSection(QStringLiteral("tasks"));
  EXPECT_EQ(app_->currentView(), QStringLiteral("month"));
  app_->openSection(QStringLiteral("knowledge"));
  // Docs is its own screen of Knowledge again (DG-070, revised).
  EXPECT_EQ(app_->currentView(), QStringLiteral("docs"));
  app_->openSection(QStringLiteral("settings"));
  EXPECT_EQ(app_->currentView(), QStringLiteral("settings"));
  app_->openSection(QStringLiteral("nonsense"));
  EXPECT_EQ(app_->currentView(), QStringLiteral("today"));
}

// Month is reachable from the rail, so it needs a binding like its neighbours.
TEST_F(ViewFocusTest, MonthHasAShortcutWithALabelAndDescription) {
  bool found = false;
  for(const QVariant& v : app_->shortcuts()) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() != QStringLiteral("view.month")) {
      continue;
    }
    found = true;
    EXPECT_FALSE(m.value(QStringLiteral("label")).toString().isEmpty());
    EXPECT_FALSE(m.value(QStringLiteral("description")).toString().isEmpty());
  }
  EXPECT_TRUE(found);
}

// Every catalog entry must be unique — two views on one chord means one of
// them is unreachable, which is the shape of the bug this renumbering fixes.
TEST_F(ViewFocusTest, NoTwoShortcutsShareASequence) {
  QHash<QString, QString> seen;
  for(const QVariant& v : app_->shortcuts()) {
    const QVariantMap m = v.toMap();
    const QString seq = m.value(QStringLiteral("sequence")).toString();
    const QString id = m.value(QStringLiteral("id")).toString();
    if(seq.isEmpty()) {
      continue;
    }
    // A column-header key shares its chord with an app key on purpose: it is
    // live only on a header (DG-133), and findShortcutConflict says so.
    if(seen.contains(seq) && app_->findShortcutConflict(id, seq).isEmpty() && app_->findShortcutConflict(seen.value(seq), seq).isEmpty()) {
      continue;
    }
    EXPECT_FALSE(seen.contains(seq)) << "both " << seen.value(seq).toStdString() << " and " << id.toStdString() << " are bound to "
                                     << seq.toStdString();
    seen.insert(seq, id);
  }
}

// ─── shortcut override migration ──────────────────────────────────────
// Settings written before kShortcutsSchema 2 stored *every* binding, not only
// the rebound ones. Restoring all of them would pin each shortcut to whatever
// the default was on the day the file was written — the renumbering above
// would reach nobody, and Ctrl+4 would end up bound to both Month and Docs.

namespace {

QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// A state file whose settings.shortcuts carry `entries`, with no schema marker
// — i.e. what an older build wrote.
void writeLegacyStateWithShortcuts(const QJsonObject& entries) {
  QDir(appDataDir()).removeRecursively();
  QDir().mkpath(appDataDir());

  QJsonObject settings;
  settings["shortcuts"] = entries;  // deliberately no "shortcutsSchema"

  QJsonObject root;
  root["schemaVersion"] = heap::state::kSchemaVersion;
  root["settings"] = settings;
  // A file with no profile is not a state file (PLAT-7): it would be set
  // aside as damaged, shortcuts and all.
  root["profiles"] = QJsonArray{QJsonObject{{"id", "default"}, {"name", "Example"}}};

  QFile f(appDataDir() + "/state.json");
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument(root).toJson());
}

QString sequenceIn(AppController& app, const QString& id) {
  for(const QVariant& v : app.shortcuts()) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() == id) {
      return m.value(QStringLiteral("sequence")).toString();
    }
  }
  return {};
}

}  // namespace

TEST(ShortcutMigration, ALegacyFilesStoredDefaultsDoNotPinTheOldLayout) {
  writeLegacyStateWithShortcuts({
      {QStringLiteral("view.docs"), QStringLiteral("Ctrl+4")},   // was the default
      {QStringLiteral("view.notes"), QStringLiteral("Ctrl+5")},  // was the default
      {QStringLiteral("view.archive"), QStringLiteral("Ctrl+7")},
  });

  AppController app;
  EXPECT_EQ(sequenceIn(app, QStringLiteral("view.docs")), QStringLiteral("G, D")) << "today's default, not the old Ctrl+4";
  EXPECT_TRUE(sequenceIn(app, QStringLiteral("view.notes")).isEmpty());
  EXPECT_TRUE(sequenceIn(app, QStringLiteral("view.archive")).isEmpty()) << "Ctrl+7 was its old default, not a choice";
}

TEST(ShortcutMigration, ALegacyFilesRealRebindSurvives) {
  writeLegacyStateWithShortcuts({
      {QStringLiteral("view.docs"), QStringLiteral("Ctrl+4")},       // default of the day → dropped
      {QStringLiteral("task.new"), QStringLiteral("Ctrl+Shift+J")},  // never a default → kept
  });

  AppController app;
  EXPECT_EQ(sequenceIn(app, QStringLiteral("task.new")), QStringLiteral("Ctrl+Shift+J"));
  EXPECT_EQ(sequenceIn(app, QStringLiteral("view.docs")), QStringLiteral("G, D"));
}

// A cleared binding is a choice too, and "" never matched a default.
TEST(ShortcutMigration, ALegacyFilesClearedBindingSurvives) {
  writeLegacyStateWithShortcuts({{QStringLiteral("task.openExternal"), QString()}});

  AppController app;
  EXPECT_TRUE(sequenceIn(app, QStringLiteral("task.openExternal")).isEmpty());
}

// ─── heap 2 shell (APP-258) ───────────────────────────────────────────
// A 0.7 settings file (shortcutsSchema 2): the user's own bindings stay,
// a new default that collides with one steps aside, and the change is said
// once.

namespace {

void writeV2StateWithShortcuts(const QJsonObject& entries) {
  QDir(appDataDir()).removeRecursively();
  QDir().mkpath(appDataDir());
  QJsonObject settings;
  settings["shortcuts"] = entries;
  settings["shortcutsSchema"] = 2;
  settings["currentView"] = QStringLiteral("week");
  QJsonObject root;
  root["schemaVersion"] = heap::state::kSchemaVersion;
  root["settings"] = settings;
  root["profiles"] = QJsonArray{QJsonObject{{"id", "default"}, {"name", "Example"}}};
  QFile f(appDataDir() + "/state.json");
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument(root).toJson());
}

}  // namespace

// APP-281 A4: an old key says where its action went the first time it is
// pressed after the update — once, and only the keys actually pressed.
TEST(ShellMigration, AReturningUserIsToldOnceWhatMoved) {
  writeV2StateWithShortcuts({});
  {
    AppController app;
    EXPECT_TRUE(app.shellNotice().isEmpty()) << "nothing said up front";
    EXPECT_TRUE(app.hasKeymapNotice());
    EXPECT_EQ(app.currentView(), QStringLiteral("week")) << "the last screen stays the start screen";
    EXPECT_EQ(app.currentSection(), QStringLiteral("tasks"));
    EXPECT_EQ(sequenceIn(app, QStringLiteral("section.today")), QStringLiteral("Ctrl+1"));
    EXPECT_EQ(sequenceIn(app, QStringLiteral("task.openExternal")), QStringLiteral("G, X"));
    QSignalSpy toasts(&app, &AppController::toast);
    app.noteKeyPressed(QStringLiteral("Ctrl+2"));
    ASSERT_EQ(toasts.count(), 1);
    EXPECT_TRUE(toasts.at(0).at(0).toString().contains(app.keyText(QStringLiteral("Ctrl+2"))))
        << toasts.at(0).at(0).toString().toStdString();
    app.noteKeyPressed(QStringLiteral("Ctrl+2"));
    app.noteKeyPressed(QStringLiteral("J"));
    EXPECT_EQ(toasts.count(), 1) << "said once; a key that did not move says nothing";
    app.flushSave();
  }
  AppController again;
  QSignalSpy toasts(&again, &AppController::toast);
  again.noteKeyPressed(QStringLiteral("Ctrl+2"));
  EXPECT_EQ(toasts.count(), 0) << "said once, across a restart";
  again.noteKeyPressed(QStringLiteral("O"));
  EXPECT_EQ(toasts.count(), 1) << "O was not pressed yet";
}

TEST(ShellMigration, TheUsersOwnBindingKeepsItsKey) {
  writeV2StateWithShortcuts({{QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+2")}});
  AppController app;
  EXPECT_EQ(sequenceIn(app, QStringLiteral("theme.toggle")), QStringLiteral("Ctrl+2"));
  EXPECT_TRUE(sequenceIn(app, QStringLiteral("section.tasks")).isEmpty()) << "the new default steps aside";
  EXPECT_EQ(sequenceIn(app, QStringLiteral("section.today")), QStringLiteral("Ctrl+1"));
  EXPECT_TRUE(app.shellNotice().contains(app.keyText(QStringLiteral("Ctrl+2")))) << app.shellNotice().toStdString();
  // Their own key is theirs: no "Ctrl 2 moved" when they press it.
  QSignalSpy toasts(&app, &AppController::toast);
  app.noteKeyPressed(QStringLiteral("Ctrl+2"));
  EXPECT_EQ(toasts.count(), 0);
}

TEST(ShellMigration, ANewInstallHearsNothing) {
  QDir(appDataDir()).removeRecursively();
  AppController app;
  EXPECT_TRUE(app.shellNotice().isEmpty());
  EXPECT_FALSE(app.hasKeymapNotice());
}

// A 0.7 user who bound T to an action of their own keeps it: the timer's new
// default steps aside (APP-272), and T is not announced as moved.
TEST(ShellMigration, ANewSingleLetterDefaultStepsAsideForTheUsersOwn) {
  writeV2StateWithShortcuts({{QStringLiteral("theme.toggle"), QStringLiteral("T")}});
  AppController app;
  EXPECT_EQ(sequenceIn(app, QStringLiteral("theme.toggle")), QStringLiteral("T"));
  EXPECT_TRUE(sequenceIn(app, QStringLiteral("task.timer")).isEmpty());
  QSignalSpy toasts(&app, &AppController::toast);
  app.noteKeyPressed(QStringLiteral("T"));
  EXPECT_EQ(toasts.count(), 0);
}

// ─── keymap schemes (dogfood 2026-10-19) ──────────────────────────────
// A new install starts on Classic; a settings file from before the choice
// was on the Vim-based keymap of 0.8 and stays on it; the choice is saved.

namespace {

void writeV4State(const QJsonObject& entries) {
  QDir(appDataDir()).removeRecursively();
  QDir().mkpath(appDataDir());
  QJsonObject settings;
  settings["shortcuts"] = entries;
  settings["shortcutsSchema"] = 4;
  QJsonObject root;
  root["schemaVersion"] = heap::state::kSchemaVersion;
  root["settings"] = settings;
  root["profiles"] = QJsonArray{QJsonObject{{"id", "default"}, {"name", "Example"}}};
  QFile f(appDataDir() + "/state.json");
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument(root).toJson());
}

}  // namespace

TEST(KeymapScheme, ANewInstallIsClassic) {
  QDir(appDataDir()).removeRecursively();
  AppController app;
  EXPECT_EQ(app.keymapScheme(), QStringLiteral("classic"));
  EXPECT_EQ(sequenceIn(app, QStringLiteral("board.cursorDown")), QStringLiteral("Down"));
  EXPECT_EQ(sequenceIn(app, QStringLiteral("task.done")), QStringLiteral("Ctrl+D"));
}

TEST(KeymapScheme, AnUpgradeFromTheVimKeymapStaysOnVim) {
  writeV4State({{QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+Alt+T")}});
  AppController app;
  EXPECT_EQ(app.keymapScheme(), QStringLiteral("vim"));
  EXPECT_EQ(sequenceIn(app, QStringLiteral("board.cursorDown")), QStringLiteral("J"));
  EXPECT_EQ(sequenceIn(app, QStringLiteral("view.board")), QStringLiteral("G, B"));
  EXPECT_EQ(sequenceIn(app, QStringLiteral("theme.toggle")), QStringLiteral("Ctrl+Alt+T")) << "the user's own key";
  EXPECT_TRUE(app.shellNotice().isEmpty()) << "nothing moved, nothing to say";
}

TEST(KeymapScheme, TheChoiceAndTheRebindingsSurviveARestart) {
  writeV4State({});
  {
    AppController app;
    ASSERT_EQ(app.keymapScheme(), QStringLiteral("vim"));
    app.setKeymapScheme(QStringLiteral("classic"));
    EXPECT_TRUE(app.setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+Alt+T")));
    app.flushSave();
  }
  AppController again;
  EXPECT_EQ(again.keymapScheme(), QStringLiteral("classic"));
  EXPECT_EQ(sequenceIn(again, QStringLiteral("task.rename")), QStringLiteral("F2"));
  EXPECT_EQ(sequenceIn(again, QStringLiteral("theme.toggle")), QStringLiteral("Ctrl+Alt+T"));
  EXPECT_EQ(sequenceIn(again, QStringLiteral("task.timer")), QStringLiteral("Ctrl+T")) << "a Classic default, not stored";
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
