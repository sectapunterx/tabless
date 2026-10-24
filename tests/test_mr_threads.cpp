// "Ждут меня" (dogfood 2026-10-21 item 2): a thread judged against my login
// (waiting on me, waiting on them, nothing), a review owed since the last
// push, the oldest wait first — and the watch that reads a request's threads
// only when its list entry moved: a quiet request costs no request at all,
// across any number of syncs (incident 2026-10-20). GitLab discussions and
// GitHub's GraphQL against FakeHttpServer; GraphQL is a read, never a
// mutation.

#include "FakeHttpServer.h"

#include "git/ForgeApi.h"
#include "git/MrThreads.h"
#include "net/FeatureGate.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

using namespace heap::git;
using heap::testing::FakeHttpServer;

namespace {

QDateTime at(const char* iso) {
  return QDateTime::fromString(QString::fromLatin1(iso), Qt::ISODate);
}

ThreadNote note(const QString& who, const char* when, const QString& body = QStringLiteral("…"), const QString& id = {}) {
  ThreadNote n;
  n.author = who;
  n.at = at(when);
  n.body = body;
  n.id = id.isEmpty() ? who + QString::fromLatin1(when) : id;
  n.url = QStringLiteral("https://gl/x/-/merge_requests/1#note_") + n.id;
  return n;
}

MrThread thread(const QList<ThreadNote>& notes, bool resolvable = true, bool resolved = false) {
  MrThread t;
  t.id = notes.isEmpty() ? QString() : notes.first().id;
  t.resolvable = resolvable;
  t.resolved = resolved;
  t.notes = notes;
  t.noteCount = static_cast<int>(notes.size());
  t.starter = notes.isEmpty() ? QString() : notes.first().author;
  return t;
}

const QString kMe = QStringLiteral("fin");

}  // namespace

// ---- the pure rules ----

TEST(MrThreadsRules, MentionsAreWholeHandlesInAnyCase) {
  EXPECT_TRUE(mentions(QStringLiteral("@fin глянь"), kMe));
  EXPECT_TRUE(mentions(QStringLiteral("cc @FIN."), kMe));
  EXPECT_TRUE(mentions(QStringLiteral("(@fin)"), QStringLiteral("@fin")));
  EXPECT_FALSE(mentions(QStringLiteral("@finance team"), kMe));
  EXPECT_FALSE(mentions(QStringLiteral("@fin-bot"), kMe));
  EXPECT_FALSE(mentions(QStringLiteral("mail fin@corp.ru"), kMe));
  EXPECT_FALSE(mentions(QStringLiteral("@fin"), QString()));
}

TEST(MrThreadsRules, TheirWordAfterMineWaitsOnMeSinceTheirFirstWord) {
  const MrThread t = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00"),
                             note(kMe, "2026-10-20T11:00:00"),
                             note(QStringLiteral("ira"), "2026-10-20T12:00:00"),
                             note(QStringLiteral("max"), "2026-10-20T13:00:00")});
  const ThreadVerdict v = judgeThread(t, kMe, QStringLiteral("someone"));
  EXPECT_EQ(v.wait, ThreadWait::OnMe);
  EXPECT_EQ(v.reason, QStringLiteral("replied"));
  EXPECT_EQ(v.since, at("2026-10-20T12:00:00")) << "the wait began with the first word after mine";
  EXPECT_EQ(v.who, QStringLiteral("max"));
  EXPECT_TRUE(v.url.endsWith(QStringLiteral("#note_ira2026-10-20T12:00:00")));
}

TEST(MrThreadsRules, MyLastWordWaitsOnThem) {
  const MrThread t = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00"), note(kMe, "2026-10-20T11:00:00")});
  const ThreadVerdict v = judgeThread(t, kMe, kMe);
  EXPECT_EQ(v.wait, ThreadWait::OnThem);
  EXPECT_EQ(v.since, at("2026-10-20T11:00:00"));
}

TEST(MrThreadsRules, ANewThreadOnMyRequestWaitsOnMe) {
  const MrThread t = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00")});
  const ThreadVerdict v = judgeThread(t, kMe, kMe);
  EXPECT_EQ(v.wait, ThreadWait::OnMe);
  EXPECT_EQ(v.reason, QStringLiteral("started"));
}

