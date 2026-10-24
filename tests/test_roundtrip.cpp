// Lossless serialization guard (HEAP-131).
//
// Two serializers write Task/CalEvent: heap::state (the live save + export path
// behind AppController::saveStateNow) and heap::sync::SyncSerializer (the sync
// transport). A field that only one of them knows about is a field that gets
// silently dropped. This suite pins both:
//
//   1. A property test: 1000 randomized Tasks and CalEvents must satisfy
//      x == fromJson(toJson(x)) through EACH serializer.
//   2. A field-count guard: makeFullTask/makeFullEvent set every declared field
//      to a non-default value, and the emitted JSON must carry one key per
//      field. Add a field without serializing it and this fails. Add a field
//      without extending the fixtures and the static_asserts in
//      src/StateSerializer.cpp and src/sync/SyncSerializer.cpp fail the build.

#include "FieldCount.h"
#include "Models.h"
#include "StateSerializer.h"

#include "sync/SyncSerializer.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTime>

#include <gtest/gtest.h>

#include <random>

namespace {

// Every declared field of ExternalMeta, set to something that is not its
// default — including a comment count, whose default is -1 rather than 0.
ExternalMeta makeFullMeta() {
  ExternalMeta m;
  m.author = QStringLiteral("grace");
  m.issueType = QStringLiteral("Bug");
  m.project = QStringLiteral("sectapunterx/heap");
  m.milestone = QStringLiteral("v0.5.0");
  m.commentCount = 7;
  m.createdAt = QDateTime(QDate(2026, 6, 1), QTime(8, 15, 30, 500));
  m.updatedAt = QDateTime(QDate(2026, 7, 8), QTime(11, 45, 10, 125));
  m.dueAt = QDateTime(QDate(2026, 7, 11), QTime(16, 0, 0, 750));
  m.crossProject = true;
  m.status = QStringLiteral("In Review");
  m.title = QStringLiteral("Harden the task data model (tracker)");
  m.body = QStringLiteral("as the tracker last sent it");
  m.column = QStringLiteral("review");
  m.unsyncedStatus = QStringLiteral("blocked");
  m.goneUpstream = true;
  m.scope = QStringLiteral("3f9a0c1e7b2d");
  m.outOfScope = true;
  m.priority = QStringLiteral("P1");
  m.labels = {QStringLiteral("bug"), QStringLiteral("ui")};
  m.conflicts = {QStringLiteral("title"), QStringLiteral("priority")};
  m.pushQueued = true;
  // APP-242 / APP-255: no key named createdAt/updatedAt in here (JsonMerger).
  m.details =
      QJsonObject{{QStringLiteral("kind"), QStringLiteral("mr")},
                  {QStringLiteral("sourceBranch"), QStringLiteral("feature/x")},
                  {QStringLiteral("conflicts"), true},
                  {QStringLiteral("closes"), QJsonArray{QStringLiteral("#12")}},
                  {QStringLiteral("sprint"),
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("S 14")}, {QStringLiteral("end"), QStringLiteral("2026-10-16")}}}};
  return m;
}

