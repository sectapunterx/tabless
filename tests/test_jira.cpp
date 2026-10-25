#include "FakeHttpServer.h"

#include "integrations/JiraProvider.h"
#include "integrations/SmartChecklist.h"
#include "integrations/StatusMap.h"
#include "integrations/TrackerImages.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimeZone>

#include <gtest/gtest.h>

using heap::integrations::ExternalTask;
using heap::integrations::jiraAdfToMarkdown;
using heap::integrations::parseJiraIssues;

TEST(JiraParse, ParsesSearchResponse) {
  const QByteArray json = R"({
    "issues": [
      {
        "key": "LTE-2398",
        "fields": {
          "summary": "Handover fails on X2",
          "description": {
            "type": "doc",
            "content": [
              { "type": "paragraph", "content": [ { "type": "text", "text": "First line." } ] },
              { "type": "paragraph", "content": [ { "type": "text", "text": "Second line." } ] }
            ]
          },
          "status": { "name": "In Progress" },
          "priority": { "name": "High" },
          "labels": ["radio", "urgent"],
          "updated": "2026-07-02T12:34:56.000+0000"
        }
      }
    ]
  })";

  const QVector<ExternalTask> tasks = parseJiraIssues(json, "https://acme.atlassian.net/");
  ASSERT_EQ(tasks.size(), 1);

  const ExternalTask& t = tasks.first();
  EXPECT_EQ(t.providerId, QString("jira"));
  EXPECT_EQ(t.externalId, QString("LTE-2398"));
  // Trailing slash on baseUrl is trimmed before building the browse URL.
  EXPECT_EQ(t.url, QString("https://acme.atlassian.net/browse/LTE-2398"));
  EXPECT_EQ(t.title, QString("Handover fails on X2"));
  EXPECT_EQ(t.status, QString("In Progress"));
  EXPECT_EQ(t.priority, QString("High"));
  ASSERT_EQ(t.labels.size(), 2);
  EXPECT_EQ(t.labels[0], QString("radio"));
  // ADF flattened to plain text, paragraphs preserved on separate lines.
  EXPECT_TRUE(t.body.contains("First line."));
  EXPECT_TRUE(t.body.contains("Second line."));
}

TEST(JiraParse, PlainStringDescriptionAndEmpty) {
  // Some responses (or older APIs) carry a plain-string description.
  const QByteArray json = R"({
    "issues": [
      { "key": "AB-1", "fields": { "summary": "s", "description": "just text", "status": { "name": "Done" } } }
    ]
  })";
  const QVector<ExternalTask> tasks = parseJiraIssues(json, "https://x.atlassian.net");
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].body, QString("just text"));
  EXPECT_EQ(tasks[0].status, QString("Done"));

  EXPECT_TRUE(parseJiraIssues(QByteArray(), "https://x").isEmpty());
  EXPECT_TRUE(parseJiraIssues("[]", "https://x").isEmpty());  // array, not the search object
  EXPECT_TRUE(parseJiraIssues("garbage", "https://x").isEmpty());
}

// ── HEAP-117: the identity and context a card needs ──

TEST(JiraParse, ParsesAssigneeReporterDueTypeProjectAndFixVersion) {
  const QByteArray json = R"({
    "issues": [
      {
        "key": "LTE-77",
        "fields": {
          "summary": "s",
          "status": { "name": "To Do" },
          "created": "2026-06-01T09:00:00.000+0000",
          "updated": "2026-07-02T12:34:56.000+0000",
          "duedate": "2026-08-15",
          "assignee": { "displayName": "Ada Lovelace" },
          "reporter": { "displayName": "Grace Hopper" },
          "issuetype": { "name": "Bug" },
          "project": { "key": "LTE" },
          "fixVersions": [ { "name": "24.10" }, { "name": "24.11" } ]
        }
      }
    ]
  })";
  const QVector<ExternalTask> tasks = parseJiraIssues(json, "https://acme.atlassian.net");
  ASSERT_EQ(tasks.size(), 1);
  const ExternalTask& t = tasks[0];
  EXPECT_EQ(t.assignee, QString("Ada Lovelace"));
  EXPECT_EQ(t.author, QString("Grace Hopper"));
  EXPECT_EQ(t.issueType, QString("Bug"));
  EXPECT_EQ(t.project, QString("LTE"));
  EXPECT_EQ(t.milestone, QString("24.10"));  // the first fix version
  EXPECT_TRUE(t.createdAt.isValid());
  ASSERT_TRUE(t.dueAt.isValid());
  EXPECT_EQ(t.dueAt.date(), QDate(2026, 8, 15));
  EXPECT_FALSE(t.dueHasTime);
  // Comments are not requested, so the count stays "unknown".
  EXPECT_EQ(t.commentCount, -1);
}

TEST(JiraParse, TimestampWithColonlessOffsetIsValid) {
  // Jira sends "+0000" without the colon Qt::ISODate expects; the shared
  // timestamp parser has to accept it or every Jira date silently vanishes.
  const QByteArray json = R"({"issues":[{"key":"A-1","fields":{"summary":"s","updated":"2026-07-02T12:34:56.000+0000"}}]})";
  const QVector<ExternalTask> tasks = parseJiraIssues(json, "https://x");
  ASSERT_EQ(tasks.size(), 1);
  ASSERT_TRUE(tasks[0].updatedAt.isValid());
  EXPECT_EQ(tasks[0].updatedAt.toUTC().date(), QDate(2026, 7, 2));
  EXPECT_EQ(tasks[0].updatedAt.toUTC().time(), QTime(12, 34, 56));
}

TEST(JiraParse, MissingOptionalFieldsStayDefault) {
  const QByteArray json = R"({"issues":[{"key":"A-1","fields":{"summary":"s"}}]})";
  const QVector<ExternalTask> tasks = parseJiraIssues(json, "https://x");
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_TRUE(tasks[0].assignee.isEmpty());
  EXPECT_TRUE(tasks[0].author.isEmpty());
  EXPECT_TRUE(tasks[0].milestone.isEmpty());
  EXPECT_FALSE(tasks[0].dueAt.isValid());
}

TEST(JiraComments, FlattensAdfAndPlainStringBodies) {
  // Cloud sends ADF; Server/DC sends a plain string. Both have to read.
  const QByteArray json = R"({
    "comments": [
      {
        "author": { "displayName": "Ada Lovelace" },
        "created": "2026-07-02T12:34:56.000+0000",
        "body": { "type": "doc", "content": [
          { "type": "paragraph", "content": [ { "type": "text", "text": "Reproduced on trunk." } ] } ] }
      },
      { "author": { "displayName": "Grace Hopper" }, "created": "2026-07-01T09:00:00.000+0000",
        "body": "plain server comment" },
      { "author": { "displayName": "nobody" }, "created": "2026-07-01T09:00:00.000+0000", "body": "" }
    ]
  })";
  const QVector<heap::integrations::ExternalComment> comments = heap::integrations::parseJiraComments(json);
  ASSERT_EQ(comments.size(), 2) << "an empty body was kept";
  EXPECT_EQ(comments[0].author, QString("Ada Lovelace"));
  EXPECT_TRUE(comments[0].body.contains("Reproduced on trunk."));
  EXPECT_TRUE(comments[0].createdAt.isValid()) << "the colon-less offset did not parse";
  EXPECT_EQ(comments[1].body, QString("plain server comment"));
}

TEST(JiraComments, HandlesGarbage) {
  EXPECT_TRUE(heap::integrations::parseJiraComments(QByteArray()).isEmpty());
  EXPECT_TRUE(heap::integrations::parseJiraComments("[]").isEmpty());
  EXPECT_TRUE(heap::integrations::parseJiraComments("garbage").isEmpty());
  EXPECT_TRUE(heap::integrations::parseJiraComments(R"({"comments":[]})").isEmpty());
}

TEST(JiraAdf, FlattensNestedContent) {
  const QByteArray adf = R"({
    "type": "doc",
    "content": [
      { "type": "paragraph", "content": [ { "type": "text", "text": "Hello " }, { "type": "text", "text": "world" } ] }
    ]
  })";
  const QString text = jiraAdfToMarkdown(adf);
  EXPECT_TRUE(text.contains("Hello world"));
}

// ── Base URL normalization ──────────────────────────────────────────────────
// People paste whatever their browser showed. Anything that is not a site root
// produced a 404 on every call.

TEST(JiraBaseUrl, NormalizesWhatPeoplePaste) {
  using heap::integrations::normalizeJiraBaseUrl;
  const QString want = QStringLiteral("https://acme.atlassian.net");
  EXPECT_EQ(normalizeJiraBaseUrl("https://acme.atlassian.net"), want);
  EXPECT_EQ(normalizeJiraBaseUrl("  https://acme.atlassian.net/  "), want);
  EXPECT_EQ(normalizeJiraBaseUrl("acme.atlassian.net"), want);  // scheme added
  EXPECT_EQ(normalizeJiraBaseUrl("https://ACME.atlassian.net"), want);
  // A board / project / issue URL straight out of the address bar.
  EXPECT_EQ(normalizeJiraBaseUrl("https://acme.atlassian.net/jira/software/projects/LTE/boards/1"), want);
  EXPECT_EQ(normalizeJiraBaseUrl("https://acme.atlassian.net/browse/LTE-42"), want);
  EXPECT_EQ(normalizeJiraBaseUrl("https://acme.atlassian.net/jira/your-work?atlOrigin=x"), want);

  // A self-hosted instance may legitimately live under a path prefix, so only
  // trailing slashes are trimmed there.
  EXPECT_EQ(normalizeJiraBaseUrl("https://jira.corp.example.com/jira/"), QStringLiteral("https://jira.corp.example.com/jira"));
  EXPECT_EQ(normalizeJiraBaseUrl("http://127.0.0.1:8080"), QStringLiteral("http://127.0.0.1:8080"));

  // Self-hosted: a pasted ticket URL is the single most likely paste, and
  // keeping its path sent every API call to "<host>/browse/TEL-1/rest/api/…".
  EXPECT_EQ(normalizeJiraBaseUrl("https://j.example.com/browse/TEL-123456"), QStringLiteral("https://j.example.com"));
  EXPECT_EQ(normalizeJiraBaseUrl("j.example.com/browse/TEL-123456"), QStringLiteral("https://j.example.com"));
  EXPECT_EQ(normalizeJiraBaseUrl("https://j.example.com/secure/RapidBoard.jspa?rapidView=7"), QStringLiteral("https://j.example.com"));
  EXPECT_EQ(normalizeJiraBaseUrl("https://j.example.com/projects/TEL/issues"), QStringLiteral("https://j.example.com"));
  // …but a real deployment prefix has to survive, including in front of a route.
  EXPECT_EQ(normalizeJiraBaseUrl("https://corp.example.com/jira/browse/TEL-1"), QStringLiteral("https://corp.example.com/jira"));
  EXPECT_EQ(normalizeJiraBaseUrl("https://corp.example.com/jira/secure/Dashboard.jspa"), QStringLiteral("https://corp.example.com/jira"));
  // An API URL names its own root.
  EXPECT_EQ(normalizeJiraBaseUrl("https://j.example.com/rest/api/2/myself"), QStringLiteral("https://j.example.com"));
  EXPECT_EQ(normalizeJiraBaseUrl("   "), QString());
}

TEST(JiraJql, DefaultIsBounded) {
  // /search/jql rejects a query with no search restriction ("Unbounded JQL
  // queries are not allowed here"), which an "order by"-only default is.
  const QString jql = heap::integrations::defaultJiraJql();
  EXPECT_TRUE(jql.contains(QStringLiteral("currentUser()")));
  EXPECT_FALSE(jql.trimmed().startsWith(QStringLiteral("ORDER"), Qt::CaseInsensitive));
}

// ── Scoped-token fallback ───────────────────────────────────────────────────
// Atlassian's newer scoped API tokens are rejected by the site host with a 401
// and only work through https://api.atlassian.com/ex/jira/{cloudId}. The
// provider takes that 401 as the signal to resolve the site's cloudId and retry
// once through the gateway. Driven against a local fake server.

namespace {
using FakeJira = heap::testing::FakeHttpServer;
using heap::testing::waitFor;
}  // namespace

