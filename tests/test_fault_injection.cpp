// Durability under injected write faults (HEAP-156).
//
// Killing the process at a byte offset proves nothing here: saveStateNow() goes
// through QSaveFile, which writes a temp file and atomically renames, so an
// interrupted write leaves state.json untouched. The interesting faults live at
// the serializer/writer seam, so that is where they are injected — via
// AppController::setStateWriterForTesting:
//
//   truncate      the first N bytes reach disk, the rest do not
//   garbage       the bytes reach disk with trailing junk appended
//   dropped rename the bytes never reach state.json at all
//
// For each of 100 deterministic cases the app must reopen to a valid state,
// quarantine anything damaged, and leave a recovery-log record. It must never
// reopen to a fresh demo seed on top of the user's data.

#include "AppController.h"
#include "RecoveryLog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

QString appDataDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString statePath() {
  return appDataDir() + "/state.json";
}

QString backupDir() {
  return appDataDir() + "/backups";
}

void writeRaw(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

QByteArray readRaw(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool isValidJsonObject(const QByteArray& bytes) {
  const QJsonDocument d = QJsonDocument::fromJson(bytes);
  return !d.isNull() && d.isObject();
}

void clearAppData() {
  QDir(appDataDir()).removeRecursively();
  QDir().mkpath(appDataDir());
}

enum class Fault { Truncate, Garbage, DroppedRename };

// The three injected writers. Each is a pure function of (bytes, seed), so a
// failing case number reproduces exactly.
AppController::StateWriter faultyWriter(Fault fault, int seed) {
  return [fault, seed](const QString& path, const QByteArray& bytes) -> bool {
    switch(fault) {
      case Fault::Truncate: {
        const int cut = bytes.isEmpty() ? 0 : (seed * 7919) % bytes.size();
        QFile f(path);
        if(f.open(QIODevice::WriteOnly)) {
          f.write(bytes.left(cut));
        }
        return false;
      }
      case Fault::Garbage: {
        QFile f(path);
        if(f.open(QIODevice::WriteOnly)) {
          f.write(bytes);
          f.write(QByteArray(" <<<garbage\xff>>> ").repeated(1 + (seed % 3)));
        }
        return false;
      }
      case Fault::DroppedRename:
        // QSaveFile wrote its temp file and then never committed: state.json is
        // whatever it was before. Nothing to do.
        return false;
    }
    return false;
  };
}

// One good save, so a valid backup exists to recover from.
void seedGoodProfile() {
  AppController app;
  QVariantMap draft = app.newTaskDraft(QStringLiteral("todo"));
  draft["_isNew"] = true;
  draft["id"] = QStringLiteral("SEED-1");
  draft["title"] = QStringLiteral("survivor");
  app.saveTask(draft);
  app.flushSave();
  // Promote the good file into the backup dir: rotateBackupIfDue() only fires
  // once per interval, and a fresh AppController has no backup history.
  QDir().mkpath(backupDir());
  QFile::copy(statePath(), backupDir() + "/state-20260101-000000.json");
}

class FaultInjectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    clearAppData();
    seedGoodProfile();
    good_ = readRaw(statePath());
    ASSERT_TRUE(isValidJsonObject(good_));
  }

  void TearDown() override {
    AppController::setStateWriterForTesting({});
  }

  QByteArray good_;
};

}  // namespace

