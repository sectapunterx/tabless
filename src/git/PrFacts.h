#pragma once

#include "GitTypes.h"

#include <QByteArray>
#include <QJsonArray>
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
  QString ci;     // "failed" | "running" | "passed" | "" (no CI)
  QString merge;  // "blocked" (conflict, changes asked) | "ready" | "waiting" | "" (nothing known)
};

// A CI word of any forge as the left dot: "failed" | "running" | "passed" |
// "" (none, cancelled, skipped).
QString ciDotOf(const QString& ci);

// Both dots. A merged or closed request, or none, has no dots. "ready" only
// when CI passed, the approvals are in, there is no conflict and it is not
// a draft.
MrDots mrDots(const MrSignals& s);

}  // namespace heap::git
