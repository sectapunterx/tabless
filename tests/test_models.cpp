// Direct coverage for the three QAbstractListModel subclasses in Models.cpp:
// role→field mapping in data(), upsert insert-vs-update, the mutation guards
// (setStatus/setArchived/stampStatusChange), setBlockedStuckIds range/union
// logic, the runtime git-info merge, remove/insert clamping, and the
// PersonModel/EventModel-specific ops (cycleState, todoCount, detachTask,
// setTaskId). Pure and headless — no AppController, no QApplication needed.

#include "Models.h"
#include "TaskDefer.h"
#include "TaskFilterProxy.h"

#include "recap/WeeklyRecap.h"

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSignalSpy>
#include <QVariantMap>
#include <QVector>

#include <gtest/gtest.h>

namespace {

Task mkTask(const QString& id) {
  Task t;
  t.id = id;
  t.title = id + QStringLiteral(" title");
  t.status = QStringLiteral("todo");
  t.priority = QStringLiteral("P2");
  return t;
}

Person mkPerson(const QString& id, const QString& state) {
  Person p;
  p.id = id;
  p.name = id;
  p.state = state;
  p.color = QColor(QStringLiteral("#112233"));
  return p;
}

}  // namespace

// ─── TaskModel::data role mapping ─────────────────────────────────────

TEST(TaskModelData, MapsEveryRole) {
  TaskModel m;
  Task t;
  t.id = QStringLiteral("T1");
  t.title = QStringLiteral("Fix");
  t.desc = QStringLiteral("d");
  t.priority = QStringLiteral("P1");
  t.status = QStringLiteral("prog");
  t.branch = QStringLiteral("feat/x");
  t.dueAt = QDateTime(QDate(2030, 1, 2), QTime(0, 0));
  t.scheduledAt = t.dueAt;
  t.archived = true;
  t.statusChangedAt = QDateTime(QDate(2030, 1, 2), QTime(9, 0));
  m.reset({t});

  const QModelIndex i = m.index(0, 0);
  EXPECT_EQ(m.data(i, TaskModel::IdRole).toString(), QString("T1"));
  EXPECT_EQ(m.data(i, TaskModel::TitleRole).toString(), QString("Fix"));
  EXPECT_EQ(m.data(i, TaskModel::DescRole).toString(), QString("d"));
  EXPECT_EQ(m.data(i, TaskModel::PriorityRole).toString(), QString("P1"));
  EXPECT_EQ(m.data(i, TaskModel::StatusRole).toString(), QString("prog"));
  EXPECT_EQ(m.data(i, TaskModel::BranchRole).toString(), QString("feat/x"));
  EXPECT_EQ(m.data(i, TaskModel::DeadlineRole).toDate(), QDate(2030, 1, 2));
  EXPECT_TRUE(m.data(i, TaskModel::ArchivedRole).toBool());
  EXPECT_EQ(m.data(i, TaskModel::StatusChangedAtRole).toDateTime(), QDateTime(QDate(2030, 1, 2), QTime(9, 0)));
  // Default (no git info pushed) — runtime git roles are empty/zero.
  EXPECT_FALSE(m.data(i, TaskModel::BlockedStuckRole).toBool());
  EXPECT_EQ(m.data(i, TaskModel::PrStateRole).toString(), QString());
  EXPECT_EQ(m.data(i, TaskModel::PrNumberRole).toInt(), 0);
}

TEST(TaskModelData, InvalidAndOutOfRangeReturnInvalid) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("T1"))});
  EXPECT_FALSE(m.data(QModelIndex(), TaskModel::IdRole).isValid());
  EXPECT_FALSE(m.data(m.index(5, 0), TaskModel::IdRole).isValid());
  EXPECT_FALSE(m.data(m.index(0, 0), Qt::DisplayRole).isValid());
}

// ─── TaskModel::upsert ────────────────────────────────────────────────

TEST(TaskModelUpsert, InsertAppendsUpdateReplacesInPlace) {
  TaskModel m;
  QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);

  m.upsert(mkTask(QStringLiteral("A")));
  m.upsert(mkTask(QStringLiteral("B")));
  m.upsert(mkTask(QStringLiteral("C")));
  EXPECT_EQ(m.rowCount(), 3);
  EXPECT_EQ(m.indexOfId(QStringLiteral("A")), 0);
  EXPECT_EQ(m.indexOfId(QStringLiteral("C")), 2);
  EXPECT_EQ(ins.count(), 3);
  EXPECT_EQ(chg.count(), 0);

  Task b = mkTask(QStringLiteral("B"));
  b.title = QStringLiteral("new");
  m.upsert(b);
  EXPECT_EQ(m.rowCount(), 3);
  EXPECT_EQ(m.indexOfId(QStringLiteral("B")), 1);  // not moved to end
  EXPECT_EQ(m.data(m.index(1, 0), TaskModel::TitleRole).toString(), QString("new"));
  EXPECT_EQ(ins.count(), 3);  // no new insert
  EXPECT_EQ(chg.count(), 1);  // one update
}

// ─── TaskModel::setStatus / setArchived / stampStatusChange ───────────

TEST(TaskModelMutate, SetStatusStampsAndGuards) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("T1"))});
  EXPECT_FALSE(m.data(m.index(0, 0), TaskModel::StatusChangedAtRole).toDateTime().isValid());

  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
  m.setStatus(QStringLiteral("T1"), QStringLiteral("done"));
  EXPECT_EQ(m.data(m.index(0, 0), TaskModel::StatusRole).toString(), QString("done"));
  EXPECT_TRUE(m.data(m.index(0, 0), TaskModel::StatusChangedAtRole).toDateTime().isValid());
  EXPECT_EQ(chg.count(), 1);

  // Same value → no-op, no emit.
  m.setStatus(QStringLiteral("T1"), QStringLiteral("done"));
  EXPECT_EQ(chg.count(), 1);
  // Unknown id → no-op.
  m.setStatus(QStringLiteral("ghost"), QStringLiteral("todo"));
  EXPECT_EQ(chg.count(), 1);
}

TEST(TaskModelMutate, SetArchivedGuards) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("T1"))});
  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
  m.setArchived(QStringLiteral("T1"), true);
  EXPECT_TRUE(m.data(m.index(0, 0), TaskModel::ArchivedRole).toBool());
  EXPECT_EQ(chg.count(), 1);
  m.setArchived(QStringLiteral("T1"), true);  // same value
  EXPECT_EQ(chg.count(), 1);
}

// ─── TaskModel::setBlockedStuckIds ────────────────────────────────────