TEST(MrThreadsRules, SomeoneElsesThreadIsNotMineUnlessIAmNamed) {
  const MrThread quiet = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00"), note(QStringLiteral("max"), "2026-10-20T11:00:00")});
  EXPECT_EQ(judgeThread(quiet, kMe, QStringLiteral("ira")).wait, ThreadWait::None);
  const MrThread named = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00", QStringLiteral("@fin что думаешь?"))});
  const ThreadVerdict v = judgeThread(named, kMe, QStringLiteral("ira"));
  EXPECT_EQ(v.wait, ThreadWait::OnMe);
  EXPECT_EQ(v.reason, QStringLiteral("mention"));
}

TEST(MrThreadsRules, IdiotTestNoLoginAResolvedThreadAndAnEmptyOne) {
  const MrThread t = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00", QStringLiteral("@fin"))});
  EXPECT_EQ(judgeThread(t, QString(), kMe).wait, ThreadWait::None) << "no login known: nothing waits on anyone";
  EXPECT_EQ(judgeThread(t, QStringLiteral("@"), kMe).wait, ThreadWait::None);
  const MrThread resolved = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00")}, true, true);
  EXPECT_EQ(judgeThread(resolved, kMe, kMe).wait, ThreadWait::None);
  EXPECT_EQ(judgeThread(MrThread{}, kMe, kMe).wait, ThreadWait::None);
}

TEST(MrThreadsRules, AGeneralNoteWaitsOnlyForAnUnansweredMention) {
  const MrThread chat = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00", QStringLiteral("LGTM"))}, false);
  EXPECT_EQ(judgeThread(chat, kMe, kMe).wait, ThreadWait::None) << "a general note is not a thread to answer";
  const MrThread named = thread({note(QStringLiteral("ira"), "2026-10-20T10:00:00", QStringLiteral("@fin когда релиз?"))}, false);
  EXPECT_EQ(judgeThread(named, kMe, kMe).wait, ThreadWait::OnMe);
  // I answered elsewhere on the request after it: answered.
  EXPECT_EQ(judgeThread(named, kMe, kMe, at("2026-10-20T11:00:00")).wait, ThreadWait::None);
  // My own general note is not "waiting on them".
  const MrThread mine = thread({note(kMe, "2026-10-20T10:00:00")}, false);
  EXPECT_EQ(judgeThread(mine, kMe, kMe).wait, ThreadWait::None);
}

TEST(MrThreadsRules, AReviewIsOwedUntilGivenAfterTheLastPush) {
  MrBrief mr;
  mr.url = QStringLiteral("https://gl/x/-/merge_requests/1");
  mr.author = QStringLiteral("ira");
  mr.reviewers = {QStringLiteral("@Fin")};
  mr.createdAt = at("2026-10-19T09:00:00");
  mr.open = true;
  MrThreadFacts f;
  f.ok = true;
  MrWaits w = summarizeWaits(mr, f, kMe);
  EXPECT_TRUE(w.review);
  EXPECT_EQ(w.reason, QStringLiteral("review"));
  EXPECT_EQ(w.since, mr.createdAt);
  EXPECT_EQ(w.url, mr.url);
  f.events.append({MrEvent::Approved, kMe, at("2026-10-19T12:00:00")});
  EXPECT_FALSE(summarizeWaits(mr, f, kMe).review) << "approved, nothing pushed since";
  f.events.append({MrEvent::Push, QStringLiteral("ira"), at("2026-10-20T08:00:00")});
  w = summarizeWaits(mr, f, kMe);
  EXPECT_TRUE(w.review) << "a push after my approval asks again";
  EXPECT_EQ(w.since, at("2026-10-20T08:00:00"));
  // Not a review of my own request; nothing on a closed one; nothing without a login.
  mr.author = kMe;
  EXPECT_FALSE(summarizeWaits(mr, f, kMe).review);
  mr.author = QStringLiteral("ira");
  mr.open = false;
  EXPECT_FALSE(summarizeWaits(mr, f, kMe).waitsOnMe());
  mr.open = true;
  EXPECT_FALSE(summarizeWaits(mr, f, QString()).waitsOnMe());
}

