// The two dots of a merge / pull request card (dogfood 2026-10-20 item 9):
// CI on the left, "can it be merged" on the right, and under the cursor who
// the request waits on. Everything here is read from facts the app already
// holds — the PR watcher's PrInfo (m_tasks' git facts), a tracker card's
// details, a CODEOWNERS report. Nothing here asks a forge anything: the
// dots never cost a request (2026-10-20: a company GitLab took the old
// polling for brute force).

#include "AppController.h"

#include "git/PrFacts.h"
#include "integrations/IntegrationProvider.h"
#include "net/FeatureGate.h"
#include "people/PingTime.h"

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

QString bareUrl(QString u) {
  while(u.endsWith(QChar('/'))) {
    u.chop(1);
  }
  return u.toLower();
}

// Every look already taken at one request's CI: the tracker's pull (first,
// when the task's request is a tracker's) and the git watcher's when it is
// the same request. A result about an older push than the latest head
// either look saw is "unknown", not that result; of the current ones the
// later look's, the tracker's when neither says when (dogfood 2026-10-21).
QList<heap::git::CiReading> ciLooks(
    bool review, const QVariantMap& details, const QString& trackerUrl, const QDateTime& trackerSeen, const QVariantMap& git) {
  QList<heap::git::CiReading> looks;
  if(review) {
    looks.append({details.value(QStringLiteral("pipeline")).toString(),
                  details.value(QStringLiteral("ciHead")).toString(),
                  details.value(QStringLiteral("headSha")).toString(),
                  trackerSeen});
  }
  const QString gitUrl = git.value(QStringLiteral("prUrl")).toString();
  if(!review || gitUrl.isEmpty() || trackerUrl.isEmpty() || bareUrl(gitUrl) == bareUrl(trackerUrl)) {
    looks.append({git.value(QStringLiteral("prChecks")).toString(),
                  git.value(QStringLiteral("prChecksHead")).toString(),
                  git.value(QStringLiteral("prHeadSha")).toString(),
                  git.value(QStringLiteral("prSeenAt")).toDateTime()});
  }
  return looks;
}

// The commit a reading's result is about.
QString ciAbout(const heap::git::CiReading& r) {
  return r.ciHead.isEmpty() ? r.mrHead : r.ciHead;
}

// A read of a run's jobs or a job's log is good for: a finished run's for
// good, a running one's a minute, a failed read five minutes (not asked
// again sooner: 2026-10-20, a company GitLab took retries for brute force).
constexpr qint64 kCiRunningSecs = 60;
constexpr qint64 kCiFailedReadSecs = 5 * 60;
constexpr int kCiReadsKept = 200;

