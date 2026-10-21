// The two dots of a merge / pull request card (dogfood 2026-10-20 item 9):
// CI on the left, "can it be merged" on the right, and under the cursor who
// the request waits on. Everything here is read from facts the app already
// holds — the PR watcher's PrInfo (m_tasks' git facts), a tracker card's
// details, a CODEOWNERS report. Nothing here asks a forge anything: the
// dots never cost a request (2026-10-20: a company GitLab took the old
// polling for brute force).

#include "AppController.h"

#include "git/PrFacts.h"

#include <QColor>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace {

// "@Ira" and "ira" are one person.
QString handleKey(const QString& handle) {
  QString s = handle.trimmed();
  if(s.startsWith(QLatin1Char('@'))) {
    s = s.mid(1);
  }
  return s.toLower();
}

QString bareHandle(const QString& handle) {
  QString s = handle.trimmed();
  return s.startsWith(QLatin1Char('@')) ? s.mid(1) : s;
}

bool isReviewKind(const QString& kind) {
  return kind == QLatin1String("mr") || kind == QLatin1String("pr");
}

}  // namespace

void AppController::putPrReviewFacts(QVariantMap& entry, const QVariantMap& pr) {
  entry[QStringLiteral("prDraft")] = pr.value(QStringLiteral("draft")).toBool();
  entry[QStringLiteral("prReviewDecision")] = pr.value(QStringLiteral("reviewDecision")).toString();
  entry[QStringLiteral("prMergeable")] = pr.value(QStringLiteral("mergeable")).toString();
  entry[QStringLiteral("prReviewRequests")] = pr.value(QStringLiteral("reviewRequests")).toStringList();
}

void AppController::bumpMrRevision() {
  // A sync touches hundreds of rows at once: one bump for all of them.
  if(m_mrBumpQueued) {
    return;
  }
  m_mrBumpQueued = true;
  QTimer::singleShot(0, this, [this]() {
    m_mrBumpQueued = false;
    ++m_mrRevision;
    emit mrRevisionChanged();
  });
}

void AppController::wireMrRevision() {
  connect(&m_tasks, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
    if(roles.isEmpty() || roles.contains(TaskModel::PrStateRole) || roles.contains(TaskModel::TicketRole) ||
       roles.contains(TaskModel::PrMoveRole)) {
      bumpMrRevision();
    }
  });
  connect(&m_tasks, &QAbstractItemModel::modelReset, this, &AppController::bumpMrRevision);
  connect(&m_tasks, &QAbstractItemModel::rowsInserted, this, &AppController::bumpMrRevision);
  connect(&m_tasks, &QAbstractItemModel::rowsRemoved, this, &AppController::bumpMrRevision);
  connect(this, &AppController::codeOwnersChanged, this, &AppController::bumpMrRevision);
  // Who we wait on is shown by the name a person has here.
  connect(&m_people, &QAbstractItemModel::dataChanged, this, &AppController::bumpMrRevision);
  connect(&m_people, &QAbstractItemModel::rowsInserted, this, &AppController::bumpMrRevision);
  connect(&m_people, &QAbstractItemModel::modelReset, this, &AppController::bumpMrRevision);
}

void AppController::setGitInfoForTest(const QString& taskId, const QVariantMap& info) {
  m_tasks.setGitInfoForId(taskId, info);
}

