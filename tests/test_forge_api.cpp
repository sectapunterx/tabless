// The CLI-free forge client (dogfood 2026-10-20): host matching, URL and
// answer parsing, and every call against a fake GitLab / GitHub on loopback —
// the auth headers it sends, every page it walks, CODEOWNERS found or not or
// unreadable, and that it never sends anything but a GET.

#include "FakeHttpServer.h"

#include "git/ForgeApi.h"
#include "net/FeatureGate.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

using namespace heap::git;
using heap::testing::FakeHttpServer;

namespace {

ForgeAuth gitlabAuth(const FakeHttpServer& server) {
  ForgeAuth a;
  a.glab = true;
  a.providerId = QStringLiteral("gitlab");
  a.host = QStringLiteral("gitlab.company.ru");
  a.apiBase = server.base() + QStringLiteral("/api/v4");
  a.headers = {{"PRIVATE-TOKEN", "glpat-secret"}, {"Accept", "application/json"}};
  return a;
}

ForgeAuth githubAuth(const FakeHttpServer& server) {
  ForgeAuth a;
  a.providerId = QStringLiteral("github");
  a.host = QStringLiteral("ghe.corp");
  a.apiBase = forgeApiBase(false, server.base());  // Enterprise: /api/v3
  a.headers = {{"Authorization", "token ghp-secret"}, {"Accept", "application/vnd.github+json"}};
  return a;
}

FakeHttpServer::Response ok(const QByteArray& body, const QList<QPair<QByteArray, QByteArray>>& headers = {}) {
  return {200, body, headers};
}

void expectOnlyGets(const FakeHttpServer& server) {
  ASSERT_FALSE(server.requests().isEmpty());
  for(const auto& r : server.requests()) {
    EXPECT_EQ(r.method, QByteArray("GET")) << r.key().toStdString();
  }
}

QByteArray diffsPage(int from, int to) {
  QByteArray out = "[";
  for(int i = from; i < to; ++i) {
    out += (i > from ? "," : "") + QByteArray(R"({"old_path":"src/f)") + QByteArray::number(i) + R"(.cpp","new_path":"src/f)" +
           QByteArray::number(i) + R"(.cpp"})";
  }
  return out + "]";
}

}  // namespace

// ---- hosts ----

TEST(ForgeHost, HostsAndApiBases) {
  EXPECT_EQ(forgeHostOf(QStringLiteral("https://GitLab.Company.ru:8443/sub/")), QStringLiteral("gitlab.company.ru"));
  EXPECT_EQ(forgeHostOf(QStringLiteral("gitlab.company.ru")), QStringLiteral("gitlab.company.ru"));
  EXPECT_EQ(forgeHostOf(QStringLiteral("https://api.github.com")), QStringLiteral("github.com"));
  EXPECT_EQ(forgeApiBase(true, QStringLiteral("https://gitlab.company.ru/")), QStringLiteral("https://gitlab.company.ru/api/v4"));
  EXPECT_EQ(forgeApiBase(true, QStringLiteral("gitlab.company.ru/gitlab")), QStringLiteral("https://gitlab.company.ru/gitlab/api/v4"));
  EXPECT_EQ(forgeApiBase(true, QString()), QStringLiteral("https://gitlab.com/api/v4"));
  EXPECT_EQ(forgeApiBase(false, QStringLiteral("https://github.com")), QStringLiteral("https://api.github.com"));
  EXPECT_EQ(forgeApiBase(false, QStringLiteral("https://ghe.corp")), QStringLiteral("https://ghe.corp/api/v3"));
}

TEST(ForgeHost, CandidatesByHostThenEveryOneForAnAlias) {
  ForgeAuth lab;
  lab.glab = true;
  lab.host = QStringLiteral("gitlab.company.ru");
  lab.apiBase = QStringLiteral("https://gitlab.company.ru/api/v4");
  ForgeAuth hub;
  hub.host = QStringLiteral("github.com");
  hub.apiBase = QStringLiteral("https://api.github.com");
  const QList<ForgeAuth> both{hub, lab};
  EXPECT_EQ(forgeAuthCandidates(both, QStringLiteral("gitlab.company.ru")), (QList<int>{1}));
  EXPECT_EQ(forgeAuthCandidates(both, QStringLiteral("GITHUB.com")), (QList<int>{0}));
  // `git@gitlab:group/app.git` with `gitlab` an ssh alias: both, GitLab first.
  EXPECT_EQ(forgeAuthCandidates(both, QStringLiteral("gitlab")), (QList<int>{1, 0}));
  EXPECT_EQ(forgeAuthCandidates(both, QStringLiteral("devbox-git")), (QList<int>{0, 1}));
  // gitlab.com is nobody's alias: a token for another host is not tried there.
  EXPECT_TRUE(forgeAuthCandidates({lab}, QStringLiteral("gitlab.com")).isEmpty());
  EXPECT_TRUE(forgeAuthCandidates({}, QStringLiteral("gitlab")).isEmpty());
}