TEST(TaskModelBlockedStuck, MembershipEqualSetAndUnionClear) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("T1")), mkTask(QStringLiteral("T2")), mkTask(QStringLiteral("T3"))});

  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
  m.setBlockedStuckIds({QStringLiteral("T2")});
  EXPECT_TRUE(m.data(m.index(1, 0), TaskModel::BlockedStuckRole).toBool());
  EXPECT_FALSE(m.data(m.index(0, 0), TaskModel::BlockedStuckRole).toBool());
  EXPECT_FALSE(m.data(m.index(2, 0), TaskModel::BlockedStuckRole).toBool());
  EXPECT_EQ(chg.count(), 1);

  // Equal set → no-op.
  m.setBlockedStuckIds({QStringLiteral("T2")});
  EXPECT_EQ(chg.count(), 1);

  // Clear → T2 refreshes back to false (union of old+new covers it).
  m.setBlockedStuckIds({});
  EXPECT_FALSE(m.data(m.index(1, 0), TaskModel::BlockedStuckRole).toBool());
  EXPECT_EQ(chg.count(), 2);
}

TEST(TaskModelBlockedStuck, UnknownOnlyIdEmitsNothing) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("T1"))});
  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
  m.setBlockedStuckIds({QStringLiteral("ghost")});
  EXPECT_FALSE(m.data(m.index(0, 0), TaskModel::BlockedStuckRole).toBool());
  EXPECT_EQ(chg.count(), 0);  // no in-model row affected → no dataChanged
}

// ─── TaskModel git info ───────────────────────────────────────────────

TEST(TaskModelGit, PartialMergeUnknownNoopAndClear) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("T1"))});

  QVariantMap info;
  info["prState"] = QStringLiteral("open");
  info["prNumber"] = 42;
  info["prUrl"] = QStringLiteral("http://x");
  info["ahead"] = 3;
  info["behind"] = 1;
  m.setGitInfoForId(QStringLiteral("T1"), info);
  const QModelIndex i = m.index(0, 0);
  EXPECT_EQ(m.data(i, TaskModel::PrStateRole).toString(), QString("open"));
  EXPECT_EQ(m.data(i, TaskModel::PrNumberRole).toInt(), 42);
  EXPECT_EQ(m.data(i, TaskModel::GitAheadRole).toInt(), 3);
  EXPECT_EQ(m.data(i, TaskModel::GitBehindRole).toInt(), 1);

  // Partial merge: only "ahead" supplied — others persist.
  QVariantMap partial;
  partial["ahead"] = 9;
  m.setGitInfoForId(QStringLiteral("T1"), partial);
  EXPECT_EQ(m.data(i, TaskModel::GitAheadRole).toInt(), 9);
  EXPECT_EQ(m.data(i, TaskModel::PrStateRole).toString(), QString("open"));
  EXPECT_EQ(m.data(i, TaskModel::PrNumberRole).toInt(), 42);

  // Unknown id → no-op, no emit.
  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
  m.setGitInfoForId(QStringLiteral("ghost"), info);
  EXPECT_EQ(chg.count(), 0);

  // clearAllGitInfo wipes runtime git state.
  m.clearAllGitInfo();
  EXPECT_EQ(m.data(i, TaskModel::PrStateRole).toString(), QString());
  EXPECT_EQ(m.data(i, TaskModel::GitAheadRole).toInt(), 0);
}

TEST(TaskModelGit, ResetClearsGitInfo) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("T1"))});
  QVariantMap info;
  info["prState"] = QStringLiteral("open");
  m.setGitInfoForId(QStringLiteral("T1"), info);
  m.reset({mkTask(QStringLiteral("T1"))});
  EXPECT_EQ(m.data(m.index(0, 0), TaskModel::PrStateRole).toString(), QString());
}

// ─── TaskModel remove / insert clamp ──────────────────────────────────

TEST(TaskModelRemoveInsert, ReindexAndClamp) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("T1")), mkTask(QStringLiteral("T2")), mkTask(QStringLiteral("T3"))});

  m.removeById(QStringLiteral("T2"));
  EXPECT_EQ(m.rowCount(), 2);
  EXPECT_EQ(m.indexOfId(QStringLiteral("T2")), -1);
  EXPECT_EQ(m.indexOfId(QStringLiteral("T3")), 1);

  QSignalSpy rem(&m, &QAbstractItemModel::rowsRemoved);
  m.removeById(QStringLiteral("ghost"));  // no-op
  EXPECT_EQ(rem.count(), 0);

  m.insertAt(1, mkTask(QStringLiteral("X")));
  EXPECT_EQ(m.indexOfId(QStringLiteral("X")), 1);
  m.insertAt(-5, mkTask(QStringLiteral("H")));  // clamp to front
  EXPECT_EQ(m.indexOfId(QStringLiteral("H")), 0);
  m.insertAt(999, mkTask(QStringLiteral("E")));  // clamp to end
  EXPECT_EQ(m.indexOfId(QStringLiteral("E")), m.rowCount() - 1);
}

// ─── EventModel ───────────────────────────────────────────────────────

TEST(EventModelData, MapsRoles) {
  EventModel m;
  CalEvent e;
  e.id = QStringLiteral("E1");
  e.title = QStringLiteral("Standup");
  e.type = QStringLiteral("standup");
  e.start = 9.5;
  e.end = 10.0;
  e.attendees = QStringLiteral("a,b");
  e.date = QDate(2030, 3, 4);
  e.taskId = QStringLiteral("T7");
  e.profileId = QStringLiteral("P");
  e.context = QStringLiteral("ctx");
  m.reset({e});
  const QModelIndex i = m.index(0, 0);
  EXPECT_DOUBLE_EQ(m.data(i, EventModel::StartRole).toDouble(), 9.5);
  EXPECT_DOUBLE_EQ(m.data(i, EventModel::EndRole).toDouble(), 10.0);
  EXPECT_EQ(m.data(i, EventModel::TypeRole).toString(), QString("standup"));
  EXPECT_EQ(m.data(i, EventModel::TaskIdRole).toString(), QString("T7"));
  EXPECT_EQ(m.data(i, EventModel::ProfileIdRole).toString(), QString("P"));
  EXPECT_EQ(m.data(i, EventModel::ContextRole).toString(), QString("ctx"));
  EXPECT_EQ(m.data(i, EventModel::DateRole).toDate(), QDate(2030, 3, 4));
  EXPECT_FALSE(m.data(QModelIndex(), EventModel::IdRole).isValid());
}