TEST(MrThreadsRules, ThreadsComeOldestFirstAndTheRowSaysHowMany) {
  MrBrief mr;
  mr.url = QStringLiteral("https://gl/x/-/merge_requests/1");
  mr.author = kMe;
  mr.open = true;
  MrThreadFacts f;
  f.ok = true;
  f.threads = {thread({note(QStringLiteral("ira"), "2026-10-20T12:00:00")}),
               thread({note(QStringLiteral("max"), "2026-10-20T09:00:00")}),
               thread({note(QStringLiteral("ira"), "2026-10-19T09:00:00"), note(kMe, "2026-10-19T10:00:00")})};
  const MrWaits w = summarizeWaits(mr, f, kMe);
  ASSERT_EQ(w.items.size(), 3);
  EXPECT_EQ(w.onMe, 2);
  EXPECT_EQ(w.items.at(0).verdict.who, QStringLiteral("max")) << "the longest wait first";
  EXPECT_EQ(w.items.at(1).verdict.who, QStringLiteral("ira"));
  EXPECT_EQ(w.items.at(2).verdict.wait, ThreadWait::OnThem) << "waiting on them after waiting on me";
  EXPECT_EQ(w.reason, QStringLiteral("threads"));
  EXPECT_EQ(w.since, at("2026-10-20T09:00:00"));

  MrWaits older;
  older.since = at("2026-10-18T09:00:00");
  MrWaits unknown;
  EXPECT_TRUE(waitsLonger(older, QStringLiteral("b"), w, QStringLiteral("a")));
  EXPECT_FALSE(waitsLonger(w, QStringLiteral("a"), older, QStringLiteral("b")));
  EXPECT_TRUE(waitsLonger(w, QStringLiteral("a"), unknown, QStringLiteral("0"))) << "an unknown age goes last";
}

TEST(MrThreadsRules, AReadIsDueOnlyWhenTheListEntryMoved) {
  const QString s1 = threadStamp(at("2026-10-20T10:00:00"), 3);
  EXPECT_TRUE(threadsNeedRead(QString(), s1)) << "never read";
  EXPECT_FALSE(threadsNeedRead(s1, s1));
  EXPECT_TRUE(threadsNeedRead(s1, threadStamp(at("2026-10-20T10:05:00"), 3)));
  EXPECT_TRUE(threadsNeedRead(s1, threadStamp(at("2026-10-20T10:00:00"), 4)));
  EXPECT_FALSE(threadsNeedRead(s1, QString()));
}

// ---- a thread set aside (dogfood 2026-10-22 item 3) ----

namespace {

MrBrief openMine() {
  MrBrief mr;
  mr.url = QStringLiteral("https://gl/x/-/merge_requests/1");
  mr.author = kMe;
  mr.open = true;
  return mr;
}

}  // namespace

TEST(MrThreadsIgnore, AnIgnoredThreadWaitsOnNobodyAndCountsNowhere) {
  MrThreadFacts f;
  f.ok = true;
  const MrThread a = thread({note(QStringLiteral("ira"), "2026-10-19T09:00:00")});
  const MrThread b = thread({note(QStringLiteral("max"), "2026-10-20T09:00:00")});
  f.threads = {a, b};
  QHash<QString, ThreadIgnore> marks;
  marks.insert(a.id, ignoreMarkOf(a));
  const MrWaits w = summarizeWaits(openMine(), f, kMe, marks);
  EXPECT_EQ(w.onMe, 1) << "the ignored thread does not wait on me";
  ASSERT_EQ(w.items.size(), 1);
  EXPECT_EQ(w.items.first().thread.id, b.id);
  ASSERT_EQ(w.ignored.size(), 1) << "it is still there, set aside";
  EXPECT_EQ(w.ignored.first().thread.id, a.id);
  EXPECT_EQ(w.reason, QStringLiteral("started")) << "one thread left, not \"threads\"";
  EXPECT_EQ(w.since, at("2026-10-20T09:00:00")) << "the row's age is the thread left's";

  // Every thread aside: nothing waits.
  marks.insert(b.id, ignoreMarkOf(b));
  const MrWaits none = summarizeWaits(openMine(), f, kMe, marks);
  EXPECT_EQ(none.onMe, 0);
  EXPECT_FALSE(none.waitsOnMe());
  EXPECT_EQ(none.ignored.size(), 2);
}

