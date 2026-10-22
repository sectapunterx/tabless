// Reminders for calendar events.
//
// heap had a "minutes before a meeting" setting and exactly one meeting it
// applied to: the standup, at a fixed time from settings. Every real event in
// the calendar went unannounced, which made the setting read like a promise
// the app did not keep.
//
// The window rule is pure and takes `now` as an argument (src/cal/Reminders.h).
// It has to be: a test can only place an event a few minutes out, saveEvent()
// snaps it to the calendar's grid, and whether the snapped slot lands before or
// after the current instant depends on what time the suite happens to run. The
// first version of this file did exactly that and passed locally while failing
// on CI, for no reason connected to the feature.
//
// The AppController cases below cover what is genuinely its own: the
// once-per-day sentinel, the settings switch, and the fact that quiet hours do
// not gate a meeting. They set a lead wide enough that the grid can snap the
// event wherever it likes.

#include "AppController.h"
#include "Models.h"

#include "cal/Reminders.h"
#include "capture/SelfReminder.h"
#include "notify/NotificationCenter.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using heap::cal::dueMeetingReminders;

namespace {

const QDate kDay(2026, 9, 21);
// Mid-morning, so nothing in these cases is near a day boundary.
const QDateTime kNow(kDay, QTime(10, 0));

CalEvent at(const QString& id, double startHour, const QString& type = QStringLiteral("sync")) {
  CalEvent e;
  e.id = id;
  e.title = QStringLiteral("standup with the team");
  e.type = type;
  e.date = kDay;
  e.start = startHour;
  e.end = startHour + 0.5;
  return e;
}

}  // namespace

// ── The window rule ──

TEST(MeetingWindow, AnEventInsideTheLeadWindowIsDue) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 10.0 + 5.0 / 60.0)}, kNow, 10);

  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.at(0).eventId, QStringLiteral("a"));
  EXPECT_EQ(due.at(0).minutesLeft, 5);
}

TEST(MeetingWindow, AnEventBeyondTheLeadWindowIsNotDueYet) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 12.0)}, kNow, 10);

  EXPECT_TRUE(due.isEmpty());
}

// An event that has already started is not something to be reminded about.
TEST(MeetingWindow, AnEventThatHasPassedIsNotDue) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 9.5)}, kNow, 10);

  EXPECT_TRUE(due.isEmpty());
}

// The far edge is inclusive: a lead of 10 has to announce a meeting exactly
// ten minutes out, or the setting is off by one for its own value.
TEST(MeetingWindow, TheEdgeOfTheWindowIsIncluded) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 10.0 + 10.0 / 60.0)}, kNow, 10);

  EXPECT_EQ(due.size(), 1);
}

// A lead of 0 means "when it starts", not "never" — the slider's minimum is 0
// and it has to mean something.
TEST(MeetingWindow, AZeroLeadStillAnnouncesAtTheStart) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 10.0)}, kNow, 0);

  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.at(0).minutesLeft, 0);
}

TEST(MeetingWindow, AZeroLeadAnnouncesNothingEarlier) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 10.0 + 5.0 / 60.0)}, kNow, 0);

  EXPECT_TRUE(due.isEmpty());
}

// A focus block is the user's own time — they put it there on purpose and are
// already in it. Reminding them about it is noise.
TEST(MeetingWindow, AFocusBlockIsNotAMeeting) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 10.0 + 5.0 / 60.0, QStringLiteral("focus"))}, kNow, 10);

  EXPECT_TRUE(due.isEmpty());
}

// A2-17: a lead the user picked for a focus block by hand is kept.
TEST(MeetingWindow, AFocusBlockWithItsOwnLeadReminds) {
  CalEvent e = at(QStringLiteral("a"), 10.0 + 5.0 / 60.0, QStringLiteral("focus"));
  e.reminderMinutes = 30;
  const auto due = dueMeetingReminders({e}, kNow, 10);

  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.first().eventId, QStringLiteral("a"));
}

TEST(MeetingWindow, TwoEventsInTheWindowAreBothDue) {
  const QVector<CalEvent> events{at(QStringLiteral("a"), 10.0 + 3.0 / 60.0), at(QStringLiteral("b"), 10.0 + 6.0 / 60.0)};
  const auto due = dueMeetingReminders(events, kNow, 10);

  EXPECT_EQ(due.size(), 2);
}