TEST(EventModelLink, DetachTaskAndSetTaskId) {
  EventModel m;
  CalEvent e1;
  e1.id = QStringLiteral("E1");
  e1.taskId = QStringLiteral("T7");
  CalEvent e2;
  e2.id = QStringLiteral("E2");
  e2.taskId = QStringLiteral("T7");
  CalEvent e3;
  e3.id = QStringLiteral("E3");
  e3.taskId = QStringLiteral("T9");
  m.reset({e1, e2, e3});

  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
  m.detachTask(QStringLiteral("T7"));
  EXPECT_EQ(m.data(m.index(0, 0), EventModel::TaskIdRole).toString(), QString());
  EXPECT_EQ(m.data(m.index(1, 0), EventModel::TaskIdRole).toString(), QString());
  EXPECT_EQ(m.data(m.index(2, 0), EventModel::TaskIdRole).toString(), QString("T9"));
  EXPECT_EQ(chg.count(), 2);  // one per matching row

  m.detachTask(QStringLiteral("nomatch"));
  EXPECT_EQ(chg.count(), 2);

  m.setTaskId(QStringLiteral("E3"), QStringLiteral("T1"));
  EXPECT_EQ(m.data(m.index(2, 0), EventModel::TaskIdRole).toString(), QString("T1"));
  m.setTaskId(QStringLiteral("ghost"), QStringLiteral("T1"));  // no-op
  EXPECT_EQ(chg.count(), 3);
}

// ─── PersonModel ──────────────────────────────────────────────────────

TEST(PersonModelData, MapsRolesIncludingColor) {
  PersonModel m;
  Person p;
  p.id = QStringLiteral("p1");
  p.name = QStringLiteral("Ann");
  p.role = QStringLiteral("QA");
  p.question = QStringLiteral("?");
  p.state = QStringLiteral("pinged");
  p.color = QColor(QStringLiteral("#ff0000"));
  m.reset({p});
  const QModelIndex i = m.index(0, 0);
  EXPECT_EQ(m.data(i, PersonModel::NameRole).toString(), QString("Ann"));
  EXPECT_EQ(m.data(i, PersonModel::RoleRole).toString(), QString("QA"));
  EXPECT_EQ(m.data(i, PersonModel::QuestionRole).toString(), QString("?"));
  EXPECT_EQ(m.data(i, PersonModel::StateRole).toString(), QString("pinged"));
  EXPECT_EQ(m.data(i, PersonModel::ColorRole).value<QColor>(), QColor(QStringLiteral("#ff0000")));
  EXPECT_FALSE(m.data(m.index(2, 0), PersonModel::IdRole).isValid());
}

TEST(PersonModelCycle, StateMachineAndDefault) {
  PersonModel m;
  m.reset({mkPerson(QStringLiteral("p1"), QStringLiteral("todo"))});
  m.cycleState(QStringLiteral("p1"));
  EXPECT_EQ(m.data(m.index(0, 0), PersonModel::StateRole).toString(), QString("pinged"));
  m.cycleState(QStringLiteral("p1"));
  EXPECT_EQ(m.data(m.index(0, 0), PersonModel::StateRole).toString(), QString("replied"));
  // Past "replied" is off the rail, not back to the start: ActivePeopleModel
  // hides "idle", so the last cycle is how a finished conversation leaves.
  m.cycleState(QStringLiteral("p1"));
  EXPECT_EQ(m.data(m.index(0, 0), PersonModel::StateRole).toString(), QString("idle"));
  m.cycleState(QStringLiteral("p1"));
  EXPECT_EQ(m.data(m.index(0, 0), PersonModel::StateRole).toString(), QString("todo"));

  // Garbage current state → resets to todo (default branch).
  m.reset({mkPerson(QStringLiteral("p2"), QStringLiteral("banana"))});
  m.cycleState(QStringLiteral("p2"));
  EXPECT_EQ(m.data(m.index(0, 0), PersonModel::StateRole).toString(), QString("todo"));

  // Unknown id → no-op, no crash.
  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
  m.cycleState(QStringLiteral("ghost"));
  EXPECT_EQ(chg.count(), 0);
}

TEST(PersonModelState, SetStateGuardAndTodoCount) {
  PersonModel m;
  m.reset({mkPerson(QStringLiteral("p1"), QStringLiteral("todo")),
           mkPerson(QStringLiteral("p2"), QStringLiteral("pinged")),
           mkPerson(QStringLiteral("p3"), QStringLiteral("todo")),
           mkPerson(QStringLiteral("p4"), QStringLiteral("replied"))});
  EXPECT_EQ(m.todoCount(), 2);

  QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
  m.setState(QStringLiteral("p2"), QStringLiteral("todo"));
  EXPECT_EQ(m.todoCount(), 3);
  EXPECT_EQ(chg.count(), 1);
  m.setState(QStringLiteral("p2"), QStringLiteral("todo"));  // same → no-op
  EXPECT_EQ(chg.count(), 1);
  m.setState(QStringLiteral("ghost"), QStringLiteral("todo"));  // unknown → no-op
  EXPECT_EQ(chg.count(), 1);

  PersonModel empty;
  EXPECT_EQ(empty.todoCount(), 0);
}

// ─── External-identity + label roles (HEAP-140) ───

TEST(TaskModelExternalRoles, RoleNamesExposeTheTrackerLink) {
  const TaskModel m;
  const QHash<int, QByteArray> names = m.roleNames();
  const QList<QByteArray> values = names.values();
  for(const char* expected : {"externalProvider", "externalUrl", "externalKey", "labels", "assignee", "ticket", "searchText"}) {
    EXPECT_TRUE(values.contains(QByteArray(expected))) << "missing role: " << expected;
  }
}

// Twelve QML files read roles as literal `Qt.UserRole + N` offsets. Inserting a
// role anywhere but the end silently reassigns every offset after it, and
// nothing in the build would notice (HEAP-117).
TEST(TaskModelExternalRoles, NewRolesAreAppendedAfterTheExistingOnes) {
  EXPECT_EQ(TaskModel::ExternalProviderRole, Qt::UserRole + 26);
  EXPECT_EQ(TaskModel::ExternalUrlRole, Qt::UserRole + 27);
  EXPECT_EQ(TaskModel::ExternalKeyRole, Qt::UserRole + 28);
  EXPECT_EQ(TaskModel::LabelsRole, Qt::UserRole + 29);
  EXPECT_EQ(TaskModel::AssigneeRole, Qt::UserRole + 30);
  EXPECT_EQ(TaskModel::TicketRole, Qt::UserRole + 31);
  EXPECT_EQ(TaskModel::SearchTextRole, Qt::UserRole + 32);
}

