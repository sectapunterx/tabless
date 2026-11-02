#include "Models.h"
#include "TaskDefer.h"

#include "integrations/SmartChecklist.h"
#include "integrations/SyncState.h"
#include "local/Checklist.h"
#include "local/Effective.h"
#include "local/Sessions.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

bool isReviewItem(const Task& t) {
  const QString kind = t.externalMeta.details.value(QStringLiteral("kind")).toString();
  return !t.externalId.isEmpty() && (kind == QLatin1String("mr") || kind == QLatin1String("pr"));
}

QString externalKeyOf(const Task& t) {
  if(t.externalId.isEmpty()) {
    return {};
  }
  // A GitLab merge request is "!17", qualified like an issue number when it
  // came from a pull across projects ("web!17").
  if(t.externalId.startsWith(QChar('!'))) {
    if(t.externalMeta.crossProject && !t.externalMeta.project.isEmpty()) {
      return t.externalMeta.project.section(QChar('/'), -1) + t.externalId;
    }
    return t.externalId;
  }
  // Jira hands us the human key already ("PROJ-123"); the issue-number trackers
  // hand us a bare number, which reads as "#123" everywhere they render it.
  bool numeric = false;
  t.externalId.toLongLong(&numeric);
  if(!numeric) {
    return t.externalId;
  }
  // A number pulled from an "assigned to me" endpoint spans projects, so "#42"
  // alone does not say which issue it is. Qualify it with the repo's own name.
  if(t.externalMeta.crossProject && !t.externalMeta.project.isEmpty()) {
    const QString shortName = t.externalMeta.project.section(QChar('/'), -1);
    return shortName + QStringLiteral("#") + t.externalId;
  }
  return QStringLiteral("#") + t.externalId;
}

// The tracker's details as QML reads them. A Smart Checklist stored before
// statuses were read (dogfood 2026-10-23 #15) gets the built-in status of
// each item's state, so every item has `statusName` and `category`.
static QVariantMap detailsToVariant(const QJsonObject& details) {
  const QString key = QStringLiteral("smartChecklist");
  if(!details.contains(key)) {
    return details.toVariantMap();
  }
  QJsonObject d = details;
  d.insert(key, heap::integrations::smartchecklist::withStatuses(details.value(key).toArray()));
  return d.toVariantMap();
}

QVariantMap ticketToVariant(const Task& t) {
  if(t.externalProvider.isEmpty()) {
    return {};
  }
  return {
      {QStringLiteral("provider"), t.externalProvider},
      {QStringLiteral("key"), externalKeyOf(t)},
      {QStringLiteral("url"), t.externalUrl},
      {QStringLiteral("assignee"), t.assignee},
      {QStringLiteral("author"), t.externalMeta.author},
      {QStringLiteral("issueType"), t.externalMeta.issueType},
      {QStringLiteral("project"), t.externalMeta.project},
      {QStringLiteral("milestone"), t.externalMeta.milestone},
      {QStringLiteral("commentCount"), t.externalMeta.commentCount},
      {QStringLiteral("createdAt"), t.externalMeta.createdAt},
      {QStringLiteral("updatedAt"), t.externalMeta.updatedAt},
      {QStringLiteral("unsynced"), !t.externalMeta.unsyncedStatus.isEmpty()},
      {QStringLiteral("gone"), t.externalMeta.goneUpstream},
      {QStringLiteral("outOfScope"), t.externalMeta.outOfScope},
      {QStringLiteral("queued"), t.externalMeta.pushQueued},
      // A field both sides changed: the editor shows the tracker's version
      // next to the local one and lets the user pick.
      {QStringLiteral("conflict"), !t.externalMeta.conflicts.isEmpty()},
      {QStringLiteral("conflicts"), t.externalMeta.conflicts},
      {QStringLiteral("remoteTitle"), t.externalMeta.title},
      {QStringLiteral("remoteBody"), t.externalMeta.body},
      {QStringLiteral("remotePriority"), t.externalMeta.priority},
      // The tracker's status as the last pull saw it, raw and as a column:
      // the other side of a status conflict (APP-163).
      {QStringLiteral("remoteStatus"), t.externalMeta.status},
      {QStringLiteral("remoteColumn"), t.externalMeta.column},
      // A merge / pull request's facts (APP-242) and a Jira sprint (APP-255),
      // as the tracker gave them; "review" says the card is such a request.
      {QStringLiteral("details"), detailsToVariant(t.externalMeta.details)},
      {QStringLiteral("review"), isReviewItem(t)},
  };
}

