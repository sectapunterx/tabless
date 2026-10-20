// The safety net (APP-157…) as AppController runs it: the settings switches,
// once-a-day / once-per-link bookkeeping, and that nothing changes the data.
// The rules themselves are tested pure in test_safety_net.cpp.

#include "AppController.h"
#include "Models.h"
#include "StateSerializer.h"

#include "people/AttendeeMatch.h"
#include "platform/Paths.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

// Today at a fixed hour: the tick is given its clock, so nothing here depends
// on when the suite runs. Today, not a fixed date, because the bookkeeping of
// sent notices drops entries older than three days on load.
QDateTime todayAt(int hour, int minute = 0) {
  return {QDate::currentDate(), QTime(hour, minute)};
}

QString settingsJson(const QJsonObject& safety) {
  QJsonObject notif;
  // Only the safety net may speak.
  notif["meetingReminders"] = false;
  notif["deadlineReminders"] = false;
  notif["standupReminder"] = false;
  notif["blockedDailyDigest"] = false;
  notif["quietHours"] = false;
  QJsonObject root;
  root["notifications"] = notif;
  root["safety"] = safety;
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

Task task(const QString& id, const QString& status) {
  Task t;
  t.id = id;
  t.title = id + QStringLiteral(" title");
  t.status = status;
  t.priority = QStringLiteral("P2");
  return t;
}

}  // namespace

class SafetyNetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    QFile::remove(heap::paths::dataDir() + QStringLiteral("/reminders.json"));
    app_ = std::make_unique<AppController>();
    app_->events()->reset({});
    app_->tasks()->reset({});
    // The profile on disk persists between runs (LinksSurviveARestart saves).
    for(const WaitingOn& w : app_->waitingOnLinks()) {
      app_->clearWaitingOn(w.taskId);
    }
  }

  void TearDown() override {
    app_.reset();
  }

  std::unique_ptr<AppController> app_;
};

// ── APP-157 ──

TEST_F(SafetyNetTest, EndOfDayIsOffByDefault) {
  app_->setAppSettingsJson(settingsJson({}));
  Task t = task(QStringLiteral("T-1"), QStringLiteral("prog"));
  t.timerStartedAt = todayAt(9);
  app_->tasks()->reset({t});
  const QSignalSpy spy(app_.get(), &AppController::safetyNotice);

  app_->runAutomationAt(todayAt(18, 5));

  EXPECT_EQ(spy.count(), 0);
}

TEST_F(SafetyNetTest, EndOfDaySaysOnceThatATimerIsRunning) {
  app_->setAppSettingsJson(settingsJson({{"endOfDay", true}, {"endOfDayTime", "18:00"}}));
  Task t = task(QStringLiteral("T-1"), QStringLiteral("prog"));
  t.timerStartedAt = todayAt(9);
  app_->tasks()->reset({t});
  const QSignalSpy spy(app_.get(), &AppController::safetyNotice);

  app_->runAutomationAt(todayAt(17, 59));
  EXPECT_EQ(spy.count(), 0);
  app_->runAutomationAt(todayAt(18, 1));
  app_->runAutomationAt(todayAt(18, 2));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("endOfDay"));
  EXPECT_EQ(spy.at(0).at(3).toStringList(), QStringList{QStringLiteral("T-1")});
  // It said so; it did not stop the timer.
  EXPECT_TRUE(app_->tasks()->items().at(0).timerStartedAt.isValid());
}

TEST_F(SafetyNetTest, EndOfDayNamesTasksInProgressWithoutAMove) {
  app_->setAppSettingsJson(settingsJson({{"endOfDay", true}, {"staleDays", 3}}));
  Task stale = task(QStringLiteral("T-1"), QStringLiteral("prog"));
  stale.statusChangedAt = todayAt(10).addDays(-4);
  Task fresh = task(QStringLiteral("T-2"), QStringLiteral("prog"));
  fresh.statusChangedAt = todayAt(10).addDays(-1);
  Task done = task(QStringLiteral("T-3"), QStringLiteral("done"));
  done.statusChangedAt = todayAt(10).addDays(-30);
  app_->tasks()->reset({stale, fresh, done});
  const QSignalSpy spy(app_.get(), &AppController::safetyNotice);

  app_->runAutomationAt(todayAt(18, 30));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(3).toStringList(), QStringList{QStringLiteral("T-1")});
  EXPECT_EQ(app_->tasks()->items().at(0).status, QStringLiteral("prog"));
}