TEST(TaskModelExternalRoles, PulledJiraIssueExposesItsKeyAndLabels) {
  Task t;
  t.id = QStringLiteral("jira-PROJ-7");
  t.externalId = QStringLiteral("PROJ-7");
  t.externalUrl = QStringLiteral("https://x.atlassian.net/browse/PROJ-7");
  t.externalProvider = QStringLiteral("jira");
  t.labels = {Label{QStringLiteral("backend"), QStringLiteral("#ff0000")}, Label{QStringLiteral("p1"), QString()}};
  t.assignee = QStringLiteral("ann");

  TaskModel m;
  m.reset({t});
  const QModelIndex i = m.index(0, 0);

  EXPECT_EQ(m.data(i, TaskModel::ExternalProviderRole).toString(), QStringLiteral("jira"));
  EXPECT_EQ(m.data(i, TaskModel::ExternalKeyRole).toString(), QStringLiteral("PROJ-7"));
  EXPECT_EQ(m.data(i, TaskModel::ExternalUrlRole).toString(), t.externalUrl);
  EXPECT_EQ(m.data(i, TaskModel::AssigneeRole).toString(), QStringLiteral("ann"));

  const QVariantList labels = m.data(i, TaskModel::LabelsRole).toList();
  ASSERT_EQ(labels.size(), 2);
  EXPECT_EQ(labels.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("backend"));
  EXPECT_EQ(labels.at(0).toMap().value(QStringLiteral("color")).toString(), QStringLiteral("#ff0000"));
  EXPECT_TRUE(labels.at(1).toMap().value(QStringLiteral("color")).toString().isEmpty());
}

// An issue-number tracker's key reads as "#123"; a purely local card has none.
TEST(TaskModelExternalRoles, IssueNumberBecomesAHashKeyAndLocalCardsAreEmpty) {
  Task gh;
  gh.id = QStringLiteral("github-68");
  gh.externalId = QStringLiteral("68");
  gh.externalProvider = QStringLiteral("github");

  Task local;
  local.id = QStringLiteral("LTE-1");

  TaskModel m;
  m.reset({gh, local});
  EXPECT_EQ(m.data(m.index(0, 0), TaskModel::ExternalKeyRole).toString(), QStringLiteral("#68"));

  const QModelIndex li = m.index(1, 0);
  EXPECT_TRUE(m.data(li, TaskModel::ExternalKeyRole).toString().isEmpty());
  EXPECT_TRUE(m.data(li, TaskModel::ExternalProviderRole).toString().isEmpty());
  EXPECT_TRUE(m.data(li, TaskModel::ExternalUrlRole).toString().isEmpty());
  EXPECT_TRUE(m.data(li, TaskModel::LabelsRole).toList().isEmpty());
  EXPECT_TRUE(m.data(li, TaskModel::AssigneeRole).toString().isEmpty());
}

// ─── Ticket identity (HEAP-117) ───

// "#42" says nothing about which repo, once a pull spans them.
TEST(TaskModelTicketRole, CrossProjectNumberIsQualifiedByItsRepo) {
  Task t;
  t.id = QStringLiteral("github-web-42");
  t.externalId = QStringLiteral("42");
  t.externalProvider = QStringLiteral("github");
  t.externalMeta.project = QStringLiteral("acme/web");
  t.externalMeta.crossProject = true;
  EXPECT_EQ(externalKeyOf(t), QStringLiteral("web#42"));

  // A scoped pull is unambiguous already: HEAP-117 asks for a plain "#1234".
  t.externalMeta.crossProject = false;
  EXPECT_EQ(externalKeyOf(t), QStringLiteral("#42"));

  // A key that is already human-readable is never decorated.
  Task jira;
  jira.externalId = QStringLiteral("PROJ-7");
  jira.externalProvider = QStringLiteral("jira");
  jira.externalMeta.project = QStringLiteral("PROJ");
  jira.externalMeta.crossProject = true;
  EXPECT_EQ(externalKeyOf(jira), QStringLiteral("PROJ-7"));
}

TEST(TaskModelTicketRole, LocalTaskHasAnEmptyTicketMap) {
  Task local;
  local.id = QStringLiteral("LTE-1");
  local.title = QStringLiteral("write the thing");
  EXPECT_TRUE(ticketToVariant(local).isEmpty());

  TaskModel m;
  m.reset({local});
  EXPECT_TRUE(m.data(m.index(0, 0), TaskModel::TicketRole).toMap().isEmpty());
}

TEST(TaskModelTicketRole, LinkedTaskExposesEveryMetadataField) {
  Task t;
  t.id = QStringLiteral("github-42");
  t.externalId = QStringLiteral("42");
  t.externalUrl = QStringLiteral("https://github.com/acme/web/issues/42");
  t.externalProvider = QStringLiteral("github");
  t.assignee = QStringLiteral("ada");
  t.externalMeta.author = QStringLiteral("grace");
  t.externalMeta.issueType = QStringLiteral("Bug");
  t.externalMeta.project = QStringLiteral("acme/web");
  t.externalMeta.milestone = QStringLiteral("v2");
  t.externalMeta.commentCount = 4;
  t.externalMeta.updatedAt = QDateTime(QDate(2026, 7, 1), QTime(12, 0));

  TaskModel m;
  m.reset({t});
  const QVariantMap ticket = m.data(m.index(0, 0), TaskModel::TicketRole).toMap();
  EXPECT_EQ(ticket.value(QStringLiteral("provider")).toString(), QStringLiteral("github"));
  EXPECT_EQ(ticket.value(QStringLiteral("key")).toString(), QStringLiteral("#42"));
  EXPECT_EQ(ticket.value(QStringLiteral("url")).toString(), t.externalUrl);
  EXPECT_EQ(ticket.value(QStringLiteral("assignee")).toString(), QStringLiteral("ada"));
  EXPECT_EQ(ticket.value(QStringLiteral("author")).toString(), QStringLiteral("grace"));
  EXPECT_EQ(ticket.value(QStringLiteral("issueType")).toString(), QStringLiteral("Bug"));
  EXPECT_EQ(ticket.value(QStringLiteral("project")).toString(), QStringLiteral("acme/web"));
  EXPECT_EQ(ticket.value(QStringLiteral("milestone")).toString(), QStringLiteral("v2"));
  EXPECT_EQ(ticket.value(QStringLiteral("commentCount")).toInt(), 4);
  EXPECT_TRUE(ticket.value(QStringLiteral("updatedAt")).toDateTime().isValid());
}

