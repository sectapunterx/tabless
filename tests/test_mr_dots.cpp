// The two dots of a merge / pull request card (dogfood 2026-10-20 item 9):
// left = CI, right = can it be merged. Every CI state against every merge
// state, then the forge vocabularies that feed them.

#include "git/PrFacts.h"

#include <QString>

#include <gtest/gtest.h>

#include <array>

using heap::git::ciDotOf;
using heap::git::MrDots;
using heap::git::mrDots;
using heap::git::MrSignals;

namespace {

// One of the five merge situations, on an open request.
MrSignals withMerge(const QString& merge) {
  MrSignals s;
  s.state = QStringLiteral("open");
  if(merge == QLatin1String("waiting")) {
    s.reviewDecision = QStringLiteral("REVIEW_REQUIRED");
  } else if(merge == QLatin1String("ready")) {
    s.reviewDecision = QStringLiteral("APPROVED");
    s.mergeStatus = QStringLiteral("MERGEABLE");
  } else if(merge == QLatin1String("conflict")) {
    s.reviewDecision = QStringLiteral("APPROVED");
    s.mergeStatus = QStringLiteral("CONFLICTING");
  } else if(merge == QLatin1String("draft")) {
    s.draft = true;
    s.reviewDecision = QStringLiteral("APPROVED");
  }
  // "none": nothing at all is known about reviews or merging.
  return s;
}

}  // namespace

TEST(MrDots, EveryCiStateAgainstEveryMergeState) {
  const std::array<std::pair<const char*, const char*>, 4> cis{{
      {"failed", "failed"},
      {"running", "running"},
      {"success", "passed"},
      {"", ""},
  }};
  const std::array<const char*, 5> merges{"waiting", "ready", "conflict", "draft", "none"};
  for(const auto& [ciIn, ciDot] : cis) {
    for(const char* merge : merges) {
      MrSignals s = withMerge(QString::fromLatin1(merge));
      s.ci = QString::fromLatin1(ciIn);
      const MrDots d = mrDots(s);
      const QString m = QString::fromLatin1(merge);
      SCOPED_TRACE(std::string(ciIn) + " x " + merge);
      EXPECT_EQ(d.ci, QString::fromLatin1(ciDot));
      QString want;
      if(m == QLatin1String("waiting") || m == QLatin1String("draft")) {
        want = QStringLiteral("waiting");
      } else if(m == QLatin1String("conflict")) {
        want = QStringLiteral("blocked");
      } else if(m == QLatin1String("ready")) {
        // Green only when CI passed as well.
        want = QString::fromLatin1(ciDot) == QLatin1String("passed") ? QStringLiteral("ready") : QStringLiteral("waiting");
      }
      EXPECT_EQ(d.merge, want);
    }
  }
}

TEST(MrDots, MergedClosedOrNoRequestHaveNoDots) {
  for(const char* state : {"merged", "closed", "locked", ""}) {
    MrSignals s = withMerge(QStringLiteral("ready"));
    s.ci = QStringLiteral("failed");
    s.state = QString::fromLatin1(state);
    const MrDots d = mrDots(s);
    EXPECT_TRUE(d.ci.isEmpty()) << state;
    EXPECT_TRUE(d.merge.isEmpty()) << state;
  }
}

TEST(MrDots, CiWordsOfEveryForge) {
  for(const char* w : {"failed", "failing", "FAILURE", "error"}) {
    EXPECT_EQ(ciDotOf(QString::fromLatin1(w)), QStringLiteral("failed")) << w;
  }
  for(const char* w : {"success", "passed", "passing"}) {
    EXPECT_EQ(ciDotOf(QString::fromLatin1(w)), QStringLiteral("passed")) << w;
  }
  for(const char* w : {"running", "pending", "created", "manual", "scheduled", "waiting_for_resource", "preparing"}) {
    EXPECT_EQ(ciDotOf(QString::fromLatin1(w)), QStringLiteral("running")) << w;
  }
  for(const char* w : {"", "canceled", "cancelled", "skipped"}) {
    EXPECT_TRUE(ciDotOf(QString::fromLatin1(w)).isEmpty()) << w;
  }
}

TEST(MrDots, ChangesRequestedIsARing) {
  MrSignals s;
  s.state = QStringLiteral("open");
  s.ci = QStringLiteral("success");
  s.reviewDecision = QStringLiteral("CHANGES_REQUESTED");
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("blocked"));
  s.reviewDecision.clear();
  s.mergeStatus = QStringLiteral("requested_changes");
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("blocked"));
}