namespace {

// Markdown task items in a description: "- [ ] thing" and "- [x] thing".
// A template ships checklists, so a card that carries one should be able to
// say how far along it is without the user opening it.
//
// The card's own checklist (APP-236) counts with them: `done`/`total` are
// the sum, `localDone`/`localTotal` and `descDone`/`descTotal` the two parts
// for the tooltip, and `next` the next step (the local list first, else the
// first open item of the description's list).
// A Jira ticket's Smart Checklist (dogfood 2026-10-22 #6) counts too, as
// `smartDone`/`smartTotal`: a skipped item is finished, as Smart Checklist
// counts it, and a header is no item.
QVariantMap checklistOf(const Task& t) {
  const bool descMayHave =
      t.desc.contains(QStringLiteral("[ ]")) || t.desc.contains(QStringLiteral("[x]")) || t.desc.contains(QStringLiteral("[X]"));
  const QJsonArray smartItems = t.externalMeta.details.value(QStringLiteral("smartChecklist")).toArray();
  if(!descMayHave && t.local.checklist.isEmpty() && smartItems.isEmpty()) {
    return {};  // the common case: no scan, no allocation
  }
  int descTotal = 0;
  int descDone = 0;
  QString descNext;
  if(descMayHave) {
    static const QRegularExpression rx(QStringLiteral(R"(^\s*(?:[-*+]|\d+[.)])\s+\[([ xX])\][ \t]*(.*)$)"),
                                       QRegularExpression::MultilineOption);
    auto it = rx.globalMatch(t.desc);
    while(it.hasNext()) {
      const auto m = it.next();
      ++descTotal;
      if(m.captured(1) != QStringLiteral(" ")) {
        ++descDone;
      } else if(descNext.isEmpty()) {
        descNext = m.captured(2).trimmed();
      }
    }
  }
  const heap::local::checklist::Progress local = heap::local::checklist::progress(t.local.checklist);
  const heap::integrations::smartchecklist::Progress smart = heap::integrations::smartchecklist::progressOf(smartItems);
  if(descTotal + local.total + smart.total == 0) {
    return {};
  }
  const int nextAt = heap::local::checklist::nextStep(t.local.checklist);
  return {{QStringLiteral("done"), descDone + local.done + smart.done},
          {QStringLiteral("total"), descTotal + local.total + smart.total},
          {QStringLiteral("localDone"), local.done},
          {QStringLiteral("localTotal"), local.total},
          {QStringLiteral("descDone"), descDone},
          {QStringLiteral("descTotal"), descTotal},
          {QStringLiteral("smartDone"), smart.done},
          {QStringLiteral("smartTotal"), smart.total},
          {QStringLiteral("next"),
           nextAt >= 0           ? t.local.checklist.at(nextAt).text
           : !descNext.isEmpty() ? descNext
                                 : smart.next}};
}

// What a card shows of its local layer (APP-237…241, 251) in one role, so
// the card's marks read one map instead of a role each. Empty for a task with
// nothing local, which is most of them.
QVariantMap localMarksOf(const Task& t) {
  const TaskLocal& l = t.local;
  if(l.notes.trimmed().isEmpty() && l.commentDraft.trimmed().isEmpty() && l.tags.isEmpty() && l.myPriority.isEmpty() &&
     !l.myDueAt.isValid() && l.related.isEmpty()) {
    return {};
  }
  QVariantList tags;
  for(const LocalTag& tag : l.tags) {
    tags.append(QVariantMap{{QStringLiteral("id"), tag.id}, {QStringLiteral("color"), tag.color}});
  }
  return {
      {QStringLiteral("notes"), !l.notes.trimmed().isEmpty()},
      {QStringLiteral("draft"), !l.commentDraft.trimmed().isEmpty()},
      {QStringLiteral("tags"), tags},
      {QStringLiteral("myPriority"), l.myPriority},
      {QStringLiteral("myDue"), l.myDueAt.isValid()},
      // The tracker moved its own value since I set mine (APP-238).
      {QStringLiteral("trackerChanged"),
       (!l.myPriority.isEmpty() && l.myPriorityBase != t.priority) || (l.myDueAt.isValid() && l.myDueBase != t.dueAt)},
      {QStringLiteral("related"), static_cast<int>(l.related.size())},
  };
}

}  // namespace

// One lowercase haystack per task, so the five views that filter on a search
// box each read one role instead of concatenating four themselves — and so a
// ticket is findable by its key, its labels and its owner, not just its title.
QString TaskModel::searchTextOf(const Task& t) {
  QStringList parts{t.title, t.id, t.desc, externalKeyOf(t), t.assignee, t.externalMeta.project, t.externalMeta.milestone};
  for(const Label& l : t.labels) {
    parts.append(l.id);
  }
  // My own layer is findable too (APP-237, 239, 241): the notepad, my tags,
  // the checklist and the comment draft.
  if(!t.local.notes.isEmpty()) {
    parts.append(t.local.notes);
  }
  for(const LocalTag& tag : t.local.tags) {
    parts.append(tag.id);
  }
  for(const LocalCheckItem& c : t.local.checklist) {
    parts.append(c.text);
  }
  if(!t.local.commentDraft.isEmpty()) {
    parts.append(t.local.commentDraft);
  }
  // The search box promises branches (PERA-9). Also with its separators read
  // as spaces, so "rate limit" finds fix/login-rate-limit.
  if(!t.branch.isEmpty()) {
    static const QRegularExpression kBranchSeparators(QStringLiteral("[-_/.]+"));
    parts.append(t.branch);
    parts.append(QString(t.branch).replace(kBranchSeparators, QStringLiteral(" ")));
  }
  // Folded as heap::query::foldSearch folds the typed words: «ё» reads as «е»
  // (A3-11). Spelled out here so the model does not depend on the query code.
  QString hay = parts.join(QChar(' ')).toLower();
  hay.replace(QChar(0x0451), QChar(0x0435));
  return hay;
}

const QString& TaskModel::searchTextAt(int row) const {
  // Built on first read and kept until the row's text can have changed
  // (upsert, reset). Every board column is a proxy over the whole model and
  // re-tests every row on each keystroke; rebuilding the haystack there —
  // title, description, labels, joined and lowercased — cost ~250 ms per
  // keystroke at 3k tasks. The status/archive/timer setters leave it alone:
  // none of the fields it is made of move there.
  if(m_searchCache.size() != m_items.size()) {
    m_searchCache = QVector<QString>(m_items.size());
  }
  QString& hay = m_searchCache[row];
  if(hay.isNull()) {
    hay = searchTextOf(m_items[row]);
  }
  return hay;
}

QVariantList labelsToVariant(const QVector<Label>& labels) {
  QVariantList out;
  out.reserve(labels.size());
  for(const Label& l : labels) {
    out.append(QVariantMap{{QStringLiteral("id"), l.id}, {QStringLiteral("color"), l.color}});
  }
  return out;
}

