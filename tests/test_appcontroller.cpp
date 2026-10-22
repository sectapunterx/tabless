// Coverage for AppController's non-selection public logic: the rebindable
// shortcut catalog (set/reset/conflict/normalize), scheduleTask + its
// read-back, localized humanDate/shortDate, and the QVariant wrappers over
// the text/chrono helpers. Boots headless exactly like test_selection /
// test_notes: offscreen QPA + QStandardPaths test mode so nothing touches
// the real state.json.

#include "AppController.h"
#include "FakeHttpServer.h"
#include "Models.h"

#include "cal/OutlookDesktop.h"
#include "cal/OutlookWatch.h"
#include "git/BranchTaskMatcher.h"
#include "integrations/AutoSync.h"
#include "integrations/CommentCache.h"
#include "integrations/IntegrationTypes.h"
#include "integrations/StatusTime.h"
#include "integrations/TrackerEstimate.h"
#include "local/Effective.h"
#include "recap/WeeklyRecap.h"
#include "recur/RecurrenceEngine.h"
#include "storage/Attachments.h"
#include "text/LocaleFormat.h"
#include "views/UiScale.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QVector>

#include <gtest/gtest.h>

namespace {

Task mkTask(const QString& id, const QString& title) {
  Task t;
  t.id = id;
  t.title = title;
  t.status = QStringLiteral("todo");
  t.priority = QStringLiteral("P2");
  return t;
}

}  // namespace

class AppControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->tasks()->reset({});
    app_->events()->reset({});
    app_->setLanguage(QStringLiteral("en"));
    // AppController persists shortcut rebinds to the test-mode state.json, which
    // a prior test in this process may have written; restore the catalog to
    // defaults so every shortcut test starts from a known baseline. The
    // shortcut tests below were written against the Vim keymap; the Classic
    // ones say so.
    app_->setKeymapScheme(QStringLiteral("vim"));
    app_->resetAllShortcuts();
  }

  void TearDown() override {
    app_.reset();
  }

  // Integration config lives inside the appSettingsJson blob; these keep the
  // tests to the setting they care about.
  void writeIntegrationConfig(const QString& providerId, const QJsonObject& cfg) {
    QJsonObject settings = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object();
    QJsonObject integrations = settings.value(QStringLiteral("integrations")).toObject();
    integrations.insert(providerId, cfg);
    settings.insert(QStringLiteral("integrations"), integrations);
    app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  }

  QJsonObject readIntegrationConfig(const QString& providerId) const {
    return QJsonDocument::fromJson(app_->appSettingsJson().toUtf8())
        .object()
        .value(QStringLiteral("integrations"))
        .toObject()
        .value(providerId)
        .toObject();
  }

  std::unique_ptr<AppController> app_;
};

// ─── Weekly recap (WEAK PECAP) ────────────────────────────────────────

// Every column move is logged whatever made it, and the recap lists last
// week's net moves grouped by from -> to. A task moved and moved back is left
// out. "Last week" relative to a day a week from now is this week.
TEST_F(AppControllerTest, WeeklyRecapGroupsLastWeeksMoves) {
  app_->tasks()->reset({mkTask(QStringLiteral("R-1"), QStringLiteral("one")),
                        mkTask(QStringLiteral("R-2"), QStringLiteral("two")),
                        mkTask(QStringLiteral("R-3"), QStringLiteral("three"))});
  app_->moveTaskTo(QStringLiteral("R-1"), QStringLiteral("prog"), QString());
  app_->moveTaskTo(QStringLiteral("R-2"), QStringLiteral("prog"), QString());
  app_->moveTaskTo(QStringLiteral("R-3"), QStringLiteral("prog"), QString());
  app_->moveTaskTo(QStringLiteral("R-3"), QStringLiteral("todo"), QString());
  app_->moveTaskTo(QStringLiteral("R-2"), QStringLiteral("review"), QString());
  EXPECT_EQ(app_->statusLog().size(), 5);

  const QVariantMap recap = app_->weeklyRecapFor(QDate::currentDate().addDays(7));
  const QVariantList groups = recap.value(QStringLiteral("groups")).toList();
  ASSERT_EQ(groups.size(), 2);
  // Board column order: todo -> prog before todo -> review.
  const QVariantMap first = groups.at(0).toMap();
  EXPECT_EQ(first.value(QStringLiteral("from")).toString(), QStringLiteral("todo"));
  EXPECT_EQ(first.value(QStringLiteral("to")).toString(), QStringLiteral("prog"));
  const QVariantList firstTasks = first.value(QStringLiteral("tasks")).toList();
  ASSERT_EQ(firstTasks.size(), 1);
  EXPECT_EQ(firstTasks.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("R-1"));
  const QVariantMap second = groups.at(1).toMap();
  EXPECT_EQ(second.value(QStringLiteral("to")).toString(), QStringLiteral("review"));
  EXPECT_FALSE(second.value(QStringLiteral("toName")).toString().isEmpty());
}

TEST_F(AppControllerTest, WeeklyRecapOnlyLooksAtLastWeek) {
  app_->tasks()->reset({mkTask(QStringLiteral("R-1"), QStringLiteral("one"))});
  app_->moveTaskTo(QStringLiteral("R-1"), QStringLiteral("prog"), QString());
  // This week's moves are next week's recap, not this week's.
  EXPECT_TRUE(app_->weeklyRecap().value(QStringLiteral("groups")).toList().isEmpty());
  EXPECT_TRUE(app_->weeklyRecapFor(QDate::currentDate().addDays(14)).value(QStringLiteral("groups")).toList().isEmpty());
}

TEST_F(AppControllerTest, WeeklyRecapSkipsDeletedTasks) {
  app_->tasks()->reset({mkTask(QStringLiteral("R-1"), QStringLiteral("one"))});
  app_->moveTaskTo(QStringLiteral("R-1"), QStringLiteral("prog"), QString());
  app_->tasks()->removeById(QStringLiteral("R-1"));
  EXPECT_TRUE(app_->weeklyRecapFor(QDate::currentDate().addDays(7)).value(QStringLiteral("groups")).toList().isEmpty());
}

// Loading a profile is not a move: a reset only remembers where tasks are.
TEST_F(AppControllerTest, AResetIsNotLoggedAsAMove) {
  const qsizetype before = app_->statusLog().size();
  Task t = mkTask(QStringLiteral("R-1"), QStringLiteral("one"));
  app_->tasks()->reset({t});
  t.status = QStringLiteral("done");
  app_->tasks()->reset({t});
  EXPECT_EQ(app_->statusLog().size(), before);
}

// ─── Tracker-true dates in the recap (dogfood 2026-10-19) ─────────────

namespace {

// A Jira card as the last pull left it, in To Do since `since`.
Task jiraCard(const QString& key, const QDateTime& since) {
  Task t = mkTask(QStringLiteral("jira-") + key, key);
  t.externalProvider = QStringLiteral("jira");
  t.externalId = key;
  t.externalUrl = QStringLiteral("https://acme.atlassian.net/browse/") + key;
  t.externalMeta.project = key.section(QChar('-'), 0, 0);
  t.externalMeta.status = QStringLiteral("To Do");
  t.externalMeta.column = QStringLiteral("todo");
  t.externalMeta.title = t.title;
  t.statusChangedAt = since;
  return t;
}

heap::integrations::ExternalTask jiraIssue(const QString& key, const QString& status, const QString& category) {
  heap::integrations::ExternalTask e;
  e.providerId = QStringLiteral("jira");
  e.externalId = key;
  e.url = QStringLiteral("https://acme.atlassian.net/browse/") + key;
  e.title = key;
  e.status = status;
  e.statusCategory = category;
  e.project = key.section(QChar('-'), 0, 0);
  return e;
}

QVector<StatusChange> logOf(const QVector<StatusChange>& log, const QString& taskId) {
  QVector<StatusChange> out;
  for(const StatusChange& c : log) {
    if(c.taskId == taskId) {
      out.append(c);
    }
  }
  return out;
}

}  // namespace

// The owner's case: moved to Review in Jira on Friday, pulled on Monday (auto
// sync is off by default). The move used to be stamped with the pull.
TEST_F(AppControllerTest, ALatePullDatesAMoveByTheTracker_NotByThePull) {
  const QDateTime now = QDateTime::currentDateTime();
  const QDateTime friday = now.addDays(-3);
  app_->tasks()->reset({jiraCard(QStringLiteral("LATE-1"), now.addDays(-20))});
  heap::integrations::ExternalTask e = jiraIssue(QStringLiteral("LATE-1"), QStringLiteral("In Review"), QStringLiteral("indeterminate"));
  e.updatedAt = friday;
  e.statusCategoryChangedAt = now.addDays(-10);  // went In Progress then, same kind since
  const auto stats = app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e});

  const Task& t = app_->tasks()->items().at(0);
  EXPECT_EQ(t.status, QStringLiteral("review"));
  EXPECT_EQ(t.statusChangedAt, friday);
  const QVector<StatusChange> log = logOf(app_->statusLog(), t.id);
  ASSERT_EQ(log.size(), 1);
  EXPECT_EQ(log.at(0).at, friday);
  // And the tracker's history is asked for the exact time.
  EXPECT_TRUE(stats.statusTimesWanted.contains(QStringLiteral("LATE-1")));
}

// The history's answer puts the card, its log entry and its history event at
// the real time — and repairs a date an earlier build stamped with "now".
TEST_F(AppControllerTest, TheTrackersHistoryRepairsTheStampOfTheMove) {
  const QDateTime now = QDateTime::currentDateTime();
  app_->tasks()->reset({jiraCard(QStringLiteral("FIX-1"), now.addDays(-20))});
  // As the old build pulled it: no tracker time at all, so stamped "now".
  const auto e = jiraIssue(QStringLiteral("FIX-1"), QStringLiteral("In Review"), QString());
  app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e});
  const QString id = QStringLiteral("jira-FIX-1");
  const QDateTime wrong = app_->tasks()->items().at(0).statusChangedAt;
  ASSERT_GE(wrong, now.addSecs(-5));

  const QDateTime moved = now.addDays(-3);
  heap::integrations::ExternalStatusChange c;
  c.externalId = QStringLiteral("FIX-1");
  c.from = QStringLiteral("In Progress");
  c.to = QStringLiteral("In Review");
  c.at = moved;
  EXPECT_EQ(app_->applyTrackerStatusChanges(QStringLiteral("jira"), {c}), 1);

  const Task& t = app_->tasks()->items().at(0);
  EXPECT_EQ(t.statusChangedAt, moved);
  const QVector<StatusChange> log = logOf(app_->statusLog(), id);
  ASSERT_EQ(log.size(), 1);
  EXPECT_EQ(log.at(0).at, moved);
  const QVariantList history = app_->taskHistory(id);  // newest first
  ASSERT_FALSE(history.isEmpty());
  EXPECT_EQ(history.at(0).toMap().value(QStringLiteral("at")).toDateTime(), moved);

  // Known now: the next pull neither asks again nor moves the date.
  const auto again = app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e});
  EXPECT_FALSE(again.statusTimesWanted.contains(QStringLiteral("FIX-1")));
  EXPECT_EQ(app_->tasks()->items().at(0).statusChangedAt, moved);

  // The recap of that week names it; this week's does not.
  const QVariantMap week = app_->weeklyRecapFor(moved.date().addDays(7));
  bool named = false;
  for(const QVariant& g : week.value(QStringLiteral("groups")).toList()) {
    for(const QVariant& task : g.toMap().value(QStringLiteral("tasks")).toList()) {
      named = named || task.toMap().value(QStringLiteral("id")).toString() == id;
    }
  }
  EXPECT_EQ(named, heap::recap::weekStart(moved.date()) != heap::recap::weekStart(now.date()));
}

// A column the user picked here keeps the time they picked it; and an answer
// for a status the issue has left since is not applied.
TEST_F(AppControllerTest, TheTrackersHistoryLeavesAPlacedCardAndAStaleAnswerAlone) {
  const QDateTime now = QDateTime::currentDateTime();
  Task placed = jiraCard(QStringLiteral("KEEP-1"), now.addDays(-1));
  placed.status = QStringLiteral("blocked");  // the user's column, not the tracker's
  app_->tasks()->reset({placed, jiraCard(QStringLiteral("KEEP-2"), now.addDays(-1))});

  heap::integrations::ExternalStatusChange keep;
  keep.externalId = QStringLiteral("KEEP-1");
  keep.to = QStringLiteral("To Do");
  keep.at = now.addDays(-30);
  heap::integrations::ExternalStatusChange stale;
  stale.externalId = QStringLiteral("KEEP-2");
  stale.to = QStringLiteral("Done");
  stale.at = now.addDays(-30);
  app_->applyTrackerStatusChanges(QStringLiteral("jira"), {keep, stale});

  EXPECT_EQ(app_->tasks()->items().at(0).statusChangedAt, now.addDays(-1));
  EXPECT_EQ(app_->tasks()->items().at(1).statusChangedAt, now.addDays(-1));
  // KEEP-1 still learnt when its issue entered To Do; KEEP-2 learnt nothing.
  EXPECT_TRUE(app_->tasks()->items().at(0).externalMeta.details.contains(QStringLiteral("statusSince")));
  EXPECT_FALSE(app_->tasks()->items().at(1).externalMeta.details.contains(QStringLiteral("statusSince")));
}

// A Done ticket pulled for the first time closed when Jira says, not today:
// it used to land in this week's "closed".
TEST_F(AppControllerTest, ANewlyPulledDoneTicketClosedWhenTheTrackerSays) {
  const QDateTime now = QDateTime::currentDateTime();
  app_->tasks()->reset({});
  heap::integrations::ExternalTask e = jiraIssue(QStringLiteral("OLD-1"), QStringLiteral("Готово"), QStringLiteral("done"));
  e.statusCategoryChangedAt = now.addDays(-40);
  e.updatedAt = now.addDays(-2);  // a comment since
  e.createdAt = now.addDays(-60);
  app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e});
  ASSERT_EQ(app_->tasks()->rowCount(), 1);
  const Task& t = app_->tasks()->items().at(0);
  // An unmapped status follows its kind: a Done one is Done.
  EXPECT_EQ(t.status, QStringLiteral("done"));
  EXPECT_EQ(t.statusChangedAt, now.addDays(-40));
  for(const QVariant& row : app_->weekFacts(now.date()).value(QStringLiteral("closed")).toList()) {
    EXPECT_NE(row.toMap().value(QStringLiteral("id")).toString(), t.id);
  }
  // Nor did it "come in" this week: Jira made it two months ago.
  for(const QVariant& row : app_->weekFacts(now.date()).value(QStringLiteral("created")).toList()) {
    EXPECT_NE(row.toMap().value(QStringLiteral("id")).toString(), t.id);
  }
}

// Review 2026-10-19 (B): an issue whose history never gives a usable answer
// (a status renamed since the move, a workflow the parser cannot follow) was
// asked about on every sync, and forty of them held the whole capped batch.
// A few tries, then it stays on the provisional time.
TEST_F(AppControllerTest, AnIssueTheHistoryCannotAnswerIsAskedAFewTimesOnly) {
  const QDateTime now = QDateTime::currentDateTime();
  app_->tasks()->reset({jiraCard(QStringLiteral("ODD-1"), now.addDays(-5)), jiraCard(QStringLiteral("ODD-2"), now.addDays(-5))});
  const auto e1 = jiraIssue(QStringLiteral("ODD-1"), QStringLiteral("To Do"), QStringLiteral("new"));
  const auto e2 = jiraIssue(QStringLiteral("ODD-2"), QStringLiteral("To Do"), QStringLiteral("new"));
  heap::integrations::ExternalStatusChange other;  // ODD-2 answers; ODD-1 never does
  other.externalId = QStringLiteral("ODD-2");
  other.to = QStringLiteral("Done");  // moved since: no time learnt, but an answer
  other.at = now.addDays(-1);
  for(int round = 0; round < heap::integrations::statustime::kMaxTries; ++round) {
    const auto stats = app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e1, e2});
    ASSERT_TRUE(stats.statusTimesWanted.contains(QStringLiteral("ODD-1"))) << "round " << round;
    app_->countStatusTimeTries(QStringLiteral("jira"), stats.statusTimesWanted, {other});
  }
  const auto last = app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e1, e2});
  EXPECT_FALSE(last.statusTimesWanted.contains(QStringLiteral("ODD-1"))) << "given up after the tries, kept across pulls";
  EXPECT_TRUE(last.statusTimesWanted.contains(QStringLiteral("ODD-2"))) << "an answer is not a failed try";
  // A failed request (no answer at all) counts for nobody.
  app_->countStatusTimeTries(QStringLiteral("jira"), {QStringLiteral("ODD-2")}, {});
  EXPECT_EQ(heap::integrations::statustime::tries(app_->tasks()->items().at(1).externalMeta.details, QStringLiteral("To Do")), 0);
  // A new status is a new question.
  const auto moved =
      app_->mergeExternalTasks(QStringLiteral("jira"),
                               QStringLiteral("jira-"),
                               {jiraIssue(QStringLiteral("ODD-1"), QStringLiteral("In Progress"), QStringLiteral("indeterminate")), e2});
  EXPECT_TRUE(moved.statusTimesWanted.contains(QStringLiteral("ODD-1")));
}

// (C): a ticket with its time all spent was planned at its original estimate.
TEST(TrackerEstimate, ARemainingEstimateOfZeroIsTheAnswer) {
  Task t;
  t.externalMeta.details.insert(QStringLiteral("estimate"),
                                QJsonObject{{QStringLiteral("original"), 4 * 3600}, {QStringLiteral("remaining"), 0}});
  EXPECT_EQ(heap::integrations::trackerEstimateMinutes(t), 0);
  t.externalMeta.details.insert(QStringLiteral("estimate"),
                                QJsonObject{{QStringLiteral("original"), 4 * 3600}, {QStringLiteral("remaining"), 1800}});
  EXPECT_EQ(heap::integrations::trackerEstimateMinutes(t), 30);
  // No remaining given at all: the original.
  t.externalMeta.details.insert(QStringLiteral("estimate"), QJsonObject{{QStringLiteral("original"), 4 * 3600}});
  EXPECT_EQ(heap::integrations::trackerEstimateMinutes(t), 240);
  // The user's own estimate still wins.
  t.estimateMinutes = 15;
  EXPECT_EQ(heap::integrations::planMinutes(t), 15);
}

// (E): a tracker clock a few minutes ahead made every timestamp unusable,
// and the move was stamped "now".
TEST(StatusTime, ATrackerClockAheadIsHeldToNowNotDropped) {
  namespace st = heap::integrations::statustime;
  const QDateTime now(QDate(2026, 10, 19), QTime(12, 0));
  const QDateTime ahead = now.addSecs(180);
  const QDateTime earlier = now.addDays(-2);
  EXPECT_EQ(st::provisional(QDateTime(), ahead, earlier, QDateTime(), now, false), now) << "held to now";
  EXPECT_EQ(st::provisional(earlier, ahead, QDateTime(), QDateTime(), now, true), earlier) << "a done move still exact";
  EXPECT_EQ(st::provisional(ahead, QDateTime(), QDateTime(), earlier, now, true), now);
  EXPECT_EQ(st::provisional(QDateTime(), QDateTime(), ahead, QDateTime(), now, false), now);
  EXPECT_EQ(st::provisional(QDateTime(), earlier, QDateTime(), QDateTime(), now, false), earlier);
}

// (J): the comment cache had a ticket cap only; 300 tickets of long threads
// came to ~100 MB.
TEST(CommentCache, BoundedByTextAsWellAsTickets) {
  namespace cc = heap::integrations::commentcache;
  const auto entry = [](int chars, const QString& at) {
    return QJsonObject{{QStringLiteral("comments"), QJsonArray{QJsonObject{{QStringLiteral("body"), QString(chars, QLatin1Char('x'))}}}},
                       {QStringLiteral("fetchedAt"), at}};
  };
  QJsonObject tickets;
  tickets.insert(QStringLiteral("a"), entry(600, QStringLiteral("2026-10-19T10:00:00")));
  tickets.insert(QStringLiteral("b"), entry(600, QStringLiteral("2026-10-19T09:00:00")));
  tickets.insert(QStringLiteral("c"), entry(600, QStringLiteral("2026-10-19T11:00:00")));
  EXPECT_EQ(cc::trim(tickets, 10, 1300), 1);
  EXPECT_FALSE(tickets.contains(QStringLiteral("b"))) << "the one read longest ago goes first";
  EXPECT_EQ(cc::trim(tickets, 1, 1'000'000), 1);
  EXPECT_TRUE(tickets.contains(QStringLiteral("c")));
  // One ticket alone is kept, however long.
  EXPECT_EQ(cc::trim(tickets, 10, 10), 0);
  EXPECT_EQ(tickets.size(), 1);
}

// (I): a busy Outlook (RPC_E_CALL_REJECTED) answered -1 like a closed one,
// and the watch let go and attached again every half minute.
TEST(OutlookWatchRules, BusyIsNotGone) {
  int streak = 0;
  EXPECT_TRUE(heap::cal::keepWatching(1, streak));
  for(int i = 0; i < heap::cal::kMaxBusyChecks; ++i) {
    EXPECT_TRUE(heap::cal::keepWatching(heap::cal::kExplorersBusy, streak)) << i;
  }
  EXPECT_FALSE(heap::cal::keepWatching(heap::cal::kExplorersBusy, streak)) << "busy for minutes on end";
  streak = 0;
  EXPECT_TRUE(heap::cal::keepWatching(heap::cal::kExplorersBusy, streak));
  EXPECT_TRUE(heap::cal::keepWatching(2, streak));
  EXPECT_EQ(streak, 0);
  EXPECT_FALSE(heap::cal::keepWatching(0, streak)) << "only a headless Outlook left";
  EXPECT_FALSE(heap::cal::keepWatching(heap::cal::kExplorersGone, streak));
}

// (G/H): a stop() right after start() is never lost, and retiring a watch
// does not block.
TEST(OutlookWatchRules, AStopRightAfterStartEndsTheThread) {
  for(int i = 0; i < 20; ++i) {
    auto* w = new heap::cal::OutlookWatch;
    w->start();
    EXPECT_TRUE(w->stop(5000)) << i;
    delete w;
  }
  auto* w = new heap::cal::OutlookWatch;
  w->start();
  QElapsedTimer timer;
  timer.start();
  heap::cal::OutlookWatch::retire(w);
  EXPECT_LT(timer.elapsed(), 3000);
}

TEST_F(AppControllerTest, AnUnmappedStatusFollowsItsKindInJira) {
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("jira"),
                           QStringLiteral("jira-"),
                           {jiraIssue(QStringLiteral("KIND-1"), QStringLiteral("Тестирование"), QStringLiteral("indeterminate")),
                            jiraIssue(QStringLiteral("KIND-2"), QStringLiteral("Нечто"), QStringLiteral("new"))});
  ASSERT_EQ(app_->tasks()->rowCount(), 2);
  EXPECT_EQ(app_->tasks()->items().at(0).status, QStringLiteral("prog"));
  EXPECT_EQ(app_->tasks()->items().at(1).status, QStringLiteral("todo"));
}

// The tracker's estimate is shown and planned with, never written over mine.
TEST_F(AppControllerTest, TheTrackersEstimateIsReadOnlyAndOnlyFillsAGap) {
  app_->tasks()->reset({});
  heap::integrations::ExternalTask e = jiraIssue(QStringLiteral("EST-1"), QStringLiteral("To Do"), QStringLiteral("new"));
  e.details.insert(QStringLiteral("estimate"),
                   QJsonObject{{QStringLiteral("original"), 28800}, {QStringLiteral("remaining"), 7200}, {QStringLiteral("points"), 5}});
  app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e});
  const QString id = QStringLiteral("jira-EST-1");
  const QVariantMap est = app_->trackerEstimate(id);
  EXPECT_EQ(est.value(QStringLiteral("originalMinutes")).toInt(), 480);
  EXPECT_EQ(est.value(QStringLiteral("remainingMinutes")).toInt(), 120);
  EXPECT_DOUBLE_EQ(est.value(QStringLiteral("points")).toDouble(), 5.0);
  EXPECT_EQ(app_->tasks()->items().at(0).estimateMinutes, 0);
  EXPECT_EQ(app_->taskBlockMinutes(id), 120);  // what is left of it

  Task mine = app_->tasks()->items().at(0);
  mine.estimateMinutes = 45;
  app_->tasks()->upsert(mine);
  app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e});
  EXPECT_EQ(app_->tasks()->items().at(0).estimateMinutes, 45);
  EXPECT_EQ(app_->taskBlockMinutes(id), 45);
}

// ─── Calendar subscriptions (APP-118) ─────────────────────────────────

namespace {
QByteArray feed(const QStringList& vevents) {
  QByteArray out = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n";
  for(const QString& v : vevents) {
    out += "BEGIN:VEVENT\r\n" + v.toUtf8() + "\r\nEND:VEVENT\r\n";
  }
  return out + "END:VCALENDAR\r\n";
}

const QString kSync = QStringLiteral("UID:sync-1\r\nSUMMARY:Sprint sync\r\nDTSTART:20261006T110000\r\nDTEND:20261006T113000");
const QString kRetro = QStringLiteral("UID:retro-1\r\nSUMMARY:Retro\r\nDTSTART:20261009T150000\r\nDTEND:20261009T160000");
}  // namespace

// A fetched feed becomes the subscription's events; the next fetch replaces
// them, so a meeting cancelled in Outlook disappears here too.
TEST_F(AppControllerTest, ASubscriptionFeedIsMirroredAndReplaced) {
  EXPECT_EQ(app_->applyCalendarSubscriptionFeed(QStringLiteral("t1"), feed({kSync, kRetro})), 2);
  EXPECT_EQ(app_->events()->rowCount(), 2);
  const QVariantMap sync = app_->eventById(QStringLiteral("sub:t1:sync-1"));
  EXPECT_EQ(sync.value(QStringLiteral("title")).toString(), QStringLiteral("Sprint sync"));
  EXPECT_TRUE(app_->isSubscriptionEvent(QStringLiteral("sub:t1:sync-1")));

  EXPECT_EQ(app_->applyCalendarSubscriptionFeed(QStringLiteral("t1"), feed({kSync})), 1);
  EXPECT_EQ(app_->events()->rowCount(), 1);
  EXPECT_LT(app_->events()->indexOfId(QStringLiteral("sub:t1:retro-1")), 0);
}

// The user's own events are never touched by a feed.
TEST_F(AppControllerTest, ASubscriptionFeedLeavesOwnEventsAlone) {
  QVariantMap draft = app_->newEventDraft(9, QDate(2026, 10, 6));
  draft["title"] = QStringLiteral("Mine");
  app_->saveEvent(draft);
  const QString mine = draft.value("id").toString();
  app_->applyCalendarSubscriptionFeed(QStringLiteral("t1"), feed({kSync}));
  app_->applyCalendarSubscriptionFeed(QStringLiteral("t1"), feed({}));
  EXPECT_GE(app_->events()->indexOfId(mine), 0);
  EXPECT_EQ(app_->events()->rowCount(), 1);
}

// Outlook owns these meetings: every write path refuses them and says why.
TEST_F(AppControllerTest, SubscriptionEventsAreReadOnly) {
  app_->applyCalendarSubscriptionFeed(QStringLiteral("t1"), feed({kSync}));
  const QString id = QStringLiteral("sub:t1:sync-1");
  QSignalSpy toasts(app_.get(), &AppController::toast);

  QVariantMap draft = app_->eventById(id);
  draft["title"] = QStringLiteral("Renamed");
  app_->saveEvent(draft);
  app_->updateEvent(id, 14, 15, QDate(2026, 10, 6));
  app_->deleteEvent(id);
  app_->moveOccurrence(draft, 1.0, QStringLiteral("this"));

  const QVariantMap after = app_->eventById(id);
  EXPECT_EQ(after.value(QStringLiteral("title")).toString(), QStringLiteral("Sprint sync"));
  EXPECT_DOUBLE_EQ(after.value(QStringLiteral("start")).toDouble(), 11.0);
  EXPECT_EQ(toasts.count(), 4);
}

TEST_F(AppControllerTest, NotACalendarIsAnError) {
  QString error;
  EXPECT_EQ(app_->applyCalendarSubscriptionFeed(QStringLiteral("t1"), QByteArray("<html>login</html>"), &error), -1);
  EXPECT_FALSE(error.isEmpty());
  EXPECT_EQ(app_->events()->rowCount(), 0);
}

TEST_F(AppControllerTest, RemovingASubscriptionTakesItsEvents) {
  app_->applyCalendarSubscriptionFeed(QStringLiteral("t1"), feed({kSync}));
  app_->applyCalendarSubscriptionFeed(QStringLiteral("t2"), feed({kRetro}));
  app_->removeCalendarSubscription(QStringLiteral("t1"));
  EXPECT_LT(app_->events()->indexOfId(QStringLiteral("sub:t1:sync-1")), 0);
  EXPECT_GE(app_->events()->indexOfId(QStringLiteral("sub:t2:retro-1")), 0);
}

// Outlook occurrences go through the same replace-and-read-only path.
TEST_F(AppControllerTest, DesktopOutlookEventsAreMirroredReadOnly) {
  heap::cal::OutlookItem it;
  it.key = QStringLiteral("G1-202610061100");
  it.subject = QStringLiteral("Sprint sync");
  it.start = QDateTime(QDate(2026, 10, 6), QTime(11, 0));
  it.end = QDateTime(QDate(2026, 10, 6), QTime(11, 30));
  EXPECT_EQ(app_->replaceSubscriptionEvents(QStringLiteral("outlook"), heap::cal::outlookEvents({it}, QStringLiteral("outlook"))), 1);
  const QString id = QStringLiteral("sub:outlook:G1-202610061100");
  ASSERT_GE(app_->events()->indexOfId(id), 0);
  app_->deleteEvent(id);
  EXPECT_GE(app_->events()->indexOfId(id), 0);
  EXPECT_EQ(app_->replaceSubscriptionEvents(QStringLiteral("outlook"), {}), 0);
  EXPECT_LT(app_->events()->indexOfId(id), 0);
}

TEST_F(AppControllerTest, ABadLinkIsRefusedBeforeAnythingIsStored) {
  const QVariantMap r = app_->addCalendarSubscription(QStringLiteral("Work"), QStringLiteral("ftp://example.com/cal.ics"), 15);
  EXPECT_FALSE(r.value(QStringLiteral("ok")).toBool());
  EXPECT_TRUE(app_->calendarSubscriptions().isEmpty());
}

// ─── Shortcut catalog ─────────────────────────────────────────────────

TEST_F(AppControllerTest, SetShortcutSwapsConflictingOwner) {
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("task.new"), QStringLiteral("Ctrl+K")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString("Ctrl+K"));
  // palette.open previously owned Ctrl+K → freed.
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("palette.open")), QString());
}

TEST_F(AppControllerTest, SetShortcutUnknownIdReturnsFalse) {
  EXPECT_FALSE(app_->setShortcut(QStringLiteral("no.such.id"), QStringLiteral("Ctrl+K")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString("Ctrl+N"));
}

TEST_F(AppControllerTest, SetShortcutSameSequenceIsNoopTrue) {
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("task.new"), QStringLiteral("ctrl+n")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString("Ctrl+N"));
  // palette.open untouched.
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("palette.open")), QString("Ctrl+K"));
}

TEST_F(AppControllerTest, SetShortcutEmptyClearsWithoutSwapping) {
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("task.new"), QString()));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString());
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("palette.open")), QString("Ctrl+K"));
}

TEST_F(AppControllerTest, NormalizeTrimsAndCaseFolds) {
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("task.new"), QStringLiteral("  ctrl+alt+j ")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString("Ctrl+Alt+J"));
}

TEST_F(AppControllerTest, ShortcutForUnknownIsEmpty) {
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("does.not.exist")), QString());
  EXPECT_EQ(app_->defaultShortcutFor(QStringLiteral("does.not.exist")), QString());
  EXPECT_EQ(app_->defaultShortcutFor(QStringLiteral("palette.open")), QString("Ctrl+K"));
}

TEST_F(AppControllerTest, FindShortcutConflict) {
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("task.new"), QStringLiteral("Ctrl+K")), QString("palette.open"));
  // self excluded
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("palette.open"), QStringLiteral("Ctrl+K")), QString());
  // normalized before compare
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("task.new"), QStringLiteral("ctrl+k")), QString("palette.open"));
  // unbound combo
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("task.new"), QStringLiteral("Ctrl+Alt+Shift+F12")), QString());
  // empty
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("task.new"), QString()), QString());
}