// Every declared field of Task, set to something that is not its default.
Task makeFullTask() {
  Task t;
  t.id = QStringLiteral("HEAP-104");
  t.title = QStringLiteral("Harden the task data model");
  t.desc = QStringLiteral("lossless, time-aware persistence");
  t.priority = QStringLiteral("P0");
  t.status = QStringLiteral("prog");
  t.scheduledAt = QDateTime(QDate(2026, 7, 10), QTime(9, 0, 0, 250));
  t.dueAt = QDateTime(QDate(2026, 7, 11), QTime(16, 0, 0, 750));
  t.scheduledHasTime = true;
  t.dueHasTime = true;
  t.branch = QStringLiteral("heap-104-lossless-task-model");
  t.statusChangedAt = QDateTime(QDate(2026, 7, 9), QTime(14, 30, 5, 125));
  t.archived = true;
  t.trackedSeconds = 4242;
  t.timerStartedAt = QDateTime(QDate(2026, 7, 9), QTime(15, 0, 1, 5));
  t.recurrence = QStringLiteral("every:weekday");
  t.externalId = QStringLiteral("104");
  t.externalUrl = QStringLiteral("https://github.com/sectapunterx/heap/issues/104");
  t.externalProvider = QStringLiteral("github");
  t.labels = {Label{QStringLiteral("infra"), QStringLiteral("#5cc2dd")}, Label{QStringLiteral("trust"), QString()}};
  t.estimateMinutes = 480;
  t.someday = true;
  t.assignee = QStringLiteral("sectapunterx");
  t.externalMeta = makeFullMeta();
  // Schema v5. A fractional rank on purpose: the midpoint of two neighbours is
  // what a drop between cards produces, and it has to survive the round trip
  // exactly or the card moves on the next launch.
  t.rank = 1536.5;
  t.links = {TaskLink{QStringLiteral("blocks"), QStringLiteral("HEAP-105")},
             TaskLink{QStringLiteral("blocks"), QStringLiteral("HEAP-106")}};
  // A size past 2^32 on purpose: it travels as a JSON number and must come
  // back as the same qint64.
  t.attachments = {Attachment{QStringLiteral("0123456789abcdef0123456789abcdef.png"),
                              QStringLiteral("shot [1].png"),
                              5000000123LL,
                              QStringLiteral("image/png")},
                   Attachment{QStringLiteral("fedcba9876543210fedcba9876543210"), QStringLiteral("Makefile"), 42, QString()}};
  // Schema v12: the local layer (APP-244), every field set.
  t.local.notes = QStringLiteral("tried a smaller pool:\n```sh\nmake bench\n```");
  t.local.checklist = {LocalCheckItem{QStringLiteral("c1"), QStringLiteral("repro"), 1, true, false, QString()},
                       LocalCheckItem{QStringLiteral("c2"), QStringLiteral("fix"), 3, true, true, QStringLiteral("HEAP-107")}};
  t.local.myPriority = QStringLiteral("P1");
  t.local.myDueAt = QDateTime(QDate(2026, 7, 12), QTime(10, 0, 0, 500));
  t.local.myDueHasTime = true;
  t.local.myPriorityBase = QStringLiteral("P2");
  t.local.myDueBase = QDateTime(QDate(2026, 7, 15), QTime(0, 0));
  t.local.tags = {LocalTag{QStringLiteral("after-release"), QStringLiteral("#b1a7f0")}, LocalTag{QStringLiteral("quick"), QString()}};
  t.local.related = {
      LocalLink{
          QStringLiteral("r1"), QStringLiteral("related"), QStringLiteral("https://gitlab.example/a/b/-/merge_requests/17"), QString()},
      LocalLink{QStringLiteral("r2"), QStringLiteral("related"), QStringLiteral("APP-12"), QStringLiteral("work")}};
  t.local.commentDraft = QStringLiteral("Reproduced on 0.8.0, see notes.");
  t.local.doneFrom = QStringLiteral("review");
  t.local.sessions = {
      TimerSession{QStringLiteral("before-0.8.0"), QDateTime(), QDateTime(), 3600},
      TimerSession{
          QStringLiteral("s1"), QDateTime(QDate(2026, 7, 10), QTime(23, 30, 0, 250)), QDateTime(QDate(2026, 7, 11), QTime(0, 45)), 0}};
  t.local.remindAt = QDateTime(QDate(2026, 10, 16), QTime(15, 0));
  t.local.remindLead = 15;
  t.local.reminder = true;
  t.local.extra = QJsonObject{{QStringLiteral("futureLocalField"), true}};
  // A key a newer build wrote: it has to come back out as itself (PLAT-15).
  t.extra = QJsonObject{{QStringLiteral("futureTaskField"), QJsonObject{{QStringLiteral("n"), 1}}}};
  return t;
}