QVector<Label> labelsFromVariant(const QVariantList& list) {
  QVector<Label> out;
  out.reserve(list.size());
  for(const QVariant& v : list) {
    // A plain string is accepted so the editor can hand over comma-separated
    // text without inventing colours for it.
    if(v.typeId() == QMetaType::QString) {
      const QString id = v.toString().trimmed();
      if(!id.isEmpty()) {
        out.append(Label{id, {}});
      }
      continue;
    }
    const QVariantMap m = v.toMap();
    const QString id = m.value(QStringLiteral("id")).toString().trimmed();
    if(!id.isEmpty()) {
      out.append(Label{id, m.value(QStringLiteral("color")).toString()});
    }
  }
  return out;
}

QHash<int, QByteArray> TaskModel::roleNames() const {
  return {
      {IdRole, "id"},
      {TitleRole, "title"},
      {DescRole, "desc"},
      {PriorityRole, "priority"},
      {StatusRole, "status"},
      {DeadlineRole, "deadline"},
      {BranchRole, "branch"},
      {StatusChangedAtRole, "statusChangedAt"},
      {ArchivedRole, "archived"},
      {BlockedStuckRole, "blockedStuck"},
      {PrStateRole, "prState"},
      {PrNumberRole, "prNumber"},
      {PrUrlRole, "prUrl"},
      {GitAheadRole, "gitAhead"},
      {GitBehindRole, "gitBehind"},
      {RecentCommitsRole, "recentCommits"},
      {TrackedSecondsRole, "trackedSeconds"},
      {IsTimingRole, "isTiming"},
      {RecurrenceRole, "recurrence"},
      {ScheduledAtRole, "scheduledAt"},
      {DueAtRole, "dueAt"},
      {HasTimeRole, "hasTime"},
      {EstimateMinutesRole, "estimateMinutes"},
      {SomedayRole, "someday"},
      {DeferStateRole, "deferState"},
      {ExternalProviderRole, "externalProvider"},
      {ExternalUrlRole, "externalUrl"},
      {ExternalKeyRole, "externalKey"},
      {LabelsRole, "labels"},
      {AssigneeRole, "assignee"},
      {TicketRole, "ticket"},
      {SearchTextRole, "searchText"},
      {RankRole, "rank"},
      {BlocksRole, "blocks"},
      {ChecklistRole, "checklist"},
      {ScheduledHasTimeRole, "scheduledHasTime"},
      {DueHasTimeRole, "dueHasTime"},
      {AttachmentCountRole, "attachmentCount"},
      {PrMoveRole, "prMove"},
      {PrMoveReasonRole, "prMoveReason"},
      {LocalRole, "local"},
  };
}

int TaskModel::roleOf(const QString& name) const {
  const QByteArray needle = name.toUtf8();
  const QHash<int, QByteArray> names = roleNames();
  for(auto it = names.constBegin(); it != names.constEnd(); ++it) {
    if(it.value() == needle) {
      return it.key();
    }
  }
  return -1;
}

QVariant TaskModel::data(const QModelIndex& idx, int role) const {
  if(!idx.isValid() || idx.row() < 0 || idx.row() >= m_items.size()) {
    return {};
  }
  const Task& t = m_items[idx.row()];
  switch(role) {
    case IdRole:
      return t.id;
    case TitleRole:
      return t.title;
    case DescRole:
      return t.desc;
    case PriorityRole:
      return heap::local::effectivePriority(t);
    case StatusRole:
      return t.status;
    case DeadlineRole:
      // Kept a QDate: every calendar/timeline view does whole-day arithmetic on
      // it. The clock component lives on DueAtRole / ScheduledAtRole.
      return heap::local::effectiveDueAt(t).isValid() ? heap::local::effectiveDueAt(t).date() : QDate();
    case BranchRole:
      return t.branch;
    case StatusChangedAtRole:
      return t.statusChangedAt;
    case ArchivedRole:
      return t.archived;
    case BlockedStuckRole:
      return m_blockedStuck.contains(t.id);
    case PrStateRole:
      return m_git.value(t.id).prState;
    case PrNumberRole:
      return m_git.value(t.id).prNumber;
    case PrMoveRole:
      return m_git.value(t.id).prMove;
    case PrMoveReasonRole:
      return m_git.value(t.id).prMoveReason;
    case PrUrlRole:
      return m_git.value(t.id).prUrl;
    case GitAheadRole:
      return m_git.value(t.id).ahead;
    case GitBehindRole:
      return m_git.value(t.id).behind;
    case RecentCommitsRole:
      return m_git.value(t.id).recentCommits;
    case TrackedSecondsRole:
      return t.trackedSeconds;
    case IsTimingRole:
      return t.timerStartedAt.isValid();
    case RecurrenceRole:
      return t.recurrence;
    case ScheduledAtRole:
      return t.scheduledAt;
    case DueAtRole:
      return heap::local::effectiveDueAt(t);
    case HasTimeRole:
    case DueHasTimeRole:
      return heap::local::effectiveDueHasTime(t);
    case ScheduledHasTimeRole:
      return t.scheduledHasTime;
    case EstimateMinutesRole:
      return t.estimateMinutes;
    case SomedayRole:
      return t.someday;
    case DeferStateRole:
      return heap::model::deferState(t, QDate::currentDate());
    case ExternalProviderRole:
      return t.externalProvider;
    case ExternalUrlRole:
      return t.externalUrl;
    case ExternalKeyRole:
      return externalKeyOf(t);
    case LabelsRole:
      return labelsToVariant(t.labels);
    case AssigneeRole:
      return t.assignee;
    case TicketRole: {
      QVariantMap m = ticketToVariant(t);
      if(!m.isEmpty()) {
        heap::integrations::SyncFacts f;
        f.pushing = m_pushing.contains(t.id);
        f.queued = t.externalMeta.pushQueued;
        f.unsyncedStatus = t.externalMeta.unsyncedStatus;
        f.conflicts = t.externalMeta.conflicts;
        f.gone = t.externalMeta.goneUpstream;
        m.insert(QStringLiteral("syncState"), heap::integrations::syncStateName(heap::integrations::syncStateOf(f)));
        m.insert(QStringLiteral("syncError"), m_pushErrors.value(t.id));
        // The tracker's original estimate and logged time, for the card's
        // "7ч 30м / 10ч" line (item 19); absent when it said nothing.
        const QJsonObject est = t.externalMeta.details.value(QStringLiteral("estimate")).toObject();
        for(const auto& [from, to] : {std::pair{"original", "originalMinutes"}, std::pair{"spent", "spentMinutes"}}) {
          const QJsonValue v = est.value(QLatin1String(from));
          if(v.isDouble() && v.toDouble() > 0) {
            m.insert(QLatin1String(to), static_cast<int>(std::lround(v.toDouble() / 60.0)));
          }
        }
      }
      return m;
    }
    case SearchTextRole:
      return searchTextAt(idx.row());
    case RankRole:
      return t.rank;
    case ChecklistRole:
      return checklistOf(t);
    case LocalRole:
      return localMarksOf(t);
    case AttachmentCountRole:
      return static_cast<int>(t.attachments.size());
    case BlocksRole: {
      QStringList ids;
      for(const TaskLink& l : t.links) {
        if(l.type == QStringLiteral("blocks")) {
          ids << l.targetId;
        }
      }
      return ids;
    }
  }
  return {};
}

