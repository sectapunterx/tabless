// The safety net's rules (APP-157…), pure: every one takes `now` as an
// argument, so nothing here depends on when the suite runs.

#include "people/PingTime.h"
#include "safety/EndOfDay.h"
#include "safety/ErrorSignature.h"
#include "safety/Immersion.h"
#include "safety/SafetyText.h"
#include "safety/Standup.h"
#include "safety/WaitingOn.h"

#include <gtest/gtest.h>

using namespace heap::safety;

namespace {

const QDate kDay(2026, 10, 6);
const QDateTime kEvening(kDay, QTime(18, 5));

}  // namespace

// ── APP-157: when the end-of-day check runs ──

TEST(EndOfDayFire, NotBeforeTheSetTime) {
  EXPECT_FALSE(shouldFireToday({}, QDateTime(kDay, QTime(17, 59)), QTime(18, 0)));
}

TEST(EndOfDayFire, AtTheSetTimeAndAfterIt) {
  EXPECT_TRUE(shouldFireToday({}, QDateTime(kDay, QTime(18, 0)), QTime(18, 0)));
  EXPECT_TRUE(shouldFireToday({}, QDateTime(kDay, QTime(21, 30)), QTime(18, 0)));
}

TEST(EndOfDayFire, OnceADay) {
  EXPECT_FALSE(shouldFireToday(kDay, kEvening, QTime(18, 0)));
  // Yesterday's run does not count for today.
  EXPECT_TRUE(shouldFireToday(kDay.addDays(-1), kEvening, QTime(18, 0)));
}

// A day missed while the app was closed is not made up the next morning.
TEST(EndOfDayFire, AMissedDayIsNotMadeUpNextMorning) {
  EXPECT_FALSE(shouldFireToday(kDay.addDays(-2), QDateTime(kDay, QTime(9, 0)), QTime(18, 0)));
}

TEST(EndOfDayFire, AnInvalidTimeNeverFires) {
  EXPECT_FALSE(shouldFireToday({}, kEvening, QTime()));
}

// ── APP-157: what it says ──

TEST(EndOfDayFindings, NothingToSayWhenAllIsWell) {
  EndOfDayFacts facts;
  facts.inProgress.append({.taskId = QStringLiteral("A-1"), .lastActivity = kEvening.addDays(-1)});
  const EndOfDayFindings f = endOfDayFindings(facts, kEvening, {});
  EXPECT_FALSE(f.any());
}

TEST(EndOfDayFindings, ARunningTimerIsSaid) {
  EndOfDayFacts facts;
  facts.runningTimerTaskIds << QStringLiteral("A-1");
  const EndOfDayFindings f = endOfDayFindings(facts, kEvening, {});
  EXPECT_TRUE(f.any());
  EXPECT_EQ(f.timerTaskIds, QStringList{QStringLiteral("A-1")});
  EXPECT_EQ(f.taskIds(), QStringList{QStringLiteral("A-1")});
}

TEST(EndOfDayFindings, StaleIsOlderThanTheSetDays) {
  EndOfDayFacts facts;
  facts.inProgress.append({.taskId = QStringLiteral("old"), .lastActivity = kEvening.addDays(-3)});
  facts.inProgress.append({.taskId = QStringLiteral("fresh"), .lastActivity = kEvening.addDays(-2)});
  EndOfDaySettings s;
  s.staleDays = 3;
  const EndOfDayFindings f = endOfDayFindings(facts, kEvening, s);
  EXPECT_EQ(f.staleTaskIds, QStringList{QStringLiteral("old")});
}

// heap cannot tell "untouched for a week" from "no history at all".
TEST(EndOfDayFindings, UnknownActivityIsNeverStale) {
  EndOfDayFacts facts;
  facts.inProgress.append({.taskId = QStringLiteral("A-1"), .lastActivity = {}});
  EXPECT_FALSE(endOfDayFindings(facts, kEvening, {}).any());
}

// A task with its timer running is being worked on, and is said once.
TEST(EndOfDayFindings, ATimedTaskIsNotAlsoStale) {
  EndOfDayFacts facts;
  facts.runningTimerTaskIds << QStringLiteral("A-1");
  facts.inProgress.append({.taskId = QStringLiteral("A-1"), .lastActivity = kEvening.addDays(-10)});
  const EndOfDayFindings f = endOfDayFindings(facts, kEvening, {});
  EXPECT_TRUE(f.staleTaskIds.isEmpty());
  EXPECT_EQ(f.taskIds().size(), 1);
}

