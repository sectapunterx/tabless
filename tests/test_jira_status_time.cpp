// Dogfood 2026-10-19: when a Jira issue entered its status, its estimate, its
// comment count, and statuses nobody mapped. Pure parsers on canned JSON (the
// shapes checked against a live Cloud site) plus the requests against a fake
// Jira. Linked into heap_jira_tests.

#include "FakeHttpServer.h"

#include "integrations/JiraProvider.h"
#include "integrations/StatusMap.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include <gtest/gtest.h>

using heap::integrations::ExternalStatusChange;
using heap::integrations::ExternalTask;
using heap::integrations::parseJiraIssues;

namespace {

QDateTime utc(int y, int mo, int d, int h, int mi, int ms = 0) {
  return QDateTime(QDate(y, mo, d), QTime(h, mi, 0, ms), QTimeZone::utc());
}

void ensureApp() {
  if(QCoreApplication::instance() == nullptr) {
    static int argc = 1;
    static char arg0[] = "heap_jira_tests";
    static char* argv[] = {arg0, nullptr};
    new QCoreApplication(argc, argv);
  }
}

}  // namespace

TEST(JiraStatusTime, TheSearchCarriesTheStatusKindAndWhenItLastChanged) {
  const QByteArray json = R"({"issues":[{"key":"LUX-15","fields":{"summary":"S",
    "status":{"name":"Готово","statusCategory":{"key":"done"}},
    "statuscategorychangedate":"2026-07-29T21:01:50.526+0300"}}]})";
  const QVector<ExternalTask> got = parseJiraIssues(json, QStringLiteral("https://x.atlassian.net"));
  ASSERT_EQ(got.size(), 1);
  EXPECT_EQ(got.at(0).statusCategory, QStringLiteral("done"));
  EXPECT_EQ(got.at(0).statusCategoryChangedAt.toUTC(), QDateTime(QDate(2026, 7, 29), QTime(18, 1, 50, 526), QTimeZone::utc()));
}

TEST(JiraStatusTime, CloudHistoriesNewestFirst_TheNewestStatusMoveWins) {
  // Cloud's expand=changelog lists the histories newest first.
  const QByteArray json = R"({"key":"LUX-15","fields":{"status":{"name":"Review"},"created":"2026-07-25T21:13:09.000+0300"},
    "changelog":{"startAt":0,"maxResults":3,"total":3,"histories":[
      {"created":"2026-10-16T14:00:00.000+0300","items":[{"field":"status","fieldId":"status","fromString":"In Progress","toString":"Review"}]},
      {"created":"2026-10-12T10:00:00.000+0300","items":[{"field":"labels","fieldId":"labels","toString":"x"}]},
      {"created":"2026-10-01T09:00:00.000+0300","items":[{"field":"status","fieldId":"status","fromString":"To Do","toString":"In Progress"}]}]}})";
  bool ok = false;
  const ExternalStatusChange c = heap::integrations::parseJiraStatusChange(json, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(c.externalId, QStringLiteral("LUX-15"));
  EXPECT_EQ(c.from, QStringLiteral("In Progress"));
  EXPECT_EQ(c.to, QStringLiteral("Review"));
  EXPECT_EQ(c.at.toUTC(), utc(2026, 10, 16, 11, 0));
}

TEST(JiraStatusTime, ServerHistoriesOldestFirst_TheNewestStatusMoveStillWins) {
  const QByteArray json = R"({"key":"OPS-2","fields":{"status":{"name":"Done"}},
    "changelog":{"startAt":0,"maxResults":2,"total":2,"histories":[
      {"created":"2026-10-01T09:00:00.000+0000","items":[{"field":"status","fromString":"Open","toString":"In Progress"}]},
      {"created":"2026-10-15T09:00:00.000+0000","items":[{"field":"status","fromString":"In Progress","toString":"Done"}]}]}})";
  bool ok = false;
  const ExternalStatusChange c = heap::integrations::parseJiraStatusChange(json, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(c.from, QStringLiteral("In Progress"));
  EXPECT_EQ(c.at.toUTC(), utc(2026, 10, 15, 9, 0));
}