TEST_F(SafetyNetTest, EndOfDayStaysQuietWhenThereIsNothingToSay) {
  app_->setAppSettingsJson(settingsJson({{"endOfDay", true}}));
  app_->tasks()->reset({task(QStringLiteral("T-1"), QStringLiteral("todo"))});
  const QSignalSpy spy(app_.get(), &AppController::safetyNotice);

  app_->runAutomationAt(todayAt(18, 30));

  EXPECT_EQ(spy.count(), 0);
}

// ── APP-190: the day's summary ──

TEST_F(SafetyNetTest, EndOfDaySummaryShowsClosedCarryOverAndTimers) {
  app_->setLanguage(QStringLiteral("en"));
  const QDateTime now = todayAt(18, 30);
  Task closed = task(QStringLiteral("T-1"), QStringLiteral("done"));
  closed.statusChangedAt = todayAt(11);
  Task closedYesterday = task(QStringLiteral("T-2"), QStringLiteral("done"));
  closedYesterday.statusChangedAt = todayAt(11).addDays(-1);
  Task dueToday = task(QStringLiteral("T-3"), QStringLiteral("todo"));
  dueToday.dueAt = todayAt(17);
  Task overdue = task(QStringLiteral("T-4"), QStringLiteral("prog"));
  overdue.scheduledAt = todayAt(9).addDays(-2);
  overdue.timerStartedAt = todayAt(14);
  Task tomorrow = task(QStringLiteral("T-5"), QStringLiteral("todo"));
  tomorrow.dueAt = todayAt(10).addDays(1);
  Task undated = task(QStringLiteral("T-6"), QStringLiteral("todo"));
  app_->tasks()->reset({closed, closedYesterday, dueToday, overdue, tomorrow, undated});

  const QVariantMap s = app_->endOfDaySummaryAt(now);
  const auto ids = [&s](const char* key) {
    QStringList out;
    for(const QVariant& v : s.value(QString::fromUtf8(key)).toList()) {
      out << v.toMap().value(QStringLiteral("id")).toString();
    }
    return out;
  };
  EXPECT_EQ(ids("closed"), QStringList{QStringLiteral("T-1")});
  EXPECT_EQ(ids("carryOver"), (QStringList{QStringLiteral("T-3"), QStringLiteral("T-4")}));
  // Other workspaces on disk may hold timers of their own; this one's is there.
  EXPECT_TRUE(ids("timers").contains(QStringLiteral("T-4")));
  EXPECT_EQ(s.value(QStringLiteral("date")).toDate(), now.date());

  // It only reads: nothing moved, rescheduled or stopped.
  const QVector<Task> after = app_->tasks()->items();
  EXPECT_EQ(after.at(2).status, QStringLiteral("todo"));
  EXPECT_EQ(after.at(2).dueAt, todayAt(17));
  EXPECT_EQ(after.at(3).scheduledAt, todayAt(9).addDays(-2));
  EXPECT_TRUE(after.at(3).timerStartedAt.isValid());
}

TEST_F(SafetyNetTest, EndOfDayWrapsUpADayWithWorkClosed) {
  app_->setLanguage(QStringLiteral("en"));
  app_->setAppSettingsJson(settingsJson({{"endOfDay", true}}));
  Task closed = task(QStringLiteral("T-1"), QStringLiteral("done"));
  closed.statusChangedAt = todayAt(11);
  app_->tasks()->reset({closed});
  const QSignalSpy spy(app_.get(), &AppController::safetyNotice);

  app_->runAutomationAt(todayAt(18, 30));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("endOfDay"));
  EXPECT_TRUE(spy.at(0).at(2).toString().contains(QStringLiteral("1 closed today"))) << spy.at(0).at(2).toString().toStdString();
}

// ── APP-158 ──

namespace {

Person person(const QString& id, const QString& state = QStringLiteral("pinged")) {
  Person p;
  p.id = id;
  p.name = QStringLiteral("Oleg");
  p.state = state;
  p.color = QColor(QStringLiteral("#7da8d9"));
  return p;
}

}  // namespace

class WaitingOnTest : public SafetyNetTest {
 protected:
  void SetUp() override {
    SafetyNetTest::SetUp();
    app_->tasks()->reset({task(QStringLiteral("T-1"), QStringLiteral("todo"))});
    app_->people()->reset({person(QStringLiteral("oleg"))});
  }
};

TEST_F(WaitingOnTest, ALinkNeedsATaskAndAPerson) {
  app_->setWaitingOn(QStringLiteral("nope"), QStringLiteral("oleg"));
  app_->setWaitingOn(QStringLiteral("T-1"), QStringLiteral("nobody"));
  EXPECT_TRUE(app_->waitingOnLinks().isEmpty());

  app_->setWaitingOn(QStringLiteral("T-1"), QStringLiteral("oleg"));
  const QVariantMap m = app_->waitingOnMap().value(QStringLiteral("T-1")).toMap();
  EXPECT_EQ(m.value(QStringLiteral("name")).toString(), QStringLiteral("Oleg"));
  EXPECT_EQ(m.value(QStringLiteral("days")).toInt(), 0);
}