CalEvent makeFullEvent() {
  CalEvent e;
  e.id = QStringLiteral("ev-1");
  e.title = QStringLiteral("Design review");
  e.type = QStringLiteral("sync");
  e.start = 10.5;
  e.end = 11.25;
  e.attendees = QStringLiteral("Ann, Bob");
  e.date = QDate(2026, 7, 10);
  e.taskId = QStringLiteral("HEAP-104");
  e.profileId = QStringLiteral("default");
  e.context = QStringLiteral("heap");
  e.allDay = true;
  e.endDate = QDate(2026, 7, 12);
  e.rrule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE;INTERVAL=2");
  e.exdates = {QDate(2026, 7, 20), QDate(2026, 8, 3)};
  e.masterId = QStringLiteral("ev-master");
  e.originalDate = QDate(2026, 7, 13);
  e.tz = QStringLiteral("America/New_York");
  e.location = QStringLiteral("Room 4");
  e.notes = QStringLiteral("agenda:\n- one");
  e.url = QStringLiteral("https://meet.example/abc");
  e.reminderMinutes = 15;
  e.extra = QJsonObject{{QStringLiteral("futureEventField"), QStringLiteral("keep")}};
  return e;
}

// Deterministic generator: the same seed produces the same 1000 cases on every
// platform, so a CI failure is reproducible locally.
class Gen {
 public:
  explicit Gen(quint32 seed) : rng_(seed) {
  }

  bool boolean() {
    return pick(0, 1) == 1;
  }

