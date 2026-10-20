#pragma once

#include "Models.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVector>

// What happened to a task (APP-165): created, moved, retitled, re-prioritised,
// re-dated, changed by a tracker pull, pushed to a tracker. Append-only and
// capped per task, so the log is a short memory, not an audit trail.
//
// Stored as one root-level key of state.json, "taskHistory", written by the
// same atomic save as everything else:
//
//   "taskHistory": { "<profileId>": { "<taskId>": [ {t, k, f, v, s}, … ] } }
//
// t = seconds since the epoch, k = kind, f/v = from/to (omitted when empty),
// s = "sync" for a change that came from or went to a tracker (omitted for a
// local one). No schema bump: a document without the key has no history, and
// a build that does not know the key carries it through a save untouched
// (root-level pass-through, PLAT-26).
namespace heap::history {

struct HistoryEvent {
  QDateTime at;
  // "created" | "status" | "title" | "priority" | "due" | "scheduled"
  // | "pushed" (a status written to the tracker)
  // | "profile" (moved to another profile; from/to are the profiles' names)
  QString kind;
  QString from;
  QString to;
  bool sync = false;  // came in with a pull, or went out with a push

  bool operator==(const HistoryEvent&) const = default;
};

inline constexpr int kMaxEventsPerTask = 50;
// Two edits of the same field by the same side this close together are one
// edit: an editor that saves as the user types must not fill the log with
// every keystroke.
inline constexpr qint64 kCoalesceSecs = 120;

// The events a change from `before` to `after` amounts to, in a fixed order.
// `before` null means the task was just created. Text values are what a
// person reads: a date as ISO (with the clock only when it is real).
QVector<HistoryEvent> diffTask(const Task* before, const Task& after, const QDateTime& now, bool sync);

class TaskHistory {
 public:
  // Appends, coalescing with the task's last event when it is the same field,
  // the same side and within kCoalesceSecs (the "from" of the first edit is
  // kept; an edit that lands back where it started removes the event). Keeps
  // at most kMaxEventsPerTask, dropping the oldest.
  void append(const QString& profileId, const QString& taskId, const HistoryEvent& e);

  QVector<HistoryEvent> events(const QString& profileId, const QString& taskId) const;
  // A task moved to another profile, or renumbered there (A5-14): its events
  // go with it, as they are, under the new key.
  void rekey(const QString& fromProfile, const QString& fromTask, const QString& toProfile, const QString& toTask);
  // Takes back the task's last event when it is of `kind` (an undone move).
  void dropLast(const QString& profileId, const QString& taskId, const QString& kind);
  // The newest pulled event of `kind` that ended in `to`, put back at `at`
  // when that is earlier (dogfood 2026-10-19: a pull stamped a tracker's move
  // with the moment it noticed it). Never before the event before it, so the
  // list keeps its order. Whether it moved.
  bool retimeLastSync(const QString& profileId, const QString& taskId, const QString& kind, const QString& to, const QDateTime& at);
  int count(const QString& profileId, const QString& taskId) const;

  void clear();
  bool isEmpty() const;

  QJsonObject toJson() const;
  static TaskHistory fromJson(const QJsonObject& o);

  // For QML: newest first, [{at, kind, from, to, sync}].
  static QVariantList toVariant(const QVector<HistoryEvent>& events);

 private:
  // profileId → taskId → events, oldest first.
  QHash<QString, QHash<QString, QVector<HistoryEvent>>> m_log;
};

}  // namespace heap::history
