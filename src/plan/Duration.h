#pragma once

#include <QString>
#include <QStringView>

// An estimate typed and shown as Jira does (dogfood 2026-10-20, item 19):
// w / d / h / m, the Russian н / д / ч / м, a day and a week of the tracker's
// own working time (Jira's 8h and 5d unless the site says otherwise). Jira
// has no month; "1mo" / "1мес" is four weeks. Pure.
namespace heap::plan {

struct DurationUnits {
  double hoursPerDay = 8.0;
  double daysPerWeek = 5.0;
};

// What a day and a week are in minutes; a zero or negative config reads as
// Jira's defaults.
int dayMinutes(const DurationUnits& u);
int weekMinutes(const DurationUnits& u);

struct DurationParse {
  bool ok = false;
  int minutes = 0;
};

// "1d 2h", "90m", "1.5h", "1,5ч", "2w", "1н 2д", "1mo", "90" (bare minutes).
// Empty is ok with 0 (no estimate). Anything else, an unknown unit, a
// negative or a duration of more than ten years, is not ok.
DurationParse parseDuration(QStringView text, const DurationUnits& u);

// The two largest units that are not zero: "1н 2д", "3д 4ч", "7ч 30м",
// "0м" for nothing. Smaller units are dropped, never rounded up.
QString formatDuration(int minutes, const DurationUnits& u, bool russian);

}  // namespace heap::plan
