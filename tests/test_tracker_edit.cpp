// Editing a ticket's content from Tabless (owner item 16, 2026-10-23): the
// description, the title, the due date, the assignee and the Smart
// Checklist, written to Jira only where the user switched content writes on,
// only after a fresh read shows Jira still holds what the card last saw, and
// shown on the card at once with a rollback when Jira does not take it.
// Tracker traffic goes to a local FakeHttpServer; "nothing was written" is
// asserted on what that server received.

#include "AppController.h"
#include "FakeHttpServer.h"
#include "Models.h"

#include "integrations/JiraProvider.h"
#include "integrations/SmartChecklist.h"
#include "net/FeatureGate.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

namespace trackeredit {

using heap::testing::FakeHttpServer;

const QByteArray kGet = "GET /rest/api/3/issue/HT-5";
const QByteArray kPut = "PUT /rest/api/3/issue/HT-5";
const QByteArray kPropPut = "PUT /rest/api/3/issue/HT-5/properties/com.railsware.SmartChecklist.checklist";
const QString kCard = QStringLiteral("jira-HT-5");

void settle(int ms = 400) {
  heap::testing::waitUntil(
      []() {
        return false;
      },
      ms);
}

QByteArray issueJson(const QString& summary,
                     const QString& due = QString(),
                     const QString& assignee = QStringLiteral("Ann"),
                     const QString& checklist = QString()) {
  QJsonObject fields{
      {QStringLiteral("summary"), summary},
      {QStringLiteral("status"), QJsonObject{{QStringLiteral("name"), QStringLiteral("To Do")}}},
      {QStringLiteral("description"),
       QJsonObject{{QStringLiteral("type"), QStringLiteral("doc")},
                   {QStringLiteral("version"), 1},
                   {QStringLiteral("content"),
                    QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("paragraph")},
                                           {QStringLiteral("content"),
                                            QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                                   {QStringLiteral("text"), QStringLiteral("Old text")}}}}}}}}},
      {QStringLiteral("duedate"), due.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(due)},
      {QStringLiteral("assignee"),
       assignee.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QJsonObject{{QStringLiteral("displayName"), assignee}})}};
  QJsonObject issue{{QStringLiteral("key"), QStringLiteral("HT-5")}, {QStringLiteral("fields"), fields}};
  if(!checklist.isNull()) {
    issue.insert(QStringLiteral("properties"), QJsonObject{{QStringLiteral("com.railsware.SmartChecklist.checklist"), checklist}});
  }
  return QJsonDocument(issue).toJson(QJsonDocument::Compact);
}

class TrackerEdit : public ::testing::Test {
 protected:
  void SetUp() override {
    heap::net::FeatureGate::instance().resetForTests();
    heap::integrations::JiraProvider::resetSiteCachesForTesting();
    app_ = std::make_unique<::AppController>();
    app_->tasks()->reset({});
    app_->setLanguage(QStringLiteral("en"));
    app_->setAppSettingsJson(QStringLiteral("{}"));
  }

  void TearDown() override {
    app_->setAppSettingsJson(QStringLiteral("{}"));
    app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
    app_.reset();
    heap::net::FeatureGate::instance().resetForTests();
  }