TEST(EndOfDayFindings, DirtNamesTheRepo) {
  EndOfDayFacts facts;
  facts.dirt = {.repo = QStringLiteral("heap"), .changedFiles = 3, .stashes = 1};
  const EndOfDayFindings f = endOfDayFindings(facts, kEvening, {});
  EXPECT_TRUE(f.any());
  EXPECT_EQ(f.changedFiles, 3);
  EXPECT_EQ(f.stashes, 1);
  EXPECT_EQ(f.repo, QStringLiteral("heap"));
}

TEST(EndOfDayFindings, NewestIgnoresInvalidTimes) {
  const QDateTime a = kEvening.addDays(-5);
  const QDateTime b = kEvening.addDays(-1);
  EXPECT_EQ(newest({a, QDateTime(), b}), b);
  EXPECT_FALSE(newest({QDateTime(), QDateTime()}).isValid());
}

TEST(EndOfDayGit, PorcelainCountsOnePerPath) {
  EXPECT_EQ(countPorcelainEntries(" M src/a.cpp\n?? notes.txt\nR  old -> new\n"), 3);
  EXPECT_EQ(countPorcelainEntries(""), 0);
}

TEST(EndOfDayGit, StashListCountsEntries) {
  EXPECT_EQ(countStashEntries("stash@{0}: WIP on main: abc\nstash@{1}: On x: y\n"), 2);
  EXPECT_EQ(countStashEntries("\n"), 0);
}

// ── APP-190: the day's summary ──

TEST(DaySummary, ClosedTodayCarryOverAndTimers) {
  const QDateTime morning(kDay, QTime(9, 0));
  QVector<DayTask> tasks;
  tasks.append({.id = "closed", .done = true, .closedAt = morning.addSecs(3600)});
  tasks.append({.id = "closed-yesterday", .done = true, .closedAt = morning.addDays(-1)});
  tasks.append({.id = "due-today", .dueAt = QDateTime(kDay, QTime(17, 0))});
  tasks.append({.id = "overdue", .scheduledAt = morning.addDays(-3)});
  tasks.append({.id = "tomorrow", .dueAt = morning.addDays(1)});
  tasks.append({.id = "undated"});
  tasks.append({.id = "archived-overdue", .archived = true, .dueAt = morning.addDays(-1)});
  tasks.append({.id = "late-timer", .timerStartedAt = QDateTime(kDay, QTime(15, 0))});
  tasks.append({.id = "early-timer", .timerStartedAt = morning});

  const DaySummary s = daySummary(tasks, kEvening);

  EXPECT_EQ(s.closedTaskIds, QStringList{"closed"});
  EXPECT_EQ(s.carryOverTaskIds, (QStringList{"due-today", "overdue"}));
  EXPECT_EQ(s.timerTaskIds, (QStringList{"early-timer", "late-timer"}));
  EXPECT_FALSE(s.empty());
}

TEST(DaySummary, AQuietDayIsEmpty) {
  QVector<DayTask> tasks;
  tasks.append({.id = "undated"});
  tasks.append({.id = "next-week", .dueAt = kEvening.addDays(7)});
  EXPECT_TRUE(daySummary(tasks, kEvening).empty());
  EXPECT_EQ(daySummaryLine(daySummary(tasks, kEvening), false), QString());
}

TEST(DaySummary, TheLineCountsBothLists) {
  DaySummary s;
  s.closedTaskIds = {"a", "b", "c"};
  s.carryOverTaskIds = {"d"};
  EXPECT_EQ(daySummaryLine(s, false), QStringLiteral("3 closed today · 1 carry over to tomorrow"));
  EXPECT_EQ(daySummaryLine(s, true), QString::fromUtf8("Закрыто сегодня: 3 · Переходит на завтра: 1"));
}

TEST(EndOfDayText, TheSummaryReadsLikeTheSpec) {
  EndOfDayFindings f;
  f.timerTaskIds << QStringLiteral("A-1");
  f.changedFiles = 3;
  f.repo = QStringLiteral("heap");
  f.staleTaskIds << QStringLiteral("A-2") << QStringLiteral("A-3");
  EXPECT_EQ(endOfDaySummary(f, 3, true),
            QStringLiteral("Таймер всё ещё идёт · 3 незакоммиченных файла в heap · 2 задачи в работе без движения 3+ дня"));
  EXPECT_EQ(endOfDaySummary(f, 3, false),
            QStringLiteral("Timer still running · 3 uncommitted files in heap · 2 tasks in progress without a move for 3+ days"));
}