TEST(JiraStatusTime, AHistoryThatDoesNotEndInTheCurrentStatusIsNotTrusted) {
  // Moved again since, or a page cut short before the newest move.
  const QByteArray json = R"({"key":"OPS-3","fields":{"status":{"name":"Done"}},
    "changelog":{"total":1,"histories":[
      {"created":"2026-10-01T09:00:00.000+0000","items":[{"field":"status","fromString":"Open","toString":"In Progress"}]}]}})";
  bool ok = true;
  static_cast<void>(heap::integrations::parseJiraStatusChange(json, &ok));
  EXPECT_FALSE(ok);
}

TEST(JiraStatusTime, NeverMoved_EnteredItsStatusWhenCreated_UnlessThePageIsPartial) {
  const QByteArray never = R"({"key":"OPS-4","fields":{"status":{"name":"To Do"},"created":"2026-09-01T08:00:00.000+0000"},
    "changelog":{"total":1,"histories":[{"created":"2026-09-02T08:00:00.000+0000","items":[{"field":"labels","toString":"a"}]}]}})";
  bool ok = false;
  const ExternalStatusChange c = heap::integrations::parseJiraStatusChange(never, &ok);
  ASSERT_TRUE(ok);
  EXPECT_TRUE(c.from.isEmpty());
  EXPECT_EQ(c.at.toUTC(), utc(2026, 9, 1, 8, 0));

  const QByteArray partial = R"({"key":"OPS-5","fields":{"status":{"name":"To Do"},"created":"2026-09-01T08:00:00.000+0000"},
    "changelog":{"total":150,"histories":[{"created":"2026-09-02T08:00:00.000+0000","items":[{"field":"labels","toString":"a"}]}]}})";
  static_cast<void>(heap::integrations::parseJiraStatusChange(partial, &ok));
  EXPECT_FALSE(ok);
  // No changelog in the answer at all says nothing either.
  static_cast<void>(
      heap::integrations::parseJiraStatusChange(R"({"key":"OPS-6","fields":{"status":{"name":"To Do"},"created":"2026-09-01"}})", &ok));
  EXPECT_FALSE(ok);
}

TEST(JiraStatusTime, ASearchAnswerGivesOneChangePerIssueItCanTell) {
  const QByteArray json = R"({"issues":[
    {"key":"A-1","fields":{"status":{"name":"Done"}},"changelog":{"total":1,"histories":[
      {"created":"2026-10-15T09:00:00.000+0000","items":[{"field":"status","fromString":"Open","toString":"Done"}]}]}},
    {"key":"A-2","fields":{"status":{"name":"Done"}}}]})";
  const QVector<ExternalStatusChange> got = heap::integrations::parseJiraStatusChanges(json);
  ASSERT_EQ(got.size(), 1);
  EXPECT_EQ(got.at(0).externalId, QStringLiteral("A-1"));
}

TEST(JiraStatusMap, AnUnmappedStatusFollowsItsKind) {
  using heap::integrations::StatusMap;
  EXPECT_EQ(StatusMap::categoryColumn(QStringLiteral("new")), QStringLiteral("todo"));
  EXPECT_EQ(StatusMap::categoryColumn(QStringLiteral("indeterminate")), QStringLiteral("prog"));
  EXPECT_EQ(StatusMap::categoryColumn(QStringLiteral("done")), QStringLiteral("done"));
  EXPECT_TRUE(StatusMap::categoryColumn(QStringLiteral("undefined")).isEmpty());
  // Jira's own names as a Russian-speaking account sees them.
  EXPECT_EQ(StatusMap::column(QStringLiteral("В работе")), QStringLiteral("prog"));
  EXPECT_EQ(StatusMap::column(QStringLiteral("Готово")), QStringLiteral("done"));
  EXPECT_EQ(StatusMap::column(QStringLiteral("К выполнению")), QStringLiteral("todo"));
  EXPECT_EQ(StatusMap::column(QStringLiteral("На ревью")), QStringLiteral("review"));
  // An override still wins over everything.
  EXPECT_EQ(StatusMap::column(QStringLiteral("Готово"), {{QStringLiteral("готово"), QStringLiteral("review")}}), QStringLiteral("review"));
}