TEST_F(WaitingOnTest, TheReminderIsOffByDefault) {
  app_->setAppSettingsJson(settingsJson({}));
  app_->setWaitingOn(QStringLiteral("T-1"), QStringLiteral("oleg"));
  const QSignalSpy spy(app_.get(), &AppController::safetyNotice);

  app_->runAutomationAt(QDateTime::currentDateTime().addDays(5));

  EXPECT_EQ(spy.count(), 0);
}

TEST_F(WaitingOnTest, OneGentleReminderPerLink) {
  app_->setAppSettingsJson(settingsJson({{"waitingOn", true}, {"waitingDays", 2}}));
  app_->setWaitingOn(QStringLiteral("T-1"), QStringLiteral("oleg"));
  const QDateTime since = app_->waitingOnLinks().at(0).since;
  const QSignalSpy spy(app_.get(), &AppController::safetyNotice);

  app_->runAutomationAt(since.addDays(1));
  EXPECT_EQ(spy.count(), 0);
  app_->runAutomationAt(since.addDays(2).addSecs(60));
  app_->runAutomationAt(since.addDays(3));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("waiting"));
  EXPECT_TRUE(spy.at(0).at(2).toString().contains(QStringLiteral("Oleg")));
  EXPECT_TRUE(spy.at(0).at(2).toString().contains(QStringLiteral("T-1 title")));
  EXPECT_EQ(spy.at(0).at(3).toStringList(), QStringList{QStringLiteral("T-1")});
  // It reminded; the link and the task are as they were.
  EXPECT_EQ(app_->waitingOnLinks().size(), 1);
  EXPECT_EQ(app_->tasks()->items().at(0).status, QStringLiteral("todo"));
}

TEST_F(WaitingOnTest, MarkingTheReplyEndsTheWait) {
  app_->setWaitingOn(QStringLiteral("T-1"), QStringLiteral("oleg"));
  app_->setPersonState(QStringLiteral("oleg"), QStringLiteral("replied"));
  EXPECT_TRUE(app_->waitingOnLinks().isEmpty());
}

TEST_F(WaitingOnTest, ClearingEndsTheWait) {
  app_->setWaitingOn(QStringLiteral("T-1"), QStringLiteral("oleg"));
  app_->clearWaitingOn(QStringLiteral("T-1"));
  EXPECT_TRUE(app_->waitingOnLinks().isEmpty());
}

TEST_F(WaitingOnTest, LinksSurviveARestart) {
  app_->setWaitingOn(QStringLiteral("T-1"), QStringLiteral("oleg"));
  const WaitingOn before = app_->waitingOnLinks().at(0);
  app_->flushSave();

  const AppController reopened;
  ASSERT_EQ(reopened.waitingOnLinks().size(), 1);
  EXPECT_EQ(reopened.waitingOnLinks().at(0).taskId, before.taskId);
  EXPECT_EQ(reopened.waitingOnLinks().at(0).personId, before.personId);
  EXPECT_EQ(reopened.waitingOnLinks().at(0).since.toSecsSinceEpoch(), before.since.toSecsSinceEpoch());
}

TEST(WaitingOnSerializer, RoundTripsThroughTheProfileObject) {
  Profile p;
  p.id = QStringLiteral("p");
  p.waitingOn.append({.taskId = QStringLiteral("T-1"),
                      .personId = QStringLiteral("oleg"),
                      .since = QDateTime(QDate(2026, 10, 1), QTime(10, 0)),
                      .remindedAt = QDateTime(QDate(2026, 10, 3), QTime(10, 0))});
  p.waitingOn.append({.taskId = QStringLiteral("T-2"),
                      .personId = QStringLiteral("anna"),
                      .since = QDateTime(QDate(2026, 10, 2), QTime(9, 0)),
                      .remindedAt = {}});
  const QJsonObject o = heap::state::profileToJson(p);
  ASSERT_TRUE(o.contains(QStringLiteral("waitingOn")));
  const Profile back = heap::state::profileFromJson(o);
  EXPECT_EQ(back.waitingOn, p.waitingOn);
  // Known, so not also carried in `extra`.
  EXPECT_FALSE(back.extra.contains(QStringLiteral("waitingOn")));
  // None → no key at all, like statusLog.
  EXPECT_FALSE(heap::state::profileToJson(Profile{}).contains(QStringLiteral("waitingOn")));
}

// ── APP-159 ──

