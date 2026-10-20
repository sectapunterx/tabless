// Repositories on an SSH dev server (dogfood 2026-10-19): the spec, the ssh
// command line and script, the batch answer, failures — and the runner and
// GitWatcher end to end against a fake `ssh` (tests/fake_ssh.cpp), never a
// real server.

#include "git/GitRunner.h"
#include "git/GitWatcher.h"
#include "git/RemoteRepo.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>

using namespace heap::git;

namespace {

bool waitUntil(const std::function<bool()>& pred, int ms = 20000) {
  QElapsedTimer t;
  t.start();
  while(!pred() && t.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return pred();
}

// Sets an environment variable for one test and puts it back after.
struct EnvGuard {
  QByteArray name;
  QByteArray old;
  bool had;

  EnvGuard(const char* n, const QByteArray& value) : name(n), old(qgetenv(n)), had(qEnvironmentVariableIsSet(n)) {
    qputenv(n, value);
  }

  ~EnvGuard() {
    if(had) {
      qputenv(name.constData(), old);
    } else {
      qunsetenv(name.constData());
    }
  }

  EnvGuard(const EnvGuard&) = delete;
  EnvGuard& operator=(const EnvGuard&) = delete;
};

QString readAll(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool writeFile(const QString& path, const QByteArray& data) {
  QFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

}  // namespace

// ---- spec ----

TEST(RemoteSpec, ScpLike) {
  const auto s = parseRemoteSpec(QStringLiteral("devbox:/home/me/src/app"));
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->host, QStringLiteral("devbox"));
  EXPECT_TRUE(s->user.isEmpty());
  EXPECT_EQ(s->port, 0);
  EXPECT_EQ(s->path, QStringLiteral("/home/me/src/app"));
  EXPECT_EQ(s->canonical(), QStringLiteral("devbox:/home/me/src/app"));
  EXPECT_EQ(s->shortName(), QStringLiteral("devbox:app"));
}

TEST(RemoteSpec, ScpLikeWithUserAndHomePath) {
  const auto s = parseRemoteSpec(QStringLiteral("  me@dev.example.com:~/src/app/ "));
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->user, QStringLiteral("me"));
  EXPECT_EQ(s->host, QStringLiteral("dev.example.com"));
  EXPECT_EQ(s->path, QStringLiteral("~/src/app/"));
  EXPECT_EQ(s->destination(), QStringLiteral("me@dev.example.com"));
  EXPECT_EQ(s->shortName(), QStringLiteral("dev.example.com:app"));
}

TEST(RemoteSpec, SshUrlWithPort) {
  const auto s = parseRemoteSpec(QStringLiteral("ssh://me@10.0.0.5:2222/srv/repo"));
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->user, QStringLiteral("me"));
  EXPECT_EQ(s->host, QStringLiteral("10.0.0.5"));
  EXPECT_EQ(s->port, 2222);
  EXPECT_EQ(s->path, QStringLiteral("/srv/repo"));
  EXPECT_EQ(s->canonical(), QStringLiteral("ssh://me@10.0.0.5:2222/srv/repo"));
  // The canonical spelling reads back to the same thing.
  EXPECT_EQ(parseRemoteSpec(s->canonical())->canonical(), s->canonical());
}

TEST(RemoteSpec, SshUrlHomeRelative) {
  const auto s = parseRemoteSpec(QStringLiteral("ssh://dev/~/src"));
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->path, QStringLiteral("~/src"));
}

TEST(RemoteSpec, LocalPathsAreNotRemote) {
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("C:/src/app")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("C:\\src\\app")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("/home/me/app")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("\\\\server\\share\\repo")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("./a:b")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("relative/dir")));
  EXPECT_FALSE(isRemoteRepoSpec(QString()));
}

TEST(RemoteSpec, Malformed) {
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("host:")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("ssh://host")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("ssh://host:99999/x")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("ssh://host:abc/x")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("-oProxyCommand=x:/p")));
  EXPECT_FALSE(isRemoteRepoSpec(QStringLiteral("-me@host:/p")));
}

TEST(RemoteSpec, WatcherKeyIsCanonical) {
  EXPECT_EQ(GitWatcher::repoKey(QStringLiteral("me@dev:/x")), QStringLiteral("me@dev:/x"));
  EXPECT_EQ(GitWatcher::repoKey(QStringLiteral("ssh://dev/x")), QStringLiteral("dev:/x"));
}

// ---- ssh_config ----

TEST(SshConfig, HostNamesWithoutPatterns) {
  QStringList includes;
  const QStringList hosts = parseSshConfigHosts(QStringLiteral("# mine\n"
                                                               "Host devbox build-01\n"
                                                               "  HostName 10.0.0.5\n"
                                                               "  User me\n"
                                                               "host=gpu\n"
                                                               "Host *.corp !bastion\n"
                                                               "Host *\n"
                                                               "Include config.d/*\n"
                                                               "Host \"quoted\" devbox\n"),
                                                &includes);
  EXPECT_EQ(hosts, (QStringList{"devbox", "build-01", "gpu", "quoted"}));
  EXPECT_EQ(includes, (QStringList{"config.d/*"}));
}

// ---- the command line ----