// One haystack per task, so the five filtering views stop each concatenating
// their own — and so a ticket is findable by key, label and owner.
TEST(TaskModelSearchRole, CoversKeyLabelsAssigneeAndProject) {
  Task t;
  t.id = QStringLiteral("github-42");
  t.title = QStringLiteral("Fix Login");
  t.desc = QStringLiteral("Steps To Repro");
  t.externalId = QStringLiteral("42");
  t.externalProvider = QStringLiteral("github");
  t.assignee = QStringLiteral("Ada");
  t.labels = {Label{QStringLiteral("Backend"), QString()}};
  t.externalMeta.project = QStringLiteral("acme/web");
  t.externalMeta.milestone = QStringLiteral("v2.0");

  TaskModel m;
  m.reset({t});
  const QString hay = m.data(m.index(0, 0), TaskModel::SearchTextRole).toString();
  for(const char* needle : {"fix login", "github-42", "steps to repro", "#42", "ada", "backend", "acme/web", "v2.0"}) {
    EXPECT_TRUE(hay.contains(QLatin1String(needle))) << needle << " missing from: " << hay.toStdString();
  }
  // Lowercased once here so each view can compare against a lowercased query.
  EXPECT_EQ(hay, hay.toLower());
}

// PERA-9: the top bar says "Search tasks, IDs, branches…" and the branch was
// not in the haystack. Its words separated by spaces match too.
TEST(TaskModelSearchRole, CoversTheBranch) {
  Task t;
  t.id = QStringLiteral("APP-101");
  t.title = QStringLiteral("Login throttling");
  t.branch = QStringLiteral("fix/login-rate-limit");

  TaskModel m;
  m.reset({t});
  const QString hay = m.data(m.index(0, 0), TaskModel::SearchTextRole).toString();
  for(const char* needle : {"fix/login-rate-limit", "rate", "rate limit"}) {
    EXPECT_TRUE(hay.contains(QLatin1String(needle))) << needle << " missing from: " << hay.toStdString();
  }
}

// ─── Defer state (HEAP-124) ───

TEST(DeferState, SomedayIsExcludedFromScheduledForTodayAndOnlyItsOwnPredicate) {
  const QDate today(2026, 7, 9);

  Task parked;
  parked.scheduledAt = QDateTime(today, QTime(9, 0));  // scheduled, but parked
  parked.someday = true;

  EXPECT_FALSE(heap::model::isScheduledForToday(parked, today));
  EXPECT_TRUE(heap::model::isSomeday(parked));
  EXPECT_EQ(heap::model::deferState(parked, today), QStringLiteral("someday"));
}

TEST(DeferState, DerivesTodayScheduledAndAnytime) {
  const QDate today(2026, 7, 9);

  Task dueToday;
  dueToday.scheduledAt = QDateTime(today, QTime(16, 0));
  EXPECT_TRUE(heap::model::isScheduledForToday(dueToday, today));
  EXPECT_EQ(heap::model::deferState(dueToday, today), QStringLiteral("today"));

  Task overdue;
  overdue.scheduledAt = QDateTime(today.addDays(-3), QTime(9, 0));
  EXPECT_TRUE(heap::model::isScheduledForToday(overdue, today)) << "an overdue task is still on today's plate";

  Task later;
  later.scheduledAt = QDateTime(today.addDays(2), QTime(9, 0));
  EXPECT_FALSE(heap::model::isScheduledForToday(later, today));
  EXPECT_EQ(heap::model::deferState(later, today), QStringLiteral("scheduled"));

  Task unscheduled;
  EXPECT_FALSE(heap::model::isScheduledForToday(unscheduled, today));
  EXPECT_EQ(heap::model::deferState(unscheduled, today), QStringLiteral("anytime"));
}

// The deadline role stays a QDate so every calendar view keeps its day math.
TEST(TaskModelScheduling, DeadlineRoleIsTheDueDateWithoutItsClockTime) {
  Task t;
  t.dueAt = QDateTime(QDate(2026, 7, 11), QTime(16, 0));
  t.scheduledAt = QDateTime(QDate(2026, 7, 10), QTime(9, 30));
  t.dueHasTime = true;

  TaskModel m;
  m.reset({t});
  const QModelIndex i = m.index(0, 0);

  EXPECT_EQ(m.data(i, TaskModel::DeadlineRole).toDate(), QDate(2026, 7, 11));
  EXPECT_EQ(m.data(i, TaskModel::DueAtRole).toDateTime(), t.dueAt);
  EXPECT_EQ(m.data(i, TaskModel::ScheduledAtRole).toDateTime(), t.scheduledAt);
  EXPECT_TRUE(m.data(i, TaskModel::HasTimeRole).toBool());

  Task undated;
  m.reset({undated});
  EXPECT_FALSE(m.data(m.index(0, 0), TaskModel::DeadlineRole).toDate().isValid());
}

// ─── indexOfId is indexed, and the index follows the rows ───

TEST(TaskModelIndex, FollowsInsertsRemovalsAndResets) {
  TaskModel m;
  m.reset({mkTask(QStringLiteral("A")), mkTask(QStringLiteral("B")), mkTask(QStringLiteral("C"))});
  EXPECT_EQ(m.indexOfId(QStringLiteral("A")), 0);
  EXPECT_EQ(m.indexOfId(QStringLiteral("C")), 2);
  EXPECT_EQ(m.indexOfId(QStringLiteral("ghost")), -1);

  // Inserting at the front shifts everything after it.
  m.insertAt(0, mkTask(QStringLiteral("Z")));
  EXPECT_EQ(m.indexOfId(QStringLiteral("Z")), 0);
  EXPECT_EQ(m.indexOfId(QStringLiteral("A")), 1);
  EXPECT_EQ(m.indexOfId(QStringLiteral("C")), 3);

  // Removing from the middle shifts everything after it.
  m.removeById(QStringLiteral("A"));
  EXPECT_EQ(m.indexOfId(QStringLiteral("A")), -1);
  EXPECT_EQ(m.indexOfId(QStringLiteral("B")), 1);
  EXPECT_EQ(m.indexOfId(QStringLiteral("C")), 2);

  // Appending through upsert.
  m.upsert(mkTask(QStringLiteral("D")));
  EXPECT_EQ(m.indexOfId(QStringLiteral("D")), 3);

  // An in-place upsert moves nothing.
  Task b = mkTask(QStringLiteral("B"));
  b.title = QStringLiteral("edited");
  m.upsert(b);
  EXPECT_EQ(m.indexOfId(QStringLiteral("B")), 1);

  // A reset invalidates everything that went before.
  m.reset({mkTask(QStringLiteral("only"))});
  EXPECT_EQ(m.indexOfId(QStringLiteral("only")), 0);
  EXPECT_EQ(m.indexOfId(QStringLiteral("B")), -1);
  EXPECT_EQ(m.indexOfId(QStringLiteral("D")), -1);

  m.reset({});
  EXPECT_EQ(m.indexOfId(QStringLiteral("only")), -1);
}

