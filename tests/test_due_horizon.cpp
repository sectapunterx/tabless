// Today's "Ближайшие" deadlines (dogfood 2026-10-19): the horizon setting, the
// window and the day groups, against a fixed today — never the clock.
#include "plan/DueHorizon.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace heap::plan;

namespace {

// Wednesday, 21 October 2026.
const QDate kToday(2026, 10, 21);

DueEntry entry(int index, const QDate& d, int hour = -1, const QString& priority = QString()) {
  DueEntry e;
  e.index = index;
  e.hasTime = hour >= 0;
  e.due = QDateTime(d, hour >= 0 ? QTime(hour, 0) : QTime(0, 0));
  e.priority = priority;
  return e;
}

}  // namespace

TEST(DueHorizon, ChoicesAreAscendingAndHoldTheDefault) {
  const QVector<int>& c = deadlineHorizonChoices();
  ASSERT_FALSE(c.isEmpty());
  EXPECT_TRUE(std::is_sorted(c.begin(), c.end()));
  EXPECT_TRUE(c.contains(kDefaultDeadlineHorizon));
  EXPECT_EQ(c.constFirst(), 1) << "through tomorrow, what Today showed before";
}

TEST(DueHorizon, ClampKeepsChoicesAndSnapsTheRest) {
  for(const int c : deadlineHorizonChoices()) {
    EXPECT_EQ(clampDeadlineHorizon(c), c);
  }
  EXPECT_EQ(clampDeadlineHorizon(0), kDefaultDeadlineHorizon) << "missing";
  EXPECT_EQ(clampDeadlineHorizon(-4), kDefaultDeadlineHorizon);
  EXPECT_EQ(clampDeadlineHorizon(2), 1) << "a tie goes to the shorter";
  EXPECT_EQ(clampDeadlineHorizon(5), 3);
  EXPECT_EQ(clampDeadlineHorizon(6), 7);
  EXPECT_EQ(clampDeadlineHorizon(10), 7);
  EXPECT_EQ(clampDeadlineHorizon(11), 14);
  EXPECT_EQ(clampDeadlineHorizon(365), 30);
}

TEST(DueHorizon, WindowIsTheDayAndTheNextN) {
  EXPECT_TRUE(dueWithin(kToday, kToday, 7));
  EXPECT_TRUE(dueWithin(kToday.addDays(7), kToday, 7)) << "the last day is in";
  EXPECT_FALSE(dueWithin(kToday.addDays(8), kToday, 7));
  EXPECT_FALSE(dueWithin(kToday.addDays(-1), kToday, 7)) << "overdue is apart";
  EXPECT_TRUE(dueWithin(kToday.addDays(1), kToday, 1));
  EXPECT_FALSE(dueWithin(kToday.addDays(2), kToday, 1));
  EXPECT_FALSE(dueWithin(QDate(), kToday, 7));
}

TEST(DueHorizon, WindowCrossesMonthAndYear) {
  const QDate nye(2026, 12, 29);
  EXPECT_TRUE(dueWithin(QDate(2027, 1, 4), nye, 7));
  EXPECT_FALSE(dueWithin(QDate(2027, 1, 6), nye, 7));
  EXPECT_TRUE(dueWithin(QDate(2026, 3, 1), QDate(2026, 2, 27), 3)) << "February";
}

TEST(DueHorizon, DayKindIsRelativeToTheRealToday) {
  EXPECT_EQ(dueDayKind(kToday, kToday), "today");
  EXPECT_EQ(dueDayKind(kToday.addDays(1), kToday), "tomorrow");
  EXPECT_EQ(dueDayKind(kToday.addDays(2), kToday), "weekday");
  EXPECT_EQ(dueDayKind(kToday.addDays(6), kToday), "weekday");
  EXPECT_EQ(dueDayKind(kToday.addDays(7), kToday), "date") << "the same weekday again needs a date";
  EXPECT_EQ(dueDayKind(kToday.addDays(-1), kToday), "date");
}

TEST(DueHorizon, GroupsByDayEarliestFirst) {
  const QVector<DueEntry> in = {
      entry(0, kToday.addDays(3)),
      entry(1, kToday),
      entry(2, kToday.addDays(1)),
      entry(3, kToday.addDays(3)),
  };
  const QVector<DueDay> days = groupDueByDay(in, kToday);
  ASSERT_EQ(days.size(), 3);
  EXPECT_EQ(days.at(0).date, kToday);
  EXPECT_EQ(days.at(0).kind, "today");
  EXPECT_EQ(days.at(0).offset, 0);
  EXPECT_EQ(days.at(1).kind, "tomorrow");
  EXPECT_EQ(days.at(2).kind, "weekday");
  EXPECT_EQ(days.at(2).offset, 3);
  EXPECT_EQ(days.at(2).members, (QVector<int>{0, 3})) << "the order they came in";
}

TEST(DueHorizon, InADayTimedFirstThenByPriority) {
  const QVector<DueEntry> in = {
      entry(10, kToday, -1, "P2"),
      entry(11, kToday, 17),
      entry(12, kToday, -1, "P0"),
      entry(13, kToday, 9, "P3"),
      entry(14, kToday, -1),
  };
  const QVector<DueDay> days = groupDueByDay(in, kToday);
  ASSERT_EQ(days.size(), 1);
  EXPECT_EQ(days.at(0).members, (QVector<int>{13, 11, 12, 10, 14}));
}

TEST(DueHorizon, LowercasePriorityRanksToo) {
  const QVector<DueEntry> in = {entry(0, kToday, -1, "p3"), entry(1, kToday, -1, "p0")};
  EXPECT_EQ(groupDueByDay(in, kToday).at(0).members, (QVector<int>{1, 0}));
}

TEST(DueHorizon, InvalidDatesAndEmptyInputGiveNoGroups) {
  EXPECT_TRUE(groupDueByDay({}, kToday).isEmpty());
  DueEntry bad;
  bad.index = 0;
  EXPECT_TRUE(groupDueByDay({bad}, kToday).isEmpty());
}