  void connectJira(FakeHttpServer& server, bool writeContent, const QByteArray& issue = issueJson(QStringLiteral("five"))) {
    app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("tok"));
    server.route("GET /rest/api/2/serverInfo", {.status = 200, .body = R"({"deploymentType":"Cloud"})"});
    server.route(kGet, {.status = 200, .body = issue});
    server.route(kPut, {.status = 204, .body = ""});
    server.route(kPropPut, {.status = 200, .body = ""});
    QJsonObject cfg{{QStringLiteral("connected"), true},
                    {QStringLiteral("baseUrl"), server.base()},
                    {QStringLiteral("email"), QStringLiteral("me@example.com")},
                    {QStringLiteral("writeContent"), writeContent}};
    QJsonObject settings = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object();
    QJsonObject integrations = settings.value(QStringLiteral("integrations")).toObject();
    integrations.insert(QStringLiteral("jira"), cfg);
    settings.insert(QStringLiteral("integrations"), integrations);
    app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  }

  // The card as the last pull left it.
  void addCard(const QJsonArray& checklist = {}) {
    Task t;
    t.id = kCard;
    t.title = QStringLiteral("five");
    t.desc = QStringLiteral("Old text");
    t.status = QStringLiteral("todo");
    t.assignee = QStringLiteral("Ann");
    t.externalId = QStringLiteral("HT-5");
    t.externalProvider = QStringLiteral("jira");
    t.externalUrl = QStringLiteral("https://acme.atlassian.net/browse/HT-5");
    t.externalMeta.project = QStringLiteral("HT");
    t.externalMeta.status = QStringLiteral("To Do");
    t.externalMeta.column = QStringLiteral("todo");
    t.externalMeta.title = t.title;
    t.externalMeta.body = t.desc;
    if(!checklist.isEmpty()) {
      t.externalMeta.details.insert(QStringLiteral("smartChecklist"), checklist);
    }
    app_->tasks()->reset({t});
  }

  const Task& card() const {
    return app_->tasks()->items().at(app_->tasks()->indexOfId(kCard));
  }

  static int count(const FakeHttpServer& server, const QByteArray& key) {
    return static_cast<int>(server.seen().count(key));
  }

  // Every edit there is, once.
  void editEverything() {
    app_->editTrackerField(kCard, QStringLiteral("title"), QStringLiteral("new title"));
    app_->editTrackerField(kCard, QStringLiteral("due"), QStringLiteral("2026-11-02"));
    app_->editTrackerField(kCard,
                           QStringLiteral("assignee"),
                           QVariantMap{{QStringLiteral("id"), QStringLiteral("acc-1")}, {QStringLiteral("name"), QStringLiteral("Bob")}});
    app_->editTrackerDescription(kCard, QStringLiteral("New text"));
    app_->setTrackerChecklistItemStatus(kCard, 0, QStringLiteral("Done"));
    app_->setTrackerChecklistItemText(kCard, 0, QStringLiteral("renamed"));
    app_->addTrackerChecklistItem(kCard, QStringLiteral("more"));
    app_->searchTrackerAssignees(kCard, QStringLiteral("bo"));
  }

  static QJsonArray oneItem() {
    return QJsonArray{QJsonObject{{QStringLiteral("text"), QStringLiteral("first")}, {QStringLiteral("state"), QStringLiteral("todo")}}};
  }

  std::unique_ptr<::AppController> app_;
};

// ── off by default, and off means nothing at all goes out ──

TEST_F(TrackerEdit, OffByDefaultNoRequestOfAnyKind) {
  EXPECT_FALSE(app_->trackerContentWriteEnabled(QStringLiteral("jira")));
  EXPECT_TRUE(app_->trackerCanEditContent(QStringLiteral("jira")));
  FakeHttpServer server;
  connectJira(server, /*writeContent=*/false);
  addCard(oneItem());
  EXPECT_FALSE(app_->canEditTrackerContent(kCard));
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);
  QSignalSpy found(app_.get(), &::AppController::trackerAssigneesFound);

  editEverything();
  settle();

  for(const auto& r : server.requests()) {
    EXPECT_FALSE(r.path.contains("/issue/") || r.path.contains("/user/") || r.method == "PUT")
        << "with content writes off, Tabless asked: " << r.key().toStdString();
  }
  EXPECT_EQ(count(server, kGet), 0);
  EXPECT_EQ(count(server, kPut), 0);
  EXPECT_EQ(count(server, kPropPut), 0);
  ASSERT_EQ(finished.count(), 7);
  for(const auto& args : finished) {
    EXPECT_EQ(args.at(2).toString(), QStringLiteral("off")) << args.at(1).toString().toStdString();
  }
  ASSERT_EQ(found.count(), 1);
  EXPECT_FALSE(found.at(0).at(3).toString().isEmpty());
  // The card shows nothing that was not written.
  EXPECT_EQ(card().title, QStringLiteral("five"));
  EXPECT_EQ(card().assignee, QStringLiteral("Ann"));
  EXPECT_EQ(card().desc, QStringLiteral("Old text"));
}