TEST(MrThreadsIgnore, ANewWordBringsTheThreadBackADeletedOneDoesNot) {
  const MrThread t = thread({note(QStringLiteral("ira"), "2026-10-19T09:00:00"), note(QStringLiteral("ira"), "2026-10-19T10:00:00")});
  const ThreadIgnore mark = ignoreMarkOf(t);
  EXPECT_TRUE(ignoreHolds(t, mark));

  MrThread later = t;
  later.notes.append(note(QStringLiteral("max"), "2026-10-21T08:00:00"));
  EXPECT_FALSE(ignoreHolds(later, mark)) << "someone wrote in it again";

  MrThread mine = t;
  mine.notes.append(note(kMe, "2026-10-21T08:00:00"));
  EXPECT_FALSE(ignoreHolds(mine, mark)) << "my own new word brings it back too";

  MrThread deleted = t;
  deleted.notes.removeLast();
  EXPECT_TRUE(ignoreHolds(deleted, mark)) << "a deleted note is not a new word";

  // Back in the counts once the mark no longer holds.
  MrThreadFacts f;
  f.ok = true;
  f.threads = {later};
  QHash<QString, ThreadIgnore> marks;
  marks.insert(later.id, mark);
  const MrWaits w = summarizeWaits(openMine(), f, kMe, marks);
  EXPECT_EQ(w.onMe, 1);
  EXPECT_TRUE(w.ignored.isEmpty());

  // Idiot test: a mark with no time and another last word comes back
  // rather than hiding a thread for good.
  ThreadIgnore blind;
  blind.noteId = QStringLiteral("gone");
  EXPECT_FALSE(ignoreHolds(t, blind));
}

TEST(MrThreadsIgnore, MarksRoundTripAndUndoKeyByKey) {
  const MrThread t = thread({note(QStringLiteral("ira"), "2026-10-19T09:00:00")});
  const ThreadIgnore mark = ignoreMarkOf(t);
  const ThreadIgnore back = ignoreFromJson(ignoreToJson(mark));
  EXPECT_EQ(back.noteId, mark.noteId);
  EXPECT_EQ(back.at, mark.at);
  EXPECT_TRUE(ignoreHolds(t, back));

  const QString mr1 = QStringLiteral("https://gl/x/-/merge_requests/1");
  const QString mr12 = QStringLiteral("https://gl/x/-/merge_requests/12");
  QJsonObject store;
  store.insert(threadIgnoreKey(mr1, QStringLiteral("d1")), ignoreToJson(mark));
  store.insert(threadIgnoreKey(mr12, QStringLiteral("d2")), ignoreToJson(mark));
  const QHash<QString, ThreadIgnore> of1 = ignoresOf(store, mr1);
  ASSERT_EQ(of1.size(), 1) << "!1 is not a prefix of !12";
  EXPECT_TRUE(of1.contains(QStringLiteral("d1")));

  // Undo the first ignore after a second one: only its key goes.
  const QJsonObject before;
  QJsonObject afterFirst;
  afterFirst.insert(QStringLiteral("a"), ignoreToJson(mark));
  QJsonObject now = afterFirst;
  now.insert(QStringLiteral("b"), ignoreToJson(mark));
  bool clean = false;
  const QJsonObject undone = rebaseIgnores(now, afterFirst, before, &clean);
  EXPECT_TRUE(clean);
  EXPECT_FALSE(undone.contains(QStringLiteral("a")));
  EXPECT_TRUE(undone.contains(QStringLiteral("b")));
  // Taken back already (returned by hand since): not clean.
  QJsonObject returned = now;
  returned.remove(QStringLiteral("a"));
  rebaseIgnores(returned, afterFirst, before, &clean);
  EXPECT_FALSE(clean);
}

// ---- parsing ----

