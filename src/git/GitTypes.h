#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace heap::git {

struct RepoConfig {
  QString path;            // worktree root (absolute)
  QString resolvedGitDir;  // <repo>/.git OR worktree gitdir target
  // Where refs, packed-refs and config live: the same as resolvedGitDir for a
  // plain clone, the main repository's .git for a linked worktree (PLAT-17).
  QString commonGitDir;
};

struct PrInfo {
  QString state;  // "open" | "draft" | "merged" | "closed" | ""
  int number = 0;
  QString url;
  QString title;
  QString baseRef;       // the branch it merges into
  qint64 projectId = 0;  // GitLab's project id, for its API
  QString headSha;       // the last commit of its source branch: its changed files are known until it moves
  bool draft = false;
  QString checks;     // CI rollup: "passing" | "failing" | "pending" | ""
  QString checksSha;  // the commit `checks` is about (GitLab's head pipeline can lag the head)
  // Facts for "whose move" (APP-156). All optional: an older gh, or glab,
  // may not say. GitHub's vocabulary; glab answers are mapped onto it.
  QString author;              // login of whoever opened the PR
  QString reviewDecision;      // "APPROVED" | "CHANGES_REQUESTED" | "REVIEW_REQUIRED" | ""
  QStringList reviewRequests;  // logins (or team slugs) a review is waiting on
  QString mergeable;           // "MERGEABLE" | "CONFLICTING" | "UNKNOWN" | ""
  QString move;                // "mine" | "theirs" | "" — see PrFacts.h
  QString moveReason;          // I18n key suffix, empty with no move
  QDateTime fetchedAt;

  QVariantMap toVariant() const {
    QVariantMap m;
    m["state"] = state;
    m["number"] = number;
    m["url"] = url;
    m["title"] = title;
    m["baseRef"] = baseRef;
    m["checksSha"] = checksSha;
    m["draft"] = draft;
    m["checks"] = checks;
    m["author"] = author;
    m["reviewDecision"] = reviewDecision;
    m["reviewRequests"] = reviewRequests;
    m["mergeable"] = mergeable;
    m["move"] = move;
    m["moveReason"] = moveReason;
    m["fetchedAt"] = fetchedAt;
    return m;
  }
};

struct RepoState {
  QString repoPath;
  QString branch;  // "" if unset; "(detached HEAD)" if detached
  QString headSha;
  QString upstream;  // "origin/main" if known
  int ahead = 0;
  int behind = 0;
  PrInfo pr;
  bool remote = false;  // on an SSH dev server
  QString error;        // a remote repo's last failure: remoteFailureKey()

  QVariantMap toVariant() const {
    QVariantMap m;
    m["repoPath"] = repoPath;
    m["remote"] = remote;
    m["error"] = error;
    m["branch"] = branch;
    m["headSha"] = headSha;
    m["upstream"] = upstream;
    m["ahead"] = ahead;
    m["behind"] = behind;
    m["pr"] = pr.toVariant();
    return m;
  }
};

}  // namespace heap::git