// 100 deterministic cases: every one must reopen to a valid state with the
// seeded task intact, and never to a demo seed.
TEST_F(FaultInjectionTest, HundredDeterministicFaultsAllRecover) {
  const Fault faults[] = {Fault::Truncate, Fault::Garbage, Fault::DroppedRename};

  for(int i = 0; i < 100; ++i) {
    const Fault fault = faults[i % 3];
    SCOPED_TRACE(::testing::Message() << "case " << i << " fault " << static_cast<int>(fault));

    clearAppData();
    writeRaw(statePath(), good_);
    QDir().mkpath(backupDir());
    writeRaw(backupDir() + "/state-20260101-000000.json", good_);

    {
      AppController::setStateWriterForTesting(faultyWriter(fault, i));
      AppController app;
      QVariantMap draft = app.newTaskDraft(QStringLiteral("todo"));
      draft["_isNew"] = true;
      draft["id"] = QStringLiteral("DOOMED-1");
      draft["title"] = QStringLiteral("written during the fault");
      app.saveTask(draft);
      app.flushSave();
      AppController::setStateWriterForTesting({});
    }

    // Reopen. Whatever the writer left behind, the app must come up on data.
    AppController reopened;
    EXPECT_GE(reopened.tasks()->rowCount(), 1) << "reopened with no tasks — the seed was lost";
    bool foundSeed = false;
    for(const Task& t : reopened.tasks()->items()) {
      foundSeed = foundSeed || t.id == QStringLiteral("SEED-1");
    }
    EXPECT_TRUE(foundSeed) << "the pre-fault task did not survive";

    // The live file must parse after recovery.
    EXPECT_TRUE(isValidJsonObject(readRaw(statePath()))) << "state.json left unparseable";
  }
}

TEST_F(FaultInjectionTest, TruncatedStateIsQuarantinedAndTheNewestBackupIsLoaded) {
  writeRaw(backupDir() + "/state-20260101-000000.json", good_);
  writeRaw(statePath(), good_.left(good_.size() / 2));  // truncated: unparseable

  AppController app;

  EXPECT_FALSE(QDir(appDataDir()).entryList({"state.corrupt-*.json"}, QDir::Files).isEmpty()) << "damaged file was not quarantined";
  bool foundSeed = false;
  for(const Task& t : app.tasks()->items()) {
    foundSeed = foundSeed || t.id == QStringLiteral("SEED-1");
  }
  EXPECT_TRUE(foundSeed) << "the newest valid backup was not loaded";
}

TEST_F(FaultInjectionTest, GarbageStateIsQuarantinedAndRecorded) {
  writeRaw(backupDir() + "/state-20260101-000000.json", good_);
  writeRaw(statePath(), good_ + "\n<<<not json>>>");

  AppController app;

  EXPECT_FALSE(QDir(appDataDir()).entryList({"state.corrupt-*.json"}, QDir::Files).isEmpty());

  bool sawQuarantine = false;
  bool sawRecovery = false;
  for(const QVariant& v : app.recoveryLog()) {
    const QString kind = v.toMap().value(QStringLiteral("kind")).toString();
    sawQuarantine = sawQuarantine || kind == QLatin1String(heap::recovery::kQuarantined);
    sawRecovery = sawRecovery || kind == QLatin1String(heap::recovery::kRecovered);
  }
  EXPECT_TRUE(sawQuarantine) << "no quarantine record in the recovery log";
  EXPECT_TRUE(sawRecovery) << "no recovery record in the recovery log";
}

TEST_F(FaultInjectionTest, CorruptWithNoBackupKeepsTheDamagedFileAndSaysSo) {
  QDir(backupDir()).removeRecursively();
  writeRaw(statePath(), QByteArray("{ truncated"));

  AppController app;

  EXPECT_FALSE(QDir(appDataDir()).entryList({"state.corrupt-*.json"}, QDir::Files).isEmpty());
  bool sawUnrecovered = false;
  for(const QVariant& v : app.recoveryLog()) {
    sawUnrecovered = sawUnrecovered || v.toMap().value(QStringLiteral("kind")).toString() == QLatin1String(heap::recovery::kUnrecovered);
  }
  EXPECT_TRUE(sawUnrecovered);
}

TEST_F(FaultInjectionTest, AFailedWriteIsRecordedInTheFaultLog) {
  AppController::setStateWriterForTesting([](const QString&, const QByteArray&) {
    return false;
  });
  AppController app;
  QVariantMap draft = app.newTaskDraft(QStringLiteral("todo"));
  draft["_isNew"] = true;
  draft["id"] = QStringLiteral("LOST-1");
  draft["title"] = QStringLiteral("edit that never reached disk");
  app.saveTask(draft);  // arms the debounced save
  app.flushSave();
  AppController::setStateWriterForTesting({});

  bool sawWriteFailure = false;
  for(const QVariant& v : app.recoveryLog()) {
    sawWriteFailure = sawWriteFailure || v.toMap().value(QStringLiteral("kind")).toString() == QLatin1String(heap::recovery::kWriteFailed);
  }
  EXPECT_TRUE(sawWriteFailure) << "a failed save left no local record";
}