namespace {

const QByteArray kGitlabPage1 = R"([
 {"id":"d1","individual_note":false,"notes":[
   {"id":101,"type":"DiffNote","body":"А если партнёр отвечает 429?","author":{"username":"ira"},
    "created_at":"2026-10-20T10:00:00.000Z","system":false,"resolvable":true,"resolved":false,
    "position":{"new_path":"src/webhooks/retry.cpp","new_line":88,"old_path":"src/webhooks/retry.cpp","old_line":80}},
   {"id":102,"type":"DiffNote","body":"Проверю","author":{"username":"fin"},
    "created_at":"2026-10-20T11:00:00.000Z","system":false,"resolvable":true,"resolved":false},
   {"id":103,"type":"DiffNote","body":"Нужен Retry-After","author":{"username":"ira"},
    "created_at":"2026-10-20T12:00:00.000Z","system":false,"resolvable":true,"resolved":false}]},
 {"id":"s1","individual_note":true,"notes":[
   {"id":104,"body":"added 2 commits\n\n* abc","author":{"username":"fin"},"created_at":"2026-10-20T09:00:00.000Z","system":true}]}
])";
const QByteArray kGitlabPage2 = R"([
 {"id":"s2","individual_note":true,"notes":[
   {"id":105,"body":"requested review from @fin and @max","author":{"username":"ira"},"created_at":"2026-10-19T09:00:00.000Z","system":true}]},
 {"id":"d2","individual_note":false,"notes":[
   {"id":106,"type":"DiscussionNote","body":"Done?","author":{"username":"max"},
    "created_at":"2026-10-19T10:00:00.000Z","system":false,"resolvable":true,"resolved":true}]},
 {"id":"g1","individual_note":true,"notes":[
   {"id":107,"body":"@fin посмотри","author":{"username":"max"},"created_at":"2026-10-20T13:00:00.000Z","system":false,"resolvable":false}]},
 {"id":"s3","individual_note":true,"notes":[
   {"id":108,"body":"approved this merge request","author":{"username":"max"},"created_at":"2026-10-20T14:00:00.000Z","system":true}]}
])";

const QByteArray kGithubAnswer = R"({"data":{"viewer":{"login":"fin"},"repository":{"pullRequest":{
 "reviewThreads":{"nodes":[
  {"id":"T1","isResolved":false,"path":"src/a.cpp","line":12,
   "head":{"nodes":[{"author":{"login":"fin"}}]},
   "comments":{"totalCount":25,"nodes":[
     {"databaseId":9,"author":{"login":"ira"},"body":"ok?","createdAt":"2026-10-20T10:00:00Z","url":"https://github.com/o/r/pull/7#discussion_r9"}]}},
  {"id":"T2","isResolved":true,"path":"src/b.cpp","line":3,
   "head":{"nodes":[{"author":{"login":"ira"}}]},
   "comments":{"totalCount":1,"nodes":[
     {"databaseId":10,"author":{"login":"ira"},"body":"nit","createdAt":"2026-10-20T10:00:00Z","url":"https://github.com/o/r/pull/7#discussion_r10"}]}}]},
 "comments":{"totalCount":1,"nodes":[
   {"databaseId":11,"author":{"login":"max"},"body":"@fin ping","createdAt":"2026-10-20T11:00:00Z","url":"https://github.com/o/r/pull/7#issuecomment-11"}]},
 "timelineItems":{"nodes":[
   {"__typename":"ReviewRequestedEvent","createdAt":"2026-10-19T09:00:00Z","requestedReviewer":{"login":"fin"}},
   {"__typename":"PullRequestReview","author":{"login":"max"},"submittedAt":"2026-10-19T10:00:00Z","state":"APPROVED"},
   {"__typename":"PullRequestCommit","commit":{"committedDate":"2026-10-19T08:00:00Z"}}]}}}}})";

}  // namespace