  int pick(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng_);
  }

  QString text() {
    static const QStringList kWords = {QStringLiteral(""),
                                       QStringLiteral("a"),
                                       QStringLiteral("ship it"),
                                       QStringLiteral("émoji ✅日本語"),
                                       QStringLiteral("quote\"and\\slash"),
                                       QStringLiteral("line\nbreak")};
    return kWords.at(pick(0, kWords.size() - 1));
  }

  // Milliseconds included: ISODateWithMs must carry them through.
  QDateTime dateTime(bool allowInvalid = true) {
    if(allowInvalid && pick(0, 3) == 0) {
      return {};
    }
    const QDate d(pick(1970, 2200), pick(1, 12), pick(1, 28));
    const QTime t(pick(0, 23), pick(0, 59), pick(0, 59), pick(0, 999));
    return QDateTime(d, t);
  }

  QDate date() {
    return pick(0, 3) == 0 ? QDate() : QDate(pick(1970, 2200), pick(1, 12), pick(1, 28));
  }

  Task task() {
    Task t;
    t.id = QStringLiteral("T-") + QString::number(pick(1, 10000));
    t.title = text();
    t.desc = text();
    t.priority = QStringLiteral("P") + QString::number(pick(0, 3));
    t.status = text();
    t.scheduledAt = dateTime();
    t.dueAt = dateTime();
    // The live reader drops a clock flag on a datetime that is not there.
    t.scheduledHasTime = t.scheduledAt.isValid() && boolean();
    t.dueHasTime = t.dueAt.isValid() && boolean();
    t.branch = text();
    // Never invalid: taskFromJson heals a missing status stamp to "now", which
    // is deliberate (old files must not sort to the epoch) and unmatchable.
    t.statusChangedAt = dateTime(false);
    t.archived = boolean();
    t.trackedSeconds = pick(0, 100000);
    t.timerStartedAt = dateTime();
    t.recurrence = boolean() ? QStringLiteral("every:week") : QString();
    if(boolean()) {
      t.externalId = QString::number(pick(1, 999));
      t.externalUrl = QStringLiteral("https://example.invalid/") + t.externalId;
      t.externalProvider = QStringLiteral("gitlab");
    }
    const int labelCount = pick(0, 3);
    for(int i = 0; i < labelCount; ++i) {
      t.labels.append(Label{QStringLiteral("l") + QString::number(pick(1, 20)), boolean() ? QStringLiteral("#ff0000") : QString()});
    }
    t.estimateMinutes = pick(0, 5000);
    t.someday = boolean();
    t.assignee = text();
    // Half the cases carry tracker metadata, half leave it default, so both the
    // "omit the whole object" and the "write it out" paths get exercised.
    if(boolean()) {
      t.externalMeta.author = text();
      t.externalMeta.issueType = text();
      t.externalMeta.project = text();
      t.externalMeta.milestone = text();
      // -1 is the default and means "unknown"; it has to survive as itself.
      t.externalMeta.commentCount = pick(-1, 50);
      t.externalMeta.createdAt = dateTime();
      t.externalMeta.updatedAt = dateTime();
      t.externalMeta.dueAt = dateTime();
      t.externalMeta.crossProject = boolean();
      t.externalMeta.status = text();
      t.externalMeta.title = text();
      t.externalMeta.body = text();
      t.externalMeta.column = text();
      t.externalMeta.unsyncedStatus = text();
      t.externalMeta.goneUpstream = boolean();
    }
    // Ranks are fractional in practice — a drop between two cards is their
    // midpoint — so halves are generated, not whole numbers.
    t.rank = pick(0, 100000) / 2.0;
    const int linkCount = pick(0, 3);
    for(int i = 0; i < linkCount; ++i) {
      t.links.append(TaskLink{QStringLiteral("blocks"), QStringLiteral("T-") + QString::number(pick(1, 10000))});
    }
    const int attachmentCount = pick(0, 2);
    for(int i = 0; i < attachmentCount; ++i) {
      t.attachments.append(Attachment{QStringLiteral("%1.pdf").arg(pick(1, 10000), 32, 10, QLatin1Char('0')),
                                      text(),
                                      pick(0, 1 << 30),
                                      boolean() ? QStringLiteral("application/pdf") : QString()});
    }
    // The local layer (APP-244): sometimes empty, which state.json omits.
    if(boolean()) {
      t.local.notes = text();
      const int items = pick(0, 3);
      for(int i = 0; i < items; ++i) {
        const bool done = boolean();
        t.local.checklist.append(
            LocalCheckItem{QStringLiteral("c") + QString::number(i), text(), pick(1, 4), done, done && boolean(), QString()});
      }
      if(boolean()) {
        t.local.myPriority = QStringLiteral("P") + QString::number(pick(0, 3));
        t.local.myPriorityBase = QStringLiteral("P") + QString::number(pick(0, 3));
      }
      if(boolean()) {
        t.local.myDueAt = QDateTime(QDate(2026, pick(1, 12), pick(1, 28)), QTime(pick(0, 23), pick(0, 59)));
        t.local.myDueHasTime = boolean();
      }
      if(boolean()) {
        t.local.tags.append(LocalTag{text() + QStringLiteral("x"), boolean() ? QStringLiteral("#123456") : QString()});
      }
      if(boolean()) {
        t.local.related.append(
            LocalLink{QStringLiteral("r1"), QStringLiteral("related"), QStringLiteral("T-") + QString::number(pick(1, 9)), QString()});
      }
      t.local.commentDraft = boolean() ? text() : QString();
      t.local.doneFrom = boolean() ? QStringLiteral("prog") : QString();
      if(boolean()) {
        t.local.remindAt = QDateTime(QDate(2026, pick(1, 12), pick(1, 28)), QTime(pick(0, 23), pick(0, 59)));
      }
      t.local.remindLead = boolean() ? pick(0, 120) : -1;
      t.local.reminder = boolean();
      if(boolean()) {
        const QDateTime start(QDate(2026, pick(1, 12), pick(1, 28)), QTime(pick(0, 23), pick(0, 59)));
        t.local.sessions.append(TimerSession{QStringLiteral("s1"), start, start.addSecs(pick(1, 9000)), 0});
      }
    }
    return t;
  }

  CalEvent event() {
    CalEvent e;
    e.id = QStringLiteral("E-") + QString::number(pick(1, 10000));
    e.title = text();
    e.type = text();
    e.start = pick(0, 47) / 2.0;
    e.end = e.start + pick(1, 4) / 2.0;
    e.attendees = text();
    e.date = date();
    e.taskId = text();
    e.profileId = text();
    e.context = text();
    e.allDay = boolean();
    // Half the cases carry an end date, so both the single-day and the
    // multi-day shape go through every serializer.
    e.endDate = boolean() ? e.date.addDays(pick(1, 5)) : QDate();
    e.rrule = boolean() ? QStringLiteral("FREQ=DAILY;INTERVAL=") + QString::number(pick(1, 4)) : QString();
    // Only off a valid date: an exdate that is itself invalid could never
    // match an occurrence, so the serializer drops it on purpose and the
    // generator must not produce one.
    e.exdates.clear();
    if(e.date.isValid()) {
      for(int k = 0, n = pick(0, 3); k < n; ++k) {
        e.exdates.append(e.date.addDays(pick(1, 60)));
      }
    }
    e.masterId = boolean() ? text() : QString();
    e.originalDate = boolean() ? date() : QDate();
    e.tz = boolean() ? QStringLiteral("Europe/Berlin") : QString();
    e.location = text();
    e.notes = text();
    e.url = boolean() ? text() : QString();
    e.reminderMinutes = pick(-2, 60);
    return e;
  }

 private:
  std::mt19937 rng_;
};