// An event on another day is not in today's window however close its hour is.
TEST(MeetingWindow, AnEventOnAnotherDayIsIgnored) {
  CalEvent e = at(QStringLiteral("a"), 10.0 + 5.0 / 60.0);
  e.date = kDay.addDays(1);

  EXPECT_TRUE(dueMeetingReminders({e}, kNow, 10).isEmpty());
}

// A 00:30 meeting is 40 minutes away from 23:50, whatever the date says: the
// window used to stop at midnight, so a meeting just after it was never
// announced (audit TIME-30).
TEST(MeetingWindow, TheWindowReachesAcrossMidnight) {
  CalEvent e = at(QStringLiteral("a"), 0.5);
  e.date = kDay.addDays(1);

  const auto due = dueMeetingReminders({e}, QDateTime(kDay, QTime(23, 50)), 60);
  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.first().minutesLeft, 40);
}

// "Starting now" only once it has: 30 seconds before the start is "in 1 min".
TEST(MeetingWindow, StartingNowIsNeverSaidEarly) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 10.0)}, QDateTime(kDay, QTime(9, 59, 30)), 5);
  ASSERT_EQ(due.size(), 1);
  EXPECT_EQ(due.first().minutesLeft, 1);
}

// A reminder whose minute passed (quiet hours, a busy tick) is still due until
// shortly after the start, and a sent key is not repeated.
TEST(MeetingWindow, ALateReminderIsStillDueUntilTheGraceEnds) {
  const CalEvent e = at(QStringLiteral("a"), 9.0);
  EXPECT_EQ(dueMeetingReminders({e}, QDateTime(kDay, QTime(9, 0, 40)), 5).size(), 1);
  EXPECT_TRUE(dueMeetingReminders({e}, QDateTime(kDay, QTime(9, 6)), 5).isEmpty());
  const auto due = dueMeetingReminders({e}, QDateTime(kDay, QTime(8, 56)), 5);
  ASSERT_EQ(due.size(), 1);
  EXPECT_TRUE(dueMeetingReminders({e}, QDateTime(kDay, QTime(8, 57)), 5, {due.first().key}).isEmpty());
}

// The key names the occurrence's start, so a meeting moved later the same day
// is reminded again (audit TIME-21).
TEST(MeetingWindow, AMovedMeetingGetsANewKey) {
  CalEvent e = at(QStringLiteral("a"), 9.0);
  const auto first = dueMeetingReminders({e}, QDateTime(kDay, QTime(8, 56)), 5);
  e.start = 15.0;
  e.end = 15.5;
  const auto second = dueMeetingReminders({e}, QDateTime(kDay, QTime(14, 56)), 5, {first.value(0).key});
  EXPECT_EQ(second.size(), 1);
}

TEST(MeetingWindow, AnEventsOwnLeadWinsAndOffSilencesIt) {
  CalEvent e = at(QStringLiteral("a"), 10.5);
  e.reminderMinutes = 30;
  EXPECT_EQ(dueMeetingReminders({e}, kNow, 5).size(), 1);
  e.reminderMinutes = CalEvent::kReminderOff;
  EXPECT_TRUE(dueMeetingReminders({e}, QDateTime(kDay, QTime(10, 29)), 5).isEmpty());
}

// ── Deadlines (audit PLAT-19) ──

TEST(DeadlineWindow, HalfAnHourOverdueIsOverdueNotDueSoon) {
  const auto call = heap::cal::deadlineReminder(QStringLiteral("T-1"), QDateTime(kDay, QTime(9, 30)), kNow, 24);
  EXPECT_TRUE(call.due);
  EXPECT_TRUE(call.overdue);
  EXPECT_EQ(call.hours, 0);
}

TEST(DeadlineWindow, ThreeHoursOverdueIsStillSaid) {
  const auto call = heap::cal::deadlineReminder(QStringLiteral("T-1"), QDateTime(kDay, QTime(7, 0)), kNow, 24);
  EXPECT_TRUE(call.due);
  EXPECT_TRUE(call.overdue);
  EXPECT_EQ(call.hours, 3);
}

TEST(DeadlineWindow, HoursLeftRoundUp) {
  const auto call = heap::cal::deadlineReminder(QStringLiteral("T-1"), QDateTime(kDay, QTime(11, 30)), kNow, 24);
  EXPECT_TRUE(call.due);
  EXPECT_FALSE(call.overdue);
  EXPECT_EQ(call.hours, 2);
}