TEST(TaskModelIndex, AgreesWithALinearScanAfterAChurnOfEdits) {
  TaskModel m;
  QVector<Task> seed;
  for(int i = 0; i < 50; ++i) {
    seed.push_back(mkTask(QStringLiteral("T") + QString::number(i)));
  }
  m.reset(seed);

  for(int i = 0; i < 50; i += 3) {
    m.removeById(QStringLiteral("T") + QString::number(i));
  }
  for(int i = 0; i < 10; ++i) {
    m.insertAt(i * 2, mkTask(QStringLiteral("N") + QString::number(i)));
  }

  // The index must agree with what a scan of the rows would say, for every row
  // and for a handful of ids that are gone.
  for(int row = 0; row < m.rowCount(); ++row) {
    const QString id = m.data(m.index(row, 0), TaskModel::IdRole).toString();
    EXPECT_EQ(m.indexOfId(id), row) << id.toStdString();
  }
  for(int i = 0; i < 50; i += 3) {
    EXPECT_EQ(m.indexOfId(QStringLiteral("T") + QString::number(i)), -1);
  }
}

// ─── TaskFilterProxy: one board column's view of the model ───
// The board used to hand the whole task model to every status column and let
// each card hide itself, so a profile with N tasks and C columns built N×C
// delegates and ran the filter in JS once per card. These are the rules that
// moved into C++.

namespace {

Task mk(const QString& id, const QString& status, const QString& priority = QStringLiteral("P2")) {
  Task t;
  t.id = id;
  t.title = QStringLiteral("title of ") + id;
  t.status = status;
  t.priority = priority;
  return t;
}

}  // namespace

TEST(TaskFilterProxy, ShowsOnlyItsOwnColumn) {
  TaskModel src;
  src.reset({mk(QStringLiteral("A-1"), QStringLiteral("todo")),
             mk(QStringLiteral("A-2"), QStringLiteral("prog")),
             mk(QStringLiteral("A-3"), QStringLiteral("todo"))});

  TaskFilterProxy proxy;
  proxy.setSourceModel(&src);
  proxy.setStatus(QStringLiteral("todo"));
  EXPECT_EQ(proxy.count(), 2);
  EXPECT_EQ(proxy.data(proxy.index(0, 0), TaskModel::IdRole).toString(), QStringLiteral("A-1"));
  EXPECT_EQ(proxy.data(proxy.index(1, 0), TaskModel::IdRole).toString(), QStringLiteral("A-3"));

  // An empty status is "every column" — used by anything that wants the whole
  // filtered set rather than one lane.
  proxy.setStatus(QString());
  EXPECT_EQ(proxy.count(), 3);
}

// APP-117: the ID sort is natural — the key's text, then its number.
TEST(TaskFilterProxy, TaskIdsCompareNaturally) {
  EXPECT_LT(TaskFilterProxy::compareTaskIds(QStringLiteral("APP-9"), QStringLiteral("APP-10")), 0);
  EXPECT_GT(TaskFilterProxy::compareTaskIds(QStringLiteral("APP-100"), QStringLiteral("APP-10")), 0);
  EXPECT_EQ(TaskFilterProxy::compareTaskIds(QStringLiteral("APP-007"), QStringLiteral("app-7")), 0);
  EXPECT_LT(TaskFilterProxy::compareTaskIds(QStringLiteral("APP-99"), QStringLiteral("BUG-1")), 0);
  EXPECT_LT(TaskFilterProxy::compareTaskIds(QStringLiteral("WEAK PECAP"), QStringLiteral("WEAK PECAP-1")), 0);
  EXPECT_LT(TaskFilterProxy::compareTaskIds(QStringLiteral("APP-12345678901234567890"), QStringLiteral("APP-123456789012345678901")), 0);
}

// WEAK PECAP: a task's net move over a week, from where it first left to
// where it last landed; one that came back is not a move at all.
TEST(WeeklyRecap, NetMovesFollowAChainAndDropRoundTrips) {
  const QDateTime start(QDate(2026, 9, 28), QTime(0, 0));
  const QDateTime end = start.addDays(7);
  const auto at = [&](int day) {
    return start.addDays(day).addSecs(3600);
  };
  const QVector<StatusChange> log = {
      {.taskId = "A", .from = "todo", .to = "prog", .at = at(0)},
      {.taskId = "B", .from = "todo", .to = "prog", .at = at(1)},
      {.taskId = "A", .from = "prog", .to = "review", .at = at(2)},
      {.taskId = "B", .from = "prog", .to = "todo", .at = at(3)},
      {.taskId = "C", .from = "todo", .to = "done", .at = start.addDays(-1)},
      {.taskId = "D", .from = "todo", .to = "done", .at = end},
  };
  const QVector<heap::recap::Move> moves = heap::recap::netMoves(log, start, end);
  ASSERT_EQ(moves.size(), 1);
  EXPECT_EQ(moves.at(0), (heap::recap::Move{.taskId = "A", .from = "todo", .to = "review"}));
  EXPECT_EQ(heap::recap::weekStart(QDate(2026, 10, 4)), QDate(2026, 9, 28));
  EXPECT_EQ(heap::recap::weekStart(QDate(2026, 10, 5)), QDate(2026, 10, 5));
}