// ── APP-158: waiting on a reply ──

TEST(Waiting, TheReminderIsDueAfterTheSetDays) {
  const WaitingOn w{.taskId = QStringLiteral("T-1"), .personId = QStringLiteral("oleg"), .since = kEvening, .remindedAt = {}};
  EXPECT_FALSE(waitingReminderDue(w, kEvening.addDays(2).addSecs(-60), 2));
  EXPECT_TRUE(waitingReminderDue(w, kEvening.addDays(2), 2));
  EXPECT_TRUE(waitingReminderDue(w, kEvening.addDays(9), 2));
}

TEST(Waiting, OncePerLink) {
  WaitingOn w{.taskId = QStringLiteral("T-1"), .personId = QStringLiteral("oleg"), .since = kEvening, .remindedAt = {}};
  w.remindedAt = kEvening.addDays(2);
  EXPECT_FALSE(waitingReminderDue(w, kEvening.addDays(5), 2));
}

// Asking again starts the wait over: a new `since`, the reminder cleared.
TEST(Waiting, ReArmingStartsOver) {
  WaitingOn w{.taskId = QStringLiteral("T-1"), .personId = QStringLiteral("oleg"), .since = kEvening, .remindedAt = kEvening.addDays(2)};
  w.since = kEvening.addDays(3);
  w.remindedAt = {};
  EXPECT_FALSE(waitingReminderDue(w, kEvening.addDays(4), 2));
  EXPECT_TRUE(waitingReminderDue(w, kEvening.addDays(5), 2));
}

TEST(Waiting, ZeroDaysMeansOne) {
  const WaitingOn w{.taskId = QStringLiteral("T-1"), .personId = QStringLiteral("oleg"), .since = kEvening, .remindedAt = {}};
  EXPECT_FALSE(waitingReminderDue(w, kEvening.addSecs(3600), 0));
}

TEST(Waiting, DaysAreCalendarDays) {
  EXPECT_EQ(waitingDays(QDateTime(kDay, QTime(23, 0)), kDay.addDays(1)), 1);
  EXPECT_EQ(waitingDays(QDateTime(kDay, QTime(9, 0)), kDay), 0);
  EXPECT_EQ(waitingDays({}, kDay), 0);
}

TEST(Waiting, OnlyAMoveToRepliedEndsIt) {
  EXPECT_TRUE(replyEndsWaiting(QStringLiteral("pinged"), QStringLiteral("replied")));
  EXPECT_TRUE(replyEndsWaiting(QStringLiteral("todo"), QStringLiteral("replied")));
  EXPECT_FALSE(replyEndsWaiting(QStringLiteral("replied"), QStringLiteral("replied")));
  EXPECT_FALSE(replyEndsWaiting(QStringLiteral("todo"), QStringLiteral("pinged")));
}

// ── APP-159: you've seen this before ──

TEST(SeenDetector, TypedErrorsAndTraces) {
  EXPECT_TRUE(looksLikeError(QStringLiteral("TypeError: Cannot read properties of undefined (reading 'id')")));
  EXPECT_TRUE(
      looksLikeError(QStringLiteral("Exception in thread \"main\" java.lang.NullPointerException: name is null\n"
                                    "    at com.acme.User.greet(User.java:42)")));
  EXPECT_TRUE(
      looksLikeError(QStringLiteral("Traceback (most recent call last):\n  File \"app.py\", line 3, in <module>\n"
                                    "ValueError: invalid literal for int() with base 10: 'x'")));
  EXPECT_TRUE(looksLikeError(QStringLiteral("panic: runtime error: index out of range [5] with length 3")));
  EXPECT_TRUE(looksLikeError(QStringLiteral("src/main.cpp:12:5: error: expected ';' after expression")));
  EXPECT_TRUE(looksLikeError(QStringLiteral("Segmentation fault (core dumped)")));
}

