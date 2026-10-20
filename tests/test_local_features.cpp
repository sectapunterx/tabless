// The local layer as the UI works it (APP-236…241, 246, 250, 251): what the
// AppController API does with Task.local, end to end through the model. The
// "no sync path writes local" matrix lives in test_local_layer.cpp; this file
// covers each feature's own behaviour, plus the pull cases that feature adds.

#include "AppController.h"
#include "Models.h"
#include "StateSerializer.h"

#include "integrations/IntegrationTypes.h"
#include "local/Effective.h"
#include "local/Sessions.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSignalSpy>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include <memory>

using heap::integrations::ExternalTask;

namespace {

class LocalFeatures : public ::testing::Test {
 protected:
  void SetUp() override {
    // CI runs seen by an earlier test (or run) are not this one's.
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/ci-seen.json"));
    app_ = std::make_unique<::AppController>();
    app_->tasks()->reset({});
    app_->setLanguage(QStringLiteral("en"));
    app_->setAppSettingsJson(QStringLiteral("{}"));
    QJsonObject settings;
    settings.insert(QStringLiteral("integrations"),
                    QJsonObject{{QStringLiteral("github"), QJsonObject{{QStringLiteral("repo"), QStringLiteral("acme/app")}}}});
    app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  }

  void TearDown() override {
    app_->setAppSettingsJson(QStringLiteral("{}"));
    app_.reset();
  }

  static ExternalTask issue(const QString& number, const QString& priority = QStringLiteral("medium")) {
    ExternalTask e;
    e.providerId = QStringLiteral("github");
    e.externalId = number;
    e.url = QStringLiteral("https://github.com/acme/app/issues/") + number;
    e.title = QStringLiteral("issue ") + number;
    e.body = QStringLiteral("body");
    e.status = QStringLiteral("open");
    e.project = QStringLiteral("acme/app");
    e.priority = priority;
    return e;
  }

  void merge(const QVector<ExternalTask>& issues) {
    app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("gh-"), issues, true);
  }

  QString addLocal(const QString& id, const QString& title = QStringLiteral("local task")) {
    Task t;
    t.id = id;
    t.title = title;
    t.status = QStringLiteral("todo");
    t.priority = QStringLiteral("P2");
    t.statusChangedAt = QDateTime::currentDateTime();
    app_->tasks()->upsert(t);
    return id;
  }

  const Task* task(const QString& id) const {
    const int row = app_->tasks()->indexOfId(id);
    return row >= 0 ? &app_->tasks()->items().at(row) : nullptr;
  }

  QStringList searchIds(const QString& text) const {
    return app_->compileSearch(text).value(QStringLiteral("ids")).toStringList();
  }

  std::unique_ptr<::AppController> app_;
};

}  // namespace

// ── APP-238: my priority and due over the tracker's ──

TEST_F(LocalFeatures, MyPriorityStaysThroughAPullAndTheChangeIsShown) {
  merge({issue("1", QStringLiteral("medium"))});
  app_->setTaskPriority(QStringLiteral("gh-1"), QStringLiteral("P0"));
  EXPECT_EQ(task("gh-1")->priority, QString("P2")) << "the tracker's field stays the tracker's";
  EXPECT_EQ(heap::local::effectivePriority(*task("gh-1")), QString("P0"));
  EXPECT_FALSE(app_->trackerValues(QStringLiteral("gh-1")).value(QStringLiteral("priorityChanged")).toBool());

  merge({issue("1", QStringLiteral("low"))});
  EXPECT_EQ(task("gh-1")->priority, QString("P3"));
  EXPECT_EQ(heap::local::effectivePriority(*task("gh-1")), QString("P0")) << "a pull never takes mine";
  EXPECT_TRUE(app_->trackerValues(QStringLiteral("gh-1")).value(QStringLiteral("priorityChanged")).toBool());
  EXPECT_TRUE(app_->tasks()
                  ->data(app_->tasks()->index(app_->tasks()->indexOfId(QStringLiteral("gh-1")), 0), TaskModel::LocalRole)
                  .toMap()
                  .value(QStringLiteral("trackerChanged"))
                  .toBool());

  app_->acknowledgeTrackerChange(QStringLiteral("gh-1"));
  EXPECT_FALSE(app_->trackerValues(QStringLiteral("gh-1")).value(QStringLiteral("priorityChanged")).toBool());
  EXPECT_EQ(heap::local::effectivePriority(*task("gh-1")), QString("P0"));

  app_->resetToTracker(QStringLiteral("gh-1"));
  EXPECT_EQ(heap::local::effectivePriority(*task("gh-1")), QString("P3"));
  EXPECT_TRUE(task("gh-1")->local.myPriority.isEmpty());
  app_->undo();
  EXPECT_EQ(heap::local::effectivePriority(*task("gh-1")), QString("P0")) << "reset is one undo step";
}