// Dogfood 2026-10-30: the recap's "Движение тикетов" — each task's whole way
// through the columns in the week, the stops between included; a round trip
// is kept, the week's edges and no-op changes are not.
TEST(WeeklyRecap, StatusChainsKeepEveryStopInTheWeek) {
  const QDateTime start(QDate(2026, 10, 26), QTime(0, 0));
  const QDateTime end = start.addDays(7);
  const auto at = [&](int day) {
    return start.addDays(day).addSecs(3600);
  };
  const QVector<StatusChange> log = {
      {.taskId = "A", .from = "prog", .to = "review", .at = at(0)},
      {.taskId = "B", .from = "todo", .to = "prog", .at = at(2)},
      {.taskId = "A", .from = "review", .to = "done", .at = at(3)},
      {.taskId = "C", .from = "todo", .to = "prog", .at = start.addSecs(-1)},
      {.taskId = "C", .from = "prog", .to = "prog", .at = at(1)},
      {.taskId = "B", .from = "prog", .to = "todo", .at = at(4)},
      {.taskId = "E", .from = "todo", .to = "prog", .at = at(4)},
      {.taskId = "E", .from = "review", .to = "done", .at = at(5)},
      {.taskId = "D", .from = "todo", .to = "done", .at = end},
  };
  const QVector<heap::recap::Chain> chains = heap::recap::statusChains(log, start, end);
  ASSERT_EQ(chains.size(), 3);
  EXPECT_EQ(chains.at(0).taskId, QStringLiteral("A"));
  EXPECT_EQ(chains.at(0).statuses, (QStringList{"prog", "review", "done"}));
  EXPECT_EQ(chains.at(0).first, at(0));
  EXPECT_EQ(chains.at(0).last, at(3));
  EXPECT_EQ(chains.at(1).taskId, QStringLiteral("B"));
  EXPECT_EQ(chains.at(1).statuses, (QStringList{"todo", "prog", "todo"})) << "a round trip still moved";
  // A gap in the log: the chain names where it was picked up again.
  EXPECT_EQ(chains.at(2).statuses, (QStringList{"todo", "prog", "review", "done"}));
  EXPECT_TRUE(heap::recap::statusChains(log, end, end.addDays(7)).size() == 1);
}

TEST(TaskFilterProxy, HidesArchivedUnlessAsked) {
  Task live = mk(QStringLiteral("A-1"), QStringLiteral("todo"));
  Task gone = mk(QStringLiteral("A-2"), QStringLiteral("todo"));
  gone.archived = true;
  TaskModel src;
  src.reset({live, gone});

  TaskFilterProxy proxy;
  proxy.setSourceModel(&src);
  proxy.setStatus(QStringLiteral("todo"));
  EXPECT_EQ(proxy.count(), 1);

  proxy.setShowArchived(true);
  EXPECT_EQ(proxy.count(), 2);
}

// An all-chips-off filter bar means "no priority filter", not "nothing
// passes" — the board would go blank on load otherwise.
TEST(TaskFilterProxy, AnEmptyPrioritySetIsNoFilterAtAll) {
  TaskModel src;
  src.reset({mk(QStringLiteral("A-1"), QStringLiteral("todo"), QStringLiteral("P0")),
             mk(QStringLiteral("A-2"), QStringLiteral("todo"), QStringLiteral("P2"))});

  TaskFilterProxy proxy;
  proxy.setSourceModel(&src);
  proxy.setStatus(QStringLiteral("todo"));
  EXPECT_EQ(proxy.count(), 2);

  proxy.setPriorities({QStringLiteral("P0")});
  ASSERT_EQ(proxy.count(), 1);
  EXPECT_EQ(proxy.data(proxy.index(0, 0), TaskModel::IdRole).toString(), QStringLiteral("A-1"));

  proxy.setPriorities({});
  EXPECT_EQ(proxy.count(), 2);
}

// The proxy searches the model's own SearchTextRole, so everything that role
// covers is findable from the board — including a ticket's tracker key, its
// labels and its assignee, none of which are in the title or the id.
TEST(TaskFilterProxy, SearchesEverythingTheModelsHaystackCovers) {
  Task a = mk(QStringLiteral("LTE-2700"), QStringLiteral("todo"));
  a.title = QStringLiteral("Fix the Handover");
  a.desc = QStringLiteral("only here: zebra");
  a.externalId = QStringLiteral("1234");
  a.externalProvider = QStringLiteral("github");
  a.assignee = QStringLiteral("Ada");
  a.labels = {Label{QStringLiteral("Backend"), QString()}};
  Task b = mk(QStringLiteral("LTE-2701"), QStringLiteral("todo"));
  b.title = QStringLiteral("Something else");
  TaskModel src;
  src.reset({a, b});

  TaskFilterProxy proxy;
  proxy.setSourceModel(&src);
  proxy.setStatus(QStringLiteral("todo"));

  proxy.setSearchText(QStringLiteral("HANDOVER"));
  EXPECT_EQ(proxy.count(), 1);
  proxy.setSearchText(QStringLiteral("zebra"));  // description only
  EXPECT_EQ(proxy.count(), 1);
  proxy.setSearchText(QStringLiteral("2701"));  // id only
  ASSERT_EQ(proxy.count(), 1);
  EXPECT_EQ(proxy.data(proxy.index(0, 0), TaskModel::IdRole).toString(), QStringLiteral("LTE-2701"));
  // …and the parts of the haystack that are in no field the board renders.
  proxy.setSearchText(QStringLiteral("#1234"));  // the tracker key
  EXPECT_EQ(proxy.count(), 1);
  proxy.setSearchText(QStringLiteral("ada"));  // the assignee
  EXPECT_EQ(proxy.count(), 1);
  proxy.setSearchText(QStringLiteral("backend"));  // a label
  EXPECT_EQ(proxy.count(), 1);
  proxy.setSearchText(QStringLiteral("nothing matches this"));
  EXPECT_EQ(proxy.count(), 0);
  proxy.setSearchText(QString());
  EXPECT_EQ(proxy.count(), 2);
}

// The cards have to follow the model, not a snapshot of it: moving a task to
// another column must remove it from this one and the count must say so.
TEST(TaskFilterProxy, FollowsTheModelWhenATaskMovesColumn) {
  TaskModel src;
  src.reset({mk(QStringLiteral("A-1"), QStringLiteral("todo"))});

  TaskFilterProxy todo;
  todo.setSourceModel(&src);
  todo.setStatus(QStringLiteral("todo"));
  TaskFilterProxy prog;
  prog.setSourceModel(&src);
  prog.setStatus(QStringLiteral("prog"));
  ASSERT_EQ(todo.count(), 1);
  ASSERT_EQ(prog.count(), 0);

  src.setStatus(QStringLiteral("A-1"), QStringLiteral("prog"));
  EXPECT_EQ(todo.count(), 0);
  EXPECT_EQ(prog.count(), 1);
}

TEST(TaskFilterProxy, RolesPassThroughUnchanged) {
  // The board delegate binds named required properties, which only works if
  // the proxy forwards the source's role names verbatim.
  TaskModel src;
  TaskFilterProxy proxy;
  proxy.setSourceModel(&src);
  EXPECT_EQ(proxy.roleNames(), src.roleNames());
}

TEST(TaskFilterProxy, WithNoSourceItIsSimplyEmpty) {
  TaskFilterProxy proxy;
  EXPECT_EQ(proxy.count(), 0);
  proxy.setStatus(QStringLiteral("todo"));
  EXPECT_EQ(proxy.count(), 0);
}