TEST(MrThreadsParse, GitlabDiscussionsEveryPageSystemNotesAndNoteLinks) {
  bool ok = false;
  const MrThreadFacts f =
      parseGitlabDiscussions(kGitlabPage1 + "\n" + kGitlabPage2, QStringLiteral("https://gl/g/app/-/merge_requests/482"), &ok);
  ASSERT_TRUE(ok);
  ASSERT_EQ(f.threads.size(), 3) << "system-only discussions are events, not threads";
  const MrThread& d1 = f.threads.at(0);
  EXPECT_TRUE(d1.resolvable);
  EXPECT_FALSE(d1.resolved);
  EXPECT_EQ(d1.path, QStringLiteral("src/webhooks/retry.cpp"));
  EXPECT_EQ(d1.line, 88);
  EXPECT_EQ(d1.notes.size(), 3);
  EXPECT_EQ(d1.notes.at(2).url, QStringLiteral("https://gl/g/app/-/merge_requests/482#note_103"));
  EXPECT_TRUE(f.threads.at(1).resolved);
  EXPECT_FALSE(f.threads.at(2).resolvable);
  int pushes = 0;
  int asked = 0;
  int approvals = 0;
  for(const MrEvent& e : f.events) {
    pushes += e.kind == MrEvent::Push ? 1 : 0;
    asked += e.kind == MrEvent::ReviewRequested ? 1 : 0;
    approvals += e.kind == MrEvent::Approved ? 1 : 0;
  }
  EXPECT_EQ(pushes, 1);
  EXPECT_EQ(asked, 2) << "@fin and @max";
  EXPECT_EQ(approvals, 1);

  MrBrief mr;
  mr.url = QStringLiteral("https://gl/g/app/-/merge_requests/482");
  mr.author = QStringLiteral("ira");
  mr.reviewers = {QStringLiteral("fin")};
  mr.open = true;
  const MrWaits w = summarizeWaits(mr, f, kMe);
  EXPECT_EQ(w.onMe, 2) << "Ira answered me in d1; Max named me in a general note";
  EXPECT_FALSE(w.review) << "my word after the last push counts as looked at";
  EXPECT_EQ(w.url, QStringLiteral("https://gl/g/app/-/merge_requests/482#note_103"));

  bool bad = true;
  parseGitlabDiscussions("<html>502</html>", QString(), &bad);
  EXPECT_FALSE(bad);
}

TEST(MrThreadsParse, GithubGraphqlThreadsConversationAndTimeline) {
  bool ok = false;
  const MrThreadFacts f = parseGithubThreads(kGithubAnswer, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(f.viewer, QStringLiteral("fin"));
  ASSERT_EQ(f.threads.size(), 3);
  EXPECT_EQ(f.threads.at(0).starter, QStringLiteral("fin")) << "the first word is read even when only the last ones are";
  EXPECT_EQ(f.threads.at(0).noteCount, 25);
  EXPECT_TRUE(f.threads.at(1).resolved);
  const ThreadVerdict v = judgeThread(f.threads.at(0), kMe, QStringLiteral("ira"));
  EXPECT_EQ(v.wait, ThreadWait::OnMe);
  EXPECT_EQ(v.reason, QStringLiteral("replied"));
  EXPECT_EQ(v.url, QStringLiteral("https://github.com/o/r/pull/7#discussion_r9"));
  MrBrief mr;
  mr.url = QStringLiteral("https://github.com/o/r/pull/7");
  mr.author = QStringLiteral("ira");
  mr.reviewers = {QStringLiteral("fin")};
  mr.reviewersPending = true;
  mr.open = true;
  const MrWaits w = summarizeWaits(mr, f, kMe);
  EXPECT_EQ(w.onMe, 2);
  EXPECT_TRUE(w.review);
  EXPECT_EQ(w.reviewSince, at("2026-10-19T09:00:00Z"));
  bool bad = true;
  parseGithubThreads(R"({"errors":[{"message":"Bad credentials"}]})", &bad);
  EXPECT_FALSE(bad);
}

// ---- the watch against a fake forge: requests bounded ----

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

ForgeAuth gitlab(const FakeHttpServer& server) {
  ForgeAuth a;
  a.glab = true;
  a.providerId = QStringLiteral("gitlab");
  a.host = QStringLiteral("gitlab.company.ru");
  a.apiBase = server.base() + QStringLiteral("/api/v4");
  a.headers = {{"PRIVATE-TOKEN", "glpat-secret"}};
  return a;
}

ForgeAuth github(const FakeHttpServer& server) {
  ForgeAuth a;
  a.providerId = QStringLiteral("github");
  a.host = QStringLiteral("ghe.corp");
  a.apiBase = forgeApiBase(false, server.base());  // Enterprise: /api/v3
  a.headers = {{"Authorization", "token ghp-secret"}};
  return a;
}

WatchedMr glMr(const FakeHttpServer& server, const char* updated, int notes) {
  WatchedMr m;
  m.url = QStringLiteral("https://gitlab.company.ru/tel/app/-/merge_requests/482");
  m.auth = gitlab(server);
  m.project = QStringLiteral("tel/app");
  m.number = 482;
  m.stamp = threadStamp(at(updated), notes);
  m.me = kMe;
  m.open = true;
  return m;
}

const QByteArray kDiscussions = "/api/v4/projects/tel%2Fapp/merge_requests/482/discussions";

int seen(const FakeHttpServer& server, const QByteArray& key) {
  int n = 0;
  for(const auto& r : server.requests()) {
    n += r.key() == key ? 1 : 0;
  }
  return n;
}

void settle(const MrThreadWatch& w) {
  heap::testing::waitUntil([&w]() {
    return !w.busy();
  });
}

}  // namespace

