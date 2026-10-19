// Live tracker updates (dogfood 2026-10-19, item 14), through AppController:
// the probe pulls only when the tracker says something changed, the setting
// turns it all off, a catch-up pulls once, and nothing is ever written.
// Tracker traffic goes to a local FakeHttpServer, never a live API.

#include "AppController.h"
#include "FakeHttpServer.h"

#include "integrations/LiveSync.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

namespace liveupdates {

using heap::testing::FakeHttpServer;

QByteArray searchAnswer(const char* updated) {
  return QByteArray(R"({"issues":[{"key":"HT-5","fields":{"summary":"five","status":{"name":"To Do"},"updated":")") + updated +
         QByteArray(R"("}}]})");
}

class LiveUpdates : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<::AppController>();
    app_->tasks()->reset({});
    app_->setLanguage(QStringLiteral("en"));
    app_->setAppSettingsJson(QStringLiteral("{}"));
  }

  void TearDown() override {
    app_->setAppSettingsJson(QStringLiteral("{}"));
    app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
    app_.reset();
  }

  void connectJira(FakeHttpServer& server, bool live) {
    server.route("GET /rest/api/2/serverInfo", {200, R"({"deploymentType":"Cloud"})", {}});
    server.route("POST /rest/api/3/search/jql", {200, searchAnswer("2026-10-19T10:00:00.000+0000"), {}});
    app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("tok"));
    QJsonObject integrations{{QStringLiteral("jira"),
                              QJsonObject{{QStringLiteral("connected"), true},
                                          {QStringLiteral("baseUrl"), server.base()},
                                          {QStringLiteral("email"), QStringLiteral("me@example.com")},
                                          {QStringLiteral("jql"), QStringLiteral("project = HT")}}}};
    if(!live) {
      integrations.insert(QStringLiteral("liveUpdates"), false);
    }
    app_->setAppSettingsJson(
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("integrations"), integrations}}).toJson(QJsonDocument::Compact)));
  }

  // Searches the server got: probes ask for one issue, pulls for a page.
  static int probes(const FakeHttpServer& server) {
    int n = 0;
    for(const auto& r : server.requests()) {
      n += r.key() == "POST /rest/api/3/search/jql" && QJsonDocument::fromJson(r.body).object().value("maxResults").toInt() == 1 ? 1 : 0;
    }
    return n;
  }

  // A pull's page, not the status-history lookup that may follow it (that
  // search expands the changelog).
  static int pulls(const FakeHttpServer& server) {
    int n = 0;
    for(const auto& r : server.requests()) {
      const QJsonObject body = QJsonDocument::fromJson(r.body).object();
      n += r.key() == "POST /rest/api/3/search/jql" && body.value("maxResults").toInt() > 1 &&
                   !body.value("expand").toVariant().toString().contains("changelog")
               ? 1
               : 0;
    }
    return n;
  }

  static int writes(const FakeHttpServer& server) {
    int n = 0;
    for(const auto& r : server.requests()) {
      n += (r.method == "PUT" || r.method == "PATCH" || r.method == "DELETE" || r.path.endsWith("/transitions") ||
            r.path.endsWith("/comment"))
               ? 1
               : 0;
    }
    return n;
  }

  // Waits until the jira pull has answered and its state is in.
  bool pulledOnce() {
    return heap::testing::waitUntil([this]() {
      return app_->liveProbeState(QStringLiteral("jira")).lastPull.isValid() && !app_->syncing();
    });
  }

  std::unique_ptr<::AppController> app_;
};

TEST_F(LiveUpdates, OnByDefault) {
  EXPECT_TRUE(app_->liveUpdatesEnabled());
  app_->setAppSettingsJson(QStringLiteral(R"({"integrations":{"liveUpdates":false}})"));
  EXPECT_FALSE(app_->liveUpdatesEnabled());
}

TEST_F(LiveUpdates, ProbePullsOnlyWhenSomethingChanged) {
  FakeHttpServer server;
  connectJira(server, /*live=*/true);
  app_->setLivePresence(heap::integrations::live::Presence::Active);

  // Never pulled: the first tick pulls.
  app_->liveTickAt(QDateTime::currentDateTime());
  ASSERT_TRUE(pulledOnce());
  EXPECT_EQ(pulls(server), 1);
  EXPECT_EQ(probes(server), 0);

  // A minute on, nothing new: one probe, no pull.
  app_->liveTickAt(QDateTime::currentDateTime().addSecs(61));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return probes(server) == 1 && !app_->liveProbeState(QStringLiteral("jira")).inFlight;
  }));
  heap::testing::waitUntil(
      []() {
        return false;
      },
      300);
  EXPECT_EQ(pulls(server), 1);

  // An issue changed in Jira: the next probe sees it and pulls.
  server.route("POST /rest/api/3/search/jql", {200, searchAnswer("2026-10-19T10:05:00.000+0000"), {}});
  app_->liveTickAt(QDateTime::currentDateTime().addSecs(122));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return pulls(server) == 2;
  }));
  EXPECT_EQ(probes(server), 2);
  EXPECT_EQ(writes(server), 0) << "live updates only read";
}

TEST_F(LiveUpdates, OffMeansNoRequestAtAll) {
  FakeHttpServer server;
  connectJira(server, /*live=*/false);
  app_->liveTickAt(QDateTime::currentDateTime());
  app_->liveCatchUpAt(QDateTime::currentDateTime());
  heap::testing::waitUntil(
      []() {
        return false;
      },
      500);
  EXPECT_EQ(probes(server) + pulls(server), 0);
}

TEST_F(LiveUpdates, CatchUpPullsOnceForBackToBackTriggers) {
  FakeHttpServer server;
  connectJira(server, /*live=*/true);
  const QDateTime now = QDateTime::currentDateTime();
  app_->liveCatchUpAt(now);             // e.g. the machine woke up…
  app_->liveCatchUpAt(now.addSecs(5));  // …and the network came back
  ASSERT_TRUE(pulledOnce());
  heap::testing::waitUntil(
      []() {
        return false;
      },
      300);
  EXPECT_EQ(pulls(server), 1);
  EXPECT_EQ(writes(server), 0);
}

TEST_F(LiveUpdates, ALateTickIsAWakeUp) {
  FakeHttpServer server;
  connectJira(server, /*live=*/true);
  const QDateTime now = QDateTime::currentDateTime();
  app_->liveTickAt(now);
  ASSERT_TRUE(pulledOnce());
  // An hour without a tick: the machine slept, and wakes to a catch-up pull.
  app_->liveTickAt(now.addSecs(3600));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return pulls(server) == 2;
  }));
  EXPECT_EQ(probes(server), 0);
}

}  // namespace liveupdates

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