TEST(SshArgs, BatchModeTimeoutAndShStdin) {
  RemoteSpec s;
  s.host = QStringLiteral("dev");
  s.user = QStringLiteral("me");
  s.port = 2222;
  s.path = QStringLiteral("/x");
  SshOptions o;
  o.connectTimeoutSec = 7;
  const QStringList a = sshArguments(s, o);
  EXPECT_TRUE(a.contains(QStringLiteral("BatchMode=yes")));
  EXPECT_TRUE(a.contains(QStringLiteral("ConnectTimeout=7")));
  EXPECT_FALSE(a.join(' ').contains(QStringLiteral("ControlMaster")));
  const qsizetype p = a.indexOf(QStringLiteral("-p"));
  ASSERT_GE(p, 0);
  EXPECT_EQ(a.at(p + 1), QStringLiteral("2222"));
  // The destination and then the remote command, last.
  ASSERT_GE(a.size(), 3);
  EXPECT_EQ(a.mid(a.size() - 3), (QStringList{"me@dev", "sh", "-s"}));
}

TEST(SshArgs, ControlMasterWhenSupported) {
  RemoteSpec s;
  s.host = QStringLiteral("dev");
  s.path = QStringLiteral("/x");
  SshOptions o;
  o.controlMaster = true;
  o.controlPath = QStringLiteral("~/.ssh/tabless-%C");
  const QStringList a = sshArguments(s, o);
  EXPECT_TRUE(a.contains(QStringLiteral("ControlMaster=auto")));
  EXPECT_TRUE(a.contains(QStringLiteral("ControlPath=~/.ssh/tabless-%C")));
  EXPECT_TRUE(a.contains(QStringLiteral("ControlPersist=120")));
}

TEST(SshArgs, WindowsHasNoControlMaster) {
#ifdef Q_OS_WIN
  EXPECT_FALSE(sshSupportsControlMaster());
  EXPECT_FALSE(defaultSshOptions().controlMaster);
#else
  EXPECT_TRUE(sshSupportsControlMaster());
#endif
}

