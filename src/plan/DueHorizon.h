#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

// The deadlines Today lists past today (dogfood 2026-10-19): "Ближайшие",
// the shown day and the N days after it, grouped by day. Pure: the shown day
// and the real today come in as arguments, so the rules are tested without a
// clock.
//
// The horizon is a number of days after the shown day, one of
// deadlineHorizonChoices(): 1 is "through tomorrow" (what Today showed before),
// 7 the default — a week ahead covers the next sprint review, the next weekly
// sync and the weekend, and still fits the side column's eight rows on a
// normal week; 14 and 30 are for the long view.
namespace heap::plan {

inline constexpr int kDefaultDeadlineHorizon = 7;

// The days the horizon can be set to, ascending.
const QVector<int>& deadlineHorizonChoices();

// A stored value as one of the choices: 0, a negative or a missing value is
// the default; anything else snaps to the nearest choice, the shorter on a tie
// (a hand-edited 5 reads as 3, 10 as 7, 365 as 30).
int clampDeadlineHorizon(int days);

// True when `due` falls on `from` or one of the `horizonDays` days after it.
bool dueWithin(const QDate& due, const QDate& from, int horizonDays);

// How a day's header names it, relative to the real today:
//   "today", "tomorrow"
//   "weekday"  — 2…6 days ahead: the weekday alone is unambiguous
//   "date"     — further, or before today
QString dueDayKind(const QDate& day, const QDate& today);

struct DueEntry {
  int index = 0;  // the caller's row
  QDateTime due;
  bool hasTime = false;
  QString priority;  // "P0" … "P3" or empty
};

struct DueDay {
  QDate date;
  int offset = 0;        // days from the real today
  QString kind;          // see dueDayKind()
  QVector<int> members;  // the entries' `index`, in display order
};

// The entries by day, the earliest day first. In a day the ones with a time
// come first, by the hour, then the ones due "by the end of the day"; ties go
// to the higher priority, then to the order they came in. Days with nothing
// are not returned.
QVector<DueDay> groupDueByDay(const QVector<DueEntry>& entries, const QDate& today);

}  // namespace heap::plan