TEST_F(TrackerEdit, TheSwitchIsPerTrackerAndOnlyWhereItCanWrite) {
  app_->setTrackerContentWriteEnabled(QStringLiteral("gitlab"), true);
  EXPECT_FALSE(app_->trackerContentWriteEnabled(QStringLiteral("gitlab")));
  app_->setTrackerContentWriteEnabled(QStringLiteral("jira"), true);
  EXPECT_TRUE(app_->trackerContentWriteEnabled(QStringLiteral("jira")));
  const QJsonObject jira = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8())
                               .object()
                               .value(QStringLiteral("integrations"))
                               .toObject()
                               .value(QStringLiteral("jira"))
                               .toObject();
  EXPECT_TRUE(jira.value(QStringLiteral("writeContent")).toBool());
  // The status switch is another one.
  EXPECT_FALSE(app_->trackerWriteEnabled(QStringLiteral("jira")));
  app_->setTrackerContentWriteEnabled(QStringLiteral("jira"), false);
  EXPECT_FALSE(app_->trackerContentWriteEnabled(QStringLiteral("jira")));
}

TEST_F(TrackerEdit, AnotherTrackerAnswersUnsupportedNothingSent) {
  FakeHttpServer server;
  connectJira(server, true);
  addCard();
  Task t = card();
  t.id = QStringLiteral("gitlab-7");
  t.externalProvider = QStringLiteral("gitlab");
  t.externalId = QStringLiteral("7");
  QVector<Task> all = app_->tasks()->items();
  all.append(t);
  app_->tasks()->reset(all);
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);
  app_->editTrackerField(QStringLiteral("gitlab-7"), QStringLiteral("title"), QStringLiteral("x"));
  ASSERT_EQ(finished.count(), 1);
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("unsupported"));
  EXPECT_FALSE(app_->canEditTrackerContent(QStringLiteral("gitlab-7")));
  settle(200);
  EXPECT_EQ(count(server, kPut), 0);
}

// ── on: read, compare, write ──

TEST_F(TrackerEdit, OnTitleIsReadThenWrittenAndBecomesTheBase) {
  FakeHttpServer server;
  connectJira(server, true);
  addCard();
  EXPECT_TRUE(app_->canEditTrackerContent(kCard));
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);

  app_->editTrackerField(kCard, QStringLiteral("title"), QStringLiteral("new title"));
  EXPECT_EQ(card().title, QStringLiteral("new title")) << "the card did not show the edit at once";
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 1;
  }));
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("ok"));
  const auto& seen = server.seen();
  ASSERT_EQ(count(server, kPut), 1);
  EXPECT_LT(seen.indexOf(kGet), seen.indexOf(kPut)) << "written before it was read";
  EXPECT_EQ(QJsonDocument::fromJson(server.lastRequest(kPut).body).object(),
            QJsonObject({{QStringLiteral("fields"), QJsonObject{{QStringLiteral("summary"), QStringLiteral("new title")}}}}));
  EXPECT_EQ(card().title, QStringLiteral("new title"));
  EXPECT_EQ(card().externalMeta.title, QStringLiteral("new title")) << "the next pull would read the edit as a change there";
}