TEST_F(LocalFeatures, SearchAndSortReadMyPriority) {
  merge({issue("1", QStringLiteral("low")), issue("2", QStringLiteral("high"))});
  app_->setTaskPriority(QStringLiteral("gh-1"), QStringLiteral("P0"));
  EXPECT_EQ(searchIds(QStringLiteral("priority:P0")), QStringList{QStringLiteral("gh-1")});
  EXPECT_TRUE(searchIds(QStringLiteral("priority:P3")).isEmpty());
}

// ── APP-237: the notepad ──

TEST_F(LocalFeatures, TheNotepadIsSearchableAndCopiesIntoANoteThatNamesTheTask) {
  merge({issue("1")});
  app_->setTaskLocalNotes(QStringLiteral("gh-1"), QStringLiteral("tried a smaller pool: zanzibar"));
  const int row = app_->tasks()->indexOfId(QStringLiteral("gh-1"));
  EXPECT_TRUE(app_->tasks()->searchTextAt(row).contains(QStringLiteral("zanzibar")));
  EXPECT_EQ(searchIds(QStringLiteral("has:notes")), QStringList{QStringLiteral("gh-1")});

  const QString title = app_->copyTaskNotesToNote(QStringLiteral("gh-1"));
  ASSERT_FALSE(title.isEmpty());
  const QVariantList mentions = app_->notesMentioningTask(QStringLiteral("gh-1"));
  ASSERT_EQ(mentions.size(), 1);
  EXPECT_EQ(mentions.first().toMap().value(QStringLiteral("title")).toString(), title);
  EXPECT_EQ(task("gh-1")->local.notes, QString("tried a smaller pool: zanzibar")) << "a copy, not a move";
  EXPECT_NE(app_->copyTaskNotesToNote(QStringLiteral("gh-1")), title) << "a second copy gets its own title";
}

// ── APP-236: the checklist ──

TEST_F(LocalFeatures, APastedListKeepsLevelsAndTicksAndTheNextStepFollows) {
  addLocal(QStringLiteral("T-1"));
  app_->addChecklistItems(QStringLiteral("T-1"), QString(), QStringLiteral("- [x] repro\n- fix\n-- parser\n-- tests\n- ship"));
  const QVariantList items = app_->taskChecklist(QStringLiteral("T-1"));
  ASSERT_EQ(items.size(), 5);
  EXPECT_EQ(items.at(2).toMap().value(QStringLiteral("level")).toInt(), 2);
  const auto role = [&]() {
    return app_->tasks()->data(app_->tasks()->index(app_->tasks()->indexOfId(QStringLiteral("T-1")), 0), TaskModel::ChecklistRole).toMap();
  };
  EXPECT_EQ(role().value(QStringLiteral("next")).toString(), QString("parser"));
  EXPECT_EQ(role().value(QStringLiteral("total")).toInt(), 5);
  EXPECT_EQ(role().value(QStringLiteral("done")).toInt(), 1);

  const QString parser = items.at(2).toMap().value(QStringLiteral("id")).toString();
  const QString tests = items.at(3).toMap().value(QStringLiteral("id")).toString();
  app_->toggleChecklistItem(QStringLiteral("T-1"), parser);
  app_->toggleChecklistItem(QStringLiteral("T-1"), tests);
  const QVariantMap fix = app_->taskChecklist(QStringLiteral("T-1")).at(1).toMap();
  EXPECT_TRUE(fix.value(QStringLiteral("done")).toBool());
  EXPECT_TRUE(fix.value(QStringLiteral("autoDone")).toBool()) << "closed because its children are";
  EXPECT_EQ(role().value(QStringLiteral("next")).toString(), QString("ship"));
  EXPECT_EQ(app_->taskChecklistText(QStringLiteral("T-1")), QString("- [x] repro\n- [x] fix\n-- [x] parser\n-- [x] tests\n- ship"));
}