// SHELL-5 (2026-09-30-1): a space typed as the key is the Space key, and
// something that is no key sequence never reads as "unbind".
TEST_F(AppControllerTest, SpaceTypedAsTheKeyIsSpace) {
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("task.new"), QStringLiteral("Ctrl+Shift+ ")), QString("quick-capture"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("task.new"), QStringLiteral(" ")), QString("board.toggleSelect"));
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("task.new"), QStringLiteral("Ctrl+Alt+ ")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString("Ctrl+Alt+Space"));
  EXPECT_FALSE(app_->setShortcut(QStringLiteral("task.new"), QStringLiteral("Ctrl+Shift+")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString("Ctrl+Alt+Space"));
}

// SHELL-4 (2026-09-30-1): the keys QML wires next to the catalog (the
// board's arrows, Enter, Menu, Ctrl+arrows, the notes' Ctrl+Shift+M) count as
// taken, and a rebind onto one is refused instead of killing both. Ctrl+P is
// a catalogue entry since APP-279 (palette.open.alt), so it is swapped.
TEST_F(AppControllerTest, BuiltinKeysAreConflicts) {
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+P")), QString("palette.open.alt"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("Up")), QString("board.cursorUp"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("board.archive"), QStringLiteral("Menu")), QString("board.cardMenu"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+Shift+M")), QString("builtin.notesMode"));
  EXPECT_FALSE(app_->shortcutLabel(QStringLiteral("builtin.notesMode")).isEmpty());
  // A view where the key is not live.
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("cal.prev"), QStringLiteral("Up")), QString());

  const QString before = app_->shortcutFor(QStringLiteral("theme.toggle"));
  QSignalSpy toasts(app_.get(), &AppController::toast);
  EXPECT_FALSE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("Up")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("theme.toggle")), before);
  ASSERT_EQ(toasts.count(), 1);
  EXPECT_EQ(toasts.last().at(1).toString(), QStringLiteral("warning"));
  // Ctrl+P is an ordinary entry: it can be taken (and is freed) or cleared.
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+P")));
  EXPECT_TRUE(app_->shortcutFor(QStringLiteral("palette.open.alt")).isEmpty());
  app_->resetShortcut(QStringLiteral("palette.open.alt"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("palette.open.alt")), QString("Ctrl+P"));
}

// The 0.8.0 keymap (keymap.md, APP-272): Vim-based defaults, a second key of
// an action named like the action, two-key sequences.
TEST_F(AppControllerTest, TheVimKeymapIsTheDefault) {
  const QList<QPair<const char*, const char*>> expected = {{"task.done", "D"},
                                                           {"view.board", "G, B"},
                                                           {"view.timeline", "G, L"},
                                                           {"view.calendar", "G, C"},
                                                           {"section.today.alt", "G, T"},
                                                           {"section.knowledge.alt", "G, N"},
                                                           {"task.openExternal", "G, X"},
                                                           {"task.schedule", "S"},
                                                           {"task.due", "Shift+S"},
                                                           {"task.priority0", "1"},
                                                           {"task.priority3", "4"},
                                                           {"task.timer", "T"},
                                                           {"task.copyId", "Y, Y"},
                                                           {"task.copyBranch", "Y, B"},
                                                           {"task.copyLink", "Y, L"},
                                                           {"task.createBranch", "C, B"},
                                                           {"board.ciPopover", "C, I"},
                                                           {"board.collapseColumn", "Z, A"},
                                                           {"view.week", "Z, W"},
                                                           {"view.month", "Z, M"},
                                                           {"cal.today", "0"},
                                                           {"cal.prev", "["},
                                                           {"cal.next", "]"},
                                                           {"cursor.first", "G, G"},
                                                           {"cursor.last", "Shift+G"},
                                                           {"nav.back", "Ctrl+O"},
                                                           {"nav.forward", "Ctrl+I"},
                                                           {"hotkeys.open", "Ctrl+/"},
                                                           {"hotkeys.open.alt", "?"},
                                                           {"palette.open.alt", "Ctrl+P"},
                                                           {"palette.commands", ":"},
                                                           {"search.focus.alt", "/"},
                                                           {"undo.alt", "U"},
                                                           {"redo.alt", "Ctrl+R"},
                                                           {"savedView.1", "Ctrl+4"},
                                                           {"savedView.6", "Ctrl+9"},
                                                           {"savedView.1.alt", "G, 1"},
                                                           {"cal.goToDate", ""}};
  for(const auto& [id, seq] : expected) {
    EXPECT_EQ(app_->defaultShortcutFor(QString::fromLatin1(id)), QString::fromLatin1(seq)) << id;
  }
  // No two actions share a key, and no key is the start of another's.
  const QVariantList cat = app_->shortcuts();
  for(const QVariant& a : cat) {
    const QString id = a.toMap().value(QStringLiteral("id")).toString();
    const QString seq = a.toMap().value(QStringLiteral("sequence")).toString();
    if(!seq.isEmpty()) {
      EXPECT_TRUE(app_->findShortcutConflict(id, seq).isEmpty()) << id.toStdString() << " " << seq.toStdString();
    }
    EXPECT_FALSE(a.toMap().value(QStringLiteral("label")).toString().startsWith(QStringLiteral("shortcut."))) << id.toStdString();
  }
  // A second key reads as its action.
  EXPECT_EQ(app_->shortcutLabel(QStringLiteral("undo.alt")), app_->shortcutLabel(QStringLiteral("undo")));
  EXPECT_EQ(app_->shortcutLabel(QStringLiteral("savedView.2.alt")), app_->shortcutLabel(QStringLiteral("savedView.2")));
}

// The Classic keymap (dogfood 2026-10-19, the default of a new install):
// arrows, Enter, Del, F2 and Ctrl / Alt chords — no bare letter and no
// two-key sequence is needed for anything.
TEST_F(AppControllerTest, TheClassicKeymapNeedsNoLetterSequences) {
  app_->setKeymapScheme(QStringLiteral("classic"));
  ASSERT_EQ(app_->keymapScheme(), QStringLiteral("classic"));
  const QList<QPair<const char*, const char*>> expected = {{"board.cursorDown", "Down"},
                                                           {"board.cursorLeft", "Left"},
                                                           {"board.moveUp", "Ctrl+Up"},
                                                           {"board.open", "Return"},
                                                           {"selection.deleteSel", "Del"},
                                                           {"notes.delete", "Del"},
                                                           {"editor.deleteBlock", "Ctrl+Shift+K"},
                                                           {"task.rename", "F2"},
                                                           {"notes.rename", "F2"},
                                                           {"task.done", "Ctrl+D"},
                                                           {"cursor.first", "Home"},
                                                           {"cursor.last", "End"},
                                                           {"cursor.pageDown", "PgDown"},
                                                           {"view.board", "Alt+B"},
                                                           {"palette.open", "Ctrl+K"},
                                                           {"redo.alt", "Ctrl+Y"},
                                                           {"cal.goToDate", "Ctrl+G"},
                                                           {"undo.alt", ""},
                                                           {"view.board", "Alt+B"}};
  for(const auto& [id, seq] : expected) {
    EXPECT_EQ(app_->defaultShortcutFor(QString::fromLatin1(id)), QString::fromLatin1(seq)) << id;
  }
  const QVariantList cat = app_->shortcuts();
  for(const QVariant& a : cat) {
    const QString id = a.toMap().value(QStringLiteral("id")).toString();
    const QString seq = a.toMap().value(QStringLiteral("sequence")).toString();
    if(seq.isEmpty()) {
      continue;
    }
    EXPECT_FALSE(seq.contains(QStringLiteral(", "))) << id.toStdString() << " is a sequence: " << seq.toStdString();
    // A bare key is a named one or a sign, never a letter or a digit.
    const bool bare = !seq.contains(QStringLiteral("Ctrl+")) && !seq.contains(QStringLiteral("Alt+"));
    const QString key = seq.section(QLatin1Char('+'), -1);
    EXPECT_FALSE(bare && key.size() == 1 && key.at(0).isLetterOrNumber()) << id.toStdString() << " " << seq.toStdString();
    EXPECT_TRUE(app_->findShortcutConflict(id, seq).isEmpty())
        << id.toStdString() << " " << seq.toStdString() << " vs " << app_->findShortcutConflict(id, seq).toStdString();
  }
}

// Every action has a key in both keymaps, but for the few that have none by
// design (opened from Ctrl+K or the menus), each listed here.
TEST_F(AppControllerTest, EveryActionHasAKeyInBothKeymaps) {
  const QSet<QString> noKeyByDesign = {QStringLiteral("view.archive"),
                                       QStringLiteral("view.notes"),
                                       QStringLiteral("tweaks.open"),
                                       QStringLiteral("hotkeys.edit"),
                                       QStringLiteral("people.open"),
                                       QStringLiteral("timeMachine.open"),
                                       QStringLiteral("standup.draft"),
                                       QStringLiteral("endOfDay.open"),
                                       QStringLiteral("welcome.replay"),
                                       QStringLiteral("example.open"),
                                       QStringLiteral("savedView.7"),
                                       QStringLiteral("savedView.8"),
                                       QStringLiteral("savedView.9"),
                                       // Classic: Space marks, Shift+arrows grow the selection.
                                       QStringLiteral("selection.toggle"),
                                       QStringLiteral("selection.range"),
                                       // Vim: ":" and the date.
                                       QStringLiteral("cal.goToDate")};
  QHash<QString, QPair<bool, bool>> has;  // base id → (vim, classic)
  for(const QVariant& a : app_->shortcuts()) {
    const QString id = a.toMap().value(QStringLiteral("id")).toString();
    QString base = id;
    base.remove(QRegularExpression(QStringLiteral("\\.alt\\d*$")));
    auto& h = has[base];
    h.first = h.first || !app_->schemeShortcutFor(id, QStringLiteral("vim")).isEmpty();
    h.second = h.second || !app_->schemeShortcutFor(id, QStringLiteral("classic")).isEmpty();
  }
  for(auto it = has.constBegin(); it != has.constEnd(); ++it) {
    if(noKeyByDesign.contains(it.key())) {
      continue;
    }
    EXPECT_TRUE(it.value().first) << it.key().toStdString() << " has no Vim key";
    EXPECT_TRUE(it.value().second) << it.key().toStdString() << " has no Classic key";
  }
}

// Switching keeps the user's own keys on top; a default of the new scheme
// that one of them holds steps aside, and reset is the new scheme's key.
TEST_F(AppControllerTest, SwitchingTheKeymapKeepsTheUsersKeys) {
  ASSERT_TRUE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+D")));
  EXPECT_TRUE(app_->shortcutFor(QStringLiteral("cursor.pageDown")).isEmpty()) << "Vim's Ctrl+D was swapped off";
  QSignalSpy changed(app_.get(), &AppController::keymapSchemeChanged);
  app_->setKeymapScheme(QStringLiteral("classic"));
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("theme.toggle")), QStringLiteral("Ctrl+D"));
  EXPECT_TRUE(app_->shortcutFor(QStringLiteral("task.done")).isEmpty()) << "Classic's Ctrl+D is the user's";
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("cursor.pageDown")), QStringLiteral("PgDown"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("board.cursorDown")), QStringLiteral("Down"));
  app_->resetShortcut(QStringLiteral("task.done"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.done")), QStringLiteral("Ctrl+D"));
  EXPECT_TRUE(app_->shortcutFor(QStringLiteral("theme.toggle")).isEmpty()) << "reset takes its key back";
  app_->resetAllShortcuts();
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("theme.toggle")), QStringLiteral("Ctrl+Shift+T"));
  // An unknown scheme, or the same one, changes nothing.
  app_->setKeymapScheme(QStringLiteral("emacs"));
  app_->setKeymapScheme(QStringLiteral("classic"));
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(app_->schemeShortcutFor(QStringLiteral("task.done"), QStringLiteral("vim")), QStringLiteral("D"));
}

// The Knowledge list's delete shares Del with the tasks' and d d with the
// task's d: they are never live on the same surface.
TEST_F(AppControllerTest, KnowledgeDeleteSharesKeysWithTheTasksOnPurpose) {
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("notes.delete")), QStringLiteral("D, D"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("notes.delete.alt")), QStringLiteral("Del"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("editor.deleteBlock")), QStringLiteral("D, D"));
  EXPECT_TRUE(app_->findShortcutConflict(QStringLiteral("notes.delete"), QStringLiteral("D, D")).isEmpty());
  EXPECT_TRUE(app_->prefixShortcutConflict(QStringLiteral("editor.deleteBlock"), QStringLiteral("D, D")).isEmpty());
  // Anything app-wide still meets them.
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("D, D")), QStringLiteral("task.done"));
  EXPECT_FALSE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("D, D")));
}

// A private key handler asks the catalogue: the key as the router reads it.
TEST_F(AppControllerTest, AKeyPressMatchesItsActionInTheActiveScheme) {
  EXPECT_TRUE(app_->keyMatches(QStringLiteral("board.cursorDown"), Qt::Key_J, 0, QStringLiteral("j"), 0));
  EXPECT_FALSE(app_->keyMatches(QStringLiteral("board.cursorDown"), Qt::Key_Down, 0, QString(), 0));
  EXPECT_TRUE(app_->keyMatches(QStringLiteral("task.done"), Qt::Key_D, 0, QStringLiteral("d"), 0));
  // Russian layout: "о" is typed on the J key.
  EXPECT_TRUE(app_->keyMatches(QStringLiteral("board.cursorDown"), 0x041e, 0, QStringLiteral("о"), 0));
  app_->setKeymapScheme(QStringLiteral("classic"));
  EXPECT_FALSE(app_->keyMatches(QStringLiteral("board.cursorDown"), Qt::Key_J, 0, QStringLiteral("j"), 0));
  EXPECT_TRUE(app_->keyMatches(QStringLiteral("board.cursorDown"), Qt::Key_Down, 0, QString(), 0));
  EXPECT_TRUE(app_->keyMatches(QStringLiteral("task.done"), Qt::Key_D, Qt::ControlModifier, QString(), 0));
  EXPECT_FALSE(app_->keyMatches(QStringLiteral("task.done"), Qt::Key_D, 0, QStringLiteral("d"), 0));
}

TEST_F(AppControllerTest, KeysAreWrittenTheKeymapWay) {
  EXPECT_EQ(app_->keyText(QStringLiteral("G, B")), QStringLiteral("g b"));
  EXPECT_EQ(app_->shortcutText(QStringLiteral("task.done")), QStringLiteral("d"));
#ifndef Q_OS_MACOS
  EXPECT_EQ(app_->shortcutText(QStringLiteral("task.due")), QStringLiteral("Shift S"));
  EXPECT_EQ(app_->shortcutText(QStringLiteral("board.open")), QStringLiteral("↵"));
  EXPECT_EQ(app_->shortcutText(QStringLiteral("palette.open")), QStringLiteral("Ctrl K"));
#endif
  // A rebinding shows at once.
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("task.done"), QStringLiteral("Shift+D")));
#ifndef Q_OS_MACOS
  EXPECT_EQ(app_->shortcutText(QStringLiteral("task.done")), QStringLiteral("Shift D"));
#else
  EXPECT_EQ(app_->shortcutText(QStringLiteral("task.done")), app_->keyText(QStringLiteral("Shift+D")));
#endif
}

// "Нельзя назначить g, если есть g b" (APP-272): a prefix is refused, not
// swapped; the same sequence is swapped as any key is.
TEST_F(AppControllerTest, APrefixOfASequenceIsRefused) {
  EXPECT_FALSE(app_->prefixShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("G")).isEmpty());
  QSignalSpy toasts(app_.get(), &AppController::toast);
  EXPECT_FALSE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("G")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("view.board")), QStringLiteral("G, B"));
  ASSERT_EQ(toasts.count(), 1);
  // …and a sequence that starts with a bound single key.
  EXPECT_FALSE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("D, D")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.done")), QStringLiteral("D"));
  // The same sequence: swapped.
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("G, B")));
  EXPECT_TRUE(app_->shortcutFor(QStringLiteral("view.board")).isEmpty());
  // A sequence of the user's own, after the g it starts with is free of it.
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("view.board"), QStringLiteral("Ctrl+Alt+B")));
}

TEST_F(AppControllerTest, SystemKeysAreRefusedWithAReason) {
  EXPECT_FALSE(app_->reservedShortcutReason(QStringLiteral("Alt+F4")).isEmpty());
  EXPECT_FALSE(app_->reservedShortcutReason(QStringLiteral("Tab")).isEmpty());
  EXPECT_TRUE(app_->reservedShortcutReason(QStringLiteral("Ctrl+Shift+K")).isEmpty());
  QSignalSpy toasts(app_.get(), &AppController::toast);
  EXPECT_FALSE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("Alt+F4")));
  ASSERT_EQ(toasts.count(), 1);
  EXPECT_EQ(toasts.last().at(1).toString(), QStringLiteral("warning"));
}

// The command line (APP-267): "block p0 order" is the Blocked column, P0 and
// a word; the clauses and the tasks they find.
TEST_F(AppControllerTest, TheCommandLineReadsTheSharedLanguage) {
  Task a = mkTask(QStringLiteral("APP-105"), QStringLiteral("Order checkout times out"));
  a.status = QStringLiteral("blocked");
  a.priority = QStringLiteral("P0");
  Task b = mkTask(QStringLiteral("APP-106"), QStringLiteral("Order history"));
  b.priority = QStringLiteral("P0");
  Task c = mkTask(QStringLiteral("APP-107"), QStringLiteral("Order export"));
  c.status = QStringLiteral("blocked");
  c.priority = QStringLiteral("P0");
  c.archived = true;
  app_->tasks()->reset({a, b, c});

  const QVariantMap r = app_->commandLine(QStringLiteral("block p0 order"));
  const QVariantList tokens = r.value(QStringLiteral("tokens")).toList();
  ASSERT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens.at(0).toMap().value(QStringLiteral("clause")).toString(), QStringLiteral("status:blocked"));
  EXPECT_EQ(tokens.at(1).toMap().value(QStringLiteral("clause")).toString(), QStringLiteral("priority:P0"));
  EXPECT_EQ(r.value(QStringLiteral("text")).toString(), QStringLiteral("order"));
  EXPECT_EQ(r.value(QStringLiteral("total")).toInt(), 2);
  const QVariantList tasks = r.value(QStringLiteral("tasks")).toList();
  ASSERT_EQ(tasks.size(), 2);
  EXPECT_EQ(tasks.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("APP-105"));
  EXPECT_TRUE(tasks.at(1).toMap().value(QStringLiteral("archived")).toBool()) << "archived last";

  // The id typed comes first; a limit keeps the count.
  const QVariantMap byId = app_->commandLine(QStringLiteral("APP-106"), 1);
  EXPECT_EQ(byId.value(QStringLiteral("total")).toInt(), 1);
  EXPECT_EQ(byId.value(QStringLiteral("tasks")).toList().value(0).toMap().value(QStringLiteral("id")).toString(),
            QStringLiteral("APP-106"));
  const QVariantMap capped = app_->commandLine(QStringLiteral("order"), 1);
  EXPECT_EQ(capped.value(QStringLiteral("total")).toInt(), 3);
  EXPECT_EQ(capped.value(QStringLiteral("tasks")).toList().size(), 1);
  // ">" is commands only: no task search.
  const QVariantMap cmds = app_->commandLine(QStringLiteral("> order"));
  EXPECT_TRUE(cmds.value(QStringLiteral("commandsOnly")).toBool());
  EXPECT_EQ(cmds.value(QStringLiteral("total")).toInt(), 0);
}

// Interface scale from the keyboard (APP-168): Ctrl+= / Ctrl+- / Ctrl+0 are
// catalog actions, and the fixed aliases next to them count as taken.
TEST_F(AppControllerTest, ZoomShortcutsAreInTheCatalog) {
  EXPECT_EQ(app_->defaultShortcutFor(QStringLiteral("zoom.in")), QString("Ctrl+="));
  EXPECT_EQ(app_->defaultShortcutFor(QStringLiteral("zoom.out")), QString("Ctrl+-"));
  EXPECT_EQ(app_->defaultShortcutFor(QStringLiteral("zoom.reset")), QString("Ctrl+0"));
  for(const char* id : {"zoom.in", "zoom.out", "zoom.reset"}) {
    EXPECT_FALSE(app_->shortcutLabel(QString::fromLatin1(id)).isEmpty()) << id;
    EXPECT_FALSE(app_->shortcutDescription(QString::fromLatin1(id)).isEmpty()) << id;
  }
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+=")), QString("zoom.in"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl++")), QString("zoom.in"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+Shift+=")), QString("zoom.in"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+Num++")), QString("zoom.in"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl+Num+-")), QString("zoom.out"));
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("cal.today"), QStringLiteral("Ctrl+Num+0")), QString("zoom.reset"));
  // An alias is the action's own key, and cannot be handed to another one.
  EXPECT_EQ(app_->findShortcutConflict(QStringLiteral("zoom.in"), QStringLiteral("Ctrl++")), QString());
  EXPECT_FALSE(app_->setShortcut(QStringLiteral("theme.toggle"), QStringLiteral("Ctrl++")));
  EXPECT_TRUE(app_->setShortcut(QStringLiteral("zoom.in"), QStringLiteral("Ctrl+Alt+=")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("zoom.in")), QString("Ctrl+Alt+="));
  app_->resetShortcut(QStringLiteral("zoom.in"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("zoom.in")), QString("Ctrl+="));
}

TEST(UiScale, StepsThroughTheScale) {
  const QList<double> steps{0.9, 1, 1.1, 1.25, 1.5};
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.0, 1, steps), 1.1);
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.1, 1, steps), 1.25);
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.25, -1, steps), 1.1);
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.0, -1, steps), 0.9);
  // The ends stay put.
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.5, 1, steps), 1.5);
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(0.9, -1, steps), 0.9);
  // Between two steps: the next one that way.
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.2, 1, steps), 1.25);
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.2, -1, steps), 1.1);
  // A value read back from JSON a hair off a step is that step.
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.1000000001, 1, steps), 1.25);
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.0999999999, -1, steps), 1.0);
  // Reset, unsorted steps, nothing to step through.
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.5, 0, steps), 1.0);
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.0, 1, {1.5, 0.9, 1.1, 1}), 1.1);
  EXPECT_DOUBLE_EQ(heap::ui::nextUiScale(1.25, 1, {}), 1.0);
}

TEST_F(AppControllerTest, StepUiScaleWritesTheAppearanceSetting) {
  const QVariantList steps{0.9, 1, 1.1, 1.25, 1.5};
  const auto stored = [this] {
    return QJsonDocument::fromJson(app_->appSettingsJson().toUtf8())
        .object()
        .value(QStringLiteral("appearance"))
        .toObject()
        .value(QStringLiteral("uiScale"));
  };
  QJsonObject settings = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object();
  QJsonObject appearance = settings.value(QStringLiteral("appearance")).toObject();
  appearance.insert(QStringLiteral("theme"), QStringLiteral("keep-me"));
  appearance.remove(QStringLiteral("uiScale"));
  settings.insert(QStringLiteral("appearance"), appearance);
  app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));

  // Unset, the keys step from the default on screen, not from 100 % (SHELL-3).
  const double def = app_->defaultUiScale(steps);
  EXPECT_DOUBLE_EQ(app_->stepUiScale(1, steps), def > 1.05 ? 1.25 : 1.1);
  appearance.insert(QStringLiteral("uiScale"), 1.0);
  settings.insert(QStringLiteral("appearance"), appearance);
  app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  EXPECT_DOUBLE_EQ(app_->stepUiScale(1, steps), 1.1);
  EXPECT_DOUBLE_EQ(stored().toDouble(), 1.1);
  EXPECT_DOUBLE_EQ(app_->stepUiScale(1, steps), 1.25);
  EXPECT_DOUBLE_EQ(app_->stepUiScale(1, steps), 1.5);
  QSignalSpy changed(app_.get(), &AppController::appSettingsJsonChanged);
  EXPECT_DOUBLE_EQ(app_->stepUiScale(1, steps), 1.5);
  EXPECT_EQ(changed.count(), 0);  // at the top nothing changes
  EXPECT_DOUBLE_EQ(app_->stepUiScale(0, steps), def);
  EXPECT_FALSE(stored().isDouble()) << "reset is the default, not a pick of 100 % (IDIOT-SHELL-14)";
  app_->stepUiScale(-1, steps);
  EXPECT_DOUBLE_EQ(app_->stepUiScale(-1, steps), 0.9);
  EXPECT_DOUBLE_EQ(app_->stepUiScale(-1, steps), 0.9);
  // The rest of the appearance group is left alone.
  EXPECT_EQ(QJsonDocument::fromJson(app_->appSettingsJson().toUtf8())
                .object()
                .value(QStringLiteral("appearance"))
                .toObject()
                .value(QStringLiteral("theme"))
                .toString(),
            QStringLiteral("keep-me"));

  // A stored value Theme.scale would not use (out of range) reads as 100 %.
  appearance = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object().value(QStringLiteral("appearance")).toObject();
  settings = QJsonDocument::fromJson(app_->appSettingsJson().toUtf8()).object();
  appearance.insert(QStringLiteral("uiScale"), 3.0);
  settings.insert(QStringLiteral("appearance"), appearance);
  app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  EXPECT_DOUBLE_EQ(app_->stepUiScale(1, steps), 1.1);
  app_->stepUiScale(0, steps);
}

// While the user has not picked a scale, the system's text size is the scale
// (APP-183) — and Ctrl+= steps on from there, not from 100 %.
TEST_F(AppControllerTest, UnsetUiScaleStartsFromTheSystemTextSize) {
  const QVariantList steps{0.9, 1, 1.1, 1.25, 1.5};
  // A test run ignores the machine's own setting.
  EXPECT_DOUBLE_EQ(app_->systemUiScale(steps), 1.0);

  qputenv("HEAP_TEXT_SCALE", "125");
  const auto restore = qScopeGuard([] {
    qunsetenv("HEAP_TEXT_SCALE");
  });
  AppController large;
  EXPECT_DOUBLE_EQ(large.systemUiScale(steps), 1.25);
  QJsonObject settings = QJsonDocument::fromJson(large.appSettingsJson().toUtf8()).object();
  QJsonObject appearance = settings.value(QStringLiteral("appearance")).toObject();
  appearance.remove(QStringLiteral("uiScale"));
  settings.insert(QStringLiteral("appearance"), appearance);
  large.setAppSettingsJson(QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact)));
  EXPECT_DOUBLE_EQ(large.stepUiScale(1, steps), 1.5);
  // Once picked, the pick wins over the system.
  EXPECT_DOUBLE_EQ(large.stepUiScale(-1, steps), 1.25);
  EXPECT_DOUBLE_EQ(large.stepUiScale(-1, steps), 1.1);
  // Reset goes back to where it started, not to 100 % (IDIOT-SHELL-14).
  EXPECT_DOUBLE_EQ(large.stepUiScale(0, steps), 1.25);
}

TEST_F(AppControllerTest, ResetShortcutRestoresAndSwaps) {
  app_->setShortcut(QStringLiteral("task.new"), QStringLiteral("Ctrl+K"));  // frees palette.open
  app_->resetShortcut(QStringLiteral("palette.open"));                      // default Ctrl+K conflicts with task.new
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("palette.open")), QString("Ctrl+K"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString());  // swapped out
}

TEST_F(AppControllerTest, ResetShortcutAlreadyDefaultNoop) {
  app_->resetShortcut(QStringLiteral("section.tasks"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("section.tasks")), QString("Ctrl+2"));
  app_->resetShortcut(QStringLiteral("unknown.id"));  // no crash
}

TEST_F(AppControllerTest, ResetAllShortcutsRestoresEveryEntry) {
  app_->setShortcut(QStringLiteral("task.new"), QStringLiteral("Ctrl+K"));
  app_->setShortcut(QStringLiteral("view.week"), QStringLiteral("Ctrl+9"));
  app_->resetAllShortcuts();
  const QVariantList cat = app_->shortcuts();
  ASSERT_FALSE(cat.isEmpty());
  for(const QVariant& v : cat) {
    const QVariantMap m = v.toMap();
    const QString id = m.value(QStringLiteral("id")).toString();
    EXPECT_EQ(app_->shortcutFor(id), app_->defaultShortcutFor(id)) << "id=" << id.toStdString();
  }
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.new")), QString("Ctrl+N"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("palette.open")), QString("Ctrl+K"));
}

// ─── scheduleTask + read-back ─────────────────────────────────────────

TEST_F(AppControllerTest, ScheduleTaskCreatesFocusEvent) {
  app_->tasks()->reset({mkTask(QStringLiteral("T-1"), QStringLiteral("Fix login"))});
  app_->scheduleTask(QStringLiteral("T-1"), 14.5, QDate(2026, 5, 15));
  ASSERT_EQ(app_->events()->rowCount(), 1);
  const CalEvent& e = app_->events()->items().at(0);
  EXPECT_EQ(e.type, QString("focus"));
  EXPECT_EQ(e.taskId, QString("T-1"));
  EXPECT_DOUBLE_EQ(e.start, 14.5);
  // No estimate on the task → the focus-block duration from settings (90 min).
  EXPECT_DOUBLE_EQ(e.end, 16.0);
  EXPECT_EQ(e.profileId, app_->activeProfileId());
  EXPECT_EQ(app_->scheduledLabelFor(QStringLiteral("T-1"), QDate(2026, 5, 15)), QString("14:30"));
}

// Dropping a task on the calendar used to carve out exactly one hour whatever
// the task said about itself.
TEST_F(AppControllerTest, ScheduleTaskUsesTheTaskEstimate) {
  Task t = mkTask(QStringLiteral("T-1"), QStringLiteral("Quick chore"));
  t.estimateMinutes = 20;
  app_->tasks()->reset({t});

  app_->scheduleTask(QStringLiteral("T-1"), 10.0, QDate(2026, 5, 15));

  const CalEvent& e = app_->events()->items().at(0);
  EXPECT_DOUBLE_EQ(e.start, 10.0);
  EXPECT_NEAR(e.end - e.start, 0.25, 1e-9) << "20 min snaps to the 15-minute grid";
}

// The block is only half of scheduling. Week, Month, Timeline and the card's
// own label all read Task.scheduledAt, so a time-blocked task used to stay
// unscheduled everywhere except the day it was dropped on.
TEST_F(AppControllerTest, ScheduleTaskSchedulesTheTaskItself) {
  app_->tasks()->reset({mkTask(QStringLiteral("T-1"), QStringLiteral("Fix login"))});

  app_->scheduleTask(QStringLiteral("T-1"), 14.5, QDate(2026, 5, 15));

  const Task& t = app_->tasks()->items().at(0);
  EXPECT_EQ(t.scheduledAt.date(), QDate(2026, 5, 15));
  EXPECT_EQ(t.scheduledAt.time(), QTime(14, 30));
  EXPECT_TRUE(t.scheduledHasTime);
}

TEST_F(AppControllerTest, ScheduleTaskLateInTheDayStaysInsideIt) {
  Task t = mkTask(QStringLiteral("T-1"), QStringLiteral("Long one"));
  t.estimateMinutes = 180;
  app_->tasks()->reset({t});

  app_->scheduleTask(QStringLiteral("T-1"), 23.0, QDate(2026, 5, 15));

  const CalEvent& e = app_->events()->items().at(0);
  EXPECT_LE(e.end, 24.0);
  EXPECT_LT(e.start, e.end);
}

TEST_F(AppControllerTest, ScheduleTaskWithoutADateUsesTheSelectedOne) {
  app_->tasks()->reset({mkTask(QStringLiteral("T-1"), QStringLiteral("x"))});
  app_->setSelectedDate(QDate(2026, 5, 20));

  app_->scheduleTask(QStringLiteral("T-1"), 10.0, QDate());

  EXPECT_EQ(app_->events()->items().at(0).date, QDate(2026, 5, 20));
}

// nextFreeSlot replaces the hardcoded 14:00 the card menu used, which stacked
// every scheduled task on top of the last.
TEST_F(AppControllerTest, NextFreeSlotStartsAtTheWorkdayWhenTheDayIsEmpty) {
  const QDate day(2026, 5, 15);  // not today, so "now" does not move the answer
  EXPECT_DOUBLE_EQ(app_->nextFreeSlot(day, 1.0), app_->workdayStart());
}