QVariantMap AppController::mrOf(const QVariantMap& task) const {
  const QString id = task.value(QStringLiteral("id")).toString();
  QVariantMap ticket = task.value(QStringLiteral("ticket")).toMap();
  QVariantMap details = ticket.value(QStringLiteral("details")).toMap();
  QString kind = details.value(QStringLiteral("kind")).toString();
  // Most cards have no request at all: out before any lookup.
  if(!isReviewKind(kind) && task.value(QStringLiteral("prState")).toString().isEmpty()) {
    return {};
  }
  const int row = id.isEmpty() ? -1 : m_tasks.indexOfId(id);
  // The watcher's facts, with what the card's own map says on top (the
  // board hands over some of them as roles; a test hands over all).
  QVariantMap git = row >= 0 ? m_tasks.gitFactsFor(id) : QVariantMap{};
  for(const char* key : {"prState", "prNumber", "prUrl", "prChecks", "prDraft", "prReviewDecision", "prMergeable", "prReviewRequests"}) {
    const QString k = QString::fromLatin1(key);
    if(task.contains(k)) {
      git.insert(k, task.value(k));
    }
  }
  QString sourceId = id;
  QString provider = ticket.value(QStringLiteral("provider")).toString();
  QString externalId;
  QDateTime since = ticket.value(QStringLiteral("createdAt")).toDateTime();
  if(row >= 0) {
    const Task& self = m_tasks.items().at(row);
    externalId = self.externalId;
    // A Jira card with its MR folded in (APP-293): that MR's facts.
    if(!isReviewKind(kind)) {
      const Task* found = nullptr;
      for(const Task& x : m_tasks.items()) {
        if(x.externalMeta.details.value(QStringLiteral("foldedInto")).toString() != id) {
          continue;
        }
        const QString st = x.externalMeta.details.value(QStringLiteral("state")).toString();
        found = &x;
        if(st == QLatin1String("open") || st == QLatin1String("opened")) {
          break;
        }
      }
      if(found != nullptr) {
        details = found->externalMeta.details.toVariantMap();
        kind = details.value(QStringLiteral("kind")).toString();
        sourceId = found->id;
        provider = found->externalProvider;
        externalId = found->externalId;
        since = found->externalMeta.createdAt;
      }
    }
  }

  heap::git::MrSignals s;
  QStringList reviewers;
  if(isReviewKind(kind)) {
    s.state = details.value(QStringLiteral("state")).toString();
    s.draft = details.value(QStringLiteral("draft")).toBool();
    s.ci = details.value(QStringLiteral("pipeline")).toString();
    s.mergeStatus = details.value(QStringLiteral("mergeStatus")).toString();
    s.conflict = details.value(QStringLiteral("conflicts")).toBool();
    if(details.contains(QStringLiteral("approvalsRequired"))) {
      s.approvalsRequired = details.value(QStringLiteral("approvalsRequired")).toInt();
    }
    reviewers = details.value(QStringLiteral("reviewers")).toStringList();
  } else {
    s.state = git.value(QStringLiteral("prState")).toString();
    s.draft = git.value(QStringLiteral("prDraft")).toBool();
  }
  // The watcher fills in what a tracker card leaves out (a branch of mine
  // with the same request), never the other way round.
  if(s.ci.isEmpty()) {
    s.ci = git.value(QStringLiteral("prChecks")).toString();
  }
  s.reviewDecision = git.value(QStringLiteral("prReviewDecision")).toString();
  if(s.mergeStatus.isEmpty()) {
    s.mergeStatus = git.value(QStringLiteral("prMergeable")).toString();
  }
  if(reviewers.isEmpty()) {
    reviewers = git.value(QStringLiteral("prReviewRequests")).toStringList();
  }

  // CODEOWNERS: whose approval is in, whose a required need still waits on.
  QVariantMap report = codeOwnersOf(sourceId);
  if(report.isEmpty() && sourceId != id) {
    report = codeOwnersOf(id);
  }
  QStringList approvedHandles;
  QStringList ownerWaits;
  if(report.value(QStringLiteral("found")).toBool()) {
    int pending = 0;
    for(const QVariant& rv : report.value(QStringLiteral("requirements")).toList()) {
      const QVariantMap req = rv.toMap();
      const bool required = !req.value(QStringLiteral("optional")).toBool();
      const bool open = req.value(QStringLiteral("state")).toString() == QLatin1String("pending");
      if(required && open) {
        ++pending;
      }
      for(const QVariant& ov : req.value(QStringLiteral("owners")).toList()) {
        const QVariantMap o = ov.toMap();
        const QString st = o.value(QStringLiteral("state")).toString();
        const QString handle = o.value(QStringLiteral("handle")).toString();
        if(st == QLatin1String("approved")) {
          approvedHandles << handle;
        } else if(required && open && (st == QLatin1String("requested") || st == QLatin1String("missing"))) {
          ownerWaits << handle;
        }
      }
    }
    s.ownersPending = pending;
  }

  const heap::git::MrDots dots = heap::git::mrDots(s);

  // Who: asked reviewers and owners a required need waits on, less whoever
  // already approved; each once.
  QSet<QString> seen;
  QStringList approved;
  QStringList approvedNames;
  for(const QString& h : std::as_const(approvedHandles)) {
    const QString key = handleKey(h);
    if(key.isEmpty() || seen.contains(key)) {
      continue;
    }
    seen.insert(key);
    approved << h;
  }
  QStringList waiting;
  for(const QString& h : reviewers + ownerWaits) {
    const QString key = handleKey(h);
    if(key.isEmpty() || seen.contains(key)) {
      continue;
    }
    seen.insert(key);
    waiting << h;
  }
  const auto names = [this](const QStringList& handles) {
    QStringList out;
    for(const QString& h : handles) {
      const QString pid = personIdForHandle(h);
      const int pr = pid.isEmpty() ? -1 : m_people.indexOfId(pid);
      out << (pr >= 0 ? m_people.items().at(pr).name : bareHandle(h));
    }
    return out;
  };

  int number = git.value(QStringLiteral("prNumber")).toInt();
  if(number <= 0 && isReviewKind(kind)) {
    number = QString(externalId).remove(QChar('!')).remove(QChar('#')).toInt();
  }
  if(number <= 0 && isReviewKind(kind)) {
    // "!481", "api!481", "repo#77": the number after the last ! or #.
    const QString key = ticket.value(QStringLiteral("key")).toString();
    const qsizetype at = std::max(key.lastIndexOf(QChar('!')), key.lastIndexOf(QChar('#')));
    number = at >= 0 ? key.mid(at + 1).toInt() : 0;
  }
  const QString url = git.value(QStringLiteral("prUrl")).toString();
  const bool gitlab =
      kind == QLatin1String("mr") || provider == QLatin1String("gitlab") || url.contains(QStringLiteral("/-/merge_requests/"));
  const QString state = s.state.toLower();
  return {{QStringLiteral("ci"), dots.ci},
          {QStringLiteral("merge"), dots.merge},
          {QStringLiteral("ref"), number > 0 ? (gitlab ? QStringLiteral("!") : QStringLiteral("#")) + QString::number(number) : QString()},
          {QStringLiteral("state"), state == QLatin1String("opened") || state == QLatin1String("draft") ? QStringLiteral("open") : state},
          {QStringLiteral("draft"), s.draft || state == QLatin1String("draft")},
          {QStringLiteral("conflict"),
           dots.merge == QLatin1String("blocked") && s.reviewDecision != QLatin1String("CHANGES_REQUESTED") &&
               s.mergeStatus != QLatin1String("requested_changes")},
          {QStringLiteral("changes"),
           s.reviewDecision == QLatin1String("CHANGES_REQUESTED") || s.mergeStatus == QLatin1String("requested_changes")},
          {QStringLiteral("waiting"), names(waiting)},
          {QStringLiteral("waitingHandles"), waiting},
          {QStringLiteral("approved"), names(approved)},
          {QStringLiteral("since"), since}};
}