void TaskModel::reset(QVector<Task> items) {
  beginResetModel();
  m_items = std::move(items);
  m_searchCache = QVector<QString>(m_items.size());
  m_git.clear();
  m_pushing.clear();
  m_pushErrors.clear();
  m_indexDirty = true;
  endResetModel();
}

QVariantMap TaskModel::gitFactsFor(const QString& id) const {
  const auto it = m_git.constFind(id);
  if(it == m_git.constEnd()) {
    return {};
  }
  return {{QStringLiteral("prState"), it->prState},
          {QStringLiteral("prNumber"), it->prNumber},
          {QStringLiteral("prUrl"), it->prUrl},
          {QStringLiteral("prChecks"), it->prChecks},
          {QStringLiteral("prDraft"), it->prDraft},
          {QStringLiteral("prReviewDecision"), it->prReviewDecision},
          {QStringLiteral("prMergeable"), it->prMergeable},
          {QStringLiteral("prReviewRequests"), it->prReviewRequests},
          {QStringLiteral("prChecksHead"), it->prChecksHead},
          {QStringLiteral("prHeadSha"), it->prHeadSha},
          {QStringLiteral("prSeenAt"), it->prSeenAt}};
}

void TaskModel::setGitInfoForId(const QString& id, const QVariantMap& info) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  GitInfo& g = m_git[id];
  if(info.contains(QStringLiteral("prState"))) {
    g.prState = info.value(QStringLiteral("prState")).toString();
  }
  if(info.contains(QStringLiteral("prNumber"))) {
    g.prNumber = info.value(QStringLiteral("prNumber")).toInt();
  }
  if(info.contains(QStringLiteral("prUrl"))) {
    g.prUrl = info.value(QStringLiteral("prUrl")).toString();
  }
  if(info.contains(QStringLiteral("prChecks"))) {
    g.prChecks = info.value(QStringLiteral("prChecks")).toString();
    // Whatever said the checks says what they are about, or nobody does.
    g.prChecksHead = info.value(QStringLiteral("prChecksHead")).toString();
    g.prHeadSha = info.value(QStringLiteral("prHeadSha")).toString();
    g.prSeenAt = info.value(QStringLiteral("prSeenAt")).toDateTime();
  }
  if(info.contains(QStringLiteral("prDraft"))) {
    g.prDraft = info.value(QStringLiteral("prDraft")).toBool();
  }
  if(info.contains(QStringLiteral("prReviewDecision"))) {
    g.prReviewDecision = info.value(QStringLiteral("prReviewDecision")).toString();
  }
  if(info.contains(QStringLiteral("prMergeable"))) {
    g.prMergeable = info.value(QStringLiteral("prMergeable")).toString();
  }
  if(info.contains(QStringLiteral("prReviewRequests"))) {
    g.prReviewRequests = info.value(QStringLiteral("prReviewRequests")).toStringList();
  }
  if(info.contains(QStringLiteral("prMove"))) {
    g.prMove = info.value(QStringLiteral("prMove")).toString();
    g.prMoveReason = info.value(QStringLiteral("prMoveReason")).toString();
  }
  if(info.contains(QStringLiteral("ahead"))) {
    g.ahead = info.value(QStringLiteral("ahead")).toInt();
  }
  if(info.contains(QStringLiteral("behind"))) {
    g.behind = info.value(QStringLiteral("behind")).toInt();
  }
  if(info.contains(QStringLiteral("recentCommits"))) {
    g.recentCommits = info.value(QStringLiteral("recentCommits")).toList();
  }
  const QModelIndex mi = index(row, 0);
  emit dataChanged(
      mi, mi, {PrStateRole, PrNumberRole, PrUrlRole, PrMoveRole, PrMoveReasonRole, GitAheadRole, GitBehindRole, RecentCommitsRole});
}

