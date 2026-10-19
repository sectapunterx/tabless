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

#include <functional>

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

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
