#pragma once

#include "local/TaskLocal.h"
#include "safety/WaitingOn.h"
#include "views/SavedView.h"

#include <QAbstractListModel>
#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <qqmlregistration.h>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <optional>

// One tag on a task (HEAP-124). `id` is the label text — it is what a tracker
// calls the label and what the user types. `color` is a "#rrggbb" string, empty
// when the source gave none.
struct Label {
  QString id;
  QString color;

  bool operator==(const Label&) const = default;
};

// What a tracker says about a mirrored issue beyond the fields heap already has
// of its own (HEAP-117). Read-only: sync writes it, nothing else does, and it
// never travels back upstream. The three timestamps are the tracker's own
// values as last seen — `dueAt` in particular is what lets a pull tell "the
// user moved this deadline" from "the deadline is still the tracker's".
struct ExternalMeta {
  QString author;
  QString issueType;
  QString project;  // "owner/name", a Jira project key, a list name…
  QString milestone;
  int commentCount = -1;  // -1 = the provider did not say
  QDateTime createdAt;
  QDateTime updatedAt;
  QDateTime dueAt;
  // True when the issue arrived from an "assigned to me" pull, which spans
  // projects. Such an issue's number is unique only within its own project, and
  // it must never be pushed back through the configured-project path.
  bool crossProject = false;
  // What the tracker said last time, per field. A pull compares against these
  // to tell "changed upstream" from "changed here": only the first may
  // overwrite the card, so a local move or edit survives a sync that did not
  // touch the issue.
  QString status;
  QString title;
  QString body;
  // The column that pull put the card in. A card still sitting there was not
  // moved by the user, so it follows a changed status mapping.
  QString column;
  // The status heap tried to push and the tracker refused. While set, the card
  // keeps its local column and shows that it is out of step.
  QString unsyncedStatus;
  // The issue was missing from the last complete pull: deleted upstream, or
  // moved somewhere this connection cannot see.
  bool goneUpstream = false;
  // The filter the issue was last pulled under (a digest of the card's scope
  // fields: repo, JQL, project…). A complete pull under a different filter
  // that no longer carries the issue says nothing about the issue itself, so
  // the card is only "out of scope" — "gone" needs the same filter.
  QString scope;
  bool outOfScope = false;
  // What the tracker said last time for priority (already mapped to P0..P3)
  // and for labels, so both merge three-way like title and body.
  QString priority;
  QStringList labels;
  // Fields ("title", "body", "priority") that changed both here and in the
  // tracker since the last pull. The local value is kept until the user picks
  // a side in the editor.
  QStringList conflicts;
  // The move in unsyncedStatus was never sent — the tracker was disconnected
  // or unreachable — so it goes out after the next successful pull.
  bool pushQueued = false;
  // Read-only facts the tracker gave beyond an issue's fields, as it gave
  // them (ExternalTask::details): a merge / pull request's branches, merge
  // status and the issues it closes (APP-242), a Jira sprint (APP-255).
  // Replaced on every pull, never edited here, never sent anywhere.
  QJsonObject details;

  bool operator==(const ExternalMeta&) const = default;
};

// A dependency between two tasks. Only the "blocks" direction is ever stored:
// "blocked by" is a reverse lookup over everyone's links, so the two halves of
// a relationship cannot drift apart.
struct TaskLink {
  QString type;  // "blocks"
  QString targetId;

  bool operator==(const TaskLink&) const = default;
};

// A file attached to a task. The bytes live in <dataDir>/attachments/<id> (see
// storage/Attachments.h) and never change: the id is derived from the content,
// so two tasks attaching the same file share one copy, and detaching only drops
// this entry. The name, size and type are what the file was called and held
// when it was attached, kept here so a chip can say what a missing file was.
struct Attachment {
  QString id;
  QString name;
  qint64 size = 0;
  QString mime;

  bool operator==(const Attachment&) const = default;
};

