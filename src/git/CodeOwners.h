#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>

// CODEOWNERS: who has to look at a merge / pull request (dogfood 2026-10-19).
//
// The pure half: read a CODEOWNERS file in GitHub's or GitLab's dialect, find
// the owners of the files an MR changes, and set them against who already
// approved and who is already asked. What comes out is a hint — "these people
// own what you changed and have not been asked yet" — never an action: Tabless
// requests no reviewer and messages no one.
namespace heap::git {

enum class OwnersFlavor : std::uint8_t {
  GitHub,
  GitLab,
};

// Where each forge looks for the file, in the order it looks. The first one
// that exists on the target branch is the one that counts.
QStringList codeOwnersLocations(OwnersFlavor flavor);

struct OwnersRule {
  QString pattern;  // as written, escapes included
  QStringList owners;
  int line = 0;
  bool exclusion = false;  // GitLab `!path`: the section does not cover it
  bool defaulted = false;  // GitLab: no owners written, the header's defaults
};

struct OwnersSection {
  QString name;  // "" for GitHub; GitLab's unnamed head is "codeowners"
  bool optional = false;
  int approvals = 1;
  QStringList defaultOwners;  // GitLab `[Section] @a @b`
  QList<OwnersRule> rules;
};

struct CodeOwnersFile {
  QList<OwnersSection> sections;
  QStringList warnings;  // "line 4: negation is not supported", …
};

CodeOwnersFile parseCodeOwners(const QString& text, OwnersFlavor flavor);

// One pattern against one repository-relative file path ("src/a.cpp", no
// leading slash). gitignore-style: `*` and `?` stop at `/`, `**` crosses
// directories, a trailing `/` matches only directories, a pattern that matches
// a directory owns everything under it. GitHub anchors a pattern with a slash
// in the middle at the root, as gitignore does. GitLab is Ruby's fnmatch on
// the whole path after its own normalisation: an unanchored path is
// `/**/path`, a trailing `/` is `/**/*` below it, and a name without one is a
// file only — "/docs" does not own docs/a.md there. `[a-z]` classes are
// GitLab only.
bool ownersPatternMatches(const QString& pattern, const QString& path, OwnersFlavor flavor);

// The owners one set of changed files needs: grouped by the owner set the
// winning rule names, per section (GitHub has one). A file whose winning rule
// names nobody needs nobody and is left out.
struct OwnersNeed {
  QString section;
  bool optional = false;
  int approvals = 1;
  QStringList owners;
  QStringList files;
};

QList<OwnersNeed> ownersForFiles(const CodeOwnersFile& file, const QStringList& changedFiles, OwnersFlavor flavor);

// One owner of one need, against the MR's facts.
struct OwnerState {
  QString handle;  // as CODEOWNERS writes it: "@alice", "@org/team", "a@b.c"
  QString state;   // "approved" | "requested" | "missing" | "author"
};

struct OwnersRequirement {
  QString section;
  bool optional = false;
  int approvals = 1;
  int approvedCount = 0;
  QStringList files;
  QList<OwnerState> owners;
  // "approved": enough owners approved. "maybe": someone approved who is not
  // named but may belong to a named team or group, which Tabless cannot see.
  // "pending": still needs owners.
  QString state;
};

struct CodeOwnersReport {
  bool found = false;  // a CODEOWNERS file was read
  QString location;    // ".github/CODEOWNERS"
  QList<OwnersRequirement> requirements;
  // Who to ping: owners of a required, pending need who are neither asked
  // yet nor the author, in the order the needs list them, once each.
  QStringList toPing;
  QStringList warnings;

  QVariantMap toVariant() const;
};

// A login against a CODEOWNERS handle: "@Alice" is "alice"; a team handle
// "@org/team" is the requested "team" or "org/team".
bool ownerHandleIs(const QString& handle, const QString& login);

CodeOwnersReport buildOwnersReport(const QString& location,
                                   const QString& text,
                                   OwnersFlavor flavor,
                                   const QStringList& changedFiles,
                                   const QStringList& approvedBy,
                                   const QStringList& requested,
                                   const QString& author);

// What the forge tells about an MR for the report. `gh pr view --json
// baseRefName,latestReviews,reviewRequests,author` plus every page of the
// PR's files; for GitLab the MR view, its `…/approvals` and every page of
// its `…/diffs` (or legacy `…/changes`).
struct OwnersFacts {
  QString baseRef;
  QStringList files;  // renamed files count under both names
  QStringList approvedBy;
  QStringList requested;
  QString author;
  bool ok = false;
  // GitLab would not give the MR's changed files (both /diffs and /changes
  // failed on the server): CODEOWNERS cannot be worked out for this head.
  bool filesUnavailable = false;
};

QString ghOwnersJsonFields();
OwnersFacts parseGhOwnersFacts(const QByteArray& raw);
// `gh api --paginate repos/O/R/pulls/N/files`: pages of
// `[{filename,previous_filename}]`, one after another.
QStringList parseGhPrFiles(const QByteArray& raw, bool* ok = nullptr);
// Approvals: `{approved_by:[{user:{username}}]}`. Diffs (`glab api
// --paginate`, pages one after another): arrays of `{old_path,new_path}`, or
// `{changes:[…]}`.
QStringList parseGlabApprovedBy(const QByteArray& raw);
QStringList parseGlabChangedFiles(const QByteArray& raw, bool* ok = nullptr);

}  // namespace heap::git
