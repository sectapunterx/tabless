#include "capture/SelfReminder.h"

#include <QRegularExpression>

namespace heap::capture {

RemindPhrase findRemindPhrase(const QString& text) {
  qsizetype lineEnd = text.indexOf(QLatin1Char('\n'));
  if(lineEnd < 0) {
    lineEnd = text.size();
  }
  const qsizetype note = text.left(lineEnd).indexOf(QStringLiteral("//"));
  if(note >= 0) {
    lineEnd = note;
  }
  const QString line = text.left(lineEnd);
  // The verb, then who: Russian may leave "мне" out ("напомни в 15:00"),
  // English may not ("remind Anna" is a task about Anna).
  static const QRegularExpression kPhrase(
      QStringLiteral(R"((?<![\p{L}\d])(?:(напомни|напомните|напомнить)(?:\s+(?:мне|себе))?|remind\s+(?:me|myself))(?![\p{L}\d]))"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  // What joins the phrase to the rest: "remind me to …", "напомни, что …".
  static const QRegularExpression kJoin(QStringLiteral(R"(^(?:\s*[,:—–-])*(?:\s*(?:to|that|что|чтобы)(?![\p{L}\d]))?(?:\s*[,:—–-])*)"),
                                        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  const auto m = kPhrase.match(line);
  if(!m.hasMatch()) {
    return {};
  }
  RemindPhrase out;
  out.start = static_cast<int>(m.capturedStart());
  out.text = m.captured();
  int end = static_cast<int>(m.capturedEnd());
  const auto j = kJoin.match(line.mid(end));
  if(j.hasMatch()) {
    end += static_cast<int>(j.capturedLength());
  }
  out.end = end;
  return out;
}

QString withoutRemindPhrase(const QString& text, const RemindPhrase& phrase) {
  if(!phrase.found()) {
    return text;
  }
  QString left = text.left(phrase.start);
  QString right = text.mid(phrase.end);
  while(!left.isEmpty() && left.back().isSpace() && left.back() != QLatin1Char('\n')) {
    left.chop(1);
  }
  while(!right.isEmpty() && right.front().isSpace() && right.front() != QLatin1Char('\n')) {
    right.remove(0, 1);
  }
  if(left.isEmpty() || right.isEmpty() || right.startsWith(QLatin1Char('\n'))) {
    return left + right;
  }
  return left + QLatin1Char(' ') + right;
}

bool isWorkDay(const QDate& day, const WorkHours& w) {
  if(w.workDays.isEmpty()) {
    return day.dayOfWeek() <= 5;
  }
  return w.workDays.contains(day.dayOfWeek());
}

namespace {

QTime hourTime(int hour) {
  return hour >= 24 ? QTime(23, 59) : QTime(qBound(0, hour, 23), 0);
}

}  // namespace

QDateTime nextWorkdayStart(const QDateTime& now, const WorkHours& w) {
  QDate day = now.date().addDays(1);
  for(int i = 0; i < 7 && !isWorkDay(day, w); ++i) {
    day = day.addDays(1);
  }
  // No working day in the week at all: tomorrow.
  if(!isWorkDay(day, w)) {
    day = now.date().addDays(1);
  }
  return QDateTime(day, hourTime(w.startHour));
}

QDateTime nextWorkStart(const QDateTime& now, const WorkHours& w) {
  const QDateTime todayStart(now.date(), hourTime(w.startHour));
  if(isWorkDay(now.date(), w) && now < todayStart) {
    return todayStart;
  }
  return nextWorkdayStart(now, w);
}

bool insideWorkHours(const QDateTime& at, const WorkHours& w) {
  if(!isWorkDay(at.date(), w)) {
    return false;
  }
  return at >= QDateTime(at.date(), hourTime(w.startHour)) && at <= QDateTime(at.date(), hourTime(w.endHour));
}

QVector<ReminderOption> reminderOptions(const QDateTime& now, const WorkHours& w) {
  const QDateTime minute(now.date(), QTime(now.time().hour(), now.time().minute()));
  const ReminderOption hour{QStringLiteral("hour"), minute.addSecs(3600)};
  const QDateTime dayEnd(now.date(), hourTime(w.endHour));
  const ReminderOption next{QStringLiteral("nextDay"), nextWorkStart(now, w)};
  const ReminderOption none{QStringLiteral("none"), QDateTime()};
  QVector<ReminderOption> out;
  // An hour from now out of the working hours (the night, before the
  // start, past the end, a day off): the next start of work comes first.
  const bool off = !insideWorkHours(hour.at, w);
  out.append(off ? next : hour);
  if(off) {
    out.append(hour);
  }
  // The end of a working day that is still ahead and is not the default.
  if(isWorkDay(now.date(), w) && dayEnd > now && dayEnd != next.at) {
    out.append({QStringLiteral("endOfDay"), dayEnd});
  }
  if(!off) {
    out.append(next);
  }
  out.append(none);
  return out;
}

QDateTime reminderMoment(const QDateTime& when, bool whenHasTime, const QDateTime& remind, const QDateTime& now, const WorkHours& w) {
  if(remind.isValid()) {
    return remind;
  }
  if(!when.isValid()) {
    return {};
  }
  if(whenHasTime) {
    return when;
  }
  // "в пятницу": that day, when work starts. "сегодня" alone says no time.
  if(when.date() <= now.date()) {
    return {};
  }
  return QDateTime(when.date(), hourTime(w.startHour));
}

}  // namespace heap::capture
