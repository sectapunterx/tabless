// Who to ping on a tracker ticket's PR / MR (dogfood 2026-10-20): the cards
// the GitLab / GitHub tracker makes, and issues a review was folded into, get
// their CODEOWNERS report through that tracker's API — no watched repo, no
// gh / glab — and a reason when there is none. Read only: the fake forge on
// loopback sees nothing but GETs.

#include "AppController.h"
#include "FakeHttpServer.h"
#include "Models.h"

#include "integrations/ProviderRegistry.h"
#include "integrations/RestIssueProvider.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

namespace forgetickets {

using heap::testing::FakeHttpServer;

const QByteArray kProject = "/api/v4/projects/group%2Fapp";

void routeMr(FakeHttpServer& server, const QByteArray& author, const QByteArray& state) {
  const QByteArray mr = R"({"iid":7,"state":")" + state +
                        R"(","web_url":"https://gitlab.company.ru/group/app/-/merge_requests/7","title":"Fix","target_branch":"main",
      "author":{"username":")" +
                        author + R"("},"reviewers":[{"username":"bob"}]})";
  server.route("GET /api/v4/user", {200, R"({"username":"fin"})", {}});
  server.route("GET " + kProject + "/merge_requests/7", {200, mr, {}});
  server.route("GET " + kProject + "/merge_requests/7/diffs", {200, R"([{"old_path":"src/a.cpp","new_path":"src/a.cpp"}])", {}});
  server.route("GET " + kProject + "/merge_requests/7/approvals", {200, R"({"approved_by":[]})", {}});
  server.route("GET " + kProject + "/repository/files/CODEOWNERS/raw", {200, "src/ @bob @lead\n", {}});
}

class ForgeTickets : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<::AppController>();
    app_->tasks()->reset({});
    app_->setLanguage(QStringLiteral("en"));
    app_->setAppSettingsJson(QStringLiteral("{}"));
  }

  void TearDown() override {
    app_->setAppSettingsJson(QStringLiteral("{}"));
    for(const char* provider : {"gitlab", "github"}) {
      app_->setIntegrationSecret(QString::fromLatin1(provider), QStringLiteral("token"), QString());
    }
    app_.reset();
  }

  void writeSettings(const std::function<void(QJsonObject&)>& change) {
    QJsonObject settings = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object();
    change(settings);
    app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  }

  void connectForge(const QString& id, const FakeHttpServer& server) {
    app_->setIntegrationSecret(id, QStringLiteral("token"), QStringLiteral("tok-") + id);
    writeSettings([&](QJsonObject& s) {
      QJsonObject integrations = s.value(QStringLiteral("integrations")).toObject();
      integrations.insert(id, QJsonObject{{QStringLiteral("connected"), true}, {QStringLiteral("host"), server.base()}});
      s.insert(QStringLiteral("integrations"), integrations);
    });
  }

  // The card the GitLab tracker makes for my MR !7 (APP-242).
  void addMrCard(const QString& id, const QString& state = QStringLiteral("open")) {
    Task t;
    t.id = id;
    t.title = QStringLiteral("Fix");
    t.status = QStringLiteral("review");
    t.externalProvider = QStringLiteral("gitlab");
    t.externalId = QStringLiteral("!7");
    t.externalUrl = QStringLiteral("https://gitlab.company.ru/group/app/-/merge_requests/7");
    t.externalMeta.project = QStringLiteral("group/app");
    t.externalMeta.author = QStringLiteral("fin");
    t.externalMeta.details = QJsonObject{{QStringLiteral("kind"), QStringLiteral("mr")},
                                         {QStringLiteral("state"), state},
                                         {QStringLiteral("sourceBranch"), QStringLiteral("fix/login")},
                                         {QStringLiteral("targetBranch"), QStringLiteral("main")}};
    QVector<Task> all = app_->tasks()->items();
    all.append(t);
    app_->tasks()->reset(all);
  }

  QVariantMap ownersSoon(const QString& id, const char* key) {
    heap::testing::waitUntil(
        [&] {
          return app_->codeOwnersOf(id).contains(QLatin1String(key));
        },
        5000);
    return app_->codeOwnersOf(id);
  }

  static bool onlyGets(const FakeHttpServer& server) {
    return std::ranges::all_of(server.requests(), [](const FakeHttpServer::Request& r) {
      return r.method == "GET";
    });
  }

  std::unique_ptr<::AppController> app_;
};