TEST_F(SafetyNetTest, SeenBeforeFindsTheTaskThatMentionedTheError) {
  Task old = task(QStringLiteral("T-2"), QStringLiteral("done"));
  old.desc = QStringLiteral("Crashed with KeyError: 'session_token' after the cache flush.");
  app_->tasks()->reset({task(QStringLiteral("T-1"), QStringLiteral("todo")), old});
  const QString pasted = QStringLiteral("Traceback (most recent call last):\n  File \"auth.py\", line 88\nKeyError: 'session_token'");

  app_->setAppSettingsJson(settingsJson({}));
  EXPECT_TRUE(app_->seenBefore(pasted).isEmpty());  // off by default

  app_->setAppSettingsJson(settingsJson({{"seenBefore", true}}));
  const QVariantMap hit = app_->seenBefore(pasted, QStringLiteral("T-1"));
  EXPECT_EQ(hit.value(QStringLiteral("kind")).toString(), QStringLiteral("task"));
  EXPECT_EQ(hit.value(QStringLiteral("id")).toString(), QStringLiteral("T-2"));
  // The task being edited does not find itself.
  EXPECT_TRUE(app_->seenBefore(pasted, QStringLiteral("T-2")).isEmpty());
  // Prose is not looked up at all.
  EXPECT_TRUE(app_->seenBefore(QStringLiteral("session token cache flush")).isEmpty());
}

// ── APP-160 ──

TEST_F(SafetyNetTest, FocusModeHoldsNotificationsUntilAsked) {
  app_->setAppSettingsJson(settingsJson({{"immersion", true}}));
  app_->tasks()->reset({task(QStringLiteral("T-1"), QStringLiteral("prog"))});
  const QSignalSpy toasts(app_.get(), &AppController::toast);
  const QSignalSpy ended(app_.get(), &AppController::immersionEnded);

  app_->startImmersion(QStringLiteral("T-1"));
  ASSERT_TRUE(app_->immersion());
  emit app_->notification(QStringLiteral("Deadline in 1 hour"), QStringLiteral("T-1"), QStringLiteral("deadline"));
  emit app_->notification(QStringLiteral("Deadline in 1 hour"), QStringLiteral("T-1"), QStringLiteral("deadline"));
  emit app_->notification(QStringLiteral("Working on T-1"), QStringLiteral("Branch x"), QStringLiteral("git"));
  // A meeting still gets through.
  emit app_->notification(QStringLiteral("Starting now"), QStringLiteral("Sync"), QStringLiteral("meeting"));
  EXPECT_EQ(toasts.count(), 1);
  EXPECT_EQ(app_->immersionHeldCount(), 2);  // one of each

  app_->stopImmersion();
  ASSERT_EQ(ended.count(), 1);
  EXPECT_EQ(ended.at(0).at(0).toInt(), 2);
  // Ending it delivers nothing by itself …
  EXPECT_EQ(toasts.count(), 1);
  // … only when asked.
  EXPECT_EQ(app_->releaseImmersionHeld(), 2);
  EXPECT_EQ(toasts.count(), 3);
  EXPECT_EQ(app_->immersionHeldCount(), 0);
}

TEST_F(SafetyNetTest, FocusModeCanHoldMeetingsToo) {
  app_->setAppSettingsJson(settingsJson({{"immersion", true}, {"immersionPassMeetings", false}}));
  const QSignalSpy toasts(app_.get(), &AppController::toast);

  app_->startImmersion();
  emit app_->notification(QStringLiteral("Starting now"), QStringLiteral("Sync"), QStringLiteral("meeting"));
  EXPECT_EQ(toasts.count(), 0);
  app_->stopImmersion();
}

TEST_F(SafetyNetTest, FocusModeTimesTheCurrentTaskAndStopsOnlyItsOwnTimer) {
  app_->setAppSettingsJson(settingsJson({{"immersion", true}}));
  Task running = task(QStringLiteral("T-2"), QStringLiteral("prog"));
  running.timerStartedAt = QDateTime::currentDateTime().addSecs(-600);
  app_->tasks()->reset({task(QStringLiteral("T-1"), QStringLiteral("prog")), running});

  app_->startImmersion(QStringLiteral("T-1"));
  EXPECT_EQ(app_->immersionTaskId(), QStringLiteral("T-1"));
  EXPECT_TRUE(app_->tasks()->items().at(0).timerStartedAt.isValid());
  app_->stopImmersion();
  EXPECT_FALSE(app_->tasks()->items().at(0).timerStartedAt.isValid());

  // A timer the user had running is theirs: focus mode leaves it on. (Timing
  // T-1 above stopped it — one timer runs at a time — so it runs again here.)
  app_->tasks()->reset({task(QStringLiteral("T-1"), QStringLiteral("prog")), running});
  app_->startImmersion(QStringLiteral("T-2"));
  app_->stopImmersion();
  EXPECT_TRUE(app_->tasks()->items().at(1).timerStartedAt.isValid());
}