TEST(JiraEstimate, TimetrackingAndStoryPointsGoToDetails) {
  const QByteArray json = R"({"issues":[{"key":"E-1","fields":{"summary":"S","status":{"name":"To Do"},
    "timetracking":{"originalEstimate":"1d","remainingEstimate":"4h","timeSpent":"2h",
      "originalEstimateSeconds":28800,"remainingEstimateSeconds":14400,"timeSpentSeconds":7200},
    "customfield_10016":null,"customfield_10026":3}},
    {"key":"E-2","fields":{"summary":"S","status":{"name":"To Do"},"timetracking":{}}}]})";
  const QVector<ExternalTask> got = parseJiraIssues(json,
                                                    QStringLiteral("https://x.atlassian.net"),
                                                    QString(),
                                                    {QStringLiteral("customfield_10016"), QStringLiteral("customfield_10026")});
  ASSERT_EQ(got.size(), 2);
  const QJsonObject e = got.at(0).details.value(QStringLiteral("estimate")).toObject();
  EXPECT_EQ(e.value(QStringLiteral("original")).toInt(), 28800);
  EXPECT_EQ(e.value(QStringLiteral("remaining")).toInt(), 14400);
  EXPECT_EQ(e.value(QStringLiteral("spent")).toInt(), 7200);
  EXPECT_DOUBLE_EQ(e.value(QStringLiteral("points")).toDouble(), 3.0);
  // Nothing estimated, nothing stored.
  EXPECT_FALSE(got.at(1).details.contains(QStringLiteral("estimate")));
}

TEST(JiraEstimate, StoryPointFieldsAreFoundBySchemaThenByName) {
  const QByteArray fields = R"([
    {"id":"customfield_10026","name":"Story Points","schema":{"type":"number","custom":"com.atlassian.jira.plugin.system.customfieldtypes:float"}},
    {"id":"customfield_10016","name":"Story point estimate","schema":{"type":"number","custom":"com.pyxis.greenhopper.jira:jsw-story-points"}},
    {"id":"customfield_10099","name":"Story Points","schema":{"type":"string"}},
    {"id":"summary","name":"Summary","schema":{"type":"string","system":"summary"}}])";
  EXPECT_EQ(heap::integrations::parseJiraStoryPointsFields(fields),
            (QStringList{QStringLiteral("customfield_10016"), QStringLiteral("customfield_10026")}));
  EXPECT_TRUE(heap::integrations::parseJiraStoryPointsFields("[]").isEmpty());
}

TEST(JiraEstimate, ThePullAsksForTheEstimateAndTheStatusTime) {
  ensureApp();
  heap::testing::FakeHttpServer server;
  server.route(
      "GET /rest/api/3/field",
      {200,
       R"([{"id":"customfield_10016","name":"Story point estimate","schema":{"type":"number","custom":"com.pyxis.greenhopper.jira:jsw-story-points"}}])"});
  server.route("POST /rest/api/3/search/jql", {200, R"({"isLast":true,"issues":[]})"});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  bool done = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&](const QVector<ExternalTask>&) {
    done = true;
  });
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitFor(done));
  for(const char* field : {"timetracking", "statuscategorychangedate", "customfield_10016"}) {
    EXPECT_TRUE(server.lastBody().contains(field)) << field << " missing from " << server.lastBody().toStdString();
  }
  EXPECT_FALSE(server.lastBody().contains("changelog")) << "a pull must not inline every history";
}

TEST(JiraComments, TheTotalIsRead) {
  int total = 0;
  static_cast<void>(
      heap::integrations::parseJiraComments(R"({"total":42,"comments":[{"id":"1","body":"hi","author":{"displayName":"A"}}]})", &total));
  EXPECT_EQ(total, 42);
  static_cast<void>(heap::integrations::parseJiraComments(R"({"comments":[]})", &total));
  EXPECT_EQ(total, -1);
}

TEST(JiraStatusTimeNetwork, StatusChangesAreOneReadOnlySearchForTheBatch) {
  ensureApp();
  heap::testing::FakeHttpServer server;
  server.route("POST /rest/api/3/search/jql",
               {200, R"({"issues":[{"key":"HT-5","fields":{"status":{"name":"Done"}},"changelog":{"total":1,"histories":[
                 {"created":"2026-10-16T14:00:00.000+0300","items":[{"field":"status","fromString":"In Progress","toString":"Done"}]}]}}]})"});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  bool done = false;
  QVector<ExternalStatusChange> got;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::statusChangesFetched, &p, [&](const QVector<ExternalStatusChange>& c) {
    got = c;
    done = true;
  });
  p.fetchStatusChanges({QStringLiteral("HT-5"), QStringLiteral("HT-6")});
  ASSERT_TRUE(heap::testing::waitFor(done));
  ASSERT_EQ(server.seen().size(), 1);
  EXPECT_EQ(server.seen().at(0), QByteArray("POST /rest/api/3/search/jql"));
  const QJsonObject body = QJsonDocument::fromJson(server.lastBody()).object();
  EXPECT_EQ(body.value(QStringLiteral("jql")).toString(), QStringLiteral(R"(key in ("HT-5", "HT-6"))"));
  EXPECT_EQ(body.value(QStringLiteral("expand")).toString(), QStringLiteral("changelog"));
  ASSERT_EQ(got.size(), 1);
  EXPECT_EQ(got.at(0).externalId, QStringLiteral("HT-5"));
  EXPECT_EQ(got.at(0).to, QStringLiteral("Done"));
}