TEST_F(AppControllerTest, NextFreeSlotStepsPastABusyBlock) {
  const QDate day(2026, 5, 15);
  QVariantMap draft = app_->newEventDraft(app_->workdayStart(), day);
  draft["end"] = app_->workdayStart() + 2.0;
  app_->saveEvent(draft);

  EXPECT_DOUBLE_EQ(app_->nextFreeSlot(day, 1.0), app_->workdayStart() + 2.0);
}

TEST_F(AppControllerTest, NextFreeSlotFitsIntoAGapBetweenBlocks) {
  const QDate day(2026, 5, 15);
  const double start = app_->workdayStart();
  QVariantMap first = app_->newEventDraft(start, day);
  first["end"] = start + 1.0;
  app_->saveEvent(first);
  // Leaves a one-hour hole at start+1 .. start+2.
  QVariantMap second = app_->newEventDraft(start + 2.0, day);
  second["end"] = start + 3.0;
  app_->saveEvent(second);

  EXPECT_DOUBLE_EQ(app_->nextFreeSlot(day, 1.0), start + 1.0);
  EXPECT_DOUBLE_EQ(app_->nextFreeSlot(day, 2.0), start + 3.0) << "a 2h block does not fit the 1h hole";
}

TEST_F(AppControllerTest, NextFreeSlotAlwaysLeavesRoomForTheBlock) {
  const QDate day(2026, 5, 15);
  QVariantMap draft = app_->newEventDraft(app_->workdayStart(), day);
  draft["end"] = 24.0;
  app_->saveEvent(draft);

  const double at = app_->nextFreeSlot(day, 1.0);
  EXPECT_LE(at + 1.0, 24.0);
  EXPECT_GE(at, 0.0);
}

TEST_F(AppControllerTest, ScheduleTaskUnknownIdIsNoop) {
  app_->scheduleTask(QStringLiteral("ghost"), 9.0, QDate(2026, 5, 15));
  EXPECT_EQ(app_->events()->rowCount(), 0);
  EXPECT_EQ(app_->scheduledLabelFor(QStringLiteral("ghost"), QDate(2026, 5, 15)), QString());
}

TEST_F(AppControllerTest, ScheduledLabelPicksEarliest) {
  app_->tasks()->reset({mkTask(QStringLiteral("T-1"), QStringLiteral("x"))});
  app_->scheduleTask(QStringLiteral("T-1"), 14.0, QDate(2026, 5, 15));
  app_->scheduleTask(QStringLiteral("T-1"), 10.0, QDate(2026, 5, 15));
  EXPECT_EQ(app_->scheduledLabelFor(QStringLiteral("T-1"), QDate(2026, 5, 15)), QString("10:00"));
}

TEST_F(AppControllerTest, ScheduledLabelInvalidDateEmpty) {
  app_->tasks()->reset({mkTask(QStringLiteral("T-1"), QStringLiteral("x"))});
  EXPECT_EQ(app_->scheduledLabelFor(QStringLiteral("T-1"), QDate()), QString());
}

// ─── event hour clamping on the two write paths ───────────────────────

// saveEvent used to fix up only "end before start"; a free-typed time went to
// disk as-is, so an event could sit outside the day entirely.
TEST_F(AppControllerTest, SaveEventClampsAFreeTypedRangeIntoTheDay) {
  QVariantMap draft = app_->newEventDraft(9.0, QDate(2026, 5, 15));
  draft["start"] = 26.0;
  draft["end"] = 30.0;
  app_->saveEvent(draft);

  ASSERT_EQ(app_->events()->rowCount(), 1);
  const CalEvent& e = app_->events()->items().at(0);
  EXPECT_LE(e.end, 24.0);
  EXPECT_GE(e.start, 0.0);
  EXPECT_LT(e.start, e.end);
}

TEST_F(AppControllerTest, SaveEventSnapsToTheGrid) {
  QVariantMap draft = app_->newEventDraft(9.0, QDate(2026, 5, 15));
  draft["start"] = 9.1;
  draft["end"] = 10.4;
  app_->saveEvent(draft);

  const CalEvent& e = app_->events()->items().at(0);
  EXPECT_DOUBLE_EQ(e.start, 9.0);
  EXPECT_DOUBLE_EQ(e.end, 10.5);
}

// The qBound(start + minDur, end, 24.0) regression: dragging the bottom edge
// of a late event past midnight asserted in a debug build.
TEST_F(AppControllerTest, UpdateEventResizedPastMidnightStaysInsideTheDay) {
  QVariantMap draft = app_->newEventDraft(23.0, QDate(2026, 5, 15));
  app_->saveEvent(draft);
  const QString id = app_->events()->items().at(0).id;

  app_->updateEvent(id, 23.9, 25.5, QDate());

  const CalEvent& e = app_->events()->items().at(0);
  EXPECT_DOUBLE_EQ(e.start, 23.75);
  EXPECT_DOUBLE_EQ(e.end, 24.0);
}

TEST_F(AppControllerTest, UpdateEventKeepsTheEventGrabbable) {
  QVariantMap draft = app_->newEventDraft(10.0, QDate(2026, 5, 15));
  app_->saveEvent(draft);
  const QString id = app_->events()->items().at(0).id;

  // Dragging the bottom edge above the top one must not invert the event.
  app_->updateEvent(id, 10.0, 9.0, QDate());

  const CalEvent& e = app_->events()->items().at(0);
  EXPECT_LT(e.start, e.end);
  EXPECT_NEAR(e.end - e.start, 0.25, 1e-9);
}

TEST_F(AppControllerTest, UpdateEventMovesTheDateWhenGivenOne) {
  QVariantMap draft = app_->newEventDraft(10.0, QDate(2026, 5, 15));
  app_->saveEvent(draft);
  const QString id = app_->events()->items().at(0).id;

  app_->updateEvent(id, 11.0, 12.0, QDate(2026, 5, 16));

  const CalEvent& e = app_->events()->items().at(0);
  EXPECT_EQ(e.date, QDate(2026, 5, 16));
  EXPECT_DOUBLE_EQ(e.start, 11.0);
}

// ─── sync task ⇄ event cascade delete (HEAP-104) ──────────────────────

namespace {
// A QuickCapture "sync": one task plus one linked meeting event.
void seedSync(AppController* app, const QString& taskId) {
  app->tasks()->reset({mkTask(taskId, QStringLiteral("синк"))});
  app->events()->reset({});
  QVariantMap ev = app->newEventDraft(13.0, QDate(2026, 7, 13));
  ev["type"] = QStringLiteral("sync");
  ev["taskId"] = taskId;
  ev["title"] = QStringLiteral("синк");
  app->saveEvent(ev);
}
}  // namespace

TEST_F(AppControllerTest, DeleteTaskRemovesLinkedSyncEvent) {
  seedSync(app_.get(), QStringLiteral("SYNC-1"));
  ASSERT_EQ(app_->events()->rowCount(), 1);

  app_->deleteTask(QStringLiteral("SYNC-1"));
  EXPECT_EQ(app_->tasks()->rowCount(), 0);
  EXPECT_EQ(app_->events()->rowCount(), 0) << "the linked meeting event must go with the task";

  // Undo brings both halves back, still linked.
  app_->undoLastDeletion();
  ASSERT_EQ(app_->tasks()->rowCount(), 1);
  ASSERT_EQ(app_->events()->rowCount(), 1);
  EXPECT_EQ(app_->events()->items().at(0).taskId, QStringLiteral("SYNC-1"));
}

TEST_F(AppControllerTest, DeleteSyncEventRemovesMirrorTask) {
  seedSync(app_.get(), QStringLiteral("SYNC-1"));
  const QString evId = app_->events()->items().at(0).id;

  app_->deleteEvent(evId);
  EXPECT_EQ(app_->events()->rowCount(), 0);
  EXPECT_EQ(app_->tasks()->rowCount(), 0) << "the mirror task must go with the sync event";

  app_->undoLastDeletion();
  ASSERT_EQ(app_->tasks()->rowCount(), 1);
  ASSERT_EQ(app_->events()->rowCount(), 1);
  EXPECT_EQ(app_->tasks()->items().at(0).id, QStringLiteral("SYNC-1"));
}

TEST_F(AppControllerTest, DeleteFocusEventKeepsItsTask) {
  // A focus block is a scheduled slice of a task, not a mirror — deleting the
  // block just unschedules; the task stays.
  app_->tasks()->reset({mkTask(QStringLiteral("T-1"), QStringLiteral("x"))});
  app_->scheduleTask(QStringLiteral("T-1"), 14.0, QDate(2026, 7, 13));
  ASSERT_EQ(app_->events()->rowCount(), 1);
  ASSERT_EQ(app_->events()->items().at(0).type, QStringLiteral("focus"));

  app_->deleteEvent(app_->events()->items().at(0).id);
  EXPECT_EQ(app_->events()->rowCount(), 0);
  EXPECT_EQ(app_->tasks()->rowCount(), 1) << "a focus block must not delete its task";
}

TEST_F(AppControllerTest, DeleteTaskRemovesItsFocusBlock) {
  app_->tasks()->reset({mkTask(QStringLiteral("T-1"), QStringLiteral("x"))});
  app_->scheduleTask(QStringLiteral("T-1"), 14.0, QDate(2026, 7, 13));
  ASSERT_EQ(app_->events()->rowCount(), 1);

  app_->deleteTask(QStringLiteral("T-1"));
  EXPECT_EQ(app_->events()->rowCount(), 0) << "the focus block must go with the deleted task";
}

// ─── eventHourLabel (24h default) ─────────────────────────────────────

TEST_F(AppControllerTest, EventHourLabel24h) {
  EXPECT_EQ(app_->eventHourLabel(9.0), QString("09:00"));
  EXPECT_EQ(app_->eventHourLabel(14.5), QString("14:30"));
  EXPECT_EQ(app_->eventHourLabel(0.25), QString("00:15"));
}

// TIME-22 (2026-09-30-1): an end at 24:00 is midnight, 12:00am — not noon.
TEST_F(AppControllerTest, EventHourLabel12hMidnightEnd) {
  const QString before = app_->appSettingsJson();
  app_->setAppSettingsJson(QStringLiteral(R"({"calendar":{"timeFormat":"12h"}})"));
  EXPECT_EQ(app_->eventHourLabel(24.0), QString("12:00am"));
  EXPECT_EQ(app_->eventHourLabel(0.0), QString("12:00am"));
  EXPECT_EQ(app_->eventHourLabel(12.0), QString("12:00pm"));
  EXPECT_EQ(app_->eventHourLabel(23.5), QString("11:30pm"));
  app_->setAppSettingsJson(before);
}

// SHELL-10 (2026-09-30-1): an ID prefix nothing can link to ("MY TEAM",
// "../../X") is not used for new ids.
TEST_F(AppControllerTest, NewTaskIdsUseAUsablePrefixOnly) {
  const QString before = app_->appSettingsJson();
  app_->setAppSettingsJson(QStringLiteral(R"({"tasks":{"idPrefix":"MY TEAM"}})"));
  EXPECT_TRUE(app_->newTaskDraft(QStringLiteral("todo")).value("id").toString().startsWith(QStringLiteral("TASK-")));
  app_->setAppSettingsJson(QStringLiteral(R"({"tasks":{"idPrefix":"../../x"}})"));
  EXPECT_TRUE(app_->newTaskDraft(QStringLiteral("todo")).value("id").toString().startsWith(QStringLiteral("TASK-")));
  app_->setAppSettingsJson(QStringLiteral(R"({"tasks":{"idPrefix":"heap2"}})"));
  EXPECT_TRUE(app_->newTaskDraft(QStringLiteral("todo")).value("id").toString().startsWith(QStringLiteral("HEAP2-")));
  app_->setAppSettingsJson(before);
}

// ─── humanDate / shortDate ────────────────────────────────────────────

TEST_F(AppControllerTest, HumanDateEnRu) {
  app_->setLanguage(QStringLiteral("en"));
  EXPECT_EQ(app_->humanDate(QDate(2026, 5, 15)), QString::fromUtf8("Friday, May 15"));
  EXPECT_EQ(app_->humanDate(QDate(2026, 1, 1)), QString::fromUtf8("Thursday, January 1"));
  app_->setLanguage(QStringLiteral("ru"));
  EXPECT_EQ(app_->humanDate(QDate(2026, 5, 15)), QString::fromUtf8("пятница, 15 мая"));
  EXPECT_EQ(app_->humanDate(QDate()), QString());
}

TEST_F(AppControllerTest, ShortDateEnRu) {
  app_->setLanguage(QStringLiteral("en"));
  EXPECT_EQ(app_->shortDate(QDate(2026, 5, 15)), QString::fromUtf8("Fri, May 15"));
  app_->setLanguage(QStringLiteral("ru"));
  EXPECT_EQ(app_->shortDate(QDate(2026, 5, 15)), QString::fromUtf8("пт, 15 мая"));
  EXPECT_EQ(app_->shortDate(QDate()), QString());
}

// APP-188: every displayed date goes through one table of named styles, in
// the UI language; the clock follows the 12h/24h setting, not the language.
TEST_F(AppControllerTest, DateLabelsFollowTheUiLanguage) {
  const QDate oct6(2026, 10, 6);
  app_->setLanguage(QStringLiteral("en"));
  EXPECT_EQ(app_->dateLabel(oct6, QStringLiteral("dayMonth")), QStringLiteral("Oct 6"));
  EXPECT_EQ(app_->dateLabel(oct6, QStringLiteral("dayMonthYear")), QStringLiteral("Oct 6, 2026"));
  EXPECT_EQ(app_->dateLabel(oct6, QStringLiteral("longWeekdayYear")), QStringLiteral("Tuesday, October 6, 2026"));
  EXPECT_EQ(app_->dateLabel(oct6, QStringLiteral("no-such-style")), QStringLiteral("Oct 6"));
  app_->setLanguage(QStringLiteral("ru"));
  EXPECT_EQ(app_->dateLabel(oct6, QStringLiteral("dayMonth")), QString::fromUtf8("6 окт."));
  EXPECT_EQ(app_->dateLabel(oct6, QStringLiteral("weekdayDay")), QString::fromUtf8("вт, 6 окт."));
  EXPECT_EQ(app_->dateLabel(oct6, QStringLiteral("longDayYear")), QString::fromUtf8("6 октября 2026"));
  EXPECT_EQ(app_->datePattern(QStringLiteral("dayMonth"), QStringLiteral("en")), QStringLiteral("MMM d"));
  EXPECT_EQ(app_->dateLabel(QDate(), QStringLiteral("dayMonth")), QString());
}

// a-31 (DG-031): a meeting found in the command line reads "15 окт 2026",
// without the locale's dot.
TEST_F(AppControllerTest, PaletteMeetingDateHasNoMonthDot) {
  QVariantMap draft = app_->newEventDraft(11.5, QDate(2026, 10, 15));
  draft["title"] = QStringLiteral("Palette dot probe");
  app_->saveEvent(draft);
  app_->setLanguage(QStringLiteral("ru"));
  QString sub;
  for(const QVariant& v : app_->commandPaletteEntries()) {
    const QVariantMap m = v.toMap();
    if(m.value("kind").toString() == QLatin1String("event") && m.value("label").toString() == QLatin1String("Palette dot probe")) {
      sub = m.value("sub").toString();
    }
  }
  app_->setLanguage(QStringLiteral("en"));
  EXPECT_TRUE(sub.contains(QString::fromUtf8("15 окт 2026"))) << sub.toStdString();
  EXPECT_FALSE(sub.contains(QString::fromUtf8("окт."))) << sub.toStdString();
  EXPECT_EQ(heap::text::withoutMonthDot(QString::fromUtf8("вт, 6 окт., 15:15"), QStringLiteral("ru")),
            QString::fromUtf8("вт, 6 окт, 15:15"));
  EXPECT_EQ(heap::text::withoutMonthDot(QStringLiteral("Oct. 6"), QStringLiteral("en")), QStringLiteral("Oct. 6"));
}

TEST_F(AppControllerTest, DateTimeLabelUsesTheClockSetting) {
  const QString before = app_->appSettingsJson();
  const QDateTime at(QDate(2026, 10, 6), QTime(15, 15));
  app_->setLanguage(QStringLiteral("en"));
  app_->setAppSettingsJson(QStringLiteral(R"({"calendar":{"timeFormat":"24h"}})"));
  EXPECT_EQ(app_->dateTimeLabel(at, QStringLiteral("dayMonth")), QStringLiteral("Oct 6, 15:15"));
  app_->setAppSettingsJson(QStringLiteral(R"({"calendar":{"timeFormat":"12h"}})"));
  EXPECT_EQ(app_->dateTimeLabel(at, QStringLiteral("dayMonth")), QStringLiteral("Oct 6, 3:15pm"));
  app_->setLanguage(QStringLiteral("ru"));
  EXPECT_EQ(app_->dateTimeLabel(at, QStringLiteral("dayMonth")), QString::fromUtf8("6 окт., 3:15pm"));
  app_->setAppSettingsJson(QStringLiteral(R"({"calendar":{"timeFormat":"24h"}})"));
  EXPECT_EQ(app_->dateTimeLabel(at, QStringLiteral("dayMonth")), QString::fromUtf8("6 окт., 15:15"));
  app_->setLanguage(QStringLiteral("en"));
  app_->setAppSettingsJson(before);
}

// ─── parseDateTime wrapper key contract ───────────────────────────────

TEST_F(AppControllerTest, ParseDateTimeMapContract) {
  const QVariantMap m = app_->parseDateTime(QStringLiteral("tomorrow 15:00"), QDateTime(QDate(2026, 7, 2), QTime(9, 0)));
  for(const char* key : {"ok", "start", "end", "hasTime", "recurrence", "consumed", "startOffset", "endOffset"}) {
    EXPECT_TRUE(m.contains(QString::fromLatin1(key))) << "missing key " << key;
  }
  // Gibberish must not parse (locale-independent).
  EXPECT_FALSE(app_->parseDateTime(QStringLiteral("zzzz qqqq wwww"), QDateTime(QDate(2026, 7, 2), QTime(9, 0)))
                   .value(QStringLiteral("ok"))
                   .toBool());
}

// ─── classifyTaskKind enum→string mapping ─────────────────────────────

TEST_F(AppControllerTest, ClassifyTaskKindMapping) {
  EXPECT_EQ(app_->classifyTaskKind(QStringLiteral("focus mode tomorrow 9am")), QString("focus"));
  EXPECT_EQ(app_->classifyTaskKind(QStringLiteral("standup at 10")), QString("sync"));
  EXPECT_EQ(app_->classifyTaskKind(QString::fromUtf8("задача: подготовить синк")), QString("ticket"));
  EXPECT_EQ(app_->classifyTaskKind(QString::fromUtf8("написать @viktor про релиз")), QString("contact"));
  EXPECT_EQ(app_->classifyTaskKind(QString::fromUtf8("купить хлеб завтра в 18:00")), QString("none"));
  EXPECT_EQ(app_->classifyTaskKind(QString()), QString("none"));
}

// ─── extractTaskMeta wrapper shape ────────────────────────────────────

TEST_F(AppControllerTest, ExtractTaskMetaShape) {
  const QVariantMap m = app_->extractTaskMeta(QString::fromUtf8("напомни @andrey про PR"));
  EXPECT_TRUE(m.contains(QStringLiteral("title")));
  EXPECT_TRUE(m.contains(QStringLiteral("desc")));
  EXPECT_TRUE(m.contains(QStringLiteral("handles")));
  EXPECT_TRUE(m.value(QStringLiteral("handles")).toStringList().contains(QStringLiteral("andrey")));
  EXPECT_FALSE(m.value(QStringLiteral("title")).toString().isEmpty());

  const QVariantMap plain = app_->extractTaskMeta(QStringLiteral("plain text no handles"));
  EXPECT_TRUE(plain.value(QStringLiteral("handles")).toStringList().isEmpty());
}

// ─── Full-text command-palette entries (HEAP-80) ──────────────────────

// The palette searches the persisted profiles, and the live models only reach
// a profile when a save runs — which is debounced by 300 ms. So a task created
// a moment ago was simply missing from Ctrl+K. Note the deliberate absence of
// flushSave() here: that is the whole point.
TEST_F(AppControllerTest, ATaskIsFindableInThePaletteBeforeTheSaveDebounceFires) {
  QVariantMap draft;
  draft["_isNew"] = true;
  draft["id"] = QStringLiteral("LTE-9100");
  draft["title"] = QStringLiteral("unflushed quokka");
  draft["priority"] = QStringLiteral("P2");
  draft["status"] = QStringLiteral("todo");
  app_->saveTask(draft);

  bool found = false;
  for(const QVariant& v : app_->commandPaletteEntries()) {
    if(v.toMap().value(QStringLiteral("taskId")).toString() == QStringLiteral("LTE-9100")) {
      found = true;
    }
  }
  EXPECT_TRUE(found) << "a just-created task was missing from the palette";

  // …and an edit is visible just as promptly.
  QVariantMap edit = app_->taskById(QStringLiteral("LTE-9100"));
  edit["_isNew"] = false;
  edit["_originalId"] = QStringLiteral("LTE-9100");
  edit["title"] = QStringLiteral("renamed wombat");
  app_->saveTask(edit);

  bool renamed = false;
  for(const QVariant& v : app_->commandPaletteEntries()) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("taskId")).toString() == QStringLiteral("LTE-9100")) {
      renamed = m.value(QStringLiteral("label")).toString().contains(QStringLiteral("wombat"));
    }
  }
  EXPECT_TRUE(renamed) << "the palette showed a stale title";

  app_->deleteTask(QStringLiteral("LTE-9100"));
  app_->clearPendingUndo();
}

TEST_F(AppControllerTest, CommandPaletteEntriesCarryBodyText) {
  // Seed a task whose search term lives only in the description, and a note
  // whose term lives only in the body; flush so the active profile picks both
  // up (commandPaletteEntries reads the profile snapshot).
  QVariantMap draft;
  draft["_isNew"] = true;
  draft["id"] = QStringLiteral("LTE-9001");
  draft["title"] = QStringLiteral("Quiet title");
  draft["desc"] = QStringLiteral("zebra hidden in the body");
  draft["priority"] = QStringLiteral("P2");
  draft["status"] = QStringLiteral("todo");
  app_->saveTask(draft);
  app_->setNotesState(QStringLiteral("a note mentioning platypus somewhere"));
  app_->flushSave();

  const QVariantList entries = app_->commandPaletteEntries();
  bool taskBodyOk = false;
  bool noteOk = false;
  for(const QVariant& v : entries) {
    const QVariantMap m = v.toMap();
    if(m.value("kind").toString() == QStringLiteral("task") && m.value("taskId").toString() == QStringLiteral("LTE-9001")) {
      // The term is absent from the label but present in the searchable body.
      EXPECT_FALSE(m.value("label").toString().contains(QStringLiteral("zebra")));
      EXPECT_TRUE(m.value("body").toString().contains(QStringLiteral("zebra")));
      taskBodyOk = true;
    }
    if(m.value("kind").toString() == QStringLiteral("note") && m.value("body").toString().contains(QStringLiteral("platypus"))) {
      noteOk = true;
    }
  }
  EXPECT_TRUE(taskBodyOk);
  EXPECT_TRUE(noteOk);
}

TEST_F(AppControllerTest, SnippetTagsAndLanguageReachPaletteBody) {
  // A snippet whose match term lives only in its tags / language must be
  // findable from the palette (HEAP-79 "first-class snippet library").
  const QString docs = QStringLiteral(
      "{\"sections\":[],\"contacts\":[],\"snippets\":["
      "{\"title\":\"Interactive rebase\",\"lang\":\"sh\",\"tags\":[\"git\",\"workflow\"],\"code\":\"git rebase -i\"}]}");
  app_->setDocsState(docs);
  app_->flushSave();

  bool ok = false;
  for(const QVariant& v : app_->commandPaletteEntries()) {
    const QVariantMap m = v.toMap();
    if(m.value("kind").toString() == QStringLiteral("snippet") && m.value("label").toString() == QStringLiteral("Interactive rebase")) {
      const QString body = m.value("body").toString();
      EXPECT_TRUE(body.contains(QStringLiteral("git")));       // tag
      EXPECT_TRUE(body.contains(QStringLiteral("workflow")));  // tag
      EXPECT_TRUE(body.contains(QStringLiteral("sh")));        // language
      ok = true;
    }
  }
  EXPECT_TRUE(ok);
}

// ─── Time tracking (HEAP-78) ──────────────────────────────────────────

TEST_F(AppControllerTest, TaskTimerStartStopAndSingleActive) {
  app_->tasks()->reset({mkTask(QStringLiteral("A"), QStringLiteral("a")), mkTask(QStringLiteral("B"), QStringLiteral("b"))});
  const auto timing = [&](const QString& id) {
    const int row = app_->tasks()->indexOfId(id);
    return app_->tasks()->data(app_->tasks()->index(row, 0), TaskModel::IsTimingRole).toBool();
  };

  app_->startTaskTimer(QStringLiteral("A"));
  EXPECT_TRUE(timing(QStringLiteral("A")));
  EXPECT_GE(app_->elapsedSecondsFor(QStringLiteral("A")), 0);

  // Only one timer runs at a time — starting B stops A.
  app_->startTaskTimer(QStringLiteral("B"));
  EXPECT_TRUE(timing(QStringLiteral("B")));
  EXPECT_FALSE(timing(QStringLiteral("A")));

  // Stopping clears the running flag.
  app_->stopTaskTimer(QStringLiteral("B"));
  EXPECT_FALSE(timing(QStringLiteral("B")));
}

// ─── Recurring tasks + templates (HEAP-77) ────────────────────────────

TEST_F(AppControllerTest, CompletingRecurringTaskSpawnsNext) {
  Task t = mkTask(QStringLiteral("REC-1"), QStringLiteral("Daily standup"));
  t.status = QStringLiteral("todo");
  t.recurrence = QStringLiteral("every:day");
  // Due today: the next one is tomorrow. (Fixed past dates would be skipped
  // forward to the first occurrence after today — see the test below.)
  const QDate today = QDate::currentDate();
  t.dueAt = QDateTime(today, QTime(0, 0));
  t.scheduledAt = t.dueAt;
  app_->tasks()->reset({t});
  const int before = app_->tasks()->rowCount();

  app_->moveTask(QStringLiteral("REC-1"), QStringLiteral("done"));

  ASSERT_EQ(app_->tasks()->rowCount(), before + 1);
  bool foundNext = false;
  for(const Task& x : app_->tasks()->items()) {
    if(x.id != QStringLiteral("REC-1") && x.recurrence == QStringLiteral("every:day")) {
      EXPECT_EQ(x.status, QString("todo"));
      EXPECT_EQ(x.dueAt.date(), today.addDays(1));  // next day
      foundNext = true;
    }
  }
  EXPECT_TRUE(foundNext);
}

// Regression (HEAP-104 review): a recurring task that is scheduled but was never
// given a due date must not gain a phantom midnight dueAt on its next
// occurrence. Otherwise runAutomation() fires an end-of-day reminder for a
// deadline the user never set, and the spurious dueAt is persisted forever.
TEST_F(AppControllerTest, RecurringScheduledOnlyTaskKeepsAnInvalidDueDate) {
  Task t = mkTask(QStringLiteral("REC-2"), QStringLiteral("Focus block"));
  t.status = QStringLiteral("todo");
  t.recurrence = QStringLiteral("every:day");
  const QDate today = QDate::currentDate();
  t.scheduledAt = QDateTime(today, QTime(9, 0));
  t.dueAt = QDateTime();  // scheduled, never owed
  app_->tasks()->reset({t});

  app_->moveTask(QStringLiteral("REC-2"), QStringLiteral("done"));

  bool foundNext = false;
  for(const Task& x : app_->tasks()->items()) {
    if(x.id != QStringLiteral("REC-2") && x.recurrence == QStringLiteral("every:day")) {
      foundNext = true;
      EXPECT_FALSE(x.dueAt.isValid()) << "recurrence manufactured a due date the task never had";
      EXPECT_EQ(x.scheduledAt, QDateTime(today.addDays(1), QTime(9, 0))) << "the scheduled clock time must roll onto the next occurrence";
    }
  }
  EXPECT_TRUE(foundNext);
}

// B7: an interval repeat rolls from the task's own date, every n of them.
TEST_F(AppControllerTest, CompletingAnIntervalRepeatSpawnsItNIntervalsOn) {
  const QDate today = QDate::currentDate();

  struct Case {
    const char* rule;
    QDate next;
  };

  const Case cases[] = {{"every:2:day", today.addDays(2)}, {"every:3:week", today.addDays(21)}, {"every:2:month", today.addMonths(2)}};
  int n = 0;
  for(const Case& c : cases) {
    const QString id = QStringLiteral("INT-%1").arg(++n);
    Task t = mkTask(id, QStringLiteral("Water plants"));
    t.recurrence = QString::fromLatin1(c.rule);
    t.scheduledAt = QDateTime(today, QTime(9, 0));
    t.scheduledHasTime = true;
    app_->tasks()->reset({t});
    app_->moveTask(id, QStringLiteral("done"));
    bool found = false;
    for(const Task& x : app_->tasks()->items()) {
      if(x.id != id) {
        found = true;
        EXPECT_EQ(x.scheduledAt, QDateTime(c.next, QTime(9, 0))) << c.rule;
        // A monthly one keeps its day as the series rule (IDIOT-CAL-5).
        EXPECT_EQ(x.recurrence, heap::recur::seriesRule(QString::fromLatin1(c.rule), today)) << c.rule;
      }
    }
    EXPECT_TRUE(found) << c.rule;
  }
}

// A list of days: Monday's done brings Wednesday's.
TEST_F(AppControllerTest, CompletingADayListRepeatSpawnsTheNextDayOfIt) {
  const QDate today = QDate::currentDate();
  Task t = mkTask(QStringLiteral("DAYS-1"), QStringLiteral("Gym"));
  t.recurrence = QStringLiteral("every:mon,wed");
  t.scheduledAt = QDateTime(today, QTime(0, 0));
  app_->tasks()->reset({t});
  app_->moveTask(QStringLiteral("DAYS-1"), QStringLiteral("done"));
  const QDate want = heap::recur::nextOccurrence(QStringLiteral("every:mon,wed"), today);
  bool found = false;
  for(const Task& x : app_->tasks()->items()) {
    if(x.id != QStringLiteral("DAYS-1")) {
      found = true;
      EXPECT_EQ(x.scheduledAt.date(), want);
      EXPECT_TRUE(want.dayOfWeek() == 1 || want.dayOfWeek() == 3);
    }
  }
  EXPECT_TRUE(found);
}

// The editors' "Custom interval…" row reads words the way capture does.
TEST_F(AppControllerTest, RecurrenceFromTextReadsARepeatOrNothing) {
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("every 3 weeks")), QStringLiteral("every:3:week"));
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("3 weeks")), QStringLiteral("every:3:week"));
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("every other day")), QStringLiteral("every:2:day"));
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("mon, wed")), QStringLiteral("every:mon,wed"));
  EXPECT_EQ(app_->recurrenceFromText(QString::fromUtf8("пн, ср")), QStringLiteral("every:mon,wed"));
  EXPECT_EQ(app_->recurrenceFromText(QString::fromUtf8("каждые 2 дня")), QStringLiteral("every:2:day"));
  EXPECT_EQ(app_->recurrenceFromText(QString::fromUtf8("2 дня")), QStringLiteral("every:2:day"));
  EXPECT_EQ(app_->recurrenceFromText(QString::fromUtf8("через день")), QStringLiteral("every:2:day"));
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("weekdays")), QStringLiteral("every:weekday"));
  // Not a repeat, or one with words left over: nothing.
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("tomorrow")), QString());
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("every 2 weeks on the moon")), QString());
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("banana")), QString());
  EXPECT_EQ(app_->recurrenceFromText(QString()), QString());
  // B7 follow-up: a day of the month with an interval, and the half-reads.
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("every 2 months on the 15th")), QStringLiteral("every:2:month:15"));
  EXPECT_EQ(app_->recurrenceFromText(QString::fromUtf8("раз в 2 месяца 15-го")), QStringLiteral("every:2:month:15"));
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("15th")), QStringLiteral("every:month:15"));
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("weeks")), QString());
  EXPECT_EQ(app_->recurrenceFromText(QString::fromUtf8("недели")), QString());
  EXPECT_EQ(app_->recurrenceFromText(QStringLiteral("every 2 weeks on mon, wed")), QString());
}