TEST_F(SafetyNetTest, FocusModeWithNoTaskIsOnlyQuiet) {
  app_->setAppSettingsJson(settingsJson({{"immersion", true}}));
  app_->tasks()->reset({task(QStringLiteral("T-1"), QStringLiteral("todo"))});
  app_->clearSelection();

  app_->startImmersion();
  EXPECT_TRUE(app_->immersion());
  EXPECT_TRUE(app_->immersionTaskId().isEmpty());
  EXPECT_FALSE(app_->tasks()->items().at(0).timerStartedAt.isValid());
  app_->stopImmersion();
  EXPECT_FALSE(app_->immersion());
}

// ── APP-170 ──

TEST_F(SafetyNetTest, StandupDraftFromTheWorkspace) {
  app_->events()->reset({});
  const Task blocked = task(QStringLiteral("T-9"), QStringLiteral("blocked"));
  app_->tasks()->reset({task(QStringLiteral("T-1"), QStringLiteral("prog")), blocked});
  const QString draft = app_->standupDraftFor(QDate::currentDate());
  EXPECT_TRUE(draft.contains(QStringLiteral("T-1 T-1 title")));
  EXPECT_TRUE(draft.contains(QStringLiteral("T-9 T-9 title")));
  // Today's in-progress task is under "Today", the blocked one under
  // "Blockers" — and the draft changed nothing.
  EXPECT_LT(draft.indexOf(QStringLiteral("T-1 T-1 title")), draft.indexOf(QStringLiteral("T-9 T-9 title")));
  EXPECT_EQ(app_->tasks()->items().at(1).status, QStringLiteral("blocked"));
}

// ── DG-002: the people dialog's linked tasks and meetings ──

TEST(AttendeeMatch, NamesAPersonByNameHandleOrFirstName) {
  using heap::people::attendeesName;
  EXPECT_TRUE(attendeesName(QStringLiteral("Олег Т."), QStringLiteral("Олег Т."), QStringLiteral("o.t")));
  EXPECT_TRUE(attendeesName(QStringLiteral("Андрей, Виктор"), QStringLiteral("Виктор С."), QString()));
  EXPECT_TRUE(attendeesName(QStringLiteral("team; @o.t"), QStringLiteral("Oleg T."), QStringLiteral("o.t")));
  EXPECT_FALSE(attendeesName(QStringLiteral("Олег К."), QStringLiteral("Олег Т."), QString()));
  EXPECT_FALSE(attendeesName(QStringLiteral("Команда продукта"), QStringLiteral("Олег Т."), QString()));
  EXPECT_FALSE(attendeesName(QString(), QStringLiteral("Олег Т."), QStringLiteral("o.t")));
}

TEST_F(WaitingOnTest, PersonLinksListWaitingTasksAndUpcomingMeetings) {
  app_->setWaitingOn(QStringLiteral("T-1"), QStringLiteral("oleg"));
  CalEvent mine;
  mine.id = QStringLiteral("ev-mine");
  mine.title = QStringLiteral("1:1");
  mine.start = 11.0;
  mine.end = 11.5;
  mine.attendees = QStringLiteral("Oleg");
  mine.date = app_->today().addDays(1);
  CalEvent other = mine;
  other.id = QStringLiteral("ev-other");
  other.attendees = QStringLiteral("Ann");
  CalEvent past = mine;
  past.id = QStringLiteral("ev-past");
  past.date = app_->today().addDays(-1);
  app_->events()->reset({mine, other, past});

  const QVariantMap links = app_->personLinks(QStringLiteral("oleg"));
  const QVariantList tasks = links.value(QStringLiteral("tasks")).toList();
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("T-1"));
  const QVariantList meetings = links.value(QStringLiteral("meetings")).toList();
  ASSERT_EQ(meetings.size(), 1);
  EXPECT_EQ(meetings.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("ev-mine"));
  EXPECT_TRUE(app_->personLinks(QStringLiteral("nobody")).isEmpty());
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir scratch;
  scratch.setAutoRemove(true);
  qputenv("XDG_CONFIG_HOME", scratch.path().toUtf8());
  qputenv("XDG_DATA_HOME", scratch.path().toUtf8());

  const QApplication qapp(argc, argv);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ── "Кому написать" in time (dogfood 2026-10-20) ──