TEST(JiraStatusTimeNetwork, ServerStatusChangesGoThroughTheV2Search) {
  ensureApp();
  heap::testing::FakeHttpServer server;
  server.route("POST /rest/api/2/search", {200, R"({"issues":[]})"});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QString(), QStringLiteral("pat"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Server);
  bool done = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::statusChangesFetched, &p, [&](const QVector<ExternalStatusChange>&) {
    done = true;
  });
  p.fetchStatusChanges({QStringLiteral("OPS-1")});
  ASSERT_TRUE(heap::testing::waitFor(done));
  ASSERT_EQ(server.seen().size(), 1);
  EXPECT_EQ(server.seen().at(0), QByteArray("POST /rest/api/2/search"));
  EXPECT_TRUE(QJsonDocument::fromJson(server.lastBody()).object().value(QStringLiteral("expand")).isArray());
}

TEST(JiraStatusTimeNetwork, APushToAnUnmappedDoneStatusFindsItByItsKind) {
  ensureApp();
  heap::testing::FakeHttpServer server;
  server.route("GET /rest/api/3/issue/HT-5/transitions",
               {200, R"({"transitions":[{"id":"11","to":{"name":"Делаем","statusCategory":{"key":"indeterminate"}}},
                                      {"id":"31","to":{"name":"Сделано","statusCategory":{"key":"done"}}}]})"});
  server.route("POST /rest/api/3/issue/HT-5/transitions", {204, ""});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  bool done = false;
  bool ok = false;
  QString remote;
  QObject::connect(&p,
                   &heap::integrations::IntegrationProvider::taskPushed,
                   &p,
                   [&](const QString&, const QString&, bool o, const QString&, const QString& r) {
                     ok = o;
                     remote = r;
                     done = true;
                   });
  p.pushStatusChange(QStringLiteral("HT-5"), QStringLiteral("done"), QString());
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_TRUE(ok);
  EXPECT_EQ(remote, QStringLiteral("Сделано"));
  EXPECT_TRUE(server.lastBody().contains("\"31\"")) << server.lastBody().toStdString();
}

// Review 2026-10-19 (B): the history keeps the name a status had when the
// issue moved; a status renamed since failed the name compare on every sync.
// Ids do not change with a rename.
TEST(JiraStatusTime, ARenamedStatusIsMatchedByItsId) {
  const QByteArray json = R"({"key":"REN-1","fields":{"status":{"name":"In Review","id":"10004"},"created":"2026-07-25T21:13:09.000+0300"},
    "changelog":{"total":1,"histories":[
      {"created":"2026-10-16T14:00:00.000+0300","items":[{"field":"status","fieldId":"status","from":"3","fromString":"In Progress","to":"10004","toString":"Code Review"}]}]}})";
  bool ok = false;
  const ExternalStatusChange c = heap::integrations::parseJiraStatusChange(json, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(c.to, QStringLiteral("In Review")) << "the status as a pull names it";
  EXPECT_EQ(c.at.toUTC(), utc(2026, 10, 16, 11, 0));
  // Moved on since (another id): not this status, whatever the names say.
  const QByteArray moved = R"({"key":"REN-2","fields":{"status":{"name":"In Review","id":"10004"}},
    "changelog":{"total":1,"histories":[
      {"created":"2026-10-16T14:00:00.000+0300","items":[{"field":"status","from":"3","fromString":"x","to":"10010","toString":"In Review"}]}]}})";
  static_cast<void>(heap::integrations::parseJiraStatusChange(moved, &ok));
  EXPECT_FALSE(ok);
}