TEST(ForgeHost, PrUrls) {
  ForgePrRef r = parseForgePrUrl(QStringLiteral("https://gitlab.company.ru/group/sub/app/-/merge_requests/7"));
  EXPECT_TRUE(r.valid());
  EXPECT_TRUE(r.glab);
  EXPECT_EQ(r.host, QStringLiteral("gitlab.company.ru"));
  EXPECT_EQ(r.project, QStringLiteral("group/sub/app"));
  EXPECT_EQ(r.number, 7);
  r = parseForgePrUrl(QStringLiteral("https://github.com/o/r/pull/12/files"));
  EXPECT_TRUE(r.valid());
  EXPECT_FALSE(r.glab);
  EXPECT_EQ(r.project, QStringLiteral("o/r"));
  EXPECT_EQ(r.number, 12);
  EXPECT_FALSE(parseForgePrUrl(QStringLiteral("https://github.com/o/r/issues/12")).valid());
  EXPECT_FALSE(parseForgePrUrl(QString()).valid());
}

// ---- answers ----

TEST(ForgeParse, GithubPullStatesTeamsAndReviews) {
  PrInfo pr = parseGithubRestPull(QJsonDocument::fromJson(R"({"number":12,"state":"closed","merged_at":"2026-10-20T10:00:00Z",
      "html_url":"https://github.com/o/r/pull/12","title":"t","base":{"ref":"main"},"user":{"login":"me"}})")
                                      .object());
  EXPECT_EQ(pr.state, QStringLiteral("merged"));
  pr = parseGithubRestPull(QJsonDocument::fromJson(R"({"number":12,"state":"open","draft":true,"base":{"ref":"dev"},
      "user":{"login":"me"},"requested_reviewers":[{"login":"bob"}],"requested_teams":[{"slug":"core"}],"mergeable":false})")
                               .object());
  EXPECT_EQ(pr.state, QStringLiteral("open"));
  EXPECT_TRUE(pr.draft);
  EXPECT_EQ(pr.baseRef, QStringLiteral("dev"));
  EXPECT_EQ(pr.reviewRequests, (QStringList{QStringLiteral("bob"), QStringLiteral("core")}));
  EXPECT_EQ(pr.mergeable, QStringLiteral("CONFLICTING"));

  // The latest say counts; a comment says nothing.
  QString decision;
  const QStringList approved =
      parseGithubApprovedBy(R"([{"user":{"login":"ann"},"state":"CHANGES_REQUESTED"},
      {"user":{"login":"bob"},"state":"APPROVED"}])"
                            "\n"
                            R"([{"user":{"login":"ann"},"state":"APPROVED"},{"user":{"login":"bob"},"state":"COMMENTED"}])",
                            &decision);
  EXPECT_EQ(approved, (QStringList{QStringLiteral("ann"), QStringLiteral("bob")}));
  EXPECT_EQ(decision, QStringLiteral("APPROVED"));
  parseGithubApprovedBy(R"([{"user":{"login":"ann"},"state":"CHANGES_REQUESTED"}])", &decision);
  EXPECT_EQ(decision, QStringLiteral("CHANGES_REQUESTED"));
}

TEST(ForgeParse, LinkHeaderAndLogin) {
  EXPECT_EQ(nextLinkUrl(R"(<https://h/api/v4/x?page=2>; rel="next", <https://h/api/v4/x?page=9>; rel="last")"),
            QStringLiteral("https://h/api/v4/x?page=2"));
  EXPECT_TRUE(nextLinkUrl(R"(<https://h/x?page=1>; rel="first")").isEmpty());
  EXPECT_EQ(parseForgeLogin(R"({"username":"fin"})"), QStringLiteral("fin"));
  EXPECT_EQ(parseForgeLogin(R"({"login":"fin"})"), QStringLiteral("fin"));
  EXPECT_EQ(ownersReasonForStatus(403), QStringLiteral("denied"));
  EXPECT_EQ(ownersReasonForStatus(0), QStringLiteral("unreachable"));
  EXPECT_TRUE(ownersReasonForStatus(200).isEmpty());
}

// ---- against a fake forge ----