// A2-25: the repeats ahead are worked out, never created; a finished or
// archived task brings none, a task with no rule none.
TEST_F(AppControllerTest, CalendarRepeatsAreComputedNotCreated) {
  const QDate today = QDate::currentDate();
  Task daily = mkTask(QStringLiteral("GH-1"), QStringLiteral("Check backups"));
  daily.recurrence = QStringLiteral("every:day");
  daily.scheduledAt = QDateTime(today, QTime(9, 30));
  daily.scheduledHasTime = true;
  Task done = mkTask(QStringLiteral("GH-2"), QStringLiteral("Old one"));
  done.recurrence = QStringLiteral("every:day");
  done.status = QStringLiteral("done");
  done.scheduledAt = QDateTime(today, QTime(0, 0));
  Task archived = mkTask(QStringLiteral("GH-3"), QStringLiteral("Archived"));
  archived.recurrence = QStringLiteral("every:day");
  archived.archived = true;
  archived.scheduledAt = QDateTime(today, QTime(0, 0));
  Task plain = mkTask(QStringLiteral("GH-4"), QStringLiteral("Once"));
  plain.scheduledAt = QDateTime(today, QTime(0, 0));
  Task undated = mkTask(QStringLiteral("GH-5"), QStringLiteral("No date"));
  undated.recurrence = QStringLiteral("every:week");
  app_->tasks()->reset({daily, done, archived, plain, undated});
  const int rows = app_->tasks()->rowCount();

  const QVariantList ghosts = app_->calendarRepeats(today.addDays(-3), today.addDays(3));
  ASSERT_EQ(ghosts.size(), 3) << "tomorrow and the two days after, nothing before today or on it";
  for(int i = 0; i < ghosts.size(); ++i) {
    const QVariantMap g = ghosts.at(i).toMap();
    EXPECT_EQ(g.value("id").toString(), QStringLiteral("GH-1"));
    EXPECT_EQ(g.value("date").toDate(), today.addDays(i + 1));
    EXPECT_EQ(g.value("day").toInt(), 4 + i);
    EXPECT_TRUE(g.value("ghost").toBool());
    EXPECT_TRUE(g.value("hasTime").toBool());
    EXPECT_DOUBLE_EQ(g.value("hour").toDouble(), 9.5);
  }
  EXPECT_EQ(app_->tasks()->rowCount(), rows) << "nothing is created ahead";
  EXPECT_TRUE(app_->calendarRepeats(today, today.addDays(-1)).isEmpty());
}

TEST_F(AppControllerTest, TemplateCreatesPrefilledChecklistTask) {
  ASSERT_FALSE(app_->taskTemplates().isEmpty());
  const int before = app_->tasks()->rowCount();

  app_->createTaskFromTemplate(QStringLiteral("PR review"));

  EXPECT_EQ(app_->tasks()->rowCount(), before + 1);
  bool found = false;
  for(const Task& x : app_->tasks()->items()) {
    if(x.title.startsWith(QStringLiteral("Review PR"))) {
      EXPECT_TRUE(x.desc.contains(QStringLiteral("- [ ]")));  // checklist markdown
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// ─── Git focus: prefix change re-matches the current branch ───────────

TEST_F(AppControllerTest, GitPrefixChangeRematchesFocusedBranch) {
  // Watched repo sitting on branch "HEAP-77-x". With prefix LTE the branch
  // carries no recognizable task id (77 is too short for the lone-digit rule);
  // switching the prefix to HEAP must make the banner pick it up WITHOUT any
  // HEAD movement. Regression: setPrefixes updated the matcher but never re-ran
  // the match on the branch already checked out, so the banner stayed stale
  // until the next checkout.
  QTemporaryDir repo;
  ASSERT_TRUE(repo.isValid());
  const QString gitDir = repo.path() + QStringLiteral("/.git");
  ASSERT_TRUE(QDir().mkpath(gitDir));
  {
    QFile head(gitDir + QStringLiteral("/HEAD"));
    ASSERT_TRUE(head.open(QIODevice::WriteOnly | QIODevice::Text));
    head.write("ref: refs/heads/HEAP-77-x\n");
  }
  const QString repoPath = QDir(repo.path()).absolutePath();

  const auto settings = [&](const QString& prefix) {
    const QJsonObject root{{"tasks", QJsonObject{{"idPrefix", prefix}}}, {"git", QJsonObject{{"watchedRepos", QJsonArray{repoPath}}}}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
  };

  app_->setAppSettingsJson(settings(QStringLiteral("LTE")));
  EXPECT_EQ(app_->focusedBranch(), QStringLiteral("HEAP-77-x"));
  EXPECT_EQ(app_->focusedTaskId(), QString());  // no match under LTE

  app_->setAppSettingsJson(settings(QStringLiteral("HEAP")));
  EXPECT_EQ(app_->focusedTaskId(), QStringLiteral("HEAP-77"));  // linked live

  app_->setAppSettingsJson(settings(QStringLiteral("LTE")));
  EXPECT_EQ(app_->focusedTaskId(), QString());  // un-match propagates too
}

// ─── CODEOWNERS hints and SSH dev-server repos (dogfood 2026-10-19) ───

namespace {

QVariantMap ownersReport(const QStringList& toPing) {
  return QVariantMap{{QStringLiteral("found"), true},
                     {QStringLiteral("location"), QStringLiteral(".github/CODEOWNERS")},
                     {QStringLiteral("toPing"), toPing},
                     {QStringLiteral("prNumber"), 12},
                     {QStringLiteral("prUrl"), QStringLiteral("https://github.com/o/n/pull/12")},
                     {QStringLiteral("prTitle"), QStringLiteral("Owners probe")},
                     {QStringLiteral("flavor"), QStringLiteral("github")}};
}

QStringList pingHandles(const QVariantList& rows) {
  QStringList out;
  for(const QVariant& v : rows) {
    out << v.toMap().value(QStringLiteral("handle")).toString();
  }
  return out;
}

}  // namespace

// The owners still to ask on my PR become "Кому написать" rows; "Написал"
// hides one until the owners to ask change; nothing is sent anywhere.
TEST_F(AppControllerTest, CodeOwnersStillToAskAreSidebarRows) {
  Task t = mkTask(QStringLiteral("HEAP-5"), QStringLiteral("owners"));
  t.branch = QStringLiteral("HEAP-5-owners");
  app_->tasks()->reset({t});
  QSignalSpy changed(app_.get(), &AppController::codeOwnersChanged);

  app_->setCodeOwnersReportForTest(
      QStringLiteral("/repo"), QStringLiteral("HEAP-5-owners"), ownersReport({QStringLiteral("@alice"), QStringLiteral("@org/team")}));
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(app_->codeOwnersOf(QStringLiteral("HEAP-5")).value(QStringLiteral("location")).toString(),
            QStringLiteral(".github/CODEOWNERS"));
  EXPECT_TRUE(app_->codeOwnersOf(QStringLiteral("HEAP-6")).isEmpty());

  QVariantList rows = app_->codeOwnerPings();
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(pingHandles(rows), (QStringList{QStringLiteral("@alice"), QStringLiteral("@org/team")}));
  const QVariantMap first = rows.first().toMap();
  EXPECT_EQ(first.value(QStringLiteral("taskId")).toString(), QStringLiteral("HEAP-5"));
  EXPECT_EQ(first.value(QStringLiteral("prNumber")).toInt(), 12);

  app_->dismissCodeOwnerPing(first.value(QStringLiteral("key")).toString());
  EXPECT_EQ(pingHandles(app_->codeOwnerPings()), QStringList{QStringLiteral("@org/team")});

  // The same report again (a poll) keeps it hidden; a new owner set brings it back.
  const int before = static_cast<int>(changed.count());
  app_->setCodeOwnersReportForTest(
      QStringLiteral("/repo"), QStringLiteral("HEAP-5-owners"), ownersReport({QStringLiteral("@alice"), QStringLiteral("@org/team")}));
  EXPECT_EQ(changed.count(), before) << "an unchanged report is not news";
  EXPECT_EQ(app_->codeOwnerPings().size(), 1);
  app_->setCodeOwnersReportForTest(
      QStringLiteral("/repo"), QStringLiteral("HEAP-5-owners"), ownersReport({QStringLiteral("@alice"), QStringLiteral("@bob")}));
  EXPECT_EQ(pingHandles(app_->codeOwnerPings()), (QStringList{QStringLiteral("@alice"), QStringLiteral("@bob")}));

  // The switch off clears them; a repo no longer watched takes its own along.
  app_->setAppSettingsJson(QStringLiteral(R"({"git":{"codeOwners":false}})"));
  EXPECT_TRUE(app_->codeOwnerPings().isEmpty());
  app_->setAppSettingsJson(QStringLiteral(R"({"git":{"codeOwners":true}})"));
  app_->setCodeOwnersReportForTest(QStringLiteral("/gone"), QStringLiteral("HEAP-5-owners"), ownersReport({QStringLiteral("@alice")}));
  EXPECT_EQ(app_->codeOwnerPings().size(), 1);
  app_->setAppSettingsJson(QStringLiteral(R"({"git":{"codeOwners":true,"watchedRepos":[]}})"));
  EXPECT_TRUE(app_->codeOwnerPings().isEmpty());
}

// What Settings needs for a repo on a dev server: tell one from a folder,
// keep one spelling, and test it without a server when it cannot be one.
TEST_F(AppControllerTest, RemoteRepoEntriesFromSettings) {
  EXPECT_TRUE(app_->isRemoteRepo(QStringLiteral("dev:/srv/app")));
  EXPECT_TRUE(app_->isRemoteRepo(QStringLiteral("ssh://me@dev:2222/srv/app")));
  EXPECT_FALSE(app_->isRemoteRepo(QStringLiteral("C:/src/app")));
  EXPECT_FALSE(app_->isRemoteRepo(QStringLiteral("/home/me/app")));
  EXPECT_EQ(app_->normalizeRepoEntry(QStringLiteral("  me@dev:/srv/app ")), QStringLiteral("me@dev:/srv/app"));
  EXPECT_EQ(app_->normalizeRepoEntry(QStringLiteral(" C:/src/app ")), QStringLiteral("C:/src/app"));
  EXPECT_TRUE(app_->remoteRepoStatus(QStringLiteral("C:/src/app")).isEmpty());
  EXPECT_TRUE(app_->remoteRepoStatus(QStringLiteral("dev:/not/watched")).isEmpty());

  QSignalSpy tested(app_.get(), &AppController::remoteRepoTested);
  app_->testRemoteRepo(QStringLiteral("C:/src/app"));
  ASSERT_EQ(tested.count(), 1);
  EXPECT_EQ(tested.first().at(1).toString(), QStringLiteral("other"));
}

// ─── headless boot ────────────────────────────────────────────────────

// ─── Time-aware save/edit (HEAP-115) ───

// The isNew guard used to drop the parsed clock time on every edit of an
// existing task. Editing one now keeps 09:00.
TEST_F(AppControllerTest, EditingAnExistingTaskKeepsTheParsedClockTime) {
  Task t = mkTask(QStringLiteral("EDIT-1"), QStringLiteral("standup"));
  t.scheduledAt = QDateTime(QDate(2026, 7, 10), QTime(0, 0));
  t.dueAt = t.scheduledAt;
  app_->tasks()->reset({t});

  QVariantMap draft = app_->taskById(QStringLiteral("EDIT-1"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("EDIT-1");
  draft["scheduledAt"] = QDateTime(QDate(2026, 7, 10), QTime(9, 0));
  draft["dueAt"] = QDateTime(QDate(2026, 7, 10), QTime(9, 0));
  draft["hasTime"] = true;
  app_->saveTask(draft);

  const int row = app_->tasks()->indexOfId(QStringLiteral("EDIT-1"));
  ASSERT_GE(row, 0);
  const Task& after = app_->tasks()->items().at(row);
  EXPECT_EQ(after.scheduledAt, QDateTime(QDate(2026, 7, 10), QTime(9, 0)));
  EXPECT_EQ(after.dueAt, QDateTime(QDate(2026, 7, 10), QTime(9, 0)));
  EXPECT_TRUE(after.dueHasTime);
  EXPECT_TRUE(after.scheduledHasTime);
}

// A caller that only knows a date (an old draft, an import) still works.
TEST_F(AppControllerTest, ALegacyDeadlineDraftKeyLandsAtMidnight) {
  QVariantMap draft = app_->newTaskDraft(QStringLiteral("todo"));
  draft["_isNew"] = true;
  draft["id"] = QStringLiteral("OLD-1");
  draft["title"] = QStringLiteral("bare date");
  draft["deadline"] = QDate(2026, 7, 8);
  app_->saveTask(draft);

  const int row = app_->tasks()->indexOfId(QStringLiteral("OLD-1"));
  ASSERT_GE(row, 0);
  const Task& t = app_->tasks()->items().at(row);
  EXPECT_EQ(t.dueAt, QDateTime(QDate(2026, 7, 8), QTime(0, 0)));
  EXPECT_FALSE(t.dueHasTime);
}

// snoozeDeadline shifts by whole days and keeps the clock time.
TEST_F(AppControllerTest, SnoozeShiftsBothDatetimesAndKeepsTheTime) {
  Task t = mkTask(QStringLiteral("SNZ-1"), QStringLiteral("ship"));
  t.dueAt = QDateTime(QDate(2026, 7, 10), QTime(16, 0));
  t.scheduledAt = t.dueAt;
  t.dueHasTime = true;
  t.scheduledHasTime = true;
  app_->tasks()->reset({t});

  app_->snoozeDeadline(QStringLiteral("SNZ-1"), 3600);

  const Task& after = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("SNZ-1")));
  EXPECT_EQ(after.dueAt, QDateTime(QDate(2026, 7, 11), QTime(16, 0)));
  EXPECT_EQ(after.scheduledAt, QDateTime(QDate(2026, 7, 11), QTime(16, 0)));
}

// ─── Pulled labels (HEAP-124) ───

TEST_F(AppControllerTest, PulledIssueLabelsArePersistedOntoTheTask) {
  heap::integrations::ExternalTask issue;
  issue.providerId = QStringLiteral("github");
  issue.externalId = QStringLiteral("68");
  issue.url = QStringLiteral("https://github.com/sectapunterx/heap/issues/68");
  issue.title = QStringLiteral("Portable build misses a DLL");
  issue.status = QStringLiteral("open");
  issue.labels = {QStringLiteral("bug"), QStringLiteral("windows")};

  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  const int row = app_->tasks()->indexOfId(QStringLiteral("github-68"));
  ASSERT_GE(row, 0);
  const Task& t = app_->tasks()->items().at(row);
  ASSERT_EQ(t.labels.size(), 2);
  EXPECT_EQ(t.labels.at(0).id, QStringLiteral("bug"));
  EXPECT_EQ(t.labels.at(1).id, QStringLiteral("windows"));
  EXPECT_EQ(t.externalProvider, QStringLiteral("github"));

  // …and they survive a save/reload of the editor draft.
  QVariantMap draft = app_->taskById(QStringLiteral("github-68"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("github-68");
  app_->saveTask(draft);
  const Task& after = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-68")));
  ASSERT_EQ(after.labels.size(), 2);
  EXPECT_EQ(after.labels.at(0).id, QStringLiteral("bug"));
}

// Regression (HEAP-104 review): a label the user adds locally to a synced task
// must survive the next pull. Sync used to clear() every label and re-add only
// the tracker's, silently deleting user-authored chips on each sync.
TEST_F(AppControllerTest, SyncPreservesUserAddedLocalLabels) {
  heap::integrations::ExternalTask issue;
  issue.providerId = QStringLiteral("github");
  issue.externalId = QStringLiteral("68");
  issue.url = QStringLiteral("https://github.com/sectapunterx/heap/issues/68");
  issue.title = QStringLiteral("Portable build misses a DLL");
  issue.status = QStringLiteral("open");
  issue.labels = {QStringLiteral("bug")};

  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  // The user adds a local chip the tracker knows nothing about.
  QVariantMap draft = app_->taskById(QStringLiteral("github-68"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("github-68");
  draft["labels"] =
      QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("bug")}, {QStringLiteral("color"), QString()}},
                   QVariantMap{{QStringLiteral("id"), QStringLiteral("urgent")}, {QStringLiteral("color"), QStringLiteral("#e6624c")}}};
  app_->saveTask(draft);

  // A second sync of the same issue — the tracker still only reports "bug".
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  const Task& after = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-68")));
  QStringList ids;
  for(const Label& l : after.labels) {
    ids << l.id;
  }
  EXPECT_TRUE(ids.contains(QStringLiteral("urgent"))) << "sync wiped the user-added label";
  EXPECT_TRUE(ids.contains(QStringLiteral("bug")));
}

// ─── Task ids must never destroy a task ───
// saveTask ends in upsert(), and TaskModel::upsert on an id that is already
// taken replaces that row outright. Three ways in, all silent, none undoable.

// The proposed id was "2700 + rowCount()". The count drops when a task is
// deleted, so the generator walks back over ids that are still in use.
TEST_F(AppControllerTest, ANewTasksProposedIdIsNeverOneAlreadyInUse) {
  app_->tasks()->reset({});

  const auto create = [this](const QString& title) {
    QVariantMap draft = app_->newTaskDraft(QStringLiteral("todo"));
    draft["_isNew"] = true;
    draft["title"] = title;
    app_->saveTask(draft);
    return draft.value(QStringLiteral("id")).toString();
  };

  const QString first = create(QStringLiteral("A"));
  const QString second = create(QStringLiteral("B"));
  ASSERT_NE(first, second);
  ASSERT_EQ(app_->tasks()->rowCount(), 2);

  // Delete the older one: the row count is now 1 again.
  app_->deleteTask(first);
  ASSERT_EQ(app_->tasks()->rowCount(), 1);

  const QString third = create(QStringLiteral("C"));
  EXPECT_NE(third, second) << "the new task reused a live id";
  ASSERT_EQ(app_->tasks()->rowCount(), 2) << "a task was overwritten";
  const int survivor = app_->tasks()->indexOfId(second);
  ASSERT_GE(survivor, 0);
  EXPECT_EQ(app_->tasks()->items().at(survivor).title, QStringLiteral("B")) << "B was replaced by C";
}

// Even when something else proposes the id, the save itself has to refuse.
TEST_F(AppControllerTest, CreatingATaskOnATakenIdIsRefusedNotMerged) {
  Task existing;
  existing.id = QStringLiteral("LTE-2700");
  existing.title = QStringLiteral("do not lose me");
  app_->tasks()->reset({existing});

  QVariantMap draft = app_->newTaskDraft(QStringLiteral("todo"));
  draft["_isNew"] = true;
  draft["id"] = QStringLiteral("LTE-2700");
  draft["title"] = QStringLiteral("the impostor");
  app_->saveTask(draft);

  ASSERT_EQ(app_->tasks()->rowCount(), 1);
  EXPECT_EQ(app_->tasks()->items().at(0).title, QStringLiteral("do not lose me"));
}

// Renaming A onto B's id used to remove A's row and overwrite B's — two tasks
// collapsing into one, with A's calendar events re-pointed at the survivor.
TEST_F(AppControllerTest, RenamingATaskOntoAnotherTasksIdIsRefused) {
  Task a;
  a.id = QStringLiteral("LTE-2700");
  a.title = QStringLiteral("A");
  Task b;
  b.id = QStringLiteral("LTE-2701");
  b.title = QStringLiteral("B");
  app_->tasks()->reset({a, b});

  QVariantMap draft = app_->taskById(QStringLiteral("LTE-2700"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("LTE-2700");
  draft["id"] = QStringLiteral("LTE-2701");  // B's id
  app_->saveTask(draft);

  ASSERT_EQ(app_->tasks()->rowCount(), 2) << "one of the two tasks was destroyed";
  const int rowA = app_->tasks()->indexOfId(QStringLiteral("LTE-2700"));
  const int rowB = app_->tasks()->indexOfId(QStringLiteral("LTE-2701"));
  ASSERT_GE(rowA, 0) << "the renamed task vanished";
  ASSERT_GE(rowB, 0);
  EXPECT_EQ(app_->tasks()->items().at(rowA).title, QStringLiteral("A"));
  EXPECT_EQ(app_->tasks()->items().at(rowB).title, QStringLiteral("B")) << "B was overwritten by A";
}

// A rename to a free id still has to work.
TEST_F(AppControllerTest, RenamingATaskToAFreeIdStillWorks) {
  Task a;
  a.id = QStringLiteral("LTE-2700");
  a.title = QStringLiteral("A");
  app_->tasks()->reset({a});

  QVariantMap draft = app_->taskById(QStringLiteral("LTE-2700"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("LTE-2700");
  draft["id"] = QStringLiteral("LTE-9000");
  app_->saveTask(draft);

  EXPECT_LT(app_->tasks()->indexOfId(QStringLiteral("LTE-2700")), 0);
  const int row = app_->tasks()->indexOfId(QStringLiteral("LTE-9000"));
  ASSERT_GE(row, 0);
  EXPECT_EQ(app_->tasks()->items().at(row).title, QStringLiteral("A"));
}

// Saving an unchanged task is not a rename, so the guard must not fire on it.
TEST_F(AppControllerTest, SavingATaskUnderItsOwnIdIsNotTreatedAsACollision) {
  Task a;
  a.id = QStringLiteral("LTE-2700");
  a.title = QStringLiteral("A");
  app_->tasks()->reset({a});

  QVariantMap draft = app_->taskById(QStringLiteral("LTE-2700"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("LTE-2700");
  draft["title"] = QStringLiteral("A, edited");
  app_->saveTask(draft);

  const int row = app_->tasks()->indexOfId(QStringLiteral("LTE-2700"));
  ASSERT_GE(row, 0);
  EXPECT_EQ(app_->tasks()->items().at(row).title, QStringLiteral("A, edited"));
}

// ─── Ticket identity and the self-scope collision (HEAP-117) ───

namespace {

// One pulled issue, with only the fields a test cares about set.
heap::integrations::ExternalTask ghIssue(const QString& id, const QString& url, const QString& title = QStringLiteral("t")) {
  heap::integrations::ExternalTask e;
  e.providerId = QStringLiteral("github");
  e.externalId = id;
  e.url = url;
  e.title = title;
  e.status = QStringLiteral("open");
  return e;
}

}  // namespace

// The bug: an "assigned to me" pull spans repos, and GitHub's externalId is the
// bare issue number. #5 of repo A and #5 of repo B used to collapse onto one
// task that flip-flopped between them on every sync.
TEST_F(AppControllerTest, SelfScopeSameNumberInTwoReposStaysTwoTasks) {
  auto a = ghIssue(QStringLiteral("5"), QStringLiteral("https://github.com/acme/web/issues/5"), QStringLiteral("web five"));
  a.project = QStringLiteral("acme/web");
  a.crossProject = true;
  auto b = ghIssue(QStringLiteral("5"), QStringLiteral("https://github.com/acme/api/issues/5"), QStringLiteral("api five"));
  b.project = QStringLiteral("acme/api");
  b.crossProject = true;

  app_->tasks()->reset({});
  const auto first = app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {a, b});
  EXPECT_EQ(first.added, 2);
  EXPECT_EQ(app_->tasks()->rowCount(), 2);

  // Re-syncing the same two issues must not add, rename or re-clobber anything.
  const auto second = app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {a, b});
  EXPECT_EQ(second.added, 0);
  EXPECT_EQ(second.updated, 0);
  ASSERT_EQ(app_->tasks()->rowCount(), 2);

  QStringList titles;
  for(const Task& t : app_->tasks()->items()) {
    titles << t.title;
  }
  titles.sort();
  EXPECT_EQ(titles, (QStringList{QStringLiteral("api five"), QStringLiteral("web five")}));
  // The ids say which repo each came from.
  for(const Task& t : app_->tasks()->items()) {
    EXPECT_TRUE(t.id.contains(QStringLiteral("web")) || t.id.contains(QStringLiteral("api"))) << t.id.toStdString();
  }
}

// A task stored by an older build has no project recorded. It is matched by URL
// and heals in one sync; the other repo's issue gets a task of its own.
TEST_F(AppControllerTest, ACollapsedLegacyTaskHealsOnTheNextSync) {
  Task legacy;
  legacy.id = QStringLiteral("github-5");
  legacy.externalId = QStringLiteral("5");
  legacy.externalProvider = QStringLiteral("github");
  legacy.externalUrl = QStringLiteral("https://github.com/acme/api/issues/5");
  legacy.title = QStringLiteral("stale");
  app_->tasks()->reset({legacy});

  auto web = ghIssue(QStringLiteral("5"), QStringLiteral("https://github.com/acme/web/issues/5"), QStringLiteral("web five"));
  web.project = QStringLiteral("acme/web");
  web.crossProject = true;
  auto api = ghIssue(QStringLiteral("5"), QStringLiteral("https://github.com/acme/api/issues/5"), QStringLiteral("api five"));
  api.project = QStringLiteral("acme/api");
  api.crossProject = true;

  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {web, api});
  ASSERT_EQ(app_->tasks()->rowCount(), 2);
  // The legacy row kept its id and its own issue, matched by URL.
  const int legacyRow = app_->tasks()->indexOfId(QStringLiteral("github-5"));
  ASSERT_GE(legacyRow, 0);
  EXPECT_EQ(app_->tasks()->items().at(legacyRow).title, QStringLiteral("api five"));
}

// upsert() on a colliding id replaces the other row, so a new task's id has to
// be checked even when nothing about the tracker is ambiguous.
TEST_F(AppControllerTest, ANewTicketNeverOverwritesAnExistingTask) {
  Task local;
  local.id = QStringLiteral("github-7");
  local.title = QStringLiteral("hand-made, not a ticket");
  app_->tasks()->reset({local});

  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("7"), QStringLiteral("https://github.com/acme/web/issues/7"))});

  ASSERT_EQ(app_->tasks()->rowCount(), 2) << "the local task was overwritten";
  const int row = app_->tasks()->indexOfId(QStringLiteral("github-7"));
  ASSERT_GE(row, 0);
  EXPECT_EQ(app_->tasks()->items().at(row).title, QStringLiteral("hand-made, not a ticket"));
}

TEST_F(AppControllerTest, SyncWritesAndClearsTheTrackerAssignee) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.assignee = QStringLiteral("ada");
  issue.author = QStringLiteral("grace");
  issue.commentCount = 3;
  issue.project = QStringLiteral("acme/web");

  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});
  const int row = app_->tasks()->indexOfId(QStringLiteral("github-9"));
  ASSERT_GE(row, 0);
  EXPECT_EQ(app_->tasks()->items().at(row).assignee, QStringLiteral("ada"));
  EXPECT_EQ(app_->tasks()->items().at(row).externalMeta.author, QStringLiteral("grace"));
  EXPECT_EQ(app_->tasks()->items().at(row).externalMeta.commentCount, 3);

  // Unassigned upstream → unassigned here.
  issue.assignee.clear();
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});
  EXPECT_TRUE(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-9"))).assignee.isEmpty());
}

TEST_F(AppControllerTest, TrackerDueDateFillsAnEmptyDeadline) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.dueAt = QDateTime(QDate(2026, 8, 15), QTime(0, 0));
  issue.dueHasTime = false;

  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});
  const Task& t = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-9")));
  EXPECT_EQ(t.dueAt, QDateTime(QDate(2026, 8, 15), QTime(0, 0)));
  EXPECT_FALSE(t.dueHasTime);
}

// The whole point of storing the tracker's due date: telling "the user moved
// this" apart from "the tracker moved this".
TEST_F(AppControllerTest, ALocallyEditedDeadlineSurvivesTheNextSync) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.dueAt = QDateTime(QDate(2026, 8, 15), QTime(0, 0));

  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  QVariantMap draft = app_->taskById(QStringLiteral("github-9"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("github-9");
  draft["dueAt"] = QDateTime(QDate(2026, 9, 1), QTime(18, 0));
  app_->saveTask(draft);

  // The tracker still says the 15th; the user's own date has to win.
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});
  const Task& t = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-9")));
  EXPECT_EQ(heap::local::effectiveDueAt(t), QDateTime(QDate(2026, 9, 1), QTime(18, 0)));
  EXPECT_EQ(t.dueAt, issue.dueAt) << "the tracker's field stays the tracker's (APP-238)";
}

TEST_F(AppControllerTest, ADeadlineRemovedUpstreamClearsOnlyWhenSyncOwned) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.dueAt = QDateTime(QDate(2026, 8, 15), QTime(0, 0));
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  issue.dueAt = QDateTime();  // the due date is dropped upstream
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});
  EXPECT_FALSE(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-9"))).dueAt.isValid());

  // Whereas a deadline the user set on an issue that never had one stays put.
  auto other = ghIssue(QStringLiteral("10"), QStringLiteral("https://github.com/acme/web/issues/10"));
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {other});
  QVariantMap draft = app_->taskById(QStringLiteral("github-10"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("github-10");
  draft["dueAt"] = QDateTime(QDate(2026, 9, 1), QTime(18, 0));
  app_->saveTask(draft);
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {other});
  EXPECT_EQ(heap::local::effectiveDueAt(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-10")))),
            QDateTime(QDate(2026, 9, 1), QTime(18, 0)));
}

TEST_F(AppControllerTest, ASnoozedDeadlineIsNotSnappedBackBySync) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.dueAt = QDateTime(QDate(2026, 8, 15), QTime(9, 0));
  issue.dueHasTime = true;
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  app_->snoozeDeadline(QStringLiteral("github-9"), 60);
  const QDateTime snoozed = heap::local::effectiveDueAt(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-9"))));
  ASSERT_NE(snoozed, issue.dueAt);

  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});
  EXPECT_EQ(heap::local::effectiveDueAt(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-9")))), snoozed);
}

// The editor rewrites labels from comma text, which drops every colour. The
// next pull has to restore them without touching a colour the user chose.
TEST_F(AppControllerTest, LabelColoursFillOnlyEmptyChips) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.labels = {QStringLiteral("bug"), QStringLiteral("ci")};
  issue.labelColors = {{QStringLiteral("bug"), QStringLiteral("#d73a4a")}, {QStringLiteral("ci"), QStringLiteral("#0000ff")}};

  Task existing;
  existing.id = QStringLiteral("github-9");
  existing.externalId = QStringLiteral("9");
  existing.externalProvider = QStringLiteral("github");
  existing.externalUrl = issue.url;
  existing.labels = {Label{QStringLiteral("ci"), QStringLiteral("#123456")}};  // the user picked this one
  app_->tasks()->reset({existing});

  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});
  const Task& t = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-9")));
  QHash<QString, QString> byName;
  for(const Label& l : t.labels) {
    byName.insert(l.id, l.color);
  }
  EXPECT_EQ(byName.value(QStringLiteral("bug")), QStringLiteral("#d73a4a"));
  EXPECT_EQ(byName.value(QStringLiteral("ci")), QStringLiteral("#123456")) << "sync overwrote a user-chosen colour";
}

TEST_F(AppControllerTest, AnEditorSavePreservesTheTrackerMetadata) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.author = QStringLiteral("grace");
  issue.project = QStringLiteral("acme/web");
  issue.milestone = QStringLiteral("v2");
  issue.commentCount = 5;
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  QVariantMap draft = app_->taskById(QStringLiteral("github-9"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("github-9");
  draft["title"] = QStringLiteral("edited locally");
  app_->saveTask(draft);

  const Task& t = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("github-9")));
  EXPECT_EQ(t.externalMeta.author, QStringLiteral("grace"));
  EXPECT_EQ(t.externalMeta.project, QStringLiteral("acme/web"));
  EXPECT_EQ(t.externalMeta.milestone, QStringLiteral("v2"));
  EXPECT_EQ(t.externalMeta.commentCount, 5);
}

TEST_F(AppControllerTest, TheEditorDraftCarriesTheTicketMap) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.author = QStringLiteral("grace");
  issue.issueType = QStringLiteral("Bug");
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  const QVariantMap ticket = app_->taskById(QStringLiteral("github-9")).value(QStringLiteral("ticket")).toMap();
  EXPECT_EQ(ticket.value(QStringLiteral("provider")).toString(), QStringLiteral("github"));
  EXPECT_EQ(ticket.value(QStringLiteral("key")).toString(), QStringLiteral("#9"));
  EXPECT_EQ(ticket.value(QStringLiteral("author")).toString(), QStringLiteral("grace"));
  EXPECT_EQ(ticket.value(QStringLiteral("issueType")).toString(), QStringLiteral("Bug"));

  Task local;
  local.id = QStringLiteral("LOCAL-1");
  app_->tasks()->reset({local});
  EXPECT_TRUE(app_->taskById(QStringLiteral("LOCAL-1")).value(QStringLiteral("ticket")).toMap().isEmpty());
}