TEST(MrThreadWatchRequests, AQuietRequestIsReadOnceAndNeverAgainUntilItMoves) {
  ManualClock clock;
  FakeHttpServer server;
  server.route("GET " + kDiscussions, {200, kGitlabPage1, {}});
  ForgeApi api;
  MrThreadWatch watch(&api);
  // A working day of syncs, one a minute, the request never moving.
  for(int minute = 0; minute < 8 * 60; ++minute) {
    watch.sync({glMr(server, "2026-10-20T12:00:00Z", 3)});
    settle(watch);
    clock.advanceMinutes(1);
  }
  EXPECT_EQ(seen(server, "GET " + kDiscussions), 1) << "a quiet request costs nothing after the first read";
  EXPECT_TRUE(watch.factsFor(glMr(server, "2026-10-20T12:00:00Z", 3).url).ok);
  // A new note: the count moves — read once more, then quiet again.
  for(int i = 0; i < 10; ++i) {
    watch.sync({glMr(server, "2026-10-20T12:00:00Z", 4)});
    settle(watch);
  }
  EXPECT_EQ(seen(server, "GET " + kDiscussions), 2);
  // updated_at alone (a resolve, a push) is a move too.
  watch.sync({glMr(server, "2026-10-20T13:00:00Z", 4)});
  settle(watch);
  EXPECT_EQ(seen(server, "GET " + kDiscussions), 3);
  for(const auto& r : server.requests()) {
    EXPECT_EQ(r.method, QByteArray("GET"));
    EXPECT_EQ(r.headers.value("private-token"), QByteArray("glpat-secret"));
  }
}

TEST(MrThreadWatchRequests, ClosedOrUnknownLoginOrNoAccessCostNothingMore) {
  ManualClock clock;
  FakeHttpServer server;
  server.route("GET " + kDiscussions, {404, R"({"message":"404 Not found"})", {}});
  ForgeApi api;
  MrThreadWatch watch(&api);
  WatchedMr noLogin = glMr(server, "2026-10-20T12:00:00Z", 3);
  noLogin.me.clear();
  WatchedMr closed = glMr(server, "2026-10-20T12:00:00Z", 3);
  closed.open = false;
  for(int i = 0; i < 20; ++i) {
    watch.sync({noLogin});
    watch.sync({closed});
    settle(watch);
  }
  EXPECT_EQ(server.requests().size(), 0) << "no login known, or a closed request: nothing is asked";
  EXPECT_FALSE(watch.has(closed.url)) << "a closed request is forgotten";
  // No access (404): asked once, not again every sync.
  for(int i = 0; i < 30; ++i) {
    watch.sync({glMr(server, "2026-10-20T12:00:00Z", 3)});
    settle(watch);
    clock.advanceMinutes(1);
  }
  EXPECT_EQ(seen(server, "GET " + kDiscussions), 1);
  EXPECT_FALSE(watch.factsFor(closed.url).ok);
}

TEST(MrThreadWatchRequests, AFailingServerIsLeftToTheGatesBackoff) {
  ManualClock clock;
  FakeHttpServer server;
  server.route("GET " + kDiscussions, {500, R"({"message":"500 Internal Server Error"})", {}});
  ForgeApi api;
  MrThreadWatch watch(&api);
  // The request keeps moving (someone is busy on it) while the server fails:
  // still not once a minute.
  for(int minute = 0; minute < 60; ++minute) {
    watch.sync({glMr(server, minute % 2 == 0 ? "2026-10-20T12:00:00Z" : "2026-10-20T12:01:00Z", minute)});
    settle(watch);
    clock.advanceMinutes(1);
  }
  const int n = seen(server, "GET " + kDiscussions);
  EXPECT_GE(n, 1);
  EXPECT_LE(n, 4) << "5, 10, 20 min back-offs within the hour";
}