TEST_F(LocalFeatures, AnItemBecomesACardThatTicksWithItsDone) {
  addLocal(QStringLiteral("T-1"));
  app_->addChecklistItems(QStringLiteral("T-1"), QString(), QStringLiteral("- migrate\n-- dump\n--- verify\n- deploy"));
  const QString itemId = app_->taskChecklist(QStringLiteral("T-1")).at(0).toMap().value(QStringLiteral("id")).toString();
  const QString cardId = app_->checklistItemToCard(QStringLiteral("T-1"), itemId);
  ASSERT_FALSE(cardId.isEmpty());
  const Task* card = task(cardId);
  ASSERT_NE(card, nullptr);
  EXPECT_EQ(card->title, QString("migrate"));
  ASSERT_EQ(card->local.checklist.size(), 2) << "the sub-items moved with it";
  EXPECT_EQ(card->local.checklist.at(0).level, 1);
  EXPECT_EQ(card->local.checklist.at(1).level, 2);
  ASSERT_EQ(card->local.related.size(), 1);
  EXPECT_EQ(card->local.related.first().kind, QString("partOf"));
  EXPECT_EQ(task("T-1")->local.checklist.size(), 2);
  EXPECT_EQ(task("T-1")->local.checklist.first().cardId, cardId);

  app_->toggleDone({cardId});
  EXPECT_TRUE(task("T-1")->local.checklist.first().done) << "the card's Done ticks the item";
  app_->toggleChecklistItem(QStringLiteral("T-1"), itemId);
  EXPECT_FALSE(task("T-1")->local.checklist.first().done);
  EXPECT_NE(app_->statusCategory(task(cardId)->status), QString("done")) << "unticking the item reopens the card";

  EXPECT_TRUE(app_->checklistCardBack(QStringLiteral("T-1"), itemId));
  EXPECT_EQ(task(cardId), nullptr);
  EXPECT_EQ(app_->taskChecklistText(QStringLiteral("T-1")), QString("- migrate\n-- dump\n--- verify\n- deploy"));
}

TEST_F(LocalFeatures, BackToListKeepsWhatWasWrittenOnTheCard) {
  // IDIOT-TASKS-4: "Back to list" deleted the card's text without a word.
  addLocal(QStringLiteral("T-1"));
  app_->addChecklistItems(QStringLiteral("T-1"), QString(), QStringLiteral("- migrate"));
  const QString itemId = app_->taskChecklist(QStringLiteral("T-1")).at(0).toMap().value(QStringLiteral("id")).toString();
  const QString cardId = app_->checklistItemToCard(QStringLiteral("T-1"), itemId);
  ASSERT_FALSE(cardId.isEmpty());
  Task card = *task(cardId);
  card.desc = QStringLiteral("Important design notes");
  app_->tasks()->upsert(card);
  QSignalSpy toasts(app_.get(), &AppController::toast);
  EXPECT_FALSE(app_->checklistCardBack(QStringLiteral("T-1"), itemId));
  EXPECT_NE(task(cardId), nullptr) << "the card and its text stay";
  EXPECT_EQ(toasts.count(), 1) << "and the refusal is said";

  // A card with nothing but a new title goes back under that title.
  card.desc.clear();
  card.title = QStringLiteral("migrate the db");
  app_->tasks()->upsert(card);
  EXPECT_TRUE(app_->checklistCardBack(QStringLiteral("T-1"), itemId));
  EXPECT_EQ(task(cardId), nullptr);
  EXPECT_EQ(app_->taskChecklistText(QStringLiteral("T-1")), QString("- migrate the db"));
}