TEST(ForgeApiGitlab, MrForABranchItsDiffsEveryPageApprovalsAndCodeOwners) {
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray p = "/api/v4/projects/group%2Fapp";
  server.route("GET " + p + "/merge_requests",
               ok(R"([{"iid":7,"state":"opened","web_url":"https://gitlab.company.ru/group/app/-/merge_requests/7","title":"Fix",
                      "target_branch":"main","project_id":42,"author":{"username":"fin"},"reviewers":[{"username":"bob"}]}])"));
  server.route("GET " + p + "/merge_requests/7",
               ok(R"({"iid":7,"state":"opened","web_url":"https://gitlab.company.ru/group/app/-/merge_requests/7","title":"Fix",
                     "target_branch":"main","project_id":42,"author":{"username":"fin"},"reviewers":[{"username":"bob"}],
                     "head_pipeline":{"status":"success"},"detailed_merge_status":"not_approved"})"));
  // The owners' reads go by the MR's project_id. Two pages of diffs, the
  // second named by the Link header.
  const QByteArray id = "/api/v4/projects/42";
  const QByteArray next = server.base().toUtf8() + id + "/merge_requests/7/diffs?per_page=100&page=2";
  server.route("GET " + id + "/merge_requests/7/diffs?per_page=100", ok(diffsPage(0, 100), {{"Link", "<" + next + ">; rel=\"next\""}}));
  server.route("GET " + id + "/merge_requests/7/diffs?per_page=100&page=2", ok(R"([{"old_path":"docs/a.md","new_path":"docs/a.md"}])"));
  server.route("GET " + id + "/merge_requests/7/approvals", ok(R"({"approved_by":[{"user":{"username":"ann"}}]})"));
  // CODEOWNERS is not at the root: GitLab's next place, docs/.
  server.route("GET " + id + "/repository/files/docs%2FCODEOWNERS/raw", ok("[Docs]\ndocs/ @ann @carol\n[Code]\nsrc/ @bob @dave\n"));

  ForgeApi api;
  PrInfo pr;
  int status = 0;
  bool done = false;
  api.fetchPrForBranch(auth, QStringLiteral("group/app"), QStringLiteral("feature/APP-7"), [&](const PrInfo& p2, int s) {
    pr = p2;
    status = s;
    done = true;
  });
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_EQ(status, 200);
  EXPECT_EQ(pr.state, QStringLiteral("open"));
  EXPECT_EQ(pr.number, 7);
  EXPECT_EQ(pr.checks, QStringLiteral("passing")) << "the MR itself carries the pipeline";
  const auto list = server.lastRequest("GET " + p + "/merge_requests");
  EXPECT_TRUE(list.query.contains("source_branch=feature%2FAPP-7")) << list.query.toStdString();
  EXPECT_TRUE(list.query.contains("state=opened"));
  EXPECT_EQ(list.headers.value("private-token"), QByteArray("glpat-secret"));

  QVariantMap report;
  done = false;
  api.fetchOwnersReport(auth, QStringLiteral("group/app"), pr, [&](const QVariantMap& r) {
    report = r;
    done = true;
  });
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_TRUE(report.value(QStringLiteral("found")).toBool()) << QJsonDocument::fromVariant(report).toJson().toStdString();
  EXPECT_EQ(report.value(QStringLiteral("location")).toString(), QStringLiteral("docs/CODEOWNERS"));
  EXPECT_EQ(report.value(QStringLiteral("fileCount")).toInt(), 101);
  EXPECT_EQ(report.value(QStringLiteral("flavor")).toString(), QStringLiteral("gitlab"));
  EXPECT_EQ(report.value(QStringLiteral("baseRef")).toString(), QStringLiteral("main"));
  // ann approved Docs; bob is asked; dave is still to ask.
  EXPECT_EQ(report.value(QStringLiteral("toPing")).toStringList(), (QStringList{QStringLiteral("@dave")}));
  EXPECT_EQ(server.lastRequest("GET " + id + "/repository/files/CODEOWNERS/raw").query, QByteArray("ref=main"));
  expectOnlyGets(server);
}