namespace {

// A minute of its own each run: the sent keys outlive the process.
QDateTime pingMinute() {
  return QDateTime(QDate(2031, 3, 5), QTime(0, 0)).addSecs(60LL * QRandomGenerator::global()->bounded(10 * 365 * 24 * 60));
}

Person ask(const QString& id, const QDateTime& at, bool hasTime) {
  Person p;
  p.id = id;
  p.name = id.at(0).toUpper() + id.mid(1);
  p.question = QStringLiteral("about the release");
  p.state = QStringLiteral("todo");
  p.color = QColor(QStringLiteral("#7da8d9"));
  p.remindAt = at;
  p.remindHasTime = hasTime;
  return p;
}

}  // namespace

class PingTimeTest : public SafetyNetTest {
 protected:
  void SetUp() override {
    SafetyNetTest::SetUp();
    app_->setAppSettingsJson(settingsJson({}));
  }
};

TEST_F(PingTimeTest, ATimedAskNotifiesOnceWithItsButtons) {
  const QDateTime at = pingMinute();
  app_->people()->reset({ask(QStringLiteral("vera"), at, true)});
  QSignalSpy toasts(app_.get(), &AppController::reminderToast);
  app_->runAutomationAt(at.addSecs(-60));
  EXPECT_EQ(toasts.count(), 0) << "not before its minute";
  app_->runAutomationAt(at);
  app_->runAutomationAt(at.addSecs(60));
  ASSERT_EQ(toasts.count(), 1) << "once, not once per tick";
  EXPECT_TRUE(toasts.at(0).at(0).toString().startsWith(QStringLiteral("ping:")));
  EXPECT_TRUE(toasts.at(0).at(1).toString().contains(QStringLiteral("Vera")));
  EXPECT_EQ(toasts.at(0).at(2).toString(), QStringLiteral("ping"));
  // It said so; the ask is as it was.
  EXPECT_EQ(app_->people()->items().at(0).state, QStringLiteral("todo"));
}

TEST_F(PingTimeTest, ADayWithoutATimeSaysNothing) {
  const QDateTime at = pingMinute();
  app_->people()->reset({ask(QStringLiteral("vera"), QDateTime(at.date(), QTime(0, 0)), false)});
  QSignalSpy toasts(app_.get(), &AppController::reminderToast);
  app_->runAutomationAt(QDateTime(at.date(), QTime(12, 0)));
  EXPECT_EQ(toasts.count(), 0);
}

TEST_F(PingTimeTest, MissedWhileClosedComesOnceOnTheNextTick) {
  const QDateTime at = pingMinute();
  app_->people()->reset({ask(QStringLiteral("vera"), at, true)});
  QSignalSpy toasts(app_.get(), &AppController::reminderToast);
  app_->runAutomationAt(at.addSecs(3600 * 10));
  app_->runAutomationAt(at.addSecs(3600 * 10 + 60));
  EXPECT_EQ(toasts.count(), 1);
}

TEST_F(PingTimeTest, WrittenOrDeletedBeforeItsTimeSaysNothing) {
  const QDateTime at = pingMinute();
  app_->people()->reset({ask(QStringLiteral("vera"), at, true), ask(QStringLiteral("gleb"), at, true)});
  app_->setPersonState(QStringLiteral("vera"), QStringLiteral("pinged"));
  app_->deletePerson(QStringLiteral("gleb"));
  QSignalSpy toasts(app_.get(), &AppController::reminderToast);
  app_->runAutomationAt(at);
  EXPECT_EQ(toasts.count(), 0);
  // "Написал" spends the time: "write again" starts without one.
  EXPECT_FALSE(app_->people()->items().at(0).remindAt.isValid());
}

TEST_F(PingTimeTest, ANewTimeAfterItFiredFiresAgain) {
  const QDateTime at = pingMinute();
  app_->people()->reset({ask(QStringLiteral("vera"), at, true)});
  QSignalSpy toasts(app_.get(), &AppController::reminderToast);
  app_->runAutomationAt(at);
  ASSERT_EQ(toasts.count(), 1);
  QVariantMap d = app_->personById(QStringLiteral("vera"));
  d[QStringLiteral("remindAt")] = at.addSecs(3600);
  d[QStringLiteral("remindHasTime")] = true;
  ASSERT_TRUE(app_->savePerson(d));
  app_->runAutomationAt(at.addSecs(3600));
  EXPECT_EQ(toasts.count(), 2);
}