TEST(DeadlineWindow, LongOverdueWorkIsNotAFlood) {
  EXPECT_FALSE(heap::cal::deadlineReminder(QStringLiteral("T-1"), kNow.addDays(-3), kNow, 24).due);
}

TEST(QuietWindow, WrapsMidnight) {
  EXPECT_TRUE(heap::cal::inQuietWindow(QTime(19, 0), QTime(9, 0), QTime(23, 0)));
  EXPECT_TRUE(heap::cal::inQuietWindow(QTime(19, 0), QTime(9, 0), QTime(8, 59)));
  EXPECT_FALSE(heap::cal::inQuietWindow(QTime(19, 0), QTime(9, 0), QTime(9, 0)));
}

// SHELL-9 (audit 2026-09-30): a stored "9:30" parsed as invalid under a strict
// "HH:mm" and the standup reminder stopped without a word.
TEST(ClockTime, AcceptsOneOrTwoDigitHoursAndRejectsTheRest) {
  EXPECT_EQ(heap::cal::clockTime(QStringLiteral("09:30")), QTime(9, 30));
  EXPECT_EQ(heap::cal::clockTime(QStringLiteral("9:30")), QTime(9, 30));
  EXPECT_EQ(heap::cal::clockTime(QStringLiteral(" 7:05 ")), QTime(7, 5));
  EXPECT_EQ(heap::cal::clockTime(QStringLiteral("23:59")), QTime(23, 59));
  EXPECT_FALSE(heap::cal::clockTime(QStringLiteral("25:00")).isValid());
  EXPECT_FALSE(heap::cal::clockTime(QStringLiteral("abc")).isValid());
  EXPECT_FALSE(heap::cal::clockTime(QString()).isValid());
  EXPECT_FALSE(heap::cal::clockTime(QStringLiteral("9:3")).isValid());
}

// An all-day event has no start to count down to. Announcing "your holiday
// begins in 0 minutes" at midnight is noise, not a reminder.
TEST(MeetingWindow, AnAllDayEventIsNotAnnounced) {
  CalEvent e = at(QStringLiteral("a"), 10.0 + 5.0 / 60.0);
  e.allDay = true;

  EXPECT_TRUE(dueMeetingReminders({e}, kNow, 10).isEmpty());
}

TEST(MeetingWindow, AnInvalidClockAnnouncesNothing) {
  EXPECT_TRUE(dueMeetingReminders({at(QStringLiteral("a"), 10.0)}, QDateTime(), 10).isEmpty());
}

TEST(MeetingWindow, ANegativeLeadIsTreatedAsZero) {
  const auto due = dueMeetingReminders({at(QStringLiteral("a"), 10.0)}, kNow, -30);

  EXPECT_EQ(due.size(), 1);
}

// ── What AppController adds on top ──

namespace {

// Only meeting reminders on, so nothing else in the tick can speak. The lead is
// a whole day: the calendar snaps a saved event to its grid, so a test that
// wants one to be "inside the window" cannot also pin the exact minute.
constexpr int kSnapMinutes = 15;

QString settingsWithMeetingLead(int leadMinutes, bool enabled = true) {
  QJsonObject notif;
  notif["meetingReminders"] = enabled;
  notif["meetingLead"] = leadMinutes;
  notif["deadlineReminders"] = false;
  notif["standupReminder"] = false;
  notif["blockedDailyDigest"] = false;
  notif["quietHours"] = false;
  // The snap grid is pinned here too, so the fixture knows where saveEvent()
  // will put an event rather than having to ask for a private accessor.
  QJsonObject cal;
  cal["snapMinutes"] = kSnapMinutes;
  QJsonObject root;
  root["notifications"] = notif;
  root["calendar"] = cal;
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

constexpr int kWholeDay = 24 * 60;

}  // namespace

class MeetingReminderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    app_ = std::make_unique<AppController>();
    app_->events()->reset({});
    app_->setAppSettingsJson(settingsWithMeetingLead(kWholeDay));
  }

  void TearDown() override {
    app_.reset();
  }

  // An event on the next slot of the calendar's grid. saveEvent() snaps what it
  // is given, so handing it an already-snapped time is the only way to know
  // where the event lands; with a whole-day lead it is then inside the window
  // whatever the hour.
  //
  // Returns false in the last slot of the day, where there is no later slot to
  // use; the case then has nothing to assert and says so.
  bool addEventOnTheNextSlot(const QString& title = QStringLiteral("standup with the team"), const QString& type = QStringLiteral("sync")) {
    const QDateTime now = QDateTime::currentDateTime();
    const int step = kSnapMinutes;
    const int minutes = (now.time().hour() * 60) + now.time().minute();
    const int slot = ((minutes / step) + 1) * step;
    if(slot >= 24 * 60) {
      return false;
    }
    const double startHour = slot / 60.0;
    QVariantMap draft = app_->newEventDraft(startHour, now.date());
    draft["title"] = title;
    draft["type"] = type;
    draft["date"] = now.date();
    draft["start"] = startHour;
    draft["end"] = startHour + 0.5;
    app_->saveEvent(draft);
    return true;
  }

  void tick() {
    QMetaObject::invokeMethod(app_.get(), "runAutomation", Qt::DirectConnection);
  }

  std::unique_ptr<AppController> app_;
};