TEST(ForgeApiGitlab, LegacyChangesWhenThereAreNoDiffs) {
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray p = "/api/v4/projects/group%2Fapp";
  server.route("GET " + p + "/merge_requests/7/changes", ok(R"({"changes":[{"old_path":"a","new_path":"b"}]})"));
  server.route("GET " + p + "/merge_requests/7/approvals", ok(R"({"approved_by":[]})"));
  ForgeApi api;
  PrInfo pr;
  pr.number = 7;
  pr.baseRef = QStringLiteral("main");
  OwnersFacts facts;
  bool done = false;
  api.fetchOwnersFacts(auth, QStringLiteral("group/app"), pr, [&](const OwnersFacts& f, int) {
    facts = f;
    done = true;
  });
  ASSERT_TRUE(heap::testing::waitFor(done));
  ASSERT_TRUE(facts.ok);
  EXPECT_EQ(facts.files, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

TEST(ForgeApiGitlab, NoCodeOwnersVersusNoAccessToTheFiles) {
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray p = "/api/v4/projects/group%2Fapp";
  ForgeApi api;
  const auto read = [&]() {
    ForgeApi::CodeOwnersText out;
    bool done = false;
    api.fetchCodeOwnersText(auth, QStringLiteral("group/app"), QStringLiteral("release/1.0"), [&](const ForgeApi::CodeOwnersText& t) {
      out = t;
      done = true;
    });
    EXPECT_TRUE(heap::testing::waitFor(done));
    return out;
  };
  // 404 everywhere, and the branch itself is not readable either: the token
  // cannot see the repository's files.
  ForgeApi::CodeOwnersText t = read();
  EXPECT_FALSE(t.ok);
  EXPECT_EQ(t.reason, QStringLiteral("noRepoAccess"));
  // The branch is there: there really is no CODEOWNERS on it.
  server.route("GET " + p + "/repository/branches/release%2F1.0", ok(R"({"name":"release/1.0"})"));
  t = read();
  EXPECT_TRUE(t.ok);
  EXPECT_FALSE(t.found);
  // Refused outright.
  server.route("GET " + p + "/repository/files/CODEOWNERS/raw", {403, R"({"message":"403 Forbidden"})", {}});
  t = read();
  EXPECT_FALSE(t.ok);
  EXPECT_EQ(t.reason, QStringLiteral("noRepoAccess"));
  expectOnlyGets(server);
}

TEST(ForgeApiGithub, EnterprisePullFilesEveryPageReviewsAndRawCodeOwners) {
  FakeHttpServer server;
  const ForgeAuth auth = githubAuth(server);
  const QByteArray r = "/api/v3/repos/team/app";
  server.route("GET " + r + "/pulls", ok(R"([{"number":12,"state":"open","html_url":"https://ghe.corp/team/app/pull/12","title":"T",
      "base":{"ref":"main"},"user":{"login":"me"},"requested_reviewers":[{"login":"carol"}]}])"));
  server.route("GET " + r + "/pulls/12", ok(R"({"number":12,"state":"open","html_url":"https://ghe.corp/team/app/pull/12","title":"T",
      "base":{"ref":"main"},"user":{"login":"me"},"requested_reviewers":[{"login":"carol"}],"mergeable":true})"));
  server.route("GET " + r + "/pulls/12/reviews", ok(R"([{"user":{"login":"ann"},"state":"APPROVED"}])"));
  QByteArray files = "[";
  for(int i = 0; i < 100; ++i) {
    files += (i ? "," : "") + QByteArray(R"({"filename":"src/f)") + QByteArray::number(i) + R"(.cpp"})";
  }
  files += "]";
  const QByteArray next = server.base().toUtf8() + r + "/pulls/12/files?per_page=100&page=2";
  server.route("GET " + r + "/pulls/12/files?per_page=100", ok(files, {{"Link", "<" + next + ">; rel=\"next\""}}));
  server.route("GET " + r + "/pulls/12/files?per_page=100&page=2", ok(R"([{"filename":"docs/x.md"}])"));
  server.route("GET " + r + "/contents/.github/CODEOWNERS", ok("src/ @ann @carol @dan\ndocs/ @eve\n"));

  ForgeApi api;
  PrInfo pr;
  bool done = false;
  api.fetchPrForBranch(auth, QStringLiteral("team/app"), QStringLiteral("feat/x"), [&](const PrInfo& p2, int) {
    pr = p2;
    done = true;
  });
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_EQ(pr.number, 12);
  EXPECT_EQ(pr.reviewDecision, QStringLiteral("APPROVED"));
  EXPECT_EQ(pr.mergeable, QStringLiteral("MERGEABLE"));
  const auto list = server.lastRequest("GET " + r + "/pulls");
  EXPECT_TRUE(list.query.contains("head=team%3Afeat%2Fx")) << list.query.toStdString();
  EXPECT_EQ(list.headers.value("authorization"), QByteArray("token ghp-secret"));

  QVariantMap report;
  done = false;
  api.fetchOwnersReport(auth, QStringLiteral("team/app"), pr, [&](const QVariantMap& rep) {
    report = rep;
    done = true;
  });
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_TRUE(report.value(QStringLiteral("found")).toBool());
  EXPECT_EQ(report.value(QStringLiteral("fileCount")).toInt(), 101);
  // ann approved src/ (enough); docs/ still needs eve.
  EXPECT_EQ(report.value(QStringLiteral("toPing")).toStringList(), (QStringList{QStringLiteral("@eve")}));
  const auto raw = server.lastRequest("GET " + r + "/contents/.github/CODEOWNERS");
  EXPECT_EQ(raw.headers.value("accept"), QByteArray("application/vnd.github.raw"));
  EXPECT_EQ(raw.query, QByteArray("ref=main"));
  expectOnlyGets(server);
}

TEST(ForgeApiGithub, NoOpenPrFallsBackToTheLatestSoAMergeShows) {
  FakeHttpServer server;
  const ForgeAuth auth = githubAuth(server);
  const QByteArray r = "/api/v3/repos/team/app";
  server.route("GET " + r + "/pulls?head=team%3Afeat&state=open&per_page=1", ok("[]"));
  server.route("GET " + r + "/pulls?head=team%3Afeat&state=all&per_page=1",
               ok(R"([{"number":3,"state":"closed","merged_at":"2026-10-20T09:00:00Z","base":{"ref":"main"},"user":{"login":"me"}}])"));
  server.route("GET " + r + "/pulls/3", ok(R"({"number":3,"state":"closed","merged":true,"base":{"ref":"main"},"user":{"login":"me"}})"));
  ForgeApi api;
  PrInfo pr;
  bool done = false;
  api.fetchPrForBranch(auth, QStringLiteral("team/app"), QStringLiteral("feat"), [&](const PrInfo& p2, int) {
    pr = p2;
    done = true;
  });
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_EQ(pr.state, QStringLiteral("merged"));
  expectOnlyGets(server);
}

TEST(ForgeApiSafety, ATokenNeverFollowsALinkToAnotherHost) {
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  server.route("GET /api/v4/projects/g%2Fa/merge_requests/1/diffs",
               ok(R"([{"old_path":"a","new_path":"a"}])", {{"Link", R"(<https://elsewhere.example/steal?page=2>; rel="next")"}}));
  ForgeApi api;
  QByteArray pages;
  int status = 0;
  bool done = false;
  api.getAllPages(auth, QStringLiteral("/projects/g%2Fa/merge_requests/1/diffs"), [&](const ForgeApi::Reply& last, const QByteArray& all) {
    pages = all;
    status = last.status;
    done = true;
  });
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_EQ(api.requestCount(), 1) << "the next page lives on another host: not asked";
  EXPECT_TRUE(pages.contains("\"a\""));
  EXPECT_EQ(status, 0);
}

// ---- politeness: a failing server is not hammered (incident 2026-10-20) ----
//
// A company GitLab answered `…/merge_requests/N/diffs` with 500 and Tabless
// asked again every minute, for every MR — the devops team read it as brute
// force. These pin how many requests a failing server can get at most.

namespace {

struct ManualClock {
  qint64 now = 1'800'000'000'000;

  // ForgeApi's gate and FeatureGate (the per-feature pause) on one clock.
  ManualClock() {
    ForgeApi::resetPolitenessForTests();
    ForgeApi::setClockForTests([this]() {
      return now;
    });
    heap::net::FeatureGate::instance().resetForTests();
    heap::net::FeatureGate::setClockForTests([this]() {
      return QDateTime::fromMSecsSinceEpoch(now);
    });
  }

  ~ManualClock() {
    ForgeApi::setClockForTests({});
    ForgeApi::resetPolitenessForTests();
    heap::net::FeatureGate::setClockForTests({});
    heap::net::FeatureGate::instance().resetForTests();
  }

  void advanceMinutes(int m) {
    now += static_cast<qint64>(m) * 60'000;
  }
};

int countSeen(const FakeHttpServer& server, const QByteArray& key) {
  int n = 0;
  for(const auto& r : server.requests()) {
    if(r.key() == key) {
      ++n;
    }
  }
  return n;
}

OwnersFacts factsOnce(ForgeApi& api, const ForgeAuth& auth, const PrInfo& pr, int* status = nullptr) {
  OwnersFacts facts;
  bool done = false;
  api.fetchOwnersFacts(auth, QStringLiteral("tel/bts/app"), pr, [&](const OwnersFacts& f, int s) {
    facts = f;
    if(status != nullptr) {
      *status = s;
    }
    done = true;
  });
  EXPECT_TRUE(heap::testing::waitFor(done));
  return facts;
}

}  // namespace

TEST(ForgeApiPoliteness, BackoffGrowsAndHonoursRetryAfter) {
  EXPECT_EQ(ForgeApi::backoffMs(1, -1), 5 * 60'000);
  EXPECT_EQ(ForgeApi::backoffMs(2, -1), 10 * 60'000);
  EXPECT_EQ(ForgeApi::backoffMs(3, -1), 20 * 60'000);
  EXPECT_EQ(ForgeApi::backoffMs(30, -1), ForgeApi::kMaxBackoffMs);
  EXPECT_EQ(ForgeApi::backoffMs(1, 3600), 3600'000) << "the server asked for an hour";
  EXPECT_EQ(ForgeApi::backoffMs(1, 10), 5 * 60'000) << "never sooner than our own back-off";
  EXPECT_TRUE(ForgeApi::isFailure(500));
  EXPECT_TRUE(ForgeApi::isFailure(503));
  EXPECT_TRUE(ForgeApi::isFailure(429));
  EXPECT_TRUE(ForgeApi::isFailure(0));
  EXPECT_FALSE(ForgeApi::isFailure(404));
  EXPECT_FALSE(ForgeApi::isFailure(200));
}

TEST(ForgeApiPoliteness, AFailingDiffsIsAskedAHandfulOfTimesInADayNotEveryMinute) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray mr = "/api/v4/projects/tel%2Fbts%2Fapp/merge_requests/2299";
  server.route("GET " + mr + "/diffs", {500, R"({"message":"500 Internal Server Error"})", {}});
  server.route("GET " + mr + "/changes", {500, R"({"message":"500 Internal Server Error"})", {}});
  ForgeApi api;
  PrInfo pr;
  pr.number = 2299;
  pr.baseRef = QStringLiteral("master");
  pr.headSha = QStringLiteral("abc123");
  // What used to happen: asked once a minute, here for a whole working day.
  for(int minute = 0; minute < 8 * 60; ++minute) {
    int status = -1;
    const OwnersFacts f = factsOnce(api, auth, pr, &status);
    EXPECT_FALSE(f.ok);
    EXPECT_EQ(ownersReasonForStatus(status), QStringLiteral("serverError"));
    clock.advanceMinutes(1);
  }
  const int diffs = countSeen(server, "GET " + mr + "/diffs");
  const int changes = countSeen(server, "GET " + mr + "/changes");
  // 5, 10, 20, 40, 80, 160 min back-offs: at most 7 tries in 8 h, and the
  // host pause cuts it further. Before the fix this was 480 + 480.
  EXPECT_GE(diffs, 1);
  EXPECT_LE(diffs, 7);
  EXPECT_LE(changes, 7);
  EXPECT_LE(server.requests().size(), 14);
}

TEST(ForgeApiPoliteness, DiffsFailingWithAServerErrorFallsBackToChanges) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray mr = "/api/v4/projects/tel%2Fbts%2Fapp/merge_requests/7";
  server.route("GET " + mr + "/diffs", {500, R"({"message":"500 Internal Server Error"})", {}});
  server.route("GET " + mr + "/changes", ok(R"({"changes":[{"old_path":"src/a.cpp","new_path":"src/a.cpp"}]})"));
  server.route("GET " + mr + "/approvals", ok(R"({"approved_by":[]})"));
  ForgeApi api;
  PrInfo pr;
  pr.number = 7;
  pr.headSha = QStringLiteral("abc");
  const OwnersFacts f = factsOnce(api, auth, pr);
  ASSERT_TRUE(f.ok);
  EXPECT_EQ(f.files, (QStringList{QStringLiteral("src/a.cpp")}));
}

TEST(ForgeApiPoliteness, ChangedFilesAreReadOncePerHeadCommit) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray mr = "/api/v4/projects/tel%2Fbts%2Fapp/merge_requests/7";
  server.route("GET " + mr + "/diffs", ok(diffsPage(0, 3)));
  server.route("GET " + mr + "/approvals", ok(R"({"approved_by":[]})"));
  ForgeApi api;
  PrInfo pr;
  pr.number = 7;
  pr.headSha = QStringLiteral("aaa");
  for(int i = 0; i < 5; ++i) {
    ASSERT_TRUE(factsOnce(api, auth, pr).ok);
  }
  EXPECT_EQ(countSeen(server, "GET " + mr + "/diffs"), 1) << "same head: the files are known";
  EXPECT_EQ(countSeen(server, "GET " + mr + "/approvals"), 5) << "approvals do change without a commit";
  pr.headSha = QStringLiteral("bbb");
  ASSERT_TRUE(factsOnce(api, auth, pr).ok);
  EXPECT_EQ(countSeen(server, "GET " + mr + "/diffs"), 2) << "a new commit: read again";
}

TEST(ForgeApiPoliteness, AHostThatKeepsFailingIsLeftAloneForAWhile) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray p = "/api/v4/projects/tel%2Fbts%2Fapp/merge_requests/";
  for(int i = 1; i <= ForgeApi::kHostTripFailures; ++i) {
    server.route("GET " + p + QByteArray::number(i), {502, "bad gateway", {}});
  }
  server.route("GET " + p + "99", ok(R"({"iid":99,"state":"opened"})"));
  ForgeApi api;
  const auto ask = [&](int n) {
    ForgeApi::Reply out;
    bool done = false;
    api.get(auth, QStringLiteral("/projects/tel%2Fbts%2Fapp/merge_requests/%1").arg(n), [&](const ForgeApi::Reply& r) {
      out = r;
      done = true;
    });
    EXPECT_TRUE(heap::testing::waitFor(done));
    return out;
  };
  for(int i = 1; i <= ForgeApi::kHostTripFailures; ++i) {
    EXPECT_EQ(ask(i).status, 502);
  }
  const auto sent = server.requests().size();
  // Tripped: even a request that would succeed is not sent.
  const ForgeApi::Reply held = ask(99);
  EXPECT_TRUE(held.held);
  EXPECT_FALSE(held.ok());
  EXPECT_EQ(server.requests().size(), sent);
  clock.advanceMinutes(16);
  const ForgeApi::Reply after = ask(99);
  EXPECT_FALSE(after.held);
  EXPECT_EQ(after.status, 200);
}

TEST(ForgeApiPoliteness, TheSameRequestInFlightIsSentOnce) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  server.route("GET /api/v4/user", ok(R"({"username":"fin"})"));
  ForgeApi a;
  ForgeApi b;  // GitWatcher and AppController each have one
  int answers = 0;
  bool all = false;
  for(int i = 0; i < 3; ++i) {
    a.get(auth, QStringLiteral("/user"), [&](const ForgeApi::Reply& r) {
      EXPECT_TRUE(r.ok());
      all = ++answers == 6;
    });
    b.get(auth, QStringLiteral("/user"), [&](const ForgeApi::Reply& r) {
      EXPECT_TRUE(r.ok());
      all = ++answers == 6;
    });
  }
  ASSERT_TRUE(heap::testing::waitFor(all));
  EXPECT_EQ(countSeen(server, "GET /api/v4/user"), 1);
  EXPECT_EQ(server.lastRequest("GET /api/v4/user").headers.value("user-agent").left(8), QByteArray("Tabless/"));
}

// ---- CODEOWNERS on a self-hosted GitLab (dogfood 2026-10-20, item 18) ----

namespace {

QVariantMap reportOnce(ForgeApi& api, const ForgeAuth& auth, const PrInfo& pr, const ForgeApi::LocalOwners& local = {}) {
  QVariantMap out;
  bool done = false;
  api.fetchOwnersReport(
      auth,
      QStringLiteral("tel/bts/l2-ref-model"),
      pr,
      [&](const QVariantMap& r) {
        out = r;
        done = true;
      },
      local);
  EXPECT_TRUE(heap::testing::waitFor(done));
  return out;
}

PrInfo nestedMr() {
  PrInfo pr;
  pr.number = 2299;
  pr.state = QStringLiteral("open");
  pr.baseRef = QStringLiteral("release/1.0");
  pr.projectId = 4242;
  pr.headSha = QStringLiteral("abc123");
  pr.author = QStringLiteral("fin");
  return pr;
}

}  // namespace

TEST(ForgeApiCodeOwners, NestedGroupGoesByTheMrsNumericIdAndEncodesEverySlash) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray p = "/api/v4/projects/4242";
  server.route("GET " + p + "/merge_requests/2299/diffs", ok(R"([{"old_path":"src/l2/mac.cpp","new_path":"src/l2/mac.cpp"}])"));
  server.route("GET " + p + "/merge_requests/2299/approvals", ok(R"({"approved_by":[]})"));
  // Only in GitLab's third place, .gitlab/.
  server.route("GET " + p + "/repository/files/.gitlab%2FCODEOWNERS/raw", ok("[L2][2] @tel/bts/l2\n/src/\n"));
  ForgeApi api;
  const QVariantMap rep = reportOnce(api, auth, nestedMr());
  ASSERT_TRUE(rep.value(QStringLiteral("found")).toBool()) << server.seen().join('\n').toStdString();
  EXPECT_EQ(rep.value(QStringLiteral("location")).toString(), QStringLiteral(".gitlab/CODEOWNERS"));
  EXPECT_EQ(rep.value(QStringLiteral("toPing")).toStringList(), (QStringList{QStringLiteral("@tel/bts/l2")}));
  // The raw request line: %2F stays %2F in the path, and the slash in the
  // ref is encoded in the query too — QUrl decodes neither on the way out.
  const auto file = server.lastRequest("GET " + p + "/repository/files/.gitlab%2FCODEOWNERS/raw");
  EXPECT_EQ(file.query, QByteArray("ref=release%2F1.0"));
  for(const auto& r : server.requests()) {
    EXPECT_FALSE(r.path.contains("tel/bts")) << r.path.toStdString();
  }
  expectOnlyGets(server);
}

TEST(ForgeApiCodeOwners, WithoutAnIdTheNestedPathStaysOnePercentEncodedSegment) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray p = "/api/v4/projects/tel%2Fbts%2Fl2-ref-model";
  server.route("GET " + p + "/merge_requests/2299/diffs", ok(R"([{"old_path":"a","new_path":"a"}])"));
  server.route("GET " + p + "/merge_requests/2299/approvals", ok(R"({"approved_by":[]})"));
  server.route("GET " + p + "/repository/files/CODEOWNERS/raw", ok("* @lead\n"));
  ForgeApi api;
  PrInfo pr = nestedMr();
  pr.projectId = 0;
  const QVariantMap rep = reportOnce(api, auth, pr);
  EXPECT_TRUE(rep.value(QStringLiteral("found")).toBool()) << server.seen().join('\n').toStdString();
  EXPECT_EQ(countSeen(server, "GET " + p + "/repository/files/CODEOWNERS/raw"), 1);
}