TEST_F(PingTimeTest, ClearingTheTimeInTheEditorStopsIt) {
  const QDateTime at = pingMinute();
  app_->people()->reset({ask(QStringLiteral("vera"), at, true)});
  QVariantMap d = app_->personById(QStringLiteral("vera"));
  d[QStringLiteral("remindAt")] = QVariant();
  ASSERT_TRUE(app_->savePerson(d));
  EXPECT_FALSE(app_->people()->items().at(0).remindAt.isValid());
  // A draft that does not carry the key keeps the time.
  app_->people()->reset({ask(QStringLiteral("vera"), at, true)});
  QVariantMap keep = app_->personById(QStringLiteral("vera"));
  keep.remove(QStringLiteral("remindAt"));
  keep[QStringLiteral("question")] = QStringLiteral("other");
  ASSERT_TRUE(app_->savePerson(keep));
  EXPECT_EQ(app_->people()->items().at(0).remindAt, at);
}

TEST_F(PingTimeTest, TheNotificationsWroteMovesTheAskToTheWait) {
  const QDateTime at = pingMinute();
  app_->people()->reset({ask(QStringLiteral("vera"), at, true)});
  QSignalSpy toasts(app_.get(), &AppController::reminderToast);
  app_->runAutomationAt(at);
  ASSERT_EQ(toasts.count(), 1);
  app_->reminderAction(toasts.at(0).at(0).toString(), QStringLiteral("wrote"));
  EXPECT_EQ(app_->people()->items().at(0).state, QStringLiteral("pinged"));
}

TEST_F(PingTimeTest, TheNotificationsOpenOpensThePerson) {
  const QDateTime at = pingMinute();
  app_->people()->reset({ask(QStringLiteral("vera"), at, true)});
  QSignalSpy toasts(app_.get(), &AppController::reminderToast);
  QSignalSpy open(app_.get(), &AppController::pingOpenRequested);
  app_->runAutomationAt(at);
  ASSERT_EQ(toasts.count(), 1);
  app_->reminderAction(toasts.at(0).at(0).toString(), QStringLiteral("open"));
  ASSERT_EQ(open.count(), 1);
  EXPECT_EQ(open.at(0).at(0).toString(), QStringLiteral("vera"));
}

TEST_F(PingTimeTest, AWaitGetsOneNudgeWhenTheSwitchIsOn) {
  const QDateTime since = pingMinute();
  Person p = ask(QStringLiteral("vera"), {}, false);
  p.state = QStringLiteral("pinged");
  p.stateAt = since;
  app_->people()->reset({p});
  const QSignalSpy spy(app_.get(), &AppController::safetyNotice);
  app_->runAutomationAt(since.addDays(3));
  EXPECT_EQ(spy.count(), 0) << "off by default";

  app_->setAppSettingsJson(settingsJson({{"waitingOn", true}, {"waitingDays", 2}}));
  app_->runAutomationAt(since.addDays(1));
  EXPECT_EQ(spy.count(), 0);
  app_->runAutomationAt(since.addDays(2).addSecs(60));
  app_->runAutomationAt(since.addDays(2).addSecs(120));
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("waiting"));
  EXPECT_TRUE(spy.at(0).at(2).toString().contains(QStringLiteral("Vera")));
  EXPECT_EQ(app_->people()->items().at(0).state, QStringLiteral("pinged")) << "it reminds, nothing more";
}

// The sidebar's rows: the asks first, then the waits, oldest first; "Написал"
// keeps the person on it as a wait, "Ответил" takes them off.
TEST_F(PingTimeTest, RowsAreAsksThenWaits) {
  const QDateTime now(QDate(2026, 10, 20), QTime(12, 0));
  Person a = ask(QStringLiteral("ada"), QDateTime(now.date(), QTime(15, 0)), true);
  Person b = ask(QStringLiteral("bob"), {}, false);
  b.state = QStringLiteral("pinged");
  b.stateAt = now.addSecs(-2 * 3600);
  Person c = ask(QStringLiteral("cat"), {}, false);
  c.state = QStringLiteral("pinged");
  c.stateAt = now.addDays(-1);
  Person d = ask(QStringLiteral("dan"), {}, false);
  d.state = QStringLiteral("replied");
  app_->people()->reset({b, a, c, d});
  const QVariantList rows = app_->pingRowsAt(now);
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("ada"));
  EXPECT_FALSE(rows.at(0).toMap().value(QStringLiteral("waiting")).toBool());
  EXPECT_FALSE(rows.at(0).toMap().value(QStringLiteral("when")).toString().isEmpty());
  EXPECT_FALSE(rows.at(0).toMap().value(QStringLiteral("overdue")).toBool());
  EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cat"));
  EXPECT_EQ(rows.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("bob"));
  EXPECT_TRUE(rows.at(2).toMap().value(QStringLiteral("waiting")).toBool());
  EXPECT_TRUE(rows.at(2).toMap().value(QStringLiteral("ago")).toString().startsWith(QStringLiteral("2 ")));
  EXPECT_TRUE(app_->pingRowsAt(now.addSecs(4 * 3600)).at(0).toMap().value(QStringLiteral("overdue")).toBool());
  // Today's short list stays the asks alone.
  EXPECT_EQ(app_->openPings().size(), 1);
}