TEST_F(MeetingReminderTest, AMeetingLaterTodayIsAnnounced) {
  if(!addEventOnTheNextSlot()) {
    GTEST_SKIP() << "no calendar slot left today";
  }
  QSignalSpy spy(app_.get(), &AppController::notification);

  tick();

  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(2).toString(), QStringLiteral("meeting"));
  // The title names the meeting, the body says when (R3-025).
  EXPECT_TRUE(spy.at(0).at(0).toString().contains(QStringLiteral("standup with the team")));
}

// The tick runs once a minute. Without a sentinel a meeting half an hour out
// would be announced thirty times.
TEST_F(MeetingReminderTest, AnEventIsAnnouncedOnceNotOncePerTick) {
  if(!addEventOnTheNextSlot()) {
    GTEST_SKIP() << "no calendar slot left today";
  }
  QSignalSpy spy(app_.get(), &AppController::notification);

  tick();
  tick();
  tick();

  EXPECT_EQ(spy.count(), 1);
}

TEST_F(MeetingReminderTest, TheSettingTurnsThemOff) {
  app_->setAppSettingsJson(settingsWithMeetingLead(kWholeDay, /*enabled=*/false));
  if(!addEventOnTheNextSlot()) {
    GTEST_SKIP() << "no calendar slot left today";
  }
  QSignalSpy spy(app_.get(), &AppController::notification);

  tick();

  EXPECT_EQ(spy.count(), 0);
}

// Quiet hours silence ambient noise — digests, deadline nags. A meeting is an
// appointment the user agreed to attend, and the default window (19:00–09:00)
// covers every early call, so gating on it would silently swallow exactly the
// reminders that matter most. The standup reminder already behaves this way.
TEST_F(MeetingReminderTest, QuietHoursDoNotSwallowAMeeting) {
  QJsonObject notif;
  notif["meetingReminders"] = true;
  notif["meetingLead"] = kWholeDay;
  notif["deadlineReminders"] = false;
  notif["standupReminder"] = false;
  notif["blockedDailyDigest"] = false;
  notif["quietHours"] = true;
  notif["quietFrom"] = QStringLiteral("00:00");
  notif["quietTo"] = QStringLiteral("23:59");
  QJsonObject cal;
  cal["snapMinutes"] = kSnapMinutes;
  QJsonObject root;
  root["notifications"] = notif;
  root["calendar"] = cal;
  app_->setAppSettingsJson(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));

  if(!addEventOnTheNextSlot()) {
    GTEST_SKIP() << "no calendar slot left today";
  }
  QSignalSpy spy(app_.get(), &AppController::notification);

  tick();

  EXPECT_GE(spy.count(), 1);
}

TEST_F(MeetingReminderTest, TwoMeetingsGetTwoReminders) {
  if(!addEventOnTheNextSlot(QStringLiteral("first")) || !addEventOnTheNextSlot(QStringLiteral("second"))) {
    GTEST_SKIP() << "no calendar slot left today";
  }
  QSignalSpy spy(app_.get(), &AppController::notification);

  tick();

  EXPECT_EQ(spy.count(), 2);
}

// ── The start of a planned task block (APP-256) ──