TEST(SshScript, QuotesEveryArgumentAndThePath) {
  EXPECT_EQ(shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
  const QString script = QString::fromUtf8(
      remoteGitScript(QStringLiteral("/srv/my repo"), {{QStringLiteral("log"), QStringLiteral("--pretty=format:%H;rm -rf /")}}));
  EXPECT_TRUE(script.contains(QStringLiteral("cd -- '/srv/my repo'")));
  EXPECT_TRUE(script.contains(QStringLiteral("git 'log' '--pretty=format:%H;rm -rf /'; printf")));
  EXPECT_TRUE(script.contains(QStringLiteral("exit 96")));
  EXPECT_TRUE(script.contains(QStringLiteral("exit 97")));
  EXPECT_TRUE(script.contains(QStringLiteral("exit 98")));
}

TEST(SshScript, HomeRelativePathExpandsHome) {
  const QString script = QString::fromUtf8(remoteGitScript(QStringLiteral("~/src/app"), {}));
  EXPECT_TRUE(script.contains(QStringLiteral("cd -- \"$HOME\"/'src/app'")));
}

// ---- the batch answer ----

TEST(SshBatch, SplitsByMarker) {
  const QByteArray out = "main\n\n@@tabless-end:0@@\n\n@@tabless-end:1@@\n2\t3\n@@tabless-end:0@@\n";
  const auto r = parseRemoteBatchOutput(out, 3);
  ASSERT_EQ(r.size(), 3);
  EXPECT_EQ(r[0].code, 0);
  EXPECT_EQ(r[0].out, QByteArray("main\n"));
  EXPECT_EQ(r[1].code, 1);
  EXPECT_TRUE(r[1].out.isEmpty());
  EXPECT_EQ(r[2].out, QByteArray("2\t3"));
}

TEST(SshBatch, ACutAnswerLeavesTheRestUnrun) {
  const auto r = parseRemoteBatchOutput("main\n\n@@tabless-end:0@@\npartial", 3);
  ASSERT_EQ(r.size(), 3);
  EXPECT_EQ(r[0].code, 0);
  EXPECT_EQ(r[1].code, -1);
  EXPECT_EQ(r[2].code, -1);
}

TEST(RemotePoll, ParsesBranchShaUpstreamAheadBehindOrigin) {
  QList<GitResult> rs(5);
  rs[0] = {0, "feature/APP-7-x\n", {}};
  rs[1] = {0, "abc123\n", {}};
  rs[2] = {0, "origin/feature/APP-7-x\n", {}};
  rs[3] = {0, "2\t5\n", {}};
  rs[4] = {0, "git@github.com:me/app.git\n", {}};
  const RemoteHead h = parseRemotePoll(rs);
  EXPECT_TRUE(h.ok);
  EXPECT_EQ(h.branch, QStringLiteral("feature/APP-7-x"));
  EXPECT_EQ(h.sha, QStringLiteral("abc123"));
  EXPECT_EQ(h.upstream, QStringLiteral("origin/feature/APP-7-x"));
  EXPECT_EQ(h.ahead, 2);
  EXPECT_EQ(h.behind, 5);
  EXPECT_EQ(h.originUrl, QStringLiteral("git@github.com:me/app.git"));
}

TEST(RemotePoll, DetachedAndNoUpstream) {
  QList<GitResult> rs(5);
  rs[0] = {1, {}, {}};
  rs[1] = {0, "abc\n", {}};
  rs[2] = {128, {}, {}};
  rs[3] = {128, {}, {}};
  rs[4] = {1, {}, {}};
  const RemoteHead h = parseRemotePoll(rs);
  EXPECT_TRUE(h.ok);
  EXPECT_EQ(h.branch, QStringLiteral("(detached HEAD)"));
  EXPECT_TRUE(h.upstream.isEmpty());
  EXPECT_EQ(h.ahead, 0);
}

TEST(RemotePoll, NothingRanIsNotOk) {
  EXPECT_FALSE(parseRemotePoll(parseRemoteBatchOutput({}, 5)).ok);
}

// ---- failures ----

TEST(RemoteFailureTest, Classified) {
  EXPECT_EQ(classifyRemoteFailure(0, {}), RemoteFailure::None);
  EXPECT_EQ(classifyRemoteFailure(255, "ssh: Could not resolve hostname devbox: Name or service not known"), RemoteFailure::Unreachable);
  EXPECT_EQ(classifyRemoteFailure(255, "ssh: connect to host 10.0.0.5 port 22: Connection timed out"), RemoteFailure::Unreachable);
  EXPECT_EQ(classifyRemoteFailure(255, "ssh: connect to host x port 22: Connection refused"), RemoteFailure::Unreachable);
  EXPECT_EQ(classifyRemoteFailure(255, "me@dev: Permission denied (publickey,password)."), RemoteFailure::Auth);
  EXPECT_EQ(classifyRemoteFailure(255, "Host key verification failed."), RemoteFailure::HostKey);
  EXPECT_EQ(classifyRemoteFailure(96, "tabless: no such directory"), RemoteFailure::NoPath);
  EXPECT_EQ(classifyRemoteFailure(97, "tabless: git not found"), RemoteFailure::NoGit);
  EXPECT_EQ(classifyRemoteFailure(98, "tabless: not a git repository"), RemoteFailure::NotRepo);
  EXPECT_EQ(classifyRemoteFailure(-1, "\ntabless: timed out"), RemoteFailure::Timeout);
  EXPECT_EQ(classifyRemoteFailure(-1, "tabless: did not start"), RemoteFailure::NoSsh);
  EXPECT_EQ(classifyRemoteFailure(3, "weird"), RemoteFailure::Other);
  EXPECT_EQ(remoteFailureKey(RemoteFailure::Auth), QStringLiteral("auth"));
  EXPECT_TRUE(remoteFailureKey(RemoteFailure::None).isEmpty());
}

// ---- origin → gh/glab ----

TEST(ForgeRepoTest, FromOriginUrls) {
  auto r = forgeRepoFromRemoteUrl(QStringLiteral("git@github.com:me/app.git"));
  EXPECT_EQ(r.host, QStringLiteral("github.com"));
  EXPECT_EQ(r.path, QStringLiteral("me/app"));
  EXPECT_EQ(ghRepoArg(r), QStringLiteral("me/app"));
  r = forgeRepoFromRemoteUrl(QStringLiteral("https://gitlab.corp.io/group/sub/app.git/"));
  EXPECT_EQ(r.host, QStringLiteral("gitlab.corp.io"));
  EXPECT_EQ(r.path, QStringLiteral("group/sub/app"));
  EXPECT_EQ(glabRepoArg(r), QStringLiteral("https://gitlab.corp.io/group/sub/app"));
  EXPECT_EQ(ghRepoArg(r), QStringLiteral("gitlab.corp.io/group/sub/app"));
  r = forgeRepoFromRemoteUrl(QStringLiteral("ssh://git@ghe.corp:2222/team/app"));
  EXPECT_EQ(r.host, QStringLiteral("ghe.corp"));
  EXPECT_EQ(r.path, QStringLiteral("team/app"));
  EXPECT_FALSE(forgeRepoFromRemoteUrl(QStringLiteral("/srv/git/app.git")).valid());
  EXPECT_FALSE(forgeRepoFromRemoteUrl(QString()).valid());
}

// ---- the runner against a fake ssh ----

class FakeSsh : public ::testing::Test {
 protected:
  QTemporaryDir dir;
  QString log;

  void SetUp() override {
    ASSERT_TRUE(dir.isValid());
    log = dir.filePath(QStringLiteral("ssh.log"));
  }

  static RemoteSpec spec(const QString& path) {
    RemoteSpec s;
    s.host = QStringLiteral("devbox");
    s.user = QStringLiteral("me");
    s.path = path;
    return s;
  }
};

TEST_F(FakeSsh, BatchIsOneConnectionWithTheScriptOnStdin) {
  const QString outFile = dir.filePath(QStringLiteral("out.txt"));
  ASSERT_TRUE(writeFile(outFile, "main\n\n@@tabless-end:0@@\nabc\n\n@@tabless-end:0@@\n"));
  EnvGuard e1("FAKE_SSH_LOG", log.toLocal8Bit());
  EnvGuard e2("FAKE_SSH_MODE", "canned");
  EnvGuard e3("FAKE_SSH_STDOUT", outFile.toLocal8Bit());
  EnvGuard e4("FAKE_SSH_EXIT", "0");
  SshGitRunner runner(QStringLiteral(FAKE_SSH_PATH), spec(QStringLiteral("/srv/app")), SshOptions{});
  QList<GitResult> got;
  bool done = false;
  runner.runBatch({{QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("HEAD")},
                   {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}},
                  10000,
                  [&](const QList<GitResult>& r, const GitResult&) {
                    got = r;
                    done = true;
                  });
  ASSERT_TRUE(waitUntil([&] {
    return done;
  }));
  ASSERT_EQ(got.size(), 2);
  EXPECT_EQ(got[0].out.trimmed(), QByteArray("main"));
  EXPECT_EQ(got[1].out.trimmed(), QByteArray("abc"));
  const QString l = readAll(log);
  EXPECT_EQ(l.count(QStringLiteral("===")), 1);  // one ssh process
  EXPECT_TRUE(l.contains(QStringLiteral("BatchMode=yes")));
  EXPECT_TRUE(l.contains(QStringLiteral("me@devbox\nsh\n-s\n---\n")));
  EXPECT_TRUE(l.contains(QStringLiteral("git 'symbolic-ref' '--short' 'HEAD'")));
  EXPECT_TRUE(l.contains(QStringLiteral("cd -- '/srv/app'")));
}

TEST_F(FakeSsh, TestConnectionReportsAuth) {
  EnvGuard e1("FAKE_SSH_MODE", "canned");
  EnvGuard e2("FAKE_SSH_STDERR", "me@devbox: Permission denied (publickey).\n");
  EnvGuard e3("FAKE_SSH_EXIT", "255");
  EnvGuard e4("FAKE_SSH_STDOUT", "");
  QObject ctx;
  QString failure = QStringLiteral("unset");
  QString detail;
  testRemoteRepo(
      QStringLiteral(FAKE_SSH_PATH), spec(QStringLiteral("/srv/app")), &ctx, [&](const QString& f, const QString& d, const QString&) {
        failure = f;
        detail = d;
      });
  ASSERT_TRUE(waitUntil([&] {
    return failure != QLatin1String("unset");
  }));
  EXPECT_EQ(failure, QStringLiteral("auth"));
  EXPECT_EQ(detail, QStringLiteral("me@devbox: Permission denied (publickey)."));
}

TEST_F(FakeSsh, TestConnectionReportsNotARepo) {
  EnvGuard e1("FAKE_SSH_MODE", "canned");
  EnvGuard e2("FAKE_SSH_STDERR", "tabless: not a git repository\n");
  EnvGuard e3("FAKE_SSH_EXIT", "98");
  EnvGuard e4("FAKE_SSH_STDOUT", "");
  QObject ctx;
  QString failure = QStringLiteral("unset");
  testRemoteRepo(
      QStringLiteral(FAKE_SSH_PATH), spec(QStringLiteral("/srv/app")), &ctx, [&](const QString& f, const QString&, const QString&) {
        failure = f;
      });
  ASSERT_TRUE(waitUntil([&] {
    return failure != QLatin1String("unset");
  }));
  EXPECT_EQ(failure, QStringLiteral("notRepo"));
}

TEST_F(FakeSsh, MissingSshBinary) {
  QObject ctx;
  QString failure = QStringLiteral("unset");
  testRemoteRepo(dir.filePath(QStringLiteral("no-such-ssh.exe")),
                 spec(QStringLiteral("/x")),
                 &ctx,
                 [&](const QString& f, const QString&, const QString&) {
                   failure = f;
                 });
  ASSERT_TRUE(waitUntil([&] {
    return failure != QLatin1String("unset");
  }));
  EXPECT_EQ(failure, QStringLiteral("noSsh"));
}

// The script itself, run by a real sh against a real repository: the fake
// ssh plays the server with the local shell.
class FakeSshServer : public FakeSsh {
 protected:
  QString repo;
  QString git;

  void SetUp() override {
    FakeSsh::SetUp();
    git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if(git.isEmpty() || QStandardPaths::findExecutable(QStringLiteral("sh")).isEmpty()) {
      GTEST_SKIP() << "needs git and sh on PATH";
    }
    repo = dir.filePath(QStringLiteral("repo"));
    ASSERT_TRUE(QDir().mkpath(repo));
    ASSERT_EQ(runGit({"init", "-q", "-b", "main"}), 0);
    ASSERT_EQ(runGit({"config", "user.email", "t@example.com"}), 0);
    ASSERT_EQ(runGit({"config", "user.name", "t"}), 0);
    ASSERT_TRUE(writeFile(repo + QStringLiteral("/CODEOWNERS"), "* @lead\n"));
    ASSERT_EQ(runGit({"add", "."}), 0);
    ASSERT_EQ(runGit({"commit", "-q", "-m", "APP-7 first"}), 0);
    ASSERT_EQ(runGit({"checkout", "-q", "-b", "feature/APP-7-remote"}), 0);
  }

  int runGit(const QStringList& args) {
    QProcess p;
    p.setWorkingDirectory(repo);
    p.start(git, args);
    p.waitForFinished(30000);
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
  }
};

TEST_F(FakeSshServer, RunnerReadsHeadAndAFileFromTheBranch) {
  EnvGuard e1("FAKE_SSH_MODE", "sh");
  EnvGuard e2("FAKE_SSH_HOME", dir.path().toLocal8Bit());
  SshGitRunner runner(QStringLiteral(FAKE_SSH_PATH), spec(repo), SshOptions{});
  QList<GitResult> got;
  GitResult overall;
  bool done = false;
  QList<QStringList> cmds = remotePollCommands();
  cmds.append({QStringLiteral("show"), QStringLiteral("main:CODEOWNERS")});
  runner.runBatch(cmds, 30000, [&](const QList<GitResult>& r, const GitResult& o) {
    got = r;
    overall = o;
    done = true;
  });
  ASSERT_TRUE(waitUntil(
      [&] {
        return done;
      },
      60000));
  ASSERT_EQ(got.size(), 6) << overall.err.constData();
  const RemoteHead h = parseRemotePoll(got.mid(0, 5));
  EXPECT_TRUE(h.ok);
  EXPECT_EQ(h.branch, QStringLiteral("feature/APP-7-remote"));
  EXPECT_EQ(h.sha.size(), 40);
  EXPECT_TRUE(h.upstream.isEmpty());
  EXPECT_EQ(got[5].code, 0);
  EXPECT_EQ(got[5].out.trimmed(), QByteArray("* @lead"));
}

TEST_F(FakeSshServer, NotARepoAndNoSuchDirectory) {
  EnvGuard e1("FAKE_SSH_MODE", "sh");
  EnvGuard e2("FAKE_SSH_HOME", dir.path().toLocal8Bit());
  for(const auto& [path, want] :
      {std::pair{dir.filePath(QStringLiteral("nope")), QStringLiteral("noPath")}, std::pair{dir.path(), QStringLiteral("notRepo")}}) {
    QObject ctx;
    QString failure = QStringLiteral("unset");
    testRemoteRepo(QStringLiteral(FAKE_SSH_PATH), spec(path), &ctx, [&](const QString& f, const QString&, const QString&) {
      failure = f;
    });
    ASSERT_TRUE(waitUntil(
        [&] {
          return failure != QLatin1String("unset");
        },
        60000));
    EXPECT_EQ(failure, want) << qPrintable(path);
  }
}

TEST_F(FakeSshServer, WatcherFollowsARemoteBranchLikeALocalOne) {
  EnvGuard e1("FAKE_SSH_MODE", "sh");
  EnvGuard e2("FAKE_SSH_HOME", dir.path().toLocal8Bit());
  GitWatcher w;
  w.setSshProgram(QStringLiteral(FAKE_SSH_PATH));
  w.setPrFetchEnabled(false);
  w.setPrefixes({QStringLiteral("APP")});
  QString branch;
  QString taskId;
  QStringList commitsFor;
  QObject::connect(&w, &GitWatcher::branchChanged, [&](const QString&, const QString& b, const QString& t) {
    branch = b;
    taskId = t;
  });
  QObject::connect(&w, &GitWatcher::commitsUpdated, [&](const QString&, const QVariantMap& byTask) {
    commitsFor = byTask.keys();
  });
  const QString entry = QStringLiteral("me@devbox:") + repo;
  w.setWatchedRepos({entry});
  ASSERT_TRUE(waitUntil(
      [&] {
        return !branch.isEmpty();
      },
      60000));
  EXPECT_EQ(branch, QStringLiteral("feature/APP-7-remote"));
  EXPECT_EQ(taskId, QStringLiteral("APP-7"));
  const QVariantMap snap = w.snapshot().value(entry).toMap();
  EXPECT_TRUE(snap.value(QStringLiteral("remote")).toBool());
  EXPECT_TRUE(snap.value(QStringLiteral("error")).toString().isEmpty());
  // Commits read over ssh too.
  ASSERT_TRUE(waitUntil(
      [&] {
        return !commitsFor.isEmpty();
      },
      60000));
  EXPECT_TRUE(commitsFor.contains(QStringLiteral("APP-7")));

  // A checkout on the server shows on the next poll.
  ASSERT_EQ(runGit({"checkout", "-q", "main"}), 0);
  w.pollRemoteNow();  // the first poll was just now: throttled
  w.setRemotePollMs(1000);
  ASSERT_TRUE(waitUntil(
      [&] {
        return branch == QLatin1String("main");
      },
      60000));

  // A branch created from Tabless is created over ssh.
  bool created = false;
  QObject::connect(&w, &GitWatcher::branchCreated, [&](const QString&, const QString&, bool ok, const QString&) {
    created = ok;
  });
  QString err;
  ASSERT_TRUE(w.createBranch(entry, QStringLiteral("feature/APP-8-new"), &err));
  ASSERT_TRUE(waitUntil(
      [&] {
        return created;
      },
      60000));
  ASSERT_TRUE(waitUntil(
      [&] {
        return branch == QLatin1String("feature/APP-8-new");
      },
      60000));
}

TEST_F(FakeSsh, WatcherSaysWhyARemoteRepoIsDown) {
  EnvGuard e1("FAKE_SSH_MODE", "canned");
  EnvGuard e2("FAKE_SSH_STDERR", "ssh: Could not resolve hostname devbox\n");
  EnvGuard e3("FAKE_SSH_EXIT", "255");
  EnvGuard e4("FAKE_SSH_STDOUT", "");
  GitWatcher w;
  w.setSshProgram(QStringLiteral(FAKE_SSH_PATH));
  QString error;
  QObject::connect(&w, &GitWatcher::repoStateUpdated, [&](const QString&, const QVariantMap& st) {
    error = st.value(QStringLiteral("error")).toString();
  });
  w.setWatchedRepos({QStringLiteral("devbox:/srv/app")});
  ASSERT_TRUE(waitUntil([&] {
    return !error.isEmpty();
  }));
  EXPECT_EQ(error, QStringLiteral("unreachable"));
}

// ---- review fixes (dogfood 2026-10-19) ----

TEST(SshBatch, ShellStartupNoiseBeforeTheBeginMarkerIsDropped) {
  // .zshenv / BASH_ENV printing something lands before the first command.
  const QByteArray out = "Welcome to devbox!\nlast login: today\n@@tabless-begin@@\nmain\n\n@@tabless-end:0@@\nabc\n\n@@tabless-end:0@@\n";
  const auto r = parseRemoteBatchOutput(out, 2);
  ASSERT_EQ(r.size(), 2);
  EXPECT_EQ(r[0].code, 0);
  EXPECT_EQ(r[0].out, QByteArray("main\n"));
  EXPECT_EQ(r[1].out, QByteArray("abc\n"));
  const QString script = QString::fromUtf8(remoteGitScript(QStringLiteral("/srv/app"), {{QStringLiteral("status")}}));
  const qsizetype begin = script.indexOf(QStringLiteral("@@tabless-begin@@"));
  ASSERT_GE(begin, 0);
  EXPECT_LT(begin, script.indexOf(QStringLiteral("git 'status'")));
  EXPECT_GT(begin, script.indexOf(QStringLiteral("rev-parse --is-inside-work-tree")));
}

namespace {

// fake_forge copied under the names it plays, plus its rules and its log.
struct FakeForge {
  QString dir;
  QString gh;
  QString glab;
  QString rules;
  QString log;

  explicit FakeForge(const QString& base) : dir(base + QStringLiteral("/forge")) {
    QDir().mkpath(dir);
#ifdef Q_OS_WIN
    const QString ext = QStringLiteral(".exe");
#else
    const QString ext;
#endif
    gh = dir + QStringLiteral("/gh") + ext;
    glab = dir + QStringLiteral("/glab") + ext;
    QFile::copy(QStringLiteral(FAKE_FORGE_PATH), gh);
    QFile::copy(QStringLiteral(FAKE_FORGE_PATH), glab);
    QFile::setPermissions(gh, QFile::permissions(gh) | QFileDevice::ExeOwner);
    QFile::setPermissions(glab, QFile::permissions(glab) | QFileDevice::ExeOwner);
    rules = dir + QStringLiteral("/rules.tsv");
    log = dir + QStringLiteral("/calls.log");
  }

  // {tool, needle, exit, stdout (inline, written to a file), sleep ms}
  struct Rule {
    QString tool;
    QString needle;
    int exit = 0;
    QByteArray out;
    int sleepMs = 0;
  };

  bool setRules(const QList<Rule>& list) const {
    QByteArray text;
    for(qsizetype i = 0; i < list.size(); ++i) {
      const Rule& r = list.at(i);
      QString file = QStringLiteral("-");
      if(!r.out.isEmpty()) {
        file = dir + QStringLiteral("/out%1.json").arg(i);
        if(!writeFile(file, r.out)) {
          return false;
        }
      }
      text += (r.tool + QLatin1Char('\t') + r.needle + QLatin1Char('\t') + QString::number(r.exit) + QLatin1Char('\t') + file +
               QLatin1Char('\t') + QString::number(r.sleepMs) + QLatin1Char('\n'))
                  .toUtf8();
    }
    return writeFile(rules, text);
  }

  QStringList calls() const {
    return readAll(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  }

  qsizetype count(const QString& needle) const {
    const QStringList c = calls();
    return std::ranges::count_if(c, [&](const QString& l) {
      return l.contains(needle);
    });
  }
};

QByteArray filesPage(int from, int to, const char* key) {
  QByteArray out = "[";
  for(int i = from; i < to; ++i) {
    if(i > from) {
      out += ',';
    }
    out += QByteArray("{\"") + key + "\":\"src/f" + QByteArray::number(i) + ".cpp\"}";
  }
  return out + "]";
}

}  // namespace

TEST(CodeOwnersPages, GhFilesEveryPage) {
  const QByteArray raw = filesPage(0, 100, "filename") + "\n" + R"([{"filename":"docs/new.md","previous_filename":"docs/old.md"}])";
  bool ok = false;
  const QStringList files = parseGhPrFiles(raw, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(files.size(), 102);
  EXPECT_TRUE(files.contains(QStringLiteral("docs/old.md")));
  EXPECT_TRUE(files.contains(QStringLiteral("docs/new.md")));
  parseGhPrFiles("[{\"filename\":\"a\"}]garbage", &ok);
  EXPECT_FALSE(ok);
  parseGhPrFiles("", &ok);
  EXPECT_FALSE(ok);
  EXPECT_FALSE(ghOwnersJsonFields().contains(QStringLiteral("files")));
}

TEST(CodeOwnersPages, GlabDiffsEveryPage) {
  QByteArray raw = "[";
  for(int i = 0; i < 100; ++i) {
    raw += (i ? "," : "") + QByteArray(R"({"old_path":"a)") + QByteArray::number(i) + R"(","new_path":"a)" + QByteArray::number(i) + "\"}";
  }
  raw += R"(][{"old_path":"z \"q\" ]","new_path":"z \"q\" ]"}])";
  bool ok = false;
  const QStringList files = parseGlabChangedFiles(raw, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(files.size(), 101);
  EXPECT_EQ(files.last(), QStringLiteral("z \"q\" ]"));
}

// A real repository behind the fake ssh, with an origin, and gh/glab played
// by fake_forge.
class FakeForgeServer : public FakeSshServer {
 protected:
  std::unique_ptr<FakeForge> forge;
  std::unique_ptr<EnvGuard> e1, e2, e3, e4;

  void SetUp() override {
    FakeSshServer::SetUp();
    if(IsSkipped()) {
      return;
    }
    forge = std::make_unique<FakeForge>(dir.path());
    e1 = std::make_unique<EnvGuard>("FAKE_SSH_MODE", "sh");
    e2 = std::make_unique<EnvGuard>("FAKE_SSH_HOME", dir.path().toLocal8Bit());
    e3 = std::make_unique<EnvGuard>("FAKE_FORGE_RULES", forge->rules.toLocal8Bit());
    e4 = std::make_unique<EnvGuard>("FAKE_FORGE_LOG", forge->log.toLocal8Bit());
  }

  void setOrigin(const QString& url) {
    ASSERT_EQ(runGit({"remote", "add", "origin", url}), 0);
  }

  // A watcher on the remote repo; the reports it emits land in `reports`.
  std::unique_ptr<GitWatcher> watch(QList<QVariantMap>* reports) {
    auto w = std::make_unique<GitWatcher>();
    w->setSshProgram(QStringLiteral(FAKE_SSH_PATH));
    w->setForgePrograms(forge->gh, forge->glab);
    QObject::connect(w.get(), &GitWatcher::codeOwnersUpdated, [reports](const QString&, const QString&, const QVariantMap& r) {
      reports->append(r);
    });
    w->setWatchedRepos({QStringLiteral("me@devbox:") + repo});
    return w;
  }
};

TEST_F(FakeForgeServer, GhOwnersReadEveryPageOfTheFiles) {
  setOrigin(QStringLiteral("https://github.com/team/app.git"));
  ASSERT_TRUE(forge->setRules({
      {"gh", "--json state,", 0, R"({"state":"OPEN","number":12,"url":"https://github.com/team/app/pull/12","title":"t",
          "author":{"login":"me"},"baseRefName":"main"})"},
      {"gh", "--json baseRefName", 0, R"({"author":{"login":"me"},"baseRefName":"main","latestReviews":[],"reviewRequests":[]})"},
      {"gh", "api user", 0, "me\n"},
      {"gh", "pulls/12/files", 0, filesPage(0, 100, "filename") + filesPage(100, 130, "filename")},
  }));
  QList<QVariantMap> reports;
  auto w = watch(&reports);
  ASSERT_TRUE(waitUntil(
      [&] {
        return !reports.isEmpty();
      },
      60000));
  EXPECT_EQ(reports.last().value(QStringLiteral("fileCount")).toInt(), 130);
  EXPECT_EQ(reports.last().value(QStringLiteral("toPing")).toStringList(), (QStringList{QStringLiteral("@lead")}));
  EXPECT_EQ(forge->count(QStringLiteral("api --paginate repos/team/app/pulls/12/files?per_page=100 --hostname github.com")), 1)
      << forge->calls().join(QLatin1Char('\n')).toStdString();
  EXPECT_EQ(forge->count(QStringLiteral("glab ")), 0);
}

