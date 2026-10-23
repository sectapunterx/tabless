#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace heap::integrations {

// A task pulled from an external tracker (any of the providers in
// ProviderRegistry). Kept as a plain value type — no QObject — so it can be
// serialized, compared and unit-tested without an event loop.
struct ExternalTask {
  QString providerId;  // "jira" | "github" | "gitlab" | …
  QString externalId;  // Jira "PROJ-123", GH issue number as string
  QString url;
  QString title;
  QString body;
  QString status;    // provider-native status (map with StatusMap on use)
  // The tracker's own kind of that status, where it has one (Jira's
  // statusCategory.key: "new" | "indeterminate" | "done"): the column a status
  // name nobody mapped falls back to (StatusMap::categoryColumn).
  QString statusCategory;
  // When the status last changed kind (Jira's statuscategorychangedate): a
  // lower bound on when the issue entered `status`, exact when the move
  // changed its category (into Done, say). Invalid = not said.
  QDateTime statusCategoryChangedAt;
  QString priority;  // provider-native priority (map with StatusMap on use)
  QStringList labels;
  QDateTime updatedAt;
  // Identity and context the trackers hand out alongside the issue (HEAP-117).
  // Every one of these is optional: a provider that does not expose a field, or
  // an issue that has none, leaves it default.
  QString assignee;  // display name / handle of the current owner
  QString author;    // who reported it
  QDateTime dueAt;   // tracker-side due date, local time
  bool dueHasTime = false;
  QDateTime createdAt;
  int commentCount = -1;  // -1 = the provider did not say
  QString issueType;      // "Bug" | "task" | Sentry level | …
  // Which project/repo the issue belongs to: "owner/name" for the git forges,
  // the project key for Jira, a list/project name elsewhere. Needed to tell two
  // issues that share a number apart when pulling across projects.
  QString project;
  QString milestone;                    // milestone / fix version / iteration
  QHash<QString, QString> labelColors;  // label name → "#rrggbb"
  // True when this issue came from the provider's "assigned to me" endpoint,
  // which spans projects — so its externalId is only unique within its project
  // and it must never be written back through the configured-project path.
  bool crossProject = false;
  // The statuses this issue's workflow can move it to right now, for trackers
  // that say (Jira's `transitions`). `transitionsKnown` false = not reported,
  // so every move is left for the tracker to judge.
  QStringList transitions;
  bool transitionsKnown = false;
  // The kind of each status in `transitions` (name → category, as
  // statusCategory), so a move to an unmapped status still finds its column.
  QHash<QString, QString> transitionCategories;
  // Read-only facts a tracker gives about the item beyond an issue's fields,
  // stored as they are on the card (ExternalMeta::details). A merge or pull
  // request (APP-242): {kind: "mr"|"pr", state, draft, sourceBranch,
  // targetBranch, mergeStatus, conflicts, pipeline, approvals,
  // approvalsRequired, reviewers, closes}. A Jira sprint (APP-255): {sprint:
  // {name, state, start, end}}. Jira's estimate (dogfood 2026-10-19):
  // {estimate: {original, remaining, spent (seconds), points}}. Keys absent
  // when the tracker did not say.
  QJsonObject details;
};

// When an issue entered the status it is in now, from the tracker's own
// history (dogfood 2026-10-19): the week recap read the time a pull first
// noticed the move, so a ticket moved on Friday and pulled on Monday showed
// as moved on Monday. `from` is empty when the issue never changed status
// (then `at` is when it was created).
struct ExternalStatusChange {
  QString externalId;
  QString from;
  QString to;
  QDateTime at;
};

// A person pulled from a chat/directory integration (Mattermost). Separate from
// ExternalTask because nothing about it is issue-shaped: there is no status to
// map to a column and nothing to push back. Also a plain value type.
struct ExternalContact {
  QString providerId;    // "mattermost"
  QString externalId;    // the provider's stable user id
  QString username;      // the @handle, as typed in chat
  QString displayName;   // full name, nickname, or the username as a last resort
  QString position;      // the job title the person set on their own profile
  QString role;          // what heap shows when there is no position: "System admin", "Member", …
  QString channelLabel;  // where we met them: "@pavel", "#backend"
  QString email;
  bool isBot = false;
  bool deactivated = false;
};

// One comment on a mirrored issue (HEAP-117). Read-only: fetched when the
// ticket is opened and kept in a local cache so it reads offline
// (dogfood 2026-10-19). Nothing is ever sent back.
struct ExternalComment {
  QString author;
  // Who wrote it, beyond the name (dogfood 2026-10-23): the tracker login
  // shown as "@nick" (Jira Server/DC `name`, else `key`; Cloud has none, so
  // the local part of `emailAddress` when the site shows it), and the
  // picture the tracker keeps for them — fetched like a ticket's pictures.
  QString login;
  QString avatarUrl;
  // Their address when the tracker shows it (Cloud may hide it).
  QString email;
  // Written by the account the tracker is connected as.
  bool mine = false;
  QString body;
  QDateTime createdAt;
  QString url;
  // Where on the issue's own page the comment sits, for trackers that give a
  // comment an id but no URL: appended to the issue URL ("#note_7",
  // "?focusedCommentId=10042").
  QString anchor;
};

// One status a project's workflows can put an issue in (dogfood 2026-10-20):
// what the status mapping lists, instead of every status the whole tracker
// instance knows. `category` is the tracker's own kind of it (Jira:
// "new" | "indeterminate" | "done"), empty when not said.
struct ExternalStatus {
  QString project;
  QString name;
  QString category;
};

// An edit of what came from the tracker (owner item 16, 2026-10-23): the
// description, a field, or one Smart Checklist item. Sent only where the
// user switched content writes on for that tracker, and only when the
// tracker still holds what the card last saw (`base`): a value changed
// there meanwhile is a conflict, never overwritten.
struct ContentEdit {
  QString externalId;
  QString project;
  // "summary" | "description" | "duedate" | "assignee" | "checklist"
  QString field;
  // summary: the text; description: markdown (or the tracker's own markup
  // when `raw`); duedate: yyyy-MM-dd, empty clears it; assignee: the id a
  // user search gave (Cloud accountId, Server/DC user name), empty unassigns.
  QString value;
  // What the card last saw, in the form a pull stores it: ExternalMeta's
  // title / body, the due date as yyyy-MM-dd, the assignee's display name.
  QString base;
  bool raw = false;
  // checklist: "state" | "text" | "add", the item (as details.smartChecklist
  // numbers them), and the list the card last saw. The new status (its name)
  // or text is `value`; a custom status's category is `checklistCategory`.
  QString checklistOp;
  int index = -1;
  QJsonArray baseChecklist;
  QString checklistCategory;
};

// How an edit ended. `outcome`: "ok"; "conflict" (the tracker changed it
// meanwhile: `remote` / `remoteChecklist` hold its value now, nothing was
// written); "refused" (cannot be written without loss: `error` is the
// reason code, `detail` the offending text); "error" (the tracker failed or
// refused: `httpStatus`, `error`); "unsupported".
struct ContentEditResult {
  QString externalId;
  QString project;
  QString field;
  QString outcome;
  QString error;
  QString detail;
  int httpStatus = 0;
  int retryAfterSecs = 0;
  // ok: the value as a pull will read it back; conflict: the tracker's.
  QString remote;
  QJsonArray remoteChecklist;
};

// Outcome of one sync cycle for a provider.
struct SyncResult {
  int pulled = 0;
  int pushed = 0;
  QStringList errors;

  bool ok() const {
    return errors.isEmpty();
  }
};

}  // namespace heap::integrations