TEST(SeenDetector, AFrameOrTwoWithoutAHeadline) {
  EXPECT_TRUE(looksLikeError(QStringLiteral("    at Object.run (/srv/app/index.js:10:5)\n    at main (/srv/app/main.js:3:1)")));
  EXPECT_TRUE(looksLikeError(QStringLiteral("#0 0x00007ffff7a42428 in raise\n#1 0x00007ffff7a4402a in abort")));
}

// Prose that mentions an error is not one.
TEST(SeenDetector, QuietOnProse) {
  EXPECT_FALSE(looksLikeError(QStringLiteral("fix the error handling in login")));
  EXPECT_FALSE(looksLikeError(QStringLiteral("Call Oleg about the deploy tomorrow 10:00")));
  EXPECT_FALSE(looksLikeError(QStringLiteral("APP-12 review")));
  EXPECT_FALSE(looksLikeError(QString()));
}

TEST(SeenSignature, KeepsTheTypeAndTheWords) {
  const ErrorSignature s = signatureOf(QStringLiteral("Uncaught TypeError: Cannot read properties of undefined (reading 'userId')"));
  EXPECT_EQ(s.type, QStringLiteral("typeerror"));
  EXPECT_TRUE(s.words.contains(QStringLiteral("properties")));
  EXPECT_TRUE(s.words.contains(QStringLiteral("undefined")));
  EXPECT_TRUE(s.words.contains(QStringLiteral("userid")));
}

TEST(SeenSignature, TheQualifiedJavaTypeIsItsLastPart) {
  const ErrorSignature s = signatureOf(QStringLiteral("Exception in thread \"main\" java.lang.NullPointerException: name is null"));
  EXPECT_EQ(s.type, QStringLiteral("nullpointerexception"));
  EXPECT_EQ(s.words, QStringList({QStringLiteral("name"), QStringLiteral("null")}));
}

TEST(SeenSignature, PythonNamesItsErrorLast) {
  const ErrorSignature s =
      signatureOf(QStringLiteral("Traceback (most recent call last):\n  File \"app.py\", line 3\n"
                                 "KeyError: 'session_token'"));
  EXPECT_EQ(s.type, QStringLiteral("keyerror"));
  EXPECT_EQ(s.words, QStringList{QStringLiteral("session_token")});
}

// Two occurrences of one failure differ in numbers, addresses, paths, times.
TEST(SeenSignature, StripsWhatChangesBetweenOccurrences) {
  const ErrorSignature a = signatureOf(QStringLiteral("2026-09-12 10:31:07 panic: lock 0x7ffe12ab held by worker 17 in /srv/a/b.go"));
  const ErrorSignature b = signatureOf(QStringLiteral("2026-10-06 18:02:44 panic: lock 0x1122aabb held by worker 3 in C:\\src\\b.go"));
  EXPECT_EQ(a.type, QStringLiteral("panic"));
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.words, QStringList({QStringLiteral("lock"), QStringLiteral("held"), QStringLiteral("worker")}));
}

TEST(SeenSignature, NothingForProse) {
  EXPECT_TRUE(signatureOf(QStringLiteral("remember to water the plants")).isEmpty());
}

TEST(SeenMatch, FindsTheNoteThatMentionsIt) {
  const ErrorSignature sig = signatureOf(QStringLiteral("TypeError: Cannot read properties of undefined (reading 'userId')"));
  QVector<SeenCandidate> c;
  c.append({.kind = QStringLiteral("note"),
            .id = QStringLiteral("n1"),
            .title = QStringLiteral("Groceries"),
            .profileId = {},
            .when = {},
            .text = QStringLiteral("milk, bread")});
  c.append({.kind = QStringLiteral("note"),
            .id = QStringLiteral("n2"),
            .title = QStringLiteral("Login crash after deploy"),
            .profileId = {},
            .when = kEvening.addDays(-20),
            .text = QStringLiteral("Saw `TypeError: Cannot read properties of undefined (reading 'userId')` — the session was empty.")});
  EXPECT_EQ(bestSeenMatch(sig, c), 1);
}

TEST(SeenMatch, TheTypeMustBeThere) {
  const ErrorSignature sig = signatureOf(QStringLiteral("KeyError: 'session_token'"));
  const QVector<SeenCandidate> c{{.kind = QStringLiteral("task"),
                                  .id = QStringLiteral("T-1"),
                                  .title = QStringLiteral("rotate session_token"),
                                  .profileId = {},
                                  .when = {},
                                  .text = {}}};
  EXPECT_EQ(bestSeenMatch(sig, c), -1);
}