void TaskModel::setPushRuntime(const QString& id, bool pushing, const QString& error) {
  const bool wasPushing = m_pushing.contains(id);
  const QString oldError = m_pushErrors.value(id);
  if(wasPushing == pushing && oldError == error) {
    return;
  }
  if(pushing) {
    m_pushing.insert(id);
  } else {
    m_pushing.remove(id);
  }
  if(error.isEmpty()) {
    m_pushErrors.remove(id);
  } else {
    m_pushErrors.insert(id, error);
  }
  const int row = indexOfId(id);
  if(row >= 0) {
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi, {TicketRole});
  }
}

void TaskModel::clearAllGitInfo() {
  if(m_git.isEmpty() || m_items.isEmpty()) {
    m_git.clear();
    return;
  }
  m_git.clear();
  emit dataChanged(index(0, 0),
                   index(static_cast<int>(m_items.size()) - 1, 0),
                   {PrStateRole, PrNumberRole, PrUrlRole, PrMoveRole, PrMoveReasonRole, GitAheadRole, GitBehindRole, RecentCommitsRole});
}

int TaskModel::indexOfId(const QString& id) const {
  // Called on essentially every mutation, and by AppController from ~30 more
  // places — mergeExternalTasks alone walks the model three times per pulled
  // issue. Lazily indexed: any structural change flips the dirty flag and the
  // next lookup rebuilds, which keeps the row-shifting cases (insertAt,
  // removeById) correct without each one maintaining the map by hand.
  if(m_indexDirty) {
    m_index.clear();
    m_index.reserve(m_items.size());
    for(int i = 0; i < m_items.size(); ++i) {
      m_index.insert(m_items[i].id, i);
    }
    m_indexDirty = false;
  }
  return m_index.value(id, -1);
}

void TaskModel::setStatus(const QString& id, const QString& status, const QDateTime& changedAt, std::optional<double> rank) {
  const int row = indexOfId(id);
  if(row < 0 || m_items[row].status == status) {
    return;
  }
  const Task before = m_observer ? m_items[row] : Task();
  m_items[row].status = status;
  m_items[row].statusChangedAt = changedAt.isValid() ? changedAt : QDateTime::currentDateTime();
  QList<int> roles{StatusRole, StatusChangedAtRole};
  if(rank.has_value()) {
    m_items[row].rank = *rank;
    roles.append(RankRole);
  }
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, roles);
  if(m_observer) {
    m_observer(&before, m_items[row]);
  }
}

void TaskModel::stampStatusChange(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  m_items[row].statusChangedAt = QDateTime::currentDateTime();
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, {StatusChangedAtRole});
}

void TaskModel::setRanks(const QHash<QString, double>& ranks) {
  int first = -1;
  int last = -1;
  for(auto it = ranks.constBegin(); it != ranks.constEnd(); ++it) {
    const int row = indexOfId(it.key());
    if(row < 0 || m_items[row].rank == it.value()) {
      continue;
    }
    m_items[row].rank = it.value();
    first = first < 0 ? row : qMin(first, row);
    last = qMax(last, row);
  }
  if(first >= 0) {
    emit dataChanged(index(first, 0), index(last, 0), {RankRole});
  }
}

void TaskModel::startTiming(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0 || m_items[row].timerStartedAt.isValid()) {
    return;
  }
  // Only one task tracks at a time — stop any other running timer first.
  for(int i = 0; i < m_items.size(); ++i) {
    if(i != row && m_items[i].timerStartedAt.isValid()) {
      stopTiming(m_items[i].id);
    }
  }
  m_items[row].timerStartedAt = QDateTime::currentDateTime();
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, {TrackedSecondsRole, IsTimingRole});
}

void TaskModel::stopTiming(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0 || !m_items[row].timerStartedAt.isValid()) {
    return;
  }
  // A session of its own (APP-251); the total is the sessions' sum, with
  // whatever the task had tracked before sessions kept as one undated one.
  Task& t = m_items[row];
  heap::local::sessions::adoptTotal(t.local.sessions, t.trackedSeconds);
  heap::local::sessions::record(t.local.sessions, t.timerStartedAt, QDateTime::currentDateTime());
  t.trackedSeconds = heap::local::sessions::total(t.local.sessions);
  t.timerStartedAt = QDateTime();  // clear → stopped
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, {TrackedSecondsRole, IsTimingRole});
}

void TaskModel::setArchived(const QString& id, bool archived) {
  const int row = indexOfId(id);
  if(row < 0 || m_items[row].archived == archived) {
    return;
  }
  m_items[row].archived = archived;
  // The archive groups by the month the status last changed (DG-161). A task
  // archived by hand that never changed status had no month and fell into
  // "no date" (R3-056); its archiving is that change.
  QList<int> roles{ArchivedRole};
  if(archived && !m_items[row].statusChangedAt.isValid()) {
    m_items[row].statusChangedAt = QDateTime::currentDateTime();
    roles << StatusChangedAtRole;
  }
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, roles);
}

void TaskModel::setBlockedStuckIds(const QSet<QString>& ids) {
  if(ids == m_blockedStuck) {
    return;
  }
  QSet<QString> changed = m_blockedStuck;
  changed.unite(ids);
  m_blockedStuck = ids;
  if(m_items.isEmpty()) {
    return;
  }
  int lo = m_items.size();
  int hi = -1;
  for(int i = 0; i < m_items.size(); ++i) {
    if(changed.contains(m_items[i].id)) {
      lo = std::min(i, lo);
      hi = std::max(i, hi);
    }
  }
  if(hi < 0) {
    return;
  }
  emit dataChanged(index(lo, 0), index(hi, 0), {BlockedStuckRole});
}