// A completed recurring ticket spawns a local occurrence, which must not carry
// the original issue's owner or metadata.
TEST_F(AppControllerTest, ARecurrenceCloneDropsTheTicketIdentity) {
  auto issue = ghIssue(QStringLiteral("9"), QStringLiteral("https://github.com/acme/web/issues/9"));
  issue.assignee = QStringLiteral("ada");
  issue.author = QStringLiteral("grace");
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});

  QVariantMap draft = app_->taskById(QStringLiteral("github-9"));
  draft["_isNew"] = false;
  draft["_originalId"] = QStringLiteral("github-9");
  draft["recurrence"] = QStringLiteral("every:week");
  draft["dueAt"] = QDateTime(QDate(2026, 8, 15), QTime(9, 0));
  app_->saveTask(draft);
  app_->moveTask(QStringLiteral("github-9"), QStringLiteral("done"));

  const Task* clone = nullptr;
  for(const Task& t : app_->tasks()->items()) {
    if(t.id != QStringLiteral("github-9")) {
      clone = &t;
    }
  }
  ASSERT_NE(clone, nullptr) << "the recurrence clone was not created";
  EXPECT_TRUE(clone->externalId.isEmpty());
  EXPECT_TRUE(clone->assignee.isEmpty());
  EXPECT_EQ(clone->externalMeta, ExternalMeta{});
}

// ─── Sync must not undo what the user did ───
// The contact half of this code has guarded against both of these since it
// was written; the task half never did.

// Deleting a mirrored issue is how the user says "not mine". The next pull
// used to put it straight back, which made the deletion meaningless.
TEST_F(AppControllerTest, ADeletedTicketDoesNotComeBackOnTheNextSync) {
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("1"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("1"))});
  ASSERT_GE(app_->tasks()->indexOfId(QStringLiteral("github-1")), 0);

  app_->deleteTask(QStringLiteral("github-1"));
  app_->clearPendingUndo();
  ASSERT_LT(app_->tasks()->indexOfId(QStringLiteral("github-1")), 0);

  // The tracker still reports it; heap must not.
  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("1"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("1"))});
  EXPECT_LT(app_->tasks()->indexOfId(QStringLiteral("github-1")), 0) << "the deleted ticket was re-created by sync";

  // Other issues are unaffected.
  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("2"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("2"))});
  EXPECT_GE(app_->tasks()->indexOfId(QStringLiteral("github-2")), 0);

  app_->restoreExternalTask(QStringLiteral("github"), QStringLiteral("1"));
}

// …and undoing the delete withdraws that, so it syncs normally again.
TEST_F(AppControllerTest, UndoingTheDeleteLetsTheTicketSyncAgain) {
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("1"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("1"))});
  app_->deleteTask(QStringLiteral("github-1"));
  app_->undoLastDeletion();
  ASSERT_GE(app_->tasks()->indexOfId(QStringLiteral("github-1")), 0);

  app_->tasks()->reset({});  // as if the profile were reloaded
  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("1"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("1"))});
  EXPECT_GE(app_->tasks()->indexOfId(QStringLiteral("github-1")), 0) << "undo did not withdraw the dismissal";
}

TEST_F(AppControllerTest, DeletingASelectionOfTicketsDismissesAllOfThem) {
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("1"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("1")),
                            ghIssue(QStringLiteral("2"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("2"))});
  app_->setSelectedTaskIds({QStringLiteral("github-1"), QStringLiteral("github-2")});
  app_->deleteSelectedTasks();
  app_->clearPendingUndo();
  ASSERT_EQ(app_->tasks()->rowCount(), 0);

  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("1"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("1")),
                            ghIssue(QStringLiteral("2"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("2"))});
  EXPECT_EQ(app_->tasks()->rowCount(), 0) << "a bulk delete of tickets was undone by sync";

  app_->restoreExternalTask(QStringLiteral("github"), QStringLiteral("1"));
  app_->restoreExternalTask(QStringLiteral("github"), QStringLiteral("2"));
}

// The integration config is global and the sync timer keeps running across a
// profile switch, so a background pull could pour one profile's tickets into
// whichever profile happened to be open.
TEST_F(AppControllerTest, AutoSyncIntoAnUnboundProfileIsANoopForTasks) {
  app_->tasks()->reset({});
  // The first merge binds the card to the profile it ran in.
  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("1"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("1"))});
  EXPECT_EQ(readIntegrationConfig(QStringLiteral("github")).value(QStringLiteral("profileId")).toString(), app_->activeProfileId());
  ASSERT_EQ(app_->tasks()->rowCount(), 1);

  // A timer tick while another profile is open must not write into it.
  QJsonObject cfg = readIntegrationConfig(QStringLiteral("github"));
  cfg.insert(QStringLiteral("profileId"), QStringLiteral("some-other-profile"));
  writeIntegrationConfig(QStringLiteral("github"), cfg);

  app_->mergeExternalTasks(QStringLiteral("github"),
                           QStringLiteral("github-"),
                           {ghIssue(QStringLiteral("2"), QStringLiteral("https://github.com/acme/web/issues/") + QStringLiteral("2"))});
  EXPECT_EQ(app_->tasks()->rowCount(), 1) << "a background sync imported into the wrong profile";
  EXPECT_LT(app_->tasks()->indexOfId(QStringLiteral("github-2")), 0);

  writeIntegrationConfig(QStringLiteral("github"), QJsonObject{});
}

// "Sync now" means "sync this, here" — a deliberate click rebinds the card.
TEST_F(AppControllerTest, AManualSyncRebindsTheProviderToTheOpenProfile) {
  writeIntegrationConfig(QStringLiteral("gitea"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("host"), QStringLiteral("http://127.0.0.1:1")},  // never contacted
                             {QStringLiteral("repo"), QStringLiteral("acme/web")},
                             {QStringLiteral("profileId"), QStringLiteral("some-other-profile")},
                         });
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QStringLiteral("tok"));

  app_->syncProvider(QStringLiteral("gitea"));
  EXPECT_EQ(readIntegrationConfig(QStringLiteral("gitea")).value(QStringLiteral("profileId")).toString(), app_->activeProfileId());

  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QString());
}

// "Sync everything" by hand binds every tracker here, as each card's own
// Sync now does: a GitLab bound to another profile had its MRs dropped
// unmerged, so they never folded into the Jira issues open here (0.9.4).
// The auto-sync tick moves nothing.
TEST_F(AppControllerTest, SyncEverythingRebindsEveryTracker_TheTimerDoesNot) {
  writeIntegrationConfig(QStringLiteral("gitea"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("host"), QStringLiteral("http://127.0.0.1:1")},  // never contacted
                             {QStringLiteral("repo"), QStringLiteral("acme/web")},
                             {QStringLiteral("profileId"), QStringLiteral("some-other-profile")},
                         });
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QStringLiteral("tok"));

  app_->syncNow(/*here=*/false);
  EXPECT_EQ(readIntegrationConfig(QStringLiteral("gitea")).value(QStringLiteral("profileId")).toString(),
            QStringLiteral("some-other-profile"));

  app_->syncNow();
  EXPECT_EQ(readIntegrationConfig(QStringLiteral("gitea")).value(QStringLiteral("profileId")).toString(), app_->activeProfileId());

  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QString());
}

// ─── A branch finds its own ticket ───
// A mirrored issue's heap id carries the provider ("jira-PROJ-123"), while the
// branch anyone writes for it names the tracker key ("PROJ-123").

TEST_F(AppControllerTest, ProjectKeysOfMirroredIssuesAreRegisteredAsPrefixes) {
  Task jira;
  jira.id = QStringLiteral("jira-PROJ-123");
  jira.externalId = QStringLiteral("PROJ-123");
  jira.externalProvider = QStringLiteral("jira");
  Task local;
  local.id = QStringLiteral("LTE-2700");
  Task numbered;  // a bare issue number has no key to register
  numbered.id = QStringLiteral("github-42");
  numbered.externalId = QStringLiteral("42");
  numbered.externalProvider = QStringLiteral("github");
  app_->tasks()->reset({jira, local, numbered});

  const QStringList prefixes = app_->collectPrefixes();
  EXPECT_TRUE(prefixes.contains(QStringLiteral("PROJ"))) << prefixes.join(QStringLiteral(",")).toStdString();
  EXPECT_FALSE(prefixes.contains(QStringLiteral("GITHUB")));
  EXPECT_GE(prefixes.size(), 2) << "the configured local prefix must still be there";
}

TEST_F(AppControllerTest, ATrackerKeyResolvesToTheTaskThatMirrorsIt) {
  Task jira;
  jira.id = QStringLiteral("jira-PROJ-123");
  jira.externalId = QStringLiteral("PROJ-123");
  jira.externalProvider = QStringLiteral("jira");
  Task local;
  local.id = QStringLiteral("LTE-2700");
  app_->tasks()->reset({jira, local});

  EXPECT_EQ(app_->taskIdForBranchMatch(QStringLiteral("PROJ-123")), QStringLiteral("jira-PROJ-123"));
  // Case-insensitively, because a branch name is usually lowercased.
  EXPECT_EQ(app_->taskIdForBranchMatch(QStringLiteral("proj-123")), QStringLiteral("jira-PROJ-123"));
  // A local task's key IS its id.
  EXPECT_EQ(app_->taskIdForBranchMatch(QStringLiteral("LTE-2700")), QStringLiteral("LTE-2700"));
  // Nothing to resolve to: hand the key back rather than inventing a task.
  EXPECT_EQ(app_->taskIdForBranchMatch(QStringLiteral("NOPE-1")), QStringLiteral("NOPE-1"));
  EXPECT_TRUE(app_->taskIdForBranchMatch(QString()).isEmpty());
}

// The loop has to close: a branch heap creates for a ticket must match back to
// that same ticket. It used to be named after the heap id, which never did.
TEST_F(AppControllerTest, ABranchCreatedForATicketIsNamedAfterItsTrackerKey) {
  Task jira;
  jira.id = QStringLiteral("jira-PROJ-123");
  jira.title = QStringLiteral("Handover fails");
  jira.externalId = QStringLiteral("PROJ-123");
  jira.externalProvider = QStringLiteral("jira");
  app_->tasks()->reset({jira});

  const QString branch =
      heap::git::BranchTaskMatcher::branchNameForTask(QStringLiteral("PROJ-123"), jira.title, QStringLiteral("feature/{id}-{slug}"));
  const heap::git::BranchTaskMatcher m(app_->collectPrefixes());
  const auto mr = m.extract(branch);
  ASSERT_TRUE(mr.matched) << branch.toStdString();
  EXPECT_EQ(app_->taskIdForBranchMatch(mr.taskId), QStringLiteral("jira-PROJ-123"));
}

// ─── Cached hot paths ───
// Both are caches, so what needs pinning is that they answer correctly after
// the thing they cache has changed.

TEST_F(AppControllerTest, StatusCountsAreOnePassAndFollowTheModel) {
  Task a;
  a.id = QStringLiteral("A");
  a.status = QStringLiteral("todo");
  Task b;
  b.id = QStringLiteral("B");
  b.status = QStringLiteral("todo");
  Task c;
  c.id = QStringLiteral("C");
  c.status = QStringLiteral("prog");
  app_->tasks()->reset({a, b, c});

  QVariantMap counts = app_->statusCounts();
  EXPECT_EQ(counts.value(QStringLiteral("todo")).toInt(), 2);
  EXPECT_EQ(counts.value(QStringLiteral("prog")).toInt(), 1);
  // A status nobody holds is absent, and reads as zero.
  EXPECT_EQ(counts.value(QStringLiteral("done")).toInt(), 0);
  // countByStatus is now a lookup into the same map.
  EXPECT_EQ(app_->countByStatus(QStringLiteral("todo")), 2);
  EXPECT_EQ(app_->countByStatus(QStringLiteral("done")), 0);

  // Moving a task must be reflected, not served from the cache.
  app_->tasks()->setStatus(QStringLiteral("A"), QStringLiteral("prog"));
  EXPECT_EQ(app_->countByStatus(QStringLiteral("todo")), 1);
  EXPECT_EQ(app_->countByStatus(QStringLiteral("prog")), 2);

  // …as must adding, removing and replacing the whole model.
  Task d;
  d.id = QStringLiteral("D");
  d.status = QStringLiteral("todo");
  app_->tasks()->upsert(d);
  EXPECT_EQ(app_->countByStatus(QStringLiteral("todo")), 2);
  app_->tasks()->removeById(QStringLiteral("B"));
  EXPECT_EQ(app_->countByStatus(QStringLiteral("todo")), 1);
  app_->tasks()->reset({});
  EXPECT_EQ(app_->countByStatus(QStringLiteral("todo")), 0);
  EXPECT_TRUE(app_->statusCounts().isEmpty());
}

// APP-121: the id → title map the markdown views use follows the model,
// and stays quiet when nothing it holds changed.
TEST_F(AppControllerTest, TaskTitlesFollowTheModel) {
  Task a;
  a.id = QStringLiteral("A-1");
  a.title = QStringLiteral("First");
  a.status = QStringLiteral("todo");
  app_->tasks()->reset({a});
  QCoreApplication::processEvents();
  EXPECT_EQ(app_->taskTitles().value(QStringLiteral("A-1")).toString(), QStringLiteral("First"));

  int changes = 0;
  QObject::connect(app_.get(), &AppController::taskTitlesChanged, [&changes]() {
    ++changes;
  });
  app_->tasks()->setStatus(QStringLiteral("A-1"), QStringLiteral("prog"));
  QCoreApplication::processEvents();
  EXPECT_EQ(changes, 0) << "a status move is not a title change";

  a.title = QStringLiteral("Renamed");
  a.status = QStringLiteral("prog");
  app_->tasks()->upsert(a);
  QCoreApplication::processEvents();
  EXPECT_EQ(changes, 1);
  EXPECT_EQ(app_->taskTitles().value(QStringLiteral("A-1")).toString(), QStringLiteral("Renamed"));
}

// APP-123: a periodic sync can wait up to a month. The timer only checks
// (never less often than hourly) and the last sync's time decides.
TEST(AutoSyncTest, DueByTheLastSyncNotTheTimer) {
  using namespace heap::integrations;
  const QDateTime t0(QDate(2026, 10, 1), QTime(9, 0));
  EXPECT_FALSE(autoSyncDue(t0, t0.addDays(400), 0)) << "off is never due";
  EXPECT_TRUE(autoSyncDue({}, t0, 15)) << "never synced is due";
  EXPECT_FALSE(autoSyncDue(t0, t0.addSecs(14 * 60), 15));
  EXPECT_TRUE(autoSyncDue(t0, t0.addSecs(15 * 60 - 1), 15)) << "a timer a hair early still counts";
  const int month = 30 * 24 * 60;
  EXPECT_FALSE(autoSyncDue(t0, t0.addDays(29), month));
  EXPECT_TRUE(autoSyncDue(t0, t0.addDays(30), month));
  EXPECT_EQ(autoSyncCheckMinutes(15), 15);
  EXPECT_EQ(autoSyncCheckMinutes(month), 60) << "a month-long wait is checked hourly";
  EXPECT_LE(static_cast<qint64>(autoSyncCheckMinutes(kMaxAutoSyncMinutes)) * 60 * 1000, static_cast<qint64>(INT_MAX));
}

// With no tracker connected the check does nothing, and records nothing.
TEST_F(AppControllerTest, AutoSyncTickWithoutTrackersIsQuiet) {
  app_->setAppSettingsJson(QStringLiteral(R"({"integrations":{"autoSyncMinutes":15}})"));
  const QDateTime before = app_->lastTrackerSync();
  app_->autoSyncTickAt(QDateTime::currentDateTime().addDays(1));
  EXPECT_EQ(app_->lastTrackerSync(), before);
}

TEST_F(AppControllerTest, SettingsMapIsCachedButNeverStale) {
  app_->setAppSettingsJson(QStringLiteral(R"({"tasks":{"idPrefix":"AAA"}})"));
  EXPECT_EQ(app_->settingsMapForTest().value(QStringLiteral("tasks")).toMap().value(QStringLiteral("idPrefix")).toString(),
            QStringLiteral("AAA"));
  // Same string twice — the second read comes from the cache and must match.
  EXPECT_EQ(app_->settingsMapForTest(), app_->settingsMapForTest());

  app_->setAppSettingsJson(QStringLiteral(R"({"tasks":{"idPrefix":"BBB"}})"));
  EXPECT_EQ(app_->settingsMapForTest().value(QStringLiteral("tasks")).toMap().value(QStringLiteral("idPrefix")).toString(),
            QStringLiteral("BBB"))
      << "the cache outlived the settings it was built from";

  // Malformed and empty both answer an empty map rather than the last good one.
  app_->setAppSettingsJson(QStringLiteral("{not json"));
  EXPECT_TRUE(app_->settingsMapForTest().isEmpty());
  app_->setAppSettingsJson(QString());
  EXPECT_TRUE(app_->settingsMapForTest().isEmpty());
}

TEST_F(AppControllerTest, EstimateAndSomedayRoundTripThroughTheEditorDraft) {
  QVariantMap draft = app_->newTaskDraft(QStringLiteral("todo"));
  draft["_isNew"] = true;
  draft["id"] = QStringLiteral("EST-1");
  draft["title"] = QStringLiteral("plan the epic");
  draft["estimateMinutes"] = 480;
  draft["someday"] = true;
  draft["labels"] = QVariantList{QStringLiteral("infra")};
  app_->saveTask(draft);

  const Task& t = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("EST-1")));
  EXPECT_EQ(t.estimateMinutes, 480);
  EXPECT_TRUE(t.someday);
  ASSERT_EQ(t.labels.size(), 1);
  EXPECT_EQ(t.labels.at(0).id, QStringLiteral("infra"));
}

// A task saved as "someday" is filed under the backlog column, whatever status
// the editor sent — that is what the checkbox means (parked, not now).
TEST_F(AppControllerTest, SomedayTaskIsFiledUnderBacklog) {
  QVariantMap draft = app_->newTaskDraft(QStringLiteral("todo"));
  draft["_isNew"] = true;
  draft["id"] = QStringLiteral("SD-1");
  draft["title"] = QStringLiteral("park me");
  draft["status"] = QStringLiteral("prog");  // deliberately not backlog
  draft["someday"] = true;
  app_->saveTask(draft);

  const int row = app_->tasks()->indexOfId(QStringLiteral("SD-1"));
  ASSERT_GE(row, 0);
  const Task& t = app_->tasks()->items().at(row);
  EXPECT_EQ(t.status, QStringLiteral("backlog")) << "a someday task must land in the backlog";
  EXPECT_TRUE(t.someday);

  // Clearing someday leaves the status alone (no forced move back).
  QVariantMap edit = app_->taskById(QStringLiteral("SD-1"));
  edit["_isNew"] = false;
  edit["_originalId"] = QStringLiteral("SD-1");
  edit["status"] = QStringLiteral("todo");
  edit["someday"] = false;
  app_->saveTask(edit);
  const Task& t2 = app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("SD-1")));
  EXPECT_FALSE(t2.someday);
  EXPECT_EQ(t2.status, QStringLiteral("todo")) << "un-parking must not force a column";
}

// The epic-level contract (HEAP-104): "review PR tomorrow 3pm" keeps its 3pm
// through capture, an edit of the existing task, save, reload and export.
TEST_F(AppControllerTest, ThreePmSurvivesCaptureEditSaveReloadAndExport) {
  const QDateTime reference(QDate(2026, 7, 9), QTime(10, 0));
  const QVariantMap parsed = app_->parseDateTime(QStringLiteral("review PR tomorrow 3pm"), reference);
  ASSERT_TRUE(parsed.value(QStringLiteral("ok")).toBool());
  ASSERT_TRUE(parsed.value(QStringLiteral("hasTime")).toBool());
  const QDateTime at = parsed.value(QStringLiteral("start")).toDateTime();
  ASSERT_EQ(at, QDateTime(QDate(2026, 7, 10), QTime(15, 0)));

  // Capture, exactly as QuickCapturePopup does.
  QVariantMap draft = app_->newQuickTaskDraft();
  draft["_isNew"] = true;
  draft["title"] = QStringLiteral("review PR");
  draft["scheduledAt"] = at;
  draft["dueAt"] = at;
  draft["hasTime"] = true;
  app_->saveTask(draft);
  const QString id = draft.value("id").toString();

  // Edit the existing task without touching the time (the old isNew guard's bug).
  QVariantMap edit = app_->taskById(id);
  edit["_isNew"] = false;
  edit["_originalId"] = id;
  edit["title"] = QStringLiteral("review PR (urgent)");
  app_->saveTask(edit);

  const Task& afterEdit = app_->tasks()->items().at(app_->tasks()->indexOfId(id));
  EXPECT_EQ(afterEdit.dueAt, at) << "the edit dropped the clock time";
  EXPECT_TRUE(afterEdit.dueHasTime);

  // Save, then reload from disk in a fresh controller.
  app_->flushSave();
  {
    AppController reloaded;
    const int row = reloaded.tasks()->indexOfId(id);
    ASSERT_GE(row, 0);
    const Task& t = reloaded.tasks()->items().at(row);
    EXPECT_EQ(t.dueAt, at) << "the clock time did not survive a reload";
    EXPECT_EQ(t.scheduledAt, at);
    EXPECT_TRUE(t.dueHasTime);
    EXPECT_TRUE(t.scheduledHasTime);
  }

  // Export carries it too.
  const QString exported = app_->exportActiveProfileJson();
  EXPECT_TRUE(exported.contains(QStringLiteral("2026-07-10T15:00:00"))) << exported.left(400).toStdString();
}

// ─── OAuth token refresh ──────────────────────────────────────────────
// ─── Open in tracker (HEAP-117) ───
// The URL is tracker-supplied, and a Jira session that never learned its site
// yields a bare "/browse/KEY". Only a web address may reach the browser.
// openTaskExternal itself is never called from a test — it would open one.
TEST_F(AppControllerTest, ExternalUrlForAcceptsOnlyHttpUrls) {
  Task https;
  https.id = QStringLiteral("gh-1");
  https.externalUrl = QStringLiteral("https://github.com/acme/web/issues/1");

  Task relative;  // Jira with no site configured
  relative.id = QStringLiteral("jira-1");
  relative.externalUrl = QStringLiteral("/browse/PROJ-1");

  Task scripted;
  scripted.id = QStringLiteral("evil-1");
  scripted.externalUrl = QStringLiteral("javascript:alert(1)");

  Task file;
  file.id = QStringLiteral("evil-2");
  file.externalUrl = QStringLiteral("file:///c:/windows/system32/calc.exe");

  Task local;
  local.id = QStringLiteral("LTE-1");

  app_->tasks()->reset({https, relative, scripted, file, local});
  EXPECT_EQ(app_->externalUrlFor(QStringLiteral("gh-1")).toString(), https.externalUrl);
  EXPECT_TRUE(app_->externalUrlFor(QStringLiteral("jira-1")).isEmpty());
  EXPECT_TRUE(app_->externalUrlFor(QStringLiteral("evil-1")).isEmpty());
  EXPECT_TRUE(app_->externalUrlFor(QStringLiteral("evil-2")).isEmpty());
  EXPECT_TRUE(app_->externalUrlFor(QStringLiteral("LTE-1")).isEmpty());
  EXPECT_TRUE(app_->externalUrlFor(QStringLiteral("no-such-task")).isEmpty());
}

TEST_F(AppControllerTest, TheOpenTicketShortcutIsInTheCatalogAndRebindable) {
  bool found = false;
  for(const QVariant& v : app_->shortcuts()) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("id")).toString() != QStringLiteral("task.openExternal")) {
      continue;
    }
    found = true;
    EXPECT_EQ(m.value(QStringLiteral("defaultSequence")).toString(), QStringLiteral("G, X"));
    EXPECT_FALSE(m.value(QStringLiteral("label")).toString().isEmpty()) << "the shortcut has no translated label";
    EXPECT_FALSE(m.value(QStringLiteral("description")).toString().isEmpty());
  }
  EXPECT_TRUE(found) << "task.openExternal is missing from the shortcut catalog";

  EXPECT_TRUE(app_->setShortcut(QStringLiteral("task.openExternal"), QStringLiteral("Ctrl+Shift+O")));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.openExternal")), QStringLiteral("Ctrl+Shift+O"));
  app_->resetShortcut(QStringLiteral("task.openExternal"));
  EXPECT_EQ(app_->shortcutFor(QStringLiteral("task.openExternal")), QStringLiteral("G, X"));
}

TEST_F(AppControllerTest, ProviderBadgesCoverEveryProviderInTheCatalog) {
  const QVariantMap badges = app_->providerBadges();
  for(const QVariant& v : app_->integrationCatalog()) {
    const QString id = v.toMap().value(QStringLiteral("id")).toString();
    ASSERT_TRUE(badges.contains(id)) << "no badge for " << id.toStdString();
    const QVariantMap badge = badges.value(id).toMap();
    EXPECT_FALSE(badge.value(QStringLiteral("name")).toString().isEmpty());
    EXPECT_FALSE(badge.value(QStringLiteral("icon")).toString().isEmpty());
    EXPECT_FALSE(badge.value(QStringLiteral("color")).toString().isEmpty());
  }
}

TEST_F(AppControllerTest, ThePaletteFindsAMirroredIssueByItsTrackerKey) {
  auto issue = ghIssue(QStringLiteral("1234"), QStringLiteral("https://github.com/acme/web/issues/1234"));
  app_->tasks()->reset({});
  app_->mergeExternalTasks(QStringLiteral("github"), QStringLiteral("github-"), {issue});
  // The palette searches the persisted profiles, not the live model.
  app_->flushSave();

  bool found = false;
  for(const QVariant& v : app_->commandPaletteEntries()) {
    const QVariantMap m = v.toMap();
    if(m.value(QStringLiteral("taskId")).toString() != QStringLiteral("github-1234")) {
      continue;
    }
    found = true;
    const QString haystack = m.value(QStringLiteral("label")).toString() + m.value(QStringLiteral("sub")).toString();
    EXPECT_TRUE(haystack.contains(QStringLiteral("#1234"))) << haystack.toStdString();
    EXPECT_TRUE(haystack.contains(QStringLiteral("GitHub"))) << haystack.toStdString();
  }
  EXPECT_TRUE(found) << "the pulled issue never reached the palette";
}

// Moving a card writes the new state back to the tracker. An issue pulled from
// an "assigned to me" endpoint belongs to some other repo: the push must go to
// that repo, never to the configured one — there the PATCH would close a
// different issue that happens to share the number. Driven against a local fake
// Gitea, because its base URL is configurable; github's is hard-coded and a
// mistake here would reach the real API.
TEST_F(AppControllerTest, MovingACrossProjectTicketPushesToItsOwnRepo) {
  heap::testing::FakeHttpServer gitea;
  gitea.route("GET /api/v1/repos/acme/web/issues", {200, "[]", {}});
  gitea.route("PATCH /api/v1/repos/acme/web/issues/5", {200, "{}", {}});
  gitea.route("PATCH /api/v1/repos/acme/api/issues/5", {200, "{}", {}});
  gitea.route("GET /api/v1/repos/acme/api/issues/5",
              {200, R"({"number":5,"title":"t","state":"open","repository":{"full_name":"acme/api"}})", {}});

  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QStringLiteral("tok"));
  writeIntegrationConfig(QStringLiteral("gitea"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("host"), gitea.base()},
                             {QStringLiteral("repo"), QStringLiteral("acme/web")},
                             {QStringLiteral("writeStatus"), true},
                             {QStringLiteral("askBeforeWrite"), false},
                         });

  // A ticket that came from another repo entirely.
  Task foreign;
  foreign.id = QStringLiteral("gitea-api-5");
  foreign.externalId = QStringLiteral("5");
  foreign.externalProvider = QStringLiteral("gitea");
  foreign.externalUrl = QStringLiteral("https://gitea.example.com/acme/api/issues/5");
  foreign.status = QStringLiteral("todo");
  foreign.externalMeta.project = QStringLiteral("acme/api");
  foreign.externalMeta.crossProject = true;
  app_->tasks()->reset({foreign});

  QStringList asked;
  QObject::connect(app_.get(),
                   &::AppController::trackerPushNeedsConfirm,
                   app_.get(),
                   [&asked](const QString& id, const QString&, const QString&, const QString&, const QString&, const QString&) {
                     asked.append(id);
                   });
  app_->moveTask(QStringLiteral("gitea-api-5"), QStringLiteral("done"));
  // The repo filter no longer covers it (APP-204): held until the user says.
  ASSERT_TRUE(heap::testing::waitUntil([&asked]() {
    return !asked.isEmpty();
  }));
  EXPECT_FALSE(gitea.seen().contains("PATCH /api/v1/repos/acme/api/issues/5"));
  app_->confirmTrackerPush(QStringLiteral("gitea-api-5"));
  ASSERT_TRUE(heap::testing::waitUntil([&gitea]() {
    return gitea.seen().contains("PATCH /api/v1/repos/acme/api/issues/5");
  })) << "the issue's own repo was never told";
  EXPECT_FALSE(gitea.seen().contains("PATCH /api/v1/repos/acme/web/issues/5")) << "closed an issue in the wrong repo";
  // The tracker agreed, so the card is not flagged as out of step.
  EXPECT_TRUE(heap::testing::waitUntil([this]() {
    return app_->tasks()->items().at(0).externalMeta.unsyncedStatus.isEmpty();
  }));

  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QString());
}

// With no repo configured at all — the default "my issues" connection — a
// moved card is still written back, to the repo it came from.
TEST_F(AppControllerTest, MyIssuesModeWritesBackThroughTheIssuesOwnRepo) {
  heap::testing::FakeHttpServer gitea;
  gitea.route("PATCH /api/v1/repos/acme/api/issues/7", {200, "{}", {}});
  gitea.route("GET /api/v1/user", {200, R"({"login":"me"})", {}});
  gitea.route("GET /api/v1/repos/acme/api/issues/7",
              {200, R"({"number":7,"title":"t","state":"open","assignees":[{"login":"me"}],"repository":{"full_name":"acme/api"}})", {}});

  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QStringLiteral("tok"));
  writeIntegrationConfig(QStringLiteral("gitea"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("host"), gitea.base()},
                             {QStringLiteral("writeStatus"), true},
                             {QStringLiteral("askBeforeWrite"), false},
                         });

  Task mine;
  mine.id = QStringLiteral("gitea-api-7");
  mine.externalId = QStringLiteral("7");
  mine.externalProvider = QStringLiteral("gitea");
  mine.status = QStringLiteral("todo");
  mine.externalMeta.project = QStringLiteral("acme/api");
  mine.externalMeta.crossProject = true;
  // Same number, other repo, never moved: must not be confused with the first.
  Task twin = mine;
  twin.id = QStringLiteral("gitea-web-7");
  twin.externalMeta.project = QStringLiteral("acme/web");
  app_->tasks()->reset({mine, twin});

  app_->moveTask(QStringLiteral("gitea-api-7"), QStringLiteral("done"));
  ASSERT_TRUE(heap::testing::waitUntil([&gitea]() {
    return gitea.seen().contains("PATCH /api/v1/repos/acme/api/issues/7");
  }));
  const QByteArray body = gitea.lastRequest("PATCH /api/v1/repos/acme/api/issues/7").body;
  EXPECT_TRUE(body.contains("closed")) << body.toStdString();
  EXPECT_FALSE(gitea.seen().contains("PATCH /api/v1/repos/acme/web/issues/7"));

  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QString());
}

// A refused write marks the card that was moved, not its same-numbered twin.
TEST_F(AppControllerTest, AFailedCrossProjectPushFlagsOnlyTheMovedCard) {
  heap::testing::FakeHttpServer gitea;
  gitea.route("PATCH /api/v1/repos/acme/api/issues/7", {403, R"({"message":"forbidden"})", {}});
  gitea.route("GET /api/v1/user", {200, R"({"login":"me"})", {}});
  gitea.route("GET /api/v1/repos/acme/api/issues/7",
              {200, R"({"number":7,"title":"t","state":"open","assignees":[{"login":"me"}],"repository":{"full_name":"acme/api"}})", {}});

  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QStringLiteral("tok"));
  writeIntegrationConfig(QStringLiteral("gitea"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("host"), gitea.base()},
                             {QStringLiteral("writeStatus"), true},
                             {QStringLiteral("askBeforeWrite"), false},
                         });

  Task mine;
  mine.id = QStringLiteral("gitea-api-7");
  mine.externalId = QStringLiteral("7");
  mine.externalProvider = QStringLiteral("gitea");
  mine.status = QStringLiteral("todo");
  mine.externalMeta.project = QStringLiteral("acme/api");
  mine.externalMeta.crossProject = true;
  Task twin = mine;
  twin.id = QStringLiteral("gitea-web-7");
  twin.externalMeta.project = QStringLiteral("acme/web");
  app_->tasks()->reset({twin, mine});

  app_->moveTask(QStringLiteral("gitea-api-7"), QStringLiteral("done"));
  ASSERT_TRUE(heap::testing::waitUntil([this]() {
    const int row = app_->tasks()->indexOfId(QStringLiteral("gitea-api-7"));
    return !app_->tasks()->items().at(row).externalMeta.unsyncedStatus.isEmpty();
  }));
  const int twinRow = app_->tasks()->indexOfId(QStringLiteral("gitea-web-7"));
  EXPECT_TRUE(app_->tasks()->items().at(twinRow).externalMeta.unsyncedStatus.isEmpty());

  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QString());
}