TEST_F(TrackerEdit, OnDueAssigneeAndDescriptionPayloads) {
  FakeHttpServer server;
  connectJira(server, true);
  addCard();
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);

  app_->editTrackerField(kCard, QStringLiteral("due"), QStringLiteral("2026-11-02"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 1;
  }));
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("ok"));
  EXPECT_EQ(QJsonDocument::fromJson(server.lastRequest(kPut).body)
                .object()
                .value(QStringLiteral("fields"))
                .toObject()
                .value(QStringLiteral("duedate"))
                .toString(),
            QStringLiteral("2026-11-02"));
  EXPECT_EQ(card().externalMeta.dueAt.date(), QDate(2026, 11, 2));
  EXPECT_EQ(card().dueAt.date(), QDate(2026, 11, 2));

  app_->editTrackerField(kCard,
                         QStringLiteral("assignee"),
                         QVariantMap{{QStringLiteral("id"), QStringLiteral("acc-1")}, {QStringLiteral("name"), QStringLiteral("Bob")}});
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 2;
  }));
  EXPECT_EQ(finished.at(1).at(2).toString(), QStringLiteral("ok"));
  EXPECT_EQ(QJsonDocument::fromJson(server.lastRequest(kPut).body)
                .object()
                .value(QStringLiteral("fields"))
                .toObject()
                .value(QStringLiteral("assignee"))
                .toObject(),
            QJsonObject({{QStringLiteral("accountId"), QStringLiteral("acc-1")}}));
  EXPECT_EQ(card().assignee, QStringLiteral("Bob"));

  app_->editTrackerDescription(kCard, QStringLiteral("Old text\n\nAnd **more**"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 3;
  }));
  EXPECT_EQ(finished.at(2).at(2).toString(), QStringLiteral("ok")) << finished.at(2).at(3).toString().toStdString();
  const QJsonObject doc = QJsonDocument::fromJson(server.lastRequest(kPut).body)
                              .object()
                              .value(QStringLiteral("fields"))
                              .toObject()
                              .value(QStringLiteral("description"))
                              .toObject();
  EXPECT_EQ(doc.value(QStringLiteral("type")).toString(), QStringLiteral("doc")) << "Cloud takes ADF";
  EXPECT_EQ(doc.value(QStringLiteral("content")).toArray().size(), 2);
  EXPECT_EQ(card().externalMeta.body, QStringLiteral("Old text\n\nAnd **more**"));
}

TEST_F(TrackerEdit, ConflictNothingOverwrittenBothVersionsGivenThenOverwrite) {
  FakeHttpServer server;
  // Somebody renamed it in Jira since the last pull.
  connectJira(server, true, issueJson(QStringLiteral("renamed in Jira")));
  addCard();
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);

  app_->editTrackerField(kCard, QStringLiteral("title"), QStringLiteral("mine"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 1;
  }));
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("conflict"));
  const QVariantMap detail = finished.at(0).at(4).toMap();
  EXPECT_EQ(detail.value(QStringLiteral("mine")).toString(), QStringLiteral("mine"));
  EXPECT_EQ(detail.value(QStringLiteral("theirs")).toString(), QStringLiteral("renamed in Jira"));
  EXPECT_EQ(count(server, kPut), 0) << "a value changed in Jira was overwritten";
  EXPECT_EQ(card().title, QStringLiteral("five")) << "the card kept a value Jira never took";

  // The user saw both and chose theirs to be overwritten.
  app_->overwriteTrackerEdit(kCard, QStringLiteral("title"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 2;
  }));
  EXPECT_EQ(finished.at(1).at(2).toString(), QStringLiteral("ok"));
  EXPECT_EQ(count(server, kPut), 1);
  EXPECT_EQ(card().title, QStringLiteral("mine"));
}

TEST_F(TrackerEdit, FailedWriteRollsBackAndIsNotHammered) {
  FakeHttpServer server;
  connectJira(server, true);
  server.route(kPut, {.status = 500, .body = R"({"errorMessages":["boom"]})"});
  addCard();
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);

  app_->editTrackerField(kCard, QStringLiteral("title"), QStringLiteral("new title"));
  EXPECT_EQ(card().title, QStringLiteral("new title"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 1;
  }));
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("error"));
  EXPECT_FALSE(finished.at(0).at(3).toString().isEmpty()) << "a failed write said nothing";
  EXPECT_EQ(card().title, QStringLiteral("five")) << "the card kept a value Jira refused";
  EXPECT_EQ(card().externalMeta.title, QStringLiteral("five"));
  EXPECT_EQ(count(server, kPut), 1);

  // Again at once: the retry rule holds it, nothing goes out.
  app_->editTrackerField(kCard, QStringLiteral("title"), QStringLiteral("new title"));
  settle(300);
  ASSERT_EQ(finished.count(), 2);
  EXPECT_NE(finished.at(1).at(2).toString(), QStringLiteral("ok"));
  EXPECT_EQ(count(server, kPut), 1) << "a failing write was sent again at once";
  EXPECT_EQ(count(server, kGet), 1);
  EXPECT_EQ(card().title, QStringLiteral("five"));
}

