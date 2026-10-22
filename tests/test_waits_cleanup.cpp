// The sidebar cleans itself (dogfood 2026-10-22, src/git/WaitsCleanup.h):
// what leaves after a sync and what stays, "ушло N сегодня" and midnight,
// "Отложить до завтра", and the answered rows of "Кому написать". Fixed
// times only.

#include "git/WaitsCleanup.h"

#include <gtest/gtest.h>

using namespace heap::waits;

namespace {

const QDateTime kNow(QDate(2026, 10, 22), QTime(14, 32));

Row rowOf(const QString& url, int threads, bool review) {
  Row r;
  r.url = url;
  r.title = QStringLiteral("Title ") + url;
  r.ref = QStringLiteral("!") + url;
  r.count = threads;
  r.review = review;
  r.since = kNow.addDays(-2);
  return r;
}

Observation seen(Seen s, int threads = 0, bool review = false) {
  Observation o;
  o.seen = s;
  o.row.count = threads;
  o.row.review = review;
  return o;
}

}  // namespace

TEST(WaitsCleanup, AReadAndClearRowLeavesWithWhy) {
  const QHash<QString, Row> known{{"a", rowOf("a", 1, false)}, {"b", rowOf("b", 0, true)}, {"c", rowOf("c", 2, true)}};
  const QHash<QString, Observation> now{{"a", seen(Seen::Clear)}, {"b", seen(Seen::Clear)}, {"c", seen(Seen::Clear)}};
  const Result r = reconcile(known, now, kNow);
  EXPECT_TRUE(r.known.isEmpty());
  ASSERT_EQ(r.gone.size(), 3);
  QHash<QString, QString> why;
  for(const Gone& g : r.gone) {
    why.insert(g.row.url, g.why);
    EXPECT_EQ(g.at, kNow);
  }
  EXPECT_EQ(why.value("a"), "replied");
  EXPECT_EQ(why.value("b"), "reviewed");
  EXPECT_EQ(why.value("c"), "resolved");
}

TEST(WaitsCleanup, AClosedRequestLeavesAsClosed) {
  const Result r = reconcile({{"a", rowOf("a", 1, true)}}, {{"a", seen(Seen::Closed)}}, kNow);
  ASSERT_EQ(r.gone.size(), 1);
  EXPECT_EQ(r.gone.first().why, "closed");
}

TEST(WaitsCleanup, NothingLeavesWithoutASuccessfulRead) {
  // The forge unreachable: not read (Unknown), or not even on the list
  // this time (absent): the row stays as it was.
  const QHash<QString, Row> known{{"a", rowOf("a", 1, false)}, {"b", rowOf("b", 1, false)}};
  const Result r = reconcile(known, {{"a", seen(Seen::Unknown)}}, kNow);
  EXPECT_TRUE(r.gone.isEmpty());
  ASSERT_EQ(r.known.size(), 2);
  EXPECT_EQ(r.known.value("a").count, 1);
}

TEST(WaitsCleanup, StillWaitingIsRememberedAsItIsNow) {
  Observation o = seen(Seen::Waits, 3, false);
  o.row.url = "a";
  o.row.title = "Renamed";
  const Result r = reconcile({{"a", rowOf("a", 1, false)}}, {{"a", o}, {"n", seen(Seen::Waits, 1)}}, kNow);
  EXPECT_TRUE(r.gone.isEmpty());
  EXPECT_EQ(r.known.value("a").title, "Renamed");
  EXPECT_EQ(r.known.value("a").count, 3);
  EXPECT_TRUE(r.known.contains("n")) << "a new wait is remembered";
}

TEST(WaitsCleanup, OffOrALocalChangeForgetsQuietly) {
  const QHash<QString, Row> known{{"a", rowOf("a", 1, false)}, {"b", rowOf("b", 1, false)}};
  Result r = reconcile(known, {{"a", seen(Seen::Off)}, {"b", seen(Seen::Clear)}}, kNow, /*logGone=*/false);
  EXPECT_TRUE(r.gone.isEmpty()) << "a thread set aside is not \"ушло\"";
  EXPECT_TRUE(r.known.isEmpty());
  r = reconcile(known, {{"a", seen(Seen::Off)}}, kNow);
  EXPECT_TRUE(r.gone.isEmpty()) << "the feature off says nothing";
  EXPECT_FALSE(r.known.contains("a"));
}

TEST(WaitsCleanup, ARowFadesForTwoSeconds) {
  const Gone g{rowOf("a", 1, false), "replied", kNow};
  EXPECT_TRUE(leaving(g, kNow));
  EXPECT_TRUE(leaving(g, kNow.addMSecs(1999)));
  EXPECT_FALSE(leaving(g, kNow.addMSecs(2000)));
  EXPECT_FALSE(leaving(g, kNow.addSecs(-1))) << "a clock set back shows nothing struck";
}

