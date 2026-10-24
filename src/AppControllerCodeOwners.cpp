// CODEOWNERS hints and repositories on an SSH dev server (dogfood
// 2026-10-19).
//
// GitWatcher reads the CODEOWNERS of an open PR's target branch and says who
// owns what it changes; this file keeps those reports per task and turns the
// owners nobody has asked yet into "Кому написать" rows. Facts only: Tabless
// requests no reviewer and messages no one — copying a handle or opening the
// PR is the user's move. The SSH half is the settings side: the host list
// from ~/.ssh/config and a one-shot "test connection".

#include "AppController.h"

#include "git/BranchTaskMatcher.h"
#include "git/ForgeApi.h"
#include "git/GitRunner.h"
#include "git/GitWatcher.h"
#include "git/RemoteRepo.h"
#include "integrations/ProviderRegistry.h"
#include "integrations/RestIssueProvider.h"
#include "net/FeatureGate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// A tracker card's owners are asked again at most this often (the forge gate
// in ForgeApi also holds anything that failed lately). Was a minute, which
// with many MR cards and a failing server read as brute force (2026-10-20).
constexpr qint64 kTrackerOwnersEveryMs = 5 * 60'000;

}  // namespace

void AppController::onCodeOwnersUpdated(const QString& repo, const QString& branch, const QVariantMap& report) {
  const QString key = repo + QChar('\n') + branch;
  // No report: the PR was merged or closed, or there is none any more.
  if(report.isEmpty()) {
    if(m_codeOwners.remove(key) > 0) {
      emit codeOwnersChanged();
    }
    return;
  }
  // The task: one whose branch is this one (a tracker MR card keeps it),
  // else the key in the branch name.
  QString taskId;
  for(const Task& t : m_tasks.items()) {
    if(!t.branch.isEmpty() && t.branch == branch) {
      taskId = t.id;
      break;
    }
  }
  if(taskId.isEmpty()) {
    const heap::git::BranchTaskMatcher m(collectPrefixes());
    const auto mr = m.extract(branch);
    if(mr.matched) {
      taskId = taskIdForBranchMatch(mr.taskId);
    }
  }
  QVariantMap entry = report;
  entry[QStringLiteral("taskId")] = taskId;
  entry[QStringLiteral("repo")] = repo;
  entry[QStringLiteral("branch")] = branch;
  storeOwnersEntry(key, entry);
}

void AppController::storeOwnersEntry(const QString& key, const QVariantMap& entry) {
  if(m_codeOwners.value(key) == entry) {
    return;
  }
  // Owners that changed are worth a second look: a hidden row comes back
  // when the people still to ask are not the same any more.
  if(m_codeOwners.value(key).value(QStringLiteral("toPing")) != entry.value(QStringLiteral("toPing"))) {
    const QString url = entry.value(QStringLiteral("prUrl")).toString();
    m_dismissedOwnerPings.removeIf([&url](const QString& k) {
      return k.startsWith(url + QChar('|'));
    });
  }
  m_codeOwners.insert(key, entry);
  emit codeOwnersChanged();
}

QVariantMap AppController::codeOwnersOf(const QString& taskId) const {
  if(taskId.isEmpty()) {
    return {};
  }
  // A report beats a reason; a watched repo's report beats the tracker's.
  QVariantMap best;
  int bestRank = -1;
  for(auto it = m_codeOwners.constBegin(); it != m_codeOwners.constEnd(); ++it) {
    if(it->value(QStringLiteral("taskId")).toString() != taskId) {
      continue;
    }
    const bool report = it->contains(QStringLiteral("found"));
    const bool tracker = it->value(QStringLiteral("source")).toString() == QLatin1String("tracker");
    const int rank = (report ? 2 : 0) + (tracker ? 0 : 1);
    if(rank > bestRank) {
      bestRank = rank;
      best = *it;
    }
  }
  return best;
}