TEST_F(TrackerEdit, ChecklistStateGoesToTheIssuePropertyUnknownLinesKept) {
  FakeHttpServer server;
  const QString list = QStringLiteral("# Steps\n- first\n> why it matters\nsomething else\n+ second");
  connectJira(server, true, issueJson(QStringLiteral("five"), QString(), QStringLiteral("Ann"), list));
  const QJsonArray items{
      QJsonObject{{QStringLiteral("text"), QStringLiteral("Steps")},
                  {QStringLiteral("state"), QStringLiteral("header")},
                  {QStringLiteral("level"), 1}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("first")},
                  {QStringLiteral("state"), QStringLiteral("todo")},
                  {QStringLiteral("details"), QStringLiteral("why it matters")}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("second")}, {QStringLiteral("state"), QStringLiteral("done")}}};
  addCard(items);
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);

  app_->setTrackerChecklistItemStatus(kCard, 1, QStringLiteral("Done"));
  EXPECT_EQ(card()
                .externalMeta.details.value(QStringLiteral("smartChecklist"))
                .toArray()
                .at(1)
                .toObject()
                .value(QStringLiteral("state"))
                .toString(),
            QStringLiteral("done"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 1;
  }));
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("ok")) << finished.at(0).at(3).toString().toStdString();
  ASSERT_EQ(count(server, kPropPut), 1);
  EXPECT_EQ(count(server, kPut), 0);
  // A bare JSON string: the property's own shape.
  EXPECT_EQ(server.lastRequest(kPropPut).body, QByteArray(R"("# Steps\n+ first\n> why it matters\nsomething else\n+ second")"));

  // A header has no status: refused before anything is asked.
  const int gets = count(server, kGet);
  app_->setTrackerChecklistItemStatus(kCard, 0, QStringLiteral("Done"));
  ASSERT_EQ(finished.count(), 2);
  EXPECT_EQ(finished.at(1).at(2).toString(), QStringLiteral("refused"));
  EXPECT_EQ(count(server, kGet), gets);
}

TEST_F(TrackerEdit, ChecklistChangedInJiraIsAConflictTheCardTakesJirasList) {
  FakeHttpServer server;
  connectJira(
      server, true, issueJson(QStringLiteral("five"), QString(), QStringLiteral("Ann"), QStringLiteral("- first\n- added in Jira")));
  addCard(oneItem());
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);

  app_->setTrackerChecklistItemStatus(kCard, 0, QStringLiteral("Done"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 1;
  }));
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("conflict"));
  EXPECT_EQ(count(server, kPropPut), 0);
  EXPECT_EQ(finished.at(0).at(4).toMap().value(QStringLiteral("theirs")).toList().size(), 2);
  EXPECT_EQ(card().externalMeta.details.value(QStringLiteral("smartChecklist")).toArray().size(), 2);
}

TEST_F(TrackerEdit, OutOfScopeCardIsReadOnly) {
  FakeHttpServer server;
  connectJira(server, true);
  addCard();
  Task t = card();
  t.externalMeta.outOfScope = true;
  app_->tasks()->upsert(t);
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);
  app_->editTrackerField(kCard, QStringLiteral("title"), QStringLiteral("x"));
  ASSERT_EQ(finished.count(), 1);
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("readOnly"));
  settle(200);
  EXPECT_EQ(count(server, kGet), 0);
}

TEST_F(TrackerEdit, AssigneeSearchAskedOnceAMinutePerQuery) {
  FakeHttpServer server;
  connectJira(server, true);
  server.route(
      "GET /rest/api/3/user/assignable/search",
      {.status = 200,
       .body = R"([{"accountId":"acc-1","displayName":"Bob","active":true},{"accountId":"acc-2","displayName":"Gone","active":false}])"});
  addCard();
  QSignalSpy found(app_.get(), &::AppController::trackerAssigneesFound);
  app_->searchTrackerAssignees(kCard, QStringLiteral("bo"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return found.count() == 1;
  }));
  const QVariantList users = found.at(0).at(2).toList();
  ASSERT_EQ(users.size(), 1);
  EXPECT_EQ(users.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("acc-1"));
  const auto req = server.lastRequest("GET /rest/api/3/user/assignable/search");
  EXPECT_TRUE(req.query.contains("issueKey=HT-5"));
  EXPECT_TRUE(req.query.contains("query=bo"));

  app_->searchTrackerAssignees(kCard, QStringLiteral("bo"));
  ASSERT_EQ(found.count(), 2);
  EXPECT_EQ(found.at(1).at(2).toList().size(), 1);
  EXPECT_EQ(count(server, "GET /rest/api/3/user/assignable/search"), 1) << "the same search went out twice in a minute";
}