class JiraNetwork : public ::testing::Test {
 protected:
  // What a site said is kept per process (it outlives a provider); each test
  // starts from nothing, on the real clock.
  void SetUp() override {
    heap::integrations::JiraProvider::resetSiteCachesForTesting();
    heap::integrations::JiraProvider::setClockForTesting({});
  }

  void TearDown() override {
    heap::integrations::JiraProvider::resetSiteCachesForTesting();
    heap::integrations::JiraProvider::setClockForTesting({});
  }

  static void SetUpTestSuite() {
    // QNetworkAccessManager needs an application object; it outlives the suite
    // on purpose.
    if(QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "heap_jira_tests";
      static char* argv[] = {arg0, nullptr};
      new QCoreApplication(argc, argv);
    }
  }
};

TEST_F(JiraNetwork, RetriesThroughTheGatewayWhenTheSiteAnswers401) {
  FakeJira server;
  server.route("GET /rest/api/3/myself", {401, R"({"errorMessages":["Client must be authenticated to access this resource."]})"});
  server.route("GET /_edge/tenant_info", {200, R"({"cloudId":"cid-123"})"});
  server.route("GET /ex/jira/cid-123/rest/api/3/myself", {200, R"({"accountId":"a1"})"});

  heap::integrations::JiraProvider p;
  p.setGatewayRoot(server.base());
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("scoped-token"), QString());
  // These exercise the Cloud API (/rest/api/3 + Basic); a loopback host would
  // otherwise be detected as Server/DC, which is a different product.
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  bool ok = false;
  QString error;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool o, const QString& e) {
    ok = o;
    error = e;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done)) << "connectionTested never arrived";
  EXPECT_TRUE(ok) << error.toStdString();
  ASSERT_EQ(server.seen().size(), 3);
  EXPECT_EQ(server.seen()[0], QByteArray("GET /rest/api/3/myself"));
  EXPECT_EQ(server.seen()[1], QByteArray("GET /_edge/tenant_info"));
  EXPECT_EQ(server.seen()[2], QByteArray("GET /ex/jira/cid-123/rest/api/3/myself"));
}

TEST_F(JiraNetwork, KeepsTheOriginal401WhenThereIsNoCloudId) {
  FakeJira server;
  server.route("GET /rest/api/3/myself", {401, R"({"errorMessages":["Basic auth with password is not allowed"]})"});
  // /_edge/tenant_info is unrouted → 404, so the fallback cannot proceed.

  heap::integrations::JiraProvider p;
  p.setGatewayRoot(server.base());
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("bad"), QString());
  // These exercise the Cloud API (/rest/api/3 + Basic); a loopback host would
  // otherwise be detected as Server/DC, which is a different product.
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  bool ok = true;
  QString error;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool o, const QString& e) {
    ok = o;
    error = e;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));
  EXPECT_FALSE(ok);
  // Jira's own words survive — they are more specific than anything generic —
  // and the fix to make is appended rather than replacing them.
  EXPECT_TRUE(error.startsWith(QString::fromUtf8("HTTP 401 — Basic auth with password is not allowed"))) << error.toStdString();
  EXPECT_TRUE(error.contains(QStringLiteral("API token"))) << error.toStdString();
}

TEST_F(JiraNetwork, PullPostsTheBoundedDefaultJql) {
  FakeJira server;
  server.route("POST /rest/api/3/search/jql", {200, R"({"issues":[{"key":"LTE-1","fields":{"summary":"S","status":{"name":"Done"}}}]})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  // These exercise the Cloud API (/rest/api/3 + Basic); a loopback host would
  // otherwise be detected as Server/DC, which is a different product.
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  QVector<ExternalTask> got;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
    got = t;
    done = true;
  });
  QObject::connect(&p, &heap::integrations::IntegrationProvider::pullFailed, &p, [&](int, const QString&) {
    done = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(done));
  ASSERT_EQ(got.size(), 1);
  EXPECT_EQ(got[0].externalId, QStringLiteral("LTE-1"));
  // The browse URL is built from the site, never from the API base.
  EXPECT_EQ(got[0].url, server.base() + QStringLiteral("/browse/LTE-1"));
  // POST, not GET: keeps a long JQL out of the URL and `fields` a real array.
  // (After the one-off look for the sprint field, APP-255.)
  EXPECT_EQ(server.seen().value(0), QByteArray("GET /rest/api/3/field"));
  EXPECT_EQ(server.seen().value(1), QByteArray("POST /rest/api/3/search/jql"));
  EXPECT_TRUE(server.lastBody("POST /rest/api/3/search/jql").contains("currentUser()"))
      << server.lastBody("POST /rest/api/3/search/jql").toStdString();
  // The endpoint returns only the fields asked for, so the identity fields the
  // card renders (HEAP-117) have to be in the request or they never arrive.
  for(const char* field : {"assignee", "reporter", "duedate", "created", "issuetype", "project", "fixVersions"}) {
    EXPECT_TRUE(server.lastBody("POST /rest/api/3/search/jql").contains(field))
        << field << " missing from " << server.lastBody("POST /rest/api/3/search/jql").toStdString();
  }
  // Asking for `comment` would inline every comment body of all 100 issues.
  EXPECT_FALSE(server.lastBody("POST /rest/api/3/search/jql").contains("\"comment\""))
      << server.lastBody("POST /rest/api/3/search/jql").toStdString();
}

// INT-6 (audit 2026-09-30): an issue missing from a "statusCategory != Done"
// pull is looked up by key — found when it was only closed, missing when the
// tracker answers 404, and neither when the lookup itself failed.
TEST_F(JiraNetwork, LookUpTellsAClosedIssueFromADeletedOne) {
  FakeJira server;
  server.route("GET /rest/api/3/issue/HT-11", {200, R"({"key":"HT-11","fields":{"summary":"S","status":{"name":"Done"}}})"});
  server.route("GET /rest/api/3/issue/HT-12",
               {404, R"({"errorMessages":["Issue does not exist or you do not have permission to see it."]})"});
  server.route("GET /rest/api/3/issue/HT-13", {500, R"({})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  ASSERT_TRUE(p.canLookUpIssues());

  bool done = false;
  QVector<ExternalTask> found;
  QStringList missing;
  QObject::connect(
      &p, &heap::integrations::IntegrationProvider::issuesLookedUp, &p, [&](const QVector<ExternalTask>& f, const QStringList& m) {
        found = f;
        missing = m;
        done = true;
      });
  p.lookUpIssues({QStringLiteral("HT-11"), QStringLiteral("HT-12"), QStringLiteral("HT-13")});
  ASSERT_TRUE(waitFor(done)) << "issuesLookedUp never arrived";
  ASSERT_EQ(found.size(), 1);
  EXPECT_EQ(found[0].externalId, QStringLiteral("HT-11"));
  EXPECT_EQ(found[0].status, QStringLiteral("Done"));
  EXPECT_EQ(missing, QStringList{QStringLiteral("HT-12")});
  // The fields a pull asks for, so the merge has what it needs.
  EXPECT_TRUE(server.lastRequest("GET /rest/api/3/issue/HT-11").query.contains("fields=summary"))
      << server.lastRequest("GET /rest/api/3/issue/HT-11").query.toStdString();
}

TEST_F(JiraNetwork, ReportsAFailedPullInsteadOfAnEmptyList) {
  FakeJira server;
  server.route("POST /rest/api/3/search/jql", {400, R"({"errorMessages":["Unbounded JQL queries are not allowed here."]})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QStringLiteral("order by updated DESC"));
  // These exercise the Cloud API (/rest/api/3 + Basic); a loopback host would
  // otherwise be detected as Server/DC, which is a different product.
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  bool fetched = false;
  int status = 0;
  QString error;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>&) {
    fetched = true;
    done = true;
  });
  QObject::connect(&p, &heap::integrations::IntegrationProvider::pullFailed, &p, [&](int s, const QString& e) {
    status = s;
    error = e;
    done = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(done));
  EXPECT_FALSE(fetched) << "a failure must not look like an empty backlog";
  EXPECT_EQ(status, 400);
  EXPECT_EQ(error, QString::fromUtf8("HTTP 400 — Unbounded JQL queries are not allowed here."));
}

// ── Browser sign-in (OAuth 2.0 3LO) ─────────────────────────────────────────
// A 3LO access token is a Bearer credential that the site host never accepts:
// every call has to go through api.atlassian.com/ex/jira/{cloudId}, and the
// cloudId is only knowable by asking accessible-resources at sign-in time.

TEST(JiraSitePick, PrefersTheSiteTheCardAlreadyNames) {
  const QByteArray json = R"([
    {"id":"cid-a","url":"https://a.atlassian.net","name":"A"},
    {"id":"cid-b","url":"https://b.atlassian.net","name":"B"}])";
  const auto picked = heap::integrations::pickJiraSite(json, QStringLiteral("https://b.atlassian.net"));
  EXPECT_EQ(picked.cloudId, QStringLiteral("cid-b")) << "switching sites would repoint every synced issue";
  EXPECT_EQ(picked.url, QStringLiteral("https://b.atlassian.net"));

  // The preference is matched after normalisation, so a pasted browser URL works.
  EXPECT_EQ(heap::integrations::pickJiraSite(json, QStringLiteral("b.atlassian.net/jira/software/projects/X/boards/1")).cloudId,
            QStringLiteral("cid-b"));
}

TEST(JiraSitePick, FallsBackToTheFirstSite) {
  const QByteArray json = R"([{"id":"cid-a","url":"https://a.atlassian.net","name":"A"}])";
  EXPECT_EQ(heap::integrations::pickJiraSite(json, QString()).cloudId, QStringLiteral("cid-a"));
  // A site the token was not granted is not honoured.
  EXPECT_EQ(heap::integrations::pickJiraSite(json, QStringLiteral("https://nope.atlassian.net")).cloudId, QStringLiteral("cid-a"));
}

TEST(JiraSitePick, NoGrantedSiteIsEmpty) {
  EXPECT_TRUE(heap::integrations::pickJiraSite("[]", QString()).cloudId.isEmpty());
  EXPECT_TRUE(heap::integrations::pickJiraSite("not json", QString()).cloudId.isEmpty());
  EXPECT_TRUE(heap::integrations::pickJiraSite({}, QStringLiteral("https://a.atlassian.net")).cloudId.isEmpty());
}

TEST_F(JiraNetwork, OAuthModeSendsABearerThroughTheGateway) {
  FakeJira server;
  server.route("GET /ex/jira/cid-123/rest/api/3/myself", {200, R"({"accountId":"a1"})"});

  heap::integrations::JiraProvider p;
  p.setGatewayRoot(server.base());
  p.setOAuthConfig(QStringLiteral("cid-123"), QStringLiteral("https://acme.atlassian.net"), QStringLiteral("at-3lo"), QString());
  ASSERT_TRUE(p.isConfigured()) << "OAuth needs no email and no base URL";

  bool done = false;
  bool ok = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool o, const QString&) {
    ok = o;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));
  EXPECT_TRUE(ok);

  const auto req = server.lastRequest("GET /ex/jira/cid-123/rest/api/3/myself");
  EXPECT_EQ(req.headers.value("authorization"), QByteArray("Bearer at-3lo")) << "a 3LO token is not a Basic credential";
  // Never the site host: it answers 401 for a 3LO token, and there is no
  // fallback left to run.
  EXPECT_FALSE(server.seen().contains("GET /rest/api/3/myself"));
  EXPECT_FALSE(server.seen().contains("GET /_edge/tenant_info"));
}

TEST_F(JiraNetwork, OAuthModeWithoutACloudIdIsNotConfigured) {
  // The sign-in landed but accessible-resources granted nothing, so there is no
  // API base. Better to stay unconfigured than to send every call to /ex/jira/.
  heap::integrations::JiraProvider p;
  p.setOAuthConfig(QString(), QStringLiteral("https://acme.atlassian.net"), QStringLiteral("at"), QString());
  EXPECT_FALSE(p.isConfigured());
}