TEST_F(FakeForgeServer, NoOwnersBeforeTheLoginAndTheLoginIsAskedAgain) {
  setOrigin(QStringLiteral("https://github.com/team/app.git"));
  const QList<FakeForge::Rule> base = {
      {"gh", "--json state,", 0, R"({"state":"OPEN","number":12,"url":"https://github.com/team/app/pull/12","author":{"login":"me"},
          "baseRefName":"main"})"},
      {"gh", "--json baseRefName", 0, R"({"author":{"login":"me"},"baseRefName":"main"})"},
      {"gh", "pulls/12/files", 0, R"([{"filename":"a.cpp"}])"},
  };
  QList<FakeForge::Rule> offline = base;
  offline.append({"gh", "api user", 1, {}});
  ASSERT_TRUE(forge->setRules(offline));
  QList<QVariantMap> reports;
  auto w = watch(&reports);
  ASSERT_TRUE(waitUntil(
      [&] {
        return forge->count(QStringLiteral("api user")) == 1 && forge->count(QStringLiteral("--json state,")) == 1;
      },
      60000));
  waitUntil(
      [] {
        return false;
      },
      1500);
  // Not knowing who "me" is, the PR is not taken for mine.
  EXPECT_EQ(forge->count(QStringLiteral("--json baseRefName")), 0) << forge->calls().join(QLatin1Char('\n')).toStdString();
  EXPECT_TRUE(reports.isEmpty());

  // The next PR fetch asks again; once the login is in, the owners follow.
  QList<FakeForge::Rule> online = base;
  online.append({"gh", "api user", 0, "me\n"});
  ASSERT_TRUE(forge->setRules(online));
  ASSERT_EQ(runGit({"checkout", "-q", "-b", "feature/APP-9-next"}), 0);
  w->setRemotePollMs(1000);
  ASSERT_TRUE(waitUntil(
      [&] {
        return !reports.isEmpty();
      },
      60000))
      << forge->calls().join(QLatin1Char('\n')).toStdString();
  EXPECT_EQ(forge->count(QStringLiteral("api user")), 2);
}