TEST_F(ForgeTickets, MyMrCardGetsItsOwnersThroughTheTrackersApi) {
  FakeHttpServer server;
  routeMr(server, "fin", "opened");
  connectForge(QStringLiteral("gitlab"), server);
  addMrCard(QStringLiteral("gitlab-MR7"));
  app_->refreshGitForTaskBranch(QStringLiteral("gitlab-MR7"));
  const QVariantMap r = ownersSoon(QStringLiteral("gitlab-MR7"), "found");
  ASSERT_TRUE(r.value(QStringLiteral("found")).toBool()) << server.seen().join('\n').toStdString();
  EXPECT_EQ(r.value(QStringLiteral("toPing")).toStringList(), QStringList{QStringLiteral("@lead")});
  EXPECT_EQ(r.value(QStringLiteral("prNumber")).toInt(), 7);
  EXPECT_EQ(r.value(QStringLiteral("source")).toString(), QStringLiteral("tracker"));
  // The PAT goes as GitLab's PRIVATE-TOKEN, the way the tracker sync sends it.
  EXPECT_EQ(server.lastRequest("GET " + kProject + "/merge_requests/7").headers.value("private-token"), QByteArray("tok-gitlab"));
  // The sidebar's "who to ping" has it too.
  const QVariantList pings = app_->codeOwnerPings();
  EXPECT_TRUE(std::ranges::any_of(pings, [](const QVariant& v) {
    return v.toMap().value(QStringLiteral("handle")).toString() == QLatin1String("@lead");
  }));
  // A settings write (it rebuilds the trackers) keeps a ticket's report.
  writeSettings([](QJsonObject& s) {
    s.insert(QStringLiteral("git"), QJsonObject{{QStringLiteral("watchedRepos"), QJsonArray{}}});
  });
  EXPECT_TRUE(app_->codeOwnersOf(QStringLiteral("gitlab-MR7")).value(QStringLiteral("found")).toBool());
  EXPECT_TRUE(onlyGets(server));
}

TEST_F(ForgeTickets, SomeoneElsesMrSaysSoAndReadsNoFiles) {
  FakeHttpServer server;
  routeMr(server, "colleague", "opened");
  connectForge(QStringLiteral("gitlab"), server);
  addMrCard(QStringLiteral("gitlab-MR7"));
  app_->refreshGitForTaskBranch(QStringLiteral("gitlab-MR7"));
  const QVariantMap r = ownersSoon(QStringLiteral("gitlab-MR7"), "reason");
  EXPECT_EQ(r.value(QStringLiteral("reason")).toString(), QStringLiteral("notMine"));
  EXPECT_EQ(server.seen().count("GET " + kProject + "/merge_requests/7/diffs"), 0);
}

TEST_F(ForgeTickets, AMergedMrHasNoReportAndNoReason) {
  FakeHttpServer server;
  routeMr(server, "fin", "merged");
  connectForge(QStringLiteral("gitlab"), server);
  addMrCard(QStringLiteral("gitlab-MR7"));
  app_->refreshGitForTaskBranch(QStringLiteral("gitlab-MR7"));
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return server.seen().contains("GET " + kProject + "/merge_requests/7");
  }));
  heap::testing::waitUntil(
      [] {
        return false;
      },
      300);
  EXPECT_TRUE(app_->codeOwnersOf(QStringLiteral("gitlab-MR7")).isEmpty());
}

TEST_F(ForgeTickets, NotConnectedSaysWhereToConnectAndAskOnlyNothing) {
  addMrCard(QStringLiteral("gitlab-MR7"));
  app_->refreshGitForTaskBranch(QStringLiteral("gitlab-MR7"));
  const QVariantMap r = ownersSoon(QStringLiteral("gitlab-MR7"), "reason");
  EXPECT_EQ(r.value(QStringLiteral("reason")).toString(), QStringLiteral("noAccess"));
  EXPECT_EQ(r.value(QStringLiteral("host")).toString(), QStringLiteral("gitlab.company.ru"));
  EXPECT_EQ(r.value(QStringLiteral("flavor")).toString(), QStringLiteral("gitlab"));
}

TEST_F(ForgeTickets, TheSettingOffIsTheReason) {
  FakeHttpServer server;
  routeMr(server, "fin", "opened");
  connectForge(QStringLiteral("gitlab"), server);
  writeSettings([](QJsonObject& s) {
    s.insert(QStringLiteral("git"), QJsonObject{{QStringLiteral("codeOwners"), false}});
  });
  addMrCard(QStringLiteral("gitlab-MR7"));
  app_->refreshGitForTaskBranch(QStringLiteral("gitlab-MR7"));
  EXPECT_EQ(app_->codeOwnersOf(QStringLiteral("gitlab-MR7")).value(QStringLiteral("reason")).toString(), QStringLiteral("off"));
  heap::testing::waitUntil(
      [] {
        return false;
      },
      200);
  EXPECT_TRUE(server.seen().isEmpty()) << "off means nothing is asked";
}