TEST_F(JiraNetwork, SwitchingBackToBasicAuthForgetsTheGateway) {
  FakeJira server;
  server.route("GET /rest/api/3/myself", {200, R"({"accountId":"a1"})"});

  heap::integrations::JiraProvider p;
  p.setGatewayRoot(server.base());
  p.setOAuthConfig(QStringLiteral("cid-123"), server.base(), QStringLiteral("at-3lo"), QString());
  // Signing out and pasting an API token instead.
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  // These exercise the Cloud API (/rest/api/3 + Basic); a loopback host would
  // otherwise be detected as Server/DC, which is a different product.
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool, const QString&) {
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));

  const auto req = server.lastRequest("GET /rest/api/3/myself");
  EXPECT_TRUE(req.headers.value("authorization").startsWith("Basic ")) << "a stale Bearer would 401 forever";
  EXPECT_FALSE(server.seen().contains("GET /ex/jira/cid-123/rest/api/3/myself"));
}

// ── What an unauthorized Jira actually tells the user ───────────────────────
// Qt renders every 401 as "Host requires authentication" and Jira's own body
// ("Client must be authenticated to access this resource") is no better: both
// leave the user with nothing to change. In practice it is one of three things
// — the account password instead of an API token, an email that belongs to a
// different Atlassian account, or a Server/DC site that is not supported.

TEST_F(JiraNetwork, ARejectedTokenNamesWhatToChange) {
  FakeJira server;
  server.route("GET /rest/api/3/myself", {401, R"({"message":"Client must be authenticated to access this resource."})"});
  // A Cloud site would answer this; a Server/DC one 404s, which is the tell.
  server.route("GET /_edge/tenant_info", {404, R"({})"});

  heap::integrations::JiraProvider p;
  p.setGatewayRoot(server.base());
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("not-a-token"), QString());
  // These exercise the Cloud API (/rest/api/3 + Basic); a loopback host would
  // otherwise be detected as Server/DC, which is a different product.
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  bool ok = true;
  QString error;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool o, const QString& e) {
    ok = o;
    error = e;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));
  EXPECT_FALSE(ok);
  EXPECT_FALSE(error.contains(QStringLiteral("Host requires authentication"))) << error.toStdString();
  EXPECT_TRUE(error.contains(QStringLiteral("API token"))) << error.toStdString();
  EXPECT_TRUE(error.contains(QStringLiteral("id.atlassian.com"))) << error.toStdString();
  // The site refused us and is not Cloud, so say the thing that is easy to miss.
  EXPECT_TRUE(error.contains(QStringLiteral("Data Center"))) << error.toStdString();
}

TEST_F(JiraNetwork, ACloudSiteThatStillRefusesDoesNotBlameTheEdition) {
  FakeJira server;
  server.route("GET /rest/api/3/myself", {401, R"({"message":"Client must be authenticated to access this resource."})"});
  server.route("GET /_edge/tenant_info", {200, R"({"cloudId":"cid-123"})"});
  // The gateway is the one that has the final say, and it refuses too.
  server.route("GET /ex/jira/cid-123/rest/api/3/myself", {401, R"({"message":"Unauthorized"})"});

  heap::integrations::JiraProvider p;
  p.setGatewayRoot(server.base());
  p.setConfig(server.base(), QStringLiteral("wrong@example.com"), QStringLiteral("tok"), QString());
  // These exercise the Cloud API (/rest/api/3 + Basic); a loopback host would
  // otherwise be detected as Server/DC, which is a different product.
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  QString error;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool, const QString& e) {
    error = e;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));
  EXPECT_TRUE(error.contains(QStringLiteral("API token"))) << error.toStdString();
  EXPECT_FALSE(error.contains(QStringLiteral("Data Center")))
      << "the cloudId resolved, so the edition is not the problem: " << error.toStdString();
}

TEST_F(JiraNetwork, AnExpiredBrowserSessionSaysToSignInAgain) {
  FakeJira server;
  server.route("GET /ex/jira/cid-123/rest/api/3/myself", {401, R"({"message":"Unauthorized"})"});

  heap::integrations::JiraProvider p;
  p.setGatewayRoot(server.base());
  p.setOAuthConfig(QStringLiteral("cid-123"), QStringLiteral("https://acme.atlassian.net"), QStringLiteral("stale"), QString());

  bool done = false;
  QString error;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool, const QString& e) {
    error = e;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));
  EXPECT_TRUE(error.contains(QStringLiteral("sign in again"))) << error.toStdString();
  EXPECT_FALSE(error.contains(QStringLiteral("API token"))) << "there is no token to fix in a browser session";
}

TEST_F(JiraNetwork, ANonAuthFailureIsLeftAlone) {
  FakeJira server;
  server.route("POST /rest/api/3/search/jql", {400, R"({"errorMessages":["Unbounded JQL queries are not allowed here."]})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QStringLiteral("order by updated DESC"));
  // These exercise the Cloud API (/rest/api/3 + Basic); a loopback host would
  // otherwise be detected as Server/DC, which is a different product.
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  QString error;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::pullFailed, &p, [&](int, const QString& e) {
    error = e;
    done = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(done));
  EXPECT_EQ(error, QString::fromUtf8("HTTP 400 — Unbounded JQL queries are not allowed here."))
      << "only 401 is rewritten; everything else keeps the tracker's own words";
}

// ── Jira Server / Data Center ───────────────────────────────────────────────
// A different product behind the same name: /rest/api/2 instead of v3, a
// Personal Access Token as a bearer credential instead of an email+token pair,
// and POST /search instead of Cloud's /search/jql. Getting it wrong produces a
// 401 or a 404 that points at nothing.

TEST(JiraDeploymentDetect, ReadsTheDeploymentTypeFromServerInfo) {
  using heap::integrations::JiraDeployment;
  using heap::integrations::parseJiraDeployment;
  EXPECT_EQ(parseJiraDeployment(R"({"deploymentType":"Cloud","version":"1001.0.0"})"), JiraDeployment::Cloud);
  EXPECT_EQ(parseJiraDeployment(R"({"deploymentType":"Server","version":"9.12.1"})"), JiraDeployment::Server);
  // Atlassian reports Data Center as "Server", but accept the explicit spelling.
  EXPECT_EQ(parseJiraDeployment(R"({"deploymentType":"DataCenter"})"), JiraDeployment::Server);
  EXPECT_EQ(parseJiraDeployment(R"({"deploymentType":"server"})"), JiraDeployment::Server);
  // Nothing usable: the caller falls back to the URL.
  EXPECT_EQ(parseJiraDeployment(R"({"version":"9.12.1"})"), JiraDeployment::Unknown);
  EXPECT_EQ(parseJiraDeployment("not json"), JiraDeployment::Unknown);
}

TEST(JiraDeploymentDetect, TheUrlIsTheFallbackEvidence) {
  using heap::integrations::guessJiraDeployment;
  using heap::integrations::JiraDeployment;
  EXPECT_EQ(guessJiraDeployment(QStringLiteral("https://acme.atlassian.net")), JiraDeployment::Cloud);
  EXPECT_EQ(guessJiraDeployment(QStringLiteral("acme.atlassian.net/browse/X-1")), JiraDeployment::Cloud);
  // Only Atlassian runs atlassian.net; a company domain is self-hosted.
  EXPECT_EQ(guessJiraDeployment(QStringLiteral("https://j.example.com")), JiraDeployment::Server);
  EXPECT_EQ(guessJiraDeployment(QStringLiteral("https://jira.corp.example.com/jira")), JiraDeployment::Server);
  EXPECT_EQ(guessJiraDeployment(QString()), JiraDeployment::Unknown);
}

TEST_F(JiraNetwork, DetectsServerAndUsesV2WithABearerToken) {
  FakeJira server;
  server.route("GET /rest/api/2/serverInfo", {200, R"({"deploymentType":"Server","version":"9.12.1"})"});
  server.route("GET /rest/api/2/myself", {200, R"({"name":"alex"})"});

  heap::integrations::JiraProvider p;
  // No email: a Personal Access Token authenticates on its own.
  p.setConfig(server.base(), QString(), QStringLiteral("pat-abc"), QString());
  ASSERT_TRUE(p.isConfigured()) << "Server/DC needs no account email";

  bool done = false;
  bool ok = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool o, const QString&) {
    ok = o;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));
  EXPECT_TRUE(ok);
  EXPECT_EQ(p.deployment(), heap::integrations::JiraDeployment::Server);

  EXPECT_EQ(server.lastRequest("GET /rest/api/2/myself").headers.value("authorization"), QByteArray("Bearer pat-abc"));
  // v3 is Cloud-only and would 404 here.
  EXPECT_FALSE(server.seen().contains("GET /rest/api/3/myself"));
}

TEST_F(JiraNetwork, ServerPullsThroughTheV2SearchEndpoint) {
  FakeJira server;
  server.route("GET /rest/api/2/serverInfo", {200, R"({"deploymentType":"Server"})"});
  // Server/DC never had /search/jql, and its description is plain text rather
  // than the ADF document Cloud returns.
  server.route("POST /rest/api/2/search", {200, R"({"issues":[
      {"key":"TEL-123","fields":{"summary":"Handover fails","description":"plain text body",
       "status":{"name":"In Progress"},"priority":{"name":"High"},"labels":["ran"],
       "updated":"2026-01-02T03:04:05.000+0300"}}]})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QString(), QStringLiteral("pat"), QString());

  bool done = false;
  QVector<ExternalTask> got;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
    got = t;
    done = true;
  });
  QObject::connect(&p, &heap::integrations::IntegrationProvider::pullFailed, &p, [&](int, const QString&) {
    done = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(done));

  ASSERT_EQ(got.size(), 1);
  EXPECT_EQ(got[0].externalId, QStringLiteral("TEL-123"));
  EXPECT_EQ(got[0].body, QStringLiteral("plain text body")) << "a v2 description is a string, not ADF";
  EXPECT_EQ(got[0].url, server.base() + QStringLiteral("/browse/TEL-123"));
  EXPECT_FALSE(server.seen().contains("POST /rest/api/3/search/jql"));
}

TEST_F(JiraNetwork, ServerNeverTriesTheCloudGateway) {
  FakeJira server;
  server.route("GET /rest/api/2/serverInfo", {200, R"({"deploymentType":"Server"})"});
  server.route("GET /rest/api/2/myself", {401, R"({"message":"Unauthorized"})"});

  heap::integrations::JiraProvider p;
  p.setGatewayRoot(server.base());
  p.setConfig(server.base(), QString(), QStringLiteral("bad-pat"), QString());

  bool done = false;
  QString error;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool, const QString& e) {
    error = e;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));

  // api.atlassian.com is a Cloud-only thing; chasing it here wastes a request
  // and hides the real answer.
  EXPECT_FALSE(server.seen().contains("GET /_edge/tenant_info"));
  EXPECT_TRUE(error.contains(QStringLiteral("Personal Access Token"))) << error.toStdString();
  EXPECT_FALSE(error.contains(QStringLiteral("id.atlassian.com"))) << "that is the Cloud advice: " << error.toStdString();
}

TEST_F(JiraNetwork, AServerInstanceThatHidesServerInfoIsStillDetectedFromItsUrl) {
  FakeJira server;
  // Behind SSO, anonymous reads refused — the URL is the only evidence left.
  server.route("GET /rest/api/2/serverInfo", {401, R"({})"});
  server.route("GET /rest/api/2/myself", {200, R"({"name":"alex"})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QString(), QStringLiteral("pat"), QString());

  bool done = false;
  bool ok = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool o, const QString&) {
    ok = o;
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));
  EXPECT_TRUE(ok);
  EXPECT_EQ(p.deployment(), heap::integrations::JiraDeployment::Server) << "a loopback host is not atlassian.net";
}

TEST_F(JiraNetwork, AnOldServerWithoutPatsStillWorksOnBasicAuth) {
  FakeJira server;
  server.route("GET /rest/api/2/serverInfo", {200, R"({"deploymentType":"Server"})"});
  server.route("GET /rest/api/2/myself", {200, R"({"name":"alex"})"});

  heap::integrations::JiraProvider p;
  // Filling in the username opts back into Basic, for a Jira too old for PATs.
  p.setConfig(server.base(), QStringLiteral("alex"), QStringLiteral("password"), QString());

  bool done = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool, const QString&) {
    done = true;
  });
  p.testConnection();
  ASSERT_TRUE(waitFor(done));
  EXPECT_TRUE(server.lastRequest("GET /rest/api/2/myself").headers.value("authorization").startsWith("Basic "));
}

