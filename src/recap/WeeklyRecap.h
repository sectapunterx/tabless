#pragma once

#include "Models.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <algorithm>

// The Monday recap (WEAK PECAP): which tasks changed column last week, as
// "backlog -> in progress: these three". Pure, so the rules are tested
// without a clock or a window.
namespace heap::recap {

// Where a task started a period and where it ended it.
struct Move {
  QString taskId;
  QString from;
  QString to;

  bool operator==(const Move&) const = default;
};

// The Monday of the week `d` falls in.
inline QDate weekStart(const QDate& d) {
  return d.addDays(1 - d.dayOfWeek());
}

// Each task's net move over [start, end): the column it left first and the
// one it was in after its last move. A task that went somewhere and came back
// (or a move that was undone) ended where it began and is left out. In the
// order the tasks first moved.
inline QVector<Move> netMoves(const QVector<StatusChange>& log, const QDateTime& start, const QDateTime& end) {
  QVector<Move> out;
  QHash<QString, qsizetype> at;
  for(const StatusChange& c : log) {
    if(c.at < start || c.at >= end || c.from == c.to) {
      continue;
    }
    const auto it = at.constFind(c.taskId);
    if(it == at.constEnd()) {
      at.insert(c.taskId, out.size());
      out.append({.taskId = c.taskId, .from = c.from, .to = c.to});
    } else {
      out[*it].to = c.to;
    }
  }
  QVector<Move> moved;
  for(const Move& m : out) {
    if(m.from != m.to) {
      moved.append(m);
    }
  }
  return moved;
}

// One task's way through the columns over a period (dogfood 2026-10-30):
// the column it left first, then every column it moved to, in order —
// "In Progress → Code review → Done", the stops between included.
struct Chain {
  QString taskId;
  QStringList statuses;
  QDateTime first;  // its first and last move in the period
  QDateTime last;

  bool operator==(const Chain&) const = default;
};

// Each task's chain over [start, end), in the order the tasks first moved.
// Unlike netMoves a task that went somewhere and came back is kept: it did
// move. A logged change to the same column is no move; a change whose `from`
// is not where the chain stood (a gap in the log) adds that column too, so
// the chain never jumps.
inline QVector<Chain> statusChains(const QVector<StatusChange>& log, const QDateTime& start, const QDateTime& end) {
  QVector<Chain> out;
  QHash<QString, qsizetype> at;
  for(const StatusChange& c : log) {
    if(c.at < start || c.at >= end || c.from == c.to) {
      continue;
    }
    const auto it = at.constFind(c.taskId);
    if(it == at.constEnd()) {
      at.insert(c.taskId, out.size());
      out.append({.taskId = c.taskId, .statuses = {c.from, c.to}, .first = c.at, .last = c.at});
      continue;
    }
    Chain& ch = out[*it];
    if(ch.statuses.constLast() != c.from) {
      ch.statuses.append(c.from);
    }
    ch.statuses.append(c.to);
    ch.last = c.at;
  }
  return out;
}

// `log` without the entries older than `horizon`, so it does not grow for
// ever.
inline QVector<StatusChange> pruned(const QVector<StatusChange>& log, const QDateTime& horizon) {
  QVector<StatusChange> out;
  out.reserve(log.size());
  for(const StatusChange& c : log) {
    if(c.at >= horizon) {
      out.append(c);
    }
  }
  return out;
}

// Put the move of `taskId` that was logged at `wrongAt` at `rightAt`, the
// time the tracker says it happened (dogfood 2026-10-19: a pull logged a
// Friday move on the Monday it noticed it). Never before the task's previous
// logged move, so its from/to chain keeps its order; the log stays sorted by
// time. Whether anything changed.
inline bool retime(QVector<StatusChange>& log, const QString& taskId, const QDateTime& wrongAt, const QDateTime& rightAt) {
  if(!wrongAt.isValid() || !rightAt.isValid() || rightAt >= wrongAt) {
    return false;
  }
  qsizetype hit = -1;
  for(qsizetype i = log.size() - 1; i >= 0; --i) {
    if(log.at(i).taskId == taskId && log.at(i).at == wrongAt) {
      hit = i;
      break;
    }
  }
  if(hit < 0) {
    return false;
  }
  QDateTime at = rightAt;
  for(qsizetype i = hit - 1; i >= 0; --i) {
    if(log.at(i).taskId == taskId) {
      at = qMax(at, log.at(i).at);
      break;
    }
  }
  if(at >= wrongAt) {
    return false;
  }
  log[hit].at = at;
  std::stable_sort(log.begin(), log.end(), [](const StatusChange& a, const StatusChange& b) {
    return a.at < b.at;
  });
  return true;
}

}  // namespace heap::recap