// The field read against a fixed clock, through the real parser.
TEST_F(PingTimeTest, TheWhenFieldIsReadAgainstNow) {
  const QDateTime now(QDate(2026, 10, 20), QTime(16, 0));
  const QVariantMap rolled = app_->parsePingWhenAt(QStringLiteral("15:00"), now);
  ASSERT_TRUE(rolled.value(QStringLiteral("ok")).toBool());
  EXPECT_EQ(rolled.value(QStringLiteral("at")).toDateTime(), QDateTime(QDate(2026, 10, 21), QTime(15, 0)));
  EXPECT_TRUE(rolled.value(QStringLiteral("rolled")).toBool());
  const QVariantMap ahead = app_->parsePingWhenAt(QStringLiteral("17:30"), now);
  EXPECT_EQ(ahead.value(QStringLiteral("at")).toDateTime(), QDateTime(QDate(2026, 10, 20), QTime(17, 30)));
  const QVariantMap tomorrow = app_->parsePingWhenAt(QStringLiteral("завтра 15:00"), now);
  EXPECT_EQ(tomorrow.value(QStringLiteral("at")).toDateTime(), QDateTime(QDate(2026, 10, 21), QTime(15, 0)));
  EXPECT_TRUE(tomorrow.value(QStringLiteral("hasTime")).toBool());
  const QVariantMap dayOnly = app_->parsePingWhenAt(QStringLiteral("завтра"), now);
  ASSERT_TRUE(dayOnly.value(QStringLiteral("ok")).toBool());
  EXPECT_FALSE(dayOnly.value(QStringLiteral("hasTime")).toBool());
  EXPECT_EQ(dayOnly.value(QStringLiteral("at")).toDateTime().date(), QDate(2026, 10, 21));
  const QVariantMap in2h = app_->parsePingWhenAt(QStringLiteral("через 2 часа"), now);
  ASSERT_TRUE(in2h.value(QStringLiteral("ok")).toBool());
  EXPECT_EQ(in2h.value(QStringLiteral("at")).toDateTime(), now.addSecs(2 * 3600));
  EXPECT_EQ(app_->parsePingWhenAt(QStringLiteral("вчера 15:00"), now).value(QStringLiteral("error")).toString(), QStringLiteral("past"));
  EXPECT_EQ(app_->parsePingWhenAt(QStringLiteral("сегодня 9:00"), now).value(QStringLiteral("error")).toString(), QStringLiteral("past"));
  EXPECT_EQ(app_->parsePingWhenAt(QStringLiteral("абвгд"), now).value(QStringLiteral("error")).toString(), QStringLiteral("unparsed"));
  EXPECT_TRUE(app_->parsePingWhenAt(QString(), now).value(QStringLiteral("empty")).toBool());
}

TEST(PingSerializer, TheTimeRoundTripsAndOldPeopleReadAsBefore) {
  Person p = ask(QStringLiteral("vera"), QDateTime(QDate(2026, 10, 21), QTime(15, 0)), true);
  p.stateAt = QDateTime(QDate(2026, 10, 20), QTime(9, 0));
  const QJsonArray a = heap::state::peopleToJson({p});
  EXPECT_TRUE(a.at(0).toObject().contains(QStringLiteral("remindAt")));
  const QVector<Person> back = heap::state::peopleFromJson(a);
  ASSERT_EQ(back.size(), 1);
  EXPECT_EQ(back.at(0).remindAt, p.remindAt);
  EXPECT_TRUE(back.at(0).remindHasTime);
  EXPECT_FALSE(back.at(0).extra.contains(QStringLiteral("remindAt"))) << "known, not carried twice";
  // A person saved by 0.9.5 ("Написал" = pinged): no time, still waiting.
  const QJsonObject old{
      {"id", "oleg"}, {"name", "Oleg"}, {"state", "pinged"}, {"color", "#7da8d9"}, {"stateAt", "2026-10-19T10:00:00.000"}};
  const QVector<Person> fromOld = heap::state::peopleFromJson(QJsonArray{old});
  EXPECT_EQ(fromOld.at(0).state, QStringLiteral("pinged"));
  EXPECT_FALSE(fromOld.at(0).remindAt.isValid());
  EXPECT_FALSE(heap::state::peopleToJson(fromOld).at(0).toObject().contains(QStringLiteral("remindAt")));
}