bool terminalCi(const QString& state) {
  return state == QLatin1String("success") || state == QLatin1String("failed") || state == QLatin1String("canceled");
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
  // The tracker's look at the request: its URL and when a pull saw it.
  QString trackerUrl;
  QDateTime trackerSeen;
  if(row >= 0) {
    const Task& self = m_tasks.items().at(row);
    externalId = self.externalId;
    if(isReviewKind(kind)) {
      trackerUrl = self.externalUrl;
      trackerSeen = m_reviewSeenAt.value(self.externalUrl);
    } else {
      // A Jira card with its MR folded in (APP-293): that MR's facts, all of
      // them — the merge dot too, not only CI (dogfood 2026-10-21: the card
      // showed one green dot and nothing to say what it was).
      const ReviewSource src = reviewSourceOf(id);
      if(src.found) {
        details = src.details.toVariantMap();
        kind = details.value(QStringLiteral("kind")).toString();
        sourceId = src.sourceId;
        provider = src.provider;
        externalId = src.externalId;
        since = src.since;
        trackerUrl = src.url;
        trackerSeen = src.seenAt;
      }
    }
  }

  heap::git::MrSignals s;
  QStringList reviewers;
  if(isReviewKind(kind)) {
    s.state = details.value(QStringLiteral("state")).toString();
    s.draft = details.value(QStringLiteral("draft")).toBool();
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
  // CI from every look already taken at this request — the tracker's pull
  // and the git watcher's — never fetched for the dot (ciLooks).
  s.ci = heap::git::currentCi(ciLooks(isReviewKind(kind), details, trackerUrl, trackerSeen, git));
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

// ---- One reading of a task's CI (dogfood 2026-10-22) ----
// The card's chip and popover, the document's PR row and CI block, and "Ждут
// меня" all read ciOf; the jobs and logs come from the same request mrOf
// reads. Before, the card read the folded MR, the document the watcher's raw
// prChecks, the CI block only a request with a card of its own: one said
// passed while the other said failed.

AppController::ReviewSource AppController::reviewSourceOf(const QString& taskId) const {
  ReviewSource out;
  const int row = taskId.isEmpty() ? -1 : m_tasks.indexOfId(taskId);
  if(row < 0) {
    return out;
  }
  const Task& self = m_tasks.items().at(row);
  const auto fromCard = [this, &out](const Task& t) {
    out.found = true;
    out.details = t.externalMeta.details;
    out.sourceId = t.id;
    out.provider = t.externalProvider;
    out.externalId = t.externalId;
    out.project = t.externalMeta.project;
    out.url = t.externalUrl;
    out.since = t.externalMeta.createdAt;
    out.seenAt = m_reviewSeenAt.value(t.externalUrl);
  };
  if(isReviewKind(self.externalMeta.details.value(QStringLiteral("kind")).toString())) {
    fromCard(self);
    return out;
  }
  // The pick of the last pull first: with or without a card of its own (an
  // MR first pulled after its issue was here never gets one). Its own
  // archived card, when it has one, keeps the last run read beside this
  // pull's head (keepFoldedCiCurrent).
  const auto picked = m_foldedReviews.constFind(taskId);
  if(picked != m_foldedReviews.constEnd()) {
    const int cardRow = picked->cardId.isEmpty() ? -1 : m_tasks.indexOfId(picked->cardId);
    const Task* card = cardRow >= 0 ? &m_tasks.items().at(cardRow) : nullptr;
    out.found = true;
    out.details = card != nullptr ? card->externalMeta.details : picked->details;
    out.sourceId = picked->cardId.isEmpty() ? taskId : picked->cardId;
    out.provider = picked->provider;
    out.externalId = picked->externalId;
    out.project = card != nullptr && !card->externalMeta.project.isEmpty() ? card->externalMeta.project : picked->project;
    out.url = picked->url;
    out.since = picked->createdAt;
    out.seenAt = picked->seenAt;
    return out;
  }
  // Else the best of the cards folded into it.
  const Task* found = nullptr;
  heap::git::ReviewPick foundPick;
  for(const Task& x : m_tasks.items()) {
    if(x.externalMeta.details.value(QStringLiteral("foldedInto")).toString() != taskId) {
      continue;
    }
    heap::git::ReviewPick pick;
    pick.state = x.externalMeta.details.value(QStringLiteral("state")).toString();
    pick.mine = x.externalMeta.details.value(QStringLiteral("mine")).toBool(true);
    pick.updatedAt = x.externalMeta.updatedAt;
    if(found == nullptr || heap::git::reviewBeats(pick, foundPick)) {
      found = &x;
      foundPick = pick;
    }
  }
  if(found != nullptr) {
    fromCard(*found);
  }
  return out;
}

QVariantMap AppController::ciOf(const QString& taskId) const {
  if(m_tasks.indexOfId(taskId) < 0) {
    return {};
  }
  const ReviewSource src = reviewSourceOf(taskId);
  const QVariantMap details = src.found ? src.details.toVariantMap() : QVariantMap{};
  const bool review = src.found && isReviewKind(details.value(QStringLiteral("kind")).toString());
  const QVariantMap git = m_tasks.gitFactsFor(taskId);
  const QList<heap::git::CiReading> looks = ciLooks(review, details, src.url, src.seenAt, git);
  const heap::git::CiPick pick = heap::git::pickCi(looks);
  if(pick.ci.isEmpty()) {
    return {};
  }
  // The tracker's run, when it is about the commit the answer is about: its
  // link, its number, its time, its jobs. Else only the watcher's word.
  const int used = pick.index >= 0 ? pick.index : pick.staleIndex;
  const heap::git::CiReading& reading = looks.at(used);
  const bool trackerRun = review && !looks.first().ci.trimmed().isEmpty() &&
                          (used == 0 || (!ciAbout(looks.first()).isEmpty() && ciAbout(looks.first()) == ciAbout(reading)));

  QString requestUrl = src.url;
  if(requestUrl.isEmpty()) {
    requestUrl = git.value(QStringLiteral("prUrl")).toString();
  }
  const QString kind = details.value(QStringLiteral("kind")).toString();
  const bool gitlab =
      kind == QLatin1String("mr") || src.provider == QLatin1String("gitlab") || requestUrl.contains(QStringLiteral("/-/merge_requests/"));
  QString url = trackerRun ? details.value(QStringLiteral("pipelineUrl")).toString() : QString();
  if(url.isEmpty()) {
    // The run left no link (GitLab gives one only with a web_url): the
    // request's own pipelines / checks page is where it is.
    url = heap::git::ciPageOf(requestUrl, gitlab);
  }
  QString sha = trackerRun ? details.value(QStringLiteral("ciSha")).toString() : QString();
  if(sha.isEmpty()) {
    sha = ciAbout(reading);
  }
  const QString at = trackerRun ? details.value(QStringLiteral("pipelineAt")).toString() : QString();
  const QString runRef = trackerRun ? details.value(QStringLiteral("ciRef")).toString() : QString();
  const QString state = heap::git::ciWordOf(pick.ci);

  int number = git.value(QStringLiteral("prNumber")).toInt();
  if(number <= 0 && review) {
    number = QString(src.externalId).remove(QChar('!')).remove(QChar('#')).toInt();
  }
  const bool fetchable = !runRef.isEmpty() && !src.provider.isEmpty();

  QVariantMap out{
      {QStringLiteral("state"), state},
      {QStringLiteral("stale"), pick.index < 0},
      {QStringLiteral("staleState"), pick.index < 0 ? heap::git::ciWordOf(looks.at(pick.staleIndex).ci) : QString()},
      {QStringLiteral("url"), url},
      {QStringLiteral("pipeline"), gitlab ? runRef : QString()},
      {QStringLiteral("sha"), sha.left(7)},
      {QStringLiteral("at"), at},
      {QStringLiteral("ago"),
       heap::people::agoShort(QDateTime::fromString(at, Qt::ISODate), QDateTime::currentDateTime(), m_language == QStringLiteral("ru"))},
      {QStringLiteral("ref"), number > 0 ? (gitlab ? QStringLiteral("!") : QStringLiteral("#")) + QString::number(number) : QString()},
      {QStringLiteral("gitlab"), gitlab},
      {QStringLiteral("requestUrl"), requestUrl},
      {QStringLiteral("hasRef"), fetchable},
      {QStringLiteral("source"), src.sourceId}};
  if(fetchable) {
    // One run: the request, the run, when it last changed and how it ended
    // (a retried job keeps GitLab's pipeline id and moves its time).
    const QString key = src.provider + QChar('|') + src.externalId + QChar('|') + runRef + QChar('|') + at + QChar('|') + state;
    out.insert(QStringLiteral("key"), key);
    out.insert(QStringLiteral("provider"), src.provider);
    out.insert(QStringLiteral("externalId"), src.externalId);
    out.insert(QStringLiteral("project"), src.project);
    out.insert(QStringLiteral("runRef"), runRef);
    const auto read = m_ciJobsRead.constFind(key);
    if(read != m_ciJobsRead.constEnd() && read->error.isEmpty()) {
      QStringList failedNames;
      for(const QVariant& j : read->jobs) {
        failedNames << j.toMap().value(QStringLiteral("name")).toString();
      }
      out.insert(QStringLiteral("jobsRead"), true);
      out.insert(QStringLiteral("jobs"), read->jobs);
      out.insert(QStringLiteral("total"), read->counts.value(QStringLiteral("total"), 0).toInt());
      out.insert(QStringLiteral("done"), read->counts.value(QStringLiteral("done"), 0).toInt());
      out.insert(QStringLiteral("failed"), read->counts.value(QStringLiteral("failed"), failedNames.size()).toInt());
      out.insert(QStringLiteral("failedNames"), failedNames);
      out.insert(QStringLiteral("firstFailed"), failedNames.value(0));
    }
  }
  return out;
}

void AppController::fetchCiJobs(const QString& taskId) {
  const QVariantMap ci = ciOf(taskId);
  const QString key = ci.value(QStringLiteral("key")).toString();
  if(key.isEmpty()) {
    emit ciJobsLoaded(taskId, {}, QStringLiteral("no pipeline"));
    return;
  }
  const auto read = m_ciJobsRead.constFind(key);
  if(read != m_ciJobsRead.constEnd()) {
    const qint64 age = read->at.secsTo(QDateTime::currentDateTime());
    const bool fresh = !read->error.isEmpty() ? age < kCiFailedReadSecs : (read->over || age < kCiRunningSecs);
    if(fresh) {
      emit ciJobsLoaded(taskId, read->jobs, read->error.isEmpty() ? QString() : providerReason(read->error));
      return;
    }
  }
  // One request a run, however many ask while it is out.
  if(m_ciJobsAsked.contains(key)) {
    QStringList& waiting = m_ciJobsAsked[key];
    if(!waiting.contains(taskId)) {
      waiting.append(taskId);
    }
    return;
  }
  const QString providerId = ci.value(QStringLiteral("provider")).toString();
  if(heap::net::FeatureGate::instance().isPaused(heap::net::features::ci(providerId))) {
    emit ciJobsLoaded(taskId, {}, providerReason(QStringLiteral("paused")));
    return;
  }
  m_ciJobsAsked.insert(key, {taskId});
  const QString externalId = ci.value(QStringLiteral("externalId")).toString();
  const QString project = ci.value(QStringLiteral("project")).toString();
  const QString ref = ci.value(QStringLiteral("runRef")).toString();
  const bool over = terminalCi(ci.value(QStringLiteral("state")).toString());
  const auto finish = [this, key, over](const QVariantList& jobs, const QVariantMap& counts, const QString& error) {
    if(m_ciJobsRead.size() >= kCiReadsKept) {
      m_ciJobsRead.clear();
    }
    m_ciJobsRead.insert(key, {jobs, counts, QString(), error, QDateTime::currentDateTime(), over});
    const QStringList waiting = m_ciJobsAsked.take(key);
    for(const QString& id : waiting) {
      emit ciJobsLoaded(id, jobs, error.isEmpty() ? QString() : providerReason(error));
    }
    // The chips say how far the run got and what failed.
    bumpMrRevision();
  };
  ensureFreshToken(
      providerId,
      [this, providerId, externalId, project, ref, finish]() {
        for(const auto& provider : m_syncProviders) {
          if(provider->id() != providerId) {
            continue;
          }
          auto* guard = new QObject(this);
          connect(
              provider.get(),
              &heap::integrations::IntegrationProvider::ciJobsFetched,
              guard,
              [guard, externalId, finish](const QString& id, const QVariantList& jobs, const QString& error, const QVariantMap& counts) {
                if(id != externalId) {
                  return;
                }
                guard->deleteLater();
                finish(jobs, counts, error);
              });
          provider->fetchCiJobs(externalId, project, ref);
          return;
        }
        finish({}, {}, QStringLiteral("not connected"));
      },
      [finish]() {
        finish({}, {}, QStringLiteral("token refresh failed"));
      });
}

void AppController::fetchCiLog(const QString& taskId, const QString& jobId) {
  const QVariantMap ci = ciOf(taskId);
  const QString providerId = ci.value(QStringLiteral("provider")).toString();
  const QString externalId = ci.value(QStringLiteral("externalId")).toString();
  if(providerId.isEmpty() || jobId.isEmpty()) {
    emit ciLogLoaded(taskId, jobId, QString(), QStringLiteral("no pipeline"));
    return;
  }
  const QString key = providerId + QChar('|') + externalId + QChar('|') + jobId;
  const auto read = m_ciLogsRead.constFind(key);
  // A failed job's log is final; a failed read waits a few minutes.
  if(read != m_ciLogsRead.constEnd() && (read->error.isEmpty() || read->at.secsTo(QDateTime::currentDateTime()) < kCiFailedReadSecs)) {
    emit ciLogLoaded(taskId, jobId, read->text, read->error.isEmpty() ? QString() : providerReason(read->error));
    return;
  }
  if(m_ciLogsAsked.contains(key)) {
    QStringList& waiting = m_ciLogsAsked[key];
    if(!waiting.contains(taskId)) {
      waiting.append(taskId);
    }
    return;
  }
  if(heap::net::FeatureGate::instance().isPaused(heap::net::features::ci(providerId))) {
    emit ciLogLoaded(taskId, jobId, QString(), providerReason(QStringLiteral("paused")));
    return;
  }
  m_ciLogsAsked.insert(key, {taskId});
  const QString project = ci.value(QStringLiteral("project")).toString();
  const auto finish = [this, key, jobId](const QString& text, const QString& error) {
    if(m_ciLogsRead.size() >= kCiReadsKept) {
      m_ciLogsRead.clear();
    }
    m_ciLogsRead.insert(key, {{}, {}, text, error, QDateTime::currentDateTime(), error.isEmpty()});
    const QStringList waiting = m_ciLogsAsked.take(key);
    for(const QString& id : waiting) {
      emit ciLogLoaded(id, jobId, text, error.isEmpty() ? QString() : providerReason(error));
    }
  };
  ensureFreshToken(
      providerId,
      [this, providerId, externalId, project, jobId, finish]() {
        for(const auto& provider : m_syncProviders) {
          if(provider->id() != providerId) {
            continue;
          }
          auto* guard = new QObject(this);
          connect(provider.get(),
                  &heap::integrations::IntegrationProvider::ciLogFetched,
                  guard,
                  [guard, externalId, jobId, finish](const QString& id, const QString& job, const QString& text, const QString& error) {
                    if(id != externalId || job != jobId) {
                      return;
                    }
                    guard->deleteLater();
                    finish(text, error);
                  });
          provider->fetchCiLog(externalId, project, jobId);
          return;
        }
        finish(QString(), QStringLiteral("not connected"));
      },
      [finish]() {
        finish(QString(), QStringLiteral("token refresh failed"));
      });
}