constexpr int kCases = 1000;

}  // namespace

// ── The compile-time half of the guard ──
// Mirrors the static_asserts inside both serializers. If the struct grows and
// only one serializer is updated, that serializer's own static_assert fires.
TEST(FieldCountGuard, TaskAndEventArityIsPinned) {
  EXPECT_EQ(heap::meta::fieldCount<Task>(), 28u);
  EXPECT_EQ(heap::meta::fieldCount<TaskLocal>(), 16u);
  EXPECT_EQ(heap::meta::fieldCount<TimerSession>(), 4u);
  EXPECT_EQ(heap::meta::fieldCount<Attachment>(), 4u);
  EXPECT_EQ(heap::meta::fieldCount<CalEvent>(), 22u);
}

// ExternalMeta is nested inside Task, so Task's own count stays 1 for the whole
// object — this is what stops a field added in there from being dropped.
TEST(FieldCountGuard, ExternalMetaArityIsPinned) {
  EXPECT_EQ(heap::meta::fieldCount<ExternalMeta>(), 22u);
}

// ── The runtime half: one emitted key per declared field ──
// The live serializer omits keys whose value is the default, so a task with
// every field set must emit exactly as many keys as the struct has fields.
TEST(FieldCountGuard, LiveSerializerEmitsAKeyForEveryTaskField) {
  const QJsonObject o = heap::state::taskToJson(makeFullTask());
  EXPECT_EQ(static_cast<std::size_t>(o.keys().size()), heap::meta::fieldCount<Task>())
      << "keys: " << o.keys().join(QStringLiteral(",")).toStdString();
}

TEST(FieldCountGuard, LiveSerializerEmitsAKeyForEveryEventField) {
  const QJsonObject o = heap::state::eventToJson(makeFullEvent());
  EXPECT_EQ(static_cast<std::size_t>(o.keys().size()), heap::meta::fieldCount<CalEvent>());
}

TEST(FieldCountGuard, SyncSerializerEmitsAKeyForEveryTaskField) {
  const QJsonObject o = heap::sync::SyncSerializer::taskToJson(makeFullTask());
  EXPECT_EQ(static_cast<std::size_t>(o.keys().size()), heap::meta::fieldCount<Task>())
      << "keys: " << o.keys().join(QStringLiteral(",")).toStdString();
}

TEST(FieldCountGuard, SyncSerializerEmitsAKeyForEveryEventField) {
  const QJsonObject o = heap::sync::SyncSerializer::eventToJson(makeFullEvent());
  EXPECT_EQ(static_cast<std::size_t>(o.keys().size()), heap::meta::fieldCount<CalEvent>());
}

// ── Fully-populated round trips ──