// "Synced 12 issue(s)" every quarter of an hour said nothing about whether
// anything changed, and the merge rewrote state.json either way.
TEST_F(AppControllerTest, AQuietResyncReportsNoChangeAndWritesNothing) {
  const QByteArray issues = R"([{"number":5,"title":"t","state":"open","html_url":"https://gitea.example.com/acme/web/issues/5"}])";
  heap::testing::FakeHttpServer gitea;
  gitea.route("GET /api/v1/repos/acme/web/issues", {200, issues, {}});

  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QStringLiteral("tok"));
  writeIntegrationConfig(QStringLiteral("gitea"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("host"), gitea.base()},
                             {QStringLiteral("repo"), QStringLiteral("acme/web")},
                         });
  app_->tasks()->reset({});

  // Collect only what reports the outcome. A pull that brought cards says
  // so through syncNews, which names them (APP-180).
  QStringList outcomes;
  QObject::connect(app_.get(), &AppController::toast, app_.get(), [&outcomes](const QString& text) {
    if(text.contains(QStringLiteral("new")) || text.contains(QStringLiteral("up to date"))) {
      outcomes << text;
    }
  });
  QObject::connect(app_.get(), &AppController::syncNews, app_.get(), [&outcomes](const QString& text) {
    outcomes << text;
  });

  app_->syncProvider(QStringLiteral("gitea"));
  ASSERT_TRUE(heap::testing::waitUntil([&outcomes]() {
    return !outcomes.isEmpty();
  }));
  EXPECT_TRUE(outcomes.last().contains(QStringLiteral("1 new"))) << outcomes.last().toStdString();
  EXPECT_EQ(app_->tasks()->rowCount(), 1);

  outcomes.clear();
  app_->syncProvider(QStringLiteral("gitea"));
  ASSERT_TRUE(heap::testing::waitUntil([&outcomes]() {
    return !outcomes.isEmpty();
  }));
  EXPECT_TRUE(outcomes.last().contains(QStringLiteral("up to date"))) << outcomes.last().toStdString();
  EXPECT_EQ(app_->tasks()->rowCount(), 1);

  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QString());
}

// Comments are only ever read, and kept in a local cache. Against a fake Gitea,
// because its base URL is configurable — github's is hard-coded.
TEST_F(AppControllerTest, TicketCommentsAreFetchedForTheIssuesOwnRepoAndCachedReadOnly) {
  heap::testing::FakeHttpServer gitea;
  gitea.route("GET /api/v1/repos/acme/api/issues/5/comments",
              {200, R"([{"user":{"login":"ada"},"body":"first","created_at":"2026-01-02T03:04:05Z"}])", {}});

  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QStringLiteral("tok"));
  writeIntegrationConfig(QStringLiteral("gitea"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("host"), gitea.base()},
                             // The configured repo is NOT the issue's own.
                             {QStringLiteral("repo"), QStringLiteral("acme/web")},
                         });

  Task t;
  t.id = QStringLiteral("gitea-api-5");
  t.externalId = QStringLiteral("5");
  t.externalProvider = QStringLiteral("gitea");
  t.externalUrl = QStringLiteral("https://gitea.example.com/acme/api/issues/5");
  t.externalMeta.project = QStringLiteral("acme/api");
  t.externalMeta.crossProject = true;
  app_->tasks()->reset({t});
  const Task before = app_->tasks()->items().at(0);

  QVariantList got;
  QString error;
  bool done = false;
  QObject::connect(app_.get(),
                   &AppController::ticketCommentsLoaded,
                   app_.get(),
                   [&](const QString& taskId, const QVariantList& comments, const QString& err) {
                     EXPECT_EQ(taskId, QStringLiteral("gitea-api-5"));
                     got = comments;
                     error = err;
                     done = true;
                   });

  app_->fetchTicketComments(QStringLiteral("gitea-api-5"));
  ASSERT_TRUE(heap::testing::waitUntil([&done]() {
    return done;
  }));
  EXPECT_TRUE(error.isEmpty()) << error.toStdString();
  ASSERT_EQ(got.size(), 1);
  EXPECT_EQ(got.at(0).toMap().value(QStringLiteral("author")).toString(), QStringLiteral("ada"));
  EXPECT_EQ(got.at(0).toMap().value(QStringLiteral("body")).toString(), QStringLiteral("first"));
  // The request went to the issue's repo, not the configured one.
  EXPECT_TRUE(gitea.seen().contains("GET /api/v1/repos/acme/api/issues/5/comments")) << "asked the wrong repo for comments";
  // Read-only towards the tracker: only GETs went out.
  for(const QByteArray& r : gitea.seen()) {
    EXPECT_TRUE(r.startsWith("GET ")) << r.toStdString();
  }
  // Kept for reading offline (dogfood 2026-10-19), and counted on the card;
  // nothing else on the task changed.
  const QVariantMap cached = app_->ticketComments(QStringLiteral("gitea-api-5"));
  ASSERT_EQ(cached.value(QStringLiteral("comments")).toList().size(), 1);
  EXPECT_EQ(cached.value(QStringLiteral("comments")).toList().at(0).toMap().value(QStringLiteral("body")).toString(),
            QStringLiteral("first"));
  EXPECT_FALSE(app_->ticketCommentsStale(QStringLiteral("gitea-api-5")));
  Task expected = before;
  expected.externalMeta.commentCount = 1;
  EXPECT_EQ(app_->tasks()->items().at(0), expected);

  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitea"), QStringLiteral("token"), QString());
}

TEST_F(AppControllerTest, FetchingCommentsForALocalTaskAnswersWithoutARequest) {
  Task local;
  local.id = QStringLiteral("LTE-1");
  app_->tasks()->reset({local});

  QString error;
  bool done = false;
  QObject::connect(
      app_.get(), &AppController::ticketCommentsLoaded, app_.get(), [&](const QString&, const QVariantList& c, const QString& err) {
        EXPECT_TRUE(c.isEmpty());
        error = err;
        done = true;
      });
  app_->fetchTicketComments(QStringLiteral("LTE-1"));
  ASSERT_TRUE(done) << "a local task should be answered synchronously";
  EXPECT_FALSE(error.isEmpty());

  done = false;
  app_->fetchTicketComments(QStringLiteral("no-such-task"));
  EXPECT_TRUE(done);
}

// The refresh token lives in the secret store but is not one of the card's
// secretKeys, so integrationConfig() never handed it to the refresh path: a
// browser-connected tracker went quiet at its first token expiry. Driven end to
// end against a local fake GitLab — the sync must spend the refresh token first
// and only then list issues, with the new access token.
TEST_F(AppControllerTest, ExpiredOAuthTokenIsRefreshedBeforeTheSync) {
  heap::testing::FakeHttpServer gitlab;
  gitlab.route("POST /oauth/token", {200, R"({"access_token":"at-new","refresh_token":"rt-new","expires_in":7200})", {}});
  gitlab.route("GET /api/v4/issues", {200, "[]", {}});

  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QStringLiteral("at-old"));
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken"), QStringLiteral("rt-old"));

  writeIntegrationConfig(QStringLiteral("gitlab"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                             {QStringLiteral("host"), gitlab.base()},
                             {QStringLiteral("clientId"), QStringLiteral("cid")},
                             {QStringLiteral("tokenExpiresAt"), QDateTime::currentDateTime().addSecs(-3600).toString(Qt::ISODate)},
                         });

  app_->syncProvider(QStringLiteral("gitlab"));
  ASSERT_TRUE(heap::testing::waitUntil([&gitlab]() {
    return gitlab.seen().contains("GET /api/v4/issues");
  })) << "the sync never reached the issue list";

  ASSERT_EQ(gitlab.seen().value(0), QByteArray("POST /oauth/token")) << "the expired token was used without a refresh";
  const QByteArray form = gitlab.lastRequest("POST /oauth/token").body;
  EXPECT_TRUE(form.contains("grant_type=refresh_token")) << form.toStdString();
  EXPECT_TRUE(form.contains("refresh_token=rt-old")) << form.toStdString();
  EXPECT_EQ(gitlab.lastRequest("GET /api/v4/issues").headers.value("authorization"), QByteArray("Bearer at-new"));

  // Rotated: both halves of the new grant are what the next run will load.
  EXPECT_EQ(app_->integrationSecret(QStringLiteral("gitlab"), QStringLiteral("token")), QStringLiteral("at-new"));
  EXPECT_EQ(app_->integrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken")), QStringLiteral("rt-new"));

  // Leave nothing behind for the suites sharing this test-mode profile.
  writeIntegrationConfig(QStringLiteral("gitlab"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QString());
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken"), QString());
}

// The mirror image, and the one that fails silently. Atlassian now forces
// rotation on every new 3LO app, but Bitbucket and ClickUp answer a refresh
// with an access token and no refresh_token at all. SecretStore::setValue
// deletes a key when handed an empty string, so writing the parsed value
// unconditionally would wipe the credential that keeps the grant alive — the
// card would work until the next expiry and then be unrecoverable without a
// fresh browser sign-in.
TEST_F(AppControllerTest, ARefreshThatReturnsNoNewRefreshTokenKeepsTheOldOne) {
  heap::testing::FakeHttpServer gitlab;
  gitlab.route("POST /oauth/token", {200, R"({"access_token":"at-new","expires_in":7200})", {}});
  gitlab.route("GET /api/v4/issues", {200, "[]", {}});

  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QStringLiteral("at-old"));
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken"), QStringLiteral("rt-keep"));

  writeIntegrationConfig(QStringLiteral("gitlab"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                             {QStringLiteral("host"), gitlab.base()},
                             {QStringLiteral("clientId"), QStringLiteral("cid")},
                             {QStringLiteral("tokenExpiresAt"), QDateTime::currentDateTime().addSecs(-3600).toString(Qt::ISODate)},
                         });

  app_->syncProvider(QStringLiteral("gitlab"));
  ASSERT_TRUE(heap::testing::waitUntil([&gitlab]() {
    return gitlab.seen().contains("GET /api/v4/issues");
  })) << "the sync never reached the issue list";

  EXPECT_EQ(gitlab.lastRequest("GET /api/v4/issues").headers.value("authorization"), QByteArray("Bearer at-new"));
  EXPECT_EQ(app_->integrationSecret(QStringLiteral("gitlab"), QStringLiteral("token")), QStringLiteral("at-new"));
  EXPECT_EQ(app_->integrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken")), QStringLiteral("rt-keep"))
      << "the silent half of the grant was cleared, leaving the next expiry nothing to spend";

  writeIntegrationConfig(QStringLiteral("gitlab"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QString());
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken"), QString());
}

// A rotated refresh token is single-use, and Sentry in particular rotates with
// no grace period: spending the same one twice does not merely fail the second
// call, it invalidates the grant and logs the user out for good. Two syncs
// landing together on an expired token must therefore produce exactly one
// token request.
TEST_F(AppControllerTest, TwoSyncsRacingAnExpiredTokenSpendTheRefreshTokenOnce) {
  heap::testing::FakeHttpServer gitlab;
  gitlab.route("POST /oauth/token", {200, R"({"access_token":"at-new","refresh_token":"rt-new","expires_in":7200})", {}});
  gitlab.route("GET /api/v4/issues", {200, "[]", {}});

  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QStringLiteral("at-old"));
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken"), QStringLiteral("rt-old"));
  writeIntegrationConfig(QStringLiteral("gitlab"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                             {QStringLiteral("host"), gitlab.base()},
                             {QStringLiteral("clientId"), QStringLiteral("cid")},
                             {QStringLiteral("tokenExpiresAt"), QDateTime::currentDateTime().addSecs(-3600).toString(Qt::ISODate)},
                         });

  // Back to back, before the first refresh can have answered.
  app_->syncProvider(QStringLiteral("gitlab"));
  app_->syncProvider(QStringLiteral("gitlab"));

  ASSERT_TRUE(heap::testing::waitUntil([&gitlab]() {
    return gitlab.seen().contains("GET /api/v4/issues");
  })) << "the sync never reached the issue list";

  int refreshes = 0;
  for(const QByteArray& seen : gitlab.seen()) {
    refreshes += seen == "POST /oauth/token" ? 1 : 0;
  }
  EXPECT_EQ(refreshes, 1) << "the same refresh token was spent twice, which kills the grant outright";

  writeIntegrationConfig(QStringLiteral("gitlab"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QString());
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken"), QString());
}

// ─── Required fields after a browser sign-in ──────────────────────────
// Signing in proves who you are, not what to sync, so the scope fields stay
// empty and the card says so. But Trello's "API key" is not a scope field at
// all: it is the app's own client ID, which makeBespokeProvider substitutes
// from the baked credential. Listing it as missing sent the user hunting for a
// value they cannot obtain, on a card that already syncs.

TEST_F(AppControllerTest, ABrowserSignInStillAsksForTheFieldsThatSayWhatToSync) {
  app_->setIntegrationSecret(QStringLiteral("sentry"), QStringLiteral("token"), QStringLiteral("at"));
  writeIntegrationConfig(QStringLiteral("sentry"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                         });

  const QStringList missing = app_->missingRequiredFields(QStringLiteral("sentry"));
  EXPECT_TRUE(missing.contains(QStringLiteral("Org slug"))) << missing.join(QStringLiteral(", ")).toStdString();
  EXPECT_TRUE(missing.contains(QStringLiteral("Project slug"))) << missing.join(QStringLiteral(", ")).toStdString();

  writeIntegrationConfig(QStringLiteral("sentry"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("sentry"), QStringLiteral("token"), QString());
}

TEST_F(AppControllerTest, TrelloDoesNotAskForAnApiKeyTheBrowserSignInAlreadySupplied) {
  app_->setIntegrationSecret(QStringLiteral("trello"), QStringLiteral("token"), QStringLiteral("tok"));
  writeIntegrationConfig(QStringLiteral("trello"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                         });

  EXPECT_FALSE(app_->missingRequiredFields(QStringLiteral("trello")).contains(QStringLiteral("API key")))
      << "the app key is the build's own, not something the user can type";

  // Pasting a personal token instead is the other path, and there the key is
  // genuinely the user's to provide.
  writeIntegrationConfig(QStringLiteral("trello"), QJsonObject{{QStringLiteral("connected"), true}});
  EXPECT_TRUE(app_->missingRequiredFields(QStringLiteral("trello")).contains(QStringLiteral("API key")));

  writeIntegrationConfig(QStringLiteral("trello"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("trello"), QStringLiteral("token"), QString());
}

// ─── Manual connect ───────────────────────────────────────────────────
// An OAuth-capable card used to offer no way to connect with typed
// credentials: the Connect button was hidden as soon as the build carried a
// client ID, so Advanced showed a full set of fields and only "Test
// connection". A self-hosted Jira — which the Atlassian gateway has never
// heard of — had no way in at all.

TEST_F(AppControllerTest, ConnectingByHandMarksTheCardConnected) {
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("pat"));
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{{QStringLiteral("baseUrl"), QStringLiteral("https://jira.acme.internal")}});

  app_->connectIntegrationManually(QStringLiteral("jira"));

  EXPECT_TRUE(readIntegrationConfig(QStringLiteral("jira")).value(QStringLiteral("connected")).toBool());

  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
}

TEST_F(AppControllerTest, ConnectingByHandRefusesWhileARequiredFieldIsEmpty) {
  // A Jira with a token and no base URL builds no provider at all, so marking
  // it connected produced a card that claimed to work and synced nothing.
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("pat"));
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});

  QSignalSpy needs(app_.get(), &AppController::integrationNeedsFields);
  app_->connectIntegrationManually(QStringLiteral("jira"));

  EXPECT_FALSE(readIntegrationConfig(QStringLiteral("jira")).value(QStringLiteral("connected")).toBool());
  ASSERT_EQ(needs.count(), 1);
  EXPECT_TRUE(needs.at(0).at(1).toStringList().contains(QStringLiteral("Base URL")))
      << needs.at(0).at(1).toStringList().join(QStringLiteral(", ")).toStdString();

  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
}

TEST_F(AppControllerTest, ConnectingByHandEndsALeftoverBrowserSession) {
  // authMode=oauth routes every Jira call through api.atlassian.com/ex/jira/
  // {cloudId}. Left in place, the base URL typed right below it is never used.
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("pat"));
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("refreshToken"), QStringLiteral("rt"));
  writeIntegrationConfig(QStringLiteral("jira"),
                         QJsonObject{
                             {QStringLiteral("baseUrl"), QStringLiteral("https://jira.acme.internal")},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                             {QStringLiteral("tokenExpiresAt"), QStringLiteral("2026-01-01T00:00:00")},
                         });

  app_->connectIntegrationManually(QStringLiteral("jira"));

  const QJsonObject cfg = readIntegrationConfig(QStringLiteral("jira"));
  EXPECT_TRUE(cfg.value(QStringLiteral("connected")).toBool());
  EXPECT_FALSE(cfg.contains(QStringLiteral("authMode")));
  EXPECT_FALSE(cfg.contains(QStringLiteral("tokenExpiresAt")));
  EXPECT_TRUE(app_->integrationSecret(QStringLiteral("jira"), QStringLiteral("refreshToken")).isEmpty());

  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
}

TEST_F(AppControllerTest, AJiraSignedInWithABrowserIsNotAskedForABaseUrl) {
  // The site comes from accessible-resources, so requiring it of everyone
  // would make a complete browser sign-in look half-finished.
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("at"));
  writeIntegrationConfig(QStringLiteral("jira"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                         });

  EXPECT_TRUE(app_->missingRequiredFields(QStringLiteral("jira")).isEmpty())
      << app_->missingRequiredFields(QStringLiteral("jira")).join(QStringLiteral(", ")).toStdString();

  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
}

// ─── Disconnect / auth mode ───────────────────────────────────────────
// authMode=oauth used to survive a disconnect, so a personal access token
// pasted afterwards was still sent as a Bearer — which GitLab (PRIVATE-TOKEN)
// and ClickUp (raw Authorization) both reject.

TEST_F(AppControllerTest, DisconnectingABrowserSessionDropsItsTokens) {
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QStringLiteral("at"));
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken"), QStringLiteral("rt"));
  writeIntegrationConfig(QStringLiteral("gitlab"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                             {QStringLiteral("tokenExpiresAt"), QStringLiteral("2026-01-01T00:00:00")},
                         });

  app_->disconnectIntegration(QStringLiteral("gitlab"));

  const QJsonObject cfg = readIntegrationConfig(QStringLiteral("gitlab"));
  EXPECT_FALSE(cfg.value(QStringLiteral("connected")).toBool());
  EXPECT_FALSE(cfg.contains(QStringLiteral("authMode"))) << "a stale authMode makes the next PAT go out as a Bearer";
  EXPECT_FALSE(cfg.contains(QStringLiteral("tokenExpiresAt")));
  EXPECT_TRUE(app_->integrationSecret(QStringLiteral("gitlab"), QStringLiteral("token")).isEmpty());
  EXPECT_TRUE(app_->integrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken")).isEmpty());

  writeIntegrationConfig(QStringLiteral("gitlab"), QJsonObject{});
}

TEST_F(AppControllerTest, DisconnectingATokenCardKeepsTheUsersOwnToken) {
  // A PAT is the user's credential, not one this app obtained — reconnecting
  // must not mean pasting it again.
  app_->setIntegrationSecret(QStringLiteral("todoist"), QStringLiteral("token"), QStringLiteral("mine"));
  writeIntegrationConfig(QStringLiteral("todoist"), QJsonObject{{QStringLiteral("connected"), true}});

  app_->disconnectIntegration(QStringLiteral("todoist"));

  EXPECT_FALSE(readIntegrationConfig(QStringLiteral("todoist")).value(QStringLiteral("connected")).toBool());
  EXPECT_EQ(app_->integrationSecret(QStringLiteral("todoist"), QStringLiteral("token")), QStringLiteral("mine"));

  app_->setIntegrationSecret(QStringLiteral("todoist"), QStringLiteral("token"), QString());
  writeIntegrationConfig(QStringLiteral("todoist"), QJsonObject{});
}

TEST_F(AppControllerTest, PastingATokenOverABrowserSessionEndsThatSession) {
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QStringLiteral("at"));
  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken"), QStringLiteral("rt"));
  writeIntegrationConfig(QStringLiteral("gitlab"),
                         QJsonObject{
                             {QStringLiteral("connected"), true},
                             {QStringLiteral("authMode"), QStringLiteral("oauth")},
                             {QStringLiteral("tokenExpiresAt"), QStringLiteral("2026-01-01T00:00:00")},
                         });

  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QStringLiteral("glpat-mine"));

  const QJsonObject cfg = readIntegrationConfig(QStringLiteral("gitlab"));
  EXPECT_FALSE(cfg.contains(QStringLiteral("authMode")));
  EXPECT_TRUE(cfg.value(QStringLiteral("connected")).toBool()) << "the card stays connected, just on the PAT";
  EXPECT_TRUE(app_->integrationSecret(QStringLiteral("gitlab"), QStringLiteral("refreshToken")).isEmpty())
      << "the old grant's refresh token is dead weight";

  app_->setIntegrationSecret(QStringLiteral("gitlab"), QStringLiteral("token"), QString());
  writeIntegrationConfig(QStringLiteral("gitlab"), QJsonObject{});
}

TEST_F(AppControllerTest, CatalogFlagsWhichProvidersCanDoOneClick) {
  // Built without the CI credentials, so every provider that refuses a public
  // client must report itself as not one-click — otherwise the card offers a
  // button whose token exchange is guaranteed to fail.
  const QVariantList catalog = app_->integrationCatalog();
  ASSERT_FALSE(catalog.isEmpty());
  int checked = 0;
  QVariantMap sentry;
  for(const QVariant& entry : catalog) {
    const QVariantMap m = entry.toMap();
    if(m.value(QStringLiteral("id")).toString() == QStringLiteral("sentry")) {
      sentry = m;
    }
    if(!m.value(QStringLiteral("oauthNeedsSecret")).toBool()) {
      continue;
    }
    ++checked;
    EXPECT_FALSE(m.value(QStringLiteral("oauthReady")).toBool()) << m.value(QStringLiteral("id")).toString().toStdString();
    EXPECT_TRUE(m.value(QStringLiteral("oauth")).toBool()) << m.value(QStringLiteral("id")).toString().toStdString();
  }
  EXPECT_GE(checked, 4) << "todoist, asana, clickup and bitbucket all need a secret";

  // Sentry is the counterexample, and the whole point of registering it as a
  // public client: no secret exists to ship, PKCE stands in for one, and the
  // client ID is committed — so even this build, which has no CI credentials
  // at all, must still offer one-click sign-in. This fails if Sentry is moved
  // back to a confidential app, and if the committed client ID is dropped.
  ASSERT_FALSE(sentry.isEmpty());
  EXPECT_FALSE(sentry.value(QStringLiteral("oauthNeedsSecret")).toBool());
  EXPECT_TRUE(sentry.value(QStringLiteral("oauthReady")).toBool())
      << "a public client needs no secret, so a plain build must still offer the button";
}

// ─── Status mapping ──────────────────────────────────────────────────
//
// StatusMap has always taken per-user overrides and had never been given any:
// every sync called it with an empty map, so a status its built-in table does
// not recognise — "QA", "Needs triage", anything from a custom Jira workflow —
// landed in "todo" with no way to say otherwise.

TEST_F(AppControllerTest, AnUnmappedStatusIsRememberedSoItCanBeMapped) {
  const QString repo = QStringLiteral("acme/widgets");
  writeIntegrationConfig(QStringLiteral("gitea"),
                         QJsonObject{{"connected", true}, {"host", QStringLiteral("http://127.0.0.1:1")}, {"repo", repo}});

  heap::integrations::ExternalTask ext;
  ext.providerId = QStringLiteral("gitea");
  ext.externalId = QStringLiteral("7");
  ext.title = QStringLiteral("Flaky retry test");
  ext.status = QStringLiteral("Needs triage");
  app_->mergeExternalTasks(QStringLiteral("gitea"), QStringLiteral("gitea-"), {ext});

  // Nothing in the built-in table matches, so the card lands in the fallback.
  const int row = app_->tasks()->indexOfId(QStringLiteral("gitea-7"));
  ASSERT_GE(row, 0);
  EXPECT_EQ(app_->tasks()->items().at(row).status, QStringLiteral("todo"));

  // But the status itself is now on offer, which is the whole point: the user
  // cannot map a vocabulary they have to guess at.
  const QVariantList rows = app_->statusMappingFor(QStringLiteral("gitea"));
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("Needs triage"));
  EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("column")).toString(), QStringLiteral("todo"));
  EXPECT_FALSE(rows.at(0).toMap().value(QStringLiteral("overridden")).toBool()) << "an unmapped row is a guess, not a decision";
}

TEST_F(AppControllerTest, AMappedStatusBeatsTheBuiltInTable) {
  writeIntegrationConfig(
      QStringLiteral("gitea"),
      QJsonObject{{"connected", true}, {"host", QStringLiteral("http://127.0.0.1:1")}, {"repo", QStringLiteral("acme/widgets")}});
  app_->setStatusMapping(QStringLiteral("gitea"), QStringLiteral("Needs triage"), QStringLiteral("backlog"));
  // "open" IS in the built-in table; an override still wins.
  app_->setStatusMapping(QStringLiteral("gitea"), QStringLiteral("open"), QStringLiteral("review"));

  heap::integrations::ExternalTask a;
  a.providerId = QStringLiteral("gitea");
  a.externalId = QStringLiteral("1");
  a.title = QStringLiteral("a");
  a.status = QStringLiteral("Needs triage");
  heap::integrations::ExternalTask b;
  b.providerId = QStringLiteral("gitea");
  b.externalId = QStringLiteral("2");
  b.title = QStringLiteral("b");
  b.status = QStringLiteral("open");
  app_->mergeExternalTasks(QStringLiteral("gitea"), QStringLiteral("gitea-"), {a, b});

  EXPECT_EQ(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("gitea-1"))).status, QStringLiteral("backlog"));
  EXPECT_EQ(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("gitea-2"))).status, QStringLiteral("review"));
}

TEST_F(AppControllerTest, ClearingAMappingGoesBackToTheGuess) {
  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{{"connected", true}});
  // Seen during a sync, so the row survives the mapping being cleared — a
  // status only ever mapped by hand has nothing left to list once the mapping
  // is gone, which is right but not what this is about.
  heap::integrations::ExternalTask e;
  e.providerId = QStringLiteral("gitea");
  e.externalId = QStringLiteral("4");
  e.title = QStringLiteral("t");
  e.status = QStringLiteral("Needs triage");
  app_->mergeExternalTasks(QStringLiteral("gitea"), QStringLiteral("gitea-"), {e});

  app_->setStatusMapping(QStringLiteral("gitea"), QStringLiteral("Needs triage"), QStringLiteral("review"));
  QVariantList rows = app_->statusMappingFor(QStringLiteral("gitea"));
  ASSERT_EQ(rows.size(), 1);
  EXPECT_TRUE(rows.at(0).toMap().value(QStringLiteral("overridden")).toBool());
  EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("column")).toString(), QStringLiteral("review"));

  // An empty column is how the UI says "Auto" — it has to remove the key, not
  // store an empty one, or the row would map to nothing at all.
  app_->setStatusMapping(QStringLiteral("gitea"), QStringLiteral("Needs triage"), QString());
  rows = app_->statusMappingFor(QStringLiteral("gitea"));
  ASSERT_EQ(rows.size(), 1);
  EXPECT_FALSE(rows.at(0).toMap().value(QStringLiteral("overridden")).toBool());
  EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("column")).toString(), QStringLiteral("todo"));
}

TEST_F(AppControllerTest, AStatusMappedByHandIsOfferedBeforeItIsEverSeen) {
  // The mapping outlives the status disappearing upstream, and a row the user
  // has decided on must not vanish from the list that shows the decision.
  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{{"connected", true}});
  app_->setStatusMapping(QStringLiteral("gitea"), QStringLiteral("Awaiting deploy"), QStringLiteral("blocked"));

  const QVariantList rows = app_->statusMappingFor(QStringLiteral("gitea"));
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("Awaiting deploy"));
  EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("column")).toString(), QStringLiteral("blocked"));
}

TEST_F(AppControllerTest, AColumnNoBoardHasIsRefused) {
  // Storing it would hide every ticket carrying that status, with nothing on
  // screen to say where they went.
  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{{"connected", true}});
  app_->setStatusMapping(QStringLiteral("gitea"), QStringLiteral("QA"), QStringLiteral("not-a-column"));
  EXPECT_TRUE(app_->statusMappingFor(QStringLiteral("gitea")).isEmpty());
}

TEST_F(AppControllerTest, SeenStatusesAccumulateWithoutDuplicating) {
  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{{"connected", true}});

  const auto push = [&](const QString& id, const QString& status) {
    heap::integrations::ExternalTask e;
    e.providerId = QStringLiteral("gitea");
    e.externalId = id;
    e.title = QStringLiteral("t") + id;
    e.status = status;
    app_->mergeExternalTasks(QStringLiteral("gitea"), QStringLiteral("gitea-"), {e});
  };
  push(QStringLiteral("1"), QStringLiteral("QA"));
  push(QStringLiteral("2"), QStringLiteral("QA"));
  push(QStringLiteral("3"), QStringLiteral("Needs triage"));

  const QVariantList rows = app_->statusMappingFor(QStringLiteral("gitea"));
  ASSERT_EQ(rows.size(), 2);
  // Sorted, so the list does not reshuffle under the cursor between syncs.
  EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("Needs triage"));
  EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("QA"));
}

// Dogfood 2026-10-20: Settings listed the whole Jira instance's statuses
// (60 of other teams', "not recognised → To Do") and not the 20 of the
// user's own project. Once a project's statuses are known they are the list,
// the instance's leftovers in the seen list go, and a kind is a recognition.
TEST_F(AppControllerTest, StatusMappingListsTheSyncedProjectsStatusesOnly) {
  QJsonArray junk;
  for(int i = 0; i < 60; ++i) {
    junk.append(QStringLiteral("Budget Reserved %1").arg(i));
  }
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{{"connected", true}, {"seenStatuses", junk}});
  // The user's own choice survives.
  app_->setStatusMapping(QStringLiteral("jira"), QStringLiteral("Obsolete"), QStringLiteral("backlog"));

  heap::integrations::ExternalTask e;
  e.providerId = QStringLiteral("jira");
  e.externalId = QStringLiteral("HT-1");
  e.title = QStringLiteral("t");
  e.status = QStringLiteral("In Progress");
  e.statusCategory = QStringLiteral("indeterminate");
  e.project = QStringLiteral("HT");
  app_->mergeExternalTasks(QStringLiteral("jira"), QStringLiteral("jira-"), {e});

  struct Own {
    const char* name;
    const char* category;
    const char* column;
  };

  const Own own[] = {
      {"Backlog", "new", "backlog"},
      {"Selected for Development", "new", "todo"},
      {"In Progress", "indeterminate", "prog"},
      {"Done", "done", "done"},
      {"Obsolete", "done", "backlog"},
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
  QVector<heap::integrations::ExternalStatus> statuses;
  for(const Own& o : own) {
    statuses.append({QStringLiteral("HT"), QString::fromLatin1(o.name), QString::fromLatin1(o.category)});
  }
  // One more status only a project of its own has, under a name nobody knows.
  statuses.append({QStringLiteral("OPS"), QStringLiteral("Awaiting CAB"), QStringLiteral("indeterminate")});
  app_->rememberProjectStatuses(QStringLiteral("jira"), statuses);

  const QVariantList rows = app_->statusMappingFor(QStringLiteral("jira"));
  ASSERT_EQ(rows.size(), 21);
  QHash<QString, QVariantMap> byName;
  for(const QVariant& v : rows) {
    byName.insert(v.toMap().value(QStringLiteral("status")).toString(), v.toMap());
  }
  for(const Own& o : own) {
    const QVariantMap row = byName.value(QString::fromLatin1(o.name));
    ASSERT_FALSE(row.isEmpty()) << o.name;
    EXPECT_EQ(row.value(QStringLiteral("column")).toString(), QString::fromLatin1(o.column)) << o.name;
    EXPECT_TRUE(row.value(QStringLiteral("known")).toBool()) << o.name << " reads as not recognised";
    EXPECT_EQ(row.value(QStringLiteral("group")).toString(), QStringLiteral("HT")) << o.name;
  }
  EXPECT_TRUE(byName.value(QStringLiteral("Obsolete")).value(QStringLiteral("overridden")).toBool());
  // Recognised by its kind, so not "not recognised".
  EXPECT_EQ(byName.value(QStringLiteral("Awaiting CAB")).value(QStringLiteral("column")).toString(), QStringLiteral("prog"));
  EXPECT_TRUE(byName.value(QStringLiteral("Awaiting CAB")).value(QStringLiteral("known")).toBool());
  EXPECT_EQ(byName.value(QStringLiteral("Awaiting CAB")).value(QStringLiteral("group")).toString(), QStringLiteral("OPS"));
  // The instance's statuses are gone from the stored list as well.
  const QJsonArray seen = readIntegrationConfig(QStringLiteral("jira")).value(QStringLiteral("seenStatuses")).toArray();
  EXPECT_EQ(seen, QJsonArray{QStringLiteral("In Progress")});
}