TEST_F(ForgeTickets, AnIssueWithAFoldedGithubPrUsesTheOnlyGithubConnected) {
  FakeHttpServer server;
  const QByteArray r = "/api/v3/repos/team/app";
  server.route("GET /api/v3/user", {200, R"({"login":"fin"})", {}});
  server.route("GET " + r + "/pulls/12",
               {200,
                R"({"number":12,"state":"open","html_url":"https://github.com/team/app/pull/12",
      "base":{"ref":"main"},"user":{"login":"fin"}})",
                {}});
  server.route("GET " + r + "/pulls/12/reviews", {200, "[]", {}});
  server.route("GET " + r + "/pulls/12/files", {200, R"([{"filename":"web/x.ts"}])", {}});
  server.route("GET " + r + "/contents/CODEOWNERS", {200, "web/ @frontend\n", {}});
  // An Enterprise-style host on the integration; the PR link says github.com.
  connectForge(QStringLiteral("github"), server);
  Task t;
  t.id = QStringLiteral("jira-ACM-3");
  t.title = QStringLiteral("Login");
  t.status = QStringLiteral("prog");
  QVector<Task> all{t};
  app_->tasks()->reset(all);
  app_->tasks()->setGitInfoForId(t.id,
                                 {{QStringLiteral("prState"), QStringLiteral("open")},
                                  {QStringLiteral("prNumber"), 12},
                                  {QStringLiteral("prUrl"), QStringLiteral("https://github.com/team/app/pull/12")}});
  app_->refreshGitForTaskBranch(t.id);
  const QVariantMap rep = ownersSoon(t.id, "found");
  ASSERT_TRUE(rep.value(QStringLiteral("found")).toBool()) << server.seen().join('\n').toStdString();
  EXPECT_EQ(rep.value(QStringLiteral("location")).toString(), QStringLiteral("CODEOWNERS"));
  EXPECT_EQ(rep.value(QStringLiteral("toPing")).toStringList(), QStringList{QStringLiteral("@frontend")});
  EXPECT_EQ(server.lastRequest("GET " + r + "/pulls/12").headers.value("authorization"), QByteArray("token tok-github"));
  EXPECT_TRUE(onlyGets(server));
}

TEST_F(ForgeTickets, TheTokenSeesTheMrButNotTheFiles) {
  FakeHttpServer server;
  routeMr(server, "fin", "opened");
  server.route("GET " + kProject + "/repository/files/CODEOWNERS/raw", {403, "{}", {}});
  connectForge(QStringLiteral("gitlab"), server);
  addMrCard(QStringLiteral("gitlab-MR7"));
  app_->refreshGitForTaskBranch(QStringLiteral("gitlab-MR7"));
  const QVariantMap r = ownersSoon(QStringLiteral("gitlab-MR7"), "reason");
  EXPECT_EQ(r.value(QStringLiteral("reason")).toString(), QStringLiteral("noRepoAccess"));
  EXPECT_EQ(r.value(QStringLiteral("prNumber")).toInt(), 7);
}

// The forge client sends the token exactly as the tracker sync does.
TEST(ForgeAuthHeaders, PatAndBrowserSignInAsTheTrackerSendsThem) {
  const heap::integrations::ProviderDescriptor* lab = heap::integrations::findDescriptor(QStringLiteral("gitlab"));
  ASSERT_NE(lab, nullptr);
  const auto has = [](const QList<QPair<QByteArray, QByteArray>>& h, const QByteArray& name, const QByteArray& value) {
    return std::ranges::any_of(h, [&](const auto& p) {
      return p.first == name && p.second == value;
    });
  };
  auto h = heap::integrations::authHeaders(*lab, {{QStringLiteral("token"), QStringLiteral("glpat")}});
  EXPECT_TRUE(has(h, "PRIVATE-TOKEN", "glpat"));
  h = heap::integrations::authHeaders(
      *lab, {{QStringLiteral("token"), QStringLiteral("oat")}, {QStringLiteral("authMode"), QStringLiteral("oauth")}});
  EXPECT_TRUE(has(h, "Authorization", "Bearer oat"));
  EXPECT_FALSE(has(h, "PRIVATE-TOKEN", "oat"));
  const heap::integrations::ProviderDescriptor* hub = heap::integrations::findDescriptor(QStringLiteral("github"));
  ASSERT_NE(hub, nullptr);
  h = heap::integrations::authHeaders(*hub, {{QStringLiteral("token"), QStringLiteral("ghp")}});
  EXPECT_TRUE(has(h, "Authorization", "token ghp"));
}

}  // namespace forgetickets

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