TEST(SeenMatch, MostOfTheWordsAndTheNewestWins) {
  const ErrorSignature sig = signatureOf(QStringLiteral("panic: lock held by worker during shutdown flush"));
  const QString seen = QStringLiteral("panic: lock held by worker during shutdown");
  QVector<SeenCandidate> c;
  c.append({.kind = QStringLiteral("note"),
            .id = QStringLiteral("old"),
            .title = {},
            .profileId = {},
            .when = kEvening.addDays(-30),
            .text = seen});
  c.append({.kind = QStringLiteral("note"),
            .id = QStringLiteral("new"),
            .title = {},
            .profileId = {},
            .when = kEvening.addDays(-2),
            .text = seen});
  c.append({.kind = QStringLiteral("note"),
            .id = QStringLiteral("weak"),
            .title = {},
            .profileId = {},
            .when = kEvening,
            .text = QStringLiteral("panic: worker")});
  EXPECT_EQ(bestSeenMatch(sig, c), 1);
}

TEST(SeenMatch, ABareTypeIsTooCommonToSay) {
  const ErrorSignature sig = signatureOf(QStringLiteral("TypeError"));
  const QVector<SeenCandidate> c{{.kind = QStringLiteral("note"),
                                  .id = QStringLiteral("n"),
                                  .title = {},
                                  .profileId = {},
                                  .when = {},
                                  .text = QStringLiteral("TypeError somewhere")}};
  EXPECT_EQ(bestSeenMatch(sig, c), -1);
}

// ── APP-160: focus mode ──

TEST(Immersion, OutsideFocusEverythingIsDelivered) {
  EXPECT_EQ(immersionDelivery(QStringLiteral("deadline"), false, true), Delivery::Deliver);
  EXPECT_EQ(immersionDelivery(QStringLiteral("meeting"), false, false), Delivery::Deliver);
}

TEST(Immersion, FocusHoldsAllButMeetings) {
  EXPECT_EQ(immersionDelivery(QStringLiteral("deadline"), true, true), Delivery::Hold);
  EXPECT_EQ(immersionDelivery(QStringLiteral("git"), true, true), Delivery::Hold);
  EXPECT_EQ(immersionDelivery(QStringLiteral("endOfDay"), true, true), Delivery::Hold);
  EXPECT_EQ(immersionDelivery(QStringLiteral("meeting"), true, true), Delivery::Deliver);
  EXPECT_EQ(immersionDelivery(QStringLiteral("standup"), true, true), Delivery::Deliver);
}

TEST(Immersion, MeetingsCanBeHeldToo) {
  EXPECT_EQ(immersionDelivery(QStringLiteral("meeting"), true, false), Delivery::Hold);
  EXPECT_EQ(immersionDelivery(QStringLiteral("standup"), true, false), Delivery::Hold);
}

TEST(Immersion, WholeMinutes) {
  EXPECT_EQ(immersionMinutes(-5), 0);
  EXPECT_EQ(immersionMinutes(59), 0);
  EXPECT_EQ(immersionMinutes((25 * 60) + 59), 25);
}

// ── APP-170: standup draft ──

namespace {

StandupTask standupTask(const QString& id, const QString& title, const QString& status) {
  StandupTask t;
  t.id = id;
  t.title = title;
  t.status = status;
  t.doing = status == QLatin1String("prog");
  t.blocked = status == QLatin1String("blocked");
  t.done = status == QLatin1String("done");
  return t;
}

// Tuesday 2026-10-06; the working day before is Monday the 5th.
StandupFacts standupFacts() {
  StandupFacts f;
  f.today = kDay;
  f.previousDay = kDay.addDays(-1);
  f.statusNames = {{QStringLiteral("todo"), QStringLiteral("To do")},
                   {QStringLiteral("prog"), QStringLiteral("In progress")},
                   {QStringLiteral("review"), QStringLiteral("Review")},
                   {QStringLiteral("blocked"), QStringLiteral("Blocked")}};
  f.tasks = {standupTask(QStringLiteral("APP-12"), QStringLiteral("Login rate limit"), QStringLiteral("review")),
             standupTask(QStringLiteral("APP-14"), QStringLiteral("CSV export"), QStringLiteral("prog")),
             standupTask(QStringLiteral("APP-20"), QStringLiteral("Deploy pipeline"), QStringLiteral("blocked"))};
  const QDateTime mon(kDay.addDays(-1), QTime(11, 0));
  f.moves = {{.taskId = QStringLiteral("APP-12"), .from = QStringLiteral("prog"), .to = QStringLiteral("review"), .at = mon},
             // Older than the previous day: not yesterday's news.
             {.taskId = QStringLiteral("APP-14"), .from = QStringLiteral("todo"), .to = QStringLiteral("prog"), .at = mon.addDays(-3)}};
  f.commits = {{.taskId = QStringLiteral("APP-14"), .subject = QStringLiteral("APP-14 csv"), .at = mon.addSecs(3600)},
               {.taskId = QStringLiteral("APP-14"), .subject = QStringLiteral("APP-14 more"), .at = mon.addSecs(7200)}};
  f.meetings = {{.title = QStringLiteral("Sync with the team"), .date = kDay.addDays(-1), .start = 10.0, .allDay = false},
                {.title = QStringLiteral("1:1 with Oleg"), .date = kDay, .start = 15.0, .allDay = false}};
  return f;
}

}  // namespace