QVariantList AppController::codeOwnerPings() const {
  QVariantList out;
  QSet<QString> listed;  // a PR read both from its repo and its ticket
  for(auto it = m_codeOwners.constBegin(); it != m_codeOwners.constEnd(); ++it) {
    const QVariantMap& r = *it;
    const QString url = r.value(QStringLiteral("prUrl")).toString();
    for(const QString& handle : r.value(QStringLiteral("toPing")).toStringList()) {
      const QString key = url + QChar('|') + handle;
      if(m_dismissedOwnerPings.contains(key) || listed.contains(key)) {
        continue;
      }
      listed.insert(key);
      // What of the change they own ("владелец src/sync в !2340"): the
      // folder of the first file of a rule they are on.
      QString area;
      for(const QVariant& rv : r.value(QStringLiteral("requirements")).toList()) {
        const QVariantMap req = rv.toMap();
        bool theirs = false;
        for(const QVariant& ov : req.value(QStringLiteral("owners")).toList()) {
          theirs = theirs || ov.toMap().value(QStringLiteral("handle")).toString().compare(handle, Qt::CaseInsensitive) == 0;
        }
        const QStringList files = req.value(QStringLiteral("files")).toStringList();
        if(theirs && !files.isEmpty()) {
          const QString f = files.first();
          const qsizetype slash = f.lastIndexOf(QLatin1Char('/'));
          area = slash > 0 ? f.left(slash) : f;
          break;
        }
      }
      out.append(QVariantMap{{QStringLiteral("handle"), handle},
                             {QStringLiteral("area"), area},
                             {QStringLiteral("prNumber"), r.value(QStringLiteral("prNumber"))},
                             {QStringLiteral("prUrl"), url},
                             {QStringLiteral("prTitle"), r.value(QStringLiteral("prTitle"))},
                             {QStringLiteral("flavor"), r.value(QStringLiteral("flavor"))},
                             {QStringLiteral("taskId"), r.value(QStringLiteral("taskId"))},
                             {QStringLiteral("key"), key}});
    }
  }
  return out;
}

void AppController::setCodeOwnersReportForTest(const QString& repo, const QString& branch, const QVariantMap& report) {
  if(report.isEmpty()) {
    // A test cleans up after itself: the report goes.
    if(m_codeOwners.remove(repo + QChar('\n') + branch) > 0) {
      emit codeOwnersChanged();
    }
    return;
  }
  onCodeOwnersUpdated(repo, branch, report);
}

void AppController::dropCodeOwnersOutside(const QStringList& repoKeys) {
  const qsizetype before = m_codeOwners.size();
  m_codeOwners.removeIf([&repoKeys](const auto& it) {
    return it.value().value(QStringLiteral("source")).toString() != QLatin1String("tracker") &&
           !repoKeys.contains(it.value().value(QStringLiteral("repo")).toString());
  });
  if(m_codeOwners.size() != before) {
    emit codeOwnersChanged();
  }
}

void AppController::dismissCodeOwnerPing(const QString& key) {
  if(key.isEmpty() || m_dismissedOwnerPings.contains(key)) {
    return;
  }
  m_dismissedOwnerPings.insert(key);
  emit codeOwnersChanged();
}