TEST_F(LocalFeatures, AChecklistOnATrackerCardSurvivesAPullThatRewritesTheBody) {
  merge({issue("1")});
  app_->addChecklistItems(QStringLiteral("gh-1"), QString(), QStringLiteral("- one\n- two"));
  ExternalTask e = issue("1");
  e.body = QStringLiteral("- [ ] tracker item");
  merge({e});
  EXPECT_EQ(task("gh-1")->local.checklist.size(), 2);
  const QVariantMap cl =
      app_->tasks()->data(app_->tasks()->index(app_->tasks()->indexOfId(QStringLiteral("gh-1")), 0), TaskModel::ChecklistRole).toMap();
  EXPECT_EQ(cl.value(QStringLiteral("localTotal")).toInt(), 2);
  EXPECT_EQ(cl.value(QStringLiteral("descTotal")).toInt(), 1);
  EXPECT_EQ(cl.value(QStringLiteral("next")).toString(), QString("one")) << "mine first";
}

// ── APP-239: my tags ──

TEST_F(LocalFeatures, TagsAreSharedRenamedMergedAndFilteredAcrossTrackers) {
  merge({issue("1")});
  addLocal(QStringLiteral("T-1"));
  app_->setTaskLocalTags(QStringLiteral("gh-1"), {QVariantMap{{"id", "after-release"}, {"color", "#5cc2dd"}}});
  app_->setTaskLocalTags(QStringLiteral("T-1"), {QStringLiteral("#After-Release"), QStringLiteral("quick")});
  EXPECT_EQ(task("T-1")->local.tags.at(0).id, QString("after-release")) << "a known tag keeps its spelling";
  EXPECT_EQ(task("T-1")->local.tags.at(0).color, QString("#5cc2dd")) << "and its colour";
  QStringList hit = searchIds(QStringLiteral("#after-release"));
  hit.sort();
  EXPECT_EQ(hit, (QStringList{"T-1", "gh-1"}));

  app_->renameLocalTag(QStringLiteral("quick"), QStringLiteral("after-release"));  // a merge
  EXPECT_EQ(task("T-1")->local.tags.size(), 1);
  const QVariantList catalog = app_->localTagCatalog();
  ASSERT_EQ(catalog.size(), 1);
  EXPECT_EQ(catalog.first().toMap().value(QStringLiteral("count")).toInt(), 2);

  merge({issue("1")});
  EXPECT_EQ(task("gh-1")->local.tags.size(), 1) << "a pull does not touch my tags";
  EXPECT_TRUE(task("gh-1")->labels.isEmpty()) << "and they never become the tracker's labels";

  app_->deleteLocalTag(QStringLiteral("after-release"));
  EXPECT_TRUE(task("gh-1")->local.tags.isEmpty());
  EXPECT_TRUE(task("T-1")->local.tags.isEmpty());
}

// ── APP-240: links drawn by hand ──

TEST_F(LocalFeatures, ARelatedLinkIsTwoSidedByKeyOrUrlAndSurvivesItsTargetGoing) {
  merge({issue("40")});
  addLocal(QStringLiteral("T-1"));
  ASSERT_TRUE(app_->addRelatedLink(QStringLiteral("T-1"), QStringLiteral("#40")));
  QVariantList rel = app_->taskRelations(QStringLiteral("gh-40"));
  ASSERT_EQ(rel.size(), 1);
  EXPECT_EQ(rel.first().toMap().value(QStringLiteral("target")).toString(), QString("T-1"));
  EXPECT_TRUE(app_->addRelatedLink(QStringLiteral("T-1"), QStringLiteral("https://gitlab.example/a/-/merge_requests/17")));
  EXPECT_FALSE(app_->addRelatedLink(QStringLiteral("T-1"), QStringLiteral("no such thing")));
  EXPECT_EQ(app_->taskRelations(QStringLiteral("T-1")).size(), 2);
  EXPECT_EQ(searchIds(QStringLiteral("has:links")).size(), 2);

  // The linked card leaves; the link only shows it is gone.
  app_->tasks()->removeById(QStringLiteral("gh-40"));
  rel = app_->taskRelations(QStringLiteral("T-1"));
  ASSERT_EQ(rel.size(), 2);
  EXPECT_FALSE(rel.first().toMap().value(QStringLiteral("exists")).toBool());

  const QString linkId = rel.first().toMap().value(QStringLiteral("linkId")).toString();
  app_->removeRelatedLink(QStringLiteral("T-1"), linkId);
  EXPECT_EQ(app_->taskRelations(QStringLiteral("T-1")).size(), 1);
}