// (D): a drop on Done picked the first transition of the Done kind, which
// could be "Won't Do". A write to Jira is not a guess.
TEST(JiraTransitions, AnUnmappedKindIsPickedOnlyWhenItIsTheOnlyOne) {
  using heap::integrations::pickJiraTransition;
  const auto t = [](const char* id, const char* name, const char* kind) {
    return QJsonObject{{QStringLiteral("id"), QString::fromLatin1(id)},
                       {QStringLiteral("to"),
                        QJsonObject{{QStringLiteral("name"), QString::fromUtf8(name)},
                                    {QStringLiteral("statusCategory"), QJsonObject{{QStringLiteral("key"), QString::fromLatin1(kind)}}}}}};
  };
  const QJsonArray several{t("11", "Делаем", "indeterminate"), t("41", "Won't Do", "done"), t("51", "Duplicate", "done")};
  QString target;
  EXPECT_TRUE(pickJiraTransition(several, QStringLiteral("done"), {}, &target).isEmpty());
  // The user's mapping settles it.
  EXPECT_EQ(pickJiraTransition(several, QStringLiteral("done"), {{QStringLiteral("duplicate"), QStringLiteral("done")}}, &target),
            QStringLiteral("51"));
  EXPECT_EQ(target, QStringLiteral("Duplicate"));
  // A well-known name is not a guess either.
  const QJsonArray named{t("41", "Won't Do", "done"), t("31", "Done", "done")};
  EXPECT_EQ(pickJiraTransition(named, QStringLiteral("done"), {}, &target), QStringLiteral("31"));
  // One of the kind: that one.
  const QJsonArray one{t("11", "Делаем", "indeterminate"), t("31", "Сделано", "done")};
  EXPECT_EQ(pickJiraTransition(one, QStringLiteral("done"), {}, &target), QStringLiteral("31"));
  EXPECT_EQ(pickJiraTransition(one, QStringLiteral("prog"), {}, &target), QStringLiteral("11"));
  // Two well-known names for Done ("Closed", "Done") are two targets: no
  // guess, nothing written (audit E4-1) — unless the user mapped one of them.
  const QJsonArray twoNamed{t("51", "Closed", "done"), t("52", "Done", "done")};
  target.clear();
  EXPECT_TRUE(pickJiraTransition(twoNamed, QStringLiteral("done"), {}, &target).isEmpty());
  EXPECT_TRUE(target.isEmpty());
  EXPECT_EQ(pickJiraTransition(twoNamed, QStringLiteral("done"), {{QStringLiteral("Done"), QStringLiteral("done")}}, &target),
            QStringLiteral("52"));
  EXPECT_EQ(target, QStringLiteral("Done"));
  // A user override pointing a well-known name elsewhere takes it out of Done.
  EXPECT_EQ(pickJiraTransition(twoNamed, QStringLiteral("done"), {{QStringLiteral("closed"), QStringLiteral("todo")}}, &target),
            QStringLiteral("52"));
  // Two transitions to the same status are one target.
  const QJsonArray sameTarget{t("61", "Done", "done"), t("62", "Done", "done")};
  EXPECT_EQ(pickJiraTransition(sameTarget, QStringLiteral("done"), {}, &target), QStringLiteral("61"));
}

TEST(JiraStatusTimeNetwork, AnAmbiguousDropWritesNothing) {
  ensureApp();
  heap::testing::FakeHttpServer server;
  server.route("GET /rest/api/3/issue/HT-7/transitions",
               {200, R"({"transitions":[{"id":"41","to":{"name":"Won't Do","statusCategory":{"key":"done"}}},
                                      {"id":"51","to":{"name":"Сделано","statusCategory":{"key":"done"}}}]})"});
  server.route("POST /rest/api/3/issue/HT-7/transitions", {204, ""});
  heap::integrations::JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(heap::integrations::JiraDeployment::Cloud);
  bool done = false;
  bool ok = true;
  QObject::connect(&p,
                   &heap::integrations::IntegrationProvider::taskPushed,
                   &p,
                   [&](const QString&, const QString&, bool o, const QString&, const QString&) {
                     ok = o;
                     done = true;
                   });
  p.pushStatusChange(QStringLiteral("HT-7"), QStringLiteral("done"), QString());
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_FALSE(ok);
  for(const QByteArray& r : server.seen()) {
    EXPECT_FALSE(r.startsWith("POST")) << r.toStdString();
  }
}
