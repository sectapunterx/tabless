// Corruption / crash recovery for AppController::loadStateOnStart() (HEAP-50).
//
// Guards the trust-critical rule: a state.json that exists but is unreadable or
// unparseable must NEVER be silently replaced by the demo seed. Instead the app
// recovers from the newest valid backup, or — failing that — quarantines the
// damaged file (state.corrupt-*.json) and boots a fresh profile.
//
// Runs headless via the same QApplication + offscreen QPA + QStandardPaths
// test-mode pattern as the selection / notes suites, so it never touches the
// user's real AppDataLocation.

#include "AppController.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantMap>

#include <gtest/gtest.h>

namespace {

QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool writeFile(const QString& path, const QByteArray& bytes) {
  QFile f(path);
  if(!f.open(QIODevice::WriteOnly)) {
    return false;
  }
  f.write(bytes);
  f.close();
  return true;
}

QByteArray readFile(const QString& path) {
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray b = f.readAll();
  f.close();
  return b;
}

QStringList corruptFiles(const QString& dir) {
  return QDir(dir).entryList({QStringLiteral("state.corrupt-*.json")}, QDir::Files);
}

QString firstProfileName(AppController& app) {
  const QVariantList ps = app.profiles();
  return ps.isEmpty() ? QString() : ps.first().toMap().value(QStringLiteral("name")).toString();
}

}  // namespace

class PersistenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Every test starts from an empty AppDataLocation — no state.json, no
    // backups, no leftover quarantine files from a previous test or run.
    const QString dir = appDataDir();
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
  }
};

// A valid state.json loads and its contents win over the demo seed; nothing is
// quarantined.
TEST_F(PersistenceTest, ValidStateLoadsWithoutQuarantine) {
  {
    AppController app;
    app.setCrumbUser(QStringLiteral("MARK_VALID"));
    app.flushSave();
  }  // destructor also flushes — state.json is valid and marked

  AppController app2;
  EXPECT_EQ(app2.crumbUser(), QStringLiteral("MARK_VALID"));
  EXPECT_TRUE(corruptFiles(appDataDir()).isEmpty());
}

// A corrupt state.json with a good backup available recovers the backup's data
// (proven by a marker) instead of seeding demo, and quarantines the bad file.
TEST_F(PersistenceTest, RecoversFromNewestBackupWhenStateCorrupt) {
  const QString dir = appDataDir();

  // 1) Produce a schema-valid state.json carrying a distinctive marker.
  {
    AppController app;
    app.setCrumbUser(QStringLiteral("RECOVERED_MARKER"));
    app.flushSave();
  }
  const QByteArray good = readFile(dir + QStringLiteral("/state.json"));
  ASSERT_FALSE(good.isEmpty());

  // 2) Stash it as a backup, then clobber the live state.json.
  ASSERT_TRUE(QDir().mkpath(dir + QStringLiteral("/backups")));
  ASSERT_TRUE(writeFile(dir + QStringLiteral("/backups/state-20240101-120000.json"), good));
  ASSERT_TRUE(writeFile(dir + QStringLiteral("/state.json"), QByteArray("{ this is not valid json")));

  // 3) Boot: recover the marker from the backup, quarantine the corrupt file.
  AppController app2;
  EXPECT_EQ(app2.crumbUser(), QStringLiteral("RECOVERED_MARKER"));
  EXPECT_FALSE(corruptFiles(dir).isEmpty());
}

// A corrupt state.json with NO backup preserves the damaged file and boots an
// empty workspace — never a silent wipe, and never the demo, which reads as a
// new install (PLAT-6).
TEST_F(PersistenceTest, QuarantinesCorruptStateWhenNoBackup) {
  const QString dir = appDataDir();
  ASSERT_TRUE(writeFile(dir + QStringLiteral("/state.json"), QByteArray("garbage{{{ not json")));

  AppController app;
  EXPECT_NE(app.crumbUser(), QStringLiteral("RECOVERED_MARKER"));
  EXPECT_NE(firstProfileName(app), QStringLiteral("Example"));
  EXPECT_EQ(app.tasks()->rowCount(), 0);
  EXPECT_FALSE(app.demoActive());
  EXPECT_TRUE(app.welcomeSeen());
  // The damaged original is preserved, not overwritten.
  EXPECT_FALSE(corruptFiles(dir).isEmpty());
}

// An absent state.json is a genuine first run: one empty profile of the
// person's own (APP-271, the example is a separate profile on request),
// quarantine nothing.
TEST_F(PersistenceTest, AbsentStateIsCleanFirstRun) {
  const QString dir = appDataDir();
  AppController app;
  EXPECT_TRUE(corruptFiles(dir).isEmpty());
  EXPECT_EQ(firstProfileName(app), QStringLiteral("Personal"));
  EXPECT_EQ(app.tasks()->rowCount(), 0);
}