void TaskModel::upsert(const Task& t) {
  const int row = indexOfId(t.id);
  if(row >= 0) {
    const Task before = m_observer ? m_items[row] : Task();
    m_items[row] = t;
    if(row < m_searchCache.size()) {
      m_searchCache[row] = QString();
    }
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi);
    if(m_observer) {
      m_observer(&before, t);
    }
  } else {
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.push_back(t);
    if(m_searchCache.size() == m_items.size() - 1) {
      m_searchCache.push_back(QString());
    }
    m_indexDirty = true;
    endInsertRows();
    if(m_observer) {
      m_observer(nullptr, t);
    }
  }
}

void TaskModel::insertAt(int row, const Task& t) {
  row = qBound(0, row, m_items.size());
  beginInsertRows({}, row, row);
  m_items.insert(row, t);
  if(row <= m_searchCache.size() && m_searchCache.size() == m_items.size() - 1) {
    m_searchCache.insert(row, QString());
  }
  m_indexDirty = true;  // every row at or after this one shifted
  endInsertRows();
  if(m_observer) {
    m_observer(nullptr, t);
  }
}

void TaskModel::removeById(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  beginRemoveRows({}, row, row);
  m_items.removeAt(row);
  if(row < m_searchCache.size() && m_searchCache.size() == m_items.size() + 1) {
    m_searchCache.removeAt(row);
  }
  m_indexDirty = true;  // every row after this one shifted
  endRemoveRows();
}

// ---- EventModel ----

QHash<int, QByteArray> EventModel::roleNames() const {
  return {
      {IdRole, "id"},
      {TitleRole, "title"},
      {TypeRole, "type"},
      {StartRole, "start"},
      {EndRole, "end"},
      {AttendeesRole, "attendees"},
      {DateRole, "date"},
      {TaskIdRole, "taskId"},
      {ProfileIdRole, "profileId"},
      {ContextRole, "context"},
      {AllDayRole, "allDay"},
      {EndDateRole, "endDate"},
      {RRuleRole, "rrule"},
      {MasterIdRole, "masterId"},
      {OccurrenceDateRole, "occurrenceDate"},
  };
}

QVariant EventModel::data(const QModelIndex& idx, int role) const {
  if(!idx.isValid() || idx.row() < 0 || idx.row() >= m_items.size()) {
    return {};
  }
  const CalEvent& e = m_items[idx.row()];
  switch(role) {
    case IdRole:
      return e.id;
    case TitleRole:
      return e.title;
    case TypeRole:
      return e.type;
    case StartRole:
      return e.start;
    case EndRole:
      return e.end;
    case AttendeesRole:
      return e.attendees;
    case DateRole:
      return e.date;
    case TaskIdRole:
      return e.taskId;
    case ProfileIdRole:
      return e.profileId;
    case ContextRole:
      return e.context;
    case AllDayRole:
      return e.allDay;
    case EndDateRole:
      // Always a usable date, so a delegate can subtract without a validity
      // check: a single-day event reports its own day.
      return e.endDate.isValid() ? e.endDate : e.date;
    case RRuleRole:
      return e.rrule;
    case MasterIdRole:
      return e.masterId;
    case OccurrenceDateRole:
      // The stored model holds masters and overrides, never expanded
      // occurrences, so this is the event's own date.
      return e.date;
  }
  return {};
}

// ───────────────── DocPageModel ─────────────────

QHash<int, QByteArray> DocPageModel::roleNames() const {
  return {
      {IdRole, "id"},
      {ParentIdRole, "parentId"},
      {TitleRole, "title"},
      {RankRole, "rank"},
      {CreatedRole, "created"},
      {UpdatedRole, "updated"},
      {ExcerptRole, "excerpt"},
  };
}

int DocPageModel::roleOf(const QString& name) const {
  const QByteArray needle = name.toUtf8();
  const QHash<int, QByteArray> names = roleNames();
  for(auto it = names.constBegin(); it != names.constEnd(); ++it) {
    if(it.value() == needle) {
      return it.key();
    }
  }
  return -1;
}

namespace {

// One line of markdown as a reader would say it: no list marker or checkbox,
// no emphasis markers, a link as its label. A list row showed
// "- [ ] **Ship** the [build](http://…)" verbatim.
QString plainLine(QString line) {
  static const QRegularExpression marker(QStringLiteral(R"(^(?:[-*+]|\d+[.)])\s+(?:\[[ xX]\]\s+)?)"));
  // A backslash keeps # or | in a note name literal ([[A\|B options]]), so an
  // escaped bar is part of the name, not the label separator (KNOW-6).
  static const QRegularExpression labelledWiki(QStringLiteral(R"(\[\[(?:\\.|[^\]|\\])*\|([^\]]+)\]\])"));
  static const QRegularExpression wiki(QStringLiteral(R"(\[\[([^\]]*)\]\])"));
  static const QRegularExpression escapedLinkChar(QStringLiteral(R"(\\([#|]))"));
  static const QRegularExpression link(QStringLiteral(R"(!?\[([^\]]*)\]\([^)]*\))"));
  static const QRegularExpression emphasis(QStringLiteral(R"((\*\*|__|~~|==|`|\*|_)(?=\S)(.+?)(?<=\S)\1)"));
  line.remove(marker);
  line.replace(labelledWiki, QStringLiteral("\\1"));
  line.replace(wiki, QStringLiteral("\\1"));
  line.replace(escapedLinkChar, QStringLiteral("\\1"));
  line.replace(link, QStringLiteral("\\1"));
  for(int pass = 0; pass < 3 && line.contains(emphasis); ++pass) {
    line.replace(emphasis, QStringLiteral("\\2"));
  }
  return line.trimmed();
}

// The first line of `body` that is not `title` and not markdown punctuation,
// so a list or tree row says something the heading does not. Walks the text
// line by line instead of splitting it: splitting a 3 MB note into a list of
// lines for one excerpt, on every save, was part of what made it slow.
QString excerptOf(const QString& body, const QString& title) {
  qsizetype start = 0;
  bool inFence = false;
  bool inFrontmatter = body.startsWith(QStringLiteral("---\n")) || body.startsWith(QStringLiteral("---\r\n"));
  bool first = true;
  while(start <= body.size()) {
    qsizetype end = body.indexOf(QLatin1Char('\n'), start);
    if(end < 0) {
      end = body.size();
    }
    QString line = body.mid(start, end - start).trimmed();
    start = end + 1;
    if(inFrontmatter) {
      if(!first && line == QStringLiteral("---")) {
        inFrontmatter = false;
      }
      first = false;
      continue;
    }
    first = false;
    if(line.startsWith(QStringLiteral("```")) || line.startsWith(QStringLiteral("~~~"))) {
      inFence = !inFence;
      continue;
    }
    if(inFence || line.isEmpty() || line == QStringLiteral("---")) {
      continue;
    }
    while(line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('>'))) {
      line = line.mid(1).trimmed();
    }
    line = plainLine(line);
    if(line.isEmpty() || line == title) {
      continue;
    }
    return line.left(120);
  }
  return {};
}

