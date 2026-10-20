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
#include "git/GitRunner.h"
#include "git/GitWatcher.h"
#include "git/RemoteRepo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

void AppController::onCodeOwnersUpdated(const QString& repo, const QString& branch, const QVariantMap& report) {
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
  const QString key = repo + QChar('\n') + branch;
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
  for(auto it = m_codeOwners.constBegin(); it != m_codeOwners.constEnd(); ++it) {
    if(it->value(QStringLiteral("taskId")).toString() == taskId) {
      return *it;
    }
  }
  return {};
}

QVariantList AppController::codeOwnerPings() const {
  QVariantList out;
  for(auto it = m_codeOwners.constBegin(); it != m_codeOwners.constEnd(); ++it) {
    const QVariantMap& r = *it;
    const QString url = r.value(QStringLiteral("prUrl")).toString();
    for(const QString& handle : r.value(QStringLiteral("toPing")).toStringList()) {
      const QString key = url + QChar('|') + handle;
      if(m_dismissedOwnerPings.contains(key)) {
        continue;
      }
      out.append(QVariantMap{{QStringLiteral("handle"), handle},
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
    return !repoKeys.contains(it.value().value(QStringLiteral("repo")).toString());
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
