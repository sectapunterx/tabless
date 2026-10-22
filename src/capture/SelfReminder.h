#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

// A reminder to self said in the capture line (dogfood 2026-10-22):
// "напомни мне позвонить в банк завтра в 15:00", "remind me to check the
// pipeline in 20 minutes". The phrase marks the line as a reminder and goes;
// the rest is read as any capture line: the title and, maybe, the time.
// With no time said, the input offers a few — the rule for them is here, a
// pure function of `now` and the working hours.
namespace heap::capture {

// Where "напомни мне" sits in the line; start < 0 = no phrase.
struct RemindPhrase {
  int start = -1;
  int end = -1;  // one past the phrase and the words joining it to the rest
  QString text;  // the phrase as typed ("Напомни мне")

  bool found() const {
    return start >= 0;
  }
};

// The first "напомни [мне|себе]", "напомнить [мне|себе]", "напомните [мне]"
// or "remind me|myself" in the first line, before any "//" note, as whole
// words in any case. A "remind" with nobody after it is a task about
// reminding someone ("remind Anna about the review"); "напоминание о
// встрече" is a title. A joining "to", "that", "что", "чтобы", comma or dash
// after the phrase goes with it.
RemindPhrase findRemindPhrase(const QString& text);

// `text` without the phrase, the space around it closed up.
QString withoutRemindPhrase(const QString& text, const RemindPhrase& phrase);

// The working hours the choices are cut to. `workDays` are Qt weekday
// numbers (Mon = 1 … Sun = 7); empty = Monday to Friday.
struct WorkHours {
  int startHour = 9;
  int endHour = 18;
  QVector<int> workDays;
};

bool isWorkDay(const QDate& day, const WorkHours& w);

// The start of the first working day after `now`'s day.
QDateTime nextWorkdayStart(const QDateTime& now, const WorkHours& w);

// The next start of work: today's, while it is still ahead on a working
// day, else the next working day's.
QDateTime nextWorkStart(const QDateTime& now, const WorkHours& w);

// `at` falls on a working day between its start and end of work.
bool insideWorkHours(const QDateTime& at, const WorkHours& w);

// One of the times offered when the line said none.
struct ReminderOption {
  QString key;   // "hour" | "endOfDay" | "nextDay" | "none"
  QDateTime at;  // invalid for "none"

  bool operator==(const ReminderOption&) const = default;
};

// The choices for a reminder with no time, the default (Enter) first:
// "in an hour", unless that falls out of the working hours (the night,
// before the start, past the end, a day off) — then the next start of work
// (today's, if it is still ahead) and "in an hour" after it. The end of
// today's working day is offered only on a working day, until it passes. "none" (no time, no notification) is always last.
QVector<ReminderOption> reminderOptions(const QDateTime& now, const WorkHours& w);

// When the reminder is for, from what the rest of the line said: a reminder
// time ("напомни в 15:00"), else the date and time said. A day with no
// clock time is that day's start of work; today with no time, or nothing
// said, is invalid — the input asks.
QDateTime reminderMoment(const QDateTime& when, bool whenHasTime, const QDateTime& remind, const QDateTime& now, const WorkHours& w);

}  // namespace heap::capture