TEST(Standup, PreviousWorkDaySkipsTheWeekend) {
  const auto weekdays = [](const QDate& d) {
    return d.dayOfWeek() <= 5;
  };
  EXPECT_EQ(previousWorkDay(QDate(2026, 10, 5), weekdays), QDate(2026, 10, 2));  // Monday → Friday
  EXPECT_EQ(previousWorkDay(QDate(2026, 10, 6), weekdays), QDate(2026, 10, 5));
  EXPECT_EQ(previousWorkDay(QDate(2026, 10, 6),
                            [](const QDate&) {
                              return false;
                            }),
            QDate(2026, 10, 5));
}

TEST(Standup, TheThreeSections) {
  EXPECT_EQ(buildStandup(standupFacts(), false),
            QStringLiteral("Yesterday: APP-12 Login rate limit (In progress \u2192 Review), APP-14 CSV export (2 commits), "
                           "meeting \u201cSync with the team\u201d.\n"
                           "Today: APP-14 CSV export, 1:1 with Oleg at 15:00.\n"
                           "Blockers: APP-20 Deploy pipeline."));
}

TEST(Standup, RussianHeadingsAndPlurals) {
  const QString ru = buildStandup(standupFacts(), true);
  EXPECT_TRUE(ru.startsWith(QStringLiteral("Вчера: ")));
  EXPECT_TRUE(ru.contains(QStringLiteral("APP-14 CSV export (2 коммита)")));
  EXPECT_TRUE(ru.contains(QStringLiteral("\nСегодня: ")));
  EXPECT_TRUE(ru.contains(QStringLiteral("в 15:00")));
  EXPECT_TRUE(ru.contains(QStringLiteral("\nБлокеры: APP-20 Deploy pipeline.")));
}

// An empty section keeps its place, so the shape is there to fill in.
TEST(Standup, EmptySectionsSaySo) {
  StandupFacts f;
  f.today = kDay;
  f.previousDay = kDay.addDays(-1);
  EXPECT_EQ(buildStandup(f, false), QStringLiteral("Yesterday: \u2014\nToday: \u2014\nBlockers: \u2014"));
}

// Moved there and back the same day: nothing to report.
TEST(Standup, ARoundTripIsNotAMove) {
  StandupFacts f = standupFacts();
  const QDateTime mon(kDay.addDays(-1), QTime(9, 0));
  f.moves = {{.taskId = QStringLiteral("APP-12"), .from = QStringLiteral("prog"), .to = QStringLiteral("review"), .at = mon},
             {.taskId = QStringLiteral("APP-12"), .from = QStringLiteral("review"), .to = QStringLiteral("prog"), .at = mon.addSecs(60)}};
  EXPECT_FALSE(buildStandup(f, false).contains(QStringLiteral("APP-12 Login rate limit (")));
}

TEST(EndOfDayText, RussianPluralForms) {
  const QString forms = QStringLiteral("файл|файла|файлов");
  EXPECT_EQ(plural(1, forms), QStringLiteral("файл"));
  EXPECT_EQ(plural(3, forms), QStringLiteral("файла"));
  EXPECT_EQ(plural(5, forms), QStringLiteral("файлов"));
  EXPECT_EQ(plural(11, forms), QStringLiteral("файлов"));
  EXPECT_EQ(plural(21, forms), QStringLiteral("файл"));
  EXPECT_EQ(plural(22, forms), QStringLiteral("файла"));
  EXPECT_EQ(plural(14, forms), QStringLiteral("файлов"));
}

