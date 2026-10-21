#include "GitWatcher.h"
#include "PrFacts.h"

#include "safety/EndOfDay.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QtGlobal>

#include <algorithm>
#include <memory>

namespace heap::git {

namespace {

// A remote repository's poll, and every git command over ssh, gets longer
// than a local one: the connection itself costs a round trip or three.
constexpr int kLocalGitTimeoutMs = 15000;
constexpr int kRemoteGitTimeoutMs = 30000;
// Polled this often with no window activation in between.
constexpr int kRemotePollDefaultMs = 60000;
// pollRemoteNow() more often than this is ignored (alt-tabbing).
constexpr qint64 kRemotePollMinGapMs = 10000;

}  // namespace

GitWatcher::GitWatcher(QObject* parent) :
    QObject(parent),
    m_fsw(new QFileSystemWatcher(this)),
    m_debounce(new QTimer(this)),
    m_remotePoll(new QTimer(this)),
    m_api(new ForgeApi(this)) {
  m_debounce->setSingleShot(true);
  m_debounce->setInterval(250);
  connect(m_debounce, &QTimer::timeout, this, &GitWatcher::onDebounceFired);
  connect(m_fsw, &QFileSystemWatcher::fileChanged, this, &GitWatcher::onFsPathChanged);
  connect(m_fsw, &QFileSystemWatcher::directoryChanged, this, &GitWatcher::onFsPathChanged);
  m_remotePoll->setInterval(kRemotePollDefaultMs);
  connect(m_remotePoll, &QTimer::timeout, this, [this]() {
    for(auto it = m_runners.constBegin(); it != m_runners.constEnd(); ++it) {
      if(it.value()->isRemote()) {
        pollRemote(it.key());
      }
    }
    m_lastRemotePoll.start();
  });

  m_gitPath = QStandardPaths::findExecutable(QStringLiteral("git"));
  m_ghPath = QStandardPaths::findExecutable(QStringLiteral("gh"));
  m_glabPath = QStandardPaths::findExecutable(QStringLiteral("glab"));
  m_sshPath = QStandardPaths::findExecutable(QStringLiteral("ssh"));
  if(m_ghPath.isEmpty() && m_glabPath.isEmpty()) {
    qInfo("GitWatcher: neither 'gh' nor 'glab' on PATH — PR state disabled");
  }
  if(m_gitPath.isEmpty()) {
    qInfo("GitWatcher: 'git' not on PATH — ahead/behind disabled");
  }
}

GitWatcher::~GitWatcher() = default;

QStringList GitWatcher::watchedRepos() const {
  return m_configs.keys();
}

QVariantMap GitWatcher::snapshot() const {
  QVariantMap out;
  for(auto it = m_state.constBegin(); it != m_state.constEnd(); ++it) {
    out.insert(it.key(), it.value().toVariant());
  }
  return out;
}

QString GitWatcher::cacheKey(const QString& repo, const QString& branch) {
  return repo + QChar('\n') + branch;
}

QString GitWatcher::repoKey(const QString& entry) {
  if(const auto spec = parseRemoteSpec(entry)) {
    return spec->canonical();
  }
  return QDir(entry).absolutePath();
}

bool GitWatcher::isRemote(const QString& repo) const {
  const GitRunner* r = runnerFor(repo);
  return r != nullptr && r->isRemote();
}

void GitWatcher::setWatchedRepos(const QStringList& paths) {
  QSet<QString> wanted;
  for(const QString& p : paths) {
    if(p.trimmed().isEmpty()) {
      continue;
    }
    const QString key = repoKey(p.trimmed());
    if(!key.isEmpty()) {
      wanted.insert(key);
    }
  }
  const auto current = QSet<QString>(m_configs.keyBegin(), m_configs.keyEnd());
  for(const QString& p : current) {
    if(!wanted.contains(p)) {
      removeRepo(p);
    }
  }
  for(const QString& p : wanted) {
    if(!m_configs.contains(p)) {
      if(const auto spec = parseRemoteSpec(p)) {
        addRemoteRepo(*spec);
      } else {
        addRepo(p);
      }
    }
  }
}

void GitWatcher::setPrefixes(const QStringList& prefixes) {
  m_matcher.setPrefixes(prefixes);
}

void GitWatcher::setPrFetchEnabled(bool on) {
  m_prEnabled = on;
}

void GitWatcher::setCodeOwnersEnabled(bool on) {
  m_ownersEnabled = on;
  if(!on) {
    m_owners.clear();
  }
}

void GitWatcher::setRemotePollMs(int ms) {
  m_remotePoll->setInterval(qMax(1000, ms));
}

void GitWatcher::pollRemoteNow() {
  if(m_lastRemotePoll.isValid() && m_lastRemotePoll.elapsed() < kRemotePollMinGapMs) {
    return;
  }
  m_lastRemotePoll.start();
  for(auto it = m_runners.constBegin(); it != m_runners.constEnd(); ++it) {
    if(it.value()->isRemote()) {
      pollRemote(it.key());
    }
  }
}

void GitWatcher::addRepo(const QString& path) {
  const QString gitDir = BranchTaskMatcher::resolveGitDir(path);
  if(gitDir.isEmpty()) {
    qInfo("GitWatcher: skipping '%s' — no .git found", qUtf8Printable(path));
    return;
  }
  const RepoConfig cfg{.path = path, .resolvedGitDir = gitDir, .commonGitDir = BranchTaskMatcher::resolveCommonDir(gitDir)};
  m_configs.insert(path, cfg);
  m_runners.insert(path, new LocalGitRunner(m_gitPath, path, this));
  RepoState st;
  st.repoPath = path;
  m_state.insert(path, st);
  rewatchFiles(cfg);
  recomputeForRepo(path);
}

void GitWatcher::addRemoteRepo(const RemoteSpec& spec) {
  const QString key = spec.canonical();
  // No git dir here: HEAD and refs are read with git commands over ssh.
  m_configs.insert(key, RepoConfig{.path = key, .resolvedGitDir = {}, .commonGitDir = {}});
  m_runners.insert(key, new SshGitRunner(m_sshPath, spec, defaultSshOptions(), this));
  RepoState st;
  st.repoPath = key;
  st.remote = true;
  m_state.insert(key, st);
  if(!m_remotePoll->isActive()) {
    m_remotePoll->start();
  }
  pollRemote(key);
}

void GitWatcher::removeRepo(const QString& path) {
  auto it = m_configs.find(path);
  if(it == m_configs.end()) {
    return;
  }
  if(!it->resolvedGitDir.isEmpty()) {
    for(const QString& fp : {it->resolvedGitDir + QStringLiteral("/HEAD"),
                             it->resolvedGitDir + QStringLiteral("/index"),
                             it->commonGitDir + QStringLiteral("/packed-refs")}) {
      m_fsw->removePath(fp);
      m_watchedFileOwner.remove(fp);
    }
  }
  m_configs.erase(it);
  m_state.remove(path);
  m_originUrl.remove(path);
  if(GitRunner* r = m_runners.take(path)) {
    delete r;  // kills what it still runs, silently
  }
  // Its callbacks will never run now, so nothing else clears the in-flight
  // marks it left ("poll:<repo>", "ab:<repo>\n<branch>", …): the same repo
  // added again would wait on them forever.
  for(auto it = m_workingTreeChecks.begin(); it != m_workingTreeChecks.end();) {
    const QString rest = it->section(QLatin1Char(':'), 1);
    if(rest == path || rest.startsWith(path + QLatin1Char('\n'))) {
      it = m_workingTreeChecks.erase(it);
    } else {
      ++it;
    }
  }
  const bool anyRemote = std::ranges::any_of(m_runners, [](const GitRunner* r) {
    return r->isRemote();
  });
  if(!anyRemote) {
    m_remotePoll->stop();
  }
}

void GitWatcher::rewatchFiles(const RepoConfig& cfg) {
  if(cfg.resolvedGitDir.isEmpty()) {
    return;  // remote: polled instead
  }
  // HEAD and index belong to the worktree; packed-refs and refs/heads to the
  // common dir, which is the same place for a plain clone.
  for(const QString& fp : {cfg.resolvedGitDir + QStringLiteral("/HEAD"),
                           cfg.resolvedGitDir + QStringLiteral("/index"),
                           cfg.commonGitDir + QStringLiteral("/packed-refs")}) {
    if(!QFile::exists(fp)) {
      continue;
    }
    if(!m_fsw->files().contains(fp)) {
      m_fsw->addPath(fp);
    }
    m_watchedFileOwner.insert(fp, cfg.path);
  }
  // Also watch the refs/heads directory: branch SHA file is created on
  // first checkout and may not exist yet.
  const QString refsHeads = cfg.commonGitDir + QStringLiteral("/refs/heads");
  if(QFileInfo(refsHeads).isDir() && !m_fsw->directories().contains(refsHeads)) {
    m_fsw->addPath(refsHeads);
    m_watchedFileOwner.insert(refsHeads, cfg.path);
  }
}

void GitWatcher::onFsPathChanged(const QString& path) {
  const QString repo = m_watchedFileOwner.value(path);
  if(!repo.isEmpty()) {
    m_pendingRepos.insert(repo);
  }

  // Re-arm watch in case of atomic-rename replacement (Windows).
  if(QFile::exists(path) && !m_fsw->files().contains(path) && !m_fsw->directories().contains(path)) {
    m_fsw->addPath(path);
  }
  m_debounce->start();
}

void GitWatcher::onDebounceFired() {
  const auto repos = m_pendingRepos;
  m_pendingRepos.clear();
  for(const QString& r : repos) {
    if(m_configs.contains(r)) {
      // Re-arm files for the repo before recomputing.
      rewatchFiles(m_configs.value(r));
      recomputeForRepo(r);
    }
  }
}

QString GitWatcher::readHeadText(const QString& gitDir) {
  QFile f(gitDir + QStringLiteral("/HEAD"));
  if(!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }
  return QString::fromUtf8(f.readAll());
}

QString GitWatcher::readShaForBranch(const QString& gitDir, const QString& branch) {
  if(branch.isEmpty() || branch == QStringLiteral("(detached HEAD)")) {
    return QString();
  }
  {
    QFile f(gitDir + QStringLiteral("/refs/heads/") + branch);
    if(f.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return QString::fromUtf8(f.readAll()).trimmed();
    }
  }
  QFile pr(gitDir + QStringLiteral("/packed-refs"));
  if(!pr.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }
  QTextStream ts(&pr);
  const QString needle = QStringLiteral(" refs/heads/") + branch;
  while(!ts.atEnd()) {
    const QString line = ts.readLine();
    if(line.startsWith('#') || line.startsWith('^')) {
      continue;
    }
    if(line.endsWith(needle)) {
      return line.left(line.indexOf(' '));
    }
  }
  return QString();
}

QString GitWatcher::upstreamForBranch(const QString& gitDir, const QString& branch) {
  if(branch.isEmpty() || branch == QStringLiteral("(detached HEAD)")) {
    return QString();
  }
  QFile f(gitDir + QStringLiteral("/config"));
  if(!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }
  QTextStream ts(&f);
  const QString header = QStringLiteral("[branch \"") + branch + QChar('"');
  bool inSection = false;
  QString remote;
  QString merge;
  while(!ts.atEnd()) {
    const QString line = ts.readLine().trimmed();
    if(line.startsWith('[')) {
      inSection = line.startsWith(header);
      continue;
    }
    if(!inSection) {
      continue;
    }
    if(line.startsWith(QStringLiteral("remote"))) {
      const int eq = line.indexOf('=');
      if(eq > 0) {
        remote = line.mid(eq + 1).trimmed();
      }
    } else if(line.startsWith(QStringLiteral("merge"))) {
      const int eq = line.indexOf('=');
      if(eq > 0) {
        merge = line.mid(eq + 1).trimmed();
      }
    }
  }
  if(remote.isEmpty() || merge.isEmpty()) {
    return QString();
  }
  QString mergeBranch = merge;
  const QString p = QStringLiteral("refs/heads/");
  if(mergeBranch.startsWith(p)) {
    mergeBranch = mergeBranch.mid(p.size());
  }
  return remote + QChar('/') + mergeBranch;
}

void GitWatcher::recomputeForRepo(const QString& path) {
  auto cit = m_configs.constFind(path);
  if(cit == m_configs.constEnd()) {
    return;
  }
  if(isRemote(path)) {
    pollRemote(path);
    return;
  }
  const RepoConfig& cfg = *cit;
  const QString headText = readHeadText(cfg.resolvedGitDir);
  const QString branch = BranchTaskMatcher::branchFromHeadText(headText);
  const QString sha = readShaForBranch(cfg.commonGitDir, branch);
  applyHead(path, branch, sha, upstreamForBranch(cfg.commonGitDir, branch), std::nullopt);
}

void GitWatcher::pollRemote(const QString& key) {
  GitRunner* runner = runnerFor(key);
  if(runner == nullptr || !runner->isRemote()) {
    return;
  }
  const QString inflightKey = QStringLiteral("poll:") + key;
  if(m_workingTreeChecks.contains(inflightKey)) {
    return;  // the poll already on its way answers this one too
  }
  m_workingTreeChecks.insert(inflightKey);
  runner->runBatch(
      remotePollCommands(), kRemoteGitTimeoutMs, [this, key, inflightKey](const QList<GitResult>& results, const GitResult& overall) {
        m_workingTreeChecks.remove(inflightKey);
        const auto sit = m_state.find(key);
        if(sit == m_state.end()) {
          return;
        }
        const RemoteHead head = parseRemotePoll(results);
        RemoteFailure failure = classifyRemoteFailure(overall.code, overall.err);
        if(failure == RemoteFailure::None && !head.ok) {
          failure = RemoteFailure::Other;
        }
        const QString error = remoteFailureKey(failure);
        if(!head.ok) {
          if(sit->error != error) {
            sit->error = error;
            qInfo("GitWatcher: remote '%s' — %s", qUtf8Printable(key), qUtf8Printable(error));
            emit repoStateUpdated(key, sit->toVariant());
          }
          return;
        }
        const bool errorCleared = !sit->error.isEmpty();
        sit->error.clear();
        if(!head.originUrl.isEmpty()) {
          m_originUrl.insert(key, head.originUrl);
        }
        const QString oldBranch = sit->branch;
        const QString oldSha = sit->headSha;
        applyHead(key, head.branch, head.sha, head.upstream, std::make_pair(head.ahead, head.behind));
        if(errorCleared && oldBranch == head.branch && oldSha == head.sha) {
          emit repoStateUpdated(key, m_state.value(key).toVariant());
        }
      });
}

void GitWatcher::applyHead(const QString& path,
                           const QString& branch,
                           const QString& sha,
                           const QString& upstream,
                           const std::optional<std::pair<int, int>>& aheadBehind) {
  RepoState& st = m_state[path];
  const QString oldBranch = st.branch;
  const QString oldSha = st.headSha;
  if(oldBranch == branch && oldSha == sha) {
    // A remote poll also reads ahead/behind: a fetch on the server moves it
    // without moving HEAD.
    if(aheadBehind && (st.ahead != aheadBehind->first || st.behind != aheadBehind->second || st.upstream != upstream)) {
      st.ahead = aheadBehind->first;
      st.behind = aheadBehind->second;
      st.upstream = upstream;
      emit repoStateUpdated(path, st.toVariant());
    }
    return;
  }

  st.branch = branch;
  st.headSha = sha;
  st.upstream = upstream;

  const bool branchActuallyChanged = (oldBranch != branch);
  if(aheadBehind) {
    st.ahead = aheadBehind->first;
    st.behind = aheadBehind->second;
  } else if(branchActuallyChanged) {
    // Reset ahead/behind until rev-list returns.
    st.ahead = 0;
    st.behind = 0;
  }
  if(branchActuallyChanged) {
    const auto mr = m_matcher.extract(branch);
    const QString taskId = mr.matched ? mr.taskId : QString();
    m_lastRepo = path;
    m_lastBranch = branch;
    m_lastTaskId = taskId;
    emit branchChanged(path, branch, taskId);
  }

  emit repoStateUpdated(path, st.toVariant());

  GitRunner* runner = runnerFor(path);
  const bool gitOk = runner != nullptr && runner->available();
  if(branchActuallyChanged) {
    if(gitOk && !aheadBehind) {
      fetchAheadBehindAsync(path, branch);
    }
    if(m_prEnabled) {
      // The API, the CLI, or — with neither — a word on why nothing shows.
      fetchPrAsync(path, branch, /*emitOneShot=*/false);
    }
  }
  // HEAD moved (branch and/or SHA) → recent commit↔task links may have
  // changed. Refresh them regardless of whether the branch name changed.
  if(gitOk) {
    fetchCommitsAsync(path);
  }
}

void GitWatcher::fetchCommitsAsync(const QString& repoPath) {
  GitRunner* runner = runnerFor(repoPath);
  if(runner == nullptr || !runner->available()) {
    return;
  }
  const QString key = QStringLiteral("log:") + repoPath;
  if(m_workingTreeChecks.contains(key)) {
    return;
  }
  m_workingTreeChecks.insert(key);
  runner->run({QStringLiteral("log"),
               QStringLiteral("--all"),
               QStringLiteral("--no-color"),
               QStringLiteral("--max-count=200"),
               // The commit time rides along as a third field, for the
               // safety net's "no sign of life" (APP-157).
               QStringLiteral("--pretty=format:%H%x1f%s%x1f%cI")},
              runner->isRemote() ? kRemoteGitTimeoutMs : kLocalGitTimeoutMs,
              [this, key, repoPath](const GitResult& r) {
                m_workingTreeChecks.remove(key);
                if(r.code == 0 && m_state.contains(repoPath)) {
                  emit commitsUpdated(repoPath, m_matcher.groupCommitsByTask(r.out));
                }
              });
}

bool GitWatcher::createBranch(const QString& repoPath, const QString& branchName, QString* errorOut) {
  const auto fail = [&](const QString& e) {
    if(errorOut) {
      *errorOut = e;
    }
    emit branchCreated(repoPath, branchName, false, e);
    return false;
  };
  if(repoPath.isEmpty()) {
    return fail(QStringLiteral("no repository selected"));
  }
  if(branchName.isEmpty()) {
    return fail(QStringLiteral("empty branch name"));
  }
  const QString key = m_runners.contains(repoPath) ? repoPath : repoKey(repoPath);
  GitRunner* runner = runnerFor(key);
  // A folder that is not watched is still a valid place to branch in: a
  // runner of its own, gone once its checkout has answered (not when any
  // other checkout does).
  GitRunner* oneShot = nullptr;
  if(runner == nullptr && !m_gitPath.isEmpty()) {
    runner = oneShot = new LocalGitRunner(m_gitPath, repoPath, this);
  }
  if(runner == nullptr || !runner->available()) {
    return fail(runner != nullptr && runner->isRemote() ? QStringLiteral("ssh not found on PATH")
                                                        : QStringLiteral("git not found on PATH"));
  }

  // Asynchronous on purpose. `git checkout -b` is fast on a warm repository
  // and not at all fast on a cold or very large one, and waiting for it here
  // froze the window — this was the only blocking process call in heap. A
  // checkout that never returns is killed after 30 s.
  runner->run({QStringLiteral("checkout"), QStringLiteral("-b"), branchName},
              30000,
              [this, repoPath, key, branchName, oneShot](const GitResult& r) {
                if(oneShot != nullptr) {
                  oneShot->deleteLater();
                }
                if(r.code != 0) {
                  QString err = QString::fromUtf8(r.err).trimmed();
                  if(isRemote(key)) {
                    const RemoteFailure f = classifyRemoteFailure(r.code < 0 ? 255 : 0, r.err);
                    if(f != RemoteFailure::None && f != RemoteFailure::Other) {
                      err = QStringLiteral("ssh: ") + remoteFailureKey(f);
                    }
                  }
                  if(err.isEmpty()) {
                    err = QStringLiteral("git checkout -b failed");
                  }
                  emit branchCreated(repoPath, branchName, false, err);
                  return;
                }
                // Reflect the new checkout immediately (the FS watcher would catch
                // up too, but an explicit recompute makes the banner update
                // deterministic; a remote repo is polled right away).
                if(m_configs.contains(key)) {
                  rewatchFiles(m_configs.value(key));
                  recomputeForRepo(key);
                }
                emit branchCreated(repoPath, branchName, true, QString());
              });
  return true;
}

void GitWatcher::runGitAsync(const QString& repoPath, const QStringList& args, const std::function<void(int, const QByteArray&)>& done) {
  GitRunner* runner = runnerFor(repoPath);
  if(runner == nullptr) {
    // An unwatched folder (the safety net may ask about one).
    runner = new LocalGitRunner(m_gitPath, repoPath, this);
    runner->run(args, kLocalGitTimeoutMs, [runner, done](const GitResult& r) {
      runner->deleteLater();
      done(r.code, r.out);
    });
    return;
  }
  runner->run(args, runner->isRemote() ? kRemoteGitTimeoutMs : kLocalGitTimeoutMs, [done](const GitResult& r) {
    done(r.code, r.out);
  });
}

void GitWatcher::checkWorkingTree(const QString& repoPath) {
  const GitRunner* runner = runnerFor(repoPath);
  const bool canRun = runner != nullptr ? runner->available() : !m_gitPath.isEmpty();
  if(!canRun || repoPath.isEmpty()) {
    emit workingTreeChecked(repoPath, 0, 0, false);
    return;
  }
  const QString key = QStringLiteral("wt:") + repoPath;
  if(m_workingTreeChecks.contains(key)) {
    return;  // the answer to the first request answers this one too
  }
  m_workingTreeChecks.insert(key);
  runGitAsync(repoPath, {QStringLiteral("status"), QStringLiteral("--porcelain")}, [this, repoPath, key](int code, const QByteArray& out) {
    if(code != 0) {
      m_workingTreeChecks.remove(key);
      emit workingTreeChecked(repoPath, 0, 0, false);
      return;
    }
    const int changed = heap::safety::countPorcelainEntries(out);
    runGitAsync(repoPath,
                {QStringLiteral("stash"), QStringLiteral("list")},
                [this, repoPath, key, changed](int stashCode, const QByteArray& stashOut) {
                  m_workingTreeChecks.remove(key);
                  emit workingTreeChecked(repoPath, changed, stashCode == 0 ? heap::safety::countStashEntries(stashOut) : 0, true);
                });
  });
}

void GitWatcher::fetchAheadBehindAsync(const QString& repoPath, const QString& branch) {
  if(branch.isEmpty() || branch == QStringLiteral("(detached HEAD)")) {
    return;
  }
  const RepoState& st = m_state.value(repoPath);
  if(st.upstream.isEmpty()) {
    return;
  }

  const QString key = QStringLiteral("ab:") + cacheKey(repoPath, branch);
  if(m_workingTreeChecks.contains(key)) {
    return;
  }
  m_workingTreeChecks.insert(key);
  runGitAsync(
      repoPath,
      {QStringLiteral("rev-list"), QStringLiteral("--left-right"), QStringLiteral("--count"), branch + QStringLiteral("...") + st.upstream},
      [this, key, repoPath, branch](int code, const QByteArray& out) {
        m_workingTreeChecks.remove(key);
        const auto sit = m_state.find(repoPath);
        if(sit == m_state.end() || sit->branch != branch) {
          return;
        }
        if(code == 0) {
          static const QRegularExpression ws(QStringLiteral("\\s+"));
          const QStringList parts = QString::fromUtf8(out).trimmed().split(ws, Qt::SkipEmptyParts);
          if(parts.size() == 2) {
            sit->ahead = parts[0].toInt();
            sit->behind = parts[1].toInt();
          }
        }
        emit repoStateUpdated(repoPath, sit->toVariant());
      });
}

QString GitWatcher::originUrlFromConfig(const QString& commonGitDir) {
  QFile f(commonGitDir + QStringLiteral("/config"));
  if(commonGitDir.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  QTextStream ts(&f);
  bool inOrigin = false;
  while(!ts.atEnd()) {
    const QString line = ts.readLine().trimmed();
    if(line.startsWith('[')) {
      inOrigin = line == QLatin1String("[remote \"origin\"]");
      continue;
    }
    if(inOrigin && line.startsWith(QStringLiteral("url"))) {
      const int eq = line.indexOf('=');
      if(eq > 0 && line.left(eq).trimmed() == QLatin1String("url")) {
        return line.mid(eq + 1).trimmed();
      }
    }
  }
  return {};
}

QString GitWatcher::forgeHostFor(const QString& repo) const {
  QString url = m_originUrl.value(repo);
  if(url.isEmpty() && !isRemote(repo)) {
    url = originUrlFromConfig(m_configs.value(repo).commonGitDir);
  }
  return forgeRepoFromRemoteUrl(url).host.toLower();
}

QProcess* GitWatcher::runForge(const QString& workDir,
                               const QString& tool,
                               const QStringList& args,
                               const std::function<void(int, const QByteArray&)>& done) {
  auto* p = new QProcess(this);
  p->setWorkingDirectory(workDir);
  p->setProgram(tool);
  p->setArguments(args);
  auto reported = std::make_shared<bool>(false);
  auto timedOut = std::make_shared<bool>(false);
  // A gh/glab that never returns (a hung network, a prompt nobody sees)
  // would hold its in-flight mark forever: killed after the timeout, like
  // GitRunner's git.
  auto* timer = new QTimer(p);
  timer->setSingleShot(true);
  timer->setInterval(m_forgeTimeoutMs);
  connect(timer, &QTimer::timeout, p, [p, timedOut]() {
    if(p->state() != QProcess::NotRunning) {
      *timedOut = true;
      p->kill();
    }
  });
  connect(p, &QProcess::finished, this, [p, done, reported, timedOut](int code, QProcess::ExitStatus status) {
    if(*reported) {
      return;
    }
    *reported = true;
    p->deleteLater();
    done(status == QProcess::NormalExit && !*timedOut ? code : -1, p->readAllStandardOutput());
  });
  connect(p, &QProcess::errorOccurred, this, [p, done, reported](QProcess::ProcessError e) {
    // Only a process that never started emits no finished().
    if(e != QProcess::FailedToStart || *reported) {
      return;
    }
    *reported = true;
    p->deleteLater();
    done(-1, {});
  });
  p->start();
  p->closeWriteChannel();
  timer->start();
  return p;
}

void GitWatcher::probeForgeHost(const QString& host) {
  // Both asked at once; the answer is in when both have answered.
  struct Probe {
    int left = 2;
    bool gh = false;
    bool glab = false;
  };

  auto probe = std::make_shared<Probe>();
  const auto answered = [this, host, probe]() {
    if(--probe->left > 0) {
      return;
    }
    // Signed in to that host with glab and not gh → GitLab; the other way →
    // GitHub; neither or both: the name decides, as it did before.
    bool glab = host.contains(QStringLiteral("gitlab"));
    if(probe->glab != probe->gh) {
      glab = probe->glab;
    }
    m_forgeByHost.insert(host, glab);
    const QList<PendingPr> waiting = m_forgeProbes.take(host);
    for(const PendingPr& w : waiting) {
      if(m_state.contains(w.repo)) {
        fetchPrAsync(w.repo, w.branch, w.emitOneShot);
      }
    }
  };
  const QString dir = QDir::tempPath();
  runForge(dir,
           m_ghPath,
           {QStringLiteral("auth"), QStringLiteral("status"), QStringLiteral("--hostname"), host},
           [probe, answered](int code, const QByteArray&) {
             probe->gh = code == 0;
             answered();
           });
  runForge(dir,
           m_glabPath,
           {QStringLiteral("auth"), QStringLiteral("status"), QStringLiteral("--hostname"), host},
           [probe, answered](int code, const QByteArray&) {
             probe->glab = code == 0;
             answered();
           });
}

bool GitWatcher::forgeToolFor(const QString& repo, QString* tool, bool* glab, QStringList* repoArgs, QString* pendingHost) {
  repoArgs->clear();
  const bool remote = isRemote(repo);
  const QString host = forgeHostFor(repo);
  ForgeRepo forge;
  if(remote) {
    // A remote repo: gh/glab run here, told which repository with -R, from
    // the origin URL the poll read on the server.
    forge = forgeRepoFromRemoteUrl(m_originUrl.value(repo));
    if(!forge.valid()) {
      return false;
    }
  }
  bool useGlab = false;
  if(m_ghPath.isEmpty() || m_glabPath.isEmpty()) {
    if(m_ghPath.isEmpty() && m_glabPath.isEmpty()) {
      return false;
    }
    useGlab = m_ghPath.isEmpty();
    // The one tool there is cannot answer for the other forge: gh asked
    // about a GitLab host failed, silently, every time.
    const bool hostIsGithub = host == QLatin1String("github.com") || host.endsWith(QLatin1String(".github.com"));
    const bool hostIsGitlab = host.contains(QLatin1String("gitlab"));
    if((useGlab && hostIsGithub) || (!useGlab && hostIsGitlab)) {
      return false;
    }
  } else if(host.isEmpty()) {
    useGlab = false;  // no origin to go by: gh, as before
  } else if(host == QLatin1String("github.com") || host.endsWith(QLatin1String(".github.com"))) {
    useGlab = false;
  } else if(host == QLatin1String("gitlab.com")) {
    useGlab = true;
  } else if(const auto known = m_forgeByHost.constFind(host); known != m_forgeByHost.constEnd()) {
    useGlab = *known;
  } else {
    // A self-hosted instance, by any name: whichever CLI is signed in to it.
    if(pendingHost != nullptr) {
      *pendingHost = host;
    }
    return false;
  }
  *tool = useGlab ? m_glabPath : m_ghPath;
  *glab = useGlab;
  if(remote) {
    *repoArgs = {QStringLiteral("-R"), useGlab ? glabRepoArg(forge) : ghRepoArg(forge)};
  }
  return true;
}

void GitWatcher::fetchPrAsync(const QString& repoPath, const QString& branch, bool emitOneShot) {
  if(branch.isEmpty() || branch == QStringLiteral("(detached HEAD)")) {
    return;
  }

  const QString key = QStringLiteral("pr:") + cacheKey(repoPath, branch);
  if((m_inflight.contains(key) && m_inflight.value(key)) || m_workingTreeChecks.contains(key)) {
    return;
  }

  // A connected GitLab / GitHub tracker for this forge: its API, no CLI.
  ForgeAuth auth;
  const int api = apiAuthFor(repoPath, &auth, PendingPr{.repo = repoPath, .branch = branch, .emitOneShot = emitOneShot});
  if(api < 0) {
    return;  // runs again once it is known which integration knows the project
  }
  if(api > 0) {
    fetchPrViaApi(repoPath, branch, emitOneShot, auth);
    return;
  }
  m_repoAuth.remove(repoPath);

  QString tool;
  bool glab = false;
  QStringList repoArgs;
  QString pendingHost;
  if(!forgeToolFor(repoPath, &tool, &glab, &repoArgs, &pendingHost)) {
    if(!pendingHost.isEmpty()) {
      // Asked once per host; this fetch runs again when it answers.
      const bool asking = m_forgeProbes.contains(pendingHost);
      QList<PendingPr>& waiting = m_forgeProbes[pendingHost];
      const bool queued = std::ranges::any_of(waiting, [&](const PendingPr& w) {
        return w.repo == repoPath && w.branch == branch;
      });
      if(!queued) {
        waiting.append(PendingPr{.repo = repoPath, .branch = branch, .emitOneShot = emitOneShot});
      }
      if(!asking) {
        probeForgeHost(pendingHost);
      }
      return;
    }
    // No integration and no CLI that can ask this forge: say so rather than
    // stay silent (only a line in the task's owners block).
    const QString host = forgeHostFor(repoPath);
    if(!host.isEmpty()) {
      emitOwnersReason(repoPath, branch, QStringLiteral("noAccess"), host, !host.contains(QLatin1String("github")), PrInfo{});
    }
    return;
  }
  QStringList args =
      glab ? QStringList{QStringLiteral("mr"), QStringLiteral("view"), QStringLiteral("--output"), QStringLiteral("json"), branch}
           : QStringList{QStringLiteral("pr"), QStringLiteral("view"), QStringLiteral("--json"), ghPrJsonFields(), branch};
  args << repoArgs;
  const GitRunner* runner = runnerFor(repoPath);
  const QString workDir = runner != nullptr ? runner->localWorkDir() : repoPath;
  // Whose move it is needs to know who "me" is: asked the first time a PR
  // is looked at (and again after a failed ask), so a setup with no watched
  // repo never runs the tool.
  fetchLoginAsync(workDir, tool, glab);

  QProcess* p = runForge(workDir, tool, args, [this, key, repoPath, branch, emitOneShot, glab](int code, const QByteArray& out) {
    m_inflight.remove(key);
    PrInfo info;
    if(code == 0) {
      info = parsePrJson(out, glab);
    }
    onPrFetched(repoPath, branch, emitOneShot, info);
  });
  m_inflight.insert(key, p);
}

void GitWatcher::onPrFetched(const QString& repoPath, const QString& branch, bool emitOneShot, PrInfo info) {
  info.fetchedAt = QDateTime::currentDateTime();
  applyMove(info, loginFor(repoPath));
  CacheEntry& ce = m_prCache[cacheKey(repoPath, branch)];
  ce.info = info;
  ce.age.start();

  const auto sit = m_state.find(repoPath);
  if(sit != m_state.end() && sit->branch == branch) {
    sit->pr = info;
    emit repoStateUpdated(repoPath, sit->toVariant());
  }
  if(emitOneShot) {
    emit prInfoUpdated(repoPath, branch, info.toVariant());
  }
  if(!m_state.contains(repoPath)) {
    return;
  }
  if(info.state == QLatin1String("open")) {
    if(m_ownersEnabled) {
      fetchOwnersFor(repoPath, branch, info);
    }
    return;
  }
  // Merged, closed or no PR at all: nobody is left to ping (E6-1).
  dropOwners(repoPath, branch);
}

void GitWatcher::dropOwners(const QString& repoPath, const QString& branch) {
  m_owners.remove(cacheKey(repoPath, branch));
  emit codeOwnersUpdated(repoPath, branch, {});
}

void GitWatcher::emitOwnersReason(
    const QString& repoPath, const QString& branch, const QString& reason, const QString& host, bool glab, const PrInfo& pr) {
  if(!m_ownersEnabled) {
    return;
  }
  const QVariantMap v = ownersReasonVariant(reason, host, glab, pr);
  OwnersEntry& e = m_owners[cacheKey(repoPath, branch)];
  e.report = v;
  e.age.start();
  emit codeOwnersUpdated(repoPath, branch, v);
}

void GitWatcher::fetchOwnersFor(const QString& repoPath, const QString& branch, const PrInfo& pr) {
  const auto api = m_repoAuth.constFind(repoPath);
  if(api != m_repoAuth.constEnd()) {
    fetchOwnersViaApi(repoPath, branch, pr, *api);
    return;
  }
  QString tool;
  bool glab = false;
  QStringList repoArgs;
  if(forgeToolFor(repoPath, &tool, &glab, &repoArgs)) {
    fetchOwnersAsync(repoPath, branch, pr, glab);
  }
}

QString GitWatcher::loginFor(const QString& repo) const {
  const auto api = m_repoAuth.constFind(repo);
  return api != m_repoAuth.constEnd() ? m_apiLogins.value(api->apiBase) : m_myLogin;
}

ForgeRepo GitWatcher::forgeRepoOf(const QString& repo) const {
  QString url = m_originUrl.value(repo);
  if(url.isEmpty() && !isRemote(repo)) {
    url = originUrlFromConfig(m_configs.value(repo).commonGitDir);
  }
  return forgeRepoFromRemoteUrl(url);
}

void GitWatcher::setForgeCredentials(const QList<ForgeAuth>& auths) {
  if(auths == m_forgeAuths) {
    return;
  }
  m_forgeAuths = auths;
  ++m_authGen;
  m_authFor.clear();
  m_authWaiting.clear();
  m_repoAuth.clear();
  m_apiLogins.clear();
  m_apiLoginAsked.clear();
  m_prCache.clear();
  m_owners.clear();
  m_workingTreeChecks.removeIf([](const QString& k) {
    return k.startsWith(QStringLiteral("pr:")) || k.startsWith(QStringLiteral("owners:"));
  });
  // A tracker just connected (or its token changed): ask again now.
  if(!m_prEnabled) {
    return;
  }
  for(auto it = m_state.constBegin(); it != m_state.constEnd(); ++it) {
    if(!it->branch.isEmpty()) {
      fetchPrAsync(it.key(), it->branch, /*emitOneShot=*/false);
    }
  }
}

void GitWatcher::refreshPrs() {
  // Offline last time: worth asking again which integration knows the project.
  m_authFor.removeIf([](const auto& it) {
    return it.value() == -2;
  });
  if(!m_prEnabled) {
    return;
  }
  for(auto it = m_state.constBegin(); it != m_state.constEnd(); ++it) {
    const QString& branch = it->branch;
    if(branch.isEmpty() || branch == QStringLiteral("(detached HEAD)")) {
      continue;
    }
    const auto cached = m_prCache.constFind(cacheKey(it.key(), branch));
    if(cached != m_prCache.constEnd() && cached->age.isValid() && cached->age.elapsed() < m_prTtlMs) {
      continue;
    }
    fetchPrAsync(it.key(), branch, /*emitOneShot=*/false);
  }
}

int GitWatcher::apiAuthFor(const QString& repo, ForgeAuth* out, const PendingPr& pending) {
  if(m_forgeAuths.isEmpty()) {
    return 0;
  }
  const ForgeRepo forge = forgeRepoOf(repo);
  if(!forge.valid()) {
    return 0;
  }
  const QString key = forge.host + QLatin1Char('\n') + forge.path;
  if(const auto known = m_authFor.constFind(key); known != m_authFor.constEnd()) {
    if(*known < 0 || *known >= m_forgeAuths.size()) {
      return 0;
    }
    *out = m_forgeAuths.at(*known);
    return 1;
  }
  const QList<int> candidates = forgeAuthCandidates(m_forgeAuths, forge.host);
  if(candidates.isEmpty()) {
    m_authFor.insert(key, -1);
    return 0;
  }
  // The integration's own host: nothing to find out.
  if(candidates.size() == 1 && forgeHostMatches(m_forgeAuths.at(candidates.first()), forge.host)) {
    m_authFor.insert(key, candidates.first());
    *out = m_forgeAuths.at(candidates.first());
    return 1;
  }
  // An ssh alias (or several on one host): the one that knows the project,
  // asked once and remembered.
  const bool asking = m_authWaiting.contains(key);
  QList<PendingPr>& waiting = m_authWaiting[key];
  const bool queued = std::ranges::any_of(waiting, [&](const PendingPr& w) {
    return w.repo == pending.repo && w.branch == pending.branch;
  });
  if(!queued) {
    waiting.append(pending);
  }
  if(!asking) {
    probeAuth(key, forge.path, candidates, 0, false);
  }
  return -1;
}

void GitWatcher::probeAuth(const QString& key, const QString& project, const QList<int>& candidates, int at, bool unsure) {
  const int gen = m_authGen;
  if(at >= candidates.size()) {
    // None of them knows it — or one could not be asked (offline): -2 is
    // asked again with the next window activation.
    m_authFor.insert(key, unsure ? -2 : -1);
    const QList<PendingPr> waiting = m_authWaiting.take(key);
    for(const PendingPr& w : waiting) {
      if(m_state.contains(w.repo)) {
        fetchPrAsync(w.repo, w.branch, w.emitOneShot);
      }
    }
    return;
  }
  const int index = candidates.at(at);
  m_api->probeProject(m_forgeAuths.at(index), project, [this, gen, key, project, candidates, at, unsure, index](bool known, int status) {
    if(gen != m_authGen) {
      return;
    }
    if(!known) {
      probeAuth(key, project, candidates, at + 1, unsure || status == 0 || status >= 500);
      return;
    }
    m_authFor.insert(key, index);
    const QList<PendingPr> waiting = m_authWaiting.take(key);
    for(const PendingPr& w : waiting) {
      if(m_state.contains(w.repo)) {
        fetchPrAsync(w.repo, w.branch, w.emitOneShot);
      }
    }
  });
}

void GitWatcher::fetchApiLogin(const ForgeAuth& auth) {
  if(m_apiLogins.contains(auth.apiBase) || m_apiLoginAsked.contains(auth.apiBase)) {
    return;
  }
  m_apiLoginAsked.insert(auth.apiBase);
  const int gen = m_authGen;
  m_api->fetchLogin(auth, [this, gen, base = auth.apiBase](const QString& login, int) {
    if(gen != m_authGen) {
      return;
    }
    if(login.isEmpty()) {
      m_apiLoginAsked.remove(base);  // asked again with the next PR
      return;
    }
    m_apiLogins.insert(base, login);
    reapplyMoves();
  });
}

void GitWatcher::fetchPrViaApi(const QString& repoPath, const QString& branch, bool emitOneShot, const ForgeAuth& auth) {
  const QString key = QStringLiteral("pr:") + cacheKey(repoPath, branch);
  const ForgeRepo forge = forgeRepoOf(repoPath);
  m_repoAuth.insert(repoPath, auth);
  fetchApiLogin(auth);
  m_workingTreeChecks.insert(key);
  const int gen = m_authGen;
  m_api->fetchPrForBranch(auth, forge.path, branch, [this, gen, key, repoPath, branch, emitOneShot, auth](const PrInfo& pr, int status) {
    if(gen != m_authGen) {
      return;
    }
    m_workingTreeChecks.remove(key);
    if(!m_state.contains(repoPath)) {
      return;
    }
    if(status < 200 || status >= 300) {
      const QString reason = ownersReasonForStatus(status);
      // Offline for a moment: what the banner shows stays.
      if(reason == QLatin1String("unreachable") || reason == QLatin1String("serverError")) {
        return;
      }
      emitOwnersReason(repoPath, branch, reason, forgeHostOf(auth.host), auth.glab, PrInfo{});
      return;
    }
    onPrFetched(repoPath, branch, emitOneShot, pr);
  });
}

void GitWatcher::fetchOwnersViaApi(const QString& repoPath, const QString& branch, const PrInfo& pr, const ForgeAuth& auth) {
  const QString login = m_apiLogins.value(auth.apiBase);
  if(login.isEmpty() || pr.number <= 0) {
    return;  // reapplyMoves() comes back here with the login
  }
  const QString host = forgeHostOf(auth.host);
  if(!pr.author.isEmpty() && pr.author.compare(login, Qt::CaseInsensitive) != 0) {
    emitOwnersReason(repoPath, branch, QStringLiteral("notMine"), host, auth.glab, pr);
    return;
  }
  const QString ckey = cacheKey(repoPath, branch);
  const auto cached = m_owners.constFind(ckey);
  if(cached != m_owners.constEnd() && cached->age.isValid() && cached->age.elapsed() < m_ownersTtlMs &&
     cached->report.value(QStringLiteral("prNumber")).toInt() == pr.number && cached->report.contains(QStringLiteral("found"))) {
    emit codeOwnersUpdated(repoPath, branch, cached->report);
    return;
  }
  const QString inflight = QStringLiteral("owners:") + ckey;
  if(m_workingTreeChecks.contains(inflight)) {
    return;
  }
  m_workingTreeChecks.insert(inflight);
  const int gen = m_authGen;
  // The checkout first: with the MR's head commit in it, the changed files
  // and CODEOWNERS come from git and the forge is asked for approvals only.
  readLocalOwners(repoPath, pr, auth.glab, [this, gen, inflight, ckey, repoPath, branch, pr, auth](const ForgeApi::LocalOwners& local) {
    if(gen != m_authGen) {
      return;
    }
    if(!m_state.contains(repoPath)) {
      m_workingTreeChecks.remove(inflight);
      return;
    }
    m_api->fetchOwnersReport(
        auth,
        forgeRepoOf(repoPath).path,
        pr,
        [this, gen, inflight, ckey, repoPath, branch](const QVariantMap& report) {
          if(gen != m_authGen) {
            return;
          }
          m_workingTreeChecks.remove(inflight);
          if(!m_state.contains(repoPath)) {
            return;
          }
          // A blip is not news: the last report stays.
          const QString why = report.value(QStringLiteral("reason")).toString();
          if((why == QLatin1String("unreachable") || why == QLatin1String("serverError")) && m_owners.contains(ckey)) {
            return;
          }
          OwnersEntry& e = m_owners[ckey];
          e.report = report;
          e.age.start();
          emit codeOwnersUpdated(repoPath, branch, report);
        },
        local);
  });
}

void GitWatcher::readLocalOwners(const QString& repoPath,
                                 const PrInfo& pr,
                                 bool glab,
                                 const std::function<void(const ForgeApi::LocalOwners&)>& done) {
  GitRunner* runner = runnerFor(repoPath);
  if(runner == nullptr || !runner->available() || pr.headSha.isEmpty() || pr.baseRef.isEmpty()) {
    done({});
    return;
  }
  // What the forge diffs: the target as origin has it, from where the MR
  // branched off, to the MR's head. --no-renames lists a renamed file under
  // both names, as the forge does.
  const QString target = QStringLiteral("refs/remotes/origin/") + pr.baseRef;
  const QStringList locations = codeOwnersLocations(glab ? OwnersFlavor::GitLab : OwnersFlavor::GitHub);
  QList<QStringList> cmds{{QStringLiteral("-c"),
                           QStringLiteral("core.quotePath=false"),
                           QStringLiteral("diff"),
                           QStringLiteral("--no-renames"),
                           QStringLiteral("--name-only"),
                           target + QStringLiteral("...") + pr.headSha}};
  for(const QString& loc : locations) {
    cmds.append({QStringLiteral("show"), target + QLatin1Char(':') + loc});
  }
  runner->runBatch(cmds,
                   runner->isRemote() ? kRemoteGitTimeoutMs : kLocalGitTimeoutMs,
                   [locations, done](const QList<GitResult>& results, const GitResult&) {
                     ForgeApi::LocalOwners local;
                     if(results.isEmpty() || results.at(0).code != 0) {
                       done(local);  // not fetched here: the forge lists them
                       return;
                     }
                     local.known = true;
                     for(const QString& line : QString::fromUtf8(results.at(0).out).split(QLatin1Char('\n'))) {
                       const QString f = line.trimmed();
                       if(!f.isEmpty()) {
                         local.files << f;
                       }
                     }
                     for(qsizetype i = 1; i < results.size(); ++i) {
                       if(results.at(i).code == 0) {
                         local.ownersLocation = locations.value(i - 1);
                         local.ownersText = QString::fromUtf8(results.at(i).out);
                         break;
                       }
                     }
                     done(local);
                   });
}

void GitWatcher::fetchOwnersAsync(const QString& repoPath, const QString& branch, const PrInfo& pr, bool glab) {
  // Only my own PRs: on someone else's, who to ping is not my question.
  // Until the login is known nothing is asked: setMyLogin() comes back here.
  if(m_myLogin.isEmpty()) {
    return;
  }
  if(!pr.author.isEmpty() && pr.author.compare(m_myLogin, Qt::CaseInsensitive) != 0) {
    return;
  }
  const QString ckey = cacheKey(repoPath, branch);
  const auto cached = m_owners.constFind(ckey);
  if(cached != m_owners.constEnd() && cached->age.isValid() && cached->age.elapsed() < m_ownersTtlMs) {
    emit codeOwnersUpdated(repoPath, branch, cached->report);
    return;
  }
  const QString inflight = QStringLiteral("owners:") + ckey;
  if(m_workingTreeChecks.contains(inflight)) {
    return;
  }
  // The CLI failed on this MR lately: the forge is left alone until the same
  // back-off ForgeApi keeps runs out (2026-10-20: re-asked every minute).
  const auto hold = m_cliOwnersHold.constFind(ckey);
  if(hold != m_cliOwnersHold.constEnd() && QDateTime::currentMSecsSinceEpoch() < hold->second) {
    return;
  }
  QString tool;
  bool toolGlab = false;
  QStringList repoArgs;
  if(!forgeToolFor(repoPath, &tool, &toolGlab, &repoArgs) || toolGlab != glab || pr.number <= 0) {
    return;
  }
  m_workingTreeChecks.insert(inflight);
  const GitRunner* runner = runnerFor(repoPath);
  const QString workDir = runner != nullptr ? runner->localWorkDir() : repoPath;
  const auto runTool = [this, tool, workDir](const QStringList& args, const std::function<void(int, const QByteArray&)>& done) {
    runForge(workDir, tool, args, done);
  };
  // `gh api` / `glab api` take no -R: a remote repo names its instance.
  QStringList hostArgs;
  ForgeRepo forge;
  if(isRemote(repoPath)) {
    forge = forgeRepoFromRemoteUrl(m_originUrl.value(repoPath));
    if(forge.valid()) {
      hostArgs = {QStringLiteral("--hostname"), forge.host};
    }
  }

  if(!glab) {
    // Reviews from the PR view; the files from the API, every page of them
    // (`pr view --json files` stops at 100).
    const QString repoPathArg = forge.valid() ? forge.path : QStringLiteral("{owner}/{repo}");
    const QString filesPath = QStringLiteral("repos/%1/pulls/%2/files?per_page=100").arg(repoPathArg).arg(pr.number);
    runTool(QStringList{QStringLiteral("pr"), QStringLiteral("view"), branch, QStringLiteral("--json"), ghOwnersJsonFields()} + repoArgs,
            [this, runTool, hostArgs, filesPath, repoPath, branch, pr, inflight](int code, const QByteArray& out) {
              const OwnersFacts facts = code == 0 ? parseGhOwnersFacts(out) : OwnersFacts{};
              if(!facts.ok) {
                cliOwnersFailed(inflight);
                return;
              }
              runTool(QStringList{QStringLiteral("api"), QStringLiteral("--paginate"), filesPath} + hostArgs,
                      [this, facts, repoPath, branch, pr, inflight](int fcode, const QByteArray& fout) {
                        bool ok = false;
                        const QStringList files = fcode == 0 ? parseGhPrFiles(fout, &ok) : QStringList{};
                        if(!ok) {
                          cliOwnersFailed(inflight);
                          return;
                        }
                        OwnersFacts all = facts;
                        all.files = files;
                        finishOwners(repoPath, branch, pr, false, all);
                      });
            });
    return;
  }
  if(pr.projectId <= 0) {
    m_workingTreeChecks.remove(inflight);
    return;
  }
  // `glab api` with an explicit project id.
  const QString base = QStringLiteral("projects/%1/merge_requests/%2/").arg(pr.projectId).arg(pr.number);
  const auto approvals = [this, runTool, hostArgs, base, repoPath, branch, pr, inflight](const QStringList& files) {
    runTool(QStringList{QStringLiteral("api"), base + QStringLiteral("approvals")} + hostArgs,
            [this, files, repoPath, branch, pr, inflight](int acode, const QByteArray& aout) {
              // Not knowing who approved is not "nobody approved": the last
              // report stays.
              if(acode != 0) {
                cliOwnersFailed(inflight);
                return;
              }
              OwnersFacts facts;
              facts.ok = true;
              facts.files = files;
              facts.baseRef = pr.baseRef;
              facts.author = pr.author;
              facts.requested = pr.reviewRequests;
              facts.approvedBy = parseGlabApprovedBy(aout);
              finishOwners(repoPath, branch, pr, true, facts);
            });
  };
  // The changed files from the checkout when it has the MR's head; only
  // otherwise every page of the diffs from the server.
  readLocalOwners(repoPath, pr, true, [this, runTool, hostArgs, base, approvals, inflight](const ForgeApi::LocalOwners& local) {
    if(local.known) {
      approvals(local.files);
      return;
    }
    runTool(QStringList{QStringLiteral("api"), QStringLiteral("--paginate"), base + QStringLiteral("diffs?per_page=100")} + hostArgs,
            [this, approvals, inflight](int code, const QByteArray& out) {
              bool ok = false;
              const QStringList files = code == 0 ? parseGlabChangedFiles(out, &ok) : QStringList{};
              if(!ok) {
                cliOwnersFailed(inflight);
                return;
              }
              approvals(files);
            });
  });
}

void GitWatcher::finishOwners(const QString& repoPath, const QString& branch, const PrInfo& pr, bool glab, const OwnersFacts& facts) {
  const QString ckey = cacheKey(repoPath, branch);
  const QString inflight = QStringLiteral("owners:") + ckey;
  GitRunner* runner = runnerFor(repoPath);
  if(runner == nullptr || !runner->available()) {
    m_workingTreeChecks.remove(inflight);
    return;
  }
  const OwnersFlavor flavor = glab ? OwnersFlavor::GitLab : OwnersFlavor::GitHub;
  const QStringList locations = codeOwnersLocations(flavor);
  const QString baseRef = facts.baseRef.isEmpty() ? pr.baseRef : facts.baseRef;
  // The target branch as the remote has it first (what the forge reads),
  // then the local branch of that name, in the forge's order of places.
  QList<QStringList> cmds;
  const QStringList refs =
      baseRef.isEmpty() ? QStringList{QStringLiteral("HEAD")} : QStringList{QStringLiteral("refs/remotes/origin/") + baseRef, baseRef};
  for(const QString& ref : refs) {
    for(const QString& loc : locations) {
      cmds.append({QStringLiteral("show"), ref + QLatin1Char(':') + loc});
    }
  }
  runner->runBatch(
      cmds,
      runner->isRemote() ? kRemoteGitTimeoutMs : kLocalGitTimeoutMs,
      [this, repoPath, branch, pr, facts, flavor, locations, inflight, ckey](const QList<GitResult>& results, const GitResult&) {
        m_workingTreeChecks.remove(inflight);
        if(!m_state.contains(repoPath)) {
          return;
        }
        QString location;
        QString text;
        for(qsizetype i = 0; i < results.size(); ++i) {
          if(results.at(i).code == 0) {
            location = locations.value(i % locations.size());
            text = QString::fromUtf8(results.at(i).out);
            break;
          }
        }
        const QString author = facts.author.isEmpty() ? pr.author : facts.author;
        const CodeOwnersReport rep = buildOwnersReport(location, text, flavor, facts.files, facts.approvedBy, facts.requested, author);
        PrInfo shown = pr;
        if(!facts.baseRef.isEmpty()) {
          shown.baseRef = facts.baseRef;
        }
        const QVariantMap v = ownersReportVariant(rep, shown, flavor == OwnersFlavor::GitLab, static_cast<int>(facts.files.size()));
        OwnersEntry& e = m_owners[ckey];
        e.report = v;
        e.age.start();
        m_cliOwnersHold.remove(ckey);
        emit codeOwnersUpdated(repoPath, branch, v);
      });
}

void GitWatcher::cliOwnersFailed(const QString& inflight) {
  m_workingTreeChecks.remove(inflight);
  const QString ckey = inflight.mid(QStringLiteral("owners:").size());
  auto& hold = m_cliOwnersHold[ckey];
  ++hold.first;
  hold.second = QDateTime::currentMSecsSinceEpoch() + ForgeApi::backoffMs(hold.first, -1);
}

void GitWatcher::applyMove(PrInfo& info, const QString& login) const {
  const MoveVerdict v = whoseMove(info, login);
  info.move = moveName(v.move);
  info.moveReason = v.reason;
}

void GitWatcher::fetchLoginAsync(const QString& workDir, const QString& tool, bool glab) {
  if(m_loginAsked || tool.isEmpty()) {
    return;
  }
  m_loginAsked = true;
  runForge(workDir,
           tool,
           glab ? QStringList{QStringLiteral("api"), QStringLiteral("user")}
                : QStringList{QStringLiteral("api"), QStringLiteral("user"), QStringLiteral("--jq"), QStringLiteral(".login")},
           [this, glab](int code, const QByteArray& out) {
             const QString login = code == 0 ? parseLogin(out, glab) : QString();
             if(login.isEmpty()) {
               // Offline, signed out, timed out: asked again with the next PR.
               m_loginAsked = false;
               return;
             }
             setMyLogin(login);
           });
}

void GitWatcher::setMyLogin(const QString& login) {
  if(login == m_myLogin) {
    return;
  }
  m_myLogin = login;
  reapplyMoves();
}

void GitWatcher::reapplyMoves() {
  // PRs read before the answer came back said nothing about whose move it
  // is: read them again against the login, without asking the tool again.
  struct OpenPr {
    QString repo;
    QString branch;
    PrInfo pr;
  };

  QList<OpenPr> open;
  for(auto it = m_prCache.begin(); it != m_prCache.end(); ++it) {
    const QString repo = it.key().section(QChar('\n'), 0, 0);
    const QString branch = it.key().section(QChar('\n'), 1);
    applyMove(it->info, loginFor(repo));
    if(it->info.state == QLatin1String("open")) {
      open.append(OpenPr{.repo = repo, .branch = branch, .pr = it->info});
    }
    emit prInfoUpdated(repo, branch, it->info.toVariant());
  }
  for(auto it = m_state.begin(); it != m_state.end(); ++it) {
    if(it->pr.state.isEmpty()) {
      continue;
    }
    applyMove(it->pr, loginFor(it.key()));
    emit repoStateUpdated(it.key(), it->toVariant());
  }
  // Who to ping waited for the login (only my own PRs are asked about).
  if(!m_ownersEnabled) {
    return;
  }
  for(const OpenPr& o : std::as_const(open)) {
    if(m_state.contains(o.repo) && !loginFor(o.repo).isEmpty()) {
      fetchOwnersFor(o.repo, o.branch, o.pr);
    }
  }
}

void GitWatcher::requestPrFetch(const QString& repoPath, const QString& branch) {
  if(repoPath.isEmpty() || branch.isEmpty()) {
    return;
  }
  const QString key = cacheKey(repoPath, branch);
  auto it = m_prCache.constFind(key);
  if(it != m_prCache.constEnd() && it->age.isValid() && it->age.elapsed() < m_prTtlMs) {
    emit prInfoUpdated(repoPath, branch, it->info.toVariant());
    const auto owners = m_owners.constFind(key);
    if(owners != m_owners.constEnd()) {
      emit codeOwnersUpdated(repoPath, branch, owners->report);
    }
    return;
  }
  fetchPrAsync(repoPath, branch, /*emitOneShot=*/true);
}

}  // namespace heap::git