TEST_F(JiraNetwork, TheDeploymentIsProbedOnceNotPerRequest) {
  FakeJira server;
  server.route("GET /rest/api/2/serverInfo", {200, R"({"deploymentType":"Server"})"});
  server.route("GET /rest/api/2/myself", {200, R"({"name":"alex"})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QString(), QStringLiteral("pat"), QString());

  for(int i = 0; i < 3; ++i) {
    bool done = false;
    QObject::connect(&p, &heap::integrations::IntegrationProvider::connectionTested, &p, [&](bool, const QString&) {
      done = true;
    });
    p.testConnection();
    ASSERT_TRUE(waitFor(done));
  }
  int probes = 0;
  for(const QByteArray& seen : server.seen()) {
    probes += seen == "GET /rest/api/2/serverInfo" ? 1 : 0;
  }
  EXPECT_EQ(probes, 1) << "the answer does not change between requests";
}

// ─── Search pagination ───────────────────────────────────────────────
//
// /search/jql caps at 100 issues a page and hands back an opaque
// nextPageToken; the code used to read page one and stop, with a comment
// saying a single page was "sufficient for v1". A JQL matching 250 issues
// mirrored 100 of them and said nothing.

TEST_F(JiraNetwork, PullWalksEveryPageOfACloudSearch) {
  FakeJira server;
  server.routeSequence("POST /rest/api/3/search/jql",
                       {{200, R"({"issues":[{"key":"LTE-1","fields":{"summary":"one"}}],"nextPageToken":"TOK2","isLast":false})"},
                        {200, R"({"issues":[{"key":"LTE-2","fields":{"summary":"two"}}],"isLast":true})"}});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  int emitted = 0;
  QVector<ExternalTask> got;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
    got = t;
    ++emitted;
    done = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(done));

  ASSERT_EQ(got.size(), 2);
  EXPECT_EQ(got[0].externalId, QStringLiteral("LTE-1"));
  EXPECT_EQ(got[1].externalId, QStringLiteral("LTE-2"));
  // One signal for the whole walk: AppController toasts per tasksFetched.
  EXPECT_EQ(emitted, 1);
  EXPECT_EQ(server.seen().count("POST /rest/api/3/search/jql"), 2);
  // The token from page one has to come back in the body of page two.
  EXPECT_TRUE(server.lastBody("POST /rest/api/3/search/jql").contains("TOK2"))
      << server.lastBody("POST /rest/api/3/search/jql").toStdString();
}

TEST_F(JiraNetwork, ALastPageWithNoTokenEndsTheWalk) {
  FakeJira server;
  // No nextPageToken at all — the shape a short result set comes back in.
  server.route("POST /rest/api/3/search/jql", {200, R"({"issues":[{"key":"LTE-9","fields":{"summary":"only"}}]})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  QVector<ExternalTask> got;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
    got = t;
    done = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(done));

  EXPECT_EQ(got.size(), 1);
  EXPECT_EQ(server.seen().count("POST /rest/api/3/search/jql"), 1) << "no token means no second request";
}

TEST_F(JiraNetwork, AFailedSecondPageKeepsTheFirst) {
  FakeJira server;
  server.routeSequence("POST /rest/api/3/search/jql",
                       {{200, R"({"issues":[{"key":"LTE-1","fields":{"summary":"one"}}],"nextPageToken":"TOK2","isLast":false})"},
                        {500, R"({"errorMessages":["boom"]})"}});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  QVector<ExternalTask> got;
  int failStatus = -1;
  QString failError;
  bool failed = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
    got = t;
  });
  QObject::connect(&p, &heap::integrations::IntegrationProvider::pullFailed, &p, [&](int status, const QString& error) {
    failStatus = status;
    failError = error;
    failed = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(failed));

  EXPECT_EQ(got.size(), 1) << "page one is real work and should still land";
  // Status 0 on purpose: a mid-walk 401 must not send AppController into its
  // refresh-and-resync path, which would re-pull page one forever.
  EXPECT_EQ(failStatus, 0);
  EXPECT_TRUE(failError.contains(QStringLiteral("only 1 page"))) << failError.toStdString();
}

TEST_F(JiraNetwork, AServerSearchPagesByRowOffset) {
  FakeJira server;
  // Server/DC has no cursor. A page shorter than maxResults is the last one,
  // and a one-issue page is short, so this stops after the first request.
  server.route("POST /rest/api/2/search", {200, R"({"issues":[{"key":"OPS-1","fields":{"summary":"s"}}]})"});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Server);

  bool done = false;
  QVector<ExternalTask> got;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
    got = t;
    done = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(done));

  EXPECT_EQ(got.size(), 1);
  EXPECT_EQ(server.seen().count("POST /rest/api/2/search"), 1);
  // The first page must not carry startAt at all — some older Server versions
  // reject startAt=0 alongside an empty JQL clause.
  EXPECT_FALSE(server.lastBody("POST /rest/api/2/search").contains("startAt")) << server.lastBody("POST /rest/api/2/search").toStdString();
}

// ── ADF → markdown (audit A4) ──────────────────────────────────────────────

TEST(JiraProvider, JiraAdfToMarkdown_CodeBlockThenList_KeepsThemApart) {
  const QByteArray adf = R"json({"type":"doc","content":[
    {"type":"codeBlock","attrs":{"language":"cpp"},"content":[{"type":"text","text":"int main() {\n}"}]},
    {"type":"bulletList","content":[
      {"type":"listItem","content":[{"type":"paragraph","content":[{"type":"text","text":"item one"}]}]},
      {"type":"listItem","content":[{"type":"paragraph","content":[{"type":"text","text":"item two"}]}]}]}]})json";
  EXPECT_EQ(jiraAdfToMarkdown(adf), QStringLiteral("```cpp\nint main() {\n}\n```\n\n- item one\n- item two"));
}

TEST(JiraProvider, JiraAdfToMarkdown_Table_BecomesMarkdownTable) {
  const QByteArray adf = R"json({"type":"doc","content":[{"type":"table","content":[
    {"type":"tableRow","content":[
      {"type":"tableHeader","content":[{"type":"paragraph","content":[{"type":"text","text":"Key"}]}]},
      {"type":"tableHeader","content":[{"type":"paragraph","content":[{"type":"text","text":"Value"}]}]}]},
    {"type":"tableRow","content":[
      {"type":"tableCell","content":[{"type":"paragraph","content":[{"type":"text","text":"a|b"}]}]},
      {"type":"tableCell","content":[{"type":"paragraph","content":[{"type":"text","text":"1"}]}]}]}]}]})json";
  EXPECT_EQ(jiraAdfToMarkdown(adf), QStringLiteral("| Key | Value |\n| --- | --- |\n| a\\|b | 1 |"));
}

TEST(JiraProvider, JiraAdfToMarkdown_HeadingMarksAndLink_AreKept) {
  const QByteArray adf = R"json({"type":"doc","content":[
    {"type":"heading","attrs":{"level":2},"content":[{"type":"text","text":"Steps"}]},
    {"type":"paragraph","content":[
      {"type":"text","text":"see "},
      {"type":"text","text":"the spec","marks":[{"type":"link","attrs":{"href":"https://example.com/spec"}}]},
      {"type":"text","text":" and "},
      {"type":"text","text":"this","marks":[{"type":"strong"}]},
      {"type":"text","text":" "},
      {"type":"text","text":"x()","marks":[{"type":"code"}]}]}]})json";
  EXPECT_EQ(jiraAdfToMarkdown(adf), QStringLiteral("## Steps\n\nsee [the spec](https://example.com/spec) and **this** `x()`"));
}

TEST(JiraProvider, JiraAdfToMarkdown_OrderedAndTaskLists_KeepMarkers) {
  const QByteArray adf = R"json({"type":"doc","content":[
    {"type":"orderedList","attrs":{"order":1},"content":[
      {"type":"listItem","content":[{"type":"paragraph","content":[{"type":"text","text":"first"}]}]},
      {"type":"listItem","content":[{"type":"paragraph","content":[{"type":"text","text":"second"}]}]}]},
    {"type":"taskList","content":[
      {"type":"taskItem","attrs":{"state":"DONE"},"content":[{"type":"text","text":"done"}]},
      {"type":"taskItem","attrs":{"state":"TODO"},"content":[{"type":"text","text":"open"}]}]}]})json";
  EXPECT_EQ(jiraAdfToMarkdown(adf), QStringLiteral("1. first\n2. second\n\n- [x] done\n- [ ] open"));
}

TEST(JiraProvider, ParseJiraComments_LinkInAdfBody_KeepsTarget) {
  const QByteArray json = R"json({"comments":[{"author":{"displayName":"A"},"created":"2026-01-01T10:00:00.000+0000",
    "body":{"type":"doc","content":[{"type":"paragraph","content":[
      {"type":"text","text":"log","marks":[{"type":"link","attrs":{"href":"https://x.test/log"}}]}]}]}}]})json";
  const auto comments = heap::integrations::parseJiraComments(json);
  ASSERT_EQ(comments.size(), 1);
  // Markdown, like the description and like GitHub's comments (INT-9).
  EXPECT_EQ(comments.at(0).body, QStringLiteral("[log](https://x.test/log)"));
}

TEST(JiraProvider, ParseJiraComments_BoldAndCode_SurviveAsMarkdown) {
  // The audit's "comment 2 bold" lost its strong mark on the way in.
  const QByteArray json = R"json({"comments":[{"author":{"displayName":"A"},"created":"2026-01-01T10:00:00.000+0000",
    "body":{"type":"doc","content":[
      {"type":"paragraph","content":[{"type":"text","text":"comment 2 "},
        {"type":"text","text":"bold","marks":[{"type":"strong"}]},{"type":"text","text":" "},
        {"type":"text","text":"x()","marks":[{"type":"code"}]}]},
      {"type":"bulletList","content":[
        {"type":"listItem","content":[{"type":"paragraph","content":[{"type":"text","text":"one"}]}]}]}]}}]})json";
  const auto comments = heap::integrations::parseJiraComments(json);
  ASSERT_EQ(comments.size(), 1);
  EXPECT_EQ(comments.at(0).body, QStringLiteral("comment 2 **bold** `x()`\n\n- one"));
}

TEST(JiraProvider, ParseJiraIssues_ExpandedTransitions_AreKept) {
  const QByteArray json = R"json({"issues":[
    {"key":"HT-1","fields":{"summary":"a","status":{"name":"To Do"}},
     "transitions":[{"id":"11","to":{"name":"In Progress"}},{"id":"21","to":{"name":"Done"}},{"id":"31","to":{"name":"Done"}}]},
    {"key":"HT-2","fields":{"summary":"b","status":{"name":"To Do"}}}]})json";
  const auto issues = heap::integrations::parseJiraIssues(json, QStringLiteral("https://acme.atlassian.net"));
  ASSERT_EQ(issues.size(), 2);
  EXPECT_TRUE(issues.at(0).transitionsKnown);
  EXPECT_EQ(issues.at(0).transitions, (QStringList{"In Progress", "Done"}));
  // Not expanded: nothing is known, so nothing is refused on its account.
  EXPECT_FALSE(issues.at(1).transitionsKnown);
}

TEST(JiraProvider, ParseJiraStatuses_StatusList_ReturnsSortedUniqueNames) {
  const QByteArray json = R"json([{"name":"To Do"},{"name":"Blocked"},{"name":"To Do"},{"name":" In Review "},{"id":"7"}])json";
  EXPECT_EQ(heap::integrations::parseJiraStatuses(json), (QStringList{"Blocked", "In Review", "To Do"}));
}

// Audit C8: a Jira comment links to itself on the issue page.
TEST(JiraProvider, ParseJiraComments_CommentId_BecomesAnAnchor) {
  const QByteArray json = R"json({"comments":[{"id":"10042","author":{"displayName":"A"},"body":"hi"}]})json";
  const auto comments = heap::integrations::parseJiraComments(json);
  ASSERT_EQ(comments.size(), 1);
  EXPECT_EQ(comments.at(0).anchor, QStringLiteral("?focusedCommentId=10042"));
}