struct Task {
  QString id;
  QString title;
  QString desc;
  QString priority;  // P0..P3
  QString status;    // backlog/todo/prog/half/blocked/review/done
  // Time-aware scheduling (HEAP-115). `scheduledAt` is when the work is meant
  // to happen, `dueAt` is when it is owed; either may be invalid (= none).
  // Each carries its own flag for whether its clock component is meaningful —
  // a bare date lands at 00:00 with the flag false. There used to be one
  // `hasTime` for both (schema ≤ 9), so a date-only deadline on a task
  // scheduled at 14:00 read as due at midnight.
  QDateTime scheduledAt;
  QDateTime dueAt;
  bool scheduledHasTime = false;
  bool dueHasTime = false;
  QString branch;
  QDateTime statusChangedAt;  // last time `status` was mutated
  bool archived = false;      // hidden from Board/Timeline once auto-archived
  // Time tracking (HEAP-78). trackedSeconds is the accumulated total; while a
  // timer runs, timerStartedAt marks when the current session began (invalid =
  // stopped). Live elapsed = trackedSeconds + (now - timerStartedAt).
  int trackedSeconds = 0;
  QDateTime timerStartedAt;
  // Recurrence (HEAP-77): "" = none, else a chrono token like "every:weekday".
  // Completing (moving to done) a recurring task spawns the next occurrence.
  QString recurrence;
  // Tracker-sync link (HEAP-74): set when this task mirrors an external issue.
  // Empty for locally-created tasks. Used to route status pushes and to match
  // pulled issues back to their task on the next sync.
  QString externalId;        // e.g. GitHub issue number as a string
  QString externalUrl;       // issue web URL
  QString externalProvider;  // "github" | "jira" | "gitlab"
  // Planning fields (HEAP-124).
  QVector<Label> labels;
  int estimateMinutes = 0;
  bool someday = false;  // parked: never surfaces in a "scheduled today" view
  QString assignee;      // tracker-supplied owner, empty for local tasks
  // What the tracker says about this issue (HEAP-117). Default for a local task.
  ExternalMeta externalMeta;
  // Manual position within a status column. Fractional on purpose: dropping a
  // card between two others takes the midpoint of their ranks, so one task is
  // rewritten per move instead of renumbering the column. That matters for
  // sync — JsonMerger resolves conflicts per element, so a renumber would turn
  // one reorder into a conflict on every card in the column.
  double rank = 0.0;
  // Dependencies this task blocks. See TaskLink.
  QVector<TaskLink> links;
  // Files attached to the task, in the order they were attached.
  QVector<Attachment> attachments;
  // What the developer keeps on top of the task, which no sync path writes
  // (ADR 0001, APP-244). Read priority and due date through
  // heap::local::effectivePriority/effectiveDue, never straight from here.
  TaskLocal local;
  // Keys of the task object this build does not read, carried through a save
  // untouched (PLAT-15), the way Profile::extra is. Only filled for a document
  // at the current schema.
  QJsonObject extra;

  bool operator==(const Task&) const = default;
};