// ─── checklist progress ───────────────────────────────────────────────
// A template ships its steps as markdown task items, and a card could not say
// how far along it was without being opened. The count is a model role so the
// board does not re-scan the text in JS once per visible delegate.

namespace {

constexpr int kChecklistRole = Qt::UserRole + 35;

QVariantMap checklistFor(const QString& desc) {
  Task t;
  t.id = QStringLiteral("T-1");
  t.desc = desc;
  TaskModel m;
  m.reset({t});
  return m.data(m.index(0, 0), kChecklistRole).toMap();
}

}  // namespace

TEST(Checklist, CountsDoneAndTotal) {
  const QVariantMap c = checklistFor(QStringLiteral("- [x] one\n- [ ] two\n- [ ] three"));
  EXPECT_EQ(c.value(QStringLiteral("done")).toInt(), 1);
  EXPECT_EQ(c.value(QStringLiteral("total")).toInt(), 3);
}

TEST(Checklist, AcceptsEveryListMarkerMarkdownDoes) {
  const QVariantMap c = checklistFor(QStringLiteral("- [ ] dash\n* [x] star\n+ [ ] plus\n1. [x] ordered"));
  EXPECT_EQ(c.value(QStringLiteral("total")).toInt(), 4);
  EXPECT_EQ(c.value(QStringLiteral("done")).toInt(), 2);
}

TEST(Checklist, AcceptsAnIndentedItem) {
  const QVariantMap c = checklistFor(QStringLiteral("- [ ] top\n    - [x] nested"));
  EXPECT_EQ(c.value(QStringLiteral("total")).toInt(), 2);
  EXPECT_EQ(c.value(QStringLiteral("done")).toInt(), 1);
}

TEST(Checklist, AcceptsACapitalX) {
  const QVariantMap c = checklistFor(QStringLiteral("- [X] shouted"));
  EXPECT_EQ(c.value(QStringLiteral("done")).toInt(), 1);
}

// No checklist means no chip, so the map has to be empty rather than 0/0 —
// the card decides by total, and 0/0 would draw an empty bar on every task.
TEST(Checklist, ADescriptionWithNoItemsIsEmpty) {
  EXPECT_TRUE(checklistFor(QStringLiteral("just prose")).isEmpty());
  EXPECT_TRUE(checklistFor(QString()).isEmpty());
}

// "[ ]" inside prose is not a task item — only a list marker makes one.
TEST(Checklist, BracketsInProseAreNotItems) {
  EXPECT_TRUE(checklistFor(QStringLiteral("see [ ] for the empty set")).isEmpty());
  EXPECT_TRUE(checklistFor(QStringLiteral("array[x] is an index")).isEmpty());
}

TEST(Checklist, AllDoneReportsEqualCounts) {
  const QVariantMap c = checklistFor(QStringLiteral("- [x] a\n- [x] b"));
  EXPECT_EQ(c.value(QStringLiteral("done")).toInt(), 2);
  EXPECT_EQ(c.value(QStringLiteral("total")).toInt(), 2);
}

// A Jira ticket's Smart Checklist (dogfood 2026-10-22 #6) counts on the card:
// a skipped item is finished, as Smart Checklist counts it; a header is no
// item; the next step is the first open one when nothing else has one.
TEST(Checklist, SmartChecklistItemsCount) {
  Task t;
  t.id = QStringLiteral("T-1");
  t.desc = QStringLiteral("- [x] in the text");
  const auto it = [](const char* text, const char* state) {
    return QJsonObject{{QStringLiteral("text"), QString::fromUtf8(text)}, {QStringLiteral("state"), QString::fromUtf8(state)}};
  };
  t.externalMeta.details.insert(
      QStringLiteral("smartChecklist"),
      QJsonArray{it("Group", "header"), it("spec read", "done"), it("tests", "progress"), it("old idea", "skipped"), it("review", "todo")});
  TaskModel m;
  m.reset({t});
  const QVariantMap c = m.data(m.index(0, 0), kChecklistRole).toMap();
  EXPECT_EQ(c.value(QStringLiteral("smartTotal")).toInt(), 4);
  EXPECT_EQ(c.value(QStringLiteral("smartDone")).toInt(), 2);
  EXPECT_EQ(c.value(QStringLiteral("total")).toInt(), 5);
  EXPECT_EQ(c.value(QStringLiteral("done")).toInt(), 3);

  // Only the Smart Checklist: still a chip, and its first open item is next.
  t.desc.clear();
  m.reset({t});
  const QVariantMap only = m.data(m.index(0, 0), kChecklistRole).toMap();
  EXPECT_EQ(only.value(QStringLiteral("total")).toInt(), 4);
  EXPECT_EQ(only.value(QStringLiteral("next")).toString(), QStringLiteral("tests"));

  // Only headers: nothing to count, no chip.
  t.externalMeta.details.insert(QStringLiteral("smartChecklist"), QJsonArray{it("Group", "header")});
  m.reset({t});
  EXPECT_TRUE(m.data(m.index(0, 0), kChecklistRole).toMap().isEmpty());
}

// Items pulled before statuses were read (dogfood 2026-10-23 #15) reach QML
// with the built-in status of their state; the stored card is not touched.
TEST(Checklist, SmartChecklistItemsReachQmlWithAStatus) {
  Task t;
  t.id = QStringLiteral("T-1");
  t.externalProvider = QStringLiteral("jira");
  t.externalId = QStringLiteral("LTE-1");
  const QJsonArray stored{
      QJsonObject{{QStringLiteral("text"), QStringLiteral("tests")}, {QStringLiteral("state"), QStringLiteral("progress")}}};
  t.externalMeta.details.insert(QStringLiteral("smartChecklist"), stored);
  const QVariantList items = ticketToVariant(t).value(QStringLiteral("details")).toMap().value(QStringLiteral("smartChecklist")).toList();
  ASSERT_EQ(items.size(), 1);
  const QVariantMap item = items.at(0).toMap();
  EXPECT_EQ(item.value(QStringLiteral("statusName")).toString(), QStringLiteral("In Progress"));
  EXPECT_EQ(item.value(QStringLiteral("category")).toString(), QStringLiteral("in-progress"));
  EXPECT_EQ(item.value(QStringLiteral("state")).toString(), QStringLiteral("progress")) << "the old field still there";
  EXPECT_EQ(t.externalMeta.details.value(QStringLiteral("smartChecklist")).toArray(), stored);
}
