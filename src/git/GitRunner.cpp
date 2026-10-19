#include "git/GitRunner.h"

#include <QDir>
#include <QProcess>
#include <QTimer>

#include <memory>
#include <utility>

namespace heap::git {

GitRunner::~GitRunner() {
  // A process still running when its repository stops being watched is
  // killed without telling anyone: its callback would land on a repository
  // that is gone.
  for(QProcess* p : findChildren<QProcess*>(Qt::FindDirectChildrenOnly)) {
    p->disconnect();
    if(p->state() != QProcess::NotRunning) {
      p->kill();
      p->waitForFinished(1000);
    }
  }
}

void GitRunner::spawn(const QString& program,
                      const QStringList& args,
                      const QString& workDir,
                      const QByteArray& stdinData,
                      int timeoutMs,
                      const std::function<void(const GitResult&, bool timedOut)>& done) {
  auto* p = new QProcess(this);
  if(!workDir.isEmpty()) {
    p->setWorkingDirectory(workDir);
  }
  p->setProgram(program);
  p->setArguments(args);
  auto reported = std::make_shared<bool>(false);
  auto timedOut = std::make_shared<bool>(false);
  auto* timer = new QTimer(p);
  timer->setSingleShot(true);
  timer->setInterval(timeoutMs);
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
    GitResult r;
    r.code = status == QProcess::NormalExit && !*timedOut ? code : -1;
    r.out = p->readAllStandardOutput();
    r.err = p->readAllStandardError();
    p->deleteLater();
    done(r, *timedOut);
  });
  connect(p, &QProcess::errorOccurred, this, [p, done, reported](QProcess::ProcessError error) {
    // A process that never started emits no finished(); every other error
    // is followed by one.
    if(error != QProcess::FailedToStart || *reported) {
      return;
    }
    *reported = true;
    p->deleteLater();
    GitResult r;
    r.err = QByteArrayLiteral("tabless: did not start");
    done(r, false);
  });
  p->start();
  if(!stdinData.isEmpty()) {
    p->write(stdinData);
  }
  p->closeWriteChannel();
  timer->start();
}

// ---- local ----

LocalGitRunner::LocalGitRunner(QString gitPath, QString workTree, QObject* parent) :
    GitRunner(parent), m_git(std::move(gitPath)), m_workTree(std::move(workTree)) {
}

void LocalGitRunner::run(const QStringList& args, int timeoutMs, const Done& done) {
  if(m_git.isEmpty()) {
    done(GitResult{});
    return;
  }
  spawn(m_git, args, m_workTree, {}, timeoutMs, [done](const GitResult& r, bool) {
    done(r);
  });
}

void LocalGitRunner::runBatch(const QList<QStringList>& commands, int timeoutMs, const BatchDone& done) {
  runFrom(commands, {}, timeoutMs, done);
}

void LocalGitRunner::runFrom(QList<QStringList> rest, QList<GitResult> acc, int timeoutMs, const BatchDone& done) {
  if(rest.isEmpty()) {
    GitResult overall;
    overall.code = m_git.isEmpty() ? -1 : 0;
    done(acc, overall);
    return;
  }
  const QStringList next = rest.takeFirst();
  run(next, timeoutMs, [this, rest, acc, timeoutMs, done](const GitResult& r) mutable {
    acc.append(r);
    runFrom(rest, acc, timeoutMs, done);
  });
}

// ---- ssh ----

SshGitRunner::SshGitRunner(QString sshPath, RemoteSpec spec, SshOptions options, QObject* parent) :
    GitRunner(parent), m_ssh(std::move(sshPath)), m_spec(std::move(spec)), m_options(std::move(options)) {
}

QString SshGitRunner::localWorkDir() const {
  return QDir::tempPath();
}

void SshGitRunner::run(const QStringList& args, int timeoutMs, const Done& done) {
  runBatch({args}, timeoutMs, [done](const QList<GitResult>& results, const GitResult& overall) {
    GitResult r = results.value(0);
    r.err = overall.err;
    if(r.code < 0 && overall.code != 0) {
      r.code = -1;
    }
    done(r);
  });
}

void SshGitRunner::runBatch(const QList<QStringList>& commands, int timeoutMs, const BatchDone& done) {
  if(m_ssh.isEmpty()) {
    GitResult overall;
    overall.err = QByteArrayLiteral("tabless: no ssh");
    done(parseRemoteBatchOutput({}, commands.size()), overall);
    return;
  }
  const qsizetype n = commands.size();
  spawn(m_ssh,
        sshArguments(m_spec, m_options),
        localWorkDir(),
        remoteGitScript(m_spec.path, commands),
        timeoutMs,
        [done, n](const GitResult& r, bool timedOut) {
          GitResult overall = r;
          if(timedOut) {
            overall.err += QByteArrayLiteral("\ntabless: timed out");
          }
          done(parseRemoteBatchOutput(r.out, n), overall);
        });
}

void testRemoteRepo(const QString& sshPath,
                    const RemoteSpec& spec,
                    QObject* context,
                    const std::function<void(const QString&, const QString&, const QString&)>& done) {
  if(sshPath.isEmpty()) {
    done(remoteFailureKey(RemoteFailure::NoSsh), {}, {});
    return;
  }
  // Short-lived: deletes itself once it has answered.
  auto* runner = new SshGitRunner(sshPath, spec, defaultSshOptions(), context);
  runner->runBatch(remotePollCommands(), 20000, [runner, done](const QList<GitResult>& results, const GitResult& overall) {
    runner->deleteLater();
    const RemoteHead head = parseRemotePoll(results);
    RemoteFailure f = classifyRemoteFailure(overall.code, overall.err);
    if(f == RemoteFailure::None && !head.ok) {
      f = RemoteFailure::Other;
    }
    QString detail;
    const QStringList lines = QString::fromUtf8(overall.err).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for(qsizetype i = lines.size() - 1; i >= 0; --i) {
      const QString l = lines.at(i).trimmed();
      if(!l.isEmpty()) {
        detail = l;
        break;
      }
    }
    done(remoteFailureKey(f), f == RemoteFailure::None ? QString() : detail, head.branch);
  });
}

}  // namespace heap::git