// ── Sprints (APP-255): read-only, the field found per site ──────────────────

TEST(JiraSprint, TheFieldIsFoundBySchema) {
  const QByteArray fields = R"([
    {"id": "summary", "name": "Summary", "schema": {"type": "string", "system": "summary"}},
    {"id": "customfield_10020", "name": "Sprint", "schema": {"type": "array", "custom": "com.pyxis.greenhopper.jira:gh-sprint"}},
    {"id": "customfield_10014", "name": "Epic Link", "schema": {"custom": "com.pyxis.greenhopper.jira:gh-epic-link"}}
  ])";
  EXPECT_EQ(heap::integrations::parseJiraSprintField(fields), QStringLiteral("customfield_10020"));
  EXPECT_TRUE(heap::integrations::parseJiraSprintField(R"([{"id":"summary","schema":{"system":"summary"}}])").isEmpty());
  EXPECT_TRUE(heap::integrations::parseJiraSprintField("{}").isEmpty());
}

TEST(JiraSprint, TheActiveOneWins_AClosedOneIsNeverShown) {
  using heap::integrations::jiraCurrentSprint;
  // In two sprints: the closed one it carried over from, and the active one.
  const QJsonArray two = QJsonDocument::fromJson(R"([
    {"id": 1, "name": "S 13", "state": "closed", "startDate": "2026-09-21T09:00:00.000Z", "endDate": "2026-10-02T17:00:00.000Z"},
    {"id": 2, "name": "S 14", "state": "active", "startDate": "2026-10-05T09:00:00.000Z", "endDate": "2026-10-16T17:00:00.000Z"}
  ])")
                             .array();
  const QJsonObject s = jiraCurrentSprint(two);
  EXPECT_EQ(s.value(QStringLiteral("name")).toString(), QStringLiteral("S 14"));
  EXPECT_EQ(s.value(QStringLiteral("state")).toString(), QStringLiteral("active"));
  EXPECT_EQ(s.value(QStringLiteral("end")).toString(), QStringLiteral("2026-10-16T17:00:00.000Z"));

  const QJsonArray closedOnly = QJsonDocument::fromJson(R"([{"name": "S 13", "state": "closed"}])").array();
  EXPECT_TRUE(jiraCurrentSprint(closedOnly).isEmpty());
  EXPECT_TRUE(jiraCurrentSprint(QJsonValue()).isEmpty());

  const QJsonArray futures = QJsonDocument::fromJson(R"([
    {"name": "S 16", "state": "future", "startDate": "2026-11-02T09:00:00.000Z"},
    {"name": "S 15", "state": "future", "startDate": "2026-10-19T09:00:00.000Z"}
  ])")
                                 .array();
  EXPECT_EQ(jiraCurrentSprint(futures).value(QStringLiteral("name")).toString(), QStringLiteral("S 15"));
}

TEST(JiraSprint, ServerStringsAreRead) {
  const QJsonArray server{
      QStringLiteral("com.atlassian.greenhopper.service.sprint.Sprint@1a2b[id=7,rapidViewId=3,state=ACTIVE,name=Team, sprint 7,"
                     "startDate=2026-10-05T09:00:00.000+03:00,endDate=2026-10-16T18:00:00.000+03:00,completeDate=<null>,sequence=7]")};
  const QJsonObject s = heap::integrations::jiraCurrentSprint(server);
  EXPECT_EQ(s.value(QStringLiteral("name")).toString(), QStringLiteral("Team, sprint 7"));
  EXPECT_EQ(s.value(QStringLiteral("state")).toString(), QStringLiteral("active"));
  EXPECT_EQ(s.value(QStringLiteral("end")).toString(), QStringLiteral("2026-10-16T18:00:00.000+03:00"));
}

TEST(JiraSprint, AnIssueCarriesItsSprintOnlyWhenTheSiteHasTheField) {
  const QByteArray json = R"({"issues": [{"key": "LTE-1", "fields": {"summary": "S", "status": {"name": "To Do"},
    "customfield_10020": [{"name": "S 14", "state": "active", "endDate": "2026-10-16T17:00:00.000Z"}]}}]})";
  const QVector<ExternalTask> with = parseJiraIssues(json, QStringLiteral("https://x"), QStringLiteral("customfield_10020"));
  ASSERT_EQ(with.size(), 1);
  EXPECT_EQ(with.at(0).details.value(QStringLiteral("sprint")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("S 14"));
  const QVector<ExternalTask> without = parseJiraIssues(json, QStringLiteral("https://x"));
  ASSERT_EQ(without.size(), 1);
  EXPECT_TRUE(without.at(0).details.isEmpty());
}

TEST_F(JiraNetwork, PullAsksForTheSprintFieldOnce_AndOnlyWhenTheSiteHasOne) {
  for(const bool hasSprints : {true, false}) {
    FakeJira server;
    server.route("GET /rest/api/3/field",
                 {200,
                  hasSprints ? R"([{"id":"customfield_10020","schema":{"custom":"com.pyxis.greenhopper.jira:gh-sprint"}}])"
                             : R"([{"id":"summary","schema":{"system":"summary"}}])"});
    server.route("POST /rest/api/3/search/jql",
                 {200,
                  R"({"isLast":true,"issues":[{"key":"LTE-1","fields":{"summary":"S","status":{"name":"To Do"},
                    "customfield_10020":[{"name":"S 14","state":"active","endDate":"2026-10-16"}]}}]})"});
    heap::integrations::JiraProvider p;
    p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
    p.setDeployment(heap::integrations::JiraDeployment::Cloud);
    int fetched = 0;
    QVector<ExternalTask> got;
    QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
      got = t;
      ++fetched;
    });
    p.pullTasks();
    ASSERT_TRUE(heap::testing::waitUntil([&] {
      return fetched == 1;
    }));
    p.pullTasks();
    ASSERT_TRUE(heap::testing::waitUntil([&] {
      return fetched == 2;
    }));
    EXPECT_EQ(server.seen().count("GET /rest/api/3/field"), 1);
    EXPECT_EQ(server.lastBody("POST /rest/api/3/search/jql").contains("customfield_10020"), hasSprints)
        << server.lastBody("POST /rest/api/3/search/jql").toStdString();
    ASSERT_EQ(got.size(), 1);
    EXPECT_EQ(got.at(0).details.contains(QStringLiteral("sprint")), hasSprints);
    for(const auto& r : server.requests()) {
      EXPECT_TRUE(r.method == "GET" || r.path.endsWith("/search/jql")) << "nothing is written to a sprint: " << r.key().toStdString();
    }
  }
}

// ── Live updates probe (dogfood 2026-10-19, item 14) ──

TEST(JiraLiveProbe, TheJqlNarrowsTheFilterToRecentChanges) {
  using heap::integrations::jiraChangedSinceJql;
  EXPECT_EQ(jiraChangedSinceJql(QStringLiteral("project = LTE ORDER BY priority DESC"), 15),
            QStringLiteral("(project = LTE) AND updated >= -15m ORDER BY updated DESC"));
  EXPECT_EQ(jiraChangedSinceJql(QStringLiteral("a = 1 OR b = 2"), 0),
            QStringLiteral("(a = 1 OR b = 2) AND updated >= -1m ORDER BY updated DESC"))
      << "an OR stays inside its parentheses; a zero window is a minute";
  // An empty filter is the default one, which is bounded.
  EXPECT_EQ(jiraChangedSinceJql(QString(), 5), QStringLiteral("(assignee = currentUser()) AND updated >= -5m ORDER BY updated DESC"));
}

TEST(JiraLiveProbe, TheNewestUpdatedIsRead) {
  using heap::integrations::jiraNewestUpdated;
  EXPECT_FALSE(jiraNewestUpdated(R"({"issues":[]})").isValid());
  EXPECT_FALSE(jiraNewestUpdated("not json").isValid());
  const QDateTime at = jiraNewestUpdated(
      R"({"issues":[{"key":"A-1","fields":{"updated":"2026-10-19T10:00:00.000+0300"}},{"key":"A-2","fields":{"updated":"2026-10-19T08:30:00.000+0000"}}]})");
  ASSERT_TRUE(at.isValid());
  EXPECT_EQ(at.toUTC(), QDateTime(QDate(2026, 10, 19), QTime(8, 30), QTimeZone::UTC));
}

TEST_F(JiraNetwork, ProbeAsksForOneIssueAndOneField) {
  FakeJira server;
  server.route("POST /rest/api/3/search/jql", {200, R"({"issues":[{"key":"LTE-7","fields":{"updated":"2026-10-19T10:00:00.000+0000"}}]})"});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QStringLiteral("project = LTE"));
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  ASSERT_TRUE(p.canProbeChanges());

  bool done = false;
  bool ok = false;
  QDateTime newest;
  QObject::connect(&p,
                   &heap::integrations::IntegrationProvider::changesProbed,
                   &p,
                   [&](bool o, int /*status*/, int /*retryAfter*/, const QDateTime& n, const QString& /*error*/) {
                     ok = o;
                     newest = n;
                     done = true;
                   });
  p.probeChanges(12);
  ASSERT_TRUE(waitFor(done));
  EXPECT_TRUE(ok);
  EXPECT_EQ(newest.toUTC(), QDateTime(QDate(2026, 10, 19), QTime(10, 0), QTimeZone::UTC));
  ASSERT_EQ(server.requests().size(), 1) << "a probe is one read, nothing written";
  const QJsonObject body = QJsonDocument::fromJson(server.lastBody()).object();
  EXPECT_EQ(body.value(QStringLiteral("jql")).toString(), QStringLiteral("(project = LTE) AND updated >= -12m ORDER BY updated DESC"));
  EXPECT_EQ(body.value(QStringLiteral("maxResults")).toInt(), 1);
  EXPECT_EQ(body.value(QStringLiteral("fields")).toArray(), QJsonArray{QStringLiteral("updated")});
}

TEST_F(JiraNetwork, ProbeReportsRetryAfterOnA429) {
  FakeJira server;
  server.route("POST /rest/api/3/search/jql", {429, R"({"errorMessages":["Rate limit exceeded"]})", {{"Retry-After", "42"}}});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  bool ok = true;
  int status = 0;
  int retryAfter = 0;
  QObject::connect(&p,
                   &heap::integrations::IntegrationProvider::changesProbed,
                   &p,
                   [&](bool o, int s, int r, const QDateTime& /*newest*/, const QString& /*error*/) {
                     ok = o;
                     status = s;
                     retryAfter = r;
                     done = true;
                   });
  p.probeChanges(5);
  ASSERT_TRUE(waitFor(done));
  EXPECT_FALSE(ok);
  EXPECT_EQ(status, 429);
  EXPECT_EQ(retryAfter, 42);
}

// ─── Project statuses, not the instance's (dogfood 2026-10-20) ──────────────
// The mapping in Settings listed GET /status — every status of every project
// on a company Jira — sorted and cut at 64, so the user's own project
// statuses ("In Progress", "Selected for Development", …) were not in it.

namespace {

struct OwnStatus {
  const char* name;
  const char* category;
  const char* column;  // what the mapping should guess
};

// The owner's project workflow, with the kinds Jira gives them.
const OwnStatus kOwnStatuses[] = {
    {"Backlog", "new", "backlog"},
    {"Selected for Development", "new", "todo"},
    {"In Progress", "indeterminate", "prog"},
    {"Done", "done", "done"},
    {"Obsolete", "done", "done"},
    {"In Review", "indeterminate", "review"},
    {"On hold", "indeterminate", "blocked"},
    {"Blocked", "indeterminate", "blocked"},
    {"Ready for Review", "indeterminate", "review"},
    {"Ready for Testing", "indeterminate", "review"},
    {"In Testing", "indeterminate", "review"},
    {"Funnel", "new", "backlog"},
    {"New", "new", "todo"},
    {"Accepted", "new", "todo"},
    {"Evaluate", "indeterminate", "todo"},
    {"In Discovery", "indeterminate", "todo"},
    {"Ready for Discovery", "new", "todo"},
    {"Resolved", "done", "done"},
    {"Reopened", "new", "todo"},
    {"Suspended", "done", "done"},
};

// GET /project/HT/statuses: two issue types sharing most statuses.
QByteArray ownProjectStatusesJson() {
  QJsonArray task;
  QJsonArray bug;
  int i = 0;
  for(const OwnStatus& s : kOwnStatuses) {
    const QJsonObject st{{"id", QString::number(10000 + i)},
                         {"name", QString::fromLatin1(s.name)},
                         {"statusCategory", QJsonObject{{"key", QString::fromLatin1(s.category)}}}};
    task.append(st);
    if(i % 2 == 0) {
      bug.append(st);
    }
    ++i;
  }
  return QJsonDocument(QJsonArray{QJsonObject{{"id", "1"}, {"name", "Task"}, {"statuses", task}},
                                  QJsonObject{{"id", "2"}, {"name", "Bug"}, {"statuses", bug}}})
      .toJson(QJsonDocument::Compact);
}

// GET /status: the whole instance, 60 statuses of other teams.
QByteArray instanceStatusesJson() {
  QJsonArray all;
  for(int i = 0; i < 60; ++i) {
    all.append(QJsonObject{{"name", QStringLiteral("Budget Reserved %1").arg(i)}});
  }
  return QJsonDocument(all).toJson(QJsonDocument::Compact);
}

}  // namespace

