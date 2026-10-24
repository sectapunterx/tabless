#pragma once

#include "git/RemoteRepo.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

// Where a watched repository's git runs: on this machine, or over ssh on a
// dev server. GitWatcher asks the same questions of both; only the runner
// knows how to reach the repository. Every call is asynchronous and killed
// after its timeout.
namespace heap::git {

class GitRunner : public QObject {
  Q_OBJECT
 public:
  using Done = std::function<void(const GitResult&)>;
  // One result per command, and the whole run's (its code is the transport's:
  // 0 when the commands could be run at all).
  using BatchDone = std::function<void(const QList<GitResult>&, const GitResult&)>;

  explicit GitRunner(QObject* parent = nullptr) : QObject(parent) {
  }

  ~GitRunner() override;

  virtual bool available() const = 0;
  virtual bool isRemote() const = 0;
  // A directory gh/glab can run in: the work tree itself, or for a remote
  // repository somewhere harmless (they then get `--repo`).
  virtual QString localWorkDir() const = 0;

  virtual void run(const QStringList& args, int timeoutMs, const Done& done) = 0;
  virtual void runBatch(const QList<QStringList>& commands, int timeoutMs, const BatchDone& done) = 0;

 protected:
  // Starts `program args` (stdin fed when not empty) and reports once.
  // `timedOut` is set when the timer killed it.
  void spawn(const QString& program,
             const QStringList& args,
             const QString& workDir,
             const QByteArray& stdinData,
             int timeoutMs,
             const std::function<void(const GitResult&, bool timedOut)>& done);
};

class LocalGitRunner : public GitRunner {
  Q_OBJECT
 public:
  LocalGitRunner(QString gitPath, QString workTree, QObject* parent = nullptr);

  bool available() const override {
    return !m_git.isEmpty();
  }

  bool isRemote() const override {
    return false;
  }

  QString localWorkDir() const override {
    return m_workTree;
  }

  void run(const QStringList& args, int timeoutMs, const Done& done) override;
  // One after the other, each its own process.
  void runBatch(const QList<QStringList>& commands, int timeoutMs, const BatchDone& done) override;

 private:
  void runFrom(QList<QStringList> rest, QList<GitResult> acc, int timeoutMs, const BatchDone& done);
  QString m_git;
  QString m_workTree;
};

class SshGitRunner : public GitRunner {
  Q_OBJECT
 public:
  SshGitRunner(QString sshPath, RemoteSpec spec, SshOptions options, QObject* parent = nullptr);

  bool available() const override {
    return !m_ssh.isEmpty();
  }

  bool isRemote() const override {
    return true;
  }

  QString localWorkDir() const override;

  const RemoteSpec& spec() const {
    return m_spec;
  }

  void run(const QStringList& args, int timeoutMs, const Done& done) override;
  // All commands in one connection, one script.
  void runBatch(const QList<QStringList>& commands, int timeoutMs, const BatchDone& done) override;

 private:
  QString m_ssh;
  RemoteSpec m_spec;
  SshOptions m_options;
};

// Checks that `spec` answers: connects, finds git and a work tree, reads the
// branch. `done` gets "" on success or a remoteFailureKey(), plus ssh's last
// stderr line for the details.
void testRemoteRepo(const QString& sshPath,
                    const RemoteSpec& spec,
                    QObject* context,
                    const std::function<void(const QString& failure, const QString& detail, const QString& branch)>& done);

}  // namespace heap::git
