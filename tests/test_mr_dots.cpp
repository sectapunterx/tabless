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