QString pageExcerpt(const DocPage& p) {
  return excerptOf(p.body, p.title);
}

}  // namespace

QVariant DocPageModel::data(const QModelIndex& idx, int role) const {
  if(!idx.isValid() || idx.row() < 0 || idx.row() >= m_items.size()) {
    return {};
  }
  const DocPage& p = m_items[idx.row()];
  switch(role) {
    case IdRole:
      return p.id;
    case ParentIdRole:
      return p.parentId;
    case TitleRole:
      return p.title;
    case RankRole:
      return p.rank;
    case CreatedRole:
      return p.created;
    case UpdatedRole:
      return p.updated;
    case ExcerptRole:
      return pageExcerpt(p);
  }
  return {};
}

void DocPageModel::reset(QVector<DocPage> items) {
  beginResetModel();
  m_items = std::move(items);
  endResetModel();
}

int DocPageModel::indexOfId(const QString& id) const {
  for(int i = 0; i < m_items.size(); ++i) {
    if(m_items[i].id == id) {
      return i;
    }
  }
  return -1;
}

void DocPageModel::upsert(const DocPage& p) {
  const int row = indexOfId(p.id);
  if(row >= 0) {
    m_items[row] = p;
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi);
  } else {
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.push_back(p);
    endInsertRows();
  }
}

void DocPageModel::insertAt(int row, const DocPage& p) {
  row = qBound(0, row, static_cast<int>(m_items.size()));
  beginInsertRows({}, row, row);
  m_items.insert(row, p);
  endInsertRows();
}

void DocPageModel::removeById(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  beginRemoveRows({}, row, row);
  m_items.removeAt(row);
  endRemoveRows();
}

// ───────────────── NoteModel ─────────────────

QHash<int, QByteArray> NoteModel::roleNames() const {
  return {
      {IdRole, "id"},
      {TitleRole, "title"},
      {FolderRole, "folder"},
      {PinnedRole, "pinned"},
      {CreatedRole, "created"},
      {UpdatedRole, "updated"},
      {ExcerptRole, "excerpt"},
  };
}

int NoteModel::roleOf(const QString& name) const {
  const QByteArray needle = name.toUtf8();
  const QHash<int, QByteArray> names = roleNames();
  for(auto it = names.constBegin(); it != names.constEnd(); ++it) {
    if(it.value() == needle) {
      return it.key();
    }
  }
  return -1;
}

namespace {

// The first line of a note that is not its own title and not markdown
// punctuation, so a list row says something about the note rather than
// repeating its heading back.
QString noteExcerpt(const Note& n) {
  return excerptOf(n.body, n.title);
}

}  // namespace

QVariant NoteModel::data(const QModelIndex& idx, int role) const {
  if(!idx.isValid() || idx.row() < 0 || idx.row() >= m_items.size()) {
    return {};
  }
  const Note& n = m_items[idx.row()];
  switch(role) {
    case IdRole:
      return n.id;
    case TitleRole:
      return n.title;
    case FolderRole:
      return n.folder;
    case PinnedRole:
      return n.pinned;
    case CreatedRole:
      return n.created;
    case UpdatedRole:
      return n.updated;
    case ExcerptRole:
      return noteExcerpt(n);
  }
  return {};
}

void NoteModel::reset(QVector<Note> items) {
  beginResetModel();
  m_items = std::move(items);
  endResetModel();
}

int NoteModel::indexOfId(const QString& id) const {
  for(int i = 0; i < m_items.size(); ++i) {
    if(m_items[i].id == id) {
      return i;
    }
  }
  return -1;
}

void NoteModel::upsert(const Note& n) {
  const int row = indexOfId(n.id);
  if(row >= 0) {
    m_items[row] = n;
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi);
  } else {
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.push_back(n);
    endInsertRows();
  }
}

void NoteModel::insertAt(int row, const Note& n) {
  row = qBound(0, row, static_cast<int>(m_items.size()));
  beginInsertRows({}, row, row);
  m_items.insert(row, n);
  endInsertRows();
}

void NoteModel::removeById(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  beginRemoveRows({}, row, row);
  m_items.removeAt(row);
  endRemoveRows();
}