TEST_F(LocalFeatures, BlocksDrawnByHandFeedIsBlocked) {
  addLocal(QStringLiteral("T-1"), QStringLiteral("blocker"));
  addLocal(QStringLiteral("T-2"), QStringLiteral("waits"));
  ASSERT_TRUE(app_->addBlockLink(QStringLiteral("T-2"), QStringLiteral("T-1"), /*blocksOther=*/false));
  EXPECT_EQ(searchIds(QStringLiteral("is:blocked")), QStringList{QStringLiteral("T-2")});
  QVariantList rel = app_->taskRelations(QStringLiteral("T-2"));
  ASSERT_EQ(rel.size(), 1);
  EXPECT_EQ(rel.first().toMap().value(QStringLiteral("kind")).toString(), QString("blockedBy"));
  app_->toggleDone({QStringLiteral("T-1")});
  EXPECT_TRUE(searchIds(QStringLiteral("is:blocked")).isEmpty()) << "a done blocker blocks nothing";
  app_->removeBlockLink(QStringLiteral("T-1"), QStringLiteral("T-2"));
  EXPECT_TRUE(app_->taskRelations(QStringLiteral("T-2")).isEmpty());
}

// ── APP-241: the comment draft ──

TEST_F(LocalFeatures, TheDraftIsCopiedNotSentAndClearedOnlyByHandWithUndo) {
  merge({issue("1")});
  app_->setTaskCommentDraft(QStringLiteral("gh-1"), QStringLiteral("Reproduced:\n\n- step one\n- step two"));
  merge({issue("1")});
  EXPECT_FALSE(task("gh-1")->local.commentDraft.isEmpty()) << "a pull keeps the draft";
  EXPECT_EQ(searchIds(QStringLiteral("has:draft")), QStringList{QStringLiteral("gh-1")});

  const QString url = app_->copyCommentDraft(QStringLiteral("gh-1"));
  EXPECT_EQ(url, QString("https://github.com/acme/app/issues/1"));
  const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
  ASSERT_NE(mime, nullptr);
  EXPECT_TRUE(mime->text().contains(QStringLiteral("- step one")));
  EXPECT_TRUE(mime->hasHtml());
  EXPECT_FALSE(task("gh-1")->local.commentDraft.isEmpty()) << "copying does not clear it";

  app_->clearTaskCommentDraft(QStringLiteral("gh-1"));
  EXPECT_TRUE(task("gh-1")->local.commentDraft.isEmpty());
  app_->undo();
  EXPECT_FALSE(task("gh-1")->local.commentDraft.isEmpty());
}

// ── APP-246: estimates ──

TEST_F(LocalFeatures, EstimateSumsCountTheTasksWithoutOneApart) {
  addLocal(QStringLiteral("T-1"));
  addLocal(QStringLiteral("T-2"));
  addLocal(QStringLiteral("T-3"));
  Task t = *task(QStringLiteral("T-1"));
  t.estimateMinutes = 90;
  app_->tasks()->upsert(t);
  t = *task(QStringLiteral("T-2"));
  t.estimateMinutes = 30;
  app_->tasks()->upsert(t);
  const QVariantMap s =
      app_->estimateSummary({QStringLiteral("T-1"), QStringLiteral("T-2"), QStringLiteral("T-3"), QStringLiteral("nope")});
  EXPECT_EQ(s.value(QStringLiteral("count")).toInt(), 3);
  EXPECT_EQ(s.value(QStringLiteral("minutes")).toInt(), 120);
  EXPECT_EQ(s.value(QStringLiteral("without")).toInt(), 1);
  EXPECT_EQ(searchIds(QStringLiteral("estimate:none")), QStringList{QStringLiteral("T-3")});
  EXPECT_EQ(searchIds(QStringLiteral("estimate:>1h")), QStringList{QStringLiteral("T-1")});
}

// ── APP-251: timer sessions ──

