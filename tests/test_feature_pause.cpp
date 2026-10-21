// A tracker's sync that keeps failing is paused (owner, 2026-10-21), through
// AppController against a fake Jira on loopback: the shared back-off between
// failed pulls, the pause past the rule, nothing for days but one quiet probe
// a day, "try again" as exactly one request, "switch off" as the tracker's
// own setting with the connection kept.

#include "AppController.h"
#include "FakeHttpServer.h"

#include "net/FeatureGate.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

namespace featurepause {

using heap::net::FeatureGate;
using heap::testing::FakeHttpServer;

class FeaturePause : public ::testing::Test {
 protected:
  void SetUp() override {
    FeatureGate::instance().resetForTests();
    now_ = QDateTime(QDate(2026, 10, 21), QTime(15, 23));
    FeatureGate::setClockForTests([this]() {
      return now_;
    });
    app_ = std::make_unique<::AppController>();
    app_->tasks()->reset({});
    app_->setLanguage(QStringLiteral("en"));
    app_->setAppSettingsJson(QStringLiteral("{}"));
  }

  void TearDown() override {
    app_->setAppSettingsJson(QStringLiteral("{}"));
    app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
    app_.reset();
    FeatureGate::setClockForTests({});
    FeatureGate::instance().resetForTests();
  }

  void connectJira(FakeHttpServer& server) {
    server.route("GET /rest/api/2/serverInfo", {200, R"({"deploymentType":"Cloud"})", {}});
    server.route("POST /rest/api/3/search/jql", {500, R"({"errorMessages":["Internal server error"]})", {}});
    app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("tok"));
    const QJsonObject integrations{{QStringLiteral("liveUpdates"), false},
                                   {QStringLiteral("jira"),
                                    QJsonObject{{QStringLiteral("connected"), true},
                                                {QStringLiteral("baseUrl"), server.base()},
                                                {QStringLiteral("email"), QStringLiteral("me@example.com")},
                                                {QStringLiteral("jql"), QStringLiteral("project = HT")}}}};
    app_->setAppSettingsJson(
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("integrations"), integrations}}).toJson(QJsonDocument::Compact)));
  }

  static int searches(const FakeHttpServer& server) {
    int n = 0;
    for(const auto& r : server.requests()) {
      n += r.key() == "POST /rest/api/3/search/jql" ? 1 : 0;
    }
    return n;
  }

  // The timer's sync at the gate's `now`, waited out.
  void timerSync() {
    app_->syncNow(/*here=*/false);
    heap::testing::waitUntil([this]() {
      return !app_->syncing();
    });
  }

  QDateTime now_;
  std::unique_ptr<::AppController> app_;
};

TEST_F(FeaturePause, AFailingSyncBacksOffThenPausesThenAsksOnceADay) {
  FakeHttpServer server;
  connectJira(server);
  const QString feature = heap::net::features::sync(QStringLiteral("jira"));
  FeatureGate& gate = FeatureGate::instance();

  // The timer every 10 minutes (the user picked 10): after a failure the
  // next pull waits 5, 10, 20, 40 … minutes, not the timer's 10.
  int ticks = 0;
  while(!gate.isPaused(feature) && ticks < 100) {
    timerSync();
    now_ = now_.addSecs(600);
    ++ticks;
  }
  ASSERT_TRUE(gate.isPaused(feature));
  EXPECT_EQ(searches(server), heap::net::kPauseFailures) << "five failed pulls in a row, then the pause";
  EXPECT_GT(ticks, heap::net::kPauseFailures) << "the back-off skipped some ticks";

  // The strip's row.
  const QVariantList rows = app_->pausedFeatures();
  ASSERT_EQ(rows.size(), 1);
  const QVariantMap row = rows.first().toMap();
  EXPECT_EQ(row.value("id").toString(), feature);
  EXPECT_EQ(row.value("kind").toString(), QStringLiteral("errors"));
  EXPECT_EQ(row.value("status").toInt(), 500);
  EXPECT_EQ(row.value("count").toInt(), heap::net::kPauseFailures);
  EXPECT_EQ(row.value("nameArg").toString(), QStringLiteral("Jira"));
  EXPECT_FALSE(row.value("host").toString().isEmpty());

  // Three days of the timer, every 10 minutes: one quiet probe a day.
  const int before = searches(server);
  for(int i = 0; i < 3 * 24 * 6; ++i) {
    timerSync();
    now_ = now_.addSecs(600);
  }
  EXPECT_EQ(searches(server) - before, 3);
  EXPECT_TRUE(gate.isPaused(feature));

  // "Sync everything" by hand leaves the paused one alone…
  const int beforeHand = searches(server);
  app_->syncNow(/*here=*/true);
  heap::testing::waitUntil([this]() {
    return !app_->syncing();
  });
  EXPECT_EQ(searches(server), beforeHand);
  // …"try again" sends exactly one pull.
  app_->retryFeature(feature);
  heap::testing::waitUntil([this]() {
    return !app_->syncing();
  });
  EXPECT_EQ(searches(server), beforeHand + 1);
  timerSync();
  EXPECT_EQ(searches(server), beforeHand + 1) << "and only one";

  // The server is fixed: the next daily probe answers and the sync resumes.
  server.route("POST /rest/api/3/search/jql", {200, R"({"issues":[]})", {}});
  now_ = now_.addSecs(24 * 3600 + 60);
  timerSync();
  EXPECT_FALSE(gate.isPaused(feature));
  EXPECT_TRUE(app_->pausedFeatures().isEmpty());
  // Back to the timer's own cadence.
  const int working = searches(server);
  now_ = now_.addSecs(600);
  timerSync();
  EXPECT_GT(searches(server), working);
}

