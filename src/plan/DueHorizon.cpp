#include "plan/DueHorizon.h"

#include <QMap>

#include <algorithm>
#include <cstdlib>

namespace heap::plan {

namespace {

int priorityRank(const QString& p) {
  if(p.size() == 2 && p.at(0).toUpper() == QLatin1Char('P') && p.at(1) >= QLatin1Char('0') && p.at(1) <= QLatin1Char('3')) {
    return p.at(1).unicode() - '0';
  }
  return 9;
}

}  // namespace

const QVector<int>& deadlineHorizonChoices() {
  static const QVector<int> choices = {1, 3, 7, 14, 30};
  return choices;
}

int clampDeadlineHorizon(int days) {
  if(days <= 0) {
    return kDefaultDeadlineHorizon;
  }
  int best = deadlineHorizonChoices().constFirst();
  for(const int c : deadlineHorizonChoices()) {
    if(std::abs(c - days) < std::abs(best - days)) {
      best = c;
    }
  }
  return best;
}

bool dueWithin(const QDate& due, const QDate& from, int horizonDays) {
  if(!due.isValid() || !from.isValid()) {
    return false;
  }
  const qint64 off = from.daysTo(due);
  return off >= 0 && off <= std::max(0, horizonDays);
}

QString dueDayKind(const QDate& day, const QDate& today) {
  const qint64 off = today.daysTo(day);
  if(off == 0) {
    return QStringLiteral("today");
  }
  if(off == 1) {
    return QStringLiteral("tomorrow");
  }
  if(off >= 2 && off <= 6) {
    return QStringLiteral("weekday");
  }
  return QStringLiteral("date");
}

QVector<DueDay> groupDueByDay(const QVector<DueEntry>& entries, const QDate& today) {
  QMap<QDate, QVector<int>> byDay;  // positions into `entries`
  for(int i = 0; i < entries.size(); ++i) {
    const QDateTime& due = entries.at(i).due;
    if(due.isValid()) {
      byDay[due.date()].append(i);
    }
  }
  QVector<DueDay> out;
  out.reserve(byDay.size());
  for(auto it = byDay.begin(); it != byDay.end(); ++it) {
    QVector<int> pos = it.value();
    std::stable_sort(pos.begin(), pos.end(), [&entries](int a, int b) {
      const DueEntry& ea = entries.at(a);
      const DueEntry& eb = entries.at(b);
      if(ea.hasTime != eb.hasTime) {
        return ea.hasTime;
      }
      if(ea.hasTime && ea.due.time() != eb.due.time()) {
        return ea.due.time() < eb.due.time();
      }
      return priorityRank(ea.priority) < priorityRank(eb.priority);
    });
    DueDay day;
    day.date = it.key();
    day.offset = static_cast<int>(today.daysTo(day.date));
    day.kind = dueDayKind(day.date, today);
    day.members.reserve(pos.size());
    for(const int p : pos) {
      day.members.append(entries.at(p).index);
    }
    out.append(day);
  }
  return out;
}

}  // namespace heap::plan
