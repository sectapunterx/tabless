#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QTime>

#include <algorithm>

// The sidebar keeps itself clean (dogfood 2026-10-22, Hints.dc.html): after
// a successful sync a "Ждут меня" row whose every reason resolved fades out
// and goes to "ушло N сегодня"; a row whose request could not be read stays
// as it was. "Кому написать" lets an answer go after a day or once it was
// read, an approval at once. A row can be put off until tomorrow's workday.
// The rules, with the clock passed in; AppControllerThreads.cpp applies
// them. Everything here is local: nothing is sent to a forge or a tracker.
namespace heap::waits {

// A "Ждут меня" row as it was last seen waiting.
struct Row {
  QString url;
  QString title;
  QString ref;  // "!2342" / "#12"
  QString taskId;
  QString flavor;  // "gitlab" | "github"
  int count = 0;   // threads waiting on me
  bool review = false;
  bool ci = false;  // its CI had failed
  QDateTime since;
};

// What a sync says about a request that was (or now is) on the list.
enum class Seen {
  Waits,    // read, still waits on me
  Clear,    // read, nothing waits on me any more
  Closed,   // merged or closed: off the tracker's list of open requests
  Unknown,  // not read, or the read failed: as it was
  Off,      // the feature is off, or who I am there is unknown: forget it quietly
};

struct Observation {
  Seen seen = Seen::Unknown;
  Row row;  // now: the reasons left (Clear) or the row as it is (Waits)
};

struct Gone {
  Row row;
  QString why;  // "replied" | "reviewed" | "closed" | "resolved"
  QDateTime at;
};

// The fade of a row that just went (~2 s, struck through).
inline constexpr qint64 kLeaveMs = 2000;

// Why a row left: the request closed, the review given, my answer in its
// threads, or several of those at once.
inline QString goneWhy(const Row& before, const Observation& now) {
  if(now.seen == Seen::Closed) {
    return QStringLiteral("closed");
  }
  const bool reviewed = before.review && !now.row.review;
  const bool replied = before.count > 0 && now.row.count == 0;
  if(reviewed && replied) {
    return QStringLiteral("resolved");
  }
  if(reviewed) {
    return QStringLiteral("reviewed");
  }
  if(replied) {
    return QStringLiteral("replied");
  }
  return QStringLiteral("resolved");
}

struct Result {
  QHash<QString, Row> known;
  QList<Gone> gone;
};

// After a sync: a request still waiting is remembered as it is now; one
// read and clear, or closed, leaves (into `gone` when `logGone`); one not
// read — absent from `now` or Unknown — stays as it was; Off is forgotten
// without a word. `logGone` false is a local change (a thread set aside):
// what stopped waiting by it is forgotten, not "ушло".
inline Result reconcile(const QHash<QString, Row>& known,
                        const QHash<QString, Observation>& now,
                        const QDateTime& at,
                        bool logGone = true) {
  Result r;
  for(auto it = known.constBegin(); it != known.constEnd(); ++it) {
    const auto o = now.constFind(it.key());
    if(o == now.constEnd() || o->seen == Seen::Unknown) {
      r.known.insert(it.key(), it.value());
      continue;
    }
    if((o->seen == Seen::Clear || o->seen == Seen::Closed) && logGone) {
      r.gone.append(Gone{it.value(), goneWhy(it.value(), *o), at});
    }
  }
  for(auto it = now.constBegin(); it != now.constEnd(); ++it) {
    if(it->seen == Seen::Waits) {
      r.known.insert(it.key(), it->row);
    }
  }
  return r;
}

// Still fading at `now` (it went less than kLeaveMs ago).
inline bool leaving(const Gone& g, const QDateTime& now) {
  if(!g.at.isValid()) {
    return false;
  }
  const qint64 ms = g.at.msecsTo(now);
  return ms >= 0 && ms < kLeaveMs;
}

// ---- "ушло N сегодня" ----
// What left today, and what was brought back ("вернуть": shown again until
// midnight). A new local day starts empty.
struct Day {
  QDate date;
  QList<Gone> gone;
  QSet<QString> back;  // urls brought back today
};

inline void rollDay(Day& d, const QDate& today) {
  if(d.date != today) {
    d = Day{};
    d.date = today;
  }
}

// Went now: one entry per request (the latest), not brought back any more.
inline void addGone(Day& d, const QList<Gone>& gone, const QDate& today) {
  rollDay(d, today);
  for(const Gone& g : gone) {
    d.gone.erase(std::remove_if(d.gone.begin(),
                                d.gone.end(),
                                [&](const Gone& x) {
                                  return x.row.url == g.row.url;
                                }),
                 d.gone.end());
    d.back.remove(g.row.url);
    d.gone.append(g);
  }
}

// Waits again: it is a row of the list, not "ушло".
inline void forgetGone(Day& d, const QString& url) {
  d.gone.erase(std::remove_if(d.gone.begin(),
                              d.gone.end(),
                              [&](const Gone& x) {
                                return x.row.url == url;
                              }),
               d.gone.end());
  d.back.remove(url);
}

// "ушло N": what went today and was not brought back.
inline int goneCount(const Day& d, const QDate& today) {
  if(d.date != today) {
    return 0;
  }
  int n = 0;
  for(const Gone& g : d.gone) {
    n += d.back.contains(g.row.url) ? 0 : 1;
  }
  return n;
}

inline QJsonObject rowToJson(const Row& r) {
  return {{QStringLiteral("url"), r.url},
          {QStringLiteral("title"), r.title},
          {QStringLiteral("ref"), r.ref},
          {QStringLiteral("taskId"), r.taskId},
          {QStringLiteral("flavor"), r.flavor},
          {QStringLiteral("count"), r.count},
          {QStringLiteral("review"), r.review},
          {QStringLiteral("ci"), r.ci},
          {QStringLiteral("since"), r.since.toString(Qt::ISODate)}};
}

inline Row rowFromJson(const QJsonObject& o) {
  Row r;
  r.url = o.value(QStringLiteral("url")).toString();
  r.title = o.value(QStringLiteral("title")).toString();
  r.ref = o.value(QStringLiteral("ref")).toString();
  r.taskId = o.value(QStringLiteral("taskId")).toString();
  r.flavor = o.value(QStringLiteral("flavor")).toString();
  r.count = o.value(QStringLiteral("count")).toInt();
  r.review = o.value(QStringLiteral("review")).toBool();
  r.ci = o.value(QStringLiteral("ci")).toBool();
  r.since = QDateTime::fromString(o.value(QStringLiteral("since")).toString(), Qt::ISODate);
  return r;
}

inline QJsonObject dayToJson(const Day& d) {
  if(!d.date.isValid() || (d.gone.isEmpty() && d.back.isEmpty())) {
    return {};
  }
  QJsonArray gone;
  for(const Gone& g : d.gone) {
    QJsonObject o = rowToJson(g.row);
    o.insert(QStringLiteral("why"), g.why);
    o.insert(QStringLiteral("at"), g.at.toString(Qt::ISODateWithMs));
    gone.append(o);
  }
  QJsonArray back;
  for(const QString& u : d.back) {
    back.append(u);
  }
  return {{QStringLiteral("date"), d.date.toString(Qt::ISODate)}, {QStringLiteral("gone"), gone}, {QStringLiteral("back"), back}};
}

inline Day dayFromJson(const QJsonObject& o) {
  Day d;
  d.date = QDate::fromString(o.value(QStringLiteral("date")).toString(), Qt::ISODate);
  for(const auto& v : o.value(QStringLiteral("gone")).toArray()) {
    const QJsonObject g = v.toObject();
    Gone x{rowFromJson(g),
           g.value(QStringLiteral("why")).toString(),
           QDateTime::fromString(g.value(QStringLiteral("at")).toString(), Qt::ISODateWithMs)};
    if(!x.row.url.isEmpty()) {
      d.gone.append(x);
    }
  }
  for(const auto& v : o.value(QStringLiteral("back")).toArray()) {
    d.back.insert(v.toString());
  }
  return d;
}

// ---- "Отложить до завтра" ----
// Back at the start of tomorrow's workday (Settings → Calendar, an hour),
// or at midnight when the hour makes no sense.
inline QDateTime snoozeWake(const QDateTime& now, int workdayStartHour) {
  const int h = workdayStartHour >= 0 && workdayStartHour <= 23 ? workdayStartHour : 0;
  return QDateTime(now.date().addDays(1), QTime(h, 0));
}

// The store: request URL -> wake time (ISO). Put off at `now`?
inline bool snoozed(const QJsonObject& store, const QString& url, const QDateTime& now) {
  const QDateTime wake = QDateTime::fromString(store.value(url).toString(), Qt::ISODate);
  return wake.isValid() && now < wake;
}

// The store without what woke up already.
inline QJsonObject pruneSnoozes(const QJsonObject& store, const QDateTime& now) {
  QJsonObject out;
  for(auto it = store.constBegin(); it != store.constEnd(); ++it) {
    if(snoozed(store, it.key(), now)) {
      out.insert(it.key(), it.value());
    }
  }
  return out;
}

// ---- "Кому написать": an answer leaves by itself ----
inline constexpr qint64 kAnsweredShownSecs = 24LL * 3600;

// An answered row stays a day, unless it was read (opened from the row:
// `readAt` is the answer's time then) or the answer was an approval.
inline bool answeredRowStays(const QDateTime& answeredAt, const QDateTime& readAt, const QDateTime& now, bool approved = false) {
  if(approved || !answeredAt.isValid()) {
    return false;
  }
  if(readAt.isValid() && readAt >= answeredAt) {
    return false;
  }
  return answeredAt.secsTo(now) < kAnsweredShownSecs;
}

// The read marks (row key -> the answer's time, ISO with ms) without those older
// than two days: their rows are gone anyway.
inline QJsonObject pruneRead(const QJsonObject& read, const QDateTime& now) {
  QJsonObject out;
  for(auto it = read.constBegin(); it != read.constEnd(); ++it) {
    const QDateTime at = QDateTime::fromString(it.value().toString(), Qt::ISODateWithMs);
    if(at.isValid() && at.secsTo(now) < 2 * kAnsweredShownSecs) {
      out.insert(it.key(), it.value());
    }
  }
  return out;
}

}  // namespace heap::waits