void EventModel::reset(QVector<CalEvent> items) {
  beginResetModel();
  m_items = std::move(items);
  endResetModel();
}

int EventModel::indexOfId(const QString& id) const {
  for(int i = 0; i < m_items.size(); ++i) {
    if(m_items[i].id == id) {
      return i;
    }
  }
  return -1;
}

void EventModel::upsert(const CalEvent& e) {
  const int row = indexOfId(e.id);
  if(row >= 0) {
    m_items[row] = e;
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi);
  } else {
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.push_back(e);
    endInsertRows();
  }
}

void EventModel::removeById(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  beginRemoveRows({}, row, row);
  m_items.removeAt(row);
  endRemoveRows();
}

void EventModel::detachTask(const QString& taskId) {
  for(int i = 0; i < m_items.size(); ++i) {
    if(m_items[i].taskId == taskId) {
      m_items[i].taskId.clear();
      const QModelIndex mi = index(i, 0);
      emit dataChanged(mi, mi, {TaskIdRole});
    }
  }
}

void EventModel::insertAt(int row, const CalEvent& e) {
  row = qBound(0, row, m_items.size());
  beginInsertRows({}, row, row);
  m_items.insert(row, e);
  endInsertRows();
}

void EventModel::setTaskId(const QString& eventId, const QString& taskId) {
  const int row = indexOfId(eventId);
  if(row < 0) {
    return;
  }
  m_items[row].taskId = taskId;
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, {TaskIdRole});
}

// ---- PersonModel ----

QHash<int, QByteArray> PersonModel::roleNames() const {
  return {
      {IdRole, "id"},
      {NameRole, "name"},
      {RoleRole, "role"},
      {QuestionRole, "question"},
      {StateRole, "state"},
      {ColorRole, "color"},
      {StateAtRole, "stateAt"},
      {RemindAtRole, "remindAt"},
      {RemindHasTimeRole, "remindHasTime"},
  };
}

QVariant PersonModel::data(const QModelIndex& idx, int role) const {
  if(!idx.isValid() || idx.row() < 0 || idx.row() >= m_items.size()) {
    return {};
  }
  const Person& p = m_items[idx.row()];
  switch(role) {
    case IdRole:
      return p.id;
    case NameRole:
      return p.name;
    case RoleRole:
      return p.role;
    case QuestionRole:
      return p.question;
    case StateRole:
      return p.state;
    case ColorRole:
      return p.color;
    case StateAtRole:
      return p.stateAt;
    case RemindAtRole:
      return p.remindAt;
    case RemindHasTimeRole:
      return p.remindHasTime;
  }
  return {};
}

void PersonModel::reset(QVector<Person> items) {
  beginResetModel();
  m_items = std::move(items);
  endResetModel();
}

int PersonModel::indexOfId(const QString& id) const {
  for(int i = 0; i < m_items.size(); ++i) {
    if(m_items[i].id == id) {
      return i;
    }
  }
  return -1;
}

void PersonModel::cycleState(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  const QString cur = m_items[row].state;
  // todo → pinged → replied → idle. The last hop is what takes someone off the
  // People rail: "idle" is the state the rail filters out (ActivePeopleModel),
  // so answering the last person empties the rail instead of cycling it back
  // round to "to write" forever. Anything unrecognised — including idle —
  // starts the loop again.
  QString next = "todo";
  if(cur == "todo") {
    next = "pinged";
  } else if(cur == "pinged") {
    next = "replied";
  } else if(cur == "replied") {
    next = "idle";
  }
  m_items[row].state = next;
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, {StateRole});
}

void PersonModel::setState(const QString& id, const QString& state) {
  const int row = indexOfId(id);
  if(row < 0 || m_items[row].state == state) {
    return;
  }
  m_items[row].state = state;
  // When it moved: "жду ответа · 2 ч" (X-Oth-Archive-People, R3-057).
  m_items[row].stateAt = QDateTime::currentDateTime();
  QList<int> roles{StateRole, StateAtRole};
  // "When to write" belongs to the ask: once written it is spent, and a
  // "write again" starts without a time long past (dogfood 2026-10-20).
  if(state != QLatin1String("todo") && m_items[row].remindAt.isValid()) {
    m_items[row].remindAt = {};
    m_items[row].remindHasTime = false;
    roles << RemindAtRole << RemindHasTimeRole;
  }
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, roles);
}

void PersonModel::setRemindAt(const QString& id, const QDateTime& at, bool hasTime) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  Person& p = m_items[row];
  const bool timed = at.isValid() && hasTime;
  if(p.remindAt == at && p.remindHasTime == timed) {
    return;
  }
  p.remindAt = at;
  p.remindHasTime = timed;
  const QModelIndex mi = index(row, 0);
  emit dataChanged(mi, mi, {RemindAtRole, RemindHasTimeRole});
}

void PersonModel::upsert(const Person& p) {
  const int row = indexOfId(p.id);
  if(row >= 0) {
    m_items[row] = p;
    const QModelIndex mi = index(row, 0);
    emit dataChanged(mi, mi);
  } else {
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.push_back(p);
    endInsertRows();
  }
}

void PersonModel::insertAt(int row, const Person& p) {
  row = qBound(0, row, m_items.size());
  beginInsertRows({}, row, row);
  m_items.insert(row, p);
  endInsertRows();
}

void PersonModel::removeById(const QString& id) {
  const int row = indexOfId(id);
  if(row < 0) {
    return;
  }
  beginRemoveRows({}, row, row);
  m_items.removeAt(row);
  endRemoveRows();
}

int PersonModel::todoCount() const {
  int n = 0;
  for(const auto& p : m_items) {
    if(p.state == "todo") {
      ++n;
    }
  }
  return n;
}
