// Writing a ticket's content back to Jira (owner item 16, 2026-10-23): the
// markdown → ADF / wiki conversions with their read-back check, the Smart
// Checklist line edits, and JiraProvider::editContent's requests on Cloud
// and on Server / Data Center against a local fake server.

#include "FakeHttpServer.h"

#include "integrations/JiraContentWrite.h"
#include "integrations/JiraProvider.h"
#include "integrations/SmartChecklist.h"
#include "integrations/SmartChecklistEdit.h"
#include "markdown/JiraWiki.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

namespace {

namespace sc = heap::integrations::smartchecklist;
using heap::integrations::ContentEdit;
using heap::integrations::ContentEditResult;
using heap::integrations::JiraDeployment;
using heap::integrations::JiraProvider;
using heap::testing::FakeHttpServer;

QJsonObject para(const QJsonArray& content) {
  return QJsonObject{{QStringLiteral("type"), QStringLiteral("paragraph")}, {QStringLiteral("content"), content}};
}

QJsonObject text(const QString& s) {
  return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), s}};
}

QJsonObject doc(const QJsonArray& content) {
  return QJsonObject{{QStringLiteral("type"), QStringLiteral("doc")}, {QStringLiteral("version"), 1}, {QStringLiteral("content"), content}};
}

QString render(const QJsonObject& adf) {
  return heap::integrations::jiraAdfToMarkdown(QJsonDocument(adf).toJson(QJsonDocument::Compact));
}

}  // namespace

// ── Smart Checklist: one line changes, the rest stays as it was ──

TEST(SmartChecklistEdit, StateChangesOnlyThatLineUnknownLinesHeadersDetailsKept) {
  const QString list =
      QStringLiteral("## Before release\r\n- write notes\r\n> the short ones\r\nnot an item at all\r\n\r\n- [ ] tag it\r\n~ build\r\n");
  sc::Edit e;
  e.op = sc::Edit::Op::State;
  e.index = 1;
  e.state = QStringLiteral("done");
  sc::EditResult r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok) << r.reason.toStdString();
  EXPECT_EQ(
      r.text,
      QStringLiteral("## Before release\r\n+ write notes\r\n> the short ones\r\nnot an item at all\r\n\r\n- [ ] tag it\r\n~ build\r\n"));
  EXPECT_EQ(r.items.at(1).toObject().value(QStringLiteral("details")).toString(), QStringLiteral("the short ones"));

  // A task-list line stays one: only the mark changes.
  e.index = 2;
  r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok);
  EXPECT_TRUE(r.text.contains(QStringLiteral("- [x] tag it\r\n")));
  e.state = QStringLiteral("progress");
  EXPECT_TRUE(sc::applyEdit(list, e).text.contains(QStringLiteral("- [~] tag it")));
  e.state = QStringLiteral("skipped");
  EXPECT_TRUE(sc::applyEdit(list, e).text.contains(QStringLiteral("x tag it")));
  e.index = 3;
  e.state = QStringLiteral("todo");
  EXPECT_TRUE(sc::applyEdit(list, e).text.contains(QStringLiteral("\r\n- build\r\n")));
}

TEST(SmartChecklistEdit, TextAndAdd) {
  const QString list = QStringLiteral("# Steps\n\\+ escaped done\n- two\n");
  sc::Edit e;
  e.op = sc::Edit::Op::Text;
  e.index = 1;
  e.text = QStringLiteral("  renamed  ");
  sc::EditResult r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok) << r.reason.toStdString();
  EXPECT_EQ(r.text, QStringLiteral("# Steps\n\\+ renamed\n- two\n"));
  EXPECT_EQ(r.items.at(1).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("done"));

  e.op = sc::Edit::Op::Add;
  e.text = QStringLiteral("three");
  r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.text, QStringLiteral("# Steps\n\\+ escaped done\n- two\n- three\n")) << "the list's trailing newline moved";
  EXPECT_EQ(r.items.size(), 4);

  // Into an empty list.
  r = sc::applyEdit(QString(), e);
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.text, QStringLiteral("- three"));
}