TEST(ForgeApiCodeOwners, NoFileListFromTheServerSaysSoAndIsNotAskedAgainUntilANewCommit) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray mr = "/api/v4/projects/4242/merge_requests/2299";
  server.route("GET " + mr + "/diffs", {500, R"({"message":"500 Internal Server Error"})", {}});
  server.route("GET " + mr + "/changes", {500, R"({"message":"500 Internal Server Error"})", {}});
  ForgeApi api;
  ForgeApi other;  // AppController's own: the memo is process-wide
  PrInfo pr = nestedMr();
  // A whole day of the window coming to the front every few minutes.
  for(int i = 0; i < 24 * 12; ++i) {
    const QVariantMap rep = reportOnce(i % 2 == 0 ? api : other, auth, pr);
    EXPECT_EQ(rep.value(QStringLiteral("reason")).toString(), QStringLiteral("noFiles"));
    EXPECT_EQ(rep.value(QStringLiteral("host")).toString(), QStringLiteral("gitlab.company.ru"));
    clock.advanceMinutes(5);
  }
  EXPECT_EQ(countSeen(server, "GET " + mr + "/diffs"), 1);
  EXPECT_EQ(countSeen(server, "GET " + mr + "/changes"), 1);
  EXPECT_EQ(countSeen(server, "GET " + mr + "/approvals"), 0) << "no files, nothing to approve against";
  // A new commit: asked once more.
  pr.headSha = QStringLiteral("def456");
  reportOnce(api, auth, pr);
  EXPECT_EQ(countSeen(server, "GET " + mr + "/diffs"), 2);
}

