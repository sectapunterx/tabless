#include "FieldCount.h"
#include "StateSerializer.h"

#include "board/ColumnCategory.h"

#include <QColor>
#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTime>

#include <algorithm>
#include <functional>

namespace heap::state {

namespace {

// Every declared field of Task and CalEvent has to survive toJson→fromJson. The
// counts below are the guard: add a field and this build fails until the
// serializers here, the ones in sync/SyncSerializer.cpp, and the round-trip
// fixture in tests/test_roundtrip.cpp all learn about it.
static_assert(heap::meta::fieldCount<TaskLink>() == 2,
              "TaskLink gained or lost a field. Update linksToJson/linksFromJson here AND in "
              "src/sync/SyncSerializer.cpp, extend makeFullTask() in tests/test_roundtrip.cpp, "
              "then bump this count.");
static_assert(heap::meta::fieldCount<Attachment>() == 4,
              "Attachment gained or lost a field. Update attachmentsToJson/attachmentsFromJson here AND in "
              "src/sync/SyncSerializer.cpp, extend makeFullTask() in tests/test_roundtrip.cpp, "
              "then bump this count.");
static_assert(heap::meta::fieldCount<Task>() == 28,
              "Task gained or lost a field. Update taskToJson/taskFromJson here AND in "
              "src/sync/SyncSerializer.cpp, extend makeFullTask() in tests/test_roundtrip.cpp, "
              "then bump this count.");
static_assert(heap::meta::fieldCount<ExternalMeta>() == 22,
              "ExternalMeta gained or lost a field. Update externalMetaToJson/FromJson here AND "
              "in src/sync/SyncSerializer.cpp, extend makeFullTask() in tests/test_roundtrip.cpp, "
              "then bump this count.");
static_assert(heap::meta::fieldCount<CalEvent>() == 22,
              "CalEvent gained or lost a field. Update eventToJson/eventFromJson here AND in "
              "src/sync/SyncSerializer.cpp, extend makeFullEvent() in tests/test_roundtrip.cpp, "
              "then bump this count.");

// Milliseconds are written so a QDateTime round-trips exactly; Qt::ISODate on the
// read side accepts both the fractional and the whole-second form, so files
// written before this change still parse.
QString dtToStr(const QDateTime& dt) {
  return dt.isValid() ? dt.toString(Qt::ISODateWithMs) : QString();
}

QDateTime dtFromStr(const QString& s) {
  return s.isEmpty() ? QDateTime() : QDateTime::fromString(s, Qt::ISODate);
}

QJsonArray labelsToJson(const QVector<Label>& labels) {
  QJsonArray a;
  for(const Label& l : labels) {
    QJsonObject o;
    o["id"] = l.id;
    o["color"] = l.color;
    a.append(o);
  }
  return a;
}

// The keys of `o` this build does not read (PLAT-15). Each object type that
// carries an `extra` lists every key its toJson writes and its fromJson reads,
// legacy ones included; anything else comes back on the next save. A key added
// to a toJson must be listed too, or a stale copy of it would return from
// `extra` whenever the new code omits it at its default.
QJsonObject unknownKeys(const QJsonObject& o, const QStringList& known) {
  QJsonObject out;
  for(auto it = o.constBegin(); it != o.constEnd(); ++it) {
    if(!known.contains(it.key())) {
      out.insert(it.key(), it.value());
    }
  }
  return out;
}

QVector<Label> labelsFromJson(const QJsonArray& a) {
  QVector<Label> out;
  out.reserve(a.size());
  for(const auto& it : a) {
    const QJsonObject o = it.toObject();
    out.append(Label{o["id"].toString(), o["color"].toString()});
  }
  return out;
}

QJsonArray linksToJson(const QVector<TaskLink>& links) {
  QJsonArray a;
  for(const TaskLink& l : links) {
    QJsonObject o;
    o["type"] = l.type;
    o["targetId"] = l.targetId;
    a.append(o);
  }
  return a;
}

QVector<TaskLink> linksFromJson(const QJsonArray& a) {
  QVector<TaskLink> out;
  out.reserve(a.size());
  for(const auto& it : a) {
    const QJsonObject o = it.toObject();
    out.append(TaskLink{o["type"].toString(), o["targetId"].toString()});
  }
  return out;
}

QJsonArray attachmentsToJson(const QVector<Attachment>& xs) {
  QJsonArray a;
  for(const Attachment& x : xs) {
    QJsonObject o;
    o["id"] = x.id;
    o["name"] = x.name;
    o["size"] = static_cast<double>(x.size);
    o["mime"] = x.mime;
    a.append(o);
  }
  return a;
}

QVector<Attachment> attachmentsFromJson(const QJsonArray& a) {
  QVector<Attachment> out;
  out.reserve(a.size());
  for(const auto& it : a) {
    const QJsonObject o = it.toObject();
    out.append(Attachment{o["id"].toString(), o["name"].toString(), static_cast<qint64>(o["size"].toDouble(0)), o["mime"].toString()});
  }
  return out;
}

}  // namespace

// ───────────────── Task ─────────────────

namespace {

// The tracker's own view of a mirrored issue (HEAP-117). Nested rather than
// flat, and the timestamps are deliberately NOT called `updatedAt`/`createdAt`:
// JsonMerger treats a task's top-level keys by those names as heap's own
// last-write clock, and a device that merely re-pulled would then outrank one
// that actually edited. Default sub-keys are omitted, and the whole object is
// omitted when nothing in it is set, so a local task's JSON is unchanged.
QJsonObject externalMetaToJson(const ExternalMeta& m) {
  QJsonObject o;
  if(!m.author.isEmpty()) {
    o["author"] = m.author;
  }
  if(!m.issueType.isEmpty()) {
    o["issueType"] = m.issueType;
  }
  if(!m.project.isEmpty()) {
    o["project"] = m.project;
  }
  if(!m.milestone.isEmpty()) {
    o["milestone"] = m.milestone;
  }
  if(m.commentCount >= 0) {
    o["commentCount"] = m.commentCount;
  }
  if(m.createdAt.isValid()) {
    o["remoteCreatedAt"] = dtToStr(m.createdAt);
  }
  if(m.updatedAt.isValid()) {
    o["remoteUpdatedAt"] = dtToStr(m.updatedAt);
  }
  if(m.dueAt.isValid()) {
    o["remoteDueAt"] = dtToStr(m.dueAt);
  }
  if(m.crossProject) {
    o["crossProject"] = true;
  }
  if(!m.status.isEmpty()) {
    o["remoteStatus"] = m.status;
  }
  if(!m.title.isEmpty()) {
    o["remoteTitle"] = m.title;
  }
  if(!m.body.isEmpty()) {
    o["remoteBody"] = m.body;
  }
  if(!m.column.isEmpty()) {
    o["remoteColumn"] = m.column;
  }
  if(!m.unsyncedStatus.isEmpty()) {
    o["unsyncedStatus"] = m.unsyncedStatus;
  }
  if(m.goneUpstream) {
    o["goneUpstream"] = true;
  }
  if(!m.scope.isEmpty()) {
    o["remoteScope"] = m.scope;
  }
  if(m.outOfScope) {
    o["outOfScope"] = true;
  }
  if(!m.priority.isEmpty()) {
    o["remotePriority"] = m.priority;
  }
  if(!m.labels.isEmpty()) {
    o["remoteLabels"] = QJsonArray::fromStringList(m.labels);
  }
  if(!m.conflicts.isEmpty()) {
    o["conflicts"] = QJsonArray::fromStringList(m.conflicts);
  }
  if(m.pushQueued) {
    o["pushQueued"] = true;
  }
  if(!m.details.isEmpty()) {
    o["remoteDetails"] = m.details;
  }
  return o;
}

QStringList stringsFromJson(const QJsonValue& v) {
  QStringList out;
  for(const QJsonValue& s : v.toArray()) {
    if(s.isString()) {
      out.append(s.toString());
    }
  }
  return out;
}

ExternalMeta externalMetaFromJson(const QJsonObject& o) {
  ExternalMeta m;
  m.author = o["author"].toString();
  m.issueType = o["issueType"].toString();
  m.project = o["project"].toString();
  m.milestone = o["milestone"].toString();
  m.commentCount = o["commentCount"].toInt(-1);
  m.createdAt = dtFromStr(o["remoteCreatedAt"].toString());
  m.updatedAt = dtFromStr(o["remoteUpdatedAt"].toString());
  m.dueAt = dtFromStr(o["remoteDueAt"].toString());
  m.crossProject = o["crossProject"].toBool(false);
  m.status = o["remoteStatus"].toString();
  m.title = o["remoteTitle"].toString();
  m.body = o["remoteBody"].toString();
  m.column = o["remoteColumn"].toString();
  m.unsyncedStatus = o["unsyncedStatus"].toString();
  m.goneUpstream = o["goneUpstream"].toBool(false);
  m.scope = o["remoteScope"].toString();
  m.outOfScope = o["outOfScope"].toBool(false);
  m.priority = o["remotePriority"].toString();
  m.labels = stringsFromJson(o["remoteLabels"]);
  m.conflicts = stringsFromJson(o["conflicts"]);
  m.pushQueued = o["pushQueued"].toBool(false);
  m.details = o["remoteDetails"].toObject();
  return m;
}

}  // namespace

QJsonObject taskToJson(const Task& t) {
  // Unknown keys first, so every key this build owns overwrites a stale copy.
  QJsonObject o = t.extra;
  o["id"] = t.id;
  o["title"] = t.title;
  o["desc"] = t.desc;
  o["priority"] = t.priority;
  o["status"] = t.status;
  o["branch"] = t.branch;
  o["statusChangedAt"] = dtToStr(t.statusChangedAt);
  o["archived"] = t.archived;
  // Scheduling (HEAP-115) — omitted when unset so an undated task's JSON stays
  // as small as it was when `deadline` was a bare date.
  if(t.scheduledAt.isValid()) {
    o["scheduledAt"] = dtToStr(t.scheduledAt);
  }
  if(t.dueAt.isValid()) {
    o["dueAt"] = dtToStr(t.dueAt);
  }
  if(t.scheduledHasTime) {
    o["scheduledHasTime"] = true;
  }
  if(t.dueHasTime) {
    o["dueHasTime"] = true;
  }
  // Time tracking (HEAP-78) — omitted when zero/stopped so untimed task JSON
  // stays byte-identical.
  if(t.trackedSeconds > 0) {
    o["trackedSeconds"] = t.trackedSeconds;
  }
  if(t.timerStartedAt.isValid()) {
    o["timerStartedAt"] = dtToStr(t.timerStartedAt);
  }
  // Recurrence (HEAP-77) — omitted for non-recurring tasks.
  if(!t.recurrence.isEmpty()) {
    o["recurrence"] = t.recurrence;
  }
  // Tracker-sync link — only emitted for synced tasks so locally-created
  // task JSON stays byte-identical to before HEAP-74.
  if(!t.externalId.isEmpty()) {
    o["externalId"] = t.externalId;
    o["externalUrl"] = t.externalUrl;
    o["externalProvider"] = t.externalProvider;
  }
  // Planning fields (HEAP-124).
  if(!t.labels.isEmpty()) {
    o["labels"] = labelsToJson(t.labels);
  }
  if(t.estimateMinutes > 0) {
    o["estimateMinutes"] = t.estimateMinutes;
  }
  if(t.someday) {
    o["someday"] = true;
  }
  if(!t.assignee.isEmpty()) {
    o["assignee"] = t.assignee;
  }
  // Tracker metadata (HEAP-117) — omitted entirely for a local task, so its
  // JSON stays byte-identical to before.
  const QJsonObject meta = externalMetaToJson(t.externalMeta);
  if(!meta.isEmpty()) {
    o["externalMeta"] = meta;
  }
  // Board ordering + dependencies (schema v5). Omitted at their defaults so a
  // task that has never been reordered or linked keeps the JSON it had before.
  if(t.rank != 0.0) {
    o["rank"] = t.rank;
  }
  if(!t.links.isEmpty()) {
    o["links"] = linksToJson(t.links);
  }
  // Attachments (schema v11) — optional: a file without the key simply has
  // none, and a task with none keeps the JSON it had before.
  if(!t.attachments.isEmpty()) {
    o["attachments"] = attachmentsToJson(t.attachments);
  }
  // The local layer (schema v12, APP-244) — omitted while empty.
  if(!heap::local::isEmpty(t.local)) {
    o["local"] = heap::local::toJson(t.local, /*compact=*/true);
  }
  return o;
}

Task taskFromJson(const QJsonObject& o) {
  Task t;
  t.id = o["id"].toString();
  t.title = o["title"].toString();
  t.desc = o["desc"].toString();
  t.priority = o["priority"].toString();
  t.status = o["status"].toString();
  t.branch = o["branch"].toString();
  // Missing stays missing: stamping the load time made every such task
  // "closed today" in the week recap after each restart, and the first save
  // made the guess permanent (dogfood 2026-10-19). A tracker card gets the
  // real time from the tracker on its next sync.
  t.statusChangedAt = dtFromStr(o["statusChangedAt"].toString());
  t.archived = o["archived"].toBool(false);
  t.scheduledAt = dtFromStr(o["scheduledAt"].toString());
  t.dueAt = dtFromStr(o["dueAt"].toString());
  if(o.contains("dueHasTime") || o.contains("scheduledHasTime")) {
    t.scheduledHasTime = t.scheduledAt.isValid() && o["scheduledHasTime"].toBool(false);
    t.dueHasTime = t.dueAt.isValid() && o["dueHasTime"].toBool(false);
  } else {
    // A profile exported by a schema ≤ 9 build: imports carry no ladder.
    applyLegacyHasTime(t, o["hasTime"].toBool(false));
  }
  // Legacy bare-date deadline (schema ≤ 3, and any profile exported by an older
  // build). state.json itself is migrated by migrateState() before it reaches
  // here; this covers imports, which carry no schema ladder.
  if(!t.scheduledAt.isValid() && !t.dueAt.isValid()) {
    const QDate legacy = QDate::fromString(o["deadline"].toString(), Qt::ISODate);
    if(legacy.isValid()) {
      t.scheduledAt = QDateTime(legacy, QTime(0, 0));
      t.dueAt = t.scheduledAt;
      t.scheduledHasTime = false;
      t.dueHasTime = false;
    }
  }
  t.trackedSeconds = o["trackedSeconds"].toInt(0);
  t.timerStartedAt = dtFromStr(o["timerStartedAt"].toString());
  t.recurrence = o["recurrence"].toString();
  t.externalId = o["externalId"].toString();
  t.externalUrl = o["externalUrl"].toString();
  t.externalProvider = o["externalProvider"].toString();
  t.labels = labelsFromJson(o["labels"].toArray());
  t.estimateMinutes = o["estimateMinutes"].toInt(0);
  t.someday = o["someday"].toBool(false);
  t.assignee = o["assignee"].toString();
  t.externalMeta = externalMetaFromJson(o["externalMeta"].toObject());
  t.rank = o["rank"].toDouble(0.0);
  t.links = linksFromJson(o["links"].toArray());
  t.attachments = attachmentsFromJson(o["attachments"].toArray());
  t.local = heap::local::fromJson(o["local"].toObject());
  static const QStringList kKnown = {QStringLiteral("id"),
                                     QStringLiteral("title"),
                                     QStringLiteral("desc"),
                                     QStringLiteral("priority"),
                                     QStringLiteral("status"),
                                     QStringLiteral("branch"),
                                     QStringLiteral("statusChangedAt"),
                                     QStringLiteral("archived"),
                                     QStringLiteral("scheduledAt"),
                                     QStringLiteral("dueAt"),
                                     QStringLiteral("scheduledHasTime"),
                                     QStringLiteral("dueHasTime"),
                                     QStringLiteral("trackedSeconds"),
                                     QStringLiteral("timerStartedAt"),
                                     QStringLiteral("recurrence"),
                                     QStringLiteral("externalId"),
                                     QStringLiteral("externalUrl"),
                                     QStringLiteral("externalProvider"),
                                     QStringLiteral("labels"),
                                     QStringLiteral("estimateMinutes"),
                                     QStringLiteral("someday"),
                                     QStringLiteral("assignee"),
                                     QStringLiteral("externalMeta"),
                                     QStringLiteral("rank"),
                                     QStringLiteral("links"),
                                     QStringLiteral("attachments"),
                                     QStringLiteral("local"),
                                     // Read, never written: schema ≤ 3 and ≤ 9.
                                     QStringLiteral("deadline"),
                                     QStringLiteral("hasTime")};
  t.extra = unknownKeys(o, kKnown);
  return t;
}

QJsonArray tasksToJson(const QVector<Task>& xs) {
  QJsonArray a;
  for(const Task& t : xs) {
    a.append(taskToJson(t));
  }
  return a;
}

QVector<Task> tasksFromJson(const QJsonArray& a) {
  QVector<Task> v;
  v.reserve(a.size());
  for(const auto& it : a) {
    v.append(taskFromJson(it.toObject()));
  }
  return v;
}

// ───────────────── CalEvent ─────────────────

// The dates a recurring event has had deleted out of it. Stored as a plain
// array of ISO strings; anything unparseable is dropped rather than kept as an
// invalid date that would never match an occurrence.
QJsonArray datesToJson(const QVector<QDate>& xs) {
  QJsonArray a;
  for(const QDate& d : xs) {
    if(d.isValid()) {
      a.append(d.toString(Qt::ISODate));
    }
  }
  return a;
}

QVector<QDate> datesFromJson(const QJsonArray& a) {
  QVector<QDate> v;
  v.reserve(a.size());
  for(const auto& it : a) {
    const QDate d = QDate::fromString(it.toString(), Qt::ISODate);
    if(d.isValid()) {
      v.append(d);
    }
  }
  return v;
}

QJsonObject eventToJson(const CalEvent& e) {
  QJsonObject o = e.extra;  // unknown keys first (PLAT-15)
  o["id"] = e.id;
  o["title"] = e.title;
  o["type"] = e.type;
  o["start"] = e.start;
  o["end"] = e.end;
  o["attendees"] = e.attendees;
  o["date"] = e.date.isValid() ? e.date.toString(Qt::ISODate) : QString();
  o["taskId"] = e.taskId;
  o["profileId"] = e.profileId;
  o["context"] = e.context;
  o["allDay"] = e.allDay;
  // Written even when absent so the key set matches the field count; an empty
  // string reads back as an invalid date, which means "single day".
  o["endDate"] = e.endDate.isValid() ? e.endDate.toString(Qt::ISODate) : QString();
  o["rrule"] = e.rrule;
  o["exdates"] = datesToJson(e.exdates);
  o["masterId"] = e.masterId;
  o["originalDate"] = e.originalDate.isValid() ? e.originalDate.toString(Qt::ISODate) : QString();
  // Optional since 0.5.3 (audit-time); an older file lacks them and reads the
  // defaults — floating local time, no notes, the settings' reminder lead.
  o["tz"] = e.tz;
  o["location"] = e.location;
  o["notes"] = e.notes;
  o["url"] = e.url;
  o["reminderMinutes"] = e.reminderMinutes;
  return o;
}

CalEvent eventFromJson(const QJsonObject& o, const QString& fallbackProfileId) {
  CalEvent e;
  e.id = o["id"].toString();
  e.title = o["title"].toString();
  e.type = o["type"].toString();
  e.start = o["start"].toDouble();
  e.end = o["end"].toDouble();
  e.attendees = o["attendees"].toString();
  e.date = QDate::fromString(o["date"].toString(), Qt::ISODate);
  e.taskId = o["taskId"].toString();
  e.profileId = o.contains("profileId") ? o["profileId"].toString() : fallbackProfileId;
  e.context = o["context"].toString();
  e.allDay = o["allDay"].toBool();
  e.endDate = QDate::fromString(o["endDate"].toString(), Qt::ISODate);
  e.rrule = o["rrule"].toString();
  e.exdates = datesFromJson(o["exdates"].toArray());
  e.masterId = o["masterId"].toString();
  e.originalDate = QDate::fromString(o["originalDate"].toString(), Qt::ISODate);
  e.tz = o["tz"].toString();
  e.location = o["location"].toString();
  e.notes = o["notes"].toString();
  e.url = o["url"].toString();
  e.reminderMinutes = o["reminderMinutes"].toInt(CalEvent::kReminderDefault);
  static const QStringList kKnown = {QStringLiteral("id"),           QStringLiteral("title"),   QStringLiteral("type"),
                                     QStringLiteral("start"),        QStringLiteral("end"),     QStringLiteral("attendees"),
                                     QStringLiteral("date"),         QStringLiteral("taskId"),  QStringLiteral("profileId"),
                                     QStringLiteral("context"),      QStringLiteral("allDay"),  QStringLiteral("endDate"),
                                     QStringLiteral("rrule"),        QStringLiteral("exdates"), QStringLiteral("masterId"),
                                     QStringLiteral("originalDate"), QStringLiteral("tz"),      QStringLiteral("location"),
                                     QStringLiteral("notes"),        QStringLiteral("url"),     QStringLiteral("reminderMinutes")};
  e.extra = unknownKeys(o, kKnown);
  return e;
}

QJsonArray eventsToJson(const QVector<CalEvent>& xs) {
  QJsonArray a;
  for(const CalEvent& e : xs) {
    a.append(eventToJson(e));
  }
  return a;
}

QVector<CalEvent> eventsFromJson(const QJsonArray& a, const QString& fallbackProfileId) {
  QVector<CalEvent> v;
  v.reserve(a.size());
  for(const auto& it : a) {
    v.append(eventFromJson(it.toObject(), fallbackProfileId));
  }
  return v;
}

// ───────────────── Note ─────────────────

QJsonObject noteToJson(const Note& n) {
  QJsonObject o;
  o["id"] = n.id;
  o["title"] = n.title;
  o["folder"] = n.folder;
  o["body"] = n.body;
  o["pinned"] = n.pinned;
  o["created"] = dtToStr(n.created);
  o["updated"] = dtToStr(n.updated);
  // Optional, written only when set: a note that never met a vault folder
  // keeps the shape it always had.
  if(!n.vaultPath.isEmpty()) {
    o["vaultPath"] = n.vaultPath;
  }
  if(!n.vaultHash.isEmpty()) {
    o["vaultHash"] = n.vaultHash;
  }
  if(!n.frontmatter.isEmpty()) {
    o["frontmatter"] = n.frontmatter;
  }
  return o;
}

Note noteFromJson(const QJsonObject& o) {
  Note n;
  n.id = o["id"].toString();
  n.title = o["title"].toString();
  n.folder = o["folder"].toString();
  n.body = o["body"].toString();
  n.pinned = o["pinned"].toBool();
  n.created = dtFromStr(o["created"].toString());
  n.updated = dtFromStr(o["updated"].toString());
  n.vaultPath = o["vaultPath"].toString();
  n.vaultHash = o["vaultHash"].toString();
  n.frontmatter = o["frontmatter"].toString();
  return n;
}

QJsonArray notesToJson(const QVector<Note>& xs) {
  QJsonArray a;
  for(const Note& n : xs) {
    a.append(noteToJson(n));
  }
  return a;
}

QVector<Note> notesFromJson(const QJsonArray& a) {
  QVector<Note> v;
  v.reserve(a.size());
  for(const auto& it : a) {
    v.append(noteFromJson(it.toObject()));
  }
  return v;
}

// ───────────────── DocPage ─────────────────

QJsonObject docPageToJson(const DocPage& p) {
  QJsonObject o;
  o["id"] = p.id;
  o["parentId"] = p.parentId;
  o["title"] = p.title;
  o["body"] = p.body;
  o["rank"] = p.rank;
  o["created"] = dtToStr(p.created);
  o["updated"] = dtToStr(p.updated);
  return o;
}

DocPage docPageFromJson(const QJsonObject& o) {
  DocPage p;
  p.id = o["id"].toString();
  p.parentId = o["parentId"].toString();
  p.title = o["title"].toString();
  p.body = o["body"].toString();
  p.rank = o["rank"].toDouble();
  p.created = dtFromStr(o["created"].toString());
  p.updated = dtFromStr(o["updated"].toString());
  return p;
}

QJsonArray docPagesToJson(const QVector<DocPage>& xs) {
  QJsonArray a;
  for(const DocPage& p : xs) {
    a.append(docPageToJson(p));
  }
  return a;
}

QVector<DocPage> docPagesFromJson(const QJsonArray& a) {
  QVector<DocPage> v;
  v.reserve(a.size());
  for(const auto& it : a) {
    v.append(docPageFromJson(it.toObject()));
  }
  return v;
}

// ───────────────── Person / statuses ─────────────────

QJsonArray peopleToJson(const QVector<Person>& xs) {
  QJsonArray a;
  for(const Person& p : xs) {
    QJsonObject o = p.extra;  // unknown keys first (PLAT-15)
    o["id"] = p.id;
    o["name"] = p.name;
    o["role"] = p.role;
    o["question"] = p.question;
    o["state"] = p.state;
    o["color"] = p.color.name();
    if(p.stateAt.isValid()) {
      o["stateAt"] = p.stateAt.toString(Qt::ISODateWithMs);
    }
    a.append(o);
  }
  return a;
}

QVector<Person> peopleFromJson(const QJsonArray& a) {
  QVector<Person> v;
  v.reserve(a.size());
  for(const auto& it : a) {
    const QJsonObject o = it.toObject();
    Person p;
    p.id = o["id"].toString();
    p.name = o["name"].toString();
    p.role = o["role"].toString();
    p.question = o["question"].toString();
    p.state = o["state"].toString();
    p.color = QColor(o["color"].toString());
    p.stateAt = QDateTime::fromString(o["stateAt"].toString(), Qt::ISODateWithMs);
    static const QStringList kKnown = {QStringLiteral("id"),
                                       QStringLiteral("name"),
                                       QStringLiteral("role"),
                                       QStringLiteral("question"),
                                       QStringLiteral("state"),
                                       QStringLiteral("color"),
                                       QStringLiteral("stateAt")};
    p.extra = unknownKeys(o, kKnown);
    v.append(p);
  }
  return v;
}

QJsonArray statusesToJson(const QVariantList& xs) {
  QJsonArray a;
  for(const QVariant& v : xs) {
    const QVariantMap m = v.toMap();
    // Unknown keys first (PLAT-15). A column is a plain map, so they ride in
    // it under kStatusExtraKey rather than loose among the ones QML reads.
    QJsonObject o = QJsonObject::fromVariantMap(m.value(QLatin1String(kStatusExtraKey)).toMap());
    o["id"] = m.value("id").toString();
    o["name"] = m.value("name").toString();
    const QVariant col = m.value("color");
    o["color"] = col.canConvert<QColor>() ? col.value<QColor>().name() : col.toString();
    // WIP limit. Omitted when unset so a column that has never had one keeps
    // the JSON it had before.
    const int wip = m.value("wip").toInt();
    if(wip > 0) {
      o["wip"] = wip;
    }
    // Days before a card in the column is archived (APP-122). Absent = the
    // column's default: Settings → Tasks for Done, never for the others.
    if(m.contains(QStringLiteral("archiveDays"))) {
      o["archiveDays"] = qMax(0, m.value("archiveDays").toInt());
    }
    // The column's stage (schema v12, APP-259), always written; and, until the
    // user confirms it, that it was assigned rather than picked.
    const QString category = m.value(QStringLiteral("category")).toString();
    o["category"] = heap::board::isColumnCategory(category) ? category : heap::board::defaultCategoryFor(o["id"].toString());
    if(m.value(QStringLiteral("categoryGuessed")).toBool()) {
      o["categoryGuessed"] = true;
    }
    a.append(o);
  }
  return a;
}

QVariantList statusesFromJson(const QJsonArray& a) {
  QVariantList v;
  for(const auto& it : a) {
    const QJsonObject o = it.toObject();
    QVariantMap m;
    m["id"] = o["id"].toString();
    m["name"] = o["name"].toString();
    m["color"] = QColor(o["color"].toString());
    m["wip"] = o["wip"].toInt(0);
    if(o.contains("archiveDays")) {
      m["archiveDays"] = qMax(0, o["archiveDays"].toInt(0));
    }
    // A column from before stages (schema ≤ 11, or an old export) gets its
    // stage here: a built-in id is its own, anything else "todo", flagged so
    // the user is asked once to check (APP-259). Never guessed from the name.
    const QString category = o["category"].toString();
    if(heap::board::isColumnCategory(category)) {
      m["category"] = category;
      m["categoryGuessed"] = o["categoryGuessed"].toBool(false);
    } else {
      const QString id = m["id"].toString();
      m["category"] = heap::board::defaultCategoryFor(id);
      m["categoryGuessed"] = !heap::board::isColumnCategory(id);
    }
    static const QStringList kKnown = {QStringLiteral("id"),
                                       QStringLiteral("name"),
                                       QStringLiteral("color"),
                                       QStringLiteral("wip"),
                                       QStringLiteral("archiveDays"),
                                       QStringLiteral("category"),
                                       QStringLiteral("categoryGuessed")};
    const QJsonObject extra = unknownKeys(o, kKnown);
    if(!extra.isEmpty()) {
      m[QLatin1String(kStatusExtraKey)] = extra.toVariantMap();
    }
    v.append(m);
  }
  return v;
}

// ───────────────── Profile ─────────────────

QJsonObject profileToJson(const Profile& p) {
  // Unknown keys first, so every key this build owns overwrites a stale copy.
  QJsonObject o = p.extra;
  o["id"] = p.id;
  o["name"] = p.name;
  o["color"] = p.color;
  o["createdAt"] = dtToStr(p.createdAt);
  o["tasks"] = tasksToJson(p.tasks);
  o["people"] = peopleToJson(p.people);
  o["statuses"] = statusesToJson(p.statuses);
  if(!p.docsState.isEmpty()) {
    const QJsonDocument d = QJsonDocument::fromJson(p.docsState.toUtf8());
    if(!d.isNull() && d.isObject()) {
      o["docs"] = d.object();
    }
  }
  // An array since v8. The key used to hold the whole of a profile's notes as
  // one markdown string; profileFromJson still reads that form, because a
  // document written by an older build is not migrated until it is opened.
  if(!p.notes.isEmpty()) {
    o["notes"] = notesToJson(p.notes);
  }
  if(!p.activeNoteId.isEmpty()) {
    o["activeNoteId"] = p.activeNoteId;
  }
  // Separate from `docs`, which is the link/snippet/contact catalog. The
  // catalog is still there and still useful; pages are the long-form half it
  // never had.
  if(!p.docPages.isEmpty()) {
    o["docPages"] = docPagesToJson(p.docPages);
  }
  if(!p.activeDocPageId.isEmpty()) {
    o["activeDocPageId"] = p.activeDocPageId;
  }
  // Optional: a profile with none (or one written before saved views) has no
  // key at all.
  if(!p.savedViews.isEmpty()) {
    o["savedViews"] = heap::savedviews::listToJson(p.savedViews);
  }
  // Optional too, and no schema bump: a build that does not know the key
  // carries it through a save in `extra` (PLAT-26).
  if(!p.statusLog.isEmpty()) {
    QJsonArray log;
    for(const StatusChange& c : p.statusLog) {
      log.append(QJsonObject{{"task", c.taskId}, {"from", c.from}, {"to", c.to}, {"at", dtToStr(c.at)}});
    }
    o["statusLog"] = log;
  }
  // Optional as well, same reasoning (APP-158).
  if(!p.waitingOn.isEmpty()) {
    QJsonArray waiting;
    for(const WaitingOn& w : p.waitingOn) {
      QJsonObject wo{{"task", w.taskId}, {"person", w.personId}, {"since", dtToStr(w.since)}};
      if(w.remindedAt.isValid()) {
        wo["reminded"] = dtToStr(w.remindedAt);
      }
      waiting.append(wo);
    }
    o["waitingOn"] = waiting;
  }
  return o;
}

Profile profileFromJson(const QJsonObject& o, QVector<CalEvent>* outLegacyEvents) {
  Profile p;
  p.id = o["id"].toString();
  p.name = o["name"].toString();
  p.color = o["color"].toString();
  p.createdAt = dtFromStr(o["createdAt"].toString());
  p.tasks = tasksFromJson(o["tasks"].toArray());
  p.people = peopleFromJson(o["people"].toArray());
  p.statuses = statusesFromJson(o["statuses"].toArray());
  if(o.contains("docs")) {
    p.docsState = QJsonDocument(o["docs"].toObject()).toJson(QJsonDocument::Compact);
  }
  // Either shape. A document written before v8 holds every note as one
  // markdown string; one written since holds an array. Reading both here means
  // an older file opens correctly whether or not the migration ladder has run
  // over it yet, which is the same tolerance `deadline` has.
  if(o.contains("notes")) {
    if(o["notes"].isArray()) {
      p.notes = notesFromJson(o["notes"].toArray());
    } else {
      p.notesState = o["notes"].toString();
    }
  }
  p.activeNoteId = o["activeNoteId"].toString();
  p.docPages = docPagesFromJson(o["docPages"].toArray());
  p.activeDocPageId = o["activeDocPageId"].toString();
  p.savedViews = heap::savedviews::listFromJson(o["savedViews"].toArray());
  for(const auto& v : o["statusLog"].toArray()) {
    const QJsonObject c = v.toObject();
    const StatusChange change{
        .taskId = c["task"].toString(), .from = c["from"].toString(), .to = c["to"].toString(), .at = dtFromStr(c["at"].toString())};
    if(!change.taskId.isEmpty() && change.at.isValid()) {
      p.statusLog.append(change);
    }
  }
  for(const auto& v : o["waitingOn"].toArray()) {
    const QJsonObject wo = v.toObject();
    const WaitingOn w{.taskId = wo["task"].toString(),
                      .personId = wo["person"].toString(),
                      .since = dtFromStr(wo["since"].toString()),
                      .remindedAt = dtFromStr(wo["reminded"].toString())};
    if(!w.taskId.isEmpty() && !w.personId.isEmpty() && w.since.isValid()) {
      p.waitingOn.append(w);
    }
  }
  if(outLegacyEvents && o.contains("events")) {
    outLegacyEvents->append(eventsFromJson(o["events"].toArray(), p.id));
  }
  // Whatever else the object carries passes through a save (PLAT-26). A key
  // added to profileToJson must be listed here too, or a stale copy of it
  // would come back from `extra` when the new code omits it.
  static const QStringList kKnown = {QStringLiteral("id"),
                                     QStringLiteral("name"),
                                     QStringLiteral("color"),
                                     QStringLiteral("createdAt"),
                                     QStringLiteral("tasks"),
                                     QStringLiteral("people"),
                                     QStringLiteral("statuses"),
                                     QStringLiteral("docs"),
                                     QStringLiteral("notes"),
                                     QStringLiteral("activeNoteId"),
                                     QStringLiteral("docPages"),
                                     QStringLiteral("activeDocPageId"),
                                     QStringLiteral("savedViews"),
                                     QStringLiteral("statusLog"),
                                     QStringLiteral("waitingOn"),
                                     QStringLiteral("events")};
  for(auto it = o.constBegin(); it != o.constEnd(); ++it) {
    if(!kKnown.contains(it.key())) {
      p.extra.insert(it.key(), it.value());
    }
  }
  return p;
}

void dropPassThrough(Profile& p) {
  p.extra = {};
  for(Task& t : p.tasks) {
    t.extra = {};
  }
  for(Person& person : p.people) {
    person.extra = {};
  }
  for(QVariant& v : p.statuses) {
    QVariantMap m = v.toMap();
    if(m.remove(QLatin1String(kStatusExtraKey)) > 0) {
      v = m;
    }
  }
}

void dropPassThrough(QVector<CalEvent>& events) {
  for(CalEvent& e : events) {
    e.extra = {};
  }
}

// ───────────────── Migration ─────────────────

namespace {

// v3→v4: a bare `deadline` date becomes a scheduledAt/dueAt pair at midnight
// with no clock component. Both are set so the task keeps showing up wherever a
// due date used to put it while also being schedulable.
void migrateTaskV3ToV4(QJsonObject& task) {
  if(!task.contains("deadline")) {
    return;
  }
  const QDate legacy = QDate::fromString(task["deadline"].toString(), Qt::ISODate);
  task.remove("deadline");
  if(!legacy.isValid()) {
    return;  // an empty "" deadline: dropping the dead key is the whole migration
  }
  if(!task.contains("scheduledAt") && !task.contains("dueAt")) {
    const QString at = dtToStr(QDateTime(legacy, QTime(0, 0)));
    task["scheduledAt"] = at;
    task["dueAt"] = at;
  }
}

// v4→v5: every task gains a rank. The board had no per-task order at all, so
// the order it *showed* was the array order — which is exactly what this
// preserves. Ranks are spread by kRankStep so a later drop between two cards
// has room to take a midpoint.
void migrateTaskV4ToV5(QJsonObject& task, int index) {
  if(!task.contains("rank")) {
    task["rank"] = (index + 1) * kRankStep;
  }
}

// Rebuilds the array rather than writing back through an index: QJsonArray's
// subscript hands out a reference proxy, and in-place index mutation is exactly
// the shape a well-meaning "modernize to a range-for" rewrite would break.
QJsonArray migratedTaskArrayV3ToV4(const QJsonArray& tasks) {
  QJsonArray out;
  for(const QJsonValue& v : tasks) {
    QJsonObject t = v.toObject();
    migrateTaskV3ToV4(t);
    out.append(t);
  }
  return out;
}

QJsonArray migratedTaskArrayV4ToV5(const QJsonArray& tasks) {
  QJsonArray out;
  int index = 0;
  for(const QJsonValue& v : tasks) {
    QJsonObject t = v.toObject();
    migrateTaskV4ToV5(t, index++);
    out.append(t);
  }
  return out;
}

// v9→v10, part one: the shared `hasTime` becomes one flag per datetime. The
// rule lives in applyLegacyHasTime(), which imports use as well.
void migrateTaskV9ToV10(QJsonObject& task) {
  if(!task.contains("hasTime")) {
    return;
  }
  const bool legacy = task["hasTime"].toBool(false);
  task.remove("hasTime");
  Task t;
  t.scheduledAt = dtFromStr(task["scheduledAt"].toString());
  t.dueAt = dtFromStr(task["dueAt"].toString());
  applyLegacyHasTime(t, legacy);
  if(t.scheduledHasTime) {
    task["scheduledHasTime"] = true;
  }
  if(t.dueHasTime) {
    task["dueHasTime"] = true;
  }
}

// v9→v10, part two: ranks. The v4→v5 rung numbered every task, but the demo
// seed, every synced issue and every task saved through the editor still came
// out at rank 0, where ties fall back to the id and a drop "to the top" has no
// room above 0. Each column that holds a tie is renumbered in the order the
// board showed it (rank, then id), so nothing visibly moves.
QJsonArray migratedTaskArrayV9ToV10(const QJsonArray& tasks) {
  QVector<QJsonObject> objs;
  objs.reserve(tasks.size());
  for(const QJsonValue& v : tasks) {
    QJsonObject t = v.toObject();
    migrateTaskV9ToV10(t);
    objs.append(t);
  }
  QHash<QString, QVector<int>> byStatus;
  for(int i = 0; i < objs.size(); ++i) {
    byStatus[objs[i]["status"].toString()].append(i);
  }
  for(auto it = byStatus.begin(); it != byStatus.end(); ++it) {
    QVector<int>& rows = it.value();
    std::sort(rows.begin(), rows.end(), [&](int a, int b) {
      const double ra = objs[a]["rank"].toDouble(0.0);
      const double rb = objs[b]["rank"].toDouble(0.0);
      return ra != rb ? ra < rb : objs[a]["id"].toString() < objs[b]["id"].toString();
    });
    bool tie = false;
    for(int k = 1; k < rows.size() && !tie; ++k) {
      tie = objs[rows[k - 1]]["rank"].toDouble(0.0) == objs[rows[k]]["rank"].toDouble(0.0);
    }
    if(!tie) {
      continue;
    }
    for(int k = 0; k < rows.size(); ++k) {
      objs[rows[k]]["rank"] = (k + 1) * kRankStep;
    }
  }
  QJsonArray out;
  for(const QJsonObject& o : objs) {
    out.append(o);
  }
  return out;
}

// One rung of the ladder: every task in the document, wherever it lives.
// Tasks sit under profiles[].tasks from v2 on, and at the top level in v1.
void forEachTaskArray(QJsonObject& root, const std::function<QJsonArray(const QJsonArray&)>& step) {
  if(root.contains("profiles")) {
    QJsonArray profiles;
    for(const QJsonValue& v : root["profiles"].toArray()) {
      QJsonObject p = v.toObject();
      p["tasks"] = step(p["tasks"].toArray());
      profiles.append(p);
    }
    root["profiles"] = profiles;
  } else if(root.contains("tasks")) {
    root["tasks"] = step(root["tasks"].toArray());
  }
}

// The blob becomes one note. Its title is the document's first heading, since
// that is what the user called it; failing that, "Notes", because a note with
// no name cannot be found in a list.
void migrateNotesV7ToV8(QJsonObject& root) {
  QJsonArray profiles;
  bool touched = false;
  for(const QJsonValue& v : root["profiles"].toArray()) {
    QJsonObject p = v.toObject();
    if(p.contains("notes") && !p["notes"].isArray()) {
      const QString blob = p["notes"].toString();
      QJsonArray notes;
      if(!blob.trimmed().isEmpty()) {
        QString title;
        for(const QString& raw : blob.split(QLatin1Char('\n'))) {
          const QString line = raw.trimmed();
          if(line.startsWith(QLatin1Char('#'))) {
            title = line.mid(line.lastIndexOf(QLatin1Char('#')) + 1).trimmed();
            break;
          }
        }
        const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
        QJsonObject n;
        n["id"] = QStringLiteral("note-migrated");
        n["title"] = title.isEmpty() ? QStringLiteral("Notes") : title;
        n["folder"] = QString();
        n["body"] = blob;
        n["pinned"] = false;
        n["created"] = now;
        n["updated"] = now;
        notes.append(n);
        p["activeNoteId"] = QStringLiteral("note-migrated");
      }
      p["notes"] = notes;
      touched = true;
    }
    profiles.append(p);
  }
  if(touched) {
    root["profiles"] = profiles;
  }
}

// v11→v12 (APP-244, owner's call 2026-10-08): a tracker card's local edits
// used to live in the tracker fields, where the next pull could take them.
// Each one moves into `local`, and the tracker field gets the tracker's value
// back. An empty tracker value (a card older than three-way merge) is not a
// divergence: nothing moves. `ru` picks the language of the notes headers.
void migrateTaskV11ToV12(QJsonObject& task, bool ru) {
  if(task.value(QStringLiteral("externalId")).toString().isEmpty() || !task.value(QStringLiteral("externalMeta")).isObject()) {
    return;
  }
  QJsonObject meta = task.value(QStringLiteral("externalMeta")).toObject();
  QJsonObject local = task.value(QStringLiteral("local")).toObject();
  QStringList conflicts;
  for(const auto& v : meta.value(QStringLiteral("conflicts")).toArray()) {
    conflicts.append(v.toString());
  }
  QStringList notes;
  if(!local.value(QStringLiteral("notes")).toString().isEmpty()) {
    notes.append(local.value(QStringLiteral("notes")).toString());
  }

  const QString remotePriority = meta.value(QStringLiteral("remotePriority")).toString();
  const QString priority = task.value(QStringLiteral("priority")).toString();
  if(!remotePriority.isEmpty() && !priority.isEmpty() && priority != remotePriority) {
    local["myPriority"] = priority;
    local["myPriorityBase"] = remotePriority;
    task["priority"] = remotePriority;
    conflicts.removeAll(QStringLiteral("priority"));
  }

  const QDateTime remoteDue = dtFromStr(meta.value(QStringLiteral("remoteDueAt")).toString());
  const QDateTime due = dtFromStr(task.value(QStringLiteral("dueAt")).toString());
  if(remoteDue.isValid() && due.isValid() && due != remoteDue) {
    local["myDueAt"] = dtToStr(due);
    if(task.value(QStringLiteral("dueHasTime")).toBool(false)) {
      local["myDueHasTime"] = true;
    }
    local["myDueBase"] = dtToStr(remoteDue);
    task["dueAt"] = dtToStr(remoteDue);
    // The pull sets the clock flag from the tracker; a tracker due date sent
    // at exactly midnight is a bare date.
    if(remoteDue.time() == QTime(0, 0)) {
      task.remove("dueHasTime");
    } else {
      task["dueHasTime"] = true;
    }
  }

  const QJsonArray remoteLabels = meta.value(QStringLiteral("remoteLabels")).toArray();
  if(!remoteLabels.isEmpty() && task.value(QStringLiteral("labels")).isArray()) {
    QStringList remote;
    for(const auto& v : remoteLabels) {
      remote.append(v.toString());
    }
    QJsonArray keep;
    QJsonArray tags = local.value(QStringLiteral("tags")).toArray();
    for(const auto& v : task.value(QStringLiteral("labels")).toArray()) {
      const QJsonObject label = v.toObject();
      if(remote.contains(label.value(QStringLiteral("id")).toString())) {
        keep.append(label);
      } else {
        tags.append(label);  // same {id, color} shape as a local tag
      }
    }
    if(keep.isEmpty()) {
      task.remove("labels");
    } else {
      task["labels"] = keep;
    }
    if(!tags.isEmpty()) {
      local["tags"] = tags;
    }
  }

  const QString remoteTitle = meta.value(QStringLiteral("remoteTitle")).toString();
  const QString title = task.value(QStringLiteral("title")).toString();
  if(!remoteTitle.isEmpty() && title != remoteTitle) {
    notes.append((ru ? QStringLiteral("Мой заголовок (до 0.8.0): ") : QStringLiteral("My title (before 0.8.0): ")) + title);
    task["title"] = remoteTitle;
    conflicts.removeAll(QStringLiteral("title"));
  }
  const QString remoteBody = meta.value(QStringLiteral("remoteBody")).toString();
  const QString desc = task.value(QStringLiteral("desc")).toString();
  if(!remoteBody.isEmpty() && desc != remoteBody) {
    if(!desc.trimmed().isEmpty()) {
      notes.append((ru ? QStringLiteral("## Моя версия описания (до 0.8.0)\n\n")
                       : QStringLiteral("## My version of the description (before 0.8.0)\n\n")) +
                   desc);
    }
    task["desc"] = remoteBody;
    conflicts.removeAll(QStringLiteral("body"));
  }

  if(!notes.isEmpty()) {
    local["notes"] = notes.join(QStringLiteral("\n\n"));
  }
  if(conflicts.isEmpty()) {
    meta.remove("conflicts");
  } else {
    meta["conflicts"] = QJsonArray::fromStringList(conflicts);
  }
  task["externalMeta"] = meta;
  if(!local.isEmpty()) {
    task["local"] = local;
  }
}

// v11→v12 (APP-251): the timer kept one total; it becomes one session with no
// date ("before 0.8.0"), so the sum stays what it was and new sessions add to
// it. trackedSeconds itself is kept: it is the sum the views read.
void migrateTimerToSessionsV11ToV12(QJsonObject& task) {
  const int tracked = task.value(QStringLiteral("trackedSeconds")).toInt(0);
  if(tracked <= 0) {
    return;
  }
  QJsonObject local = task.value(QStringLiteral("local")).toObject();
  if(local.contains(QStringLiteral("sessions"))) {
    return;
  }
  local["sessions"] = QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("before-0.8.0")},
                                             {QStringLiteral("start"), QString()},
                                             {QStringLiteral("end"), QString()},
                                             {QStringLiteral("seconds"), tracked}}};
  task["local"] = local;
}