struct CalEvent {
  QString id;
  QString title;
  QString type;    // none/standup/oneone/sync/focus
  double start{};  // hour 0..24
  double end{};
  QString attendees;
  QDate date;
  QString taskId;     // optional link to task in same profile
  QString profileId;  // optional attribution to a feature profile (empty = global)
  QString context;    // free-form context label rendered before the title in calendar views
  // An all-day event has no hours: start/end are forced to 0/24 and carry no
  // meaning. `endDate` is the last day the event covers — invalid or equal to
  // `date` for the ordinary single-day case, later for a multi-day event or a
  // timed one that crosses midnight. See src/cal/EventSpan.h for the
  // invariants every write path puts these through.
  bool allDay{};
  QDate endDate;
  // Recurrence. A master carries `rrule` (an RRULE body — see src/cal/RRule.h)
  // and the dates of the occurrences the user deleted one by one. An override
  // is a separate event that names the master it replaces and the occurrence
  // date it stands in for, which is how "edit only this one" is stored without
  // materialising the whole series.
  QString rrule;
  QVector<QDate> exdates;
  QString masterId;
  QDate originalDate;
  // The IANA zone the wall-clock fields above are written in. Empty — the
  // common case — means floating local time: the event is at 10:00 wherever
  // the user is. A series imported from another zone keeps it, so each
  // occurrence is converted on its own day and a New York meeting follows
  // New York's DST, not the viewer's. Exdates and an override's originalDate
  // are dates in this zone too. See src/cal/Occurrences.h.
  QString tz;
  // Where, what for and a link to join. `context` stays the short label drawn
  // before the title; it used to double as the location.
  QString location;
  QString notes;
  QString url;
  // Minutes before the start to remind, per event. kReminderDefault uses the
  // notifications setting; kReminderOff never reminds.
  int reminderMinutes = -1;
  // Keys this build does not read, carried through a save (PLAT-15).
  QJsonObject extra;

  static constexpr int kReminderDefault = -1;
  static constexpr int kReminderOff = -2;

  bool operator==(const CalEvent&) const = default;
};

// Tracker-native issue key for a task: Jira stores "PROJ-123" verbatim, the
// issue-number trackers store a bare number that reads as "#123". A number
// pulled across projects is ambiguous on its own, so it is qualified with the
// repo it came from ("web#42"). Empty for locally-created tasks.
QString externalKeyOf(const Task& t);

// A merge request or pull request mirrored from a forge (APP-242): read-only,
// never written back, its column the request's stage unless the user lets
// such cards move.
bool isReviewItem(const Task& t);

// Everything the ticket UI needs about a task's tracker link, as one map:
// provider, key, url, assignee, author, type, project, milestone, comment count
// and the tracker's created/updated timestamps. Empty for a local task, so a
// QML delegate can test it to decide whether to render the ticket strip at all.
QVariantMap ticketToVariant(const Task& t);

// Labels across the QML boundary: a list of { id, color } maps.
QVariantList labelsToVariant(const QVector<Label>& labels);
QVector<Label> labelsFromVariant(const QVariantList& list);

struct Person {
  QString id;
  QString name;
  QString role;
  QString question;
  // todo (to write) / pinged (wrote, waiting for the reply) / replied / idle.
  QString state;
  QColor color;
  QDateTime stateAt;  // when `state` last moved; invalid before 0.8.1
  // When to write (dogfood 2026-10-20): an optional day, and with a time a
  // notification at that moment. Invalid = none. Only a "todo" ask has one.
  QDateTime remindAt;
  bool remindHasTime{};
  // Keys this build does not read, carried through a save (PLAT-15).
  QJsonObject extra;

  bool operator==(const Person&) const = default;
};

// One profile = one feature workspace: its own tasks, people,
// kanban-statuses and docs. Events live globally on AppController so the
// calendar can show meetings/focus blocks from every profile at once;
// CalEvent.profileId records the optional feature attribution.
// One note.
//
// Notes were a single markdown blob per profile: everything anybody ever wrote
// lived in one document, and the only structure available was how far down you
// had scrolled. A note is now a thing with a name that can be found, pinned,
// filed and linked to.
//
// `folder` is a plain path ("meetings/2026"), not a tree of objects — Obsidian
// stores folders as directories and a flat string is what maps onto a directory
// on import and export without a second representation to keep in sync.
struct Note {
  QString id;
  QString title;
  QString folder;
  QString body;
  bool pinned{};
  QDateTime created;
  QDateTime updated;
  // Where the note last met a folder of .md files (see notes/MdVault.h): the
  // file's path relative to that folder, and a hash of the title and body the
  // file held then. The hash is what tells a re-import which side changed —
  // without it an edit made in heap since the last import was overwritten by
  // the older file. Empty for a note that never left heap.
  QString vaultPath;
  QString vaultHash;
  // The frontmatter lines heap has no field for (Obsidian's tags, aliases, …),
  // kept verbatim so an export writes them back instead of stripping them.
  QString frontmatter;