int AppController::pingMrReviewers(const QString& taskId) {
  const QVariantMap mr = mrOf(taskById(taskId));
  if(mr.value(QStringLiteral("state")).toString() != QLatin1String("open")) {
    emit toast(tr_("mr.ping.noRequest"));
    return 0;
  }
  const QStringList handles = mr.value(QStringLiteral("waitingHandles")).toStringList();
  if(handles.isEmpty()) {
    emit toast(tr_("mr.ping.nobody"));
    return 0;
  }
  const QString ref = mr.value(QStringLiteral("ref")).toString();
  const QString question = tr_("mr.ping.question").arg(ref.isEmpty() ? taskId : ref).trimmed();
  QStringList added;
  UndoScope scope(this, QString());
  for(const QString& handle : handles) {
    const QString pid = personIdForHandle(handle);
    const int at = pid.isEmpty() ? -1 : m_people.indexOfId(pid);
    if(at >= 0) {
      Person p = m_people.items().at(at);
      const QString before = p.state;
      if(p.state != QLatin1String("todo")) {
        p.state = QStringLiteral("todo");
        p.stateAt = QDateTime::currentDateTime();
      }
      if(p.question.isEmpty()) {
        p.question = question;
      }
      if(!(p == m_people.items().at(at))) {
        m_people.upsert(p);
        personStateMoved(p.id, before);
      }
      added << p.name;
      continue;
    }
    // A handle nobody here has yet: a person of its own, so it can be
    // written to and ticked off like anyone else.
    Person p;
    p.name = bareHandle(handle);
    p.id = suggestPersonId(p.name);
    if(p.id.isEmpty()) {
      p.id = QStringLiteral("p-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    }
    p.state = QStringLiteral("todo");
    p.question = question;
    p.color = QColor(QStringLiteral("#7da8d9"));
    p.stateAt = QDateTime::currentDateTime();
    m_people.upsert(p);
    added << p.name;
  }
  const QString list = added.join(QStringLiteral(", "));
  scope.setLabel(tr_("mr.ping.undone").arg(list));
  scheduleSave();
  emit undoableToast(tr_("mr.ping.added").arg(list), 6);
  return static_cast<int>(added.size());
}