TEST(JiraProvider, ParseJiraProjectStatuses_SharedAcrossIssueTypes_OnceEachWithItsKind) {
  const QVector<heap::integrations::ExternalStatus> got = heap::integrations::parseJiraProjectStatuses(ownProjectStatusesJson(), "HT");
  ASSERT_EQ(got.size(), 20);
  EXPECT_EQ(got.at(0).project, QStringLiteral("HT"));
  EXPECT_EQ(got.at(0).name, QStringLiteral("Backlog"));
  EXPECT_EQ(got.at(2).category, QStringLiteral("indeterminate"));
}

TEST(StatusMapNames, AProjectWorkflowIsRecognisedByNameAndKind) {
  using heap::integrations::StatusMap;
  for(const OwnStatus& s : kOwnStatuses) {
    const QString byKind = StatusMap::categoryColumn(QString::fromLatin1(s.category));
    EXPECT_EQ(StatusMap::column(QString::fromLatin1(s.name), {}, byKind), QString::fromLatin1(s.column)) << s.name;
  }
  // A name nobody listed falls back to its kind, never to nothing.
  EXPECT_EQ(StatusMap::column(QStringLiteral("Awaiting CAB"), {}, StatusMap::categoryColumn(QStringLiteral("indeterminate"))),
            QStringLiteral("prog"));
  EXPECT_TRUE(StatusMap::isTestingStatus(QStringLiteral("In Testing")));
  EXPECT_FALSE(StatusMap::isTestingStatus(QStringLiteral("In Review")));
}

TEST_F(JiraNetwork, StatusesAreThoseOfTheSyncedProjectsNotTheInstance) {
  FakeJira server;
  server.route(
      "POST /rest/api/3/search/jql",
      {200, R"({"isLast":true,"issues":[{"key":"HT-1","fields":{"summary":"S","project":{"key":"HT"},"status":{"name":"In Progress"}}},
                         {"key":"HT-2","fields":{"summary":"T","project":{"key":"HT"},"status":{"name":"Backlog"}}}]})"});
  server.route("GET /rest/api/3/status", {200, instanceStatusesJson()});
  server.route("GET /rest/api/3/project/HT/statuses", {200, ownProjectStatusesJson()});

  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);

  bool done = false;
  QVector<heap::integrations::ExternalStatus> got;
  QObject::connect(
      &p, &heap::integrations::IntegrationProvider::projectStatusesFetched, &p, [&](const QVector<heap::integrations::ExternalStatus>& s) {
        got = s;
        done = true;
      });
  // As AppController calls them: the ask, then the pull it waits for.
  p.fetchStatuses();
  p.pullTasks();
  ASSERT_TRUE(waitFor(done)) << "projectStatusesFetched never arrived";
  ASSERT_EQ(got.size(), 20);
  for(const auto& s : got) {
    EXPECT_EQ(s.project, QStringLiteral("HT"));
    EXPECT_FALSE(s.name.startsWith(QStringLiteral("Budget"))) << s.name.toStdString();
  }
  EXPECT_FALSE(server.seen().contains("GET /rest/api/3/status")) << "the instance-wide list is not asked for";

  // A second pull does not ask the same project again.
  bool pulled = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>&) {
    pulled = true;
  });
  p.pullTasks();
  ASSERT_TRUE(waitFor(pulled));
  QCoreApplication::processEvents();
  EXPECT_EQ(server.seen().count("GET /rest/api/3/project/HT/statuses"), 1);
}

// The rule after 2026-10-20 (a failing request re-sent every minute read as
// a brute force to a company's devops): a Jira whose project statuses, field
// list and configuration all fail is asked a bounded number of times over six
// hours of a sync every 5 minutes — each with a provider rebuilt, as
// AppController does on every settings write — and Settings opened each time.
TEST_F(JiraNetwork, AFailingJiraIsNotHammeredOverHoursOfSyncs) {
  FakeJira server;
  server.route(
      "POST /rest/api/3/search/jql",
      {200,
       R"({"isLast":true,"issues":[{"key":"HT-1","fields":{"summary":"S","project":{"key":"HT"},"status":{"name":"In Progress"}}}]})"});
  server.route("GET /rest/api/3/project/HT/statuses", {500, R"({"errorMessages":["boom"]})"});
  server.route("GET /rest/api/3/field", {500, R"({"errorMessages":["boom"]})"});
  server.route("GET /rest/api/3/configuration", {503, R"({"errorMessages":["down"]})"});

  const QDateTime start(QDate(2026, 10, 20), QTime(9, 0));
  QDateTime now = start;
  heap::integrations::JiraProvider::setClockForTesting([&now]() {
    return now;
  });
  for(int minute = 0; minute < 6 * 60; minute += 5) {
    now = start.addSecs(qint64(minute) * 60);
    heap::integrations::JiraProvider p;
    p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
    p.setDeployment(heap::integrations::JiraDeployment::Cloud);
    bool pulled = false;
    QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>&) {
      pulled = true;
    });
    p.fetchStatuses();  // Settings open
    p.pullTasks();
    ASSERT_TRUE(waitFor(pulled)) << "minute " << minute;
    // The statuses go out after the pull; let them come back.
    heap::testing::waitUntil(
        []() {
          return false;
        },
        40);
  }
  // 5, 10, 20, 40, 80, 160 minutes apart: asked at 0, 5, 15, 35, 75, 155, 315.
  const auto count = [&server](const QByteArray& key) {
    return server.seen().count(key);
  };
  EXPECT_EQ(count("POST /rest/api/3/search/jql"), 72) << "the sync itself runs every time";
  EXPECT_GE(count("GET /rest/api/3/project/HT/statuses"), 1);
  EXPECT_LE(count("GET /rest/api/3/project/HT/statuses"), 7);
  EXPECT_LE(count("GET /rest/api/3/field"), 7);
  EXPECT_LE(count("GET /rest/api/3/configuration"), 7);
  EXPECT_FALSE(server.seen().contains("GET /rest/api/3/status"));

  // A healthy site's list is read once and then not again that day, sync
  // after sync.
  server.route("GET /rest/api/3/project/HT/statuses", {200, ownProjectStatusesJson()});
  now = start.addDays(1);
  const qsizetype before = count("GET /rest/api/3/project/HT/statuses");
  for(int minute = 0; minute < 60; minute += 5) {
    now = start.addDays(1).addSecs(qint64(minute) * 60);
    heap::integrations::JiraProvider p;
    p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
    p.setDeployment(heap::integrations::JiraDeployment::Cloud);
    bool pulled = false;
    QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>&) {
      pulled = true;
    });
    p.pullTasks();
    ASSERT_TRUE(waitFor(pulled));
    heap::testing::waitUntil(
        []() {
          return false;
        },
        40);
  }
  EXPECT_EQ(count("GET /rest/api/3/project/HT/statuses") - before, 1);
}

// Settings says when the statuses were last read: a rebuilt provider (or the
// next start) does not ask a project read within the day, and Settings opened
// asks again only once the list is over 5 minutes old.
TEST_F(JiraNetwork, ProjectStatusesReadBeforeAreNotAskedAgainSoon) {
  FakeJira server;
  server.route("GET /rest/api/3/project/HT/statuses", {200, ownProjectStatusesJson()});
  const QDateTime start(QDate(2026, 10, 20), QTime(9, 0));
  QDateTime now = start;
  heap::integrations::JiraProvider::setClockForTesting([&now]() {
    return now;
  });
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  p.seedProjectStatuses({{QStringLiteral("HT"), start.addSecs(-60)}});
  p.fetchStatuses();
  heap::testing::waitUntil(
      []() {
        return false;
      },
      40);
  EXPECT_EQ(server.seen().count("GET /rest/api/3/project/HT/statuses"), 0);

  now = start.addSecs(10 * 60);
  bool done = false;
  QObject::connect(
      &p, &heap::integrations::IntegrationProvider::projectStatusesFetched, &p, [&](const QVector<heap::integrations::ExternalStatus>&) {
        done = true;
      });
  p.fetchStatuses();
  ASSERT_TRUE(waitFor(done));
  EXPECT_EQ(server.seen().count("GET /rest/api/3/project/HT/statuses"), 1);
}

TEST(JiraParse, TimeTrackingConfiguration) {
  const QJsonObject c = heap::integrations::parseJiraTimeTracking(
      R"({"timeTrackingEnabled":true,"timeTrackingConfiguration":{"workingHoursPerDay":7.5,"workingDaysPerWeek":4,"timeFormat":"pretty","defaultUnit":"minute"}})");
  EXPECT_TRUE(c.value("enabled").toBool());
  EXPECT_DOUBLE_EQ(c.value("hoursPerDay").toDouble(), 7.5);
  EXPECT_DOUBLE_EQ(c.value("daysPerWeek").toDouble(), 4.0);
  // Not said: Jira's own 8h days and 5-day weeks.
  const QJsonObject d = heap::integrations::parseJiraTimeTracking(R"({"timeTrackingEnabled":false})");
  EXPECT_FALSE(d.value("enabled").toBool());
  EXPECT_DOUBLE_EQ(d.value("hoursPerDay").toDouble(), 8.0);
  EXPECT_DOUBLE_EQ(d.value("daysPerWeek").toDouble(), 5.0);
  EXPECT_TRUE(heap::integrations::parseJiraTimeTracking("not json").isEmpty());
}

// Item 19: a search answers `timetracking` as {} on some sites; the flat
// seconds fields carry the same estimate.
TEST(JiraParse, EstimateFromTheFlatTimeFields) {
  const QByteArray json = R"({"issues":[{"key":"HT-1","fields":{"summary":"S","status":{"name":"To Do"},
      "timetracking":{},"timeoriginalestimate":36000,"timeestimate":27000,"timespent":9000}}]})";
  const auto tasks = heap::integrations::parseJiraIssues(json, QStringLiteral("https://acme.atlassian.net"), QString(), {});
  ASSERT_EQ(tasks.size(), 1);
  const QJsonObject e = tasks.first().details.value(QStringLiteral("estimate")).toObject();
  EXPECT_EQ(e.value("original").toInteger(), 36000);
  EXPECT_EQ(e.value("remaining").toInteger(), 27000);
  EXPECT_EQ(e.value("spent").toInteger(), 9000);
}

// ─── Pictures in a ticket's text (dogfood 2026-10-20) ────────────────────────

TEST(JiraProvider, JiraAdfToMarkdown_MediaSingle_BecomesAnImageOfTheFile) {
  const QByteArray adf = R"json({"type":"doc","content":[
    {"type":"paragraph","content":[{"type":"text","text":"See:"}]},
    {"type":"mediaSingle","content":[{"type":"media","attrs":{"id":"9f1c","type":"file","collection":"c","alt":"screenshot 1.png"}}]},
    {"type":"mediaSingle","content":[{"type":"media","attrs":{"id":"aa-bb","type":"file","collection":"c"}}]},
    {"type":"mediaGroup","content":[{"type":"media","attrs":{"id":"cc","type":"file","alt":"report.pdf"}}]}]})json";
  const QString md = jiraAdfToMarkdown(adf);
  EXPECT_TRUE(md.contains(QStringLiteral("![screenshot 1.png](screenshot%201.png)"))) << md.toStdString();
  EXPECT_TRUE(md.contains(QStringLiteral("![](jira-media:aa-bb)"))) << md.toStdString();
  EXPECT_TRUE(md.contains(QStringLiteral("[attachment: report.pdf]"))) << md.toStdString();
}