TEST(TaskBlockWindow, DueFromTheLeadToAFewMinutesAfterTheStart) {
  const QDateTime at(kDay, QTime(14, 0));
  EXPECT_FALSE(heap::cal::taskBlockReminder(QStringLiteral("T-1"), at, at.addSecs(-6 * 60), 5).due);
  const auto early = heap::cal::taskBlockReminder(QStringLiteral("T-1"), at, at.addSecs(-5 * 60), 5);
  EXPECT_TRUE(early.due);
  EXPECT_EQ(early.minutesLeft, 5);
  const auto start = heap::cal::taskBlockReminder(QStringLiteral("T-1"), at, at, 0);
  EXPECT_TRUE(start.due);
  EXPECT_EQ(start.minutesLeft, 0);
  EXPECT_FALSE(heap::cal::taskBlockReminder(QStringLiteral("T-1"), at, at.addSecs(-60), 0).due) << "a zero lead says nothing early";
  EXPECT_FALSE(heap::cal::taskBlockReminder(QStringLiteral("T-1"), at, at.addSecs(6 * 60), 0).due);
}

TEST(TaskBlockWindow, AMovedTaskGetsANewKey) {
  const QDateTime at(kDay, QTime(14, 0));
  EXPECT_NE(heap::cal::taskBlockReminder(QStringLiteral("T-1"), at, at, 0).key,
            heap::cal::taskBlockReminder(QStringLiteral("T-1"), at.addSecs(3600), at.addSecs(3600), 0).key);
}

// ── "Remind me" on a task (APP-319) ──

TEST(TaskRemind, AMomentWinsOverALeadAndALeadNeedsATime) {
  const QDateTime plan(kDay, QTime(15, 0));
  const QDateTime at(kDay, QTime(9, 30));
  EXPECT_EQ(heap::cal::taskReminderFireAt(at, 10, plan, true, {}, false), at);
  EXPECT_EQ(heap::cal::taskReminderFireAt({}, 10, plan, true, {}, false), plan.addSecs(-600));
  EXPECT_FALSE(heap::cal::taskReminderFireAt({}, 10, plan, false, {}, false).isValid()) << "a day with no time";
  const QDateTime due(kDay, QTime(18, 0));
  EXPECT_EQ(heap::cal::taskReminderFireAt({}, 0, {}, false, due, true), due) << "the deadline, when there is no plan";
  EXPECT_FALSE(heap::cal::taskReminderFireAt({}, -1, plan, true, {}, false).isValid());
}

TEST(TaskRemind, DueFromItsMinuteForHalfADayAndLateAfterTheGrace) {
  const QDateTime at(kDay, QTime(9, 30));
  EXPECT_FALSE(heap::cal::taskReminder(QStringLiteral("T-1"), at, at.addSecs(-60)).due);
  const auto now = heap::cal::taskReminder(QStringLiteral("T-1"), at, at);
  EXPECT_TRUE(now.due);
  EXPECT_FALSE(now.late);
  const auto late = heap::cal::taskReminder(QStringLiteral("T-1"), at, at.addSecs(3 * 3600));
  EXPECT_TRUE(late.due);
  EXPECT_TRUE(late.late);
  EXPECT_FALSE(heap::cal::taskReminder(QStringLiteral("T-1"), at, at.addSecs(13 * 3600)).due);
  EXPECT_NE(now.key, heap::cal::taskReminder(QStringLiteral("T-1"), at.addSecs(60), at.addSecs(60)).key) << "a new time is a new reminder";
}

TEST(TaskRemind, LeadsAreReadInBothLanguages) {
  EXPECT_EQ(heap::cal::reminderLeadMinutes(QStringLiteral("за 15 мин")), 15);
  EXPECT_EQ(heap::cal::reminderLeadMinutes(QStringLiteral("15m")), 15);
  EXPECT_EQ(heap::cal::reminderLeadMinutes(QStringLiteral("1 ч")), 60);
  EXPECT_EQ(heap::cal::reminderLeadMinutes(QStringLiteral("10 min before")), 10);
  EXPECT_EQ(heap::cal::reminderLeadMinutes(QStringLiteral("0")), 0);
  EXPECT_EQ(heap::cal::reminderLeadMinutes(QStringLiteral("9")), -1) << "a bare number is the hour";
  EXPECT_EQ(heap::cal::reminderLeadMinutes(QStringLiteral("15:00")), -1);
}