// The bug-report capture must read only local files. The controller owns a
// QNetworkAccessManager for the update check, so the assertion is not "no
// manager exists" but "capture creates no manager and issues no request": every
// QNetworkReply is a child of the manager that created it, so a request made
// during capture would show up here.
TEST_F(FaultInjectionTest, CaptureForABugReportMakesNoNetworkRequest) {
  writeRaw(statePath(), QByteArray("{ truncated"));
  AppController app;  // fires a recovery, so there is something to attach

  const auto managerCount = [&app] {
    return app.findChildren<QNetworkAccessManager*>().size();
  };
  const auto replyCount = [&app] {
    return app.findChildren<QNetworkReply*>().size();
  };
  const int managersBefore = managerCount();
  ASSERT_EQ(replyCount(), 0) << "a request was already in flight before capture";

  const QString body = app.issueReportBody();
  const QString dest = appDataDir() + "/exported-recovery.log";
  const bool exported = app.exportRecoveryLog(QUrl::fromLocalFile(dest));
  QCoreApplication::processEvents();

  EXPECT_EQ(managerCount(), managersBefore) << "capture created a network manager";
  EXPECT_EQ(replyCount(), 0) << "capture issued a network request";
  EXPECT_TRUE(body.contains(QStringLiteral("Diagnostics")));
  EXPECT_TRUE(body.contains(QStringLiteral("Recovery log"))) << "the recovery record was not attached";
  EXPECT_TRUE(exported);
  EXPECT_TRUE(QFile::exists(dest));
  EXPECT_FALSE(readRaw(dest).isEmpty());
}

TEST_F(FaultInjectionTest, RecoveryIsSurfacedToTheUser) {
  writeRaw(backupDir() + "/state-20260101-000000.json", good_);
  writeRaw(statePath(), QByteArray("{ truncated"));

  AppController app;
  // A persistent storage banner, not a toast that is gone in two seconds
  // under the welcome dialog (PLAT-6).
  EXPECT_EQ(app.storageState(), QStringLiteral("recovered")) << "recovery happened silently";
  EXPECT_FALSE(app.storageMessage().isEmpty());
}

// Retention prunes only the copies the app itself named. Copies someone put in
// the folder by hand (`state-pre-<tag>-<stamp>.json`) sort above the app's own
// by name; pruning by name once kept an alphabetical handful of them and
// deleted the newest ones and the app's own (dogfood 2026-10-19).
TEST_F(FaultInjectionTest, RetentionPrunesOnlyTheAppsOwnCopiesByTime) {
  const QString good = statePath();
  QStringList own;
  for(int day = 1; day <= 25; ++day) {
    const QString name = QStringLiteral("state-202609%1-120000.json").arg(day, 2, 10, QLatin1Char('0'));
    QFile::copy(good, backupDir() + "/" + name);
    own.append(name);
  }
  const QStringList foreign{QStringLiteral("state-pre-dogfood-done-20261017-175145.json"),
                            QStringLiteral("state-pre-console-ticket-20261013-221551.json"),
                            QStringLiteral("state-pre-writeback-toggle-20261008-091137.json"),
                            QStringLiteral("state-premigration-v11-20261001-052830.json")};
  for(const QString& name : foreign) {
    QFile::copy(good, backupDir() + "/" + name);
  }

  AppController app;
  ASSERT_TRUE(app.restoreFromBackup(own.last()));  // takes a -prerestore copy, which prunes

  const QDir dir(backupDir());
  for(const QString& name : foreign) {
    EXPECT_TRUE(dir.exists(name)) << name.toStdString() << " is not the app's and must never be pruned";
  }
  // The newest own copies stay, the oldest go (the seeded 2026-01-01 one first).
  EXPECT_TRUE(dir.exists(own.last()));
  EXPECT_TRUE(dir.exists(QStringLiteral("state-20260906-120000.json")));
  EXPECT_FALSE(dir.exists(QStringLiteral("state-20260905-120000.json")));
  EXPECT_FALSE(dir.exists(QStringLiteral("state-20260101-000000.json")));
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