// Tracked time must survive an app restart (HEAP-78 "across sessions").
TEST_F(PersistenceTest, TrackedSecondsSurvivesReload) {
  {
    AppController app;
    Task t;
    t.id = QStringLiteral("TIME-1");
    t.title = QStringLiteral("timed");
    t.status = QStringLiteral("todo");
    t.priority = QStringLiteral("P2");
    t.trackedSeconds = 4242;
    app.tasks()->reset({t});
    app.setCrumbUser(QStringLiteral("arm-save"));  // arm the save timer
    app.flushSave();
  }
  AppController app2;
  const int row = app2.tasks()->indexOfId(QStringLiteral("TIME-1"));
  ASSERT_GE(row, 0);
  EXPECT_EQ(app2.tasks()->items().at(row).trackedSeconds, 4242);
}

// APP-1: notes saved as "Untitled note" before their titles followed their
// text are named after it on the next load; a note somebody named is not.
TEST_F(PersistenceTest, UntitledNotesAreNamedAfterTheirTextOnLoad) {
  {
    AppController app;
    Note untitled;
    untitled.id = QStringLiteral("note-old");
    untitled.title = QStringLiteral("Untitled note");
    untitled.body = QStringLiteral("\n- Groceries for Friday\nmilk");
    Note named;
    named.id = QStringLiteral("note-named");
    named.title = QStringLiteral("Recipes");
    named.body = QStringLiteral("Pancakes\nflour");
    app.notes()->reset({untitled, named});
    app.setCrumbUser(QStringLiteral("arm-save"));
    app.flushSave();
  }
  AppController app2;
  const auto title = [&](const char* id) {
    const int row = app2.notes()->indexOfId(QString::fromLatin1(id));
    return row >= 0 ? app2.notes()->items().at(row).title : QString();
  };
  EXPECT_EQ(title("note-old"), QStringLiteral("Groceries for Friday"));
  EXPECT_EQ(title("note-named"), QStringLiteral("Recipes"));
}

// WEAK PECAP: the status log is the profile's, so it survives a restart.
TEST_F(PersistenceTest, StatusLogSurvivesReload) {
  {
    AppController app;
    Task t;
    t.id = QStringLiteral("LOG-1");
    t.title = QStringLiteral("moves");
    t.status = QStringLiteral("todo");
    t.priority = QStringLiteral("P2");
    app.tasks()->reset({t});
    app.moveTaskTo(QStringLiteral("LOG-1"), QStringLiteral("prog"), QString());
    app.flushSave();
  }
  AppController app2;
  const QVector<StatusChange> log = app2.statusLog();
  ASSERT_FALSE(log.isEmpty());
  EXPECT_EQ(log.constLast().taskId, QStringLiteral("LOG-1"));
  EXPECT_EQ(log.constLast().from, QStringLiteral("todo"));
  EXPECT_EQ(log.constLast().to, QStringLiteral("prog"));
  EXPECT_TRUE(log.constLast().at.isValid());
}

// Dogfood 2026-10-19: a task saved without a status time came back from every
// restart stamped with the moment of the load — a Done one then read as
// "closed today" in the week recap, and the first save made it stick.
TEST_F(PersistenceTest, ARestartDoesNotStampAMissingStatusTimeWithToday) {
  {
    AppController app;
    Task t;
    t.id = QStringLiteral("RST-1");
    t.title = QStringLiteral("closed long ago");
    t.status = QStringLiteral("done");
    t.priority = QStringLiteral("P2");
    app.tasks()->reset({t});
    app.setCrumbUser(QStringLiteral("arm-save"));
    app.flushSave();
  }
  qsizetype logged = -1;
  for(int restart = 0; restart < 2; ++restart) {
    AppController app;
    const int row = app.tasks()->indexOfId(QStringLiteral("RST-1"));
    ASSERT_GE(row, 0);
    EXPECT_FALSE(app.tasks()->items().at(row).statusChangedAt.isValid()) << "restart " << restart;
    for(const QVariant& v : app.weekFacts(QDate::currentDate()).value(QStringLiteral("closed")).toList()) {
      EXPECT_NE(v.toMap().value(QStringLiteral("id")).toString(), QStringLiteral("RST-1"));
    }
    // Nor is a restart a move.
    if(logged < 0) {
      logged = app.statusLog().size();
    }
    EXPECT_EQ(app.statusLog().size(), logged);
    app.setCrumbUser(QStringLiteral("arm-save"));
    app.flushSave();
  }
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