TEST(JiraParse, AttachmentsGoToDetails) {
  const QByteArray json = R"({"issues":[{"key":"HT-1","fields":{"summary":"S","attachment":[
      {"id":"10001","filename":"screenshot-1.png","mimeType":"image/png","size":2048,"content":"https://x/secure/attachment/10001/screenshot-1.png"},
      {"id":10002,"filename":"log.txt","mimeType":"text/plain","size":10}]}}]})";
  const auto issues = parseJiraIssues(json, "https://jira.example.com");
  ASSERT_EQ(issues.size(), 1);
  const auto files = heap::integrations::trackerAttachments(issues.at(0).details);
  ASSERT_EQ(files.size(), 2);
  EXPECT_EQ(files.at(0).id, QStringLiteral("10001"));
  EXPECT_EQ(files.at(0).name, QStringLiteral("screenshot-1.png"));
  EXPECT_EQ(files.at(0).mime, QStringLiteral("image/png"));
  EXPECT_EQ(files.at(0).size, 2048);
  EXPECT_EQ(files.at(1).id, QStringLiteral("10002")) << "a numeric id is read too";
}

TEST(TrackerImages, RewritesNamedUnnamedMissingAndLeavesTheRestAlone) {
  using heap::integrations::TrackerAttachment;
  using heap::integrations::TrackerImageState;
  const QVector<TrackerAttachment> files = {
      {QStringLiteral("1"), QStringLiteral("screenshot-1.png"), QStringLiteral("image/png"), 10},
      {QStringLiteral("2"), QStringLiteral("Screen Shot.png"), QStringLiteral("image/png"), 10},
      {QStringLiteral("3"), QStringLiteral("other.jpg"), QStringLiteral("image/jpeg"), 10},
      {QStringLiteral("4"), QStringLiteral("broken.png"), QStringLiteral("image/png"), 10},
  };
  QStringList asked;
  const auto stateOf = [&asked](const TrackerAttachment& f) {
    asked << f.id;
    TrackerImageState st;
    if(f.id == QLatin1String("1")) {
      st.localId = QStringLiteral("0123456789abcdef0123456789abcdef.png");
    } else if(f.id == QLatin1String("4")) {
      st.error = QStringLiteral("HTTP 403");
    } else {
      st.loading = true;
    }
    return st;
  };
  const QString md = QStringLiteral(
      "![screenshot-1.png](screenshot-1.png)\n"
      "![](jira-media:x)\n"
      "![Screen Shot.png](Screen%20Shot.png)\n"
      "![gone.png](gone.png)\n"
      "![broken.png](broken.png)\n"
      "![web](https://example.com/a.png) ![mine](attachments/0123456789abcdef0123456789abcdef.png)\n"
      "```\n![screenshot-1.png](screenshot-1.png)\n```");
  const QString out = heap::integrations::rewriteTrackerImages(md, files, stateOf);
  const QStringList lines = out.split(QChar('\n'));
  EXPECT_EQ(lines.at(0), QStringLiteral("![screenshot-1.png](attachments/0123456789abcdef0123456789abcdef.png)"));
  // The unnamed one takes the first picture nobody named: other.jpg.
  EXPECT_EQ(lines.at(1), QStringLiteral("![other.jpg](tracker-image:loading/other.jpg)"));
  EXPECT_EQ(lines.at(2), QStringLiteral("![Screen Shot.png](tracker-image:loading/Screen%20Shot.png)"));
  EXPECT_EQ(lines.at(3), QStringLiteral("![gone.png](tracker-image:failed/gone.png/missing)"));
  EXPECT_EQ(lines.at(4), QStringLiteral("![broken.png](tracker-image:failed/broken.png/HTTP%20403)"));
  EXPECT_EQ(lines.at(5), QStringLiteral("![web](https://example.com/a.png) ![mine](attachments/0123456789abcdef0123456789abcdef.png)"));
  EXPECT_EQ(lines.at(7), QStringLiteral("![screenshot-1.png](screenshot-1.png)")) << "code is left as written";
  EXPECT_EQ(asked.size(), 4);
}

// Dogfood 2026-10-21: a comment is drawn in pieces around the ticket's own
// pictures; any other image and anything in code stays in the text.
TEST(TrackerImages, SplitsATextAtTheTicketsPictures) {
  using heap::integrations::splitTrackerImages;
  const QString id = QStringLiteral("0123456789abcdef0123456789abcdef.png");
  const QString md = QStringLiteral("Before ![a\\[1\\].png](attachments/") + id +
                     QStringLiteral(
                         ") middle\n\n![x.png](tracker-image:loading/x.png)\n"
                         "![web](https://example.com/a.png)\n```\n![c](attachments/") +
                     id + QStringLiteral(")\n```\nend");
  const auto parts = splitTrackerImages(md);
  ASSERT_EQ(parts.size(), 5);
  EXPECT_FALSE(parts.at(0).image);
  EXPECT_EQ(parts.at(0).text, QStringLiteral("Before "));
  EXPECT_TRUE(parts.at(1).image);
  EXPECT_EQ(parts.at(1).alt, QStringLiteral("a[1].png"));
  EXPECT_EQ(parts.at(1).src, QStringLiteral("attachments/") + id);
  EXPECT_EQ(parts.at(2).text, QStringLiteral(" middle\n\n"));
  EXPECT_TRUE(parts.at(3).image);
  EXPECT_EQ(parts.at(3).src, QStringLiteral("tracker-image:loading/x.png"));
  EXPECT_FALSE(parts.at(4).image);
  EXPECT_TRUE(parts.at(4).text.contains(QStringLiteral("https://example.com/a.png")));
  EXPECT_TRUE(parts.at(4).text.contains(QStringLiteral("![c](attachments/"))) << "code is left whole";
  EXPECT_TRUE(splitTrackerImages(QStringLiteral("no pictures")).size() == 1);
}

// The whole file, never the thumbnail: the viewer shows what Jira stores.
TEST_F(JiraNetwork, FetchAttachmentTakesTheContentNotTheThumbnail) {
  const QByteArray full = QByteArray("full-") + QByteArray(64, 'x');
  FakeJira server;
  server.route("GET /rest/api/3/attachment/content/10001", {200, full});
  server.route("GET /rest/api/3/attachment/thumbnail/10001", {200, QByteArray("thumb")});
  heap::integrations::JiraProvider cloud;
  cloud.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  cloud.setDeployment(heap::integrations::JiraDeployment::Cloud);
  QByteArray got;
  bool done = false;
  QObject::connect(&cloud,
                   &heap::integrations::IntegrationProvider::attachmentFetched,
                   &cloud,
                   [&](const QString&, const QByteArray& b, const QString&) {
                     got = b;
                     done = true;
                   });
  cloud.fetchAttachment(QStringLiteral("10001"), QStringLiteral("shot.png"));
  ASSERT_TRUE(waitFor(done));
  EXPECT_EQ(got, full);
  for(const auto& r : server.requests()) {
    EXPECT_FALSE(r.path.contains("thumbnail")) << r.path.toStdString();
  }
}

TEST_F(JiraNetwork, FetchAttachmentAuthenticatesAndReturnsTheBytes) {
  const QByteArray png =
      QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
  FakeJira server;
  server.route("GET /rest/api/3/attachment/content/10001", {200, png});
  server.route("GET /rest/api/3/attachment/content/10002", {403, R"({"errorMessages":["You do not have permission"]})"});
  server.route("GET /secure/attachment/10003/screenshot-1.png", {200, png});

  heap::integrations::JiraProvider cloud;
  cloud.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  cloud.setDeployment(heap::integrations::JiraDeployment::Cloud);
  QHash<QString, QPair<QByteArray, QString>> got;
  const auto collect = [&got](const QString& id, const QByteArray& bytes, const QString& error) {
    got.insert(id, {bytes, error});
  };
  QObject::connect(&cloud, &heap::integrations::IntegrationProvider::attachmentFetched, &cloud, collect);
  cloud.fetchAttachment(QStringLiteral("10001"), QStringLiteral("screenshot-1.png"));
  cloud.fetchAttachment(QStringLiteral("10002"), QStringLiteral("secret.png"));
  ASSERT_TRUE(heap::testing::waitUntil([&got]() {
    return got.size() == 2;
  }));
  EXPECT_EQ(got.value("10001").first, png);
  EXPECT_TRUE(got.value("10001").second.isEmpty());
  EXPECT_TRUE(got.value("10002").first.isEmpty());
  EXPECT_TRUE(got.value("10002").second.contains(QStringLiteral("403"))) << got.value("10002").second.toStdString();
  // Credentials went with it, and the bytes came from here, not a redirect.
  const auto& req = server.requests().at(0);
  EXPECT_EQ(req.query, QByteArray("redirect=false"));
  EXPECT_TRUE(req.headers.value("authorization").startsWith("Basic ")) << req.headers.value("authorization").toStdString();
  for(const auto& r : server.requests()) {
    EXPECT_EQ(r.method, QByteArray("GET")) << "nothing is ever written to Jira";
  }

  // Server/DC: the file from the site, with the personal access token.
  heap::integrations::JiraProvider dc;
  dc.setConfig(server.base(), QString(), QStringLiteral("pat"), QString());
  dc.setDeployment(heap::integrations::JiraDeployment::Server);
  bool done = false;
  QByteArray bytes;
  QObject::connect(
      &dc, &heap::integrations::IntegrationProvider::attachmentFetched, &dc, [&](const QString&, const QByteArray& b, const QString&) {
        bytes = b;
        done = true;
      });
  dc.fetchAttachment(QStringLiteral("10003"), QStringLiteral("screenshot-1.png"));
  ASSERT_TRUE(waitFor(done));
  EXPECT_EQ(bytes, png);
  EXPECT_EQ(server.requests().last().headers.value("authorization"), QByteArray("Bearer pat"));
}

// ── Smart Checklist (dogfood 2026-10-22 #6) ──
// Railsware's checklist, read only: its markdown-like lines, the issue
// property it lives in, the text field a site may keep it in instead.

namespace {
namespace sc = heap::integrations::smartchecklist;

QString stateAt(const QJsonArray& a, int i) {
  return a.at(i).toObject().value(QStringLiteral("state")).toString();
}

QString textAt(const QJsonArray& a, int i) {
  return a.at(i).toObject().value(QStringLiteral("text")).toString();
}
}  // namespace

TEST(SmartChecklist, EveryPrefixIsRead) {
  const QJsonArray a =
      sc::parse(QStringLiteral("- to do\n+ done\n~ going\nx dropped\nX dropped too\n* star\n"
                               "- [ ] box open\n- [x] box done\n- [X] box shouted"));
  ASSERT_EQ(a.size(), 9);
  EXPECT_EQ(stateAt(a, 0), QStringLiteral("todo"));
  EXPECT_EQ(textAt(a, 0), QStringLiteral("to do"));
  EXPECT_EQ(stateAt(a, 1), QStringLiteral("done"));
  EXPECT_EQ(stateAt(a, 2), QStringLiteral("progress"));
  EXPECT_EQ(stateAt(a, 3), QStringLiteral("skipped"));
  EXPECT_EQ(stateAt(a, 4), QStringLiteral("skipped"));
  EXPECT_EQ(stateAt(a, 5), QStringLiteral("todo"));
  EXPECT_EQ(stateAt(a, 6), QStringLiteral("todo"));
  EXPECT_EQ(textAt(a, 6), QStringLiteral("box open"));
  EXPECT_EQ(stateAt(a, 7), QStringLiteral("done"));
  EXPECT_EQ(textAt(a, 7), QStringLiteral("box done"));
  EXPECT_EQ(stateAt(a, 8), QStringLiteral("done"));
}