// APP-122: a column's auto-archive days survive a save, 0 included; a
// column that never had one stays without the key.
// A column's stage (schema v12, APP-259) survives a save; one from before
// stages gets its own: a built-in id is its stage, any other column "todo",
// flagged for the user to check — never guessed from the name.
TEST(RoundTrip, ColumnStageSurvivesAndOldColumnsGetOne) {
  QVariantMap picked{{"id", "qa"}, {"name", "QA"}, {"color", "#8a8e98"}, {"category", "review"}, {"categoryGuessed", false}};
  const QVariantList back = heap::state::statusesFromJson(heap::state::statusesToJson({picked}));
  ASSERT_EQ(back.size(), 1);
  EXPECT_EQ(back.at(0).toMap().value("category").toString(), QString("review"));
  EXPECT_FALSE(back.at(0).toMap().value("categoryGuessed").toBool());

  QJsonArray legacy;
  legacy.append(QJsonObject{{"id", "prog"}, {"name", "Doing"}, {"color", "#5aa9e6"}});
  legacy.append(QJsonObject{{"id", "waiting-for-qa"}, {"name", "Done-ish"}, {"color", "#5aa9e6"}});
  const QVariantList old = heap::state::statusesFromJson(legacy);
  EXPECT_EQ(old.at(0).toMap().value("category").toString(), QString("prog"));
  EXPECT_FALSE(old.at(0).toMap().value("categoryGuessed").toBool());
  EXPECT_EQ(old.at(1).toMap().value("category").toString(), QString("todo")) << "not guessed from a name that says done";
  EXPECT_TRUE(old.at(1).toMap().value("categoryGuessed").toBool());
  // and written back with the stage, still flagged until the user picks one
  const QJsonArray saved = heap::state::statusesToJson(old);
  EXPECT_EQ(saved.at(1).toObject().value("category").toString(), QString("todo"));
  EXPECT_TRUE(saved.at(1).toObject().value("categoryGuessed").toBool());
  EXPECT_FALSE(saved.at(0).toObject().contains("categoryGuessed"));
}

TEST(RoundTrip, ColumnArchiveDaysSurvive) {
  QVariantMap obsolete{{"id", "obsolete"}, {"name", "Obsolete"}, {"color", "#8a8e98"}, {"archiveDays", 14}};
  QVariantMap never{{"id", "later"}, {"name", "Later"}, {"color", "#8a8e98"}, {"archiveDays", 0}};
  QVariantMap plain{{"id", "todo"}, {"name", "To Do"}, {"color", "#8a8e98"}};
  const QVariantList back = heap::state::statusesFromJson(heap::state::statusesToJson({obsolete, never, plain}));
  ASSERT_EQ(back.size(), 3);
  EXPECT_EQ(back.at(0).toMap().value("archiveDays").toInt(), 14);
  EXPECT_TRUE(back.at(1).toMap().contains("archiveDays"));
  EXPECT_EQ(back.at(1).toMap().value("archiveDays").toInt(), 0);
  EXPECT_FALSE(back.at(2).toMap().contains("archiveDays"));
  EXPECT_FALSE(back.at(0).toMap().contains("_extra")) << "archiveDays is a known key, not an extra";
}

TEST(RoundTrip, FullTaskSurvivesLiveSerializer) {
  const Task t = makeFullTask();
  EXPECT_EQ(t, heap::state::taskFromJson(heap::state::taskToJson(t)));
}

TEST(RoundTrip, FullTaskSurvivesSyncSerializer) {
  const Task t = makeFullTask();
  EXPECT_EQ(t, heap::sync::SyncSerializer::taskFromJson(heap::sync::SyncSerializer::taskToJson(t)));
}

TEST(RoundTrip, FullEventSurvivesBothSerializers) {
  const CalEvent e = makeFullEvent();
  EXPECT_EQ(e, heap::state::eventFromJson(heap::state::eventToJson(e)));
  EXPECT_EQ(e, heap::sync::SyncSerializer::eventFromJson(heap::sync::SyncSerializer::eventToJson(e)));
}

// A running timer, a recurrence rule and an external id in one task.
TEST(RoundTrip, RunningTimerRecurrenceAndExternalIdAllSurvive) {
  Task t;
  t.id = QStringLiteral("X-1");
  t.statusChangedAt = QDateTime(QDate(2026, 1, 1), QTime(1, 2, 3));
  t.trackedSeconds = 61;
  t.timerStartedAt = QDateTime(QDate(2026, 1, 1), QTime(2, 0));
  t.recurrence = QStringLiteral("every:mon");
  t.externalId = QStringLiteral("PROJ-9");
  t.externalUrl = QStringLiteral("https://jira.invalid/browse/PROJ-9");
  t.externalProvider = QStringLiteral("jira");

  const Task live = heap::state::taskFromJson(heap::state::taskToJson(t));
  EXPECT_EQ(live.trackedSeconds, 61);
  EXPECT_EQ(live.timerStartedAt, t.timerStartedAt);
  EXPECT_EQ(live.recurrence, QString("every:mon"));
  EXPECT_EQ(live.externalId, QString("PROJ-9"));
  EXPECT_EQ(live, t);

  const Task synced = heap::sync::SyncSerializer::taskFromJson(heap::sync::SyncSerializer::taskToJson(t));
  EXPECT_EQ(synced, t);
}