TEST(MrThreadWatchRequests, GithubThreadsAreOneGraphqlReadAndAMutationNeverLeaves) {
  ManualClock clock;
  FakeHttpServer server;
  server.route("POST /api/graphql", {200, kGithubAnswer, {}});
  ForgeApi api;
  MrThreadWatch watch(&api);
  WatchedMr m;
  m.url = QStringLiteral("https://ghe.corp/o/r/pull/7");
  m.auth = github(server);
  m.project = QStringLiteral("o/r");
  m.number = 7;
  m.stamp = threadStamp(at("2026-10-20T12:00:00Z"), -1);
  m.me = kMe;
  m.open = true;
  for(int i = 0; i < 25; ++i) {
    watch.sync({m});
    settle(watch);
  }
  ASSERT_EQ(server.requests().size(), 1);
  const auto& r = server.requests().first();
  EXPECT_EQ(r.method, QByteArray("POST"));
  EXPECT_EQ(r.path, QByteArray("/api/graphql")) << "Enterprise GraphQL lives beside /api/v3";
  EXPECT_EQ(r.headers.value("authorization"), QByteArray("token ghp-secret"));
  const QJsonObject body = QJsonDocument::fromJson(r.body).object();
  EXPECT_TRUE(body.value(QStringLiteral("query")).toString().startsWith(QStringLiteral("query")));
  EXPECT_FALSE(r.body.contains("mutation"));
  EXPECT_EQ(body.value(QStringLiteral("variables")).toObject().value(QStringLiteral("number")).toInt(), 7);
  EXPECT_EQ(watch.factsFor(m.url).threads.size(), 3);

  bool answered = false;
  api.graphql(
      github(server), R"({"query":"mutation{resolveReviewThread(input:{threadId:\"T1\"}){thread{id}}}"})", [&](const ForgeApi::Reply& rr) {
        EXPECT_EQ(rr.status, 0);
        answered = true;
      });
  EXPECT_TRUE(answered);
  EXPECT_EQ(server.requests().size(), 1) << "a mutation is refused before it is sent";
  EXPECT_EQ(forgeGraphqlUrl(QStringLiteral("https://api.github.com")), QStringLiteral("https://api.github.com/graphql"));
  EXPECT_EQ(forgeGraphqlUrl(QStringLiteral("https://ghe.corp/api/v3")), QStringLiteral("https://ghe.corp/api/graphql"));
}

TEST(MrThreadWatchRequests, PausedAfterFailingNothingIsReadForDaysButOneProbeADay) {
  ManualClock clock;
  FakeHttpServer server;
  server.route("GET " + kDiscussions, {503, R"({"message":"503 Service Unavailable"})", {}});
  ForgeApi api;
  MrThreadWatch watch(&api);
  heap::net::FeatureGate& gate = heap::net::FeatureGate::instance();
  int minute = 0;
  // Someone is busy on the request (it keeps moving) and the server fails.
  while(!gate.isPaused(heap::net::features::mrThreads()) && minute < 24 * 60) {
    watch.sync({glMr(server, minute % 2 == 0 ? "2026-10-20T12:00:00Z" : "2026-10-20T12:01:00Z", minute)});
    settle(watch);
    clock.advanceMinutes(1);
    ++minute;
  }
  ASSERT_TRUE(gate.isPaused(heap::net::features::mrThreads()));
  const qsizetype before = server.requests().size();
  for(int i = 0; i < 3 * 24 * 60; i += 5) {
    watch.sync({glMr(server, i % 2 == 0 ? "2026-10-20T12:00:00Z" : "2026-10-20T12:01:00Z", 1000 + i)});
    settle(watch);
    clock.advanceMinutes(5);
  }
  const qsizetype during = server.requests().size() - before;
  EXPECT_GE(during, 2);
  EXPECT_LE(during, 3) << "one probe a day while paused";
  // Fixed: the next probe reads it, and the watch works again.
  server.route("GET " + kDiscussions, {200, kGitlabPage1, {}});
  clock.advanceMinutes(24 * 60);
  ForgeApi::resetPolitenessForTests();
  watch.sync({glMr(server, "2026-10-21T09:00:00Z", 5000)});
  settle(watch);
  EXPECT_FALSE(gate.isPaused(heap::net::features::mrThreads()));
  EXPECT_TRUE(watch.factsFor(glMr(server, "2026-10-21T09:00:00Z", 5000).url).ok);
}