TEST(MrDots, GitlabTrackerDetails) {
  MrSignals s;
  s.state = QStringLiteral("opened");
  s.ci = QStringLiteral("success");
  s.mergeStatus = QStringLiteral("mergeable");
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("ready"));
  s.mergeStatus = QStringLiteral("not_approved");
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("waiting"));
  s.mergeStatus.clear();
  s.conflict = true;
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("blocked"));
  s.conflict = false;
  s.mergeStatus = QStringLiteral("draft_status");
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("waiting"));
}

TEST(MrDots, ApprovalCountsAndCodeOwners) {
  MrSignals s;
  s.state = QStringLiteral("open");
  s.ci = QStringLiteral("passing");
  s.approvalsRequired = 2;
  s.approvalsGiven = 1;
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("waiting"));
  s.approvalsGiven = 2;
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("ready"));
  // The forge is happy but a CODEOWNERS need is still open: not ready.
  s.ownersPending = 1;
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("waiting"));
  MrSignals owners;
  owners.state = QStringLiteral("open");
  owners.ci = QStringLiteral("passing");
  owners.ownersPending = 0;
  EXPECT_EQ(mrDots(owners).merge, QStringLiteral("ready"));
}

TEST(MrDots, NothingKnownAboutReviewsLeavesTheRightDotOut) {
  MrSignals s;
  s.state = QStringLiteral("open");
  s.ci = QStringLiteral("success");
  const MrDots d = mrDots(s);
  EXPECT_EQ(d.ci, QStringLiteral("passed"));
  EXPECT_TRUE(d.merge.isEmpty());
}

TEST(MrDots, ADraftIsNeverReady) {
  MrSignals s;
  s.state = QStringLiteral("draft");
  s.ci = QStringLiteral("success");
  s.reviewDecision = QStringLiteral("APPROVED");
  s.mergeStatus = QStringLiteral("MERGEABLE");
  EXPECT_EQ(mrDots(s).merge, QStringLiteral("waiting"));
}

// ── Dogfood 2026-10-21: the dot said CI passed when it had not ──

TEST(MrDots, AResultAboutAnOlderPushIsAnEmptyRingNeverReady) {
  EXPECT_EQ(ciDotOf(QStringLiteral("unknown")), QStringLiteral("unknown"));
  MrSignals s;
  s.state = QStringLiteral("opened");
  s.ci = QStringLiteral("unknown");
  s.mergeStatus = QStringLiteral("mergeable");
  const MrDots d = mrDots(s);
  EXPECT_EQ(d.ci, QStringLiteral("unknown"));
  EXPECT_EQ(d.merge, QStringLiteral("waiting")) << "ready needs a pass about the head";
}

TEST(ReviewPick, OpenBeatsMergedBeatsClosedWhateverTheOrderOrSpelling) {
  using heap::git::reviewBeats;
  using heap::git::ReviewPick;
  ReviewPick open{QStringLiteral("opened"), true, 1, QDateTime()};
  ReviewPick merged{QStringLiteral("merged"), true, 3, QDateTime::currentDateTime()};
  ReviewPick closed{QStringLiteral("closed"), true, 3, QDateTime::currentDateTime()};
  EXPECT_TRUE(reviewBeats(open, merged)) << "GitLab says \"opened\"";
  EXPECT_FALSE(reviewBeats(merged, open));
  EXPECT_TRUE(reviewBeats(merged, closed));
  EXPECT_TRUE(reviewBeats({QStringLiteral("draft"), true, 0, {}}, merged));
  EXPECT_EQ(heap::git::reviewStateRank(QStringLiteral("open")), heap::git::reviewStateRank(QStringLiteral("opened")));
}

TEST(ReviewPick, ThenMineThenTheStrongerLinkThenTheLatest) {
  using heap::git::reviewBeats;
  using heap::git::ReviewPick;
  const QDateTime t0 = QDateTime(QDate(2026, 10, 21), QTime(10, 0));
  const QDateTime t1 = t0.addSecs(3600);
  // A colleague's MR naming my ticket in its title loses to mine naming it
  // only in the description.
  EXPECT_TRUE(reviewBeats({QStringLiteral("opened"), true, 1, t0}, {QStringLiteral("opened"), false, 3, t1}));
  // The branch says the key: stronger than a "Closes TEL-1" in a description.
  EXPECT_TRUE(reviewBeats({QStringLiteral("opened"), true, 3, t0}, {QStringLiteral("opened"), true, 1, t1}));
  // All else equal, the one updated last.
  EXPECT_TRUE(reviewBeats({QStringLiteral("opened"), true, 2, t1}, {QStringLiteral("opened"), true, 2, t0}));
  EXPECT_FALSE(reviewBeats({QStringLiteral("opened"), true, 2, t0}, {QStringLiteral("opened"), true, 2, t1}));
  EXPECT_TRUE(reviewBeats({QStringLiteral("opened"), true, 2, t0}, {QStringLiteral("opened"), true, 2, QDateTime()}));
}

