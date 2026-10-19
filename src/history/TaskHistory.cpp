#include "history/TaskHistory.h"

#include <QJsonArray>
#include <QVariantMap>

#include <ranges>

namespace heap::history {

namespace {

// A title is kept whole up to here; the log is for recognising a change, not
// for restoring a long one.
constexpr qsizetype kMaxText = 200;

QString clip(const QString& s) {
  return s.size() > kMaxText ? s.left(kMaxText - 1) + QChar(0x2026) : s;
}

QString dateText(const QDateTime& dt, bool hasTime) {
  if(!dt.isValid()) {
    return {};
  }
  return dt.toString(hasTime ? QStringLiteral("yyyy-MM-dd HH:mm") : QStringLiteral("yyyy-MM-dd"));
}

// Only edits that arrive as a stream are merged: a title typed, a date
// nudged. A move between columns is a decision each time.
bool coalesces(const QString& kind) {
  return kind == QLatin1String("title") || kind == QLatin1String("due") || kind == QLatin1String("scheduled");
}

HistoryEvent make(const QString& kind, const QString& from, const QString& to, const QDateTime& now, bool sync) {
  return HistoryEvent{.at = now, .kind = kind, .from = from, .to = to, .sync = sync};
}

}  // namespace

QVector<HistoryEvent> diffTask(const Task* before, const Task& after, const QDateTime& now, bool sync) {
  QVector<HistoryEvent> out;
  if(before == nullptr) {
    out.append(make(QStringLiteral("created"), QString(), after.status, now, sync));
    return out;
  }
  if(before->status != after.status) {
    out.append(make(QStringLiteral("status"), before->status, after.status, now, sync));
  }
  if(before->title != after.title) {
    out.append(make(QStringLiteral("title"), clip(before->title), clip(after.title), now, sync));
  }
  if(before->priority != after.priority) {
    out.append(make(QStringLiteral("priority"), before->priority, after.priority, now, sync));
  }
  const QString dueBefore = dateText(before->dueAt, before->dueHasTime);
  const QString dueAfter = dateText(after.dueAt, after.dueHasTime);
  if(dueBefore != dueAfter) {
    out.append(make(QStringLiteral("due"), dueBefore, dueAfter, now, sync));
  }
  const QString schedBefore = dateText(before->scheduledAt, before->scheduledHasTime);
  const QString schedAfter = dateText(after.scheduledAt, after.scheduledHasTime);
  if(schedBefore != schedAfter) {
    out.append(make(QStringLiteral("scheduled"), schedBefore, schedAfter, now, sync));
  }
  return out;
}

void TaskHistory::append(const QString& profileId, const QString& taskId, const HistoryEvent& e) {
  if(taskId.isEmpty() || e.kind.isEmpty()) {
    return;
  }
  QVector<HistoryEvent>& list = m_log[profileId][taskId];
  if(!list.isEmpty() && coalesces(e.kind)) {
    HistoryEvent& last = list.last();
    const qint64 gap = last.at.secsTo(e.at);
    if(last.kind == e.kind && last.sync == e.sync && gap >= 0 && gap <= kCoalesceSecs) {
      last.to = e.to;
      last.at = e.at;
      if(last.from == last.to) {
        list.removeLast();
      }
      return;
    }
  }
  list.append(e);
  if(list.size() > kMaxEventsPerTask) {
    list.remove(0, list.size() - kMaxEventsPerTask);
  }
}

QVector<HistoryEvent> TaskHistory::events(const QString& profileId, const QString& taskId) const {
  return m_log.value(profileId).value(taskId);
}

void TaskHistory::rekey(const QString& fromProfile, const QString& fromTask, const QString& toProfile, const QString& toTask) {
  if(fromProfile == toProfile && fromTask == toTask) {
    return;
  }
  const auto p = m_log.find(fromProfile);
  if(p == m_log.end()) {
    return;
  }
  const auto t = p->find(fromTask);
  if(t == p->end()) {
    return;
  }
  QVector<HistoryEvent> moved = std::move(*t);
  p->erase(t);
  if(p->isEmpty()) {
    m_log.erase(p);
  }
  QVector<HistoryEvent>& into = m_log[toProfile][toTask];
  // Anything already under the new key (it should not be) stays after.
  moved += into;
  into = std::move(moved);
}

void TaskHistory::dropLast(const QString& profileId, const QString& taskId, const QString& kind) {
  const auto p = m_log.find(profileId);
  if(p == m_log.end()) {
    return;
  }
  const auto t = p->find(taskId);
  if(t == p->end() || t->isEmpty() || t->last().kind != kind) {
    return;
  }
  t->removeLast();
  if(t->isEmpty()) {
    p->erase(t);
  }
}

bool TaskHistory::retimeLastSync(
    const QString& profileId, const QString& taskId, const QString& kind, const QString& to, const QDateTime& at) {
  const auto p = m_log.find(profileId);
  if(p == m_log.end() || !at.isValid()) {
    return false;
  }
  const auto t = p->find(taskId);
  if(t == p->end()) {
    return false;
  }
  QVector<HistoryEvent>& list = *t;
  for(qsizetype i = list.size() - 1; i >= 0; --i) {
    HistoryEvent& e = list[i];
    if(e.kind != kind) {
      continue;
    }
    if(!e.sync || e.to != to) {
      return false;  // the newest move is not the pulled one asked about
    }
    const QDateTime floor = i > 0 ? list.at(i - 1).at : QDateTime();
    const QDateTime target = floor.isValid() ? qMax(floor, at) : at;
    if(target >= e.at) {
      return false;
    }
    e.at = target;
    return true;
  }
  return false;
}

int TaskHistory::count(const QString& profileId, const QString& taskId) const {
  return static_cast<int>(m_log.value(profileId).value(taskId).size());
}

void TaskHistory::clear() {
  m_log.clear();
}

bool TaskHistory::isEmpty() const {
  return m_log.isEmpty();
}

QJsonObject TaskHistory::toJson() const {
  QJsonObject root;
  for(auto p = m_log.constBegin(); p != m_log.constEnd(); ++p) {
    QJsonObject tasks;
    for(auto t = p->constBegin(); t != p->constEnd(); ++t) {
      if(t->isEmpty()) {
        continue;
      }
      QJsonArray arr;
      for(const HistoryEvent& e : *t) {
        QJsonObject o;
        o[QStringLiteral("t")] = static_cast<double>(e.at.toSecsSinceEpoch());
        o[QStringLiteral("k")] = e.kind;
        if(!e.from.isEmpty()) {
          o[QStringLiteral("f")] = e.from;
        }
        if(!e.to.isEmpty()) {
          o[QStringLiteral("v")] = e.to;
        }
        if(e.sync) {
          o[QStringLiteral("s")] = QStringLiteral("sync");
        }
        arr.append(o);
      }
      tasks.insert(t.key(), arr);
    }
    if(!tasks.isEmpty()) {
      root.insert(p.key(), tasks);
    }
  }
  return root;
}

TaskHistory TaskHistory::fromJson(const QJsonObject& o) {
  TaskHistory h;
  for(auto p = o.constBegin(); p != o.constEnd(); ++p) {
    const QJsonObject tasks = p.value().toObject();
    for(auto t = tasks.constBegin(); t != tasks.constEnd(); ++t) {
      QVector<HistoryEvent> list;
      for(const auto& v : t.value().toArray()) {
        const QJsonObject e = v.toObject();
        const QString kind = e.value(QStringLiteral("k")).toString();
        if(kind.isEmpty() || !e.value(QStringLiteral("t")).isDouble()) {
          continue;  // a hand edit or a later build's shape: skip, do not guess
        }
        list.append(HistoryEvent{.at = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e.value(QStringLiteral("t")).toDouble())),
                                 .kind = kind,
                                 .from = e.value(QStringLiteral("f")).toString(),
                                 .to = e.value(QStringLiteral("v")).toString(),
                                 .sync = e.value(QStringLiteral("s")).toString() == QLatin1String("sync")});
      }
      if(list.size() > kMaxEventsPerTask) {
        list.remove(0, list.size() - kMaxEventsPerTask);
      }
      if(!list.isEmpty()) {
        h.m_log[p.key()].insert(t.key(), list);
      }
    }
  }
  return h;
}

QVariantList TaskHistory::toVariant(const QVector<HistoryEvent>& events) {
  QVariantList out;
  out.reserve(events.size());
  for(const HistoryEvent& e : std::views::reverse(events)) {
    out.append(QVariantMap{
        {QStringLiteral("at"), e.at},
        {QStringLiteral("kind"), e.kind},
        {QStringLiteral("from"), e.from},
        {QStringLiteral("to"), e.to},
        {QStringLiteral("sync"), e.sync},
    });
  }
  return out;
}

}  // namespace heap::history
