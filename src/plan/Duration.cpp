#include "plan/Duration.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace heap::plan {

namespace {

constexpr double kMaxMinutes = 10.0 * 365 * 24 * 60;

enum class Unit { Minute, Hour, Day, Week, Month, None };

Unit unitOf(const QString& word) {
  static const QStringList minutes{QString(),
                                   QStringLiteral("m"),
                                   QStringLiteral("min"),
                                   QStringLiteral("mins"),
                                   QStringLiteral("minute"),
                                   QStringLiteral("minutes"),
                                   QStringLiteral("м"),
                                   QStringLiteral("мин"),
                                   QStringLiteral("минута"),
                                   QStringLiteral("минуты"),
                                   QStringLiteral("минут")};
  static const QStringList hours{QStringLiteral("h"),
                                 QStringLiteral("hr"),
                                 QStringLiteral("hrs"),
                                 QStringLiteral("hour"),
                                 QStringLiteral("hours"),
                                 QStringLiteral("ч"),
                                 QStringLiteral("час"),
                                 QStringLiteral("часа"),
                                 QStringLiteral("часов")};
  static const QStringList days{QStringLiteral("d"),
                                QStringLiteral("day"),
                                QStringLiteral("days"),
                                QStringLiteral("д"),
                                QStringLiteral("дн"),
                                QStringLiteral("день"),
                                QStringLiteral("дня"),
                                QStringLiteral("дней")};
  static const QStringList weeks{QStringLiteral("w"),
                                 QStringLiteral("wk"),
                                 QStringLiteral("week"),
                                 QStringLiteral("weeks"),
                                 QStringLiteral("н"),
                                 QStringLiteral("нед"),
                                 QStringLiteral("неделя"),
                                 QStringLiteral("неделю"),
                                 QStringLiteral("недели"),
                                 QStringLiteral("недель")};
  static const QStringList months{QStringLiteral("mo"),
                                  QStringLiteral("mon"),
                                  QStringLiteral("month"),
                                  QStringLiteral("months"),
                                  QStringLiteral("мес"),
                                  QStringLiteral("месяц"),
                                  QStringLiteral("месяца"),
                                  QStringLiteral("месяцев")};
  if(minutes.contains(word)) {
    return Unit::Minute;
  }
  if(hours.contains(word)) {
    return Unit::Hour;
  }
  if(days.contains(word)) {
    return Unit::Day;
  }
  if(weeks.contains(word)) {
    return Unit::Week;
  }
  if(months.contains(word)) {
    return Unit::Month;
  }
  return Unit::None;
}

}  // namespace

int dayMinutes(const DurationUnits& u) {
  const double h = u.hoursPerDay > 0 ? u.hoursPerDay : 8.0;
  return std::max(1, static_cast<int>(std::lround(h * 60.0)));
}

int weekMinutes(const DurationUnits& u) {
  const double d = u.daysPerWeek > 0 ? u.daysPerWeek : 5.0;
  return std::max(1, static_cast<int>(std::lround(d * dayMinutes(u))));
}

DurationParse parseDuration(QStringView text, const DurationUnits& u) {
  const QString s = text.toString().trimmed().toLower();
  if(s.isEmpty()) {
    return {true, 0};
  }
  // A number, then its unit with or without a space: "1d", "1 д", "1.5ч".
  static const QRegularExpression token(QStringLiteral(R"((\d+(?:[.,]\d+)?)\s*([a-zа-яё]*))"));
  double total = 0;
  qsizetype at = 0;
  auto it = token.globalMatch(s);
  bool any = false;
  while(it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    // Only spaces between the parts: "1d x 2h" or "-2h" is not a duration.
    if(!s.mid(at, m.capturedStart() - at).trimmed().isEmpty()) {
      return {};
    }
    at = m.capturedEnd();
    bool numOk = false;
    const double n = m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&numOk);
    if(!numOk) {
      return {};
    }
    double unitMin = 0;
    switch(unitOf(m.captured(2))) {
      case Unit::Minute:
        unitMin = 1;
        break;
      case Unit::Hour:
        unitMin = 60;
        break;
      case Unit::Day:
        unitMin = dayMinutes(u);
        break;
      case Unit::Week:
        unitMin = weekMinutes(u);
        break;
      case Unit::Month:
        unitMin = 4.0 * weekMinutes(u);
        break;
      case Unit::None:
        return {};
    }
    total += n * unitMin;
    if(total > kMaxMinutes) {
      return {};
    }
    any = true;
  }
  if(!any || !s.mid(at).trimmed().isEmpty()) {
    return {};
  }
  return {true, static_cast<int>(std::lround(total))};
}

QString formatDuration(int minutes, const DurationUnits& u, bool russian) {
  const int week = weekMinutes(u);
  const int day = dayMinutes(u);
  const int sizes[] = {week, day, 60, 1};
  const QString names[] = {russian ? QStringLiteral("н") : QStringLiteral("w"),
                           russian ? QStringLiteral("д") : QStringLiteral("d"),
                           russian ? QStringLiteral("ч") : QStringLiteral("h"),
                           russian ? QStringLiteral("м") : QStringLiteral("m")};
  int left = std::max(0, minutes);
  QStringList parts;
  for(int i = 0; i < 4 && parts.size() < 2; ++i) {
    const int n = left / sizes[i];
    left -= n * sizes[i];
    if(n > 0) {
      parts.append(QString::number(n) + names[i]);
    }
  }
  if(parts.isEmpty()) {
    return QStringLiteral("0") + names[3];
  }
  return parts.join(QLatin1Char(' '));
}

}  // namespace heap::plan