  bool operator==(const Note&) const = default;
};

// One page of documentation.
//
// Docs was a catalog: links, snippets and contacts, all of them one line long.
// There was nowhere to write the paragraph explaining why the link matters,
// which is the thing people actually need from a team's docs.
//
// A page is markdown on the same stack notes use. `parentId` makes the tree —
// a plain parent pointer rather than nested children, because a flat list is
// what a QAbstractListModel can carry and what survives a merge per element.
// `rank` orders siblings, fractionally, for the same reason Task::rank does:
// moving one page rewrites one page.
struct DocPage {
  QString id;
  QString parentId;
  QString title;
  QString body;
  double rank{};
  QDateTime created;
  QDateTime updated;

  bool operator==(const DocPage&) const = default;
};

// One move of a task between columns, as it happened (WEAK PECAP: the
// Monday recap lists what moved last week). Kept per profile, newest last,
// pruned to a few months.
struct StatusChange {
  QString taskId;
  QString from;
  QString to;
  QDateTime at;

  bool operator==(const StatusChange&) const = default;
};

struct Profile {
  QString id;     // slug, unique
  QString name;   // human-readable
  QString color;  // accent ("#5cc2dd")
  QDateTime createdAt;
  QVector<Task> tasks;
  QVector<Person> people;
  QVariantList statuses;  // [{ id, name, color }]
  QString docsState;      // JSON blob, same shape as AppController::docsState
  // The blob notes used to be. Kept as the body of the active note so that
  // everything reading `notesState` keeps working; the notes themselves live
  // in `notes`.
  QString notesState;
  QVector<Note> notes;
  QString activeNoteId;
  QVector<DocPage> docPages;
  QString activeDocPageId;
  // Named filter snapshots, in sidebar order (profile key `savedViews`).
  QVector<heap::savedviews::SavedView> savedViews;
  // Status moves, oldest first (profile key `statusLog`; absent = none).
  QVector<StatusChange> statusLog;
  // Tasks waiting on someone's reply (APP-158; profile key `waitingOn`,
  // absent = none). See safety/WaitingOn.h.
  QVector<WaitingOn> waitingOn;
  // Keys of the profile object this build does not read, carried through a
  // save untouched (PLAT-26). Only filled for a document at the current schema.
  QJsonObject extra;
};

// Notes, without their bodies.
//
// A note list showing fifty notes would otherwise carry fifty documents across
// the QML boundary on every repaint. The editor asks for one body at a time
// through AppController::noteBody().
class NoteModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by AppController")
 public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    TitleRole,
    FolderRole,
    PinnedRole,
    CreatedRole,
    UpdatedRole,
    // A one-line preview, so a list can show what a note is about without
    // holding the whole thing.
    ExcerptRole,
  };

  explicit NoteModel(QObject* parent = nullptr) : QAbstractListModel(parent) {
  }

  int rowCount(const QModelIndex& = {}) const override {
    return m_items.size();
  }

  QVariant data(const QModelIndex& idx, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  Q_INVOKABLE int roleOf(const QString& name) const;

  void reset(QVector<Note> items);

  const QVector<Note>& items() const {
    return m_items;
  }

  Q_INVOKABLE int indexOfId(const QString& id) const;
  void upsert(const Note& n);
  // Undo puts a deleted note back in its old row, not at the end of the list.
  void insertAt(int row, const Note& n);
  void removeById(const QString& id);

 private:
  QVector<Note> m_items;
};