TEST(ForgeApiCodeOwners, DiffsFailingAndNoLegacyChangesIsAlsoNoFiles) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray mr = "/api/v4/projects/4242/merge_requests/2299";
  server.route("GET " + mr + "/diffs", {500, "{}", {}});  // /changes: 404, gone in newer GitLab
  ForgeApi api;
  EXPECT_EQ(reportOnce(api, auth, nestedMr()).value(QStringLiteral("reason")).toString(), QStringLiteral("noFiles"));
}

TEST(ForgeApiCodeOwners, ACheckoutsFilesAndCodeOwnersLeaveOnlyTheApprovalsToAsk) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server);
  const QByteArray mr = "/api/v4/projects/4242/merge_requests/2299";
  server.route("GET " + mr + "/diffs", {500, "{}", {}});
  server.route("GET " + mr + "/approvals", ok(R"({"approved_by":[{"user":{"username":"ann"}}]})"));
  ForgeApi api;
  ForgeApi::LocalOwners local;
  local.known = true;
  local.files = {QStringLiteral("src/a.cpp"), QStringLiteral("docs/b.md")};
  local.ownersLocation = QStringLiteral("CODEOWNERS");
  local.ownersText = QStringLiteral("/src/ @ann\n/docs/ @bob\n");
  const QVariantMap rep = reportOnce(api, auth, nestedMr(), local);
  ASSERT_TRUE(rep.value(QStringLiteral("found")).toBool());
  EXPECT_EQ(rep.value(QStringLiteral("fileCount")).toInt(), 2);
  EXPECT_EQ(rep.value(QStringLiteral("toPing")).toStringList(), (QStringList{QStringLiteral("@bob")}));
  ASSERT_EQ(server.requests().size(), 1) << server.seen().join('\n').toStdString();
  EXPECT_EQ(server.requests().first().key(), QByteArray("GET " + mr + "/approvals"));
  // No CODEOWNERS on the checkout's target branch: that is the answer too.
  local.ownersLocation.clear();
  local.ownersText.clear();
  const QVariantMap none = reportOnce(api, auth, nestedMr(), local);
  EXPECT_TRUE(none.contains(QStringLiteral("found")));
  EXPECT_FALSE(none.value(QStringLiteral("found")).toBool());
  EXPECT_EQ(countSeen(server, "GET /api/v4/projects/4242/repository/files/CODEOWNERS/raw"), 0);
}