namespace {

void collectSshHosts(const QString& file, int depth, QStringList& out) {
  QFile f(file);
  if(depth > 3 || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  QStringList includes;
  for(const QString& h : heap::git::parseSshConfigHosts(QString::fromUtf8(f.readAll()), &includes)) {
    if(!out.contains(h)) {
      out << h;
    }
  }
  const QString sshDir = QDir::homePath() + QStringLiteral("/.ssh");
  for(QString inc : includes) {
    if(inc.startsWith(QStringLiteral("~/"))) {
      inc = QDir::homePath() + inc.mid(1);
    } else if(QFileInfo(inc).isRelative()) {
      inc = sshDir + QChar('/') + inc;  // relative Includes are under ~/.ssh
    }
    const QFileInfo fi(inc);
    const QDir dir = fi.dir();
    for(const QString& name : dir.entryList({fi.fileName()}, QDir::Files, QDir::Name)) {
      collectSshHosts(dir.filePath(name), depth + 1, out);
    }
  }
}

}  // namespace

QStringList AppController::sshHosts() const {
  QStringList out;
  collectSshHosts(QDir::homePath() + QStringLiteral("/.ssh/config"), 0, out);
  return out;
}

bool AppController::isRemoteRepo(const QString& entry) const {
  return heap::git::isRemoteRepoSpec(entry);
}

QString AppController::normalizeRepoEntry(const QString& entry) const {
  if(const auto spec = heap::git::parseRemoteSpec(entry)) {
    return spec->canonical();
  }
  return entry.trimmed();
}

QVariantMap AppController::remoteRepoForm(const QString& host, const QString& path) const {
  const heap::git::RemoteFormCheck c = heap::git::checkRemoteForm(host, path);
  return {{QStringLiteral("entry"), c.entry}, {QStringLiteral("problem"), c.problem}};
}

void AppController::testRemoteRepo(const QString& entry) {
  const auto spec = heap::git::parseRemoteSpec(entry);
  if(!spec) {
    emit remoteRepoTested(entry, QStringLiteral("other"), QString(), QString());
    return;
  }
  heap::git::testRemoteRepo(QStandardPaths::findExecutable(QStringLiteral("ssh")),
                            *spec,
                            this,
                            [this, entry](const QString& failure, const QString& detail, const QString& branch) {
                              emit remoteRepoTested(entry, failure, detail, branch);
                            });
}

QVariantMap AppController::remoteRepoStatus(const QString& entry) const {
  if(!m_gitWatcher || !heap::git::isRemoteRepoSpec(entry)) {
    return {};
  }
  const QVariantMap st = m_gitWatcher->snapshot().value(heap::git::GitWatcher::repoKey(entry)).toMap();
  if(st.isEmpty()) {
    return {};
  }
  return QVariantMap{{QStringLiteral("remote"), true},
                     {QStringLiteral("branch"), st.value(QStringLiteral("branch"))},
                     {QStringLiteral("error"), st.value(QStringLiteral("error"))}};
}

// ---- CODEOWNERS of tracker PR / MR tickets ----

heap::git::ForgeApi* AppController::forgeApi() {
  if(m_forgeApi == nullptr) {
    m_forgeApi = new heap::git::ForgeApi(this);
  }
  return m_forgeApi;
}

QList<heap::git::ForgeAuth> AppController::forgeCredentials() const {
  QList<heap::git::ForgeAuth> out;
  for(const QString& id : {QStringLiteral("gitlab"), QStringLiteral("github")}) {
    const heap::integrations::ProviderDescriptor* d = heap::integrations::findDescriptor(id);
    if(d == nullptr) {
      continue;
    }
    const QVariantMap cfg = integrationConfig(id);
    if(!cfg.value(QStringLiteral("connected")).toBool() || cfg.value(d->tokenKey).toString().trimmed().isEmpty()) {
      continue;
    }
    heap::git::ForgeAuth a;
    a.glab = id == QLatin1String("gitlab");
    a.providerId = id;
    // GitLab's host field (gitlab.com when blank); GitHub is github.com, or
    // an Enterprise server when a host is set.
    QString web = cfg.value(QStringLiteral("host")).toString().trimmed();
    if(web.isEmpty()) {
      web = a.glab ? QStringLiteral("https://gitlab.com") : QStringLiteral("https://github.com");
    }
    a.apiBase = heap::git::forgeApiBase(a.glab, web);
    a.host = heap::git::forgeHostOf(web);
    a.headers = heap::integrations::authHeaders(*d, cfg);
    out.append(a);
  }
  return out;
}

void AppController::pushForgeCredentials() {
  if(m_gitWatcher) {
    m_gitWatcher->setForgeCredentials(forgeCredentials());
  }
}

namespace {

// The PR / MR a ticket stands for: a card the tracker made for it, or an
// issue a review was folded into (its "PR #n" link).
struct TicketPr {
  heap::git::ForgePrRef ref;
  QString providerId;  // the tracker the card came from, "" for a folded link
  QString branch;
};

TicketPr ticketPrOf(const Task& t, const QVariantMap& gitFacts) {
  TicketPr out;
  if(isReviewItem(t) && (t.externalProvider == QLatin1String("gitlab") || t.externalProvider == QLatin1String("github"))) {
    out.providerId = t.externalProvider;
    out.branch = t.externalMeta.details.value(QStringLiteral("sourceBranch")).toString();
    out.ref = heap::git::parseForgePrUrl(t.externalUrl);
    // The card knows its project and number even when the URL says little.
    out.ref.glab = t.externalProvider == QLatin1String("gitlab");
    if(!t.externalMeta.project.isEmpty()) {
      out.ref.project = t.externalMeta.project;
    }
    out.ref.number = QString(t.externalId).remove(QChar('!')).toInt();
    if(out.ref.host.isEmpty()) {
      out.ref.host = heap::git::forgeHostOf(t.externalUrl);
    }
    return out;
  }
  out.ref = heap::git::parseForgePrUrl(gitFacts.value(QStringLiteral("prUrl")).toString());
  return out;
}

}  // namespace

void AppController::refreshTrackerOwners(const QString& taskId, bool force) {
  const int row = m_tasks.indexOfId(taskId);
  if(row < 0) {
    return;
  }
  const Task& t = m_tasks.items().at(row);
  const TicketPr ticket = ticketPrOf(t, m_tasks.gitFactsFor(taskId));
  const heap::git::ForgePrRef ref = ticket.ref;
  if(ref.number <= 0 || !ref.project.contains(QLatin1Char('/'))) {
    return;  // no PR: nothing to say
  }
  const QString key = QStringLiteral("tracker:") + taskId;
  // A watched repo already reads this PR's owners.
  for(auto it = m_codeOwners.constBegin(); it != m_codeOwners.constEnd(); ++it) {
    if(it.key() != key && it->value(QStringLiteral("taskId")).toString() == taskId && it->contains(QStringLiteral("found"))) {
      return;
    }
  }
  heap::git::PrInfo known;
  known.number = ref.number;
  known.url = t.externalUrl.isEmpty() ? m_tasks.gitFactsFor(taskId).value(QStringLiteral("prUrl")).toString() : t.externalUrl;
  const auto store = [this, key, taskId, branch = ticket.branch](QVariantMap report) {
    if(report.isEmpty()) {
      if(m_codeOwners.remove(key) > 0) {
        emit codeOwnersChanged();
      }
      return;
    }
    report[QStringLiteral("taskId")] = taskId;
    report[QStringLiteral("source")] = QStringLiteral("tracker");
    report[QStringLiteral("repo")] = QString();
    report[QStringLiteral("branch")] = branch;
    storeOwnersEntry(key, report);
  };
  if(!settingsMap().value("git").toMap().value("codeOwners", true).toBool()) {
    store(heap::git::ownersReasonVariant(QStringLiteral("off"), ref.host, ref.glab, known));
    return;
  }
  QElapsedTimer& asked = m_trackerOwnersAt[taskId];
  if(!force && asked.isValid() && asked.elapsed() < kTrackerOwnersEveryMs) {
    return;
  }
  asked.start();

  // The tracker the card came from; for a folded link, the one on the PR's
  // host, else the only one of that kind (an alias or a proxy in between).
  const QList<heap::git::ForgeAuth> creds = forgeCredentials();
  const heap::git::ForgeAuth* auth = nullptr;
  for(const heap::git::ForgeAuth& a : creds) {
    if(!ticket.providerId.isEmpty() ? a.providerId == ticket.providerId : heap::git::forgeHostMatches(a, ref.host)) {
      auth = &a;
      break;
    }
  }
  if(auth == nullptr && ticket.providerId.isEmpty()) {
    const auto sameKind = std::ranges::count_if(creds, [&ref](const heap::git::ForgeAuth& a) {
      return a.glab == ref.glab;
    });
    if(sameKind == 1) {
      auth = &*std::ranges::find_if(creds, [&ref](const heap::git::ForgeAuth& a) {
        return a.glab == ref.glab;
      });
    }
  }
  if(auth == nullptr) {
    store(heap::git::ownersReasonVariant(QStringLiteral("noAccess"), ref.host, ref.glab, known));
    return;
  }
  // Through FeatureGate as CODEOWNERS: nothing goes while it is paused.
  const heap::git::ForgeAuth a = auth->withFeature(heap::net::features::codeOwners());
  const QString project = ref.project;
  const auto withPr = [this, a, key, project, ref, known, store](const QString& login) {
    forgeApi()->fetchPr(a, project, ref.number, [this, a, key, project, login, known, store](const heap::git::PrInfo& pr, int status) {
      const QString host = heap::git::forgeHostOf(a.host);
      if(status < 200 || status >= 300) {
        const QString reason = heap::git::ownersReasonForStatus(status);
        // Offline for a moment: the last report stays.
        if((reason == QLatin1String("unreachable") || reason == QLatin1String("serverError")) &&
           m_codeOwners.value(key).contains(QStringLiteral("found"))) {
          return;
        }
        store(heap::git::ownersReasonVariant(reason, host, a.glab, known));
        return;
      }
      if(pr.state != QLatin1String("open")) {
        store({});  // merged or closed: nobody left to ping
        return;
      }
      if(!pr.author.isEmpty() && pr.author.compare(login, Qt::CaseInsensitive) != 0) {
        store(heap::git::ownersReasonVariant(QStringLiteral("notMine"), host, a.glab, pr));
        return;
      }
      forgeApi()->fetchOwnersReport(a, project, pr, [store](const QVariantMap& report) {
        store(report);
      });
    });
  };
  const QString login = m_forgeLogins.value(a.apiBase);
  if(!login.isEmpty()) {
    withPr(login);
    return;
  }
  forgeApi()->fetchLogin(a, [this, a, withPr, store, ref, known](const QString& me, int status) {
    if(me.isEmpty()) {
      const QString reason = heap::git::ownersReasonForStatus(status);
      store(heap::git::ownersReasonVariant(
          reason.isEmpty() ? QStringLiteral("denied") : reason, heap::git::forgeHostOf(a.host), a.glab, known));
      return;
    }
    m_forgeLogins.insert(a.apiBase, me);
    withPr(me);
  });
}

void AppController::refreshTrackerOwnersAll() {
  QStringList ids;
  for(auto it = m_codeOwners.constBegin(); it != m_codeOwners.constEnd(); ++it) {
    if(it->value(QStringLiteral("source")).toString() == QLatin1String("tracker")) {
      ids << it->value(QStringLiteral("taskId")).toString();
    }
  }
  // My open PR / MR cards, once the login on their forge is known: the
  // sidebar's "who to ping" without opening each of them.
  QSet<QString> mine;
  for(const QString& login : std::as_const(m_forgeLogins)) {
    mine.insert(login.toLower());
  }
  int budget = 20;
  for(const Task& t : m_tasks.items()) {
    if(budget <= 0) {
      break;
    }
    if(t.archived || !isReviewItem(t) || ids.contains(t.id) ||
       t.externalMeta.details.value(QStringLiteral("state")).toString() != QLatin1String("open") ||
       !mine.contains(t.externalMeta.author.toLower())) {
      continue;
    }
    ids << t.id;
    --budget;
  }
  for(const QString& id : std::as_const(ids)) {
    refreshTrackerOwners(id, false);
  }
}