TEST_F(FakeForgeServer, SelfHostedGitlabByAnyNameIsAskedOnceAndPaged) {
  setOrigin(QStringLiteral("https://code.corp/team/app.git"));
  QByteArray page1 = "[";
  for(int i = 0; i < 100; ++i) {
    page1 +=
        (i ? "," : "") + QByteArray(R"({"old_path":"f)") + QByteArray::number(i) + R"(","new_path":"f)" + QByteArray::number(i) + "\"}";
  }
  page1 += "]";
  ASSERT_TRUE(forge->setRules({
      {"gh", "auth status --hostname code.corp", 1, {}},
      {"glab", "auth status --hostname code.corp", 0, {}},
      {"glab", "mr view", 0, R"({"state":"opened","iid":5,"project_id":42,"web_url":"https://code.corp/team/app/-/merge_requests/5",
          "author":{"username":"me"},"target_branch":"main"})"},
      {"glab", "api user", 0, R"({"username":"me"})"},
      {"glab", "merge_requests/5/diffs", 0, page1 + "\n" + R"([{"old_path":"last.md","new_path":"last.md"}])"},
      {"glab", "merge_requests/5/approvals", 0, R"({"approved_by":[]})"},
  }));
  QList<QVariantMap> reports;
  auto w = watch(&reports);
  ASSERT_TRUE(waitUntil(
      [&] {
        return !reports.isEmpty();
      },
      60000))
      << forge->calls().join(QLatin1Char('\n')).toStdString();
  EXPECT_EQ(reports.last().value(QStringLiteral("fileCount")).toInt(), 101);
  EXPECT_EQ(reports.last().value(QStringLiteral("flavor")).toString(), QStringLiteral("gitlab"));
  EXPECT_EQ(forge->count(QStringLiteral("api --paginate projects/42/merge_requests/5/diffs?per_page=100 --hostname code.corp")), 1);
  EXPECT_EQ(forge->count(QStringLiteral("gh pr view")), 0);
  EXPECT_EQ(forge->count(QStringLiteral("gh auth status")), 1);
  EXPECT_EQ(forge->count(QStringLiteral("glab auth status")), 1);
}