// ── "Кому написать" in time (dogfood 2026-10-20) ──

namespace {

heap::chrono::ParseResult parsed(const QDateTime& start, bool hasTime) {
  heap::chrono::ParseResult r;
  r.ok = true;
  r.start = start;
  r.hasTime = hasTime;
  return r;
}

}  // namespace

TEST(PingWhen, NothingTypedClearsTheTime) {
  const heap::people::PingWhen w = heap::people::resolvePingWhen(QStringLiteral("  "), {}, kEvening);
  EXPECT_TRUE(w.ok);
  EXPECT_TRUE(w.empty);
  EXPECT_FALSE(w.at.isValid());
}

TEST(PingWhen, WhatIsNotATimeIsSaidSo) {
  const heap::people::PingWhen w = heap::people::resolvePingWhen(QStringLiteral("когда-нибудь"), {}, kEvening);
  EXPECT_FALSE(w.ok);
  EXPECT_EQ(w.error, QStringLiteral("unparsed"));
}

TEST(PingWhen, ABareTimeStillAheadIsToday) {
  const heap::people::PingWhen w =
      heap::people::resolvePingWhen(QStringLiteral("19:00"), parsed(QDateTime(kDay, QTime(19, 0)), true), kEvening);
  ASSERT_TRUE(w.ok);
  EXPECT_EQ(w.at, QDateTime(kDay, QTime(19, 0)));
  EXPECT_TRUE(w.hasTime);
  EXPECT_FALSE(w.rolled);
}

TEST(PingWhen, ABareTimeAlreadyGoneIsTomorrow) {
  const heap::people::PingWhen w =
      heap::people::resolvePingWhen(QStringLiteral("15:00"), parsed(QDateTime(kDay, QTime(15, 0)), true), kEvening);
  ASSERT_TRUE(w.ok);
  EXPECT_EQ(w.at, QDateTime(kDay.addDays(1), QTime(15, 0)));
  EXPECT_TRUE(w.rolled);
}

TEST(PingWhen, TodayAtAPastTimeIsRefused) {
  const heap::people::PingWhen w =
      heap::people::resolvePingWhen(QStringLiteral("сегодня 15:00"), parsed(QDateTime(kDay, QTime(15, 0)), true), kEvening);
  EXPECT_FALSE(w.ok);
  EXPECT_EQ(w.error, QStringLiteral("past"));
  EXPECT_FALSE(heap::people::resolvePingWhen(QStringLiteral("today 3pm"), parsed(QDateTime(kDay, QTime(15, 0)), true), kEvening).ok);
}

TEST(PingWhen, AnotherPastMomentIsRefused) {
  EXPECT_EQ(
      heap::people::resolvePingWhen(QStringLiteral("вчера 15:00"), parsed(QDateTime(kDay.addDays(-1), QTime(15, 0)), true), kEvening).error,
      QStringLiteral("past"));
  EXPECT_EQ(heap::people::resolvePingWhen(QStringLiteral("вчера"), parsed(QDateTime(kDay.addDays(-1), QTime(0, 0)), false), kEvening).error,
            QStringLiteral("past"));
}

TEST(PingWhen, ADayWithoutATimeKeepsTheDayAndDoesNotNotify) {
  const heap::people::PingWhen today =
      heap::people::resolvePingWhen(QStringLiteral("сегодня"), parsed(QDateTime(kDay, QTime(0, 0)), false), kEvening);
  ASSERT_TRUE(today.ok);
  EXPECT_FALSE(today.hasTime);
  EXPECT_EQ(today.at.date(), kDay);
  const heap::people::PingWhen fri =
      heap::people::resolvePingWhen(QStringLiteral("пт"), parsed(QDateTime(kDay.addDays(3), QTime(0, 0)), false), kEvening);
  ASSERT_TRUE(fri.ok);
  EXPECT_FALSE(fri.hasTime);
  EXPECT_FALSE(heap::people::pingReminder(QStringLiteral("oleg"), QStringLiteral("todo"), fri.at, fri.hasTime, fri.at.addSecs(60)).due);
}