TEST(SmartChecklist, HeadersAndDetails) {
  const QJsonArray a = sc::parse(QStringLiteral("# Before\n- spec\n> read it twice\n> and ask\n## After\n+ shipped\n> notes"));
  ASSERT_EQ(a.size(), 4);
  EXPECT_EQ(stateAt(a, 0), QStringLiteral("header"));
  EXPECT_EQ(textAt(a, 0), QStringLiteral("Before"));
  EXPECT_EQ(a.at(0).toObject().value(QStringLiteral("level")).toInt(), 1);
  EXPECT_EQ(a.at(1).toObject().value(QStringLiteral("details")).toString(), QStringLiteral("read it twice\nand ask"));
  EXPECT_EQ(a.at(2).toObject().value(QStringLiteral("level")).toInt(), 2);
  EXPECT_EQ(a.at(3).toObject().value(QStringLiteral("details")).toString(), QStringLiteral("notes"));
}

TEST(SmartChecklist, EmptyAndGarbage) {
  EXPECT_TRUE(sc::parse(QString()).isEmpty());
  EXPECT_TRUE(sc::parse(QStringLiteral("\n  \n\t")).isEmpty());
  // Prose, a bare marker, details with nothing above, a header with no words.
  EXPECT_TRUE(sc::parse(QStringLiteral("just words\n-\n- \n+\n> orphan details\n#\n#nospace\n{\"json\":1}")).isEmpty());
  // A details line after prose belongs to nothing.
  const QJsonArray a = sc::parse(QStringLiteral("- one\nprose\n> not one's"));
  ASSERT_EQ(a.size(), 1);
  EXPECT_FALSE(a.at(0).toObject().contains(QStringLiteral("details")));
  // An escaped marker from rich text is still one; CRLF lines too.
  const QJsonArray b = sc::parse(QStringLiteral("\\+ escaped done\r\n- crlf\r\n"));
  ASSERT_EQ(b.size(), 2);
  EXPECT_EQ(stateAt(b, 0), QStringLiteral("done"));
  EXPECT_EQ(textAt(b, 1), QStringLiteral("crlf"));
}

TEST(SmartChecklist, PropertyValueShapes) {
  // The text itself.
  EXPECT_EQ(sc::fromPropertyValue(QJsonValue(QStringLiteral("- a\n+ b"))).size(), 2);
  // Wrapped in an object.
  EXPECT_EQ(sc::fromPropertyValue(QJsonObject{{QStringLiteral("value"), QStringLiteral("- a")}}).size(), 1);
  // Item objects.
  const QJsonArray items = sc::fromPropertyValue(
      QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("one")}, {QStringLiteral("checked"), true}},
                 QJsonObject{{QStringLiteral("title"), QStringLiteral("two")}, {QStringLiteral("status"), QStringLiteral("In progress")}},
                 QJsonObject{{QStringLiteral("text"), QStringLiteral("three")},
                             {QStringLiteral("status"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Skipped")}}}},
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("G")}, {QStringLiteral("isHeader"), true}},
                 QJsonObject{{QStringLiteral("nothing"), 1}}});
  ASSERT_EQ(items.size(), 4);
  EXPECT_EQ(stateAt(items, 0), QStringLiteral("done"));
  EXPECT_EQ(stateAt(items, 1), QStringLiteral("progress"));
  EXPECT_EQ(stateAt(items, 2), QStringLiteral("skipped"));
  EXPECT_EQ(stateAt(items, 3), QStringLiteral("header"));
  // A JSON document kept as a string.
  EXPECT_EQ(sc::fromPropertyValue(QJsonValue(QStringLiteral(R"([{"name":"x","checked":false}])"))).size(), 1);
  // Nothing usable.
  EXPECT_TRUE(sc::fromPropertyValue(QJsonValue()).isEmpty());
  EXPECT_TRUE(sc::fromPropertyValue(QJsonValue(42)).isEmpty());
  EXPECT_TRUE(sc::fromPropertyValue(QJsonObject{{QStringLiteral("other"), 1}}).isEmpty());
}

TEST(SmartChecklist, FieldsAreFoundBySchemaThenName_NeverTheProgressOne) {
  const QByteArray fields = R"([
    {"id":"customfield_1","name":"Checklists","schema":{"type":"string"}},
    {"id":"customfield_2","name":"Smart Checklist Progress","schema":{"type":"string","custom":"com.railsware.smartchecklist:progress"}},
    {"id":"customfield_3","name":"Smart Checklist","schema":{"type":"string","custom":"com.railsware.smartchecklist:checklist"}},
    {"id":"customfield_4","name":"Checklist","schema":{"type":"number"}},
    {"id":"summary","name":"Summary","schema":{"type":"string","system":"summary"}}])";
  EXPECT_EQ(sc::parseJiraChecklistFields(fields), (QStringList{QStringLiteral("customfield_3"), QStringLiteral("customfield_1")}));
  EXPECT_TRUE(sc::parseJiraChecklistFields("garbage").isEmpty());
}

TEST(SmartChecklist, AnIssueCarriesThePropertyFirst_ThenTheField) {
  const QByteArray both = R"({"issues":[{"key":"LTE-1",
    "properties":{"com.railsware.SmartChecklist.checklist":"- from property\n+ done"},
    "fields":{"summary":"S","status":{"name":"To Do"},"customfield_3":"- from field"}}]})";
  const QVector<ExternalTask> a = parseJiraIssues(both, QStringLiteral("https://x"), {}, {}, {QStringLiteral("customfield_3")});
  ASSERT_EQ(a.size(), 1);
  const QJsonArray fromProp = a.at(0).details.value(QStringLiteral("smartChecklist")).toArray();
  ASSERT_EQ(fromProp.size(), 2);
  EXPECT_EQ(textAt(fromProp, 0), QStringLiteral("from property"));

  const QByteArray fieldOnly = R"({"issues":[{"key":"LTE-1",
    "fields":{"summary":"S","status":{"name":"To Do"},"customfield_3":"- from field\nx skipped"}}]})";
  const QVector<ExternalTask> b = parseJiraIssues(fieldOnly, QStringLiteral("https://x"), {}, {}, {QStringLiteral("customfield_3")});
  ASSERT_EQ(b.size(), 1);
  const QJsonArray fromField = b.at(0).details.value(QStringLiteral("smartChecklist")).toArray();
  ASSERT_EQ(fromField.size(), 2);
  EXPECT_EQ(textAt(fromField, 0), QStringLiteral("from field"));
  EXPECT_EQ(stateAt(fromField, 1), QStringLiteral("skipped"));

  // A Cloud text field answers rich text (ADF).
  const QByteArray adf = R"({"issues":[{"key":"LTE-1","fields":{"summary":"S","status":{"name":"To Do"},
    "customfield_3":{"type":"doc","version":1,"content":[{"type":"bulletList","content":[
      {"type":"listItem","content":[{"type":"paragraph","content":[{"type":"text","text":"rich item"}]}]}]}]}}}]})";
  const QVector<ExternalTask> c = parseJiraIssues(adf, QStringLiteral("https://x"), {}, {}, {QStringLiteral("customfield_3")});
  ASSERT_EQ(c.size(), 1);
  const QJsonArray rich = c.at(0).details.value(QStringLiteral("smartChecklist")).toArray();
  ASSERT_EQ(rich.size(), 1);
  EXPECT_EQ(textAt(rich, 0), QStringLiteral("rich item"));

  // Neither: no key at all.
  const QVector<ExternalTask> d = parseJiraIssues(R"({"issues":[{"key":"LTE-1","fields":{"summary":"S"}}]})", QStringLiteral("https://x"));
  ASSERT_EQ(d.size(), 1);
  EXPECT_FALSE(d.at(0).details.contains(QStringLiteral("smartChecklist")));
}

TEST_F(JiraNetwork, PullAsksForTheSmartChecklist_AndNeverWritesIt) {
  FakeJira server;
  server.route(
      "GET /rest/api/3/field",
      {200,
       R"([{"id":"customfield_3","name":"Smart Checklist","schema":{"type":"string","custom":"com.railsware.smartchecklist:checklist"}}])"});
  server.route("POST /rest/api/3/search/jql",
               {200,
                R"({"isLast":true,"issues":[
                  {"key":"LTE-1","properties":{"com.railsware.SmartChecklist.checklist":"- a\n+ b\n~ c"},
                   "fields":{"summary":"S","status":{"name":"To Do"}}},
                  {"key":"LTE-2","fields":{"summary":"T","status":{"name":"To Do"},"customfield_3":"+ only field"}}]})"});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  QVector<ExternalTask> got;
  bool fetched = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
    got = t;
    fetched = true;
  });
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return fetched;
  }));
  const QJsonObject body = QJsonDocument::fromJson(server.lastBody("POST /rest/api/3/search/jql")).object();
  EXPECT_TRUE(body.value(QStringLiteral("properties")).toArray().contains(QStringLiteral("com.railsware.SmartChecklist.checklist")))
      << server.lastBody("POST /rest/api/3/search/jql").toStdString();
  EXPECT_TRUE(body.value(QStringLiteral("fields")).toArray().contains(QStringLiteral("customfield_3")));
  ASSERT_EQ(got.size(), 2);
  EXPECT_EQ(got.at(0).details.value(QStringLiteral("smartChecklist")).toArray().size(), 3);
  EXPECT_EQ(textAt(got.at(1).details.value(QStringLiteral("smartChecklist")).toArray(), 0), QStringLiteral("only field"));
  for(const auto& r : server.requests()) {
    EXPECT_TRUE(r.method == "GET" || r.path.endsWith("/search/jql")) << "nothing is written to a checklist: " << r.key().toStdString();
  }
}

// Server/DC's POST /rest/api/2/search answers 400 "Unrecognized field
// properties" for the whole page: the sync must not send it there.
TEST_F(JiraNetwork, ServerSearchSendsNoProperties_TheChecklistFieldStillComes) {
  FakeJira server;
  server.route(
      "GET /rest/api/2/field",
      {200,
       R"([{"id":"customfield_3","name":"Smart Checklist","schema":{"type":"string","custom":"com.railsware.smartchecklist:checklist"}}])"});
  server.route("POST /rest/api/2/search",
               {200,
                R"({"startAt":0,"maxResults":100,"total":1,"issues":[
                  {"key":"TEL-1","fields":{"summary":"S","status":{"name":"To Do"},"customfield_3":"- a\n+ b"}}]})"});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Server);
  QVector<ExternalTask> got;
  bool fetched = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>& t) {
    got = t;
    fetched = true;
  });
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return fetched;
  }));
  const QJsonObject body = QJsonDocument::fromJson(server.lastBody("POST /rest/api/2/search")).object();
  EXPECT_FALSE(body.contains(QStringLiteral("properties"))) << server.lastBody("POST /rest/api/2/search").toStdString();
  EXPECT_TRUE(body.value(QStringLiteral("fields")).toArray().contains(QStringLiteral("customfield_3")));
  ASSERT_EQ(got.size(), 1);
  EXPECT_EQ(got.at(0).details.value(QStringLiteral("smartChecklist")).toArray().size(), 2);
}

TEST_F(JiraNetwork, LookUpAsksForTheSmartChecklistProperty) {
  FakeJira server;
  server.route("GET /rest/api/3/issue/LTE-7",
               {200,
                R"({"key":"LTE-7","properties":{"com.railsware.SmartChecklist.checklist":"+ one\n- two"},
                   "fields":{"summary":"S","status":{"name":"To Do"}}})"});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  QVector<ExternalTask> found;
  bool done = false;
  QObject::connect(
      &p, &heap::integrations::IntegrationProvider::issuesLookedUp, &p, [&](const QVector<ExternalTask>& f, const QStringList&) {
        found = f;
        done = true;
      });
  p.lookUpIssues({QStringLiteral("LTE-7")});
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return done;
  }));
  ASSERT_EQ(found.size(), 1);
  EXPECT_EQ(found.at(0).details.value(QStringLiteral("smartChecklist")).toArray().size(), 2);
  bool asked = false;
  for(const auto& r : server.requests()) {
    if(r.path.contains("/issue/LTE-7") && r.query.contains("properties=com.railsware.SmartChecklist.checklist")) {
      asked = true;
    }
  }
  EXPECT_TRUE(asked);
}