TEST_F(FakeForgeServer, AFailedApprovalsCallIsNotNobodyApproved) {
  setOrigin(QStringLiteral("https://gitlab.com/team/app.git"));
  ASSERT_TRUE(forge->setRules({
      {"glab", "mr view", 0, R"({"state":"opened","iid":5,"project_id":42,"author":{"username":"me"},"target_branch":"main"})"},
      {"glab", "api user", 0, R"({"username":"me"})"},
      {"glab", "merge_requests/5/diffs", 0, R"([{"old_path":"a","new_path":"a"}])"},
      {"glab", "merge_requests/5/approvals", 1, {}},
  }));
  QList<QVariantMap> reports;
  auto w = watch(&reports);
  ASSERT_TRUE(waitUntil(
      [&] {
        return forge->count(QStringLiteral("approvals")) == 1;
      },
      60000))
      << forge->calls().join(QLatin1Char('\n')).toStdString();
  waitUntil(
      [] {
        return false;
      },
      1000);
  EXPECT_TRUE(reports.isEmpty()) << "a report naming people who may have approved";
}

TEST_F(FakeSsh, AHungOrMissingForgeToolStillAnswers) {
  FakeForge forge(dir.path());
  ASSERT_TRUE(forge.setRules({{"gh", "pr view", 0, R"({"state":"OPEN","number":1})", 10000}, {"gh", "api user", 0, "me\n", 10000}}));
  EnvGuard e1("FAKE_FORGE_RULES", forge.rules.toLocal8Bit());
  GitWatcher w;
  w.setForgePrograms(forge.gh, QString());
  w.setForgeTimeoutMs(500);
  int answers = 0;
  QObject::connect(&w, &GitWatcher::prInfoUpdated, [&](const QString&, const QString&, const QVariantMap&) {
    ++answers;
  });
  QElapsedTimer t;
  t.start();
  w.requestPrFetch(dir.path(), QStringLiteral("feature/a"));
  ASSERT_TRUE(waitUntil([&] {
    return answers == 1;
  }));
  EXPECT_LT(t.elapsed(), 8000);  // killed, not waited out
  // A tool that does not start answers too (and frees its fetch).
  w.setForgePrograms(dir.filePath(QStringLiteral("no-such-gh.exe")), QString());
  w.requestPrFetch(dir.path(), QStringLiteral("feature/b"));
  ASSERT_TRUE(waitUntil([&] {
    return answers == 2;
  }));
}