// ── Tracker metadata (HEAP-117) ──

TEST(RoundTrip, LiveSerializerOmitsDefaultMetaEntirely) {
  // A locally-created task must serialize exactly as it did before HEAP-117.
  Task t;
  t.id = QStringLiteral("LOCAL-1");
  t.statusChangedAt = QDateTime(QDate(2026, 1, 1), QTime(1, 2, 3));
  const QJsonObject o = heap::state::taskToJson(t);
  EXPECT_FALSE(o.contains(QStringLiteral("externalMeta")));
  EXPECT_EQ(heap::state::taskFromJson(o), t);
}

TEST(RoundTrip, LiveSerializerOmitsDefaultSubKeysOfMeta) {
  Task t;
  t.id = QStringLiteral("GH-1");
  t.statusChangedAt = QDateTime(QDate(2026, 1, 1), QTime(1, 2, 3));
  t.externalMeta.author = QStringLiteral("grace");
  const QJsonObject meta = heap::state::taskToJson(t).value(QStringLiteral("externalMeta")).toObject();
  EXPECT_EQ(meta.keys(), QStringList{QStringLiteral("author")});
  EXPECT_EQ(heap::state::taskFromJson(heap::state::taskToJson(t)), t);
}

TEST(RoundTrip, SyncSerializerAlwaysEmitsEveryMetaKey) {
  // The merge transport reads a missing key as a deletion, so this serializer
  // may not skip defaults the way the live one does.
  Task t;
  t.id = QStringLiteral("LOCAL-1");
  const QJsonObject meta = heap::sync::SyncSerializer::taskToJson(t).value(QStringLiteral("externalMeta")).toObject();
  EXPECT_EQ(static_cast<std::size_t>(meta.keys().size()), heap::meta::fieldCount<ExternalMeta>())
      << "keys: " << meta.keys().join(QStringLiteral(",")).toStdString();
}

TEST(RoundTrip, MetaTimestampsAreNotNamedLikeHeapsOwnClock) {
  // JsonMerger reads a task's top-level `updatedAt` as heap's last-write clock
  // and applies "earlier wins" to any `createdAt` at any depth. A tracker
  // timestamp under either name would let a device that merely re-pulled
  // outrank one that actually edited.
  const QJsonObject o = heap::sync::SyncSerializer::taskToJson(makeFullTask());
  EXPECT_FALSE(o.contains(QStringLiteral("updatedAt")));
  EXPECT_FALSE(o.contains(QStringLiteral("createdAt")));
  const QJsonObject meta = o.value(QStringLiteral("externalMeta")).toObject();
  EXPECT_FALSE(meta.contains(QStringLiteral("updatedAt")));
  EXPECT_FALSE(meta.contains(QStringLiteral("createdAt")));
  EXPECT_TRUE(meta.contains(QStringLiteral("remoteUpdatedAt")));
  EXPECT_TRUE(meta.contains(QStringLiteral("remoteCreatedAt")));
}

TEST(RoundTrip, UnknownCommentCountSurvivesAsUnknown) {
  // -1 means "the provider did not say", which is not the same as zero
  // comments; a naive toInt() would read a missing key back as 0.
  Task t;
  t.id = QStringLiteral("GH-1");
  t.statusChangedAt = QDateTime(QDate(2026, 1, 1), QTime(1, 2, 3));
  t.externalMeta.project = QStringLiteral("acme/web");
  ASSERT_EQ(t.externalMeta.commentCount, -1);
  EXPECT_EQ(heap::state::taskFromJson(heap::state::taskToJson(t)).externalMeta.commentCount, -1);
  EXPECT_EQ(heap::sync::SyncSerializer::taskFromJson(heap::sync::SyncSerializer::taskToJson(t)).externalMeta.commentCount, -1);

  t.externalMeta.commentCount = 0;
  EXPECT_EQ(heap::state::taskFromJson(heap::state::taskToJson(t)).externalMeta.commentCount, 0);
  EXPECT_EQ(heap::sync::SyncSerializer::taskFromJson(heap::sync::SyncSerializer::taskToJson(t)).externalMeta.commentCount, 0);
}

