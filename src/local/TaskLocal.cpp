#include "FieldCount.h"

#include "local/TaskLocal.h"

#include <QJsonArray>
#include <QStringList>

namespace heap::local {

namespace {

// Same guard as the task's own: a new field here fails the build until the
// JSON below learns it and tests/test_roundtrip.cpp fills it in makeFullTask().
static_assert(heap::meta::fieldCount<TaskLocal>() == 16,
              "TaskLocal gained or lost a field. Update toJson/fromJson in src/local/TaskLocal.cpp, "
              "extend makeFullTask() in tests/test_roundtrip.cpp, then bump this count.");
static_assert(heap::meta::fieldCount<LocalCheckItem>() == 6,
              "LocalCheckItem gained or lost a field. Update checklistToJson/FromJson in "
              "src/local/TaskLocal.cpp and makeFullTask() in tests/test_roundtrip.cpp.");
static_assert(heap::meta::fieldCount<LocalLink>() == 4,
              "LocalLink gained or lost a field. Update relatedToJson/FromJson in src/local/TaskLocal.cpp "
              "and makeFullTask() in tests/test_roundtrip.cpp.");
static_assert(heap::meta::fieldCount<LocalTag>() == 2,
              "LocalTag gained or lost a field. Update tagsToJson/FromJson in src/local/TaskLocal.cpp "
              "and makeFullTask() in tests/test_roundtrip.cpp.");

QString dtToStr(const QDateTime& dt) {
  return dt.isValid() ? dt.toString(Qt::ISODateWithMs) : QString();
}

QDateTime dtFromStr(const QString& s) {
  return s.isEmpty() ? QDateTime() : QDateTime::fromString(s, Qt::ISODate);
}

static_assert(heap::meta::fieldCount<TimerSession>() == 4,
              "TimerSession gained or lost a field. Update sessionsToJson/FromJson in src/local/TaskLocal.cpp "
              "and makeFullTask() in tests/test_roundtrip.cpp.");

QJsonArray sessionsToJson(const QVector<TimerSession>& xs) {
  QJsonArray a;
  for(const TimerSession& s : xs) {
    QJsonObject o;
    o["id"] = s.id;
    o["start"] = dtToStr(s.start);
    o["end"] = dtToStr(s.end);
    o["seconds"] = s.seconds;
    a.append(o);
  }
  return a;
}

QVector<TimerSession> sessionsFromJson(const QJsonArray& a) {
  QVector<TimerSession> out;
  for(const auto& v : a) {
    const QJsonObject o = v.toObject();
    TimerSession s;
    s.id = o["id"].toString();
    s.start = dtFromStr(o["start"].toString());
    s.end = dtFromStr(o["end"].toString());
    s.seconds = qMax(0, o["seconds"].toInt(0));
    out.append(s);
  }
  return out;
}

QJsonArray checklistToJson(const QVector<LocalCheckItem>& xs) {
  QJsonArray a;
  for(const LocalCheckItem& c : xs) {
    QJsonObject o;
    o["id"] = c.id;
    o["text"] = c.text;
    o["level"] = c.level;
    o["done"] = c.done;
    o["autoDone"] = c.autoDone;
    o["cardId"] = c.cardId;
    a.append(o);
  }
  return a;
}

QVector<LocalCheckItem> checklistFromJson(const QJsonArray& a) {
  QVector<LocalCheckItem> out;
  for(const auto& v : a) {
    const QJsonObject o = v.toObject();
    LocalCheckItem c;
    c.id = o["id"].toString();
    c.text = o["text"].toString();
    c.level = qMax(1, o["level"].toInt(1));
    c.done = o["done"].toBool(false);
    c.autoDone = c.done && o["autoDone"].toBool(false);
    c.cardId = o["cardId"].toString();
    out.append(c);
  }
  return out;
}

QJsonArray relatedToJson(const QVector<LocalLink>& xs) {
  QJsonArray a;
  for(const LocalLink& l : xs) {
    QJsonObject o;
    o["id"] = l.id;
    o["kind"] = l.kind;
    o["target"] = l.target;
    o["profileId"] = l.profileId;
    a.append(o);
  }
  return a;
}

QVector<LocalLink> relatedFromJson(const QJsonArray& a) {
  QVector<LocalLink> out;
  for(const auto& v : a) {
    const QJsonObject o = v.toObject();
    LocalLink l;
    l.id = o["id"].toString();
    l.kind = o["kind"].toString();
    l.target = o["target"].toString();
    l.profileId = o["profileId"].toString();
    if(!l.target.isEmpty()) {
      out.append(l);
    }
  }
  return out;
}

QJsonArray tagsToJson(const QVector<LocalTag>& xs) {
  QJsonArray a;
  for(const LocalTag& t : xs) {
    QJsonObject o;
    o["id"] = t.id;
    o["color"] = t.color;
    a.append(o);
  }
  return a;
}

QVector<LocalTag> tagsFromJson(const QJsonArray& a) {
  QVector<LocalTag> out;
  for(const auto& v : a) {
    const QJsonObject o = v.toObject();
    LocalTag t;
    t.id = o["id"].toString();
    t.color = o["color"].toString();
    if(!t.id.isEmpty()) {
      out.append(t);
    }
  }
  return out;
}

}  // namespace

void appendNote(TaskLocal& l, const QString& block) {
  if(block.trimmed().isEmpty()) {
    return;
  }
  l.notes = l.notes.isEmpty() ? block : l.notes + QStringLiteral("\n\n") + block;
}

bool isEmpty(const TaskLocal& l) {
  return l == TaskLocal{};
}

QJsonObject toJson(const TaskLocal& l, bool compact) {
  QJsonObject o = l.extra;  // unknown keys first, so every known one overwrites
  const auto put = [&](const char* key, const QJsonValue& v, bool empty) {
    if(!compact || !empty) {
      o[QLatin1String(key)] = v;
    }
  };
  put("notes", l.notes, l.notes.isEmpty());
  put("checklist", checklistToJson(l.checklist), l.checklist.isEmpty());
  put("myPriority", l.myPriority, l.myPriority.isEmpty());
  put("myDueAt", dtToStr(l.myDueAt), !l.myDueAt.isValid());
  put("myDueHasTime", l.myDueHasTime, !l.myDueHasTime);
  put("myPriorityBase", l.myPriorityBase, l.myPriorityBase.isEmpty());
  put("myDueBase", dtToStr(l.myDueBase), !l.myDueBase.isValid());
  put("tags", tagsToJson(l.tags), l.tags.isEmpty());
  put("related", relatedToJson(l.related), l.related.isEmpty());
  put("commentDraft", l.commentDraft, l.commentDraft.isEmpty());
  put("doneFrom", l.doneFrom, l.doneFrom.isEmpty());
  put("sessions", sessionsToJson(l.sessions), l.sessions.isEmpty());
  put("remindAt", dtToStr(l.remindAt), !l.remindAt.isValid());
  put("remindLead", l.remindLead, l.remindLead < 0);
  put("reminder", l.reminder, !l.reminder);
  return o;
}

TaskLocal fromJson(const QJsonObject& o) {
  TaskLocal l;
  l.notes = o["notes"].toString();
  l.checklist = checklistFromJson(o["checklist"].toArray());
  l.myPriority = o["myPriority"].toString();
  l.myDueAt = dtFromStr(o["myDueAt"].toString());
  l.myDueHasTime = l.myDueAt.isValid() && o["myDueHasTime"].toBool(false);
  l.myPriorityBase = o["myPriorityBase"].toString();
  l.myDueBase = dtFromStr(o["myDueBase"].toString());
  l.tags = tagsFromJson(o["tags"].toArray());
  l.related = relatedFromJson(o["related"].toArray());
  l.commentDraft = o["commentDraft"].toString();
  l.doneFrom = o["doneFrom"].toString();
  l.sessions = sessionsFromJson(o["sessions"].toArray());
  l.remindAt = dtFromStr(o["remindAt"].toString());
  l.remindLead = qMax(-1, o["remindLead"].toInt(-1));
  l.reminder = o["reminder"].toBool(false);
  static const QStringList kKnown = {QStringLiteral("notes"),
                                     QStringLiteral("checklist"),
                                     QStringLiteral("myPriority"),
                                     QStringLiteral("myDueAt"),
                                     QStringLiteral("myDueHasTime"),
                                     QStringLiteral("myPriorityBase"),
                                     QStringLiteral("myDueBase"),
                                     QStringLiteral("tags"),
                                     QStringLiteral("related"),
                                     QStringLiteral("commentDraft"),
                                     QStringLiteral("doneFrom"),
                                     QStringLiteral("sessions"),
                                     QStringLiteral("remindAt"),
                                     QStringLiteral("remindLead"),
                                     QStringLiteral("reminder")};
  for(auto it = o.begin(); it != o.end(); ++it) {
    if(!kKnown.contains(it.key())) {
      l.extra.insert(it.key(), it.value());
    }
  }
  return l;
}

}  // namespace heap::local
