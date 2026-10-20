#pragma once

#include "chrono/ChronoTypes.h"
#include "text/LocaleFormat.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QTimeZone>

// "Кому написать" in time (dogfood 2026-10-20): an ask may say when to write,
// and with a time it notifies at that moment; after "Написал" it waits for
// the reply. Everything here is pure and takes `now`, so the rules are tested
// without a clock.
namespace heap::people {

// A notification whose minute passed while the app was closed still comes,
// once, on the next launch — within two days; older is only shown as late on
// the row. The sent keys live three days, longer than this.
inline constexpr int kPingLateHours = 48;

// What the "Когда написать" field said.
struct PingWhen {
  bool ok = false;
  bool empty = false;    // nothing typed: the time is cleared
  QDateTime at;          // the moment (or the day at 00:00 without a time)
  bool hasTime = false;  // a time was said: it notifies
  bool rolled = false;   // a bare time already gone today: tomorrow
  QString error;         // "unparsed" or "past" when !ok
};

// Whether the text names today itself: "сегодня 9:00" at 10:00 is a time in
// the past, not tomorrow's.
inline bool saysToday(const QString& text) {
  static const QRegularExpression kRe(QStringLiteral(R"((^|[^\p{L}])(сегодня|today)($|[^\p{L}]))"),
                                      QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  return kRe.match(text).hasMatch();
}

// The parsed field against `now`. A time with no day is today, or tomorrow
// when today's has gone; any other moment in the past is refused.
inline PingWhen resolvePingWhen(const QString& text, const heap::chrono::ParseResult& r, const QDateTime& now) {
  PingWhen out;
  if(text.trimmed().isEmpty()) {
    out.ok = true;
    out.empty = true;
    return out;
  }
  if(!r.ok || !r.start.isValid()) {
    out.error = QStringLiteral("unparsed");
    return out;
  }
  if(r.hasTime) {
    QDateTime at(r.start.date(), QTime(r.start.time().hour(), r.start.time().minute()), now.timeZone());
    if(at < now) {
      if(at.date() == now.date() && !saysToday(text)) {
        at = at.addDays(1);
        out.rolled = true;
      } else {
        out.error = QStringLiteral("past");
        return out;
      }
    }
    out.ok = true;
    out.at = at;
    out.hasTime = true;
    return out;
  }
  if(r.start.date() < now.date()) {
    out.error = QStringLiteral("past");
    return out;
  }
  out.ok = true;
  out.at = QDateTime(r.start.date(), QTime(0, 0), now.timeZone());
  return out;
}

// The chrono parser itself moves a bare time already gone today to tomorrow
// ("15:00" at 16:00). Read the same text against today's midnight too: it
// was such a roll when that reading is today at the same time and the one
// against `now` is tomorrow ("завтра 15:00" is tomorrow in both).
inline bool parserRolled(const heap::chrono::ParseResult& atNow, const heap::chrono::ParseResult& atMidnight, const QDateTime& now) {
  return atNow.ok && atMidnight.ok && atNow.hasTime && atMidnight.hasTime && atNow.start.date() == now.date().addDays(1) &&
         atMidnight.start.date() == now.date() && atMidnight.start.time() == atNow.start.time();
}

struct PingCall {
  bool due = false;
  bool late = false;  // its minute passed by more than a few minutes
  QString key;
};

inline QString pingReminderKey(const QString& personId, const QDateTime& at) {
  return QStringLiteral("ping:%1@%2").arg(personId, at.toString(Qt::ISODate));
}

// Due from the moment until kPingLateHours after it, for an ask still to
// write with a time. The key carries the moment: a new time re-arms it.
inline PingCall pingReminder(const QString& personId, const QString& state, const QDateTime& at, bool hasTime, const QDateTime& now) {
  PingCall out;
  if(state != QLatin1String("todo") || !hasTime || !at.isValid() || !now.isValid() || now < at ||
     at.secsTo(now) > 3600LL * kPingLateHours) {
    return out;
  }
  out.due = true;
  out.late = at.secsTo(now) > 5LL * 60;
  out.key = pingReminderKey(personId, at);
  return out;
}

// Waiting for a reply (state "pinged") for `days` full days: one soft nudge,
// when the setting "Напоминать, чьего ответа жду" is on. Due for two days
// after that, so a nudge is never said twice (the sent key outlives it).
inline bool waitingNudgeDue(const QString& state, const QDateTime& since, const QDateTime& now, int days) {
  if(state != QLatin1String("pinged") || !since.isValid() || !now.isValid()) {
    return false;
  }
  const QDateTime from = since.addDays(qMax(1, days));
  return from <= now && from.secsTo(now) <= 3600LL * kPingLateHours;
}

inline QString waitingNudgeKey(const QString& personId, const QDateTime& since) {
  return QStringLiteral("waitping:%1@%2").arg(personId, since.toString(Qt::ISODate));
}

// How long ago, short: "только что", "15 мин", "2 ч", "3 дн".
inline QString agoShort(const QDateTime& since, const QDateTime& now, bool ru) {
  if(!since.isValid() || !now.isValid()) {
    return {};
  }
  const qint64 secs = qMax<qint64>(0, since.secsTo(now));
  if(secs < 60) {
    return ru ? QStringLiteral("только что") : QStringLiteral("just now");
  }
  if(secs < 3600) {
    return QString::number(secs / 60) + (ru ? QStringLiteral(" мин") : QStringLiteral(" min"));
  }
  if(secs < 86400) {
    return QString::number(secs / 3600) + (ru ? QStringLiteral(" ч") : QStringLiteral(" h"));
  }
  return QString::number(secs / 86400) + (ru ? QStringLiteral(" дн") : QStringLiteral(" d"));
}

// When to write, short: "15:00" today, "завтра 15:00", "пт 15:00" within the
// week, "21.10 15:00" further; a day alone without the time.
inline QString whenShort(const QDateTime& at, bool hasTime, const QDateTime& now, bool ru, bool twelveHour) {
  if(!at.isValid() || !now.isValid()) {
    return {};
  }
  static const char* const kRu[] = {"пн", "вт", "ср", "чт", "пт", "сб", "вс"};
  static const char* const kEn[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  const qint64 days = now.date().daysTo(at.date());
  QString day;
  if(days == 0) {
    day = hasTime ? QString() : (ru ? QStringLiteral("сегодня") : QStringLiteral("today"));
  } else if(days == 1) {
    day = ru ? QStringLiteral("завтра") : QStringLiteral("tomorrow");
  } else if(days == -1) {
    day = ru ? QStringLiteral("вчера") : QStringLiteral("yesterday");
  } else if(days > 1 && days < 7) {
    day = QString::fromUtf8((ru ? kRu : kEn)[at.date().dayOfWeek() - 1]);
  } else {
    day = at.date().toString(ru ? QStringLiteral("dd.MM") : QStringLiteral("MMM d"));
  }
  if(!hasTime) {
    return day;
  }
  const QString time = heap::text::formatTime(at.time(), twelveHour);
  return day.isEmpty() ? time : day + QLatin1Char(' ') + time;
}

}  // namespace heap::people