namespace {

QString blockSettings(bool enabled) {
  QJsonObject notif;
  notif["meetingReminders"] = false;
  notif["deadlineReminders"] = false;
  notif["standupReminder"] = false;
  notif["blockedDailyDigest"] = false;
  notif["quietHours"] = false;
  notif["taskBlockReminders"] = enabled;
  notif["taskBlockLead"] = 5;
  QJsonObject root;
  root["notifications"] = notif;
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

Task plannedAt(const QString& id, const QDateTime& at) {
  Task t;
  t.id = id;
  t.title = QStringLiteral("Review the cache");
  t.status = QStringLiteral("todo");
  t.scheduledAt = at;
  t.scheduledHasTime = true;
  return t;
}

}  // namespace

// Off by default: nothing is said unless it is switched on.
TEST(TaskBlockReminder, OffByDefault) {
  AppController app;
  app.events()->reset({});
  // A day far from anything the other cases use, so the sent keys never meet.
  const QDateTime at(QDate(2031, 3, 4), QTime(14, 0));
  app.tasks()->reset({plannedAt(QStringLiteral("BLK-OFF"), at)});
  QSignalSpy toasts(&app, &AppController::reminderToast);
  QSignalSpy plain(&app, &AppController::toast);
  app.runAutomationAt(at);
  EXPECT_EQ(toasts.count(), 0);
  for(const QList<QVariant>& args : plain) {
    EXPECT_FALSE(args.at(0).toString().contains(QStringLiteral("Review the cache")));
  }
}

TEST(TaskBlockReminder, SaidOnceWithItsButtonsAndNoTimer) {
  AppController app;
  app.events()->reset({});
  app.setAppSettingsJson(blockSettings(true));
  // The sent keys outlive the process (reminders.json in the test profile):
  // a minute of its own each run, so a second run is not "already said" —
  // drawn from ten years of minutes: one taken from the clock repeated within
  // the same minute and failed every rerun.
  const QDateTime at = QDateTime(QDate(2031, 3, 5), QTime(0, 0)).addSecs(60LL * QRandomGenerator::global()->bounded(10 * 365 * 24 * 60));
  app.tasks()->reset({plannedAt(QStringLiteral("BLK-ON"), at)});
  QSignalSpy toasts(&app, &AppController::reminderToast);
  app.runAutomationAt(at.addSecs(-4 * 60));
  app.runAutomationAt(at.addSecs(-3 * 60));
  app.runAutomationAt(at);
  ASSERT_EQ(toasts.count(), 1) << "once, not once per tick";
  EXPECT_TRUE(toasts.at(0).at(0).toString().startsWith(QStringLiteral("taskBlock:")));
  EXPECT_TRUE(toasts.at(0).at(1).toString().contains(QStringLiteral("Review the cache")));
  const Task t = app.tasks()->items().first();
  EXPECT_FALSE(t.timerStartedAt.isValid()) << "a reminder starts no timer";
  EXPECT_EQ(t.status, QStringLiteral("todo")) << "and moves nothing";
}

namespace {

Task remindAt(const QString& id, const QDateTime& at) {
  Task t;
  t.id = id;
  t.title = QStringLiteral("Call the bank");
  t.status = QStringLiteral("todo");
  t.local.remindAt = at;
  return t;
}

QDateTime freshMinute() {
  // The sent keys outlive the process (reminders.json): a minute of its own
  // each run, so a rerun is not "already said".
  return QDateTime(QDate(2032, 1, 6), QTime(0, 0)).addSecs(60LL * QRandomGenerator::global()->bounded(10 * 365 * 24 * 60));
}

}  // namespace

// Set on a task, said once at its minute, with Open and Snooze; nothing for a
// done task. No setting to switch on: setting the reminder is the opt-in.
TEST(TaskRemindReminder, SaidOnceAtItsMinute) {
  AppController app;
  app.events()->reset({});
  app.setAppSettingsJson(blockSettings(false));
  const QDateTime at = freshMinute();
  Task done = remindAt(QStringLiteral("REM-DONE"), at);
  done.status = QStringLiteral("done");
  app.tasks()->reset({remindAt(QStringLiteral("REM-ON"), at), done});
  QSignalSpy toasts(&app, &AppController::reminderToast);
  app.runAutomationAt(at.addSecs(-60));
  EXPECT_EQ(toasts.count(), 0) << "not before its minute";
  app.runAutomationAt(at);
  app.runAutomationAt(at.addSecs(60));
  ASSERT_EQ(toasts.count(), 1) << "once, and not for the done task";
  EXPECT_TRUE(toasts.at(0).at(0).toString().contains(QStringLiteral("REM-ON")));
  EXPECT_TRUE(toasts.at(0).at(1).toString().contains(QStringLiteral("Call the bank")));
  EXPECT_EQ(toasts.at(0).at(2).toString(), QStringLiteral("remind"));
}

// Typed into the document's chip: a moment, a lead, or nothing to take it off.
TEST(TaskRemindReminder, SetFromTextIsOneUndoStep) {
  AppController app;
  const QDateTime now(QDate(2026, 5, 20), QTime(10, 0));
  Task t;
  t.id = QStringLiteral("REM-SET");
  t.title = QStringLiteral("Deploy");
  t.status = QStringLiteral("todo");
  app.tasks()->reset({t});
  EXPECT_FALSE(app.setTaskReminderAt(t.id, QStringLiteral("за 15 мин"), now)) << "a lead with no time to count from";
  ASSERT_TRUE(app.setTaskReminderAt(t.id, QStringLiteral("15:00"), now));
  EXPECT_EQ(app.tasks()->items().first().local.remindAt, QDateTime(now.date(), QTime(15, 0)));
  app.undo();
  EXPECT_FALSE(app.tasks()->items().first().local.remindAt.isValid());
  EXPECT_FALSE(app.setTaskReminderAt(t.id, QStringLiteral("когда-нибудь"), now));
  ASSERT_TRUE(app.setTaskReminderAt(t.id, QStringLiteral("15:00"), now));
  ASSERT_TRUE(app.setTaskReminderAt(t.id, QString(), now));
  EXPECT_FALSE(app.tasks()->items().first().local.remindAt.isValid());
  EXPECT_EQ(app.tasks()->items().first().local.remindLead, -1);
}

// ── A reminder to self (dogfood 2026-10-22) ──

// "напомни мне …" in the line: the phrase goes, the date said is the
// reminder's time and not a plan; with none said, the option picked.
TEST(SelfReminderCapture, TheDraftIsAFlaggedTask) {
  AppController app;
  const QDateTime now(QDate(2026, 5, 20), QTime(10, 0));  // Wednesday
  const QVariantMap d = app.reminderDraft(QStringLiteral("напомни мне позвонить в банк завтра в 15:00"), now);
  EXPECT_EQ(d.value("title").toString(), QStringLiteral("позвонить в банк"));
  EXPECT_TRUE(d.value("reminder").toBool());
  EXPECT_EQ(d.value("remindAt").toDateTime(), QDateTime(QDate(2026, 5, 21), QTime(15, 0)));
  EXPECT_FALSE(d.contains("scheduledAt")) << "the time is the reminder's, not also a plan";

  const QVariantMap hour = app.reminderDraft(QStringLiteral("напомни ответить Антону"), now);
  EXPECT_EQ(hour.value("remindAt").toDateTime(), now.addSecs(3600)) << "Enter takes the default";
  const QVariantMap none = app.reminderDraft(QStringLiteral("напомни ответить Антону"), now, QStringLiteral("none"));
  EXPECT_FALSE(none.contains("remindAt")) << "no time, no notification";
  EXPECT_TRUE(none.value("reminder").toBool());
  const QVariantMap next = app.reminderDraft(QStringLiteral("remind me to renew the domain"), now, QStringLiteral("nextDay"));
  EXPECT_EQ(next.value("title").toString(), QStringLiteral("renew the domain"));
  EXPECT_EQ(next.value("remindAt").toDateTime(), QDateTime(QDate(2026, 5, 21), QTime(app.workdayStart(), 0)));

  const QVariantMap read = app.reminderCapture(QStringLiteral("напомни мне ответить Антону"), now);
  EXPECT_TRUE(read.value("found").toBool());
  EXPECT_FALSE(read.value("timed").toBool());
  EXPECT_EQ(read.value("options").toList().size(), 4);
  EXPECT_FALSE(app.reminderCapture(QStringLiteral("напоминание о встрече"), now).value("found").toBool());
}

TEST(SelfReminderCapture, SavedWithItsFlagAndKeptOnEdit) {
  AppController app;
  app.tasks()->reset({});
  QVariantMap d = app.reminderDraft(QStringLiteral("напомни полить цветы"), QDateTime(QDate(2026, 5, 20), QTime(10, 0)), "none");
  ASSERT_TRUE(app.saveTask(d));
  const QString id = d.value("id").toString();
  ASSERT_EQ(app.tasks()->items().size(), 1);
  EXPECT_TRUE(app.tasks()->items().first().local.reminder);
  QVariantMap edit = app.taskById(id);
  EXPECT_TRUE(edit.value("reminder").toBool());
  edit.remove("reminder");
  edit["title"] = QStringLiteral("полить все цветы");
  edit["_originalId"] = id;
  ASSERT_TRUE(app.saveTask(edit));
  EXPECT_TRUE(app.tasks()->items().first().local.reminder) << "an edit keeps it a reminder";
}

// Today: at its time with the bell; with no time under "someday" on today
// only; not among the tasks "without a date".
TEST(SelfReminderCapture, TodayListsThem) {
  AppController app;
  app.events()->reset({});
  const QDate today = app.today();
  Task timed = remindAt(QStringLiteral("SR-AT"), QDateTime(today, QTime(15, 0)));
  timed.local.reminder = true;
  Task someday = remindAt(QStringLiteral("SR-NONE"), QDateTime());
  someday.local.reminder = true;
  Task done = remindAt(QStringLiteral("SR-DONE"), QDateTime(today, QTime(16, 0)));
  done.local.reminder = true;
  done.status = QStringLiteral("done");
  app.tasks()->reset({timed, someday, done});
  const QVariantMap day = app.todayData(today);
  const QVariantList rows = day.value("reminders").toList();
  ASSERT_EQ(rows.size(), 2) << "a done one is gone";
  QVariantMap at;
  QVariantMap none;
  for(const QVariant& v : rows) {
    (v.toMap().value("id").toString() == QStringLiteral("SR-AT") ? at : none) = v.toMap();
  }
  EXPECT_FALSE(at.value("someday").toBool());
  EXPECT_DOUBLE_EQ(at.value("start").toDouble(), 15.0);
  EXPECT_TRUE(none.value("someday").toBool());
  EXPECT_EQ(day.value("undated").toInt(), 0) << "a reminder is not a task without a date";
  EXPECT_TRUE(app.todayData(today.addDays(1)).value("reminders").toList().isEmpty()) << "someday is today's";
}

// Its minute: the title alone, kind selfRemind (Done, +15 min, Tomorrow).
TEST(SelfReminderCapture, FiresAsASelfReminder) {
  AppController app;
  app.events()->reset({});
  app.setAppSettingsJson(blockSettings(false));
  const QDateTime at = freshMinute();
  Task t = remindAt(QStringLiteral("SR-FIRE"), at);
  t.local.reminder = true;
  app.tasks()->reset({t});
  QSignalSpy toasts(&app, &AppController::reminderToast);
  app.runAutomationAt(at);
  ASSERT_EQ(toasts.count(), 1);
  EXPECT_EQ(toasts.at(0).at(2).toString(), QStringLiteral("selfRemind"));
  EXPECT_TRUE(toasts.at(0).at(1).toString().startsWith(QStringLiteral("Call the bank")));
}

// "Tomorrow": again at the next working day's start, one undo step.
TEST(SelfReminderCapture, TomorrowMovesItToTheNextWorkday) {
  AppController app;
  Task t = remindAt(QStringLiteral("SR-TMR"), QDateTime(app.today(), QTime(15, 0)));
  t.local.reminder = true;
  app.tasks()->reset({t});
  const QString id = heap::notify::routingId(QStringLiteral("selfRemind"), heap::notify::taskRef(app.activeProfileId(), t.id));
  app.reminderAction(id, QStringLiteral("tomorrow"));
  const heap::capture::WorkHours w{app.workdayStart(), app.workdayEnd(), {}};
  EXPECT_EQ(app.tasks()->items().first().local.remindAt, heap::capture::nextWorkdayStart(QDateTime::currentDateTime(), w));
  app.undo();
  EXPECT_EQ(app.tasks()->items().first().local.remindAt, QDateTime(app.today(), QTime(15, 0)));
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir scratch;
  scratch.setAutoRemove(true);
  qputenv("XDG_CONFIG_HOME", scratch.path().toUtf8());
  qputenv("XDG_DATA_HOME", scratch.path().toUtf8());

  QApplication qapp(argc, argv);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
