#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

// When CI of my merge / pull request is news (dogfood 2026-10-20, item 1).
// Read only from what the app already fetched — a tracker pull's CI and the
// git watcher's PR facts; nothing here asks a forge anything. Pure: `now` is
// an argument, the store is a plain map AppController keeps in ci-seen.json.
namespace heap::ci {

// One word for a run's state from either source: "success", "failed",
// "running", "canceled" or "" (unknown). The git watcher says "passing",
// "failing", "pending".
inline QString normalState(const QString& s) {
  if(s == QLatin1String("success") || s == QLatin1String("passing")) {
    return QStringLiteral("success");
  }
  if(s == QLatin1String("failed") || s == QLatin1String("failing")) {
    return QStringLiteral("failed");
  }
  if(s == QLatin1String("running") || s == QLatin1String("pending")) {
    return QStringLiteral("running");
  }
  if(s == QLatin1String("canceled")) {
    return QStringLiteral("canceled");
  }
  return {};
}

// The same merge request from a tracker pull and from the git watcher.
inline QString keyOf(const QString& url) {
  QString k = url.trimmed().toLower();
  while(k.endsWith(QLatin1Char('/'))) {
    k.chop(1);
  }
  return k;
}

// What was last seen of one merge request's CI: the run (its head commit),
// the state, and when it was seen (for pruning).
struct Seen {
  QString run;
  QString state;
  QDateTime at;
};

// What a source sees now.
struct Observation {
  QString key;           // keyOf(the merge request's URL)
  QString run;           // head commit; "" when the source cannot say
  QString state;         // normalState()
  QDateTime finishedAt;  // when the run last changed, if known
  QString title;         // "!7 cache the build"
  QString taskId;        // the card a click opens ("" = none)
};

enum class News : std::uint8_t { None, Failed, Passed };

// A run that finished longer ago than this is not news any more: the app was
// closed for the weekend, say.
inline constexpr qint64 kStaleSecs = 12LL * 3600;
// Records of merge requests not seen for this long are dropped.
inline constexpr qint64 kForgetSecs = 30LL * 24 * 3600;

// Whether a run's change is news. Never from nothing (the first time a merge
// request is seen, after an install or an upgrade), never for the same state
// of the same run twice (a restart, two sources), never for a run still
// going or cancelled. A run is new when its head commit moved.
inline News newsOf(const std::optional<Seen>& before, const Observation& now, const QDateTime& clock) {
  if(!before.has_value() || now.state.isEmpty()) {
    return News::None;
  }
  if(now.state != QLatin1String("failed") && now.state != QLatin1String("success")) {
    return News::None;
  }
  if(now.finishedAt.isValid() && clock.isValid() && now.finishedAt.secsTo(clock) > kStaleSecs) {
    return News::None;
  }
  const bool sameRun = before->run.isEmpty() || now.run.isEmpty() || before->run == now.run;
  if(sameRun && before->state == now.state) {
    return News::None;
  }
  return now.state == QLatin1String("failed") ? News::Failed : News::Passed;
}

// What the store keeps after `now` is seen: an unknown state leaves the
// record as it was (an error reading CI is not "no CI").
inline std::optional<Seen> nextSeen(const std::optional<Seen>& before, const Observation& now, const QDateTime& clock) {
  if(now.state.isEmpty()) {
    return before;
  }
  return Seen{now.run.isEmpty() && before.has_value() ? before->run : now.run, now.state, clock};
}

// One notification of a batch.
struct Message {
  bool failed = false;  // any failed in it: the louder kind ("ci", else "ciPassed")
  int failedCount = 0;
  int passedCount = 0;
  QStringList lines;  // "!7 cache the build"
  QString taskId;     // what a click opens: the first failed, else the first
};

// The notifications of one batch: failed ones together, passed ones
// together. `summary` (the first batch after launch: runs that finished while
// the app was closed) folds everything into one.
inline QVector<Message> compose(const QVector<std::pair<Observation, News>>& news, bool summary) {
  Message failed;
  failed.failed = true;
  Message passed;
  for(const auto& [o, n] : news) {
    if(n == News::Failed) {
      ++failed.failedCount;
      failed.lines.append(o.title);
      if(failed.taskId.isEmpty()) {
        failed.taskId = o.taskId;
      }
    } else if(n == News::Passed) {
      ++passed.passedCount;
      passed.lines.append(o.title);
      if(passed.taskId.isEmpty()) {
        passed.taskId = o.taskId;
      }
    }
  }
  QVector<Message> out;
  if(summary && failed.failedCount > 0 && passed.passedCount > 0) {
    failed.passedCount = passed.passedCount;
    failed.lines.append(passed.lines);
    out.append(failed);
    return out;
  }
  if(failed.failedCount > 0) {
    out.append(failed);
  }
  if(passed.passedCount > 0) {
    out.append(passed);
  }
  return out;
}

// ci-seen.json: {key: {run, state, at}}, the stale ones left out.
inline QHash<QString, Seen> seenFromJson(const QJsonObject& o, const QDateTime& clock) {
  QHash<QString, Seen> out;
  for(auto it = o.constBegin(); it != o.constEnd(); ++it) {
    const QJsonObject r = it.value().toObject();
    Seen s{r.value(QStringLiteral("run")).toString(),
           r.value(QStringLiteral("state")).toString(),
           QDateTime::fromString(r.value(QStringLiteral("at")).toString(), Qt::ISODate)};
    if(s.at.isValid() && s.at.secsTo(clock) > kForgetSecs) {
      continue;
    }
    out.insert(it.key(), s);
  }
  return out;
}

inline QJsonObject seenToJson(const QHash<QString, Seen>& seen) {
  QJsonObject o;
  for(auto it = seen.constBegin(); it != seen.constEnd(); ++it) {
    o.insert(
        it.key(),
        QJsonObject{
            {QStringLiteral("run"), it->run}, {QStringLiteral("state"), it->state}, {QStringLiteral("at"), it->at.toString(Qt::ISODate)}});
  }
  return o;
}

}  // namespace heap::ci