TEST_F(LocalFeatures, SessionsReplaceTheTotalAndEditsRecomputeIt) {
  addLocal(QStringLiteral("T-1"));
  Task t = *task(QStringLiteral("T-1"));
  t.trackedSeconds = 600;  // tracked before sessions
  app_->tasks()->upsert(t);
  const QDateTime start(QDate::currentDate(), QTime(9, 0));
  ASSERT_TRUE(app_->addTaskSession(QStringLiteral("T-1"), start, start.addSecs(1800)));
  EXPECT_EQ(task("T-1")->trackedSeconds, 2400) << "the old total is kept as an undated session";
  QVariantList ss = app_->taskSessions(QStringLiteral("T-1"));
  ASSERT_EQ(ss.size(), 2);
  EXPECT_TRUE(ss.last().toMap().value(QStringLiteral("undated")).toBool());
  EXPECT_EQ(app_->trackedSecondsOn(QDate::currentDate()), 1800);

  const QString sid = ss.first().toMap().value(QStringLiteral("id")).toString();
  ASSERT_TRUE(app_->updateTaskSession(QStringLiteral("T-1"), sid, start, start.addSecs(3600)));
  EXPECT_EQ(task("T-1")->trackedSeconds, 4200);
  app_->removeTaskSession(QStringLiteral("T-1"), sid);
  EXPECT_EQ(task("T-1")->trackedSeconds, 600);
  EXPECT_FALSE(app_->addTaskSession(QStringLiteral("T-1"), start, start)) << "an empty session is refused";
}

TEST_F(LocalFeatures, StoppingTheTimerRecordsASession) {
  addLocal(QStringLiteral("T-1"));
  Task t = *task(QStringLiteral("T-1"));
  t.timerStartedAt = QDateTime::currentDateTime().addSecs(-120);
  app_->tasks()->upsert(t);
  app_->stopTaskTimer(QStringLiteral("T-1"));
  ASSERT_EQ(task("T-1")->local.sessions.size(), 1);
  EXPECT_GE(task("T-1")->trackedSeconds, 119);
  EXPECT_EQ(task("T-1")->trackedSeconds, heap::local::sessions::total(task("T-1")->local.sessions));
}