TEST_F(FeaturePause, SwitchingOffKeepsTheConnection) {
  FakeHttpServer server;
  connectJira(server);
  const QString feature = heap::net::features::sync(QStringLiteral("jira"));
  for(int i = 0; i < 5; ++i) {
    FeatureGate::instance().report(feature, 503, QStringLiteral("jira.x.ru"));
    now_ = now_.addSecs(900);
  }
  ASSERT_TRUE(FeatureGate::instance().isPaused(feature));
  ASSERT_EQ(app_->pausedFeatures().size(), 1);

  app_->disableFeature(feature);
  EXPECT_FALSE(app_->trackerAutoSync(QStringLiteral("jira")));
  const QVariantMap jira = app_->settingsMapForTest().value("integrations").toMap().value("jira").toMap();
  EXPECT_TRUE(jira.value("connected").toBool()) << "never a disconnect";
  EXPECT_TRUE(app_->pausedFeatures().isEmpty());
  // Off: the timer asks nothing, ever.
  const int before = searches(server);
  for(int i = 0; i < 20; ++i) {
    now_ = now_.addSecs(24 * 3600);
    timerSync();
  }
  EXPECT_EQ(searches(server), before);
}

TEST_F(FeaturePause, CodeOwnersAndThreadsSwitchOffTheirOwnSettings) {
  for(const QString& f : {heap::net::features::codeOwners(), heap::net::features::mrThreads()}) {
    for(int i = 0; i < 3; ++i) {
      FeatureGate::instance().report(f, 403, QStringLiteral("gitlab.x.ru"));
    }
  }
  ASSERT_EQ(app_->pausedFeatures().size(), 2);
  EXPECT_EQ(app_->pausedFeatures().first().toMap().value("kind").toString(), QStringLiteral("noAccess"));
  app_->hidePausedFeature(heap::net::features::codeOwners());
  EXPECT_TRUE(FeatureGate::instance().record(heap::net::features::codeOwners()).hidden);
  EXPECT_TRUE(FeatureGate::instance().isPaused(heap::net::features::codeOwners())) << "× hides, it does not unpause";

  app_->disableFeature(heap::net::features::codeOwners());
  app_->disableFeature(heap::net::features::mrThreads());
  const QVariantMap git = app_->settingsMapForTest().value("git").toMap();
  EXPECT_FALSE(git.value("codeOwners", true).toBool());
  EXPECT_FALSE(git.value("mrThreads", true).toBool());
  EXPECT_TRUE(app_->pausedFeatures().isEmpty());
}

}  // namespace featurepause

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir scratch;
  scratch.setAutoRemove(true);
  qputenv("XDG_CONFIG_HOME", scratch.path().toUtf8());
  qputenv("XDG_DATA_HOME", scratch.path().toUtf8());

  QApplication qapp(argc, argv);

  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if(!appData.isEmpty()) {
    QFile::remove(appData + QStringLiteral("/state.json"));
    QFile::remove(appData + QStringLiteral("/secrets.json"));
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