// A testing status nobody mapped goes to a board's own testing column.
TEST_F(AppControllerTest, ATestingStatusFindsTheBoardsTestingColumn) {
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{{"connected", true}});
  app_->rememberProjectStatuses(QStringLiteral("jira"),
                                {{QStringLiteral("HT"), QStringLiteral("In Testing"), QStringLiteral("indeterminate")}});
  const auto columnOf = [this]() {
    return app_->statusMappingFor(QStringLiteral("jira")).at(0).toMap().value(QStringLiteral("column")).toString();
  };
  EXPECT_EQ(columnOf(), QStringLiteral("review"));
  app_->addStatus(QStringLiteral("Testing"));
  QString testingId;
  for(const QVariant& v : app_->statuses()) {
    if(v.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Testing")) {
      testingId = v.toMap().value(QStringLiteral("id")).toString();
    }
  }
  ASSERT_FALSE(testingId.isEmpty());
  EXPECT_EQ(columnOf(), testingId);
}

// Item 16 (dogfood 2026-10-20): a screenshot in a Jira Server ticket's text
// was a grey "missing" box. It is fetched with the tracker's credentials into
// the attachments store and the text points there; one that cannot be
// fetched says why.
TEST_F(AppControllerTest, ATicketsScreenshotIsFetchedAndShown) {
  const QByteArray png =
      QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
  // The test profile outlives a run: a picture stored by the last one would
  // be shown without being fetched.
  QFile::remove(app_->dataDir() + QStringLiteral("/tracker-images.json"));
  heap::testing::FakeHttpServer jira;
  jira.route("GET /secure/attachment/10001/screenshot-1.png", {200, png, {}});
  jira.route("GET /secure/attachment/10002/secret.png", {403, R"({"errorMessages":["No permission"]})", {}});

  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("pat"));
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{{"connected", true}, {"baseUrl", jira.base()}});

  Task t;
  t.id = QStringLiteral("jira-HT-1");
  t.title = QStringLiteral("Broken layout");
  t.externalId = QStringLiteral("HT-1");
  t.externalProvider = QStringLiteral("jira");
  t.externalUrl = jira.base() + QStringLiteral("/browse/HT-1");
  t.status = QStringLiteral("todo");
  t.desc = QStringLiteral("Looks like this:\n\n!screenshot-1.png|thumbnail!\n\nand !secret.png! and !gone.png!");
  t.externalMeta.details.insert(
      QStringLiteral("attachments"),
      QJsonArray{QJsonObject{{"id", "10001"}, {"name", "screenshot-1.png"}, {"mime", "image/png"}, {"size", png.size()}},
                 QJsonObject{{"id", "10002"}, {"name", "secret.png"}, {"mime", "image/png"}, {"size", 10}}});
  app_->tasks()->reset({t});

  const QString first = app_->trackerDescription(t.id);
  EXPECT_TRUE(first.contains(QStringLiteral("(tracker-image:loading/screenshot-1.png)"))) << first.toStdString();
  EXPECT_TRUE(first.contains(QStringLiteral("(tracker-image:failed/gone.png/missing)"))) << first.toStdString();

  ASSERT_TRUE(heap::testing::waitUntil([this, &t]() {
    const QString d = app_->trackerDescription(t.id);
    return !d.contains(QStringLiteral("tracker-image:loading"));
  })) << app_->trackerDescription(t.id).toStdString();
  const QString shown = app_->trackerDescription(t.id);
  static const QRegularExpression stored(QStringLiteral("!\\[screenshot-1.png\\]\\(attachments/([0-9a-f]{32}\\.png)\\)"));
  const QRegularExpressionMatch m = stored.match(shown);
  ASSERT_TRUE(m.hasMatch()) << shown.toStdString();
  QFile file(app_->dataDir() + QStringLiteral("/attachments/") + m.captured(1));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), png);
  EXPECT_TRUE(shown.contains(QStringLiteral("(tracker-image:failed/secret.png/"))) << shown.toStdString();
  // With the tracker's credentials, and only ever read.
  for(const auto& r : jira.requests()) {
    EXPECT_EQ(r.method, QByteArray("GET"));
    if(r.path.startsWith("/secure/attachment/")) {
      EXPECT_EQ(r.headers.value("authorization"), QByteArray("Bearer pat"));
    }
  }
  // Kept by the attachments cleanup while the ticket is here.
  EXPECT_EQ(app_->unusedAttachments().value(QStringLiteral("count")).toInt(), 0);

  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
}

// The rule after 2026-10-20: a picture whose download fails is asked again
// 5, 10, 20… minutes later, never on every render — six hours of the ticket
// open and re-read every minute make a handful of requests, not 360.
TEST_F(AppControllerTest, AFailingScreenshotIsNotAskedForOnEveryRender) {
  QFile::remove(app_->dataDir() + QStringLiteral("/tracker-images.json"));
  heap::testing::FakeHttpServer jira;
  jira.route("GET /secure/attachment/10001/screenshot-1.png", {500, R"({"errorMessages":["boom"]})", {}});
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("pat"));
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{{"connected", true}, {"baseUrl", jira.base()}});

  Task t;
  t.id = QStringLiteral("jira-HT-1");
  t.title = QStringLiteral("Broken layout");
  t.externalId = QStringLiteral("HT-1");
  t.externalProvider = QStringLiteral("jira");
  t.status = QStringLiteral("todo");
  t.externalUrl = jira.base() + QStringLiteral("/browse/HT-1");
  t.desc = QStringLiteral("Looks like this:\n\n!screenshot-1.png|thumbnail!");
  t.externalMeta.details.insert(
      QStringLiteral("attachments"),
      QJsonArray{QJsonObject{{"id", "10001"}, {"name", "screenshot-1.png"}, {"mime", "image/png"}, {"size", 100}}});
  app_->tasks()->reset({t});

  const QDateTime start(QDate(2026, 10, 20), QTime(9, 0));
  QDateTime now = start;
  app_->setFetchClockForTest([&now]() {
    return now;
  });
  const auto asked = [&jira]() {
    return jira.seen().count("GET /secure/attachment/10001/screenshot-1.png");
  };
  QString last;
  for(int minute = 0; minute < 6 * 60; ++minute) {
    now = start.addSecs(qint64(minute) * 60);
    const qsizetype before = asked();
    last = app_->trackerDescription(t.id);
    if(last.contains(QStringLiteral("tracker-image:loading"))) {
      // Asked: wait for the answer before the clock moves on.
      ASSERT_TRUE(heap::testing::waitUntil([&]() {
        return asked() > before && !app_->trackerDescription(t.id).contains(QStringLiteral("tracker-image:loading"));
      })) << "minute "
          << minute;
    }
  }
  // 5, 10, 20, 40, 80, 160 minutes apart: asked at 0, 5, 15, 35, 75, 155, 315.
  EXPECT_GE(asked(), 2);
  EXPECT_LE(asked(), 7);
  // Says why, readably, while it waits.
  EXPECT_TRUE(app_->trackerDescription(t.id).contains(QStringLiteral("(tracker-image:failed/screenshot-1.png/")))
      << app_->trackerDescription(t.id).toStdString();

  app_->setFetchClockForTest({});
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
}

// Dogfood 2026-10-21, items 1 and 3: a screenshot in a Jira comment was a
// bare link and nothing opened whole. A comment comes in pieces with its
// pictures fetched like the description's — the whole file, never the
// thumbnail — and the viewer's picture not stored yet is fetched once.
TEST_F(AppControllerTest, ACommentsScreenshotIsFetchedWholeAndOpensOnce) {
  const QByteArray png =
      QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
  QFile::remove(app_->dataDir() + QStringLiteral("/tracker-images.json"));
  heap::testing::FakeHttpServer jira;
  jira.route("GET /secure/attachment/10001/shot.png", {200, png, {}});
  jira.route("GET /secure/thumbnail/10001/shot.png", {200, QByteArray("thumbnail"), {}});
  jira.route("GET /secure/attachment/10002/other.png", {200, png + QByteArray("2"), {}});
  jira.route("GET /secure/attachment/10003/broken.png", {500, R"({"errorMessages":["boom"]})", {}});
  jira.route("GET /rest/api/2/issue/HT-7/comment",
             {200,
              R"({"total":1,"comments":[{"id":"5","author":{"displayName":"Ann"},
                  "body":"Look here:\n\n!shot.png|thumbnail!\n\nand <b>this</b>","created":"2026-10-20T10:00:00.000+0000"}]})",
              {}});
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("pat"));
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{{"connected", true}, {"baseUrl", jira.base()}});

  Task t;
  t.id = QStringLiteral("jira-HT-7");
  t.title = QStringLiteral("Comment pictures");
  t.externalId = QStringLiteral("HT-7");
  t.externalProvider = QStringLiteral("jira");
  t.externalUrl = jira.base() + QStringLiteral("/browse/HT-7");
  t.status = QStringLiteral("todo");
  t.externalMeta.details.insert(
      QStringLiteral("attachments"),
      QJsonArray{QJsonObject{{"id", "10001"}, {"name", "shot.png"}, {"mime", "image/png"}, {"size", png.size()}},
                 QJsonObject{{"id", "10002"}, {"name", "other.png"}, {"mime", "image/png"}, {"size", png.size() + 1}},
                 QJsonObject{{"id", "10003"}, {"name", "broken.png"}, {"mime", "image/png"}, {"size", 10}},
                 QJsonObject{{"id", "10004"}, {"name", "notes.txt"}, {"mime", "text/plain"}, {"size", 10}}});
  app_->tasks()->reset({t});

  bool loaded = false;
  QObject::connect(
      app_.get(), &AppController::ticketCommentsLoaded, app_.get(), [&loaded](const QString&, const QVariantList&, const QString&) {
        loaded = true;
      });
  app_->fetchTicketComments(t.id);
  ASSERT_TRUE(heap::testing::waitUntil([&loaded]() {
    return loaded;
  }));
  const auto imagePart = [this, &t]() {
    const QVariantList comments = app_->ticketComments(t.id).value(QStringLiteral("comments")).toList();
    if(comments.isEmpty()) {
      return QVariantMap();
    }
    for(const QVariant& p : comments.at(0).toMap().value(QStringLiteral("parts")).toList()) {
      if(p.toMap().value(QStringLiteral("image")).toBool()) {
        return p.toMap();
      }
    }
    return QVariantMap();
  };
  ASSERT_FALSE(imagePart().isEmpty());
  ASSERT_TRUE(heap::testing::waitUntil([&imagePart]() {
    return imagePart().value(QStringLiteral("src")).toString().startsWith(QStringLiteral("attachments/"));
  })) << imagePart().value(QStringLiteral("src")).toString().toStdString();
  const QString stored = imagePart().value(QStringLiteral("src")).toString().mid(12);
  QFile file(app_->dataDir() + QStringLiteral("/attachments/") + stored);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), png) << "the whole file, not the thumbnail";
  // The text around it stays safe text: a tag reads as its characters.
  const QVariantList parts =
      app_->ticketComments(t.id).value(QStringLiteral("comments")).toList().at(0).toMap().value(QStringLiteral("parts")).toList();
  ASSERT_EQ(parts.size(), 3);
  EXPECT_TRUE(parts.at(0).toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("Look here")));
  EXPECT_FALSE(parts.at(2).toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("<b>")));

  // The viewer's list: the ticket's pictures only, the stored one known.
  const QVariantList images = app_->trackerImages(t.id);
  ASSERT_EQ(images.size(), 3);
  EXPECT_EQ(images.at(0).toMap().value(QStringLiteral("attId")).toString(), stored);
  EXPECT_TRUE(images.at(1).toMap().value(QStringLiteral("attId")).toString().isEmpty());

  // A picture no text shows is fetched whole when the viewer opens it, once.
  QVariantMap other = app_->trackerImage(t.id, QStringLiteral("10002"));
  EXPECT_TRUE(other.value(QStringLiteral("loading")).toBool());
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    other = app_->trackerImage(t.id, QStringLiteral("10002"));
    return !other.value(QStringLiteral("attId")).toString().isEmpty();
  }));
  for(int i = 0; i < 10; ++i) {
    app_->trackerImage(t.id, QStringLiteral("10002"));
    app_->ticketComments(t.id);
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(jira.seen().count("GET /secure/attachment/10002/other.png"), 1);
  EXPECT_EQ(jira.seen().count("GET /secure/attachment/10001/shot.png"), 1);
  EXPECT_EQ(jira.seen().count("GET /secure/thumbnail/10001/shot.png"), 0);

  // One that fails says why, and opening it again and again asks no more.
  QVariantMap broken = app_->trackerImage(t.id, QStringLiteral("10003"));
  ASSERT_TRUE(heap::testing::waitUntil([&]() {
    broken = app_->trackerImage(t.id, QStringLiteral("10003"));
    return !broken.value(QStringLiteral("error")).toString().isEmpty();
  }));
  for(int i = 0; i < 20; ++i) {
    broken = app_->trackerImage(t.id, QStringLiteral("10003"));
  }
  QCoreApplication::processEvents();
  EXPECT_FALSE(broken.value(QStringLiteral("error")).toString().isEmpty());
  EXPECT_EQ(jira.seen().count("GET /secure/attachment/10003/broken.png"), 1);
  EXPECT_EQ(app_->trackerImage(t.id, QStringLiteral("nope")).value(QStringLiteral("error")).toString(), QStringLiteral("missing"));
  for(const auto& r : jira.requests()) {
    EXPECT_EQ(r.method, QByteArray("GET"));
  }
  // The test profile outlives a run: a file only this test stores would be
  // an orphan the next run's cleanup counts.
  // Stored read-only: through the store, not QFile::remove.
  EXPECT_TRUE(
      heap::attachments::Store(app_->dataDir() + QStringLiteral("/attachments")).remove(other.value(QStringLiteral("attId")).toString()));

  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});
  app_->setIntegrationSecret(QStringLiteral("jira"), QStringLiteral("token"), QString());
}

// Item 19: the tracker's estimate comes with what a day and a week of it are,
// the site's own working time once read, Jira's 8h / 5d before.
TEST_F(AppControllerTest, TrackerEstimateCarriesTheSitesWorkingTime) {
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{{"connected", true}, {"baseUrl", "https://acme.atlassian.net"}});
  Task t;
  t.id = QStringLiteral("jira-HT-2");
  t.title = QStringLiteral("Estimate");
  t.externalId = QStringLiteral("HT-2");
  t.externalProvider = QStringLiteral("jira");
  t.status = QStringLiteral("todo");
  t.externalMeta.details.insert(QStringLiteral("estimate"), QJsonObject{{"original", 36000}, {"spent", 27000}});
  app_->tasks()->reset({t});

  QVariantMap e = app_->trackerEstimate(t.id);
  EXPECT_EQ(e.value("originalMinutes").toInt(), 600);
  EXPECT_EQ(e.value("spentMinutes").toInt(), 450);
  EXPECT_DOUBLE_EQ(e.value("hoursPerDay").toDouble(), 8.0);
  EXPECT_DOUBLE_EQ(e.value("daysPerWeek").toDouble(), 5.0);

  app_->rememberTrackerTimeTracking(QStringLiteral("jira"), QJsonObject{{"enabled", true}, {"hoursPerDay", 7.5}, {"daysPerWeek", 4.0}});
  e = app_->trackerEstimate(t.id);
  EXPECT_DOUBLE_EQ(e.value("hoursPerDay").toDouble(), 7.5);
  EXPECT_DOUBLE_EQ(e.value("daysPerWeek").toDouble(), 4.0);
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});
}

// Item 19: an estimate typed in the card is read in the tracker's own day and
// week, said back in two units, and stays on the task: nothing is queued for
// the tracker, with status writes off (the default) or on.
TEST_F(AppControllerTest, AnEstimateIsReadInTheTrackersDaysAndNeverSent) {
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{{"connected", true}, {"baseUrl", "https://acme.atlassian.net"}});
  EXPECT_FALSE(app_->trackerWriteEnabled(QStringLiteral("jira"))) << "tracker writes are off by default";
  EXPECT_EQ(app_->parseDuration(QStringLiteral("1d 2h"), QStringLiteral("jira")).value("minutes").toInt(), 600);
  EXPECT_FALSE(app_->parseDuration(QStringLiteral("soon"), QStringLiteral("jira")).value("ok").toBool());
  app_->rememberTrackerTimeTracking(QStringLiteral("jira"), QJsonObject{{"enabled", true}, {"hoursPerDay", 7.0}, {"daysPerWeek", 5.0}});
  EXPECT_EQ(app_->parseDuration(QStringLiteral("1d"), QStringLiteral("jira")).value("minutes").toInt(), 420);
  EXPECT_EQ(app_->parseDuration(QStringLiteral("1d"), QString()).value("minutes").toInt(), 480) << "a local task keeps Jira's 8h";
  EXPECT_EQ(app_->timeUnits(QStringLiteral("jira")).value("hoursPerDay").toDouble(), 7.0);

  Task t;
  t.id = QStringLiteral("jira-ES-1");
  t.title = QStringLiteral("Estimate me");
  t.externalId = QStringLiteral("ES-1");
  t.externalProvider = QStringLiteral("jira");
  t.status = QStringLiteral("todo");
  t.externalMeta.details.insert(QStringLiteral("estimate"), QJsonObject{{"original", 36000}, {"spent", 27000}});
  app_->tasks()->reset({t});
  // The card's line reads the tracker's original estimate and logged time.
  const QVariantMap ticket = app_->tasks()->data(app_->tasks()->index(0), TaskModel::TicketRole).toMap();
  EXPECT_EQ(ticket.value("originalMinutes").toInt(), 600);
  EXPECT_EQ(ticket.value("spentMinutes").toInt(), 450);

  for(const bool writes : {false, true}) {
    app_->setTrackerWriteEnabled(QStringLiteral("jira"), writes);
    QVariantMap d = app_->taskById(t.id);
    d.insert(QStringLiteral("_originalId"), t.id);
    d.insert(QStringLiteral("estimateMinutes"), writes ? 90 : 600);
    ASSERT_TRUE(app_->saveTask(d));
    const Task& after = app_->tasks()->items().at(0);
    EXPECT_EQ(after.estimateMinutes, writes ? 90 : 600);
    EXPECT_FALSE(after.externalMeta.pushQueued) << "an estimate is never sent, writes " << writes;
    EXPECT_TRUE(after.externalMeta.unsyncedStatus.isEmpty());
  }
  app_->setTrackerWriteEnabled(QStringLiteral("jira"), false);
  writeIntegrationConfig(QStringLiteral("jira"), QJsonObject{});
}

TEST_F(AppControllerTest, RemappingDoesNotRecolumnTasksAlreadyMirrored) {
  // Moving a mirrored ticket is a real move that heap pushes back upstream, so
  // re-columning existing cards here would push a change the user never made.
  writeIntegrationConfig(QStringLiteral("gitea"), QJsonObject{{"connected", true}});
  heap::integrations::ExternalTask e;
  e.providerId = QStringLiteral("gitea");
  e.externalId = QStringLiteral("5");
  e.title = QStringLiteral("t");
  e.status = QStringLiteral("Needs triage");
  app_->mergeExternalTasks(QStringLiteral("gitea"), QStringLiteral("gitea-"), {e});
  ASSERT_EQ(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("gitea-5"))).status, QStringLiteral("todo"));

  app_->setStatusMapping(QStringLiteral("gitea"), QStringLiteral("Needs triage"), QStringLiteral("review"));
  EXPECT_EQ(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("gitea-5"))).status, QStringLiteral("todo"))
      << "the mapping applies on the next sync, not retroactively";

  app_->mergeExternalTasks(QStringLiteral("gitea"), QStringLiteral("gitea-"), {e});
  EXPECT_EQ(app_->tasks()->items().at(app_->tasks()->indexOfId(QStringLiteral("gitea-5"))).status, QStringLiteral("review"));
}

// ─── Search box → query ───────────────────────────────────────────────
// The board filters in C++ (TaskFilterProxy); the archive, timeline, week and
// month views build JS snapshots and ask compileSearch instead. Both halves
// have to agree on what a clause is, or one search box means two things.

TEST_F(AppControllerTest, CompileSearchSplitsClausesFromSearchWords) {
  Task blocked = mkTask(QStringLiteral("T-1"), QStringLiteral("login flow"));
  blocked.status = QStringLiteral("blocked");
  Task todo = mkTask(QStringLiteral("T-2"), QStringLiteral("login page"));
  Task other = mkTask(QStringLiteral("T-3"), QStringLiteral("export csv"));
  other.status = QStringLiteral("blocked");
  app_->tasks()->reset({blocked, todo, other});

  const QVariantMap r = app_->compileSearch(QStringLiteral("status:blocked login"));
  EXPECT_TRUE(r.value(QStringLiteral("isQuery")).toBool());
  // The clause is consumed, so only the loose word is left to substring-match.
  EXPECT_EQ(r.value(QStringLiteral("freeText")).toString(), QStringLiteral("login"));
  // The ids are the clause half only — "export csv" is blocked too, and the
  // caller is the one that drops it on the free text.
  const QStringList ids = r.value(QStringLiteral("ids")).toStringList();
  EXPECT_EQ(ids, QStringList({QStringLiteral("T-1"), QStringLiteral("T-3")}));
}

TEST_F(AppControllerTest, CompileSearchBuildsNoIdListForPlainText) {
  app_->tasks()->reset({mkTask(QStringLiteral("T-1"), QStringLiteral("login"))});

  const QVariantMap r = app_->compileSearch(QStringLiteral("login"));
  EXPECT_FALSE(r.value(QStringLiteral("isQuery")).toBool());
  EXPECT_EQ(r.value(QStringLiteral("freeText")).toString(), QStringLiteral("login"));
  // With no clauses every task would be in the list, which is both useless and
  // the largest thing this call could hand back.
  EXPECT_TRUE(r.value(QStringLiteral("ids")).toStringList().isEmpty());
}

TEST_F(AppControllerTest, SearchIsQueryOnlyParsesAndAdvertisesItsFields) {
  EXPECT_TRUE(app_->searchIsQuery(QStringLiteral("priority:P0")));
  EXPECT_FALSE(app_->searchIsQuery(QStringLiteral("https://example.test/x"))) << "a colon is not a clause unless the field is one we know";
  EXPECT_FALSE(app_->searchIsQuery(QString()));

  const QStringList fields = app_->searchFields();
  for(const char* f : {"status", "priority", "due", "tag", "mention"}) {
    EXPECT_TRUE(fields.contains(QLatin1String(f))) << f << " must be offered as a hint";
  }
}

// ─── Mattermost contact merge ─────────────────────────────────────────
// Imported people land in the Docs contact list, and the ones actually talked
// to also in the People rail. The rules that matter: never clobber an edit of
// the user's, never resurrect something they deleted, and never churn the docs
// blob when nothing changed.

namespace {

heap::integrations::ExternalContact mkContact(
    const QString& id, const QString& username, const QString& name, const QString& role, const QString& where) {
  heap::integrations::ExternalContact c;
  c.providerId = QStringLiteral("mattermost");
  c.externalId = id;
  c.username = username;
  c.displayName = name;
  c.role = role;
  c.channelLabel = where;
  return c;
}

const QString kDm = QStringLiteral("direct message");

}  // namespace

class ContactMergeTest : public AppControllerTest {
 protected:
  void SetUp() override {
    AppControllerTest::SetUp();
    app_->setDocsState(QString());
    writeIntegrationConfig(QStringLiteral("mattermost"), QJsonObject{});
  }

  QJsonArray contacts() const {
    return QJsonDocument::fromJson(app_->docsState().toUtf8()).object().value(QStringLiteral("contacts")).toArray();
  }

  QJsonObject contactNamed(const QString& name) const {
    const QJsonArray list = contacts();
    for(const auto& v : list) {
      if(v.toObject().value(QStringLiteral("name")).toString() == name) {
        return v.toObject();
      }
    }
    return {};
  }

  int merge(const QVector<heap::integrations::ExternalContact>& c) {
    return app_->mergeExternalContacts(QStringLiteral("mattermost"), c);
  }
};