TEST_F(TrackerEdit, ChecklistCustomStatusIsWrittenTheWaySmartChecklistWritesIt) {
  FakeHttpServer server;
  // The project's lists already use "IN QA" (unchecked) and "PASSED" (checked).
  const QString list = QStringLiteral("- [IN QA] first\n+ [PASSED] second\n- third");
  connectJira(server, true, issueJson(QStringLiteral("five"), QString(), QStringLiteral("Ann"), list));
  addCard(heap::integrations::smartchecklist::parse(list));
  const QVariantList statuses = app_->smartChecklistStatuses(kCard);
  QStringList names;
  for(const QVariant& v : statuses) {
    names.append(v.toMap().value(QStringLiteral("name")).toString());
  }
  EXPECT_TRUE(names.contains(QStringLiteral("IN QA")));
  EXPECT_TRUE(names.contains(QStringLiteral("PASSED")));
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);

  // A checked custom status: "+ [NAME] item"; the card shows it at once.
  app_->setTrackerChecklistItemStatus(kCard, 2, QStringLiteral("passed"));
  const QJsonObject shown = card().externalMeta.details.value(QStringLiteral("smartChecklist")).toArray().at(2).toObject();
  EXPECT_EQ(shown.value(QStringLiteral("statusName")).toString(), QStringLiteral("PASSED"))
      << "the known name was not used as Jira spells it";
  EXPECT_EQ(shown.value(QStringLiteral("category")).toString(), QStringLiteral("done"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 1;
  }));
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("ok")) << finished.at(0).at(3).toString().toStdString();
  EXPECT_EQ(server.lastRequest(kPropPut).body, QByteArray(R"("- [IN QA] first\n+ [PASSED] second\n+ [PASSED] third")"));

  // Its text edited: the status stays in front of it.
  server.route(kGet,
               {.status = 200,
                .body = issueJson(QStringLiteral("five"),
                                  QString(),
                                  QStringLiteral("Ann"),
                                  QStringLiteral("- [IN QA] first\n+ [PASSED] second\n+ [PASSED] third"))});
  app_->setTrackerChecklistItemText(kCard, 0, QStringLiteral("first, renamed"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 2;
  }));
  EXPECT_EQ(finished.at(1).at(2).toString(), QStringLiteral("ok")) << finished.at(1).at(3).toString().toStdString();
  EXPECT_EQ(server.lastRequest(kPropPut).body, QByteArray(R"("- [IN QA] first, renamed\n+ [PASSED] second\n+ [PASSED] third")"));

  // A built-in again: the plain marker, the custom name gone.
  server.route(kGet,
               {.status = 200,
                .body = issueJson(QStringLiteral("five"),
                                  QString(),
                                  QStringLiteral("Ann"),
                                  QStringLiteral("- [IN QA] first, renamed\n+ [PASSED] second\n+ [PASSED] third"))});
  app_->setTrackerChecklistItemStatus(kCard, 0, QStringLiteral("In Progress"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 3;
  }));
  EXPECT_EQ(finished.at(2).at(2).toString(), QStringLiteral("ok")) << finished.at(2).at(3).toString().toStdString();
  EXPECT_EQ(server.lastRequest(kPropPut).body, QByteArray(R"("~ first, renamed\n+ [PASSED] second\n+ [PASSED] third")"));
  EXPECT_EQ(card()
                .externalMeta.details.value(QStringLiteral("smartChecklist"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("statusName"))
                .toString(),
            QStringLiteral("In Progress"));

  // Not a name a status can have: refused before anything is asked.
  const int gets = count(server, kGet);
  app_->setTrackerChecklistItemStatus(kCard, 1, QStringLiteral("[x]"));
  ASSERT_EQ(finished.count(), 4);
  EXPECT_EQ(finished.at(3).at(2).toString(), QStringLiteral("refused"));
  EXPECT_EQ(count(server, kGet), gets);
}

// ── a tracker switched off (owner item 7) ──