TEST(RoundTrip, AV4FileWithoutMetaLoadsWithDefaults) {
  // Every state.json written before HEAP-117 has no externalMeta key at all.
  QJsonObject o;
  o["id"] = "GH-5";
  o["statusChangedAt"] = "2026-07-01T10:00:00";
  o["externalId"] = "5";
  o["externalProvider"] = "github";
  const Task t = heap::state::taskFromJson(o);
  EXPECT_EQ(t.externalMeta, ExternalMeta{});
  EXPECT_EQ(t.externalMeta.commentCount, -1);
  EXPECT_FALSE(t.externalMeta.crossProject);
}

// ── Property test ──

TEST(RoundTrip, ThousandRandomTasksSurviveBothSerializers) {
  Gen gen(104u);
  for(int i = 0; i < kCases; ++i) {
    const Task t = gen.task();
    const Task live = heap::state::taskFromJson(heap::state::taskToJson(t));
    ASSERT_EQ(live, t) << "live serializer, case " << i;
    const Task synced = heap::sync::SyncSerializer::taskFromJson(heap::sync::SyncSerializer::taskToJson(t));
    ASSERT_EQ(synced, t) << "sync serializer, case " << i;
  }
}

TEST(RoundTrip, ThousandRandomEventsSurviveBothSerializers) {
  Gen gen(20260709);
  for(int i = 0; i < kCases; ++i) {
    const CalEvent e = gen.event();
    ASSERT_EQ(heap::state::eventFromJson(heap::state::eventToJson(e)), e) << "live serializer, case " << i;
    ASSERT_EQ(heap::sync::SyncSerializer::eventFromJson(heap::sync::SyncSerializer::eventToJson(e)), e) << "sync serializer, case " << i;
  }
}

// ── Legacy read path ──

TEST(RoundTrip, LegacyBareDateDeadlineLandsAtMidnightWithoutTime) {
  QJsonObject o;
  o["id"] = "OLD-1";
  o["statusChangedAt"] = "2026-07-01T10:00:00";
  o["deadline"] = "2026-07-08";

  const Task live = heap::state::taskFromJson(o);
  EXPECT_EQ(live.scheduledAt, QDateTime(QDate(2026, 7, 8), QTime(0, 0)));
  EXPECT_EQ(live.dueAt, QDateTime(QDate(2026, 7, 8), QTime(0, 0)));
  EXPECT_FALSE(live.dueHasTime);
  EXPECT_FALSE(live.scheduledHasTime);

  const Task synced = heap::sync::SyncSerializer::taskFromJson(o);
  EXPECT_EQ(synced.scheduledAt, QDateTime(QDate(2026, 7, 8), QTime(0, 0)));
  EXPECT_FALSE(synced.dueHasTime);
  EXPECT_FALSE(synced.scheduledHasTime);
}

TEST(RoundTrip, LegacyEmptyDeadlineStaysUnset) {
  QJsonObject o;
  o["id"] = "OLD-2";
  o["statusChangedAt"] = "2026-07-01T10:00:00";
  o["deadline"] = "";

  const Task live = heap::state::taskFromJson(o);
  EXPECT_FALSE(live.scheduledAt.isValid());
  EXPECT_FALSE(live.dueAt.isValid());
}

// Whole-second ISO datetimes written by pre-HEAP-131 builds still parse.
TEST(RoundTrip, DatetimesWrittenWithoutMillisecondsStillParse) {
  QJsonObject o;
  o["id"] = "OLD-3";
  o["statusChangedAt"] = "2026-07-01T10:00:00";
  o["timerStartedAt"] = "2026-07-01T11:30:00";

  const Task t = heap::state::taskFromJson(o);
  EXPECT_EQ(t.statusChangedAt, QDateTime(QDate(2026, 7, 1), QTime(10, 0)));
  EXPECT_EQ(t.timerStartedAt, QDateTime(QDate(2026, 7, 1), QTime(11, 30)));
}