TEST(SmartChecklistEdit, RefusesWhatWouldNotReadBackAsMeant) {
  const QString list = QStringLiteral("# Steps\n- one\n");
  sc::Edit e;
  e.op = sc::Edit::Op::State;
  e.index = 0;
  e.state = QStringLiteral("done");
  EXPECT_EQ(sc::applyEdit(list, e).reason, QStringLiteral("header"));
  e.index = 5;
  EXPECT_EQ(sc::applyEdit(list, e).reason, QStringLiteral("index"));
  e.index = 1;
  e.state = QStringLiteral("[x]");
  EXPECT_EQ(sc::applyEdit(list, e).reason, QStringLiteral("state"));
  e.state = QStringLiteral("x");
  EXPECT_EQ(sc::applyEdit(list, e).reason, QStringLiteral("state")) << "one letter reads back as no status";
  e.op = sc::Edit::Op::Text;
  e.text = QStringLiteral("two\nlines");
  EXPECT_EQ(sc::applyEdit(list, e).reason, QStringLiteral("text"));
  // A text that reads as a status mark would change the item's status.
  e.text = QStringLiteral("[x] sneaky");
  EXPECT_EQ(sc::applyEdit(list, e).reason, QStringLiteral("roundTrip"));
}

TEST(SmartChecklistEdit, CustomStatusesWrittenInBracketsCheckedByCategory) {
  const QString list = QStringLiteral("- [IN QA] one\n> note\n+ [PASSED] two\n- [ ] three\n* four");
  sc::Edit e;
  e.op = sc::Edit::Op::State;
  // An unchecked custom status, by its words or its given category.
  e.index = 3;
  e.state = QStringLiteral("Waiting for review");
  sc::EditResult r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok) << r.reason.toStdString();
  EXPECT_TRUE(r.text.endsWith(QStringLiteral("\n- [Waiting for review] four")));
  EXPECT_EQ(r.items.at(3).toObject().value(QStringLiteral("statusName")).toString(), QStringLiteral("Waiting for review"));
  EXPECT_EQ(r.items.at(3).toObject().value(QStringLiteral("category")).toString(), QStringLiteral("problem"));
  // A checked one: "+"; a task-list line becomes a plain one.
  e.index = 2;
  e.state = QStringLiteral("Accepted by PO");
  e.category = QStringLiteral("done");
  r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok) << r.reason.toStdString();
  EXPECT_TRUE(r.text.contains(QStringLiteral("\n+ [Accepted by PO] three\n")));
  EXPECT_EQ(r.items.at(2).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
  // One custom status to another, then back to a built-in: the old name goes.
  e.index = 0;
  e.state = QStringLiteral("PASSED");
  e.category.clear();
  r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok) << r.reason.toStdString();
  EXPECT_TRUE(r.text.startsWith(QStringLiteral("+ [PASSED] one\n> note\n")));
  EXPECT_EQ(r.items.at(0).toObject().value(QStringLiteral("details")).toString(), QStringLiteral("note"));
  e.state = QStringLiteral("To Do");
  r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok);
  EXPECT_TRUE(r.text.startsWith(QStringLiteral("- one\n")));
  EXPECT_EQ(r.items.at(0).toObject().value(QStringLiteral("statusName")).toString(), QStringLiteral("To Do"));
  e.state = QStringLiteral("done");  // the old word still works
  EXPECT_TRUE(sc::applyEdit(list, e).text.startsWith(QStringLiteral("+ one\n")));
  // The text of an item with a custom status: the status stays.
  e.op = sc::Edit::Op::Text;
  e.index = 1;
  e.text = QStringLiteral("two, again");
  r = sc::applyEdit(list, e);
  ASSERT_TRUE(r.ok) << r.reason.toStdString();
  EXPECT_TRUE(r.text.contains(QStringLiteral("\n+ [PASSED] two, again\n")));
  EXPECT_EQ(r.items.at(1).toObject().value(QStringLiteral("statusName")).toString(), QStringLiteral("PASSED"));
}

