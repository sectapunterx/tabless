#pragma once

#include "GitTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QString>

#include <cstdint>

// The pure half of the PR watcher (APP-156): turning what `gh pr view --json`
// or `glab mr view --output json` printed into a PrInfo, and reading from
// those facts whose move it is. No process, no clock: the watcher spawns the
// tools, this file only reads their answers, so every rule is testable.
namespace heap::git {

// The fields `gh pr view --json` is asked for. One place, so the parser and
// the command line cannot drift apart.
QString ghPrJsonFields();

// Collapse GitHub's statusCheckRollup array into one CI verdict: "failing",
// "pending", "passing", or "" when there are no checks.
QString rollupChecks(const QJsonArray& arr);

// Parse one PR/MR. `glab` picks GitLab's field names. An unreadable answer
// yields a PrInfo with an empty state (the "no PR" shape). fetchedAt is left
// for the caller; move/moveReason are not filled (see whoseMove).
PrInfo parsePrJson(const QByteArray& raw, bool glab);

// The signed-in user's handle from `gh api user --jq .login` (a bare line) or
// `glab api user` (a JSON object with `username`). Empty when unreadable.
QString parseLogin(const QByteArray& raw, bool glab);

enum class Move : std::uint8_t {
  None,    // nothing to say: merged, closed, a draft, or not enough facts
  Mine,    // the ball is in my court
  Theirs,  // waiting on someone or something else
};

struct MoveVerdict {
  Move move = Move::None;
  // Why, as an I18n key suffix: "reviewRequested", "ciFailing",
  // "changesRequested", "conflicts", "readyToMerge", "ciRunning",
  // "awaitingReview". Empty with Move::None.
  QString reason;
};

// Whose move it is on this PR, from facts only. `myLogin` empty means heap
// does not know who the user is, so it says nothing rather than guess.
//
// Mine:   review requested from me; on my PR — CI failing, changes requested,
//         a merge conflict, or approved (or no review needed) and mergeable.
// Theirs: on my PR — CI still running, or a review is awaited.
MoveVerdict whoseMove(const PrInfo& pr, const QString& myLogin);

// "mine" | "theirs" | "" for QML.
QString moveName(Move m);

// The two dots a merge / pull request card shows at rest (dogfood
// 2026-10-20 item 9): the left one is CI, the right one whether it can be
// merged. Read from facts the app already has — the PR watcher's PrInfo, a
// tracker card's details, a CODEOWNERS report — never fetched for the dots.
struct MrSignals {
  QString state;  // "open" | "opened" | "draft" | "merged" | "closed" | "locked" | ""
  bool draft = false;
  QString ci;              // any CI word: "passing", "success", "failed", "running", "pending", "manual", …
  QString reviewDecision;  // "APPROVED" | "CHANGES_REQUESTED" | "REVIEW_REQUIRED" | ""
  // The forge's own verdict: GitLab's detailed_merge_status ("mergeable",
  // "not_approved", "conflict", …), GitHub's mergeable_state ("clean",
  // "dirty", "blocked", …) or gh's mergeable ("MERGEABLE", "CONFLICTING").
  QString mergeStatus;
  bool conflict = false;
  int approvalsRequired = -1;  // -1: not known
  int approvalsGiven = -1;
  int ownersPending = -1;  // CODEOWNERS: -1 no report, 0 every required need approved, n needs still open
};

struct MrDots {
  QString ci;     // "failed" | "running" | "passed" | "unknown" (a result about an older push) | "" (no CI)
  QString merge;  // "blocked" (conflict, changes asked) | "ready" | "waiting" | "" (nothing known)
};

// A CI word of any forge as the left dot: "failed" | "running" | "passed" |
// "" (none, cancelled, skipped).
QString ciDotOf(const QString& ci);

// Both dots. A merged or closed request, or none, has no dots. "ready" only
// when CI passed, the approvals are in, there is no conflict and it is not
// a draft.
MrDots mrDots(const MrSignals& s);

// Which of the merge requests naming one issue is that issue's (APP-293,
// dogfood 2026-10-21): an open one over a merged one over a closed one;
// then mine over one I only review; then the stronger link (the branch says
// the key, the title does, the description does); then the latest updated.
struct ReviewPick {
  QString state;  // "open" | "opened" | "draft" | "merged" | "closed" | …
  bool mine = true;
  int link = 0;  // 3 branch, 2 title, 1 description (linkedIssueKey's strength)
  QDateTime updatedAt;
};

// 2 open, 1 merged, 0 closed or not known.
int reviewStateRank(const QString& state);
// True when `a` is the better pick.
bool reviewBeats(const ReviewPick& a, const ReviewPick& b);

// One look at a request's CI: a tracker pull, or the git watcher.
struct CiReading {
  QString ci;      // any CI word ("" = this look saw none)
  QString ciHead;  // the request's head commit the result is about ("" = not known)
  QString mrHead;  // the request's head commit as this look saw it ("" = not known)
  QDateTime seenAt;
};

// The CI to show from the looks already taken at one request (nothing is
// fetched for it). A result about an older push than the latest head any
// look saw is shown as "unknown", never as that result: a green dot for the
// commit before last was the lie of dogfood 2026-10-21. Of the results about
// the head, the latest look's. "" when no look saw any CI.
QString currentCi(const QList<CiReading>& readings);

// currentCi with where its answer came from (dogfood 2026-10-22: the card,
// the document and the sidebar read CI from this one place).
struct CiPick {
  QString ci;           // as currentCi
  int index = -1;       // the reading `ci` is, or -1
  int staleIndex = -1;  // the latest reading about an older push, or -1
};

CiPick pickCi(const QList<CiReading>& readings);

// Any CI word as a pipeline word: "success" | "failed" | "running" |
// "canceled" | "unknown" | "" (none, skipped).
QString ciWordOf(const QString& ci);

// Where a request's CI is seen when the run itself left no link: the
// request's pipelines tab (GitLab) or its checks tab (GitHub). "" without a
// request URL.
QString ciPageOf(const QString& requestUrl, bool gitlab);

}  // namespace heap::git