// ---- a feature that keeps failing is paused (owner, 2026-10-21) ----
//
// CODEOWNERS against a GitLab that answers every request with a 500: past
// the rule (5 failures in a row over ≥30 min) the feature is paused and
// sends nothing at all — one quiet probe a day — whatever the app asks.

namespace {

QVariantMap ownersReportOnce(ForgeApi& api, const ForgeAuth& auth, const PrInfo& pr) {
  QVariantMap out;
  bool done = false;
  api.fetchOwnersReport(auth, QStringLiteral("tel/bts/app"), pr, [&](const QVariantMap& r) {
    out = r;
    done = true;
  });
  EXPECT_TRUE(heap::testing::waitFor(done));
  return out;
}

}  // namespace

TEST(ForgeApiFeaturePause, CodeOwnersPausedSendsNothingForDaysButOneProbeADay) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server).withFeature(heap::net::features::codeOwners());
  const QByteArray mr = "/api/v4/projects/tel%2Fbts%2Fapp/merge_requests/2299";
  server.route("GET " + mr + "/diffs", {500, R"({"message":"500 Internal Server Error"})", {}});
  server.route("GET " + mr + "/changes", {500, R"({"message":"500 Internal Server Error"})", {}});
  server.route("GET " + mr + "/approvals", {500, R"({"message":"500 Internal Server Error"})", {}});
  ForgeApi api;
  PrInfo pr;
  pr.number = 2299;
  pr.state = QStringLiteral("open");
  pr.baseRef = QStringLiteral("master");
  pr.headSha = QStringLiteral("abc123");
  heap::net::FeatureGate& gate = heap::net::FeatureGate::instance();
  // Every 10 minutes the app asks again (a new commit each time, so no
  // per-head memory helps), until the feature is paused.
  int asks = 0;
  while(!gate.isPaused(heap::net::features::codeOwners()) && asks < 200) {
    pr.headSha = QStringLiteral("sha%1").arg(asks);
    ownersReportOnce(api, auth, pr);
    clock.advanceMinutes(10);
    ++asks;
  }
  ASSERT_TRUE(gate.isPaused(heap::net::features::codeOwners())) << "never paused after " << asks << " asks";
  const heap::net::FeatureRecord rec = gate.record(heap::net::features::codeOwners());
  EXPECT_GE(rec.failures, heap::net::kPauseFailures);
  EXPECT_GE(rec.firstFailureAt.secsTo(rec.pausedAt), heap::net::kPauseSpanSecs);
  EXPECT_EQ(rec.host, QStringLiteral("gitlab.company.ru"));
  // Three simulated days, the app asking every 10 minutes.
  const qsizetype before = server.requests().size();
  for(int i = 0; i < 3 * 24 * 6; ++i) {
    pr.headSha = QStringLiteral("day%1").arg(i);
    const QVariantMap r = ownersReportOnce(api, auth, pr);
    EXPECT_FALSE(r.value(QStringLiteral("found")).toBool());
    clock.advanceMinutes(10);
  }
  const qsizetype during = server.requests().size() - before;
  EXPECT_LE(during, 3) << "one quiet probe a day, nothing else";
  EXPECT_GE(during, 1);
  EXPECT_TRUE(gate.isPaused(heap::net::features::codeOwners()));

  // The server is fixed; the next daily probe answers, and the feature works
  // again.
  server.route("GET " + mr + "/diffs", ok(diffsPage(0, 3)));
  server.route("GET " + mr + "/approvals", ok(R"({"approved_by":[]})"));
  clock.advanceMinutes(24 * 60);
  ForgeApi::resetPolitenessForTests();  // the per-URL back-off is not what is under test here
  pr.headSha = QStringLiteral("fixed");
  ownersReportOnce(api, auth, pr);
  EXPECT_FALSE(gate.isPaused(heap::net::features::codeOwners()));
  for(const auto& r : server.requests()) {
    EXPECT_EQ(r.method, QByteArray("GET"));
  }
}

