#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>

// One day as the Today screen and the calendars draw it (APP-260/247): the
// meetings and the task blocks by the hour, the free windows between them as
// a fact, and how much of the working day is taken. A pure function of what
// it is handed and `now`: it shows, it never places anything.
namespace heap::plan {

// A meeting occurrence that touches the day. Hours are wall-clock, 0..24;
// a meeting that crosses midnight has `endDate` after `date`.
struct EventIn {
  QString id;
  QString title;
  QString type;  // none / standup / oneone / sync / focus
  QString taskId;
  QString attendees;
  QDate date;
  QDate endDate;
  double start = 0;
  double end = 0;
  bool allDay = false;
  QString occurrence;  // the occurrence's date, ISO, for opening it
  QString srcTz;       // kept in another zone: that zone, and the start there
  double srcStart = 0;
};

// A task with a time on the day.
struct TaskIn {
  QString id;
  QString title;
  QString status;
  QString category;  // the column's stage
  QDateTime scheduledAt;
  int minutes = 60;     // the block's length: the estimate, else the setting
  QString profileName;  // when the day shows every profile
  QString profileId;    // the same, by id (A1-17)
};

struct Block {
  QString kind;  // "meeting" or "task"
  QString id;
  QString title;
  QString eventType;
  QString status;
  QString category;
  QString attendees;
  QString occurrence;
  QString profileName;
  QString profileId;  // another profile's task when the day shows them all
  double start = 0;  // hours on this day, may run below 0 / past 24 for display
  double end = 0;
  bool past = false;
  // A meeting going on at `now` (start <= now < end): the card itself is
  // "now", and the day draws no separate now-line while one is.
  bool ongoing = false;
  bool fromPrevDay = false;  // began the day before ("since 23:00")
  bool toNextDay = false;    // runs into the next day ("until 01:00")
  // The whole meeting, not the part on this day (A3-3): where a meeting
  // across midnight really begins and ends, and how long it is in all.
  QString srcTz;  // a meeting kept in another zone, its start there (A6-4)
  double srcStart = 0;
  double realStart = 0;
  double realEnd = 0;
  int totalMinutes = 0;
  // A meeting over several days, drawn all-day (A6-17): its first and last
  // day, and which of them this one is (1-based).
  QDate firstDay;
  QDate lastDay;
  int dayNo = 0;
  int dayCount = 0;
  QStringList overlapsWith;  // titles of what shares its time
};

struct Gap {
  double start = 0;
  double end = 0;
};

// Minutes, each counted once however many things overlap.
struct Load {
  int meetings = 0;
  int tasks = 0;     // task time not already under a meeting
  int free = 0;      // working time left
  int overWork = 0;  // planned beyond the working hours
};

struct Day {
  QDate date;
  bool workday = true;
  double workStart = 9;
  double workEnd = 19;
  // Lunch (APP-319) on a working day, inside the working hours; -1 when there
  // is none. Not free time: the load and the free windows leave it out.
  double lunchStart = -1;
  double lunchEnd = -1;
  // The hours the list spans: the working day, widened for anything outside.
  double fromHour = 9;
  double toHour = 19;
  QVector<Block> allDay;  // all-day and multi-day meetings, above the hours
  QVector<Block> blocks;  // by start time
  QVector<Gap> free;      // working-time gaps of at least `minGap` minutes
  Load load;
};

Day buildDay(const QDate& date,
             const QDateTime& now,
             const QVector<EventIn>& events,
             const QVector<TaskIn>& tasks,
             double workStart,
             double workEnd,
             bool workday,
             std::pair<double, double> lunch = {-1, -1},
             int minGap = 30);

}  // namespace heap::plan