// heap 2 (APP-258): the old sidebar's Focus pair (Blocked, In review) jumped
// to a board column; it is now two ordinary saved views at the top of "My
// views", for every profile that has those columns and lacks such a view.
void migrateFocusToSavedViewsV11ToV12(QJsonObject& root, bool ru) {
  QJsonArray profiles = root.value(QStringLiteral("profiles")).toArray();
  for(qsizetype i = 0; i < profiles.size(); ++i) {
    QJsonObject p = profiles[i].toObject();
    QStringList columns;
    for(const auto& st : p.value(QStringLiteral("statuses")).toArray()) {
      columns << st.toObject().value(QStringLiteral("id")).toString();
    }
    QJsonArray views = p.value(QStringLiteral("savedViews")).toArray();
    QStringList queries;
    QStringList ids;
    for(const auto& v : views) {
      queries << heap::savedviews::normalizeQuery(v.toObject().value(QStringLiteral("query")).toString()).toLower();
      ids << v.toObject().value(QStringLiteral("id")).toString();
    }
    QJsonArray front;
    for(const heap::savedviews::SavedView& v : {heap::savedviews::blockedView(ru), heap::savedviews::reviewView(ru)}) {
      const QString column = v.query.section(QLatin1Char(':'), 1);
      if(!columns.contains(column) || queries.contains(v.query) || ids.contains(v.id)) {
        continue;
      }
      front.append(heap::savedviews::toJson(v));
    }
    if(front.isEmpty()) {
      continue;
    }
    for(const auto& v : views) {
      front.append(v);
    }
    p[QStringLiteral("savedViews")] = front;
    profiles[i] = p;
  }
  root[QStringLiteral("profiles")] = profiles;
}

}  // namespace