TEST(SmartChecklistEdit, PropertyShapes) {
  QString t;
  EXPECT_TRUE(sc::textOfPropertyValue(QJsonValue(QStringLiteral("- a")), &t));
  EXPECT_EQ(t, QStringLiteral("- a"));
  const QJsonObject wrapped{{QStringLiteral("value"), QStringLiteral("- a")}, {QStringLiteral("rev"), 3}};
  EXPECT_TRUE(sc::textOfPropertyValue(wrapped, &t));
  const QJsonObject back = sc::withPropertyText(wrapped, QStringLiteral("+ a")).toObject();
  EXPECT_EQ(back.value(QStringLiteral("value")).toString(), QStringLiteral("+ a"));
  EXPECT_EQ(back.value(QStringLiteral("rev")).toInt(), 3) << "another key of the property was lost";
  // Item objects are read, never written.
  EXPECT_FALSE(sc::textOfPropertyValue(QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("a")}}}, &t));
  EXPECT_FALSE(sc::textOfPropertyValue(QJsonValue(QStringLiteral(R"([{"name":"a"}])")), &t));
}

// ── markdown → ADF (Cloud) ──

TEST(JiraEditAdf, CommonMarkdownReadsBackTheSame) {
  const QString md = QStringLiteral(
      "# Title\n\nSome **bold**, *em*, ~~gone~~, `code` and a [link](https://x.io).\n\n"
      "- one\n- two\n  - nested\n\n1. first\n2. second\n\n- [ ] open\n- [x] closed\n\n"
      "```cpp\nint x = 1;\n\nreturn x;\n```\n\n> quoted\n\n---\n\n| a | b |\n| --- | --- |\n| 1 | 2 |");
  const QJsonObject adf = heap::integrations::markdownToAdf(md);
  EXPECT_EQ(heap::integrations::markdownForCompare(render(adf)), heap::integrations::markdownForCompare(md)) << render(adf).toStdString();
  const heap::integrations::JiraTextWrite w = heap::integrations::jiraDescriptionForCloud(md, QJsonValue(QJsonValue::Null));
  EXPECT_TRUE(w.ok) << w.reason.toStdString() << " " << w.detail.toStdString();
}

TEST(JiraEditAdf, UnchangedBlocksAreSentBackAsTheyWereEvenWhatMarkdownCannotSay) {
  const QJsonObject mention{
      {QStringLiteral("type"), QStringLiteral("mention")},
      {QStringLiteral("attrs"),
       QJsonObject{{QStringLiteral("id"), QStringLiteral("acc-9")}, {QStringLiteral("text"), QStringLiteral("@Ann")}}}};
  const QJsonObject panel{{QStringLiteral("type"), QStringLiteral("expand")},
                          {QStringLiteral("attrs"), QJsonObject{{QStringLiteral("title"), QStringLiteral("Logs")}}},
                          {QStringLiteral("content"), QJsonArray{para({text(QStringLiteral("hidden"))})}}};
  const QJsonObject current =
      doc({para({text(QStringLiteral("Ask ")), mention, text(QStringLiteral(" first."))}), panel, para({text(QStringLiteral("Old."))})});
  const QString shown = render(current);
  const QString edited = QString(shown).replace(QStringLiteral("Old."), QStringLiteral("New."));
  heap::integrations::JiraTextWrite w = heap::integrations::jiraDescriptionForCloud(edited, current);
  ASSERT_TRUE(w.ok) << w.reason.toStdString() << " " << w.detail.toStdString();
  const QJsonArray content = w.value.toObject().value(QStringLiteral("content")).toArray();
  ASSERT_EQ(content.size(), 3);
  EXPECT_EQ(content.at(0).toObject(), current.value(QStringLiteral("content")).toArray().at(0).toObject())
      << "the mention's block was rebuilt";
  EXPECT_EQ(content.at(1).toObject(), panel);
  EXPECT_EQ(w.stored, edited);

  // Editing the paragraph with the mention would make it plain "@Ann": refused.
  const QString touched = QString(shown).replace(QStringLiteral("first."), QStringLiteral("before."));
  w = heap::integrations::jiraDescriptionForCloud(touched, current);
  EXPECT_FALSE(w.ok);
  EXPECT_EQ(w.reason, QStringLiteral("lossy"));
  EXPECT_EQ(w.detail, QStringLiteral("@Ann"));

  // Deleting it is no loss of form: allowed.
  QStringList lines = shown.split(QLatin1Char('\n'));
  lines.removeFirst();
  w = heap::integrations::jiraDescriptionForCloud(lines.join(QLatin1Char('\n')).trimmed(), current);
  EXPECT_TRUE(w.ok) << w.reason.toStdString();
}