TEST(PingReminder, DueAtItsMinuteOnceKeyedByTheMoment) {
  const QDateTime at(kDay, QTime(15, 0));
  const QString todo = QStringLiteral("todo");
  EXPECT_FALSE(heap::people::pingReminder(QStringLiteral("oleg"), todo, at, true, at.addSecs(-60)).due);
  const heap::people::PingCall call = heap::people::pingReminder(QStringLiteral("oleg"), todo, at, true, at);
  EXPECT_TRUE(call.due);
  EXPECT_FALSE(call.late);
  // Another time is another key: an edit after it fired re-arms it.
  EXPECT_NE(call.key, heap::people::pingReminder(QStringLiteral("oleg"), todo, at.addSecs(3600), true, at.addSecs(3600)).key);
}

TEST(PingReminder, MissedWhileClosedComesLateWithinTwoDays) {
  const QDateTime at(kDay, QTime(15, 0));
  const heap::people::PingCall late =
      heap::people::pingReminder(QStringLiteral("oleg"), QStringLiteral("todo"), at, true, at.addSecs(3600 * 20));
  EXPECT_TRUE(late.due);
  EXPECT_TRUE(late.late);
  EXPECT_FALSE(heap::people::pingReminder(QStringLiteral("oleg"), QStringLiteral("todo"), at, true, at.addDays(3)).due);
}

TEST(PingReminder, OnlyAnAskStillToWrite) {
  const QDateTime at(kDay, QTime(15, 0));
  EXPECT_FALSE(heap::people::pingReminder(QStringLiteral("oleg"), QStringLiteral("pinged"), at, true, at).due);
  EXPECT_FALSE(heap::people::pingReminder(QStringLiteral("oleg"), QStringLiteral("replied"), at, true, at).due);
  EXPECT_FALSE(heap::people::pingReminder(QStringLiteral("oleg"), QStringLiteral("todo"), QDateTime(), true, at).due);
}

TEST(PingWaitingNudge, AfterTheDaysOnceThenNever) {
  const QDateTime since(kDay, QTime(10, 0));
  const QString pinged = QStringLiteral("pinged");
  EXPECT_FALSE(heap::people::waitingNudgeDue(pinged, since, since.addDays(1), 2));
  EXPECT_TRUE(heap::people::waitingNudgeDue(pinged, since, since.addDays(2), 2));
  EXPECT_FALSE(heap::people::waitingNudgeDue(pinged, since, since.addDays(5), 2)) << "not forever";
  EXPECT_FALSE(heap::people::waitingNudgeDue(QStringLiteral("replied"), since, since.addDays(2), 2));
  EXPECT_FALSE(heap::people::waitingNudgeDue(pinged, QDateTime(), since.addDays(2), 2));
}

TEST(PingLabels, HowLongAgo) {
  const QDateTime now(kDay, QTime(18, 0));
  EXPECT_EQ(heap::people::agoShort(now.addSecs(-10), now, true), QStringLiteral("только что"));
  EXPECT_EQ(heap::people::agoShort(now.addSecs(-15 * 60), now, true), QStringLiteral("15 мин"));
  EXPECT_EQ(heap::people::agoShort(now.addSecs(-2 * 3600), now, true), QStringLiteral("2 ч"));
  EXPECT_EQ(heap::people::agoShort(now.addDays(-3), now, false), QStringLiteral("3 d"));
  EXPECT_TRUE(heap::people::agoShort(QDateTime(), now, true).isEmpty());
}

TEST(PingLabels, WhenToWrite) {
  const QDateTime now(kDay, QTime(9, 0));  // a Tuesday
  EXPECT_EQ(heap::people::whenShort(QDateTime(kDay, QTime(15, 0)), true, now, true, false), QStringLiteral("15:00"));
  EXPECT_EQ(heap::people::whenShort(QDateTime(kDay.addDays(1), QTime(15, 0)), true, now, true, false), QStringLiteral("завтра 15:00"));
  EXPECT_EQ(heap::people::whenShort(QDateTime(kDay.addDays(3), QTime(0, 0)), false, now, true, false), QStringLiteral("пт"));
  EXPECT_EQ(heap::people::whenShort(QDateTime(kDay.addDays(1), QTime(0, 0)), false, now, false, false), QStringLiteral("tomorrow"));
  EXPECT_EQ(heap::people::whenShort(QDateTime(kDay.addDays(20), QTime(15, 0)), true, now, true, false), QStringLiteral("26.10 15:00"));
}