// Doc pages, without their bodies — for the same reason NoteModel omits them.
class DocPageModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by AppController")
 public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    ParentIdRole,
    TitleRole,
    RankRole,
    CreatedRole,
    UpdatedRole,
    ExcerptRole,
  };

  explicit DocPageModel(QObject* parent = nullptr) : QAbstractListModel(parent) {
  }

  int rowCount(const QModelIndex& = {}) const override {
    return m_items.size();
  }

  QVariant data(const QModelIndex& idx, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  Q_INVOKABLE int roleOf(const QString& name) const;

  void reset(QVector<DocPage> items);

  const QVector<DocPage>& items() const {
    return m_items;
  }

  Q_INVOKABLE int indexOfId(const QString& id) const;
  void upsert(const DocPage& p);
  // Undo restores a page where it was, not at the end: the tree is ordered by
  // rank, but the model's own row order is what a reset would otherwise churn.
  void insertAt(int row, const DocPage& p);
  void removeById(const QString& id);

 private:
  QVector<DocPage> m_items;
};

class TaskModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by AppController")
 public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    TitleRole,
    DescRole,
    PriorityRole,
    StatusRole,
    DeadlineRole,
    BranchRole,
    StatusChangedAtRole,
    ArchivedRole,
    BlockedStuckRole,
    PrStateRole,
    PrNumberRole,
    PrUrlRole,
    GitAheadRole,
    GitBehindRole,
    RecentCommitsRole,
    TrackedSecondsRole,
    IsTimingRole,
    RecurrenceRole,
    // HEAP-115 / HEAP-124: the datetime pair behind DeadlineRole, plus planning
    // fields. DeadlineRole stays a QDate (dueAt's date) so existing views keep
    // their all-day arithmetic.
    ScheduledAtRole,
    DueAtRole,
    HasTimeRole,
    EstimateMinutesRole,
    SomedayRole,
    DeferStateRole,
    // HEAP-140: read-only reflections of the tracker link.
    ExternalProviderRole,
    ExternalUrlRole,
    ExternalKeyRole,
    LabelsRole,
    AssigneeRole,
    // HEAP-117. Twelve QML files read roles by their numeric offset from
    // Qt::UserRole, so a new role only ever goes on the end — inserting one
    // above silently reassigns every offset below it.
    TicketRole,
    SearchTextRole,
    // Manual order within a column, and the dependency links.
    RankRole,
    BlocksRole,
    // { done, total } for the task list in the description, so a card can show
    // 2/5 without every delegate re-scanning the text in JS.
    ChecklistRole,
    // Schema v10 split hasTime per field. HasTimeRole ("hasTime") keeps
    // answering for the deadline, which is what every reader of it meant.
    ScheduledHasTimeRole,
    DueHasTimeRole,
    // How many files are attached, for the card's paperclip chip.
    AttachmentCountRole,
    // Whose move it is on the linked PR (APP-156): "mine" | "theirs" | "",
    // and why (an I18n key suffix). Runtime only, like the rest of GitInfo.
    PrMoveRole,
    PrMoveReasonRole,
    // What the card shows of its local layer (APP-237…241): notes / draft /
    // my tags / my priority or due / "changed in the tracker" / related.
    LocalRole,
  };

  explicit TaskModel(QObject* parent = nullptr) : QAbstractListModel(parent) {
  }

  // ID set the controller marks as "blocked too long"; consumed by
  // BlockedStuckRole so TaskCard can paint a warning border.
  void setBlockedStuckIds(const QSet<QString>& ids);
  void setArchived(const QString& id, bool archived);
  void stampStatusChange(const QString& id);
  // Time tracking (HEAP-78). startTiming marks a task running (stopping any
  // other running task); stopTiming folds the elapsed session into
  // trackedSeconds. Both emit dataChanged for the timing roles.
  void startTiming(const QString& id);
  void stopTiming(const QString& id);

  // Push live git-derived data for a task (PR state, ahead/behind). Only
  // keys present in \p info are updated; others stay as-is. Emits
  // dataChanged for the matching row across all git roles.
  void setGitInfoForId(const QString& id, const QVariantMap& info);
  // The PR facts the task document shows (DG-061): prState, prNumber, prUrl,
  // prChecks ("passing" | "failing" | "pending" | ""). Empty when none.
  QVariantMap gitFactsFor(const QString& id) const;
  void clearAllGitInfo();

  // Told about every single-task change made through setStatus / upsert /
  // insertAt, with the task as it was (null for a new row) and as it is now —
  // what the task history (APP-165) is built from. reset() is not reported:
  // a load or a profile switch is not something that happened to a task.
  using ChangeObserver = std::function<void(const Task* before, const Task& after)>;

  void setChangeObserver(ChangeObserver observer) {
    m_observer = std::move(observer);
  }

  // Runtime half of a card's sync state (APP-163): a status write in flight
  // and the reason the tracker gave for refusing the last one. Not saved —
  // the refusal itself is (ExternalMeta::unsyncedStatus), its wording is not.
  // Read back through TicketRole as `syncState` / `syncError`.
  void setPushRuntime(const QString& id, bool pushing, const QString& error);

  bool isPushing(const QString& id) const {
    return m_pushing.contains(id);
  }

  int rowCount(const QModelIndex& = {}) const override {
    return m_items.size();
  }

  QVariant data(const QModelIndex& idx, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // The number behind a role name. QAbstractItemModel::roleNames() is not
  // invokable from QML, so anything building a plain JS list out of this model
  // has had to count offsets from Qt::UserRole by hand — and a miscount reads a
  // different field silently, which is how a filter on `hasTime` came to be
  // reading the board's rank. Returns -1 for a name that does not exist.
  Q_INVOKABLE int roleOf(const QString& name) const;

  void reset(QVector<Task> items);

  const QVector<Task>& items() const {
    return m_items;
  }

  int indexOfId(const QString& id) const;
  // SearchTextRole without the QVariant: the row's cached lowercase haystack.
  // TaskFilterProxy reads it once per row per keystroke per column.
  const QString& searchTextAt(int row) const;
  static QString searchTextOf(const Task& t);
  // Moves a task to `status`. statusChangedAt is stamped with the current time
  // unless `changedAt` is given — undo passes the original back so restoring a
  // task does not look like a fresh move (the "stuck in this column" badge is
  // computed from that timestamp).
  //
  // `rank`, when given, lands in the same change: a card dropped into another
  // column then enters that column's proxy where it belongs, instead of at its
  // old rank and re-sorting the whole column a moment later (APP-203).
  void setStatus(const QString& id, const QString& status, const QDateTime& changedAt = {}, std::optional<double> rank = {});
  void upsert(const Task& t);
  void insertAt(int row, const Task& t);
  void removeById(const QString& id);
  // Re-rank many tasks with one dataChanged: rebalancing a column one upsert
  // at a time re-sorted the board proxy once per card (seconds at 1k tasks).
  void setRanks(const QHash<QString, double>& ranks);

 private:
  struct GitInfo {
    QString prState;
    QString prUrl;
    QString prMove;
    QString prMoveReason;
    QString prChecks;
    // The review facts behind a card's merge dot (dogfood 2026-10-20).
    QString prReviewDecision;
    QString prMergeable;
    QStringList prReviewRequests;
    // What prChecks is about and when it was looked at (dogfood 2026-10-21):
    // the request's head the result speaks for, its head as that look saw
    // it, and the look's time. A result for an older push is not shown as
    // the request's CI.
    QString prChecksHead;
    QString prHeadSha;
    QDateTime prSeenAt;
    bool prDraft = false;
    int prNumber = 0;
    int ahead = 0;
    int behind = 0;
    QVariantList recentCommits;  // [ {sha, subject}, … ] mentioning this task
  };

  QVector<Task> m_items;
  QSet<QString> m_blockedStuck;
  QHash<QString, GitInfo> m_git;  // not persisted; runtime only
  ChangeObserver m_observer;
  QSet<QString> m_pushing;               // task ids with a push in flight
  QHash<QString, QString> m_pushErrors;  // task id → the tracker's last refusal
  // indexOfId's id→row map, rebuilt lazily whenever rows are added, removed or
  // replaced wholesale. Mutable so the lookup can stay const.
  mutable QHash<QString, int> m_index;
  mutable bool m_indexDirty = true;
  // searchTextAt()'s per-row haystacks, parallel to m_items. A null entry has
  // not been built yet (or was invalidated by an upsert). If the sizes ever
  // disagree the whole cache is dropped and rebuilt lazily, so a mutation path
  // that forgets to maintain it costs speed, never correctness.
  mutable QVector<QString> m_searchCache;
};

class EventModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by AppController")
 public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    TitleRole,
    TypeRole,
    StartRole,
    EndRole,
    AttendeesRole,
    DateRole,
    TaskIdRole,
    ProfileIdRole,
    ContextRole,
    // Appended, never inserted: the calendar QML reads these by numeric offset
    // from Qt::UserRole, so renumbering an existing role silently repoints
    // every view at the wrong field.
    AllDayRole,
    EndDateRole,
    RRuleRole,
    MasterIdRole,
    // The date of the occurrence being shown. Equal to DateRole for a
    // stored event; the expansion sets it per occurrence.
    OccurrenceDateRole,
  };

  explicit EventModel(QObject* parent = nullptr) : QAbstractListModel(parent) {
  }

  int rowCount(const QModelIndex& = {}) const override {
    return m_items.size();
  }

  QVariant data(const QModelIndex& idx, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void reset(QVector<CalEvent> items);

  const QVector<CalEvent>& items() const {
    return m_items;
  }

  int indexOfId(const QString& id) const;
  void upsert(const CalEvent& e);
  void insertAt(int row, const CalEvent& e);
  void removeById(const QString& id);
  void detachTask(const QString& taskId);
  void setTaskId(const QString& eventId, const QString& taskId);

 private:
  QVector<CalEvent> m_items;
};

class PersonModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by AppController")
 public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    NameRole,
    RoleRole,
    QuestionRole,
    StateRole,
    ColorRole,
    StateAtRole,
    RemindAtRole,
    RemindHasTimeRole,
  };

  explicit PersonModel(QObject* parent = nullptr) : QAbstractListModel(parent) {
  }

  int rowCount(const QModelIndex& = {}) const override {
    return m_items.size();
  }

  QVariant data(const QModelIndex& idx, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void reset(QVector<Person> items);

  const QVector<Person>& items() const {
    return m_items;
  }

  int indexOfId(const QString& id) const;
  void cycleState(const QString& id);
  void setState(const QString& id, const QString& state);
  void setRemindAt(const QString& id, const QDateTime& at, bool hasTime);
  void upsert(const Person& p);
  void insertAt(int row, const Person& p);
  void removeById(const QString& id);
  int todoCount() const;

 private:
  QVector<Person> m_items;
};

// The People rail, filtered down to the people something is actually pending
// on. A Mattermost sync imports every colleague ever direct-messaged as
// "idle", and rendering those turned the rail into a contact list nobody
// asked for: the rail is a to-do list, so it starts empty and only carries
// someone once they are on it. Everyone else stays reachable through the
// picker, @-autocomplete and the command palette, which all read the
// unfiltered PersonModel.
class ActivePeopleModel : public QSortFilterProxyModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by AppController")
 public:
  explicit ActivePeopleModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {
  }

  // Role names and rowCount() come straight from QSortFilterProxyModel, which
  // forwards the source's — PersonModel's roles apply to a delegate unchanged.
 protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
    const QAbstractItemModel* src = sourceModel();
    if(src == nullptr) {
      return false;
    }
    return src->index(row, 0, parent).data(PersonModel::StateRole).toString() != QLatin1String("idle");
  }
};