TEST_F(ContactMergeTest, ImportsAContactAndLinksAPersonForDirectMessages) {
  const int changed =
      merge({mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga Titova"), QStringLiteral("Tech Lead"), kDm)});
  EXPECT_EQ(changed, 1);

  const QJsonObject c = contactNamed(QStringLiteral("Olga Titova"));
  EXPECT_EQ(c.value(QStringLiteral("role")).toString(), QStringLiteral("Tech Lead"));
  EXPECT_EQ(c.value(QStringLiteral("mattermost")).toString(), QStringLiteral("@olga.t"));
  EXPECT_EQ(c.value(QStringLiteral("mmId")).toString(), QStringLiteral("u1"));
  EXPECT_EQ(c.value(QStringLiteral("source")).toString(), QStringLiteral("mattermost"));
  EXPECT_FALSE(c.value(QStringLiteral("color")).toString().isEmpty());

  // The id is the handle itself, so "@olga.t" in heap matches Mattermost —
  // slugifyPersonName would have produced "olgat".
  EXPECT_EQ(c.value(QStringLiteral("personId")).toString(), QStringLiteral("olga.t"));
  const int row = app_->people()->indexOfId(QStringLiteral("olga.t"));
  ASSERT_GE(row, 0);
  const Person& p = app_->people()->items().at(row);
  EXPECT_EQ(p.name, QStringLiteral("Olga Titova"));
  EXPECT_EQ(p.role, QStringLiteral("Tech Lead"));
  EXPECT_EQ(p.state, QStringLiteral("idle")) << "an import is not a list of people you owe an answer to";
}

TEST_F(ContactMergeTest, AChannelRosterStaysOutOfTheRail) {
  const int before = app_->people()->rowCount();
  merge({mkContact(
      QStringLiteral("u2"), QStringLiteral("pavel"), QStringLiteral("Pavel K"), QStringLiteral("Member"), QStringLiteral("#backend"))});
  EXPECT_FALSE(contactNamed(QStringLiteral("Pavel K")).isEmpty()) << "still a contact";
  EXPECT_EQ(app_->people()->rowCount(), before) << "but not someone you have talked to";
  EXPECT_EQ(contactNamed(QStringLiteral("Pavel K")).value(QStringLiteral("channel")).toString(), QStringLiteral("#backend"));
}

TEST_F(ContactMergeTest, ReSyncingUnchangedDataWritesNothing) {
  const QVector<heap::integrations::ExternalContact> same = {
      mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga Titova"), QStringLiteral("Tech Lead"), kDm)};
  ASSERT_EQ(merge(same), 1);
  const QString after = app_->docsState();

  QSignalSpy spy(app_.get(), &AppController::docsStateChanged);
  EXPECT_EQ(merge(same), 0);
  EXPECT_EQ(spy.count(), 0) << "an idempotent sync must not churn the docs blob";
  EXPECT_EQ(app_->docsState(), after);
}

TEST_F(ContactMergeTest, AnEditOfYourOwnSurvivesButUpstreamChangesStillArrive) {
  merge({mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga Titova"), QStringLiteral("Tech Lead"), kDm)});

  // The user renames the role by hand.
  QJsonObject docs = QJsonDocument::fromJson(app_->docsState().toUtf8()).object();
  QJsonArray list = docs.value(QStringLiteral("contacts")).toArray();
  QJsonObject c = list.at(0).toObject();
  c.insert(QStringLiteral("role"), QStringLiteral("my tech lead"));
  list.replace(0, c);
  docs.insert(QStringLiteral("contacts"), list);
  app_->setDocsState(QString::fromUtf8(QJsonDocument(docs).toJson(QJsonDocument::Compact)));

  // Upstream, both the title and the name change.
  merge({mkContact(
      QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga Titova-Smirnova"), QStringLiteral("Engineering Manager"), kDm)});

  const QJsonObject after = contacts().at(0).toObject();
  EXPECT_EQ(after.value(QStringLiteral("role")).toString(), QStringLiteral("my tech lead")) << "the user's own words win";
  EXPECT_EQ(after.value(QStringLiteral("name")).toString(), QStringLiteral("Olga Titova-Smirnova"))
      << "a field the user never touched still follows the server";
}

TEST_F(ContactMergeTest, ADeletedContactDoesNotComeBack) {
  const QVector<heap::integrations::ExternalContact> one = {
      mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga Titova"), QStringLiteral("Tech Lead"), kDm)};
  ASSERT_EQ(merge(one), 1);

  app_->dismissExternalContact(QStringLiteral("mattermost"), QStringLiteral("u1"));
  app_->setDocsState(QStringLiteral(R"({"contacts":[]})"));

  EXPECT_EQ(merge(one), 0);
  EXPECT_TRUE(contacts().isEmpty());

  // Undoing the deletion puts it back in scope.
  app_->restoreExternalContact(QStringLiteral("mattermost"), QStringLiteral("u1"));
  EXPECT_EQ(merge(one), 1);
  EXPECT_EQ(contacts().size(), 1);
}

TEST_F(ContactMergeTest, ADeletedPersonIsNotRecreated) {
  const QVector<heap::integrations::ExternalContact> one = {
      mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga Titova"), QStringLiteral("Tech Lead"), kDm)};
  ASSERT_EQ(merge(one), 1);
  ASSERT_GE(app_->people()->indexOfId(QStringLiteral("olga.t")), 0);

  app_->deletePerson(QStringLiteral("olga.t"));
  ASSERT_LT(app_->people()->indexOfId(QStringLiteral("olga.t")), 0);

  merge(one);
  EXPECT_LT(app_->people()->indexOfId(QStringLiteral("olga.t")), 0)
      << "the contact keeps its personId, so the rail entry is not rebuilt behind the user's back";
}

TEST_F(ContactMergeTest, AContactTypedByHandIsAdoptedRatherThanDuplicated) {
  // Someone who filled in the Mattermost handle before the integration existed.
  app_->setDocsState(QStringLiteral(R"({"contacts":[{"name":"Olga","role":"","channel":"","mattermost":"@Olga.T"}]})"));

  merge({mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga Titova"), QStringLiteral("Tech Lead"), kDm)});

  ASSERT_EQ(contacts().size(), 1) << "matched on the handle, case-insensitively";
  const QJsonObject c = contacts().at(0).toObject();
  EXPECT_EQ(c.value(QStringLiteral("mmId")).toString(), QStringLiteral("u1"));
  EXPECT_EQ(c.value(QStringLiteral("name")).toString(), QStringLiteral("Olga")) << "their own name for the person is kept";
  EXPECT_EQ(c.value(QStringLiteral("role")).toString(), QStringLiteral("Tech Lead")) << "an empty field is filled in";
}

TEST_F(ContactMergeTest, AutoSyncIntoAnUnboundProfileIsANoop) {
  // The first sync binds the card to the profile it ran in.
  ASSERT_EQ(merge({mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga"), QString(), kDm)}), 1);
  EXPECT_EQ(readIntegrationConfig(QStringLiteral("mattermost")).value(QStringLiteral("profileId")).toString(), app_->activeProfileId());

  // A timer firing while another workspace is open must not pour colleagues into it.
  QJsonObject cfg = readIntegrationConfig(QStringLiteral("mattermost"));
  cfg.insert(QStringLiteral("profileId"), QStringLiteral("some-other-profile"));
  writeIntegrationConfig(QStringLiteral("mattermost"), cfg);

  EXPECT_EQ(merge({mkContact(QStringLiteral("u2"), QStringLiteral("pavel"), QStringLiteral("Pavel"), QString(), kDm)}), 0);
  EXPECT_TRUE(contactNamed(QStringLiteral("Pavel")).isEmpty());
}

TEST_F(ContactMergeTest, ImportingDoesNotInflateThePendingCount) {
  const int before = app_->pendingPeopleCount();
  merge({mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga"), QString(), kDm),
         mkContact(QStringLiteral("u2"), QStringLiteral("pavel"), QStringLiteral("Pavel"), QString(), kDm)});
  ASSERT_GE(app_->people()->indexOfId(QStringLiteral("olga.t")), 0);
  EXPECT_EQ(app_->pendingPeopleCount(), before) << "the rail badge counts people you owe an answer to";
}

TEST_F(ContactMergeTest, AnImportNeverHijacksAPersonTheUserAlreadyHas) {
  // Usernames are sanitised to [a-z0-9._-], so "ALEX", "al ex" and "alex!" all
  // derive the id "alex" — which the user may already have typed by hand for
  // somebody else entirely.
  Person mine;
  mine.id = QStringLiteral("alex");
  mine.name = QStringLiteral("Alex from accounting");
  mine.role = QStringLiteral("Finance");
  mine.state = QStringLiteral("todo");
  app_->people()->upsert(mine);

  merge({mkContact(QStringLiteral("u1"), QStringLiteral("ALEX"), QStringLiteral("Alexandra Petrova"), QStringLiteral("Backend"), kDm)});

  const int row = app_->people()->indexOfId(QStringLiteral("alex"));
  ASSERT_GE(row, 0);
  const Person& kept = app_->people()->items().at(row);
  EXPECT_EQ(kept.name, QStringLiteral("Alex from accounting")) << "a stranger was welded onto an existing person";
  EXPECT_EQ(kept.role, QStringLiteral("Finance"));

  // The contact still lands, just linked to its own Person (or none at all).
  const QJsonObject c = contactNamed(QStringLiteral("Alexandra Petrova"));
  ASSERT_FALSE(c.isEmpty());
  EXPECT_NE(c.value(QStringLiteral("personId")).toString(), QStringLiteral("alex"));
}

TEST_F(ContactMergeTest, TwoServerUsersSharingAHandleBothSurvive) {
  // The handle index is the fallback for contacts typed before the
  // integration existed. It was not updated for rows appended during the same
  // merge, so a second user with the same handle overwrote the first.
  merge({mkContact(QStringLiteral("u1"), QStringLiteral("bob"), QStringLiteral("Bob One"), QString(), kDm),
         mkContact(QStringLiteral("u2"), QStringLiteral("bob"), QStringLiteral("Bob Two"), QString(), kDm)});

  EXPECT_EQ(contacts().size(), 2) << "one of them overwrote the other";
  EXPECT_FALSE(contactNamed(QStringLiteral("Bob One")).isEmpty());
  EXPECT_FALSE(contactNamed(QStringLiteral("Bob Two")).isEmpty());
}

TEST_F(ContactMergeTest, TheSameColourComesBackAcrossRuns) {
  // qHash(QString) is seeded per process, so it gave the same person a
  // different colour in another profile or after a restart.
  merge({mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga"), QString(), kDm)});
  const QString first = contactNamed(QStringLiteral("Olga")).value(QStringLiteral("color")).toString();
  ASSERT_FALSE(first.isEmpty());

  app_->setDocsState(QString());
  merge({mkContact(QStringLiteral("u1"), QStringLiteral("olga.t"), QStringLiteral("Olga"), QString(), kDm)});
  EXPECT_EQ(contactNamed(QStringLiteral("Olga")).value(QStringLiteral("color")).toString(), first);
}

// ── People picker (the rail's "+") ───────────────────────────────────────
//
// The rail is a to-do list now: it shows only the people something is pending
// on, and adding to it goes through a search over everyone already known.
// These cover the fold — one row per human across Docs contacts and People —
// and the link written back so the two never drift into duplicates.
class PeoplePickerTest : public AppControllerTest {
 protected:
  void SetUp() override {
    AppControllerTest::SetUp();
    app_->setDocsState(QString());
    app_->people()->reset({});
  }

  QVariantMap candidateNamed(const QString& name) const {
    const QVariantList list = app_->pingCandidates();
    for(const QVariant& v : list) {
      const QVariantMap m = v.toMap();
      if(m.value(QStringLiteral("name")).toString() == name) {
        return m;
      }
    }
    return {};
  }

  QJsonArray contacts() const {
    return QJsonDocument::fromJson(app_->docsState().toUtf8()).object().value(QStringLiteral("contacts")).toArray();
  }
};

TEST_F(PeoplePickerTest, TheRailHidesIdlePeopleAndTheCandidateListDoesNot) {
  Person todo;
  todo.id = QStringLiteral("ada");
  todo.name = QStringLiteral("Ada");
  todo.state = QStringLiteral("todo");
  Person imported;
  imported.id = QStringLiteral("olga.t");
  imported.name = QStringLiteral("Olga");
  imported.state = QStringLiteral("idle");
  app_->people()->reset({todo, imported});

  EXPECT_EQ(app_->people()->rowCount(), 2);
  EXPECT_EQ(app_->activePeople()->rowCount(), 1);
  EXPECT_EQ(app_->activePeople()->index(0, 0).data(PersonModel::IdRole).toString(), QStringLiteral("ada"));
  // The idle one is not lost — it is exactly who the picker is for.
  EXPECT_EQ(app_->pingCandidates().size(), 2);
  EXPECT_FALSE(candidateNamed(QStringLiteral("Olga")).value(QStringLiteral("active")).toBool());
  EXPECT_TRUE(candidateNamed(QStringLiteral("Ada")).value(QStringLiteral("active")).toBool());
}

TEST_F(PeoplePickerTest, TheRailFollowsAStateChangeInBothDirections) {
  Person p;
  p.id = QStringLiteral("ada");
  p.name = QStringLiteral("Ada");
  p.state = QStringLiteral("idle");
  app_->people()->reset({p});
  EXPECT_EQ(app_->activePeople()->rowCount(), 0);

  app_->setPersonState(QStringLiteral("ada"), QStringLiteral("todo"));
  EXPECT_EQ(app_->activePeople()->rowCount(), 1);

  // …and the last hop of the cycle (replied → idle) takes them back off it,
  // which is how the rail empties without deleting anybody.
  app_->setPersonState(QStringLiteral("ada"), QStringLiteral("replied"));
  EXPECT_EQ(app_->activePeople()->rowCount(), 1);
  app_->cyclePerson(QStringLiteral("ada"));
  EXPECT_EQ(app_->activePeople()->rowCount(), 0);
  EXPECT_EQ(app_->people()->rowCount(), 1);
}

// "Wrote" takes the person off Today; it is undoable and says so with a
// toast (dogfood 2026-10-16: the person was gone with no way back).
TEST_F(PeoplePickerTest, WroteIsUndoable) {
  Person p;
  p.id = QStringLiteral("ada");
  p.name = QStringLiteral("Ada");
  p.state = QStringLiteral("todo");
  app_->people()->reset({p});
  QStringList toasts;
  QObject::connect(app_.get(), &AppController::undoableToast, app_.get(), [&](const QString& msg, int) {
    toasts << msg;
  });
  app_->setPersonState(QStringLiteral("ada"), QStringLiteral("pinged"));
  ASSERT_EQ(toasts.size(), 1);
  EXPECT_TRUE(toasts.first().contains(QStringLiteral("Ada")));
  EXPECT_EQ(app_->people()->items().at(0).state, QStringLiteral("pinged"));
  app_->undo();
  EXPECT_EQ(app_->people()->items().at(0).state, QStringLiteral("todo"));
  // The same state again is no change and no toast.
  app_->setPersonState(QStringLiteral("ada"), QStringLiteral("todo"));
  EXPECT_EQ(toasts.size(), 1);
}

// The open asks for Today and the sidebar (APP-321): only "todo", and a way
// back to it from "Люди" says so with an Undo.
TEST_F(PeoplePickerTest, OpenPingsAreTheTodoOnesAndComeBack) {
  Person a;
  a.id = QStringLiteral("ada");
  a.name = QStringLiteral("Ada");
  a.state = QStringLiteral("todo");
  Person b = a;
  b.id = QStringLiteral("bob");
  b.name = QStringLiteral("Bob");
  b.state = QStringLiteral("pinged");
  app_->people()->reset({a, b});
  QVariantList open = app_->openPings();
  ASSERT_EQ(open.size(), 1);
  EXPECT_EQ(open.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("ada"));
  QStringList toasts;
  QObject::connect(app_.get(), &AppController::undoableToast, app_.get(), [&](const QString& msg, int) {
    toasts << msg;
  });
  app_->setPersonState(QStringLiteral("bob"), QStringLiteral("todo"));
  EXPECT_EQ(app_->openPings().size(), 2);
  ASSERT_EQ(toasts.size(), 1);
  EXPECT_TRUE(toasts.first().contains(QStringLiteral("Bob")));
}

// A click on a task's key copies it and says so.
TEST_F(AppControllerTest, CopyTaskKeyToasts) {
  QStringList toasts;
  QObject::connect(app_.get(), &AppController::toast, app_.get(), [&](const QString& msg, const QString&) {
    toasts << msg;
  });
  app_->copyTaskKey(QStringLiteral("  APP-123 "));
  ASSERT_EQ(toasts.size(), 1);
  EXPECT_TRUE(toasts.first().contains(QStringLiteral("APP-123")));
  app_->copyTaskKey(QString());
  EXPECT_EQ(toasts.size(), 1);
}

TEST_F(PeoplePickerTest, AContactAndThePersonItImportedAreOneCandidate) {
  Person p;
  p.id = QStringLiteral("olga.t");
  p.name = QStringLiteral("Olga Titova");
  p.state = QStringLiteral("idle");
  app_->people()->reset({p});
  app_->setDocsState(QStringLiteral(R"({"contacts":[{"name":"Olga Titova","role":"Tech Lead",)"
                                    R"("mattermost":"@olga.t","personId":"olga.t"}]})"));

  const QVariantList list = app_->pingCandidates();
  ASSERT_EQ(list.size(), 1);
  const QVariantMap m = list.first().toMap();
  EXPECT_EQ(m.value(QStringLiteral("personId")).toString(), QStringLiteral("olga.t"));
  EXPECT_EQ(m.value(QStringLiteral("role")).toString(), QStringLiteral("Tech Lead"));
  EXPECT_EQ(m.value(QStringLiteral("state")).toString(), QStringLiteral("idle"));
}

TEST_F(PeoplePickerTest, AContactWhosePersonWasDeletedIsOfferedAsANewOne) {
  // A handle of its own: ids are unique across every profile, and this suite
  // shares one test-mode state.json, so a name another case saved would come
  // back here as "…-2".
  app_->setDocsState(QStringLiteral(R"({"contacts":[{"name":"Nina Kaur","mattermost":"@nina.k","personId":"gone"}]})"));
  const QVariantMap m = candidateNamed(QStringLiteral("Nina Kaur"));
  EXPECT_TRUE(m.value(QStringLiteral("personId")).toString().isEmpty());

  const QVariantMap draft = app_->pingDraftFor(m);
  EXPECT_TRUE(draft.value(QStringLiteral("_isNew")).toBool());
  // The id is the handle people already @-mention, not a slug of the name.
  EXPECT_EQ(draft.value(QStringLiteral("id")).toString(), QStringLiteral("nina.k"));
}

TEST_F(PeoplePickerTest, PickingAContactCreatesThePersonAndLinksThemBack) {
  app_->setDocsState(QStringLiteral(R"({"contacts":[{"name":"Olga Titova","role":"Tech Lead","mattermost":"@olga.t"}]})"));
  QVariantMap draft = app_->pingDraftFor(candidateNamed(QStringLiteral("Olga Titova")));
  EXPECT_EQ(draft.value(QStringLiteral("state")).toString(), QStringLiteral("todo"));
  draft[QStringLiteral("question")] = QStringLiteral("release cut?");
  app_->savePerson(draft);

  ASSERT_EQ(app_->people()->rowCount(), 1);
  EXPECT_EQ(app_->activePeople()->rowCount(), 1);
  ASSERT_EQ(contacts().size(), 1);
  EXPECT_EQ(contacts().first().toObject().value(QStringLiteral("personId")).toString(), QStringLiteral("olga.t"));

  // Second time round the same contact resolves to that Person instead of
  // making a duplicate.
  EXPECT_EQ(app_->pingCandidates().size(), 1);
  const QVariantMap again = app_->pingDraftFor(candidateNamed(QStringLiteral("Olga Titova")));
  EXPECT_FALSE(again.value(QStringLiteral("_isNew")).toBool());
  EXPECT_EQ(again.value(QStringLiteral("question")).toString(), QStringLiteral("release cut?"));
}

TEST_F(PeoplePickerTest, PickingSomeoneAlreadyOnTheRailKeepsTheirState) {
  Person p;
  p.id = QStringLiteral("ada");
  p.name = QStringLiteral("Ada");
  p.state = QStringLiteral("pinged");
  app_->people()->reset({p});
  const QVariantMap draft = app_->pingDraftFor(candidateNamed(QStringLiteral("Ada")));
  EXPECT_EQ(draft.value(QStringLiteral("state")).toString(), QStringLiteral("pinged"));
}

TEST_F(PeoplePickerTest, CreatingAContactFromThePickerWritesBothSides) {
  QVariantMap draft = app_->newContactDraft(QStringLiteral("Ivan Petrov"));
  EXPECT_EQ(draft.value(QStringLiteral("name")).toString(), QStringLiteral("Ivan Petrov"));
  app_->savePerson(draft);

  ASSERT_EQ(app_->people()->rowCount(), 1);
  EXPECT_EQ(app_->activePeople()->rowCount(), 1);
  ASSERT_EQ(contacts().size(), 1);
  const QJsonObject c = contacts().first().toObject();
  EXPECT_EQ(c.value(QStringLiteral("name")).toString(), QStringLiteral("Ivan Petrov"));
  EXPECT_EQ(c.value(QStringLiteral("personId")).toString(), QStringLiteral("i.petrov"));
  // One row per human: the new contact and its Person do not both show up.
  EXPECT_EQ(app_->pingCandidates().size(), 1);
}

TEST_F(PeoplePickerTest, AnOrdinaryEditNeitherCreatesNorLinksAContact) {
  Person p;
  p.id = QStringLiteral("ada");
  p.name = QStringLiteral("Ada");
  p.state = QStringLiteral("todo");
  app_->people()->reset({p});
  QVariantMap draft = app_->personById(QStringLiteral("ada"));
  draft[QStringLiteral("question")] = QStringLiteral("ping about CI");
  app_->savePerson(draft);
  EXPECT_TRUE(contacts().isEmpty());
}

// ── APP-253: one free-window search, task blocks are busy ──

TEST_F(AppControllerTest, FreeWindowSeesTaskBlocks) {
  const QDate thu(2026, 10, 8);
  app_->setWorkdayStart(9);
  app_->setWorkdayEnd(19);
  Task busy = mkTask(QStringLiteral("T-1"), QStringLiteral("planned"));
  busy.scheduledAt = QDateTime(thu, QTime(9, 0));
  busy.scheduledHasTime = true;
  busy.estimateMinutes = 120;
  Task t = mkTask(QStringLiteral("T-2"), QStringLiteral("to place"));
  t.estimateMinutes = 60;
  app_->tasks()->reset({busy, t});

  const QVariantMap w = app_->freeWindowAt(QStringLiteral("T-2"), thu, QDateTime(thu, QTime(8, 0)));
  EXPECT_TRUE(w.value("found").toBool());
  EXPECT_DOUBLE_EQ(w.value("start").toDouble(), 11.0) << "the 9-11 task block is taken";
  // Its own block never stands in its way.
  const QVariantMap self = app_->freeWindowAt(QStringLiteral("T-1"), thu, QDateTime(thu, QTime(8, 0)));
  EXPECT_DOUBLE_EQ(self.value("start").toDouble(), 9.0);
}

TEST_F(AppControllerTest, FreeWindowSaysNoWhenTheWorkdayIsFull) {
  const QDate thu(2026, 10, 8);
  app_->setWorkdayStart(9);
  app_->setWorkdayEnd(19);
  QVariantMap draft = app_->newEventDraft(9, thu);
  draft["end"] = 19.0;
  app_->saveEvent(draft);
  Task t = mkTask(QStringLiteral("T-2"), QStringLiteral("to place"));
  t.estimateMinutes = 60;
  app_->tasks()->reset({t});

  const QVariantMap w = app_->freeWindowAt(QStringLiteral("T-2"), thu, QDateTime(thu, QTime(10, 0)));
  EXPECT_FALSE(w.value("found").toBool());
  EXPECT_EQ(w.value("nextDate").toDate(), thu.addDays(1));
  EXPECT_DOUBLE_EQ(w.value("nextStart").toDouble(), 9.0);
  EXPECT_DOUBLE_EQ(w.value("lateStart").toDouble(), 19.0);
}

// ── APP-248: carrying leftovers, by hand only ──

namespace {

Task leftover(const QString& id, const QDateTime& at, bool timed) {
  Task t = mkTask(id, id);
  t.scheduledAt = at;
  t.scheduledHasTime = timed;
  t.dueAt = QDateTime(QDate(2026, 10, 20), QTime(0, 0));
  t.recurrence = QStringLiteral("every:week");
  t.estimateMinutes = 60;
  return t;
}

}  // namespace

TEST_F(AppControllerTest, CarryTomorrowKeepsTimeDeadlineAndRepeat) {
  const QDateTime now(QDate(2026, 10, 8), QTime(18, 0));
  app_->tasks()->reset({leftover(QStringLiteral("T-1"), QDateTime(QDate(2026, 10, 7), QTime(14, 30)), true),
                        leftover(QStringLiteral("T-2"), QDateTime(QDate(2026, 10, 8), QTime(0, 0)), false)});
  EXPECT_EQ(app_->carryTasksAt({QStringLiteral("T-1"), QStringLiteral("T-2")}, QStringLiteral("tomorrow"), now), 2);
  const Task a = app_->tasks()->items().at(0);
  const Task b = app_->tasks()->items().at(1);
  EXPECT_EQ(a.scheduledAt, QDateTime(QDate(2026, 10, 9), QTime(14, 30)));
  EXPECT_TRUE(a.scheduledHasTime);
  EXPECT_EQ(b.scheduledAt.date(), QDate(2026, 10, 9));
  EXPECT_FALSE(b.scheduledHasTime);
  EXPECT_EQ(a.dueAt.date(), QDate(2026, 10, 20)) << "the deadline is never touched";
  EXPECT_EQ(a.recurrence, QStringLiteral("every:week"));

  app_->undo();
  EXPECT_EQ(app_->tasks()->items().at(0).scheduledAt, QDateTime(QDate(2026, 10, 7), QTime(14, 30))) << "one undo step";
  EXPECT_EQ(app_->tasks()->items().at(1).scheduledAt.date(), QDate(2026, 10, 8));
}

TEST_F(AppControllerTest, CarrySomedayAndClearDropOnlyThePlan) {
  const QDateTime now(QDate(2026, 10, 8), QTime(18, 0));
  app_->tasks()->reset({leftover(QStringLiteral("T-1"), QDateTime(QDate(2026, 10, 8), QTime(10, 0)), true),
                        leftover(QStringLiteral("T-2"), QDateTime(QDate(2026, 10, 8), QTime(11, 0)), true)});
  app_->carryTasksAt({QStringLiteral("T-1")}, QStringLiteral("someday"), now);
  app_->carryTasksAt({QStringLiteral("T-2")}, QStringLiteral("clear"), now);
  const Task a = app_->tasks()->items().at(0);
  const Task b = app_->tasks()->items().at(1);
  EXPECT_TRUE(a.someday);
  EXPECT_FALSE(a.scheduledAt.isValid());
  EXPECT_EQ(a.status, QStringLiteral("todo")) << "no column move";
  EXPECT_FALSE(b.someday);
  EXPECT_FALSE(b.scheduledAt.isValid());
  EXPECT_TRUE(b.dueAt.isValid());
}

TEST_F(AppControllerTest, CarryToWindowsLinesTheSelectionUp) {
  app_->setWorkdayStart(9);
  app_->setWorkdayEnd(19);
  const QDateTime now(QDate(2026, 10, 8), QTime(17, 0));  // Thursday, two hours left
  QVariantMap draft = app_->newEventDraft(17, QDate(2026, 10, 8));
  draft["end"] = 18.0;
  app_->saveEvent(draft);
  app_->tasks()->reset({leftover(QStringLiteral("T-1"), QDateTime(QDate(2026, 10, 8), QTime(10, 0)), true),
                        leftover(QStringLiteral("T-2"), QDateTime(QDate(2026, 10, 8), QTime(11, 0)), true)});
  EXPECT_EQ(app_->carryTasksAt({QStringLiteral("T-1"), QStringLiteral("T-2")}, QStringLiteral("window"), now), 2);
  EXPECT_EQ(app_->tasks()->items().at(0).scheduledAt, QDateTime(QDate(2026, 10, 8), QTime(18, 0)));
  EXPECT_EQ(app_->tasks()->items().at(1).scheduledAt, QDateTime(QDate(2026, 10, 9), QTime(9, 0)))
      << "the second one does not stack on the first, nor run past the working day";
}

// ─── 0.9 acceptance (APP-221, batch B4) ───

namespace {
Task trackerCard(const QString& id, const QString& key) {
  Task t = mkTask(id, QStringLiteral("incident"));
  t.externalId = key;
  t.externalProvider = QStringLiteral("jira");
  t.externalUrl = QStringLiteral("https://jira.example.com/browse/") + key;
  return t;
}
}  // namespace

TEST_F(AppControllerTest, ATrackerKeyIsNotGivenToANewLocalTask) {
  // A3-7: INC-100 is OPS-1's Jira key; "INC-100 follow up" must not become a
  // second task answering to INC-100.
  app_->tasks()->reset({trackerCard(QStringLiteral("OPS-1"), QStringLiteral("INC-100"))});
  const QVariantMap draft = app_->quickTaskDraft(QStringLiteral("INC-100 follow up"));
  EXPECT_NE(draft.value("id").toString(), QStringLiteral("INC-100"));
  EXPECT_TRUE(draft.value("title").toString().contains(QStringLiteral("INC-100")));
  const QVariantMap parse = app_->captureParse(QStringLiteral("INC-100 follow up"));
  EXPECT_TRUE(parse.value("ticketTaken").toBool());
  EXPECT_EQ(parse.value("ticketTakenId").toString(), QStringLiteral("OPS-1"));
}

TEST_F(AppControllerTest, TheCommandLineNamesATicketByItsKeyAndPutsTheExactKeyFirst) {
  // A3-10, A3-23: rows read the key the card shows; SD-105 is not SD-1050.
  app_->tasks()->reset(
      {trackerCard(QStringLiteral("SUP-1"), QStringLiteral("SD-1050")), trackerCard(QStringLiteral("SUP-2"), QStringLiteral("SD-105"))});
  const QVariantMap r = app_->commandLine(QStringLiteral("SD-105"), 10);
  const QVariantList tasks = r.value("tasks").toList();
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks.first().toMap().value("id").toString(), QStringLiteral("SUP-2"));
  EXPECT_EQ(tasks.first().toMap().value("key").toString(), QStringLiteral("SD-105"));
  EXPECT_EQ(app_->displayKey(QStringLiteral("SUP-1")), QStringLiteral("SD-1050"));
}

TEST_F(AppControllerTest, StartingASecondTimerSaysTheFirstOneStopped) {
  // A1-14: a 6-hour timer ended without a word.
  app_->tasks()->reset({mkTask(QStringLiteral("FE-26"), QStringLiteral("a")), mkTask(QStringLiteral("FE-3"), QStringLiteral("b"))});
  app_->startTaskTimer(QStringLiteral("FE-26"));
  QSignalSpy undoable(app_.get(), &AppController::undoableToast);
  app_->startTaskTimer(QStringLiteral("FE-3"));
  ASSERT_EQ(undoable.count(), 1);
  EXPECT_TRUE(undoable.first().first().toString().contains(QStringLiteral("FE-26")));
  EXPECT_TRUE(undoable.first().first().toString().contains(QStringLiteral("stopped")));
}

TEST_F(AppControllerTest, DoneOnARecurringTaskIsOneToastWithTheNextDay) {
  // A2-18, A2-19: the note on the next copy was replaced at once by "Done",
  // and it read an ISO date and the copy's "-r1" id.
  Task t = mkTask(QStringLiteral("DBA-21"), QStringLiteral("check backups"));
  t.recurrence = QStringLiteral("every:day");
  t.scheduledAt = QDateTime(QDate::currentDate(), QTime(9, 0));
  app_->tasks()->reset({t});
  QSignalSpy plain(app_.get(), &AppController::toast);
  QSignalSpy undoable(app_.get(), &AppController::undoableToast);
  app_->toggleDone({QStringLiteral("DBA-21")});
  ASSERT_EQ(undoable.count(), 1);
  const QString msg = undoable.first().first().toString();
  EXPECT_TRUE(msg.contains(QStringLiteral("Done: DBA-21"))) << msg.toStdString();
  EXPECT_TRUE(msg.contains(QStringLiteral("next one tomorrow"))) << msg.toStdString();
  EXPECT_FALSE(msg.contains(QStringLiteral("-r1"))) << msg.toStdString();
  EXPECT_FALSE(msg.contains(QDate::currentDate().addDays(1).toString(Qt::ISODate))) << msg.toStdString();
  for(const QList<QVariant>& args : plain) {
    EXPECT_FALSE(args.first().toString().contains(QStringLiteral("next one"))) << "said once, in the Done toast";
  }
}

TEST_F(AppControllerTest, ArchivingNamesTheKeyForDone) {
  // A5-20: e is "complete" in Todoist; here it archives — and says what d is.
  app_->tasks()->reset({mkTask(QStringLiteral("MOB-3"), QStringLiteral("a"))});
  QSignalSpy undoable(app_.get(), &AppController::undoableToast);
  app_->setArchived(QStringLiteral("MOB-3"), true);
  ASSERT_EQ(undoable.count(), 1);
  EXPECT_TRUE(undoable.first().first().toString().contains(QStringLiteral("done is"))) << undoable.first().first().toString().toStdString();
}

TEST_F(AppControllerTest, ACommonKeyAsksBeforeItIsTaken) {
  // A7-9: Ctrl+C copies everywhere else; binding it asks first.
  EXPECT_FALSE(app_->commonShortcutWarning(QStringLiteral("task.copyId"), QStringLiteral("Ctrl+C")).isEmpty());
  EXPECT_FALSE(app_->commonShortcutWarning(QStringLiteral("task.copyId"), QStringLiteral("Backspace")).isEmpty());
  EXPECT_TRUE(app_->commonShortcutWarning(QStringLiteral("task.copyId"), QStringLiteral("Ctrl+K")).isEmpty());
}

TEST_F(AppControllerTest, NotesAndDocsAreNamedAsTheSidebarNamesThem) {
  // A1-31, A6-27, A7-10: "Go to Notes", and Docs has a key.
  EXPECT_EQ(app_->shortcutLabel(QStringLiteral("section.knowledge")), QStringLiteral("Go to Notes"));
  EXPECT_FALSE(app_->shortcutText(QStringLiteral("view.docs")).isEmpty());
}

// A5-8: carrying overdue tasks moves the plan date only; the toast says the
// deadline keeps them overdue, "clear" with nothing to clear says so, and
// "clearDue" removes a local deadline by name (never a tracker's).
TEST_F(AppControllerTest, CarryOverdueByDeadlineSaysSo) {
  const QDateTime now(QDate(2026, 10, 8), QTime(18, 0));
  Task a = mkTask(QStringLiteral("T-1"), QStringLiteral("T-1"));
  a.dueAt = QDateTime(QDate(2026, 10, 1), QTime(0, 0));
  Task b = mkTask(QStringLiteral("T-2"), QStringLiteral("T-2"));
  b.dueAt = QDateTime(QDate(2026, 10, 2), QTime(0, 0));
  b.externalId = QStringLiteral("9");
  b.externalProvider = QStringLiteral("jira");
  app_->tasks()->reset({a, b});
  QStringList toasts;
  QObject::connect(app_.get(), &AppController::undoableToast, [&](const QString& m, int) {
    toasts << m;
  });
  QObject::connect(app_.get(), &AppController::toast, [&](const QString& m, const QString&) {
    toasts << m;
  });

  EXPECT_EQ(app_->carryTasksAt({QStringLiteral("T-1"), QStringLiteral("T-2")}, QStringLiteral("tomorrow"), now), 2);
  ASSERT_FALSE(toasts.isEmpty());
  EXPECT_TRUE(toasts.last().contains(QStringLiteral("2 still overdue"))) << toasts.last().toStdString();
  EXPECT_TRUE(app_->tasks()->items().at(0).dueAt.isValid()) << "carrying never touches the deadline";

  app_->undo();
  toasts.clear();
  EXPECT_EQ(app_->carryTasksAt({QStringLiteral("T-1"), QStringLiteral("T-2")}, QStringLiteral("clear"), now), 0);
  ASSERT_EQ(toasts.size(), 1) << "nothing changed is said, not silent";
  EXPECT_TRUE(toasts.first().contains(QStringLiteral("deadline")));

  toasts.clear();
  EXPECT_EQ(app_->carryTasksAt({QStringLiteral("T-1"), QStringLiteral("T-2")}, QStringLiteral("clearDue"), now), 1);
  EXPECT_FALSE(app_->tasks()->items().at(0).dueAt.isValid());
  EXPECT_TRUE(app_->tasks()->items().at(1).dueAt.isValid()) << "a tracker's deadline stays the tracker's";
  app_->undo();
  EXPECT_TRUE(app_->tasks()->items().at(0).dueAt.isValid()) << "one undo step";
}

TEST_F(AppControllerTest, CarryWithAnUnknownModeDoesNothing) {
  app_->tasks()->reset({leftover(QStringLiteral("T-1"), QDateTime(QDate(2026, 10, 8), QTime(10, 0)), true)});
  EXPECT_EQ(app_->carryTasksAt({QStringLiteral("T-1")}, QStringLiteral("auto"), QDateTime(QDate(2026, 10, 8), QTime(9, 0))), 0);
  EXPECT_TRUE(app_->tasks()->items().at(0).scheduledAt.isValid());
}

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
    // Test mode keeps integration secrets in this file instead of the OS
    // keychain; a token left by an earlier run must not leak into this one.
    QFile::remove(appData + QStringLiteral("/secrets.json"));
    QDir(appData + QStringLiteral("/backups")).removeRecursively();
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// Flathub forbids an app that checks for or offers its own updates: inside a
// Flatpak, Settings -> About says the software centre updates it, and neither
// the button nor a call from anywhere else reaches GitHub.
TEST_F(AppControllerTest, AFlatpakNeverChecksForUpdates) {
  EXPECT_FALSE(app_->updatesFromStore()) << "not a Flatpak: the updater stays";
  app_.reset();
  const bool had = qEnvironmentVariableIsSet("FLATPAK_ID");
  const QByteArray before = qgetenv("FLATPAK_ID");
  qputenv("FLATPAK_ID", "io.github.sectapunterx.tabless");
  const auto restore = qScopeGuard([had, before]() {
    if(had) {
      qputenv("FLATPAK_ID", before);
    } else {
      qunsetenv("FLATPAK_ID");
    }
  });
  app_ = std::make_unique<AppController>();
  EXPECT_TRUE(app_->updatesFromStore());
  QSignalSpy status(app_.get(), &AppController::updateStatusChanged);
  app_->checkForUpdates();
  app_->downloadUpdate();
  EXPECT_EQ(status.count(), 0);
  EXPECT_TRUE(app_->updateStatus().isEmpty());
  EXPECT_TRUE(app_->updatePhase().isEmpty());
  EXPECT_FALSE(app_->updateCanInstall());
  EXPECT_FALSE(app_->featureBuild()->supported()) << "no feature build beside a store copy";
}