TEST(JiraEditAdf, AFileCannotBeMadeFromMarkdown) {
  const heap::integrations::JiraTextWrite w =
      heap::integrations::jiraDescriptionForCloud(QStringLiteral("Look:\n\n![shot](shot.png)"), QJsonValue(QJsonValue::Null));
  EXPECT_FALSE(w.ok);
  EXPECT_EQ(w.reason, QStringLiteral("image"));
  // A picture from the web is fine.
  EXPECT_TRUE(heap::integrations::jiraDescriptionForCloud(QStringLiteral("![shot](https://x.io/a.png)"), QJsonValue(QJsonValue::Null)).ok);
}

TEST(JiraEditAdf, EmptyClearsTheDescription) {
  const heap::integrations::JiraTextWrite w =
      heap::integrations::jiraDescriptionForCloud(QString(), doc({para({text(QStringLiteral("x"))})}));
  ASSERT_TRUE(w.ok);
  EXPECT_TRUE(w.value.isNull());
}

// ── markdown → wiki (Server / Data Center) ──

TEST(JiraEditWiki, CommonMarkdownBecomesWiki) {
  EXPECT_EQ(heap::integrations::markdownToJiraWiki(
                QStringLiteral("## Head\n\nSome **bold**, *em* and `code` [here](https://x.io).\n\n- a\n  - b\n1. c")),
            QStringLiteral("h2. Head\n\nSome *bold*, _em_ and {{code}} [here|https://x.io].\n\n* a\n** b\n# c"));
  EXPECT_EQ(heap::integrations::markdownToJiraWiki(QStringLiteral("```java\nint x;\n```")), QStringLiteral("{code:java}\nint x;\n{code}"));
  EXPECT_EQ(heap::integrations::markdownToJiraWiki(QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 |")),
            QStringLiteral("||a||b||\n|1|2|"));
}

TEST(JiraEditWiki, UnchangedChunksKeepTheirMarkupChangedOnesAreChecked) {
  const QString current =
      QStringLiteral("h2. Plan\n\n{panel:title=Careful}\nDo not\n\nskip this\n{panel}\n\nAsk [~ann] first.\n\nOld line.");
  const QString shown = heap::md::jiraWikiToMarkdown(current);
  heap::integrations::JiraTextWrite w = heap::integrations::jiraDescriptionForServer(
      QString(shown).replace(QStringLiteral("Old line."), QStringLiteral("New *line*.")), current);
  ASSERT_TRUE(w.ok) << w.reason.toStdString() << " " << w.detail.toStdString();
  EXPECT_EQ(w.value.toString(),
            QStringLiteral("h2. Plan\n\n{panel:title=Careful}\nDo not\n\nskip this\n{panel}\n\nAsk [~ann] first.\n\nNew _line_."));

  w = heap::integrations::jiraDescriptionForServer(QString(shown).replace(QStringLiteral("first."), QStringLiteral("later.")), current);
  EXPECT_FALSE(w.ok);
  EXPECT_EQ(w.reason, QStringLiteral("lossy"));
  EXPECT_EQ(w.detail, QStringLiteral("@ann"));

  // Markdown characters that Jira reads as its own markup.
  w = heap::integrations::jiraDescriptionForServer(QStringLiteral("Use {toc} here"), QString());
  EXPECT_FALSE(w.ok);
  EXPECT_EQ(w.reason, QStringLiteral("roundTrip"));
}

TEST(JiraEditUsers, AssignableSearchAnswer) {
  const QByteArray cloud =
      R"([{"accountId":"a1","displayName":"Ann","emailAddress":"a@x.io","active":true},{"accountId":"a2","displayName":"Old","active":false}])";
  const QVariantList users = heap::integrations::parseJiraAssignableUsers(cloud, false);
  ASSERT_EQ(users.size(), 1);
  EXPECT_EQ(users.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("a1"));
  const QVariantList server =
      heap::integrations::parseJiraAssignableUsers(R"([{"name":"ann","key":"JIRAUSER1","displayName":"Ann"}])", true);
  ASSERT_EQ(server.size(), 1);
  EXPECT_EQ(server.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("ann"));
}

// ── the provider's requests ──

class JiraEditNetwork : public ::testing::Test {
 protected:
  void SetUp() override {
    JiraProvider::resetSiteCachesForTesting();
  }

  void TearDown() override {
    JiraProvider::resetSiteCachesForTesting();
  }

  static void SetUpTestSuite() {
    if(QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "heap_jira_tests";
      static char* argv[] = {arg0, nullptr};
      new QCoreApplication(argc, argv);
    }
  }

  static ContentEditResult run(JiraProvider& p, const ContentEdit& e) {
    ContentEditResult out;
    bool done = false;
    QObject::connect(&p, &heap::integrations::IntegrationProvider::contentEdited, &p, [&](const ContentEditResult& r) {
      out = r;
      done = true;
    });
    p.editContent(e);
    EXPECT_TRUE(heap::testing::waitFor(done)) << "contentEdited never arrived";
    return out;
  }
};

TEST_F(JiraEditNetwork, CloudSummaryAndAssignee) {
  FakeHttpServer server;
  server.route("GET /rest/api/3/issue/HT-5",
               {.status = 200, .body = R"({"key":"HT-5","fields":{"summary":"five","assignee":{"displayName":"Ann"}}})"});
  server.route("PUT /rest/api/3/issue/HT-5", {.status = 204, .body = ""});
  JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(JiraDeployment::Cloud);

  ContentEdit e;
  e.externalId = QStringLiteral("HT-5");
  e.field = QStringLiteral("summary");
  e.base = QStringLiteral("five");
  e.value = QStringLiteral("six");
  ContentEditResult r = run(p, e);
  EXPECT_EQ(r.outcome, QStringLiteral("ok")) << r.error.toStdString();
  EXPECT_EQ(server.lastRequest("PUT /rest/api/3/issue/HT-5").body, QByteArray(R"({"fields":{"summary":"six"}})"));

  e.field = QStringLiteral("assignee");
  e.base = QStringLiteral("Ann");
  e.value = QStringLiteral("acc-2");
  r = run(p, e);
  EXPECT_EQ(r.outcome, QStringLiteral("ok"));
  EXPECT_EQ(server.lastRequest("PUT /rest/api/3/issue/HT-5").body, QByteArray(R"({"fields":{"assignee":{"accountId":"acc-2"}}})"));

  // Unassign.
  e.value.clear();
  r = run(p, e);
  EXPECT_EQ(server.lastRequest("PUT /rest/api/3/issue/HT-5").body, QByteArray(R"({"fields":{"assignee":null}})"));

  // Changed in Jira since: nothing written, Jira's value handed back.
  const int puts = static_cast<int>(server.seen().count("PUT /rest/api/3/issue/HT-5"));
  e.field = QStringLiteral("summary");
  e.base = QStringLiteral("four");
  r = run(p, e);
  EXPECT_EQ(r.outcome, QStringLiteral("conflict"));
  EXPECT_EQ(r.remote, QStringLiteral("five"));
  EXPECT_EQ(server.seen().count("PUT /rest/api/3/issue/HT-5"), puts);
}

TEST_F(JiraEditNetwork, ServerWikiDescriptionNameAssigneeChecklistField) {
  FakeHttpServer server;
  server.route(
      "GET /rest/api/2/field",
      {.status = 200,
       .body =
           R"([{"id":"customfield_10500","name":"Checklists","schema":{"type":"string","custom":"com.railsware.jira.smartchecklist:text"}}])"});
  server.route("POST /rest/api/2/search", {.status = 200, .body = R"({"issues":[]})"});
  server.route(
      "GET /rest/api/2/issue/DC-1",
      {.status = 200,
       .body =
           R"({"key":"DC-1","fields":{"summary":"s","description":"h2. Plan\n\nOld.","assignee":{"displayName":"Ann"},"customfield_10500":"# Do\n- one\nfree text\n- two"}})"});
  server.route("PUT /rest/api/2/issue/DC-1", {.status = 204, .body = ""});
  JiraProvider p;
  p.setConfig(server.base(), QString(), QStringLiteral("pat"), QString());
  p.setDeployment(JiraDeployment::Server);
  // A pull learns the site's checklist field.
  bool pulled = false;
  QObject::connect(&p, &heap::integrations::IntegrationProvider::tasksFetched, &p, [&]() {
    pulled = true;
  });
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitFor(pulled));

  ContentEdit e;
  e.externalId = QStringLiteral("DC-1");
  e.field = QStringLiteral("description");
  e.base = QStringLiteral("h2. Plan\n\nOld.");
  e.value = QStringLiteral("## Plan\n\nNew **one**.");
  ContentEditResult r = run(p, e);
  ASSERT_EQ(r.outcome, QStringLiteral("ok")) << r.error.toStdString() << " " << r.detail.toStdString();
  const auto put = server.lastRequest("PUT /rest/api/2/issue/DC-1");
  EXPECT_EQ(put.headers.value("authorization"), QByteArray("Bearer pat"));
  EXPECT_EQ(
      QJsonDocument::fromJson(put.body).object().value(QStringLiteral("fields")).toObject().value(QStringLiteral("description")).toString(),
      QStringLiteral("h2. Plan\n\nNew *one*."));
  EXPECT_EQ(r.remote, QStringLiteral("h2. Plan\n\nNew *one*."));
  EXPECT_FALSE(put.body.contains("properties")) << "Server/DC refuses unknown fields";

  e.field = QStringLiteral("assignee");
  e.base = QStringLiteral("Ann");
  e.value = QStringLiteral("bob");
  r = run(p, e);
  EXPECT_EQ(server.lastRequest("PUT /rest/api/2/issue/DC-1").body, QByteArray(R"({"fields":{"assignee":{"name":"bob"}}})"));

  e.field = QStringLiteral("checklist");
  e.checklistOp = QStringLiteral("state");
  e.index = 2;
  e.value = QStringLiteral("done");
  e.baseChecklist = sc::parse(QStringLiteral("# Do\n- one\nfree text\n- two"));
  r = run(p, e);
  ASSERT_EQ(r.outcome, QStringLiteral("ok")) << r.error.toStdString();
  EXPECT_EQ(QJsonDocument::fromJson(server.lastRequest("PUT /rest/api/2/issue/DC-1").body)
                .object()
                .value(QStringLiteral("fields"))
                .toObject()
                .value(QStringLiteral("customfield_10500"))
                .toString(),
            QStringLiteral("# Do\n- one\nfree text\n+ two"));
  EXPECT_EQ(r.remoteChecklist.size(), 3);
  // The read before it asked for the field, and no property (none on DC).
  EXPECT_TRUE(server.lastRequest("GET /rest/api/2/issue/DC-1").query.contains("customfield_10500"));
}

TEST_F(JiraEditNetwork, ReadFailureNothingWritten) {
  FakeHttpServer server;
  server.route("GET /rest/api/3/issue/HT-5", {.status = 503, .body = R"({"errorMessages":["down"]})", .headers = {{"Retry-After", "120"}}});
  JiraProvider p;
  p.setConfig(server.base(), QStringLiteral("me@example.com"), QStringLiteral("tok"), QString());
  p.setDeployment(JiraDeployment::Cloud);
  ContentEdit e;
  e.externalId = QStringLiteral("HT-5");
  e.field = QStringLiteral("summary");
  e.value = QStringLiteral("x");
  const ContentEditResult r = run(p, e);
  EXPECT_EQ(r.outcome, QStringLiteral("error"));
  EXPECT_EQ(r.httpStatus, 503);
  EXPECT_EQ(r.retryAfterSecs, 120);
  EXPECT_EQ(server.seen().count("PUT /rest/api/3/issue/HT-5"), 0);
}