TEST_F(FakeSsh, ARemovedRepoAddedAgainIsPolledAgain) {
  EnvGuard e1("FAKE_SSH_MODE", "canned");
  EnvGuard e2("FAKE_SSH_STDERR", "ssh: Could not resolve hostname devbox\n");
  EnvGuard e3("FAKE_SSH_EXIT", "255");
  EnvGuard e4("FAKE_SSH_STDOUT", "");
  EnvGuard e5("FAKE_SSH_LOG", log.toLocal8Bit());
  GitWatcher w;
  w.setSshProgram(QStringLiteral(FAKE_SSH_PATH));
  QString error;
  QObject::connect(&w, &GitWatcher::repoStateUpdated, [&](const QString&, const QVariantMap& st) {
    error = st.value(QStringLiteral("error")).toString();
  });
  const QString entry = QStringLiteral("devbox:/srv/app");
  w.setWatchedRepos({entry});  // its poll is on its way…
  w.setWatchedRepos({});       // …and killed with the repo
  w.setWatchedRepos({entry});
  ASSERT_TRUE(waitUntil([&] {
    return !error.isEmpty();
  }));
  EXPECT_EQ(error, QStringLiteral("unreachable"));
}

TEST_F(FakeSshServer, ABranchInAnUnwatchedFolderIsNotCancelledByAnotherRequest) {
  GitWatcher w;
  QString created;
  QObject::connect(&w, &GitWatcher::branchCreated, [&](const QString& path, const QString& name, bool ok, const QString&) {
    if(ok && path == repo) {
      created = name;
    }
  });
  QString err;
  ASSERT_TRUE(w.createBranch(repo, QStringLiteral("feature/APP-11-x"), &err));
  // Another request answering at once (an empty name fails right away).
  EXPECT_FALSE(w.createBranch(dir.filePath(QStringLiteral("elsewhere")), QString(), &err));
  ASSERT_TRUE(waitUntil([&] {
    return !created.isEmpty();
  }));
  EXPECT_EQ(created, QStringLiteral("feature/APP-11-x"));
}

TEST_F(FakeSsh, DroppingARunnerDoesNotWaitForItsProcess) {
  FakeForge forge(dir.path());
  ASSERT_TRUE(forge.setRules({{"*", "log", 0, {}, 5000}}));
  EnvGuard e1("FAKE_FORGE_RULES", forge.rules.toLocal8Bit());
  EnvGuard e2("FAKE_FORGE_LOG", forge.log.toLocal8Bit());
  bool called = false;
  auto* runner = new LocalGitRunner(forge.gh, dir.path());
  runner->run({QStringLiteral("log")}, 30000, [&](const GitResult&) {
    called = true;
  });
  ASSERT_TRUE(waitUntil([&] {
    return forge.count(QStringLiteral("gh log")) == 1;
  }));
  QElapsedTimer t;
  t.start();
  delete runner;
  EXPECT_LT(t.elapsed(), 200);
  waitUntil(
      [] {
        return false;
      },
      300);
  EXPECT_FALSE(called);
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
