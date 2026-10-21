#include "plan/Duration.h"

#include <gtest/gtest.h>

using heap::plan::DurationUnits;
using heap::plan::formatDuration;
using heap::plan::parseDuration;

namespace {
int minutesOf(const char* text, DurationUnits u = {}) {
  const auto r = parseDuration(QString::fromUtf8(text), u);
  EXPECT_TRUE(r.ok) << text;
  return r.minutes;
}

bool bad(const char* text) {
  return !parseDuration(QString::fromUtf8(text), {}).ok;
}
}  // namespace

// Item 19: what Jira reads, read the same (a day 8h, a week 5d by default).
TEST(Duration, ParsesJiraUnits) {
  EXPECT_EQ(minutesOf("1d 2h"), 600);
  EXPECT_EQ(minutesOf("90m"), 90);
  EXPECT_EQ(minutesOf("1.5h"), 90);
  EXPECT_EQ(minutesOf("2w"), 2 * 5 * 8 * 60);
  EXPECT_EQ(minutesOf("1d2h"), 600);
  EXPECT_EQ(minutesOf("  3H  "), 180);
  EXPECT_EQ(minutesOf("45"), 45) << "a bare number is minutes, as the old chip read it";
  EXPECT_EQ(minutesOf("1h 30"), 90);
  EXPECT_EQ(minutesOf("2 hours 15 min"), 135);
}

TEST(Duration, ParsesRussianUnits) {
  EXPECT_EQ(minutesOf("1.5ч"), 90);
  EXPECT_EQ(minutesOf("1,5ч"), 90);
  EXPECT_EQ(minutesOf("1н 2д"), (5 + 2) * 8 * 60);
  EXPECT_EQ(minutesOf("30м"), 30) << "м is minutes, never months";
  EXPECT_EQ(minutesOf("2 часа 10 мин"), 130);
  EXPECT_EQ(minutesOf("3 дня"), 3 * 8 * 60);
  EXPECT_EQ(minutesOf("1 неделя"), 5 * 8 * 60);
}

TEST(Duration, AMonthIsFourWeeks) {
  EXPECT_EQ(minutesOf("1mo"), 4 * 5 * 8 * 60);
  EXPECT_EQ(minutesOf("1мес"), 4 * 5 * 8 * 60);
  EXPECT_EQ(minutesOf("1 месяц 1н"), 5 * 5 * 8 * 60);
}

TEST(Duration, FollowsTheSitesWorkingTime) {
  const DurationUnits seven{7.0, 5.0};
  EXPECT_EQ(minutesOf("1d", seven), 7 * 60);
  EXPECT_EQ(minutesOf("1w", seven), 5 * 7 * 60);
  EXPECT_EQ(minutesOf("1mo", seven), 4 * 5 * 7 * 60);
  const DurationUnits halfHour{7.5, 4.0};
  EXPECT_EQ(minutesOf("1d", halfHour), 450);
  EXPECT_EQ(minutesOf("1w", halfHour), 1800);
  EXPECT_EQ(minutesOf("1d", DurationUnits{0, 0}), 8 * 60) << "no config reads as Jira's defaults";
}

TEST(Duration, EmptyClearsAndJunkIsRejected) {
  const auto empty = parseDuration(u"   ", {});
  EXPECT_TRUE(empty.ok);
  EXPECT_EQ(empty.minutes, 0);
  EXPECT_TRUE(bad("soon"));
  EXPECT_TRUE(bad("2x"));
  EXPECT_TRUE(bad("-2h"));
  EXPECT_TRUE(bad("1d x 2h"));
  EXPECT_TRUE(bad("h"));
  EXPECT_TRUE(bad("1.5.5h"));
  EXPECT_TRUE(bad("99999w")) << "more than ten years";
}

TEST(Duration, FormatsTwoLargestUnits) {
  const DurationUnits u;
  EXPECT_EQ(formatDuration(450, u, true), QStringLiteral("7ч 30м"));
  EXPECT_EQ(formatDuration(600, u, true), QStringLiteral("1д 2ч"));
  EXPECT_EQ(formatDuration((5 + 2) * 480 + 3 * 60, u, true), QStringLiteral("1н 2д"));
  EXPECT_EQ(formatDuration(3 * 480 + 4 * 60 + 15, u, true), QStringLiteral("3д 4ч"));
  EXPECT_EQ(formatDuration(5 * 480 + 3 * 60, u, true), QStringLiteral("1н 3ч"));
  EXPECT_EQ(formatDuration(480, u, false), QStringLiteral("1d"));
  EXPECT_EQ(formatDuration(90, u, false), QStringLiteral("1h 30m"));
  EXPECT_EQ(formatDuration(0, u, true), QStringLiteral("0м"));
  EXPECT_EQ(formatDuration(-5, u, false), QStringLiteral("0m"));
}

TEST(Duration, FormatFollowsTheSitesWorkingTime) {
  const DurationUnits seven{7.0, 5.0};
  EXPECT_EQ(formatDuration(600, seven, true), QStringLiteral("1д 3ч"));
  EXPECT_EQ(formatDuration(35 * 60, seven, false), QStringLiteral("1w"));
  // What is typed comes back as typed.
  for(const char* t : {"1d 2h", "3h", "1w 2d", "45m"}) {
    const int m = parseDuration(QString::fromUtf8(t), seven).minutes;
    EXPECT_EQ(formatDuration(m, seven, false), QString::fromUtf8(t));
  }
}