TEST(ForgeApiFeaturePause, NoAccessThreeTimesPausesWithoutWaitingHalfAnHour) {
  ManualClock clock;
  FakeHttpServer server;
  const ForgeAuth auth = gitlabAuth(server).withFeature(heap::net::features::codeOwners());
  server.route("GET /api/v4/user", {401, R"({"message":"401 Unauthorized"})", {}});
  ForgeApi api;
  for(int i = 0; i < 10; ++i) {
    bool done = false;
    api.fetchLogin(auth, [&](const QString&, int) {
      done = true;
    });
    EXPECT_TRUE(heap::testing::waitFor(done));
    clock.advanceMinutes(1);
  }
  EXPECT_EQ(server.requests().size(), 3) << "the third 401 pauses it";
  EXPECT_EQ(heap::net::FeatureGate::instance().record(heap::net::features::codeOwners()).pause, heap::net::PauseKind::NoAccess);
}

TEST(ForgeApiFeaturePause, AnUntaggedRequestIsNotHeldByAPausedFeature) {
  ManualClock clock;
  FakeHttpServer server;
  server.route("GET /api/v4/user", ok(R"({"username":"me"})"));
  heap::net::FeatureGate& gate = heap::net::FeatureGate::instance();
  for(int i = 0; i < 3; ++i) {
    gate.report(heap::net::features::codeOwners(), 403);
  }
  ASSERT_TRUE(gate.isPaused(heap::net::features::codeOwners()));
  ForgeApi api;
  QString login;
  bool done = false;
  api.fetchLogin(gitlabAuth(server).withFeature(heap::net::features::mrThreads()), [&](const QString& l, int) {
    login = l;
    done = true;
  });
  ASSERT_TRUE(heap::testing::waitFor(done));
  EXPECT_EQ(login, QStringLiteral("me")) << "another feature on the same host goes on";
}