TEST(WaitsCleanup, GoneTodayCountsAndResetsAtMidnight) {
  Day d;
  addGone(d, {Gone{rowOf("a", 1, false), "replied", kNow}, Gone{rowOf("b", 1, false), "closed", kNow}}, kNow.date());
  EXPECT_EQ(goneCount(d, kNow.date()), 2);
  // The same request again: one entry, the latest.
  addGone(d, {Gone{rowOf("a", 0, true), "reviewed", kNow.addSecs(60)}}, kNow.date());
  EXPECT_EQ(d.gone.size(), 2);
  EXPECT_EQ(d.gone.last().why, "reviewed");
  // Brought back: not counted; going again counts it again.
  d.back.insert("b");
  EXPECT_EQ(goneCount(d, kNow.date()), 1);
  addGone(d, {Gone{rowOf("b", 1, false), "closed", kNow.addSecs(120)}}, kNow.date());
  EXPECT_FALSE(d.back.contains("b"));
  EXPECT_EQ(goneCount(d, kNow.date()), 2);
  // Waits again: off "ушло".
  forgetGone(d, "a");
  EXPECT_EQ(goneCount(d, kNow.date()), 1);
  // Local midnight: a new day starts empty.
  const QDate tomorrow = kNow.date().addDays(1);
  EXPECT_EQ(goneCount(d, tomorrow), 0);
  rollDay(d, tomorrow);
  EXPECT_TRUE(d.gone.isEmpty());
  EXPECT_EQ(d.date, tomorrow);
}

TEST(WaitsCleanup, TheDayRoundTrips) {
  Day d;
  addGone(d, {Gone{rowOf("a", 2, true), "resolved", kNow.addMSecs(250)}}, kNow.date());
  d.back.insert("a");
  const Day back = dayFromJson(dayToJson(d));
  EXPECT_EQ(back.date, d.date);
  ASSERT_EQ(back.gone.size(), 1);
  EXPECT_EQ(back.gone.first().row.title, "Title a");
  EXPECT_EQ(back.gone.first().row.count, 2);
  EXPECT_TRUE(back.gone.first().row.review);
  EXPECT_EQ(back.gone.first().at, kNow.addMSecs(250));
  EXPECT_TRUE(back.back.contains("a"));
  EXPECT_TRUE(dayToJson(Day{}).isEmpty()) << "nothing to keep: no key";
}

TEST(WaitsCleanup, PutOffUntilTomorrowsWorkday) {
  EXPECT_EQ(snoozeWake(kNow, 9), QDateTime(QDate(2026, 10, 23), QTime(9, 0)));
  // Late at night: still tomorrow, not in an hour.
  EXPECT_EQ(snoozeWake(QDateTime(QDate(2026, 10, 22), QTime(23, 50)), 10), QDateTime(QDate(2026, 10, 23), QTime(10, 0)));
  // An hour that makes no sense: midnight.
  EXPECT_EQ(snoozeWake(kNow, -1), QDateTime(QDate(2026, 10, 23), QTime(0, 0)));
  EXPECT_EQ(snoozeWake(kNow, 24), QDateTime(QDate(2026, 10, 23), QTime(0, 0)));

  const QJsonObject store{{"a", snoozeWake(kNow, 9).toString(Qt::ISODate)}, {"old", kNow.addDays(-1).toString(Qt::ISODate)}};
  EXPECT_TRUE(snoozed(store, "a", kNow));
  EXPECT_TRUE(snoozed(store, "a", QDateTime(QDate(2026, 10, 23), QTime(8, 59))));
  EXPECT_FALSE(snoozed(store, "a", QDateTime(QDate(2026, 10, 23), QTime(9, 0))));
  EXPECT_FALSE(snoozed(store, "old", kNow));
  EXPECT_FALSE(snoozed(store, "none", kNow));
  const QJsonObject pruned = pruneSnoozes(store, kNow);
  EXPECT_TRUE(pruned.contains("a"));
  EXPECT_FALSE(pruned.contains("old"));
}

TEST(WaitsCleanup, AnAnswerStaysADayUnlessReadOrApproved) {
  const QDateTime answered = kNow.addSecs(-3600);
  EXPECT_TRUE(answeredRowStays(answered, {}, kNow));
  EXPECT_TRUE(answeredRowStays(answered, {}, answered.addSecs(24 * 3600 - 1)));
  EXPECT_FALSE(answeredRowStays(answered, {}, answered.addSecs(24 * 3600))) << "a day, then it goes";
  EXPECT_FALSE(answeredRowStays(answered, answered, kNow)) << "read from the row";
  EXPECT_TRUE(answeredRowStays(answered, answered.addSecs(-60), kNow)) << "an older answer was read, not this one";
  EXPECT_FALSE(answeredRowStays(answered, {}, kNow, /*approved=*/true)) << "an approval goes at once";
  EXPECT_FALSE(answeredRowStays({}, {}, kNow));

  const QJsonObject read{{"fresh", answered.toString(Qt::ISODateWithMs)}, {"stale", kNow.addDays(-3).toString(Qt::ISODateWithMs)}};
  const QJsonObject pruned = pruneRead(read, kNow);
  EXPECT_TRUE(pruned.contains("fresh"));
  EXPECT_FALSE(pruned.contains("stale"));
}