TEST(CurrentCi, AResultAboutTheHeadIsShown) {
  using heap::git::currentCi;
  EXPECT_EQ(currentCi({{QStringLiteral("success"), QStringLiteral("aaa"), QStringLiteral("aaa"), {}}}), QStringLiteral("success"));
  // Nothing says which commit: taken as it is, as before.
  EXPECT_EQ(currentCi({{QStringLiteral("failed"), QString(), QString(), {}}}), QStringLiteral("failed"));
  EXPECT_TRUE(currentCi({}).isEmpty());
  EXPECT_TRUE(currentCi({{QString(), QString(), QStringLiteral("aaa"), {}}}).isEmpty()) << "no CI is no dot";
}

TEST(CurrentCi, AResultAboutAnOlderPushIsUnknownNotGreen) {
  using heap::git::currentCi;
  // GitLab had no head pipeline yet and gave the branch's last one.
  EXPECT_EQ(currentCi({{QStringLiteral("success"), QStringLiteral("old"), QStringLiteral("new"), {}}}), QStringLiteral("unknown"));
}

TEST(CurrentCi, TheLaterLookDecides) {
  using heap::git::CiReading;
  using heap::git::currentCi;
  const QDateTime t0 = QDateTime(QDate(2026, 10, 21), QTime(10, 0));
  const QDateTime t1 = t0.addSecs(600);
  // The pull at 10:00 saw a green run on aaa; the watcher at 10:10 saw the
  // request at bbb, running.
  const CiReading tracker{QStringLiteral("success"), QStringLiteral("aaa"), QStringLiteral("aaa"), t0};
  EXPECT_EQ(currentCi({tracker, {QStringLiteral("pending"), QStringLiteral("bbb"), QStringLiteral("bbb"), t1}}), QStringLiteral("pending"));
  // The watcher saw bbb but no CI for it: the green of aaa is not shown.
  EXPECT_EQ(currentCi({tracker, {QString(), QString(), QStringLiteral("bbb"), t1}}), QStringLiteral("unknown"));
  // Both about the same head: the later look's result.
  EXPECT_EQ(currentCi({tracker, {QStringLiteral("failed"), QStringLiteral("aaa"), QStringLiteral("aaa"), t1}}), QStringLiteral("failed"));
  // Neither says when: the first (the tracker's) wins, as it always did.
  EXPECT_EQ(currentCi({{QStringLiteral("success"), QString(), QString(), {}}, {QStringLiteral("failing"), QString(), QString(), {}}}),
            QStringLiteral("success"));
}

TEST(GlabPipeline, ManualIsNotAPassAndSkippedIsNoCi) {
  const auto checksOf = [](const char* status) {
    const QByteArray raw =
        QByteArray(R"({"iid":7,"state":"opened","sha":"head1","head_pipeline":{"status":")") + status + R"(","sha":"head1"}})";
    return heap::git::parsePrJson(raw, true).checks;
  };
  EXPECT_EQ(checksOf("manual"), QStringLiteral("pending"));
  EXPECT_TRUE(checksOf("skipped").isEmpty());
  EXPECT_EQ(checksOf("success"), QStringLiteral("passing"));
  EXPECT_EQ(checksOf("failed"), QStringLiteral("failing"));
}

TEST(GlabPipeline, TheHeadPipelineSpeaksForTheHead_TheBranchsLastOneForItsOwnCommit) {
  // A merged-results run has a commit of its own; it is still the head's.
  const heap::git::PrInfo head =
      heap::git::parsePrJson(R"({"iid":7,"state":"opened","sha":"head1","head_pipeline":{"status":"success","sha":"merge9"}})", true);
  EXPECT_EQ(head.checksHead, QStringLiteral("head1"));
  // No head pipeline yet (just pushed): the branch's last run is about an
  // older commit, and says so.
  const heap::git::PrInfo old =
      heap::git::parsePrJson(R"({"iid":7,"state":"opened","sha":"head2","pipeline":{"status":"success","sha":"head1"}})", true);
  EXPECT_EQ(old.checksHead, QStringLiteral("head1"));
  EXPECT_EQ(heap::git::currentCi({{old.checks, old.checksHead, old.headSha, {}}}), QStringLiteral("unknown"));
}