TEST(LocalMigration, AV11TimerTotalBecomesOneUndatedSession) {
  QJsonObject task{{QStringLiteral("id"), QStringLiteral("T-1")},
                   {QStringLiteral("title"), QStringLiteral("x")},
                   {QStringLiteral("status"), QStringLiteral("todo")},
                   {QStringLiteral("trackedSeconds"), 5400}};
  QJsonObject root{{QStringLiteral("schemaVersion"), 11},
                   {QStringLiteral("profiles"),
                    QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("p")}, {QStringLiteral("tasks"), QJsonArray{task}}}}}};
  ASSERT_TRUE(heap::state::migrateState(root, 11));
  const QJsonObject migrated =
      root[QStringLiteral("profiles")].toArray().at(0).toObject()[QStringLiteral("tasks")].toArray().at(0).toObject();
  const Task t = heap::state::taskFromJson(migrated);
  EXPECT_EQ(t.trackedSeconds, 5400);
  ASSERT_EQ(t.local.sessions.size(), 1);
  EXPECT_FALSE(t.local.sessions.first().start.isValid());
  EXPECT_EQ(heap::local::sessions::total(t.local.sessions), 5400);
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QApplication qapp(argc, argv);
  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if(!appData.isEmpty()) {
    QFile::remove(appData + QStringLiteral("/state.json"));
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST_F(LocalFeatures, ABlockingLoopIsRefused) {
  // IDIOT-TASKS-14.
  addLocal(QStringLiteral("B-1"));
  addLocal(QStringLiteral("B-2"));
  addLocal(QStringLiteral("B-3"));
  ASSERT_TRUE(app_->addBlockLink(QStringLiteral("B-1"), QStringLiteral("B-2"), true));
  ASSERT_TRUE(app_->addBlockLink(QStringLiteral("B-2"), QStringLiteral("B-3"), true));
  QSignalSpy toasts(app_.get(), &AppController::toast);
  EXPECT_FALSE(app_->addBlockLink(QStringLiteral("B-3"), QStringLiteral("B-1"), true));
  EXPECT_EQ(toasts.count(), 1);
  EXPECT_TRUE(task("B-3")->links.isEmpty());
}

// ── APP-318: my merge request's CI went red ──

TEST_F(LocalFeatures, CiTurningRedIsANotificationOnce) {
  ExternalTask mr = issue("7");
  mr.externalId = QStringLiteral("!7");
  mr.title = QStringLiteral("cache the build");
  mr.status = QStringLiteral("MR open");
  QJsonObject d;
  d.insert(QStringLiteral("kind"), QStringLiteral("mr"));
  d.insert(QStringLiteral("state"), QStringLiteral("opened"));
  d.insert(QStringLiteral("pipeline"), QStringLiteral("running"));
  mr.details = d;
  QStringList said;
  QObject::connect(app_.get(),
                   &AppController::notification,
                   app_.get(),
                   [&](const QString& title, const QString& body, const QString& kind, const QString&) {
                     if(kind == QLatin1String("ci")) {
                       said << title + QStringLiteral(" | ") + body;
                     }
                   });
  merge({mr});
  EXPECT_TRUE(said.isEmpty()) << "a first sync is not news";
  d.insert(QStringLiteral("pipeline"), QStringLiteral("failed"));
  mr.details = d;
  merge({mr});
  ASSERT_EQ(said.size(), 1);
  EXPECT_TRUE(said.first().contains(QStringLiteral("cache the build")));
  merge({mr});
  EXPECT_EQ(said.size(), 1) << "failed again is not news";
  // The card and the document read it.
  const QString id = app_->tasks()->items().last().id;
  EXPECT_EQ(app_->ciOf(id).value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
}

// ── dogfood 2026-10-20: CI notifications that actually come ──

namespace {

struct CiSaid {
  QString title;
  QString body;
  QString kind;
  QString route;
};

QVector<CiSaid>* listenCi(AppController* app, QVector<CiSaid>* out) {
  QObject::connect(
      app, &AppController::notification, app, [out](const QString& title, const QString& body, const QString& kind, const QString& route) {
        if(kind.startsWith(QLatin1String("ci"))) {
          out->append({title, body, kind, route});
        }
      });
  return out;
}

ExternalTask myMr(const QString& number, const QString& pipeline, const QString& sha = QStringLiteral("abc")) {
  ExternalTask mr;
  mr.providerId = QStringLiteral("github");
  mr.externalId = QStringLiteral("!") + number;
  mr.url = QStringLiteral("https://github.com/acme/app/pull/") + number;
  mr.title = QStringLiteral("change ") + number;
  mr.status = QStringLiteral("MR open");
  mr.project = QStringLiteral("acme/app");
  QJsonObject d;
  d.insert(QStringLiteral("kind"), QStringLiteral("mr"));
  d.insert(QStringLiteral("state"), QStringLiteral("opened"));
  d.insert(QStringLiteral("pipeline"), pipeline);
  d.insert(QStringLiteral("ciSha"), sha);
  mr.details = d;
  return mr;
}

}  // namespace

TEST_F(LocalFeatures, CiPassingAfterARunIsAQuietNotification) {
  QVector<CiSaid> said;
  listenCi(app_.get(), &said);
  merge({myMr("7", "running")});
  merge({myMr("7", "success")});
  ASSERT_EQ(said.size(), 1);
  EXPECT_EQ(said.first().kind, QStringLiteral("ciPassed")) << "no sound for good news";
  EXPECT_EQ(said.first().title, QStringLiteral("CI passed"));
  EXPECT_TRUE(said.first().route.startsWith(QStringLiteral("ciPassed:"))) << "a click opens the card";
  // Off in Settings: quiet.
  QJsonObject settings = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object();
  settings.insert(QStringLiteral("notifications"), QJsonObject{{QStringLiteral("ciPassed"), false}});
  app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  merge({myMr("7", "running", QStringLiteral("def"))});
  merge({myMr("7", "success", QStringLiteral("def"))});
  EXPECT_EQ(said.size(), 1);
}

TEST_F(LocalFeatures, CiIsAnnouncedOnceAcrossARestart) {
  QVector<CiSaid> said;
  listenCi(app_.get(), &said);
  merge({myMr("7", "running"), myMr("8", "running")});
  merge({myMr("7", "failed"), myMr("8", "running")});
  ASSERT_EQ(said.size(), 1);
  // A restart: the same failed run is not news again.
  app_.reset();
  app_ = std::make_unique<::AppController>();
  app_->setLanguage(QStringLiteral("en"));
  QJsonObject settings;
  settings.insert(QStringLiteral("integrations"),
                  QJsonObject{{QStringLiteral("github"), QJsonObject{{QStringLiteral("repo"), QStringLiteral("acme/app")}}}});
  app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  said.clear();
  listenCi(app_.get(), &said);
  // Meanwhile, while the app was closed, !8 failed and !9 passed a new run.
  merge({myMr("7", "failed"), myMr("8", "failed"), myMr("9", "running")});
  ASSERT_EQ(said.size(), 1) << "one for the run that finished while closed";
  EXPECT_TRUE(said.first().body.contains(QStringLiteral("!8")));
  EXPECT_FALSE(said.first().body.contains(QStringLiteral("!7")));
  said.clear();
  merge({myMr("7", "failed"), myMr("8", "failed"), myMr("9", "running")});
  EXPECT_TRUE(said.isEmpty()) << "and nothing twice";
}

TEST_F(LocalFeatures, TheFirstSyncAfterLaunchIsOneSummary) {
  // Seen before (ci-seen.json from an earlier session), finished meanwhile.
  QVector<heap::ci::Observation> before;
  for(const QString& n : {QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9")}) {
    heap::ci::Observation o;
    o.key = heap::ci::keyOf(QStringLiteral("https://github.com/acme/app/pull/") + n);
    o.run = QStringLiteral("abc");
    o.state = QStringLiteral("running");
    before.append(o);
  }
  app_->observeCiAt(before, QStringLiteral("earlier"), QDateTime::currentDateTime());
  QVector<CiSaid> said;
  listenCi(app_.get(), &said);
  merge({myMr("7", "failed"), myMr("8", "failed"), myMr("9", "success")});
  ASSERT_EQ(said.size(), 1) << "no storm";
  EXPECT_EQ(said.first().title, QStringLiteral("CI while you were away: 2 failed, 1 passed"));
  EXPECT_EQ(said.first().kind, QStringLiteral("ci"));
}

TEST_F(LocalFeatures, CiOfAReviewFoldedIntoItsIssueIsNewsToo) {
  // Jira + GitLab: the merge request shows on its Jira issue (APP-293), and
  // the CI notification used to stop at the fold.
  Task issue;
  issue.id = QStringLiteral("J-1");
  issue.title = QStringLiteral("the issue");
  issue.status = QStringLiteral("todo");
  issue.priority = QStringLiteral("P2");
  issue.externalProvider = QStringLiteral("jira");
  issue.externalId = QStringLiteral("PROJ-1");
  app_->tasks()->upsert(issue);
  QVector<CiSaid> said;
  listenCi(app_.get(), &said);
  ExternalTask mr = myMr("7", "running");
  mr.details.insert(QStringLiteral("sourceBranch"), QStringLiteral("PROJ-1-cache"));
  merge({mr});
  mr.details.insert(QStringLiteral("pipeline"), QStringLiteral("failed"));
  merge({mr});
  ASSERT_EQ(said.size(), 1);
  EXPECT_EQ(said.first().kind, QStringLiteral("ci"));
  EXPECT_TRUE(said.first().route.endsWith(QStringLiteral("J-1"))) << "a click opens the issue: " << said.first().route.toStdString();
}

TEST_F(LocalFeatures, CiOfAMergeRequestIOnlyReviewIsQuiet) {
  QVector<CiSaid> said;
  listenCi(app_.get(), &said);
  ExternalTask mr = myMr("7", "running");
  mr.details.insert(QStringLiteral("mine"), false);
  merge({mr});
  mr.details.insert(QStringLiteral("pipeline"), QStringLiteral("failed"));
  merge({mr});
  EXPECT_TRUE(said.isEmpty());
}