bool migrateState(QJsonObject& root, int fromVersion) {
  if(fromVersion >= kSchemaVersion) {
    return false;  // the version gate: a current document is never re-migrated
  }

  // The ladder. Each rung is guarded by the version it upgrades *from*, so a
  // document entering at v4 walks past the v3→v4 rung instead of running it
  // again. Without the guard every future rung would also re-run every older
  // one — harmless only for as long as each step happens to be idempotent.
  if(fromVersion < 4) {
    forEachTaskArray(root, migratedTaskArrayV3ToV4);
  }
  if(fromVersion < 5) {
    forEachTaskArray(root, migratedTaskArrayV4ToV5);
  }
  // v5 → v6 added CalEvent::allDay and CalEvent::endDate. It has no rung: a v5
  // event is a single-day timed event, which is exactly what the two new keys
  // mean when they are absent (false, and an invalid date). The bump exists so
  // that a v5 build meets the newer-schema guard instead of quietly writing
  // every all-day and multi-day event back out as an ordinary one.
  //
  // v6 → v7 added the recurrence fields, and has no rung for the same reason:
  // absent means "this event does not repeat", which is what every v6 event is.
  //
  // v7 → v8 turned a profile's notes from one markdown string into a list of
  // notes. This one does need a rung: the old blob is everything the user ever
  // wrote in Notes, and it has to become a note rather than a key that no
  // longer parses.
  if(fromVersion < 8) {
    migrateNotesV7ToV8(root);
  }
  // v8 -> v9 added Profile::docPages. No rung: a v8 profile simply has none,
  // and its `docs` catalog is untouched and still read the same way.
  //
  // v9 -> v10 split Task.hasTime per datetime and spread tied ranks.
  if(fromVersion < 10) {
    forEachTaskArray(root, migratedTaskArrayV9ToV10);
  }
  // v10 -> v11 added Task.attachments and Profile.savedViews. No rung: a v10
  // task has no files and a v10 profile no saved views, which is what the two
  // keys mean when absent. The bump exists for the other direction — 0.5.3
  // reads v10, does not know `attachments`, and saved every task without it
  // (PLAT-15); v11 sends it into its newer-schema read-only mode instead.
  //
  // v11 -> v12 added Task.local and moves a tracker card's local divergence
  // into it, so the next pull can no longer take it (APP-244).
  if(fromVersion < 12) {
    const bool ru = root.value(QStringLiteral("settings")).toObject().value(QStringLiteral("language")).toString() == QStringLiteral("ru");
    forEachTaskArray(root, [ru](const QJsonArray& tasks) {
      QJsonArray out;
      for(const auto& v : tasks) {
        QJsonObject t = v.toObject();
        migrateTaskV11ToV12(t, ru);
        migrateTimerToSessionsV11ToV12(t);
        out.append(t);
      }
      return out;
    });
    if(root.value(QStringLiteral("profiles")).isArray()) {
      migrateFocusToSavedViewsV11ToV12(root, ru);
    }
  }

  root["schemaVersion"] = kSchemaVersion;
  return true;
}

}  // namespace heap::state