TEST_F(TrackerEdit, DisabledTrackerMakesNoRequestAtAllKeepsTokenAndCards) {
  FakeHttpServer server;
  connectJira(server, /*writeContent=*/true);
  server.route("POST /rest/api/3/search/jql", {.status = 200, .body = R"({"issues":[]})"});
  server.route("GET /rest/api/3/user/assignable/search", {.status = 200, .body = "[]"});
  addCard(oneItem());
  EXPECT_TRUE(app_->trackerEnabled(QStringLiteral("jira")));
  EXPECT_TRUE(app_->disabledTrackers().isEmpty());
  QSignalSpy changed(app_.get(), &::AppController::disabledTrackersChanged);

  app_->setTrackerEnabled(QStringLiteral("jira"), false);
  EXPECT_FALSE(app_->trackerEnabled(QStringLiteral("jira")));
  EXPECT_EQ(app_->disabledTrackers(), QStringList{QStringLiteral("jira")});
  EXPECT_GE(changed.count(), 1);
  settle(200);
  const qsizetype before = server.requests().size();
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);
  QSignalSpy found(app_.get(), &::AppController::trackerAssigneesFound);

  // Everything that could ask: sync by hand and "everything", every edit,
  // the assignee search.
  app_->syncNow(true);
  app_->syncProvider(QStringLiteral("jira"));
  editEverything();
  settle(600);

  ASSERT_EQ(server.requests().size(), before) << "a switched-off tracker was asked: " << server.requests().last().key().toStdString();
  EXPECT_FALSE(app_->canEditTrackerContent(kCard));
  ASSERT_EQ(finished.count(), 7);
  for(const auto& args : finished) {
    EXPECT_EQ(args.at(2).toString(), QStringLiteral("disabled")) << args.at(1).toString().toStdString();
  }
  ASSERT_EQ(found.count(), 1);
  // The connection, the token and the cards stay.
  const QJsonObject jira = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8())
                               .object()
                               .value(QStringLiteral("integrations"))
                               .toObject()
                               .value(QStringLiteral("jira"))
                               .toObject();
  EXPECT_TRUE(jira.value(QStringLiteral("connected")).toBool());
  EXPECT_FALSE(jira.value(QStringLiteral("enabled")).toBool());
  EXPECT_EQ(app_->integrationSecret(QStringLiteral("jira"), QStringLiteral("token")), QStringLiteral("tok"));
  ASSERT_GE(app_->tasks()->indexOfId(kCard), 0);
  EXPECT_EQ(card().title, QStringLiteral("five"));
  bool listed = false;
  for(const QVariant& v : app_->integrationHealth()) {
    const QVariantMap row = v.toMap();
    if(row.value(QStringLiteral("id")).toString() == QLatin1String("jira")) {
      listed = true;
      EXPECT_FALSE(row.value(QStringLiteral("enabled")).toBool());
    }
  }
  EXPECT_TRUE(listed) << "a switched-off tracker left the list of connected ones";

  // On again: no sign-in, it pulls by itself and edits go again.
  app_->setTrackerEnabled(QStringLiteral("jira"), true);
  EXPECT_TRUE(app_->disabledTrackers().isEmpty());
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return count(server, "POST /rest/api/3/search/jql") >= 1;
  }));
  EXPECT_TRUE(app_->canEditTrackerContent(kCard));
}

TEST_F(TrackerEdit, SwitchingOffMidEditRollsTheCardBack) {
  FakeHttpServer server;
  connectJira(server, true);
  addCard();
  QSignalSpy finished(app_.get(), &::AppController::trackerEditFinished);
  app_->editTrackerField(kCard, QStringLiteral("title"), QStringLiteral("new title"));
  EXPECT_EQ(card().title, QStringLiteral("new title"));
  // Switched off before Jira answers.
  app_->setTrackerEnabled(QStringLiteral("jira"), false);
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    return finished.count() == 1;
  }));
  EXPECT_EQ(finished.at(0).at(2).toString(), QStringLiteral("error"));
  EXPECT_EQ(card().title, QStringLiteral("five")) << "the card kept an edit that never reached Jira";
  settle(700);
  EXPECT_EQ(count(server, kPut), 0);
}

}  // namespace trackeredit

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
