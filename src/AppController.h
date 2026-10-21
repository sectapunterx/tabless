#pragma once

#include "Models.h"

#include "board/Rank.h"
#include "capture/CaptureParse.h"
#include "history/TaskHistory.h"
#include "integrations/CiNotify.h"
#include "integrations/FetchBackoff.h"
#include "integrations/LiveSync.h"
#include "integrations/SyncHealth.h"
#include "notify/EventLog.h"
#include "notify/NotifyPayload.h"
#include "profile/ProfileMove.h"
#include "safety/EndOfDay.h"
#include "undo/UndoStack.h"
#include "update/FeatureBuild.h"

#include <QDate>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <qqmlregistration.h>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

class QNetworkAccessManager;
class QThread;

namespace heap::cal {
class OutlookWatch;
}

namespace heap::chrono {
class ChronoParser;
}

namespace heap::query {
class TaskQuery;
}

namespace heap::git {
class GitWatcher;
class ForgeApi;
class MrThreadWatch;
struct ForgeAuth;
}

namespace heap::notify {
class NotificationCenter;
}

namespace heap::platform {
class GlobalHotkey;
}

namespace heap::notes {
struct VaultPlanItem;
}

namespace heap::update {
class Updater;
}

namespace heap::storage {
class AsyncSaver;
struct SaveOutcome;
}  // namespace heap::storage

namespace heap::integrations {
class IntegrationProvider;
class MattermostClient;
class SecretStore;
struct TrackerAttachment;
struct TrackerImageState;
struct ExternalContact;
struct ExternalTask;
struct ExternalStatusChange;
struct ExternalComment;
struct ExternalStatus;
}  // namespace heap::integrations

class AppController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(TaskModel* tasks READ tasks CONSTANT)
  Q_PROPERTY(EventModel* events READ events CONSTANT)
  Q_PROPERTY(PersonModel* people READ people CONSTANT)
  // The People rail's model: `people` minus everyone in the "idle" state. See
  // ActivePeopleModel.
  Q_PROPERTY(ActivePeopleModel* activePeople READ activePeople CONSTANT)
  Q_PROPERTY(QVariantList statuses READ statuses NOTIFY statusesChanged)
  // status id → task count, in one pass. The rail and the top bar read this
  // instead of calling countByStatus once per badge.
  Q_PROPERTY(QVariantMap statusCounts READ statusCounts NOTIFY statusCountsChanged)
  // task id → title for every task in the profile, archived included. The
  // rendered markdown shows a "#APP-12" mention as its title (APP-121).
  Q_PROPERTY(QVariantMap taskTitles READ taskTitles NOTIFY taskTitlesChanged)
  // task id -> the key a card shows (displayKey) and -> its column's stage
  // (statusCategory): the rich editor draws a "#APP-12" as a pill with the
  // status ring and the key (a-05). Apart from the titles so a status drag
  // redraws only the rings, not every open note's text.
  Q_PROPERTY(QVariantMap taskKeys READ taskKeys NOTIFY taskKeysChanged)
  Q_PROPERTY(QVariantMap taskStages READ taskStages NOTIFY taskStagesChanged)
  // The active profile's saved views, in sidebar order: [{id, name, query,
  // priorities, sort, archived, showDone, view, problems}] — `problems` is what
  // the search box would flag in the query (a deleted column, a typo).
  Q_PROPERTY(QVariantList savedViews READ savedViews NOTIFY savedViewsChanged)
  // view id → how many tasks opening it shows. One pass for all views, cached
  // until a task, a column, the day or the views change.
  Q_PROPERTY(QVariantMap savedViewCounts READ savedViewCounts NOTIFY savedViewCountsChanged)
  // The applied saved view's stored query, "" when none (SavedViewsHost sets
  // it). A search that is exactly this compiles strictly: a `status:` on a
  // deleted column matches nothing, in every view and count (IDIOT-TASKS-11).
  Q_PROPERTY(QString strictQuery READ strictQuery WRITE setStrictQuery NOTIFY strictQueryChanged)
  // Moves at local midnight, on resume and on a clock or zone change: every
  // "today" in the UI binds to this, so after midnight T goes to the new day.
  Q_PROPERTY(QDate today READ today NOTIFY todayChanged)
  // provider id → { name, icon, color } for the badge on a mirrored ticket
  // (HEAP-117). Constant and cheap: a board delegate reads this per card, and
  // integrationCatalog() rebuilds every provider's field list on each call.
  Q_PROPERTY(QVariantMap providerBadges READ providerBadges CONSTANT)

  Q_PROPERTY(QDate selectedDate READ selectedDate WRITE setSelectedDate NOTIFY selectedDateChanged)
  Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
  Q_PROPERTY(QString density READ density WRITE setDensity NOTIFY densityChanged)
  Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
  Q_PROPERTY(bool pseudoLocale READ pseudoLocale CONSTANT)
  Q_PROPERTY(QString currentView READ currentView WRITE setCurrentView NOTIFY currentViewChanged)
  // The sidebar place the current view belongs to (APP-258).
  Q_PROPERTY(QString currentSection READ currentSection NOTIFY currentViewChanged)
  // What changed in the shell for someone coming from 0.7 (APP-258): said
  // once, then cleared by ackShellNotice(). Empty for a new install.
  Q_PROPERTY(QString shellNotice READ shellNotice NOTIFY shellNoticeChanged)
  Q_PROPERTY(QString focusedStatus READ focusedStatus NOTIFY focusedStatusChanged)

  Q_PROPERTY(int workdayStart READ workdayStart WRITE setWorkdayStart NOTIFY workdayChanged)
  Q_PROPERTY(int workdayEnd READ workdayEnd WRITE setWorkdayEnd NOTIFY workdayChanged)

  Q_PROPERTY(QString crumbProject READ crumbProject WRITE setCrumbProject NOTIFY crumbProjectChanged)
  Q_PROPERTY(QString crumbUser READ crumbUser WRITE setCrumbUser NOTIFY crumbUserChanged)

  Q_PROPERTY(QString docsState READ docsState WRITE setDocsState NOTIFY docsStateChanged)
  Q_PROPERTY(QString notesState READ notesState WRITE setNotesState NOTIFY notesStateChanged)
  // The notes of the active profile, without their bodies. `notesState` is the
  // body of whichever one is open, so everything that reads it keeps working.
  Q_PROPERTY(NoteModel* notes READ notes CONSTANT)
  Q_PROPERTY(QString activeNoteId READ activeNoteId WRITE setActiveNoteId NOTIFY activeNoteChanged)
  Q_PROPERTY(QString appSettingsJson READ appSettingsJson WRITE setAppSettingsJson NOTIFY appSettingsJsonChanged)
  // Trackers whose "change the status in …" switch is on (APP-243). Every
  // other tracker only ever gets read.
  Q_PROPERTY(QStringList trackerWriteProviders READ trackerWriteProviders NOTIFY appSettingsJsonChanged)
  Q_PROPERTY(bool hasPendingUndo READ hasPendingUndo NOTIFY pendingUndoChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY pendingUndoChanged)

  // ---- Onboarding (first run) ----
  // welcomeSeen: the welcome dialog has been shown/dismissed at least once.
  // demoActive: the profile is still the seeded demo, so the "this is demo
  // data" banner should offer to start fresh. Both persist in the settings blob.
  Q_PROPERTY(bool welcomeSeen READ welcomeSeen NOTIFY onboardingChanged)
  Q_PROPERTY(bool demoActive READ demoActive NOTIFY onboardingChanged)
  // An update to a new release line since the last run (R2-054): the
  // "What's new" card is due once. ackWhatsNew() puts it away.
  Q_PROPERTY(bool whatsNewDue READ whatsNewDue NOTIFY onboardingChanged)

  Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
  // Calendars read from a link (APP-118), with how their last fetch went.
  Q_PROPERTY(QVariantList calendarSubscriptions READ calendarSubscriptions NOTIFY calendarSubscriptionsChanged)
  Q_PROPERTY(QString activeProfileId READ activeProfileId WRITE setActiveProfileId NOTIFY activeProfileChanged)

  Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
  // Which keymap the catalogue's defaults come from (dogfood 2026-10-19):
  // "classic" (arrows, Enter, Del, F2, Ctrl / Alt chords; the default of a new
  // install) or "vim" (hjkl, g / y / z / c sequences, dd). The user's own
  // rebindings stay on top of either.
  Q_PROPERTY(QString keymapScheme READ keymapScheme WRITE setKeymapScheme NOTIFY keymapSchemeChanged)
  Q_PROPERTY(QStringList blockedStuckIds READ blockedStuckIds NOTIFY blockedStuckChanged)

  // ---- Multi-select ----
  Q_PROPERTY(QStringList selectedTaskIds READ selectedTaskIds NOTIFY selectedTaskIdsChanged)
  Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectedTaskIdsChanged)

  // Compile-time flag — true for Debug / RelWithDebInfo builds. QML uses
  // it to surface the developer-only "show unimplemented" toggle.
  Q_PROPERTY(bool debugBuild READ debugBuild CONSTANT)

  // Runtime facts for Settings → About (so nothing is hardcoded / stale).
  Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
  Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
  Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
  // Which build this is (dogfood 2026-10-20 #10): "" for stable, "feature" or
  // "unstable" for a channel build, with the commit and the day it was built.
  Q_PROPERTY(QString buildChannel READ buildChannel CONSTANT)
  Q_PROPERTY(QString buildSha READ buildSha CONSTANT)
  Q_PROPERTY(QString buildDate READ buildDate CONSTANT)

  // ---- Auto-update (HEAP-63) ----
  // Human-readable result of the last update check, shown in Settings → About:
  // "" (idle), a "checking" string, "up to date", or "update available: vX".
  Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateStatusChanged)
  // In-app update (APP-125). canInstall: this copy is one heap can update
  // itself (installer, portable zip, heap.app, AppImage) and the release has
  // its package; otherwise "Download" opens the release page as before.
  // phase: "" | "downloading" | "verifying" | "ready" | "installing" | "error".
  // sha256: the checksum the package was verified against, once "ready".
  Q_PROPERTY(bool updateCanInstall READ updateCanInstall NOTIFY updateStatusChanged)
  Q_PROPERTY(QString updatePhase READ updatePhase NOTIFY updateStatusChanged)
  Q_PROPERTY(double updateProgress READ updateProgress NOTIFY updateProgressChanged)
  Q_PROPERTY(QString updateSha256 READ updateSha256 NOTIFY updateStatusChanged)
  // The feature build beside this one: download, open, update, remove.
  Q_PROPERTY(heap::update::FeatureBuild* featureBuild READ featureBuild CONSTANT)
  // A Flatpak is updated by its software centre, and Flathub forbids an app
  // that checks for or offers its own updates: no check, no download, no
  // update UI, only a line saying where updates come from.
  Q_PROPERTY(bool updatesFromStore READ updatesFromStore CONSTANT)

  // Per tracker: { offline: bool, outOfScope: int }. `offline` is set while
  // the token endpoint or the tracker cannot be reached and heap is retrying
  // — the card stays connected. `outOfScope` counts cards left behind by a
  // filter change (repo, JQL…), which the card offers to archive.
  Q_PROPERTY(QVariantMap integrationStates READ integrationStates NOTIFY integrationStatesChanged)
  // A tracker or directory pull is out and has not answered yet (APP-186).
  // The header shows it only once it has taken long enough to notice.
  Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
  // Cards a sync brought in that the user has not looked at yet (APP-180):
  // read isTaskUnseen(id) with this in the binding, so a card follows.
  Q_PROPERTY(int unseenRevision READ unseenRevision NOTIFY unseenTasksChanged)
  // The cards the latest sync brought in, which `is:new` filters to.
  Q_PROPERTY(QStringList syncNewTaskIds READ syncNewTaskIds NOTIFY syncNewTaskIdsChanged)
  // The event log (APP-187), newest first: { id, at, kind, message, taskIds,
  // route, count }. This session only, at most 100 entries.
  Q_PROPERTY(QVariantList eventLog READ eventLog NOTIFY eventLogChanged)

  // ---- Git focus banner ----
  Q_PROPERTY(QString focusedTaskId READ focusedTaskId NOTIFY focusedGitChanged)
  Q_PROPERTY(QString focusedBranch READ focusedBranch NOTIFY focusedGitChanged)
  Q_PROPERTY(QString focusedRepo READ focusedRepo NOTIFY focusedGitChanged)
  Q_PROPERTY(QVariantMap focusedRepoState READ focusedRepoState NOTIFY focusedGitChanged)
  Q_PROPERTY(bool focusedBannerDismissed READ focusedBannerDismissed NOTIFY focusedGitChanged)
  // Settings → Git → "Show whose move" (APP-156). On by default: it is a fact
  // read off the PR, not a nudge.
  Q_PROPERTY(bool showWhoseMove READ showWhoseMove NOTIFY showWhoseMoveChanged)
  // The one-time coach mark waiting for its anchor (src/hints/CoachMarks.h):
  // {id, …what that one needs, e.g. taskId}; empty when there is none.
  Q_PROPERTY(QVariantMap coachMark READ coachMark NOTIFY coachMarkChanged)

  // ---- Storage health (PLAT-1/4/5) ----
  // "ok", "unreadable" (state.json exists but could not be opened: read-only
  // session, never saved over), "tooNew" (written by a newer heap: read-only),
  // or "writeFailed" (the last save did not reach disk; retried with backoff).
  // storageMessage is the localized banner text, reason included.
  Q_PROPERTY(QString storageState READ storageState NOTIFY storageStateChanged)
  Q_PROPERTY(QString storageMessage READ storageMessage NOTIFY storageStateChanged)
  // The write failure's cause in a few words ("на диске C: нет места"), or
  // "" (R2-037); the strip leads with it.
  Q_PROPERTY(QString storageReason READ storageReason NOTIFY storageStateChanged)
  // The file names the message carries (the damaged copy, the backup shown).
  Q_PROPERTY(QStringList storageArgs READ storageArgs NOTIFY storageStateChanged)
  // Where a damaged state.json stops parsing, as a byte offset; -1 when it
  // parsed but had the wrong shape, or nothing is damaged (R4-104).
  Q_PROPERTY(int storageDamagedAt READ storageDamagedAt NOTIFY storageStateChanged)
  Q_PROPERTY(QVariantList syncSources READ syncSources NOTIFY integrationHealthChanged)
  // Features paused after failing again and again (net/FeatureGate.h,
  // 2026-10-21): one row each, for the problems strip.
  Q_PROPERTY(QVariantList pausedFeatures READ pausedFeatures NOTIFY pausedFeaturesChanged)
  Q_PROPERTY(QVariantMap keychainProblem READ keychainProblem NOTIFY keychainProblemChanged)
  // The previous window session did not end cleanly (R3-018): its marker
  // file was still there at start. lastSaveTime is when state.json was last
  // written, so the launch card can say the data is intact as of then.
  Q_PROPERTY(bool lastExitUnclean READ lastExitUnclean NOTIFY lastExitUncleanChanged)
  Q_PROPERTY(QDateTime lastSaveTime READ lastSaveTime NOTIFY lastExitUncleanChanged)

 public:
  explicit AppController(QObject* parent = nullptr);
  ~AppController() override;

  // A command-line run (`heap add`, `heap done` with no window open, see
  // src/cli): the same models and save path, but no tray icon, global
  // hotkeys, git watcher, tracker sync, calendar fetches or update checks.
  // Set before the controller is constructed.
  static void setHeadless(bool headless) {
    s_headless = headless;
  }

  static bool isHeadless() {
    return s_headless;
  }

  bool lastExitUnclean() const {
    return m_lastExitUnclean;
  }

  QDateTime lastSaveTime() const {
    return m_lastSaveTime;
  }

  // The person answered the launch card: it does not come back this session.
  Q_INVOKABLE void dismissUncleanExit();
  // Tests and the capture harness only (no-op outside QStandardPaths test
  // mode): pretend the last session crashed, saved at `savedAt`.
  Q_INVOKABLE void simulateUncleanExitForTest(const QDateTime& savedAt);

  // `heap done` sent to the open window goes through moveTask() like a drag
  // does. The CLI executor mutes the sound palette (APP-177) around it: only
  // the user's own action in this window makes a sound.
  void setSoundMuted(bool muted) {
    m_soundMuted = muted;
  }

  // Settings → Sound: the "done" sound once at `volume` (0–100), so the user
  // hears what the slider set. Asked for, so quiet hours do not hold it.
  Q_INVOKABLE void previewSound(int volume);

  Q_INVOKABLE void flushSave();

  QString storageState() const {
    return m_storageState;
  }

  QString storageMessage() const {
    return m_storageMessage;
  }

  QString storageReason() const {
    return m_storageReason;
  }

  QStringList storageArgs() const {
    return m_storageSpec.isEmpty() ? QStringList() : m_storageSpec.first().second;
  }

  int storageDamagedAt() const {
    return m_storageDamagedAt;
  }

  // The strip's "Save a copy elsewhere…" (R2-037): the whole state as it is
  // in memory, written to `fileUrl`. True when it landed.
  Q_INVOKABLE bool saveStateCopyTo(const QUrl& fileUrl);

  // The banner's Retry: re-reads an unreadable state.json (and loads it), or
  // writes a failed save again now.
  Q_INVOKABLE void retryStorage();
  // The banner's Dismiss, for the two states that only report what happened
  // at startup ("recovered" from a backup, "damaged" with none): nothing is
  // wrong any more, so the user may put the message away (PLAT-6).
  Q_INVOKABLE void dismissStorageNotice();

  TaskModel* tasks() {
    return &m_tasks;
  }

  EventModel* events() {
    return &m_events;
  }

  PersonModel* people() {
    return &m_people;
  }

  ActivePeopleModel* activePeople() {
    return &m_activePeople;
  }

  QVariantList statuses() const {
    return m_statuses;
  }

  QDate today() const {
    return m_today;
  }

  QDate selectedDate() const {
    return m_selectedDate;
  }

  void setSelectedDate(const QDate& d);

  QString theme() const {
    return m_theme;
  }

  void setTheme(const QString& t);

  QString density() const {
    return m_density;
  }

  void setDensity(const QString& d);

  QString language() const {
    return m_language;
  }

  // HEAP_LANG=pseudo: the UI in a stretched, accented English (APP-189), to
  // find text cut short by a fixed width. For development; no setting.
  bool pseudoLocale() const {
    return qEnvironmentVariable("HEAP_LANG") == QLatin1String("pseudo");
  }

  void setLanguage(const QString& v);

  // Resolve a UI string for the current language. `key` is a stable
  // identifier (e.g. "task.created"); returns the EN form as a last-resort
  // fallback if the key is missing in the active table.
  Q_INVOKABLE QString tr_(const QString& key) const;
  // The same string in a given language, whatever the UI's is.
  QString trIn_(const QString& key, bool ru) const;
  // The name a task goes by on screen: its tracker key, else its id (A3-10).
  Q_INVOKABLE QString displayKey(const QString& id) const;

  QString currentView() const {
    return m_currentView;
  }

  void setCurrentView(const QString& v);

  QString focusedStatus() const {
    return m_focusedStatus;
  }

  // Jump the Board to a specific status column (sidebar Blocked / Code Review).
  Q_INVOKABLE void focusStatusColumn(const QString& statusId);
  QString currentSection() const;
  // Opens a section on the view it was last left on.
  Q_INVOKABLE void openSection(const QString& section);
  // The view a section would open on now.
  Q_INVOKABLE QString sectionView(const QString& section) const;

  QString shellNotice() const {
    return m_shellNotice;
  }

  Q_INVOKABLE void ackShellNotice();
  // The shortcut catalog moved to the heap 2 shell (APP-258) and the Vim-based
  // keymap (APP-272); called for a settings file written before it.
  void applyKeymapMigration(QVariantMap& overrides, int storedSchema);
  // Old keys whose first press after the update still owes a word (APP-281 A4).
  Q_INVOKABLE bool hasKeymapNotice() const;
  // A key was pressed (KeyRouter's chord): if it is one of those, say once
  // where its old action went.
  Q_INVOKABLE void noteKeyPressed(const QString& chord);

  int workdayStart() const {
    return m_workdayStart;
  }

  void setWorkdayStart(int v);

  int workdayEnd() const {
    return m_workdayEnd;
  }

  void setWorkdayEnd(int v);

  QString crumbProject() const {
    return m_crumbProject;
  }

  void setCrumbProject(const QString& v);

  QString crumbUser() const {
    return m_crumbUser;
  }

  void setCrumbUser(const QString& v);

  QString docsState() const {
    return m_docsState;
  }

  void setDocsState(const QString& v);
  // The same write as one entry on the undo stack, labelled with what an undo
  // says it restored. The docs catalogue's deletions go through here so they
  // share Ctrl+Z and the stack with everything else.
  Q_INVOKABLE void setDocsStateUndoable(const QString& v, const QString& label);

  NoteModel* notes() {
    return &m_notes;
  }

  QString activeNoteId() const {
    return m_activeNoteId;
  }

  void setActiveNoteId(const QString& id);

  // One note's body. The list model deliberately does not carry bodies: fifty
  // notes would mean fifty documents crossing the QML boundary on a repaint.
  Q_INVOKABLE QString noteBody(const QString& id) const;

  // Creating one returns its id so a caller can open it straight away.
  // Without a title or folder it is empty, named later by its first line.
  Q_INVOKABLE QString newNote(const QString& title = QString(), const QString& folder = QString());
  // Whether `id` is the empty new note nobody has written in yet — the one
  // going to Notes opens, dropped again when left (dogfood 2026-10-20).
  Q_INVOKABLE bool isFreshNote(const QString& id) const;
  // The notes, most recently opened first (Ctrl+Tab, dogfood 2026-10-20);
  // the ones not opened since the start follow, last edited first.
  Q_INVOKABLE QStringList recentNoteIds() const;
  // The same as [{ id, title, folder, fresh }], at most `max` (0: all).
  Q_INVOKABLE QVariantList recentNotes(int max = 0) const;
  // False when nothing changed: an empty title, or one another note in the
  // same folder already has — [[links]] to that one would have changed
  // meaning (IDIOT-KNOW-3).
  Q_INVOKABLE bool renameNote(const QString& id, const QString& title);
  // Whether a note other than `exceptId` in `folder` is called `title`
  // (case-insensitive, as [[links]] read titles).
  Q_INVOKABLE bool noteTitleTaken(const QString& title, const QString& folder, const QString& exceptId) const;
  Q_INVOKABLE void deleteNote(const QString& id);
  // APP-116: appends note `sourceId`'s text below note `targetId`'s and
  // removes the source; links to the source now point at the target. One
  // undo step. False when either is missing or they are the same note.
  Q_INVOKABLE bool mergeNotes(const QString& sourceId, const QString& targetId);
  Q_INVOKABLE void setNoteBody(const QString& id, const QString& body);
  Q_INVOKABLE void setNotePinned(const QString& id, bool pinned);
  Q_INVOKABLE void moveNoteToFolder(const QString& id, const QString& folder);
  // Every folder in use, sorted, for a tree or a picker.
  Q_INVOKABLE QStringList noteFolders() const;
  // Re-file every note in `folder` (and its subfolders) under `newName`; an
  // empty name files them at the root. One undo step; returns how many moved.
  Q_INVOKABLE int renameNoteFolder(const QString& folder, const QString& newName);
  // Remove the folder, keeping its notes: they move one level up.
  Q_INVOKABLE int removeNoteFolder(const QString& folder);

  // ── Notes as a folder of .md files ──
  //
  // Import returns a summary rather than a bool, for the same reason .ics does:
  // a vault that brought in forty notes and skipped two is neither a success
  // nor a failure. Keys: imported, updated, unchanged, kept (edited here, not
  // on disk), conflicts (edited in both: the file arrives as a copy), skipped,
  // files, folder, warnings. The whole import is one undo step.
  Q_INVOKABLE QVariantMap importNotesFolder(const QUrl& folderUrl, bool checklistTasks = false);
  // The same summary without changing anything, for a confirm step.
  Q_INVOKABLE QVariantMap previewNotesFolder(const QUrl& folderUrl);
  // Writes into a new folder inside `folderUrl` — named `subfolder`, or dated
  // when that is empty, with a suffix when the name is taken — so an export
  // never overwrites a file. Keys: written, skipped, folder.
  // One note as a .md file (R2-062 "Экспорт в .md"), the same contents a
  // folder export writes for it. False when the file cannot be written.
  Q_INVOKABLE bool exportNoteToFile(const QString& id, const QUrl& fileUrl);
  Q_INVOKABLE QVariantMap exportNotesFolder(const QUrl& folderUrl, const QString& subfolder = QString());

  // ── Links between notes ──
  //
  // What `[[target]]` points at, from the note it was written in. Returns
  // { kind: "note"|"heading"|"missing", noteId, heading, title }. The caller
  // opens the note or scrolls to the heading; "missing" is an offer to create
  // it, because a broken link is usually a note somebody meant to write.
  Q_INVOKABLE QVariantMap resolveNoteLink(const QString& target) const;
  // Which notes link to this one: [{ noteId, title, line, text }, …].
  Q_INVOKABLE QVariantList backlinksToNote(const QString& noteId) const;
  // Targets in the open note that nothing answers to.
  Q_INVOKABLE QStringList unresolvedNoteLinks() const;
  // Create the note a broken link was asking for, named after it, and open it.
  Q_INVOKABLE QString createNoteForLink(const QString& target);

  // Today's note, created on first use. A daily note that has to be made by
  // hand is one people stop making.
  Q_INVOKABLE QString openDailyNote();

  // ── Doc pages ──
  //
  // Docs was a catalog of one-line things. A page is where the paragraph
  // explaining why the link matters goes. Same shape as notes: the model
  // carries no bodies, and `docPageBody(id)` fetches one.
  Q_PROPERTY(DocPageModel* docPages READ docPages CONSTANT)
  Q_PROPERTY(QString activeDocPageId READ activeDocPageId WRITE setActiveDocPageId NOTIFY activeDocPageChanged)

  DocPageModel* docPages() {
    return &m_docPages;
  }

  QString activeDocPageId() const {
    return m_activeDocPageId;
  }

  void setActiveDocPageId(const QString& id);

  Q_INVOKABLE QString docPageBody(const QString& id) const;
  Q_INVOKABLE QString newDocPage(const QString& title = QString(), const QString& parentId = QString());
  Q_INVOKABLE void renameDocPage(const QString& id, const QString& title);
  Q_INVOKABLE void setDocPageBody(const QString& id, const QString& body);
  // Deleting a page takes its subtree: a page whose parent is gone would be
  // unreachable in the tree and invisible everywhere else.
  Q_INVOKABLE void deleteDocPage(const QString& id);
  // Re-parent and re-order in one call, because dragging a page in a tree does
  // both at once. `beforeId` is the sibling to land above, empty for last.
  Q_INVOKABLE void moveDocPage(const QString& id, const QString& newParentId, const QString& beforeId);
  // The pages under `parentId`, in order — what a tree row expands into.
  Q_INVOKABLE QVariantList docPageChildren(const QString& parentId) const;

  QString notesState() const {
    return m_notesState;
  }

  void setNotesState(const QString& v);

  // QuickCapture for Notes: appends `text` to the Inbox note (made on first
  // use) under a timestamped horizontal-rule header, whichever note is open
  // (sheet N/X-Oth-Capture: "быстрая заметка → «Входящие»", R4-073). First
  // entry gets the heading only (no leading HR — nothing to separate from).
  // A quick note (APP-320): goes on in the last note when that one was made
  // or written to less than an hour ago under the same label, else starts a
  // new note named by its first words. `label` ("hr", "#личное", "") is
  // written as a #tag. Returns the note's id.
  Q_INVOKABLE QString appendNoteEntry(const QString& text, const QString& label = QString());
  QString appendNoteEntryAt(const QString& text, const QString& label, const QDateTime& now);
  // The labels to offer for a quick note: the default three, the card labels
  // and the #tags already in notes, each once, lower case.
  Q_INVOKABLE QStringList noteLabels() const;
  // The #tags used in notes with how many notes carry each: [{id, count}],
  // most used first, for the notes list's filter chips.
  Q_INVOKABLE QVariantList noteTagCounts() const;
  // The title of the note appendNoteEntry() writes into, so the quick-note
  // popup can say where the text will land before it does.
  // "" when the next quick note under `label` starts a new note.
  Q_INVOKABLE QString quickNoteTarget(const QString& label = QString()) const;
  QString quickNoteTargetAt(const QString& label, const QDateTime& now) const;
  // Where the next quick note goes on (settingsExtra.noteChain).
  void noteChainTo(const QString& noteId, const QString& label, const QDateTime& at);
  // The Inbox note's id, "" until the first quick note made it.
  Q_INVOKABLE QString inboxNoteId() const;
  // The quick note's unsaved draft and the task it is attached to, kept in
  // settings.quickNoteDraft so it outlives a quit or a crash (R2-069).
  // {text, attachId}, empty when there is none; an empty text clears it.
  Q_INVOKABLE QVariantMap quickNoteDraft() const;
  Q_INVOKABLE void setQuickNoteDraft(const QString& text, const QString& attachId);

  // Note wiki-links (HEAP-79). Headings feed [[…]] autocomplete; backlinks list
  // which lines reference each [[target]]; the offset lets the editor jump to a
  // heading. The caller passes the live editor text so results reflect unsaved
  // edits (the pure logic lives in heap::notes and is unit-tested there).
  Q_INVOKABLE QStringList noteHeadings(const QString& markdown) const;
  Q_INVOKABLE QVariantList noteBacklinks(const QString& markdown) const;
  Q_INVOKABLE int noteHeadingOffset(const QString& markdown, const QString& heading) const;
  // The open note's own [[links]], grouped by target like noteBacklinks(), each
  // resolved against every note: { target, kind: note|heading|missing, noteId,
  // heading, resolved, refs: [{ line, text }] }.
  Q_INVOKABLE QVariantList outgoingNoteLinks(const QString& markdown) const;
  // { lines, mentions, tickets } for the editor's header.
  Q_INVOKABLE QVariantMap noteStats(const QString& markdown) const;

  // ---- Attachments (AppControllerAttachments.cpp, storage/Attachments.h) ----
  // Files go into <dataDir>/attachments under a content hash; a task lists
  // them, a note or description links them as "attachments/<id>".
  QString attachmentsDir() const;
  // Stores files (file:// URLs or paths) without attaching them to anything —
  // a new task's draft, a note that is about to link them. Returns one
  // { id, name, size, mime, isImage, ref } per stored file; a refused file is
  // left out and named in a toast. `intoText`: the caller links them from an
  // editor's text, whose own undo can bring the link back after it is deleted,
  // so the cleanup keeps them for the rest of the session (KNOW-3).
  Q_INVOKABLE QVariantList importAttachments(const QVariantList& urls, bool intoText = false);
  // Stores the files and appends them to the task, as one undo step. Returns
  // how many were attached (a file the task already has counts once).
  Q_INVOKABLE int attachFilesToTask(const QString& taskId, const QVariantList& urls);
  // Detaches one file: one undo step. The bytes stay until the cleanup.
  Q_INVOKABLE bool removeTaskAttachment(const QString& taskId, const QString& attachmentId);
  // The task's files for the editor's chips: the stored metadata plus
  // { exists, sizeText, isImage, ref }.
  Q_INVOKABLE QVariantList taskAttachments(const QString& taskId) const;
  // Chip data for a draft's list or for the files a piece of markdown links.
  Q_INVOKABLE QVariantList describeAttachments(const QVariantList& attachments) const;
  Q_INVOKABLE QVariantList markdownAttachments(const QString& markdown) const;
  // Opens a stored file in its default application. Returns "opened",
  // "confirm" (a type that runs something: ask, then call again with
  // confirmed), "missing" or "invalid".
  // A table or a text file (.csv .tsv .xlsx .txt .log) returns "view": the
  // app shows it itself (attachmentPreview, APP-315).
  Q_INVOKABLE QString openAttachment(const QString& id, bool confirmed = false);
  // The same file in its own application, for the viewer's "open outside".
  Q_INVOKABLE bool openAttachmentExternally(const QString& id);
  // What the in-app viewer shows (APP-315): { kind: "table" | "text" | "none",
  // name, sheets: [{ name, rows: [[cell…]…], total, columns }], text,
  // language ("log" for a log), truncated, error }. At most a few thousand
  // rows and a few MB of text; the rest is the file's, opened outside.
  Q_INVOKABLE QVariantMap attachmentPreview(const QString& id) const;
  // Shows the file in the system file manager.
  Q_INVOKABLE bool revealAttachment(const QString& id);
  // The file:// URL of a stored file, for a preview; empty when missing.
  Q_INVOKABLE QUrl attachmentUrl(const QString& id) const;
  // Paste: an image on the clipboard is saved as a PNG, copied files are
  // stored as they are. Empty when the clipboard holds neither, so the caller
  // falls back to pasting text. What it stores is linked from text, as with
  // importAttachments(urls, true).
  Q_INVOKABLE bool clipboardHasAttachment() const;
  Q_INVOKABLE QVariantList importClipboardAttachments();
  // A table on the clipboard (a web page's <table>, a spreadsheet's
  // tab-separated rows, a spec page's glued cells) as a markdown pipe table,
  // ready to insert; "" when the clipboard holds no table (TablePaste).
  Q_INVOKABLE QString clipboardTableMarkdown() const;
  // The clipboard's plain text, for "paste as plain text".
  Q_INVOKABLE QString clipboardPlainText() const;
  // Settings → Data: files no task, note, page or undo step refers to, and
  // that no text has linked since the app started (m_attachmentsLinkedThisSession).
  // { count, bytes, sizeText }.
  Q_INVOKABLE QVariantMap unusedAttachments() const;
  // Deletes them. Returns what unusedAttachments() said, as it was removed.
  Q_INVOKABLE QVariantMap cleanUpUnusedAttachments();
  Q_INVOKABLE QString formatBytes(double bytes) const;

  QString appSettingsJson() const {
    return m_appSettingsJson;
  }

  void setAppSettingsJson(const QString& v);

  // Kept under the old name because QML and the toast bind to it; it now means
  // "the undo stack is not empty" rather than "a five-second slot is armed".
  bool hasPendingUndo() const {
    return m_undo.canUndo();
  }

  bool canRedo() const {
    return m_undo.canRedo();
  }

  // ---- Onboarding ----
  bool welcomeSeen() const {
    return m_welcomeSeen;
  }

  bool demoActive() const {
    return m_demoActive;
  }

  // Mark the welcome dialog as shown (persists; never re-shown after this).
  Q_INVOKABLE void markWelcomeSeen();
  // Hide the demo banner without clearing anything ("keep exploring").
  Q_INVOKABLE void dismissDemo();
  // Clear the active profile's demo content (tasks, people, events, notes,
  // docs) to give the user a blank workspace; keeps the profile and its
  // columns. Also clears the demo banner.
  Q_INVOKABLE void startFresh();
  // Nuke everything — profiles, tasks, notes, docs, events, settings, the
  // on-disk state.json + backups — and re-seed the Example profile with the
  // first-run onboarding, so the app is exactly "as new" on this device.
  Q_INVOKABLE void resetToFirstRun();
  // The example (APP-271): a profile of its own, "Example", with the sample
  // tasks, people, meetings and notes — never mixed into the person's own
  // profile. openExample() makes it once and switches to it (a second call
  // only switches); removeExample() takes it away with its meetings, in one
  // undoable step. exampleChanges() counts the sample tasks edited or added
  // since it was made, for the question before removing it.
  Q_INVOKABLE QString openExample();
  Q_INVOKABLE void removeExample();
  Q_INVOKABLE bool hasExample() const;
  Q_INVOKABLE int exampleChanges() const;
  // The id named the product until APP-283; an old one is renamed on load,
  // with everything that points at it (migrateExampleProfileId).
  static constexpr const char* kExampleProfileId = "tabless-example";
  static constexpr const char* kLegacyExampleProfileIds[] = {"lowkey-example"};
  // Settings -> Data -> "Reset all settings" (UX-5): preferences go back to a
  // new install's (heap. ink + soft contrast included). Kept: the profile
  // card, tracker connections, watched repositories, your own themes, window
  // and panel layout, and anything the Settings page does not own. Undoable
  // through undoSettingsReset() for as long as the toast offers it.
  Q_INVOKABLE void resetSettingsToDefaults();
  Q_INVOKABLE void undoSettingsReset();
  // Re-open the welcome guide on demand (Settings → Help "Replay"). Purely a UI
  // request — it does NOT touch welcomeSeen/demoActive or any persisted state.
  Q_INVOKABLE void replayWelcome();

  // ---- Task ops ----
  Q_INVOKABLE void moveTask(const QString& id, const QString& newStatus);
  // "Done" in one action (APP-268): the tasks go to the first column whose
  // stage is "done"; when every one of them is done already, each goes back
  // to the column it came from. One undo for all of them, one toast.
  // Returns {count, reopened, unchecked, localOnly, error}; error
  // "noDoneColumn" when the profile has no column of that stage.
  Q_INVOKABLE QVariantMap toggleDone(const QStringList& ids);
  // The first column, in board order, whose stage is "done"; "" if none.
  Q_INVOKABLE QString doneColumn() const;
  // Manual order: place `id` in `statusId` immediately above `beforeTaskId`,
  // or at the end of the column when that is empty. This is what a drop
  // between two cards calls; moveTask() is the "somewhere in that column"
  // form and leaves the position alone.
  Q_INVOKABLE void moveTaskTo(const QString& id, const QString& statusId, const QString& beforeTaskId);
  // Same, for every selected task, preserving their order relative to one
  // another.
  Q_INVOKABLE void moveSelectedTasksTo(const QString& statusId, const QString& beforeTaskId);
  Q_INVOKABLE QVariantMap newTaskDraft(const QString& statusId) const;
  // The draft QuickCapture saves: newTaskDraft in the To Do column, with the
  // profile's id prefix like any other task. \p ticketKey, when the text named
  // a tracker ticket ("LTE-2398 fix login") and no task has that id yet, is
  // the id instead.
  Q_INVOKABLE QVariantMap newQuickTaskDraft(const QString& ticketKey = QString()) const;
  // The whole task QuickCapture makes of `raw`: newQuickTaskDraft with the
  // title, "// description", priority, #labels and the date the text names
  // (cut out of the title, read against `reference`, now when invalid). The
  // popup and `heap add` both save this, so they cannot read text apart.
  Q_INVOKABLE QVariantMap quickTaskDraft(const QString& raw,
                                         const QDateTime& reference = QDateTime(),
                                         const QStringList& rejected = {}) const;
  // One line of task input read the way quickTaskDraft reads it (APP-245):
  // {title, when, whenHasTime, due, dueHasTime, estimateMinutes, recurrence,
  //  spans: [{start, end, kind, text}]} — offsets into `raw` after the meta
  // (priority, #labels, "// …") is taken out, i.e. into `head`. A "+Profile
  // name" (A5-21) comes back as profileId / profileName and a "profile" mark,
  // and quickTaskDraft carries it as _targetProfileId.
  Q_INVOKABLE QVariantMap captureParse(const QString& raw,
                                       const QDateTime& reference = QDateTime(),
                                       const QStringList& rejected = {}) const;
  // Reusable task/checklist templates (HEAP-77). taskTemplates lists the
  // built-ins ({name, title, desc}); createTaskFromTemplate drops a pre-filled
  // task (checklist in the description) onto the board.
  Q_INVOKABLE QVariantList taskTemplates() const;
  Q_INVOKABLE void createTaskFromTemplate(const QString& name);
  // False when the draft was refused (taken or empty id, empty title) — the
  // editor then stays open on it.
  Q_INVOKABLE bool saveTask(const QVariantMap& draft);
  // Send a card's current status to its tracker again, after a failed push.
  // An explicit "send" from the card: goes out even while the tracker's
  // switch is off, but only after the same fresh check (APP-204).
  Q_INVOKABLE void retryTrackerPush(const QString& taskId);
  // The user confirmed "send anyway" for a card whose issue is outside the
  // filter (trackerPushNeedsConfirm). Sends without asking again.
  Q_INVOKABLE void confirmTrackerPush(const QString& taskId);
  // Drop a move that was never sent: the card keeps its column here and the
  // tracker is left alone (APP-243 "cancel").
  Q_INVOKABLE void discardTrackerPush(const QString& taskId);
  // Whether moving this tracker's cards changes the issue's status there.
  // Off unless the user turned it on for that tracker (APP-243).
  Q_INVOKABLE bool trackerWriteEnabled(const QString& providerId) const;
  // The switch itself (Settings → Integrations → tracker). Ignored for a
  // tracker heap cannot write to.
  Q_INVOKABLE void setTrackerWriteEnabled(const QString& providerId, bool enabled);
  // Whether the tracker can be written to at all, i.e. has the switch.
  Q_INVOKABLE bool trackerCanWriteStatus(const QString& providerId) const;
  // With writes on, a move asks before its status goes out ("Отправить
  // статус в Jira?", X/N-Dlg-Conflict, R3-147) until the user ticks "don't
  // ask again for <tracker>". On by default.
  Q_INVOKABLE bool trackerAskBeforeWrite(const QString& providerId) const;
  Q_INVOKABLE void setTrackerAskBeforeWrite(const QString& providerId, bool ask);
  // Jira Server / Data Center wiki markup shown as markdown, or as it is: the
  // user's choice per tracker, on by default (APP-313). Display only.
  Q_INVOKABLE bool trackerWikiToMarkdown(const QString& providerId) const;
  Q_INVOKABLE void setTrackerWikiToMarkdown(const QString& providerId, bool convert);
  // A tracker card's description as it is shown: the wiki markup converted
  // when that is on; the stored text is left as the tracker sent it.
  Q_INVOKABLE QString trackerDescription(const QString& taskId) const;
  // A ticket's pictures for the viewer (dogfood 2026-10-21), in the order
  // the tracker lists them: [{attachmentId, name, attId}], attId empty for
  // one not fetched yet. Asks nothing of the tracker.
  Q_INVOKABLE QVariantList trackerImages(const QString& taskId) const;
  // One picture opened in the viewer: {attId, loading, error}. Fetches the
  // whole file when it is not stored yet, once per attachment; a failure is
  // asked again only after its back-off (FetchBackoff).
  Q_INVOKABLE QVariantMap trackerImage(const QString& taskId, const QString& attachmentId);
  QStringList trackerWriteProviders() const;
  // Once per install after the update that made writes opt-in: the sentence
  // naming the connected trackers whose status heap no longer changes, or an
  // empty string. Marks itself done either way.
  Q_INVOKABLE QString trackerWriteNoticeOnce();
  // A title/description/priority both sides changed: take the tracker's
  // version of every conflicting field (true), or keep the local one and stop
  // flagging it (false). Undoable.
  Q_INVOKABLE void resolveTrackerConflict(const QString& taskId, bool useTracker);
  // One field of it ("title" | "body" | "priority" | "status"). Keeping my
  // status sends it to the tracker; taking the tracker's drops the unsent move.
  Q_INVOKABLE void resolveTrackerConflictField(const QString& taskId, const QString& field, bool useTracker);
  // The conflict sheet's "Apply choice" (R2-032): the fields in `mine` keep
  // the local value, the fields in `theirs` take the tracker's — one undo
  // step, one toast. Keeping my status sends it only where writes are on.
  Q_INVOKABLE void resolveTrackerConflictChoices(const QString& taskId, const QStringList& mine, const QStringList& theirs);
  // The newest local edit of the task (ISO, "" = none known): the "you —
  // 14:58" half of the conflict sheet's subtitle.
  Q_INVOKABLE QString lastLocalEditAt(const QString& taskId) const;
  // A card whose issue is gone from the tracker, kept as the user's own
  // task (R2-034 "оставить у себя"): the tracker link goes, the key and the
  // address go into its notepad, everything local stays. Undoable.
  Q_INVOKABLE void keepGoneTicketLocally(const QString& taskId);
  // The task whose timer runs in the active profile: { id, title, key,
  // seconds }, or an empty map (R2-052, the sidebar's timer line).
  Q_INVOKABLE QVariantMap runningTimer() const;
  // Archive every card of this tracker the current filter no longer covers.
  Q_INVOKABLE void archiveOutOfScope(const QString& providerId);
  // Settings → Integrations → Health (APP-164): one row per connected
  // tracker — { id, name, lastOk, items, failing, error, errorDetail,
  // errorAge, expiry, offline }, times relative to now, text in the UI
  // language. Read-only.
  Q_INVOKABLE QVariantList integrationHealth() const;
  QVariantList integrationHealthAt(const QDateTime& now) const;

  // The same rows as a property, for bindings (R2-035/036/048); also
  // { kind, failedAt, failedAtMs, inFlight, everOk, waiting }.
  QVariantList syncSources() const {
    return integrationHealth();
  }

  // ── Paused features (AppControllerPause.cpp) ──
  // {id, nameKey, nameArg, kind ("errors" | "noAccess"), status, host,
  //  count, since, sinceMs, hidden, canDisable}.
  QVariantList pausedFeatures() const;
  // The strip's "Выключить функцию": the feature's own setting goes off
  // (git.codeOwners, git.mrThreads, a tracker's autoSync, …). Never removes
  // a connection; nothing switches off without this click.
  Q_INVOKABLE void disableFeature(const QString& featureId);
  // "Попробовать снова": exactly one request of the feature, now.
  Q_INVOKABLE void retryFeature(const QString& featureId);
  // ×: the line hides until the feature's state changes; it stays paused.
  Q_INVOKABLE void hidePausedFeature(const QString& featureId);
  // Whether the timer, the live updates and the catch-up pulls sync this
  // tracker (on by default); "Sync now" works either way.
  Q_INVOKABLE bool trackerAutoSync(const QString& providerId) const;
  Q_INVOKABLE void setTrackerAutoSync(const QString& providerId, bool on);
  // Whether a pull reads the CI of open MRs / PRs (on by default).
  Q_INVOKABLE void setTrackerCiFacts(const QString& providerId, bool on);
  // A calendar link read by the timer (on by default) or only by hand.
  Q_INVOKABLE void setCalendarSubscriptionAuto(const QString& id, bool on);

  // The keychain refused a sign-in (R2-040): { provider, name, error }, or
  // empty. keepSecretsInFile() keeps that tracker's tokens in secrets.json
  // in the data folder (DPAPI on Windows); retryKeychain() writes them to the
  // keychain again; dismissKeychainProblem() puts the card away.
  QVariantMap keychainProblem() const {
    return m_keychainProblem;
  }

  bool whatsNewDue() const {
    return m_whatsNewDue;
  }

  // The version this data last ran on when it was an earlier patch of the
  // same line (A5-19): said once, quietly; "" otherwise. Cleared on read.
  Q_INVOKABLE QString takeUpdatedFrom() {
    return std::exchange(m_updatedFrom, QString());
  }

  Q_INVOKABLE void ackWhatsNew() {
    if(m_whatsNewDue) {
      m_whatsNewDue = false;
      emit onboardingChanged();
    }
  }

  Q_INVOKABLE void keepSecretsInFile();
  Q_INVOKABLE void retryKeychain();
  Q_INVOKABLE void dismissKeychainProblem();
  Q_INVOKABLE QString keychainName() const;

  // ---- Sync visibility (APP-180/186/187) ----
  bool syncing() const {
    return !m_syncInFlight.isEmpty();
  }

  int unseenRevision() const {
    return m_unseenRevision;
  }

  QStringList syncNewTaskIds() const {
    return m_syncNewIds;
  }

  // A card a sync brought in, not yet opened or reached by the cursor.
  Q_INVOKABLE bool isTaskUnseen(const QString& taskId) const {
    return m_unseenTaskIds.contains(taskId);
  }

  // Opened, or the keyboard cursor landed on it: the dot goes.
  Q_INVOKABLE void markTaskSeen(const QString& taskId);
  // What a pull does for the cards it adds; public so the QML tests can set
  // the state up the way a sync does.
  Q_INVOKABLE void markTasksUnseen(const QStringList& taskIds);

  QVariantList eventLog() const {
    return m_eventLog.toVariantList();
  }

  // Adds an entry to the event log. QML uses it for the failures it reports
  // itself (an import that did not read); everything C++ reports is logged
  // where it is said.
  Q_INVOKABLE void logEvent(const QString& kind, const QString& message, const QStringList& taskIds = {}, const QString& route = QString());
  // What happened to a task, newest first: [{ at, kind, from, to, sync }]
  // (APP-165). kind: created | status | title | priority | due | scheduled
  // | pushed. Capped per task; see history/TaskHistory.h.
  Q_INVOKABLE QVariantList taskHistory(const QString& taskId) const;
  // Whether a link from user or tracker content may open without asking. See
  // heap::md::isSafeLink.
  Q_INVOKABLE bool isSafeLink(const QString& url) const;
  // False when tokens live in secrets.json in the data folder rather than the
  // OS keychain (portable --data-dir run, or a build without QtKeychain).
  Q_INVOKABLE bool secretsInKeychain() const;
  Q_INVOKABLE void deleteTask(const QString& id);
  Q_INVOKABLE bool canTransitionStatus(const QString& taskId, const QString& newStatus);
  Q_INVOKABLE void setArchived(const QString& taskId, bool archived);

  // Time tracking (HEAP-78). start/stop the per-task timer (only one runs at a
  // time); elapsedSecondsFor returns the live total incl. the running session.
  // The task's own notepad (Task.local.notes, APP-237/265): no sync writes
  // it, so a tracker card's own words live here.
  Q_INVOKABLE QString taskLocalNotes(const QString& id) const;
  Q_INVOKABLE void setTaskLocalNotes(const QString& id, const QString& text);
  // Notes that name the task (its id as a word), newest first:
  // [{id, title, updated}] — the task document's "Mentioned in" (APP-265).
  Q_INVOKABLE QVariantList notesMentioningTask(const QString& id) const;

  // ---- The local layer (APP-236…241, 246, 251): AppControllerLocal.cpp ----
  // Nothing here is ever sent to a tracker, and no sync path writes it.
  //
  // My priority / due over the tracker's (APP-238): both sides and whether
  // the tracker moved its own since I set mine — {isTicket, trackerPriority,
  // trackerDueAt, trackerDueHasTime, myPriority, myDueAt, myDueHasTime,
  // priorityChanged, dueChanged}.
  Q_INVOKABLE QVariantMap trackerValues(const QString& id) const;
  // Drops my value so the tracker's shows again: field "priority", "due", or
  // empty for both. One undo step.
  Q_INVOKABLE void resetToTracker(const QString& id, const QString& field = QString());
  // Keeps mine and stops showing "changed in the tracker".
  Q_INVOKABLE void acknowledgeTrackerChange(const QString& id);
  // The notepad copied into a new note that names the task (APP-237);
  // returns the note's title ("" when there is nothing to copy).
  Q_INVOKABLE QString copyTaskNotesToNote(const QString& id);

  // The card's own checklist (APP-236). Items: [{id, text, level, done,
  // autoDone, cardId, cardExists, hasChildren}].
  Q_INVOKABLE QVariantList taskChecklist(const QString& id) const;
  Q_INVOKABLE QString taskChecklistText(const QString& id) const;
  Q_INVOKABLE void setTaskChecklistText(const QString& id, const QString& text);
  // Adds the lines of `text` after `afterItemId` (empty = at the end). A line
  // with no dashes takes `level`. Returns the id of the last item added.
  Q_INVOKABLE QString addChecklistItems(const QString& id, const QString& afterItemId, const QString& text, int level = 1);
  Q_INVOKABLE void editChecklistItem(const QString& id, const QString& itemId, const QString& text);
  Q_INVOKABLE void toggleChecklistItem(const QString& id, const QString& itemId);
  Q_INVOKABLE void indentChecklistItem(const QString& id, const QString& itemId, int delta);
  Q_INVOKABLE bool moveChecklistItem(const QString& id, const QString& itemId, int dir);
  Q_INVOKABLE void removeChecklistItem(const QString& id, const QString& itemId);
  // The item becomes a local card "part of" this one; its sub-items move
  // into the new card's checklist and the item ticks with the card's Done.
  // Returns the new card's id.
  Q_INVOKABLE QString checklistItemToCard(const QString& id, const QString& itemId);
  // Back: the card is removed and its checklist returns under the item.
  // Refused (false) when the card has time on its timer.
  Q_INVOKABLE bool checklistCardBack(const QString& id, const QString& itemId);

  // My tags (APP-239). The catalogue: [{id, color, count}] over the profile.
  Q_INVOKABLE QVariantList localTagCatalog() const;
  // Ids ("#x" or "x") or {id, color}; a tag known elsewhere keeps its colour.
  Q_INVOKABLE void setTaskLocalTags(const QString& id, const QVariantList& tags);
  // Renames everywhere; onto an existing tag = merge.
  Q_INVOKABLE void renameLocalTag(const QString& from, const QString& to);
  Q_INVOKABLE void deleteLocalTag(const QString& tag);
  Q_INVOKABLE void setLocalTagColor(const QString& tag, const QString& color);

  // Links drawn by hand (APP-240): related (both ways), blocks / blocked by,
  // part of. [{kind, linkId, target, url, isTask, exists, title, key,
  // statusName, done}].
  Q_INVOKABLE QVariantList taskRelations(const QString& id) const;
  // A task by id, tracker key or URL, or a bare URL. False when `ref` is
  // neither.
  Q_INVOKABLE bool addRelatedLink(const QString& id, const QString& ref);
  Q_INVOKABLE void removeRelatedLink(const QString& id, const QString& linkId);
  // Cards whose title, id or key hold every word of `text`, for picking a
  // link target by search: [{id, key, title, statusName}], open ones first.
  Q_INVOKABLE QVariantList matchTasks(const QString& text, int limit = 6, const QString& exceptId = QString()) const;
  // `blocksOther`: id blocks ref; else ref blocks id.
  Q_INVOKABLE bool addBlockLink(const QString& id, const QString& ref, bool blocksOther);
  Q_INVOKABLE void removeBlockLink(const QString& blockerId, const QString& blockedId);

  // The comment draft (APP-241). Saved as typed; never sent anywhere.
  Q_INVOKABLE QString taskCommentDraft(const QString& id) const;
  Q_INVOKABLE void setTaskCommentDraft(const QString& id, const QString& text);
  Q_INVOKABLE void clearTaskCommentDraft(const QString& id);
  // Puts the draft on the clipboard (markdown, plus HTML for rich editors
  // such as Jira's) and returns the ticket's URL for the caller to open.
  Q_INVOKABLE QString copyCommentDraft(const QString& id);

  // Estimates (APP-246): {count, minutes, without} over `ids` — the tasks
  // with no estimate are a count of their own, not zero minutes.
  Q_INVOKABLE QVariantMap estimateSummary(const QStringList& ids) const;

  // Timer sessions (APP-251): [{id, start, end, seconds, undated}], newest
  // first. Edits recompute the task's total.
  Q_INVOKABLE QVariantList taskSessions(const QString& id) const;
  Q_INVOKABLE bool addTaskSession(const QString& id, const QDateTime& start, const QDateTime& end);
  Q_INVOKABLE bool updateTaskSession(const QString& id, const QString& sessionId, const QDateTime& start, const QDateTime& end);
  Q_INVOKABLE void removeTaskSession(const QString& id, const QString& sessionId);
  // Seconds on the timer on `day` across the profile, a running timer
  // included up to now.
  Q_INVOKABLE int trackedSecondsOn(const QDate& day) const;

  // Knowledge (APP-269). The tasks a note names — a [[KEY]] or the bare key —
  // with their column now, in the order written: [{ id, title, status,
  // statusName, category, profileName }]; a task of another profile carries
  // that profile's name, of the active one an empty one.
  Q_INVOKABLE QVariantList noteTaskRefs(const QString& markdown) const;
  // How each [[target]] of a note reads in the rendered view: a task key →
  // { kind: "task", label: "<stage glyph> KEY", tip }, a name nothing answers
  // to → { kind: "missing", label: "name · no such note" }. Notes and headings
  // that exist are left out (drawn as links).
  Q_INVOKABLE QVariantMap wikiTargets(const QString& markdown) const;
  // Tasks whose description or own notepad links the note by its title,
  // [[Title]]: [{ id, title }]. Backlinks from notes are backlinksToNote().
  Q_INVOKABLE QVariantList tasksLinkingToNote(const QString& noteId) const;
  Q_INVOKABLE void startTaskTimer(const QString& id);
  Q_INVOKABLE void stopTaskTimer(const QString& id);
  Q_INVOKABLE int elapsedSecondsFor(const QString& id) const;

  // ---- Multi-select API ----
  QStringList selectedTaskIds() const {
    return m_selectedTaskIdsList;
  }

  int selectionCount() const {
    return m_selectedTaskIdsList.size();
  }

  Q_INVOKABLE bool isTaskSelected(const QString& id) const;
  Q_INVOKABLE void toggleTaskSelection(const QString& id);
  Q_INVOKABLE void setTaskSelected(const QString& id, bool selected);
  Q_INVOKABLE void setSelectedTaskIds(const QStringList& ids);
  Q_INVOKABLE void clearSelection();
  // The filters the views show tasks under (search or query, priority chips,
  // archived, the timeline's done toggle). Selected tasks they hide leave the
  // selection, now and before every bulk action, so Delete never reaches a
  // card the user cannot see (TASKS-13, audit 2026-09-30).
  Q_INVOKABLE void setSelectionFilter(const QString& search, const QStringList& priorities, bool showArchived, bool hideDone = false);

  // Bulk ops — operate on the current selection set.
  Q_INVOKABLE void deleteSelectedTasks();
  Q_INVOKABLE void moveSelectedTasksToStatus(const QString& statusId);
  Q_INVOKABLE void setSelectedTasksArchived(bool archived);
  // Priority and labels without the editor (src/AppControllerTaskEdits.cpp):
  // one card from its menu, or every selected card at once. Each is one undo
  // step. A label is added or removed by name ("#infra" works too).
  Q_INVOKABLE void setTaskPriority(const QString& taskId, const QString& priority);
  Q_INVOKABLE void setSelectedTasksPriority(const QString& priority);
  Q_INVOKABLE void setSelectedTasksLabel(const QString& label, bool present);
  // The filter bar's counters under the filters the user set: search text or
  // query, priority chips, the archived toggle. { total, active, blocked,
  // review }. `hideDone` is the timeline's "Show done" off. `rev` is unused;
  // binding it to statusCounts re-evaluates the counts whenever tasks change.
  // Whether a task shows under the filters on screen now (the selection
  // filter Main keeps in step): a new task that does not gets a toast that
  // says so (APP-266).
  Q_INVOKABLE bool taskInCurrentFilter(const QString& id) const;
  Q_INVOKABLE QVariantMap filteredCounts(
      const QString& search, const QStringList& priorities, bool showArchived, bool hideDone = false, const QVariant& rev = {}) const;
  // Tasks → List (APP-263): the rows of the list, in drawing order — a
  // {kind: "group", key, value, count, from, to, groupId, name?, category?}
  // before its {kind: "task", id, key, title, category, priority, label, when,
  // whenHasTime, due, dueHasTime, …}. groupBy: date | status | priority |
  // profile ("profile" lists every profile's tasks through the same query).
  Q_INVOKABLE QVariantList taskListRows(const QString& query,
                                        const QStringList& priorities,
                                        bool showArchived,
                                        const QString& groupBy) const;

  QStringList blockedStuckIds() const {
    return m_blockedStuckIds.values();
  }

  static constexpr bool debugBuild() {
#ifdef HEAP_DEBUG_BUILD
    return true;
#else
    return false;
#endif
  }

  // Actual writable data directory (where state.json / backups live) and the
  // Qt runtime version — resolved at runtime for the About panel.
  QString dataDir() const;
  QString qtVersion() const;
  QString appVersion() const;
  QString buildChannel() const;
  QString buildSha() const;
  QString buildDate() const;

  // Built from a key at read time, so a language switch repaints it (it
  // used to be the English sentence stored when the check finished).
  QString updateStatus() const {
    if(m_updateStatus.isEmpty()) {
      return {};
    }
    const QString text = tr_(m_updateStatus);
    // A channel build's own words: "A new feature build is out: abc1234".
    if(m_updateStatus.startsWith(QLatin1String("update.channel."))) {
      return m_updateStatusArg.isEmpty() ? text.arg(buildChannel()) : text.arg(buildChannel(), m_updateStatusArg);
    }
    if(m_updateStatusArg.isEmpty()) {
      return text;
    }
    if(m_updateStatus == QLatin1String("update.verified")) {
      return text.arg(m_updateStatusArg, m_updateSha256);  // version, checksum
    }
    // "Version %1 is ready": the tag without its "v".
    if(m_updateStatus == QLatin1String("update.available") && m_updateStatusArg.startsWith(QLatin1Char('v'))) {
      return text.arg(m_updateStatusArg.mid(1));
    }
    // The argument is itself a key when it names a reason ("update.offline").
    return text.arg(m_updateStatusArg.startsWith(QLatin1String("update.")) ? tr_(m_updateStatusArg) : m_updateStatusArg);
  }

  bool updateCanInstall() const;

  bool updatesFromStore() const {
    return m_updatesFromStore;
  }

  QString updatePhase() const {
    return m_updatePhase;
  }

  double updateProgress() const {
    return m_updateProgress;
  }

  QString updateSha256() const {
    return m_updateSha256;
  }

  heap::update::FeatureBuild* featureBuild() const {
    return m_featureBuild;
  }

  // ---- Diagnostics (HEAP-64) ----
  // Open the rotating-log directory in the system file manager.
  Q_INVOKABLE void openLogsFolder() const;
  // Open a pre-filled GitHub "new issue" page carrying version / OS / Qt
  // version / recent-log-tail diagnostics, so a user can file a report in one
  // click.
  // The full diagnostics go to the clipboard, not into the URL.
  Q_INVOKABLE void reportAnIssue();
  // The body reportAnIssue() pre-fills, built from purely local sources: app
  // version, OS, Qt version, the log tail and the recovery log. Split out so the
  // no-network-egress guarantee can be exercised without opening a browser.
  // It travels in a URL, so the tails are short and scrubbed of the home
  // folder and user name (audit PLAT-28).
  Q_INVOKABLE QString issueReportBody() const;
  // The longer log and recovery tails, scrubbed the same way, for the
  // clipboard.
  Q_INVOKABLE QString issueDiagnostics() const;
  // "Report a problem" form (R2-040): a GitHub "new issue" page with the
  // body the user saw in the preview, nothing added.
  Q_INVOKABLE void openIssueReport(const QString& body) const;
  // The form's preview: "lowkey <version> · <OS> · Qt <version>", then the
  // short log and recovery tails, scrubbed.
  Q_INVOKABLE QString issueReportPreview() const;

  // How much one pull actually changed. An issue that came back identical
  // counts as neither, so a quiet auto-sync writes nothing and says so.
  struct MergeStats {
    int added = 0;
    int updated = 0;
    // Cards whose title or description was edited here while the tracker
    // changed the same field: the local text is kept and the user is told.
    int conflicts = 0;
    // Cards whose issue was missing from a complete pull under the same filter.
    int gone = 0;
    // Cards newly left outside a changed filter: kept, not "gone".
    int outOfScope = 0;
    // The keys of the cards behind `conflicts`, so the toast can name them.
    QStringList conflictKeys;
    // The heap ids of the cards behind `added`, in pull order, so the toast
    // can name them and "Show" can filter to them (APP-180).
    QStringList addedIds;
    // External ids whose time in their status is not known yet, the moved
    // ones first: asked of the tracker's history after the merge.
    QStringList statusTimesWanted;
  };

  // The tracker's history answered when its issues entered the status they
  // are in (dogfood 2026-10-19). Each card still in that status keeps the
  // time (ExternalMeta.details.statusSince); a card the tracker put in its
  // column gets it as statusChangedAt, and the status-log entry and history
  // event a pull stamped with the moment it noticed the move are put back at
  // it — which also repairs the dates earlier builds wrote. Never later: a
  // column the user picked here keeps the time they picked it. How many
  // cards changed. Public so the rules run without a tracker.
  int applyTrackerStatusChanges(const QString& providerId, const QVector<heap::integrations::ExternalStatusChange>& changes);
  // The issues of `asked` the history gave no usable answer for count one
  // try each (statustime::kMaxTries); public for tests.
  void countStatusTimeTries(const QString& providerId,
                            const QStringList& asked,
                            const QVector<heap::integrations::ExternalStatusChange>& answered);

  // Fold a batch of pulled external tasks into the model. providerId tags the
  // task's externalProvider; idPrefix seeds ids for newly-created local tasks.
  // Public so the sync merge can be exercised without a live tracker.
  // `complete` says the batch holds every issue the provider's filter
  // matches; only then is a missing issue marked as gone upstream.
  // `goneCandidates`, when given, collects the external ids that would be
  // marked gone instead of marking them: the provider can look them up first
  // (INT-6, audit 2026-09-30).
  // A merge / pull request card whose Jira issue is here goes to the archive,
  // its facts onto the issue's card (APP-293); the count of cards folded.
  // With `archive` false only the facts are put back (a profile loading).
  int foldReviewCardsIntoIssues(bool archive = true);
  MergeStats mergeExternalTasks(const QString& providerId,
                                const QString& idPrefix,
                                const QVector<heap::integrations::ExternalTask>& issues,
                                bool complete = false,
                                QStringList* goneCandidates = nullptr);
  // What a lookup of those candidates found. An issue that still exists only
  // left the filter — closed under "statusCategory != Done", say: it is merged
  // like any pulled issue (so a closed one moves to Done) and marked outside
  // the filter. One the tracker says does not exist is gone. An id in neither
  // list (the lookup failed) is left for the next sync.
  MergeStats settleMissingIssues(const QString& providerId,
                                 const QString& idPrefix,
                                 const QVector<heap::integrations::ExternalTask>& found,
                                 const QStringList& missing);

  // Fold fetched contacts into the active profile's Docs contact list and, for
  // the people actually talked to, the People rail. Returns how many contacts
  // were added or changed — zero means nothing is written, so an idempotent
  // re-sync does not churn docsState. Public for the same reason as
  // mergeExternalTasks: the rules are worth testing without a live server.
  int mergeExternalContacts(const QString& providerId, const QVector<heap::integrations::ExternalContact>& contacts);

  // The one message for one pull's stats: a toast, an event-log entry and,
  // when it brought cards, syncNews naming them (APP-180). Quiet when a
  // follow-up pull found nothing. Public so the wording can be tested
  // without a live tracker.
  void reportSync(const QString& label, const MergeStats& stats, bool settlePull);

  // CI of my merge / pull requests as a source saw it (dogfood 2026-10-20):
  // a run that failed, or passed after running, is a notification, once per
  // run and state across restarts (ci-seen.json). `source` names who saw it
  // ("tracker:gitlab", "git"): the first batch of each after launch is one
  // summary at most. Public so the rules can be tested without a forge.
  void observeCiAt(const QVector<heap::ci::Observation>& seen, const QString& source, const QDateTime& now);

  // ---- Durability audit (HEAP-156) ----
  // Every quarantine / backup recovery / failed write, oldest first. Local file,
  // never uploaded.
  Q_INVOKABLE QVariantList recoveryLog() const;
  // Copy the recovery log to `fileUrl` so it can be attached to a bug report.
  Q_INVOKABLE bool exportRecoveryLog(const QUrl& fileUrl);

  // Test seam: replaces the atomic QSaveFile write behind saveStateNow() so the
  // fault-injection harness can truncate, corrupt, or drop the rename. Pass an
  // empty std::function to restore the default writer.
  using StateWriter = std::function<bool(const QString& path, const QByteArray& bytes)>;
  static void setStateWriterForTesting(StateWriter writer);

  // ---- Auto-update (HEAP-63) ----
  // Manually trigger a GitHub-Releases check (Settings → About → "Check for
  // updates"). Result surfaces via updateStatus + the updateAvailable toast.
  Q_INVOKABLE void checkForUpdates();
  // The check that runs by itself: at launch, then hourly ticks that the
  // updater's gate lets through once a day (or after a failure's back-off).
  // Off with "Only by hand" and in a Flatpak.
  void autoCheckForUpdates();
  // Open the latest release's page in the browser — the "Download" action.
  Q_INVOKABLE void openLatestRelease() const;
  // APP-125: download this copy's package from the latest release and check it
  // against the release's SHA256SUMS; then installUpdate() puts it in place
  // and quits (heap starts again on the new version).
  Q_INVOKABLE void downloadUpdate();
  Q_INVOKABLE void cancelUpdateDownload();
  Q_INVOKABLE void installUpdate();

  // ---- Tracker sync (HEAP-74/75) ----
  QVariantMap providerBadges() const;
  // "GitHub" for "github". The id itself when nothing in the catalog matches.
  QString providerDisplayName(const QString& providerId) const;
  // The issue URL for a task, or an empty URL when it has none or the tracker
  // handed over something that is not a web address. Split out from
  // openTaskExternal so the scheme check is testable without a browser: the
  // value is tracker-supplied, and a Jira session with no site yields a bare
  // "/browse/KEY".
  QUrl externalUrlFor(const QString& taskId) const;
  // Open a mirrored task's issue in the default browser. False (with a toast)
  // when the task has no usable issue URL.
  Q_INVOKABLE bool openTaskExternal(const QString& taskId);
  // Read a mirrored task's most recent comments (HEAP-117). A GET, so it
  // cannot change the issue; nothing is ever sent back. The answer arrives on
  // ticketCommentsLoaded and is kept in a local cache (ticket-comments.json,
  // not state.json) so an opened ticket reads offline (dogfood 2026-10-19);
  // the count goes onto the card.
  Q_INVOKABLE void fetchTicketComments(const QString& taskId);
  // The comments last read for a ticket, from that cache: {comments:
  // [{author, body (markdown), createdAt, url}], total, fetchedAt}; empty
  // when never read. Server/DC wiki markup reads as markdown, like the
  // description.
  Q_INVOKABLE QVariantMap ticketComments(const QString& taskId) const;
  // Whether the cached comments are old enough to read again on opening.
  Q_INVOKABLE bool ticketCommentsStale(const QString& taskId) const;
  // The tracker's estimate of a ticket, read only (dogfood 2026-10-19), in
  // minutes: {originalMinutes, remainingMinutes, spentMinutes, points};
  // keys absent when the tracker did not say. The local estimate is never
  // touched by it.
  Q_INVOKABLE QVariantMap trackerEstimate(const QString& taskId) const;
  // What a day and a week of a tracker are (item 19): {hoursPerDay,
  // daysPerWeek}, the site's own working time once read, Jira's 8h / 5d
  // otherwise and for a local task ("" provider).
  Q_INVOKABLE QVariantMap timeUnits(const QString& providerId) const;
  // An estimate typed as Jira takes it ("1d 2h", "90m", "1,5ч", "1мес"),
  // in that tracker's days and weeks: {ok, minutes}. Pure apart from the
  // config read.
  Q_INVOKABLE QVariantMap parseDuration(const QString& text, const QString& providerId) const;
  // "7ч 30м", "1н 2д": the two largest units, in the UI language.
  Q_INVOKABLE QString formatDuration(int minutes, const QString& providerId) const;
  // CI of a merge / pull request (APP-318), read-only and on demand: the
  // failed jobs (ciJobsLoaded) and the tail of one job's log (ciLogLoaded).
  // A Jira card folded with its MR asks for that MR's. Nothing is stored.
  Q_INVOKABLE void fetchCiJobs(const QString& taskId);
  Q_INVOKABLE void fetchCiLog(const QString& taskId, const QString& jobId);
  // The CI the card shows: {state, url, at, source} ("" state = none).
  Q_INVOKABLE QVariantMap ciOf(const QString& taskId) const;
  // A log line that says something failed (heap::ci::isErrorLine), for QML.
  Q_INVOKABLE bool isCiErrorLine(const QString& line) const;

  // Pull issues from every connected tracker and mirror them as tasks in the
  // active profile. No-op (with a toast) when no provider is configured.
  // By hand (`here`) it is "sync everything, here": each tracker is bound to
  // the active profile first, as one card's "Sync now" binds its own; the
  // auto-sync timer passes false and never moves a tracker.
  Q_INVOKABLE void syncNow(bool here = true);
  // Pull from a single connected provider (the per-card "Sync now" button).
  Q_INVOKABLE void syncProvider(const QString& providerId);
  // Validate the current credentials for one provider and toast the result.
  Q_INVOKABLE void testIntegration(const QString& providerId);
  // Start the browser OAuth flow for a provider; on success stores the access
  // token in the keychain and marks the provider connected (authMode=oauth).
  Q_INVOKABLE void connectOAuth(const QString& providerId);
  // Mark a provider connected from the credentials typed into its card. The
  // counterpart to connectOAuth for every card that is filled in by hand: an
  // OAuth-capable provider used with a personal access token, a self-hosted
  // Jira the Atlassian gateway knows nothing about, a tracker with no browser
  // flow at all. Refuses (with a toast) while a required field is empty, and
  // drops any leftover browser session so the typed credentials are the ones
  // actually sent.
  Q_INVOKABLE void connectIntegrationManually(const QString& providerId);
  // Drop a provider back to disconnected. For a browser/session sign-in this
  // also discards the tokens and clears authMode — otherwise a token pasted
  // afterwards would still be sent as a Bearer, which GitLab (PRIVATE-TOKEN)
  // and ClickUp (raw header) reject.
  Q_INVOKABLE void disconnectIntegration(const QString& providerId);
  // Sign in to a directory provider with a username and password (Mattermost,
  // where most corporate servers have personal access tokens switched off).
  // `credentials` holds the descriptor's loginFields; the password is used for
  // the one request and never persisted — only the session token it returns is.
  Q_INVOKABLE void connectWithCredentials(const QString& providerId, const QVariantMap& credentials);
  // Remember that the user deleted an imported contact, so the next sync does
  // not re-add it. Called by DocsView when a contact carrying an external id is
  // deleted, and undone by restoreExternalContact.
  Q_INVOKABLE void dismissExternalContact(const QString& providerId, const QString& externalId);
  Q_INVOKABLE void restoreExternalContact(const QString& providerId, const QString& externalId);
  // The same for a mirrored issue: deleting the task is how the user says "not
  // mine", and without this the next pull simply puts it back. Recorded by
  // deleteTask, undone by the delete's undo and by restoreExternalTask.
  Q_INVOKABLE void dismissExternalTask(const QString& providerId, const QString& externalId);
  Q_INVOKABLE void restoreExternalTask(const QString& providerId, const QString& externalId);
  // The full integration catalogue (id, name, colour, fields, …) for the
  // Settings → Integrations cards. Data-driven from the provider registry.
  Q_INVOKABLE QVariantList integrationCatalog() const;
  // Secret (token/key) accessors — secrets live in the OS keychain, never in
  // state.json, so QML reads/writes them through these instead of the settings
  // blob.
  Q_INVOKABLE QString integrationSecret(const QString& providerId, const QString& field) const;
  Q_INVOKABLE bool hasIntegrationSecret(const QString& providerId, const QString& field) const;
  Q_INVOKABLE void setIntegrationSecret(const QString& providerId, const QString& field, const QString& value);

  // ---- Status mapping ----
  // StatusMap has always taken per-user overrides and has never been given
  // any: every sync called it with an empty map, so a tracker status heap's
  // built-in table does not recognise — "QA", "Needs triage", anything from a
  // custom Jira workflow — landed in "todo" with no way to say otherwise.
  //
  // The rows to offer: every distinct status this provider has actually sent,
  // recorded as issues are merged, each with the column it currently resolves
  // to and whether that is the user's choice or the built-in guess.
  // [{ status, column, overridden, known, category, group }], sorted by
  // status. With the statuses of the synced projects known (Jira), those
  // are the rows, `group` the project each first appears in; a status only
  // seen on an issue, or only mapped by hand, comes after with group "".
  Q_INVOKABLE QVariantList statusMappingFor(const QString& providerId) const;
  // The statuses of the projects a provider syncs (projectStatusesFetched),
  // each project's list replacing the one stored before; statuses no
  // mirrored issue of this provider is in drop out of the seen list, which
  // used to hold the whole instance's (dogfood 2026-10-20).
  void rememberProjectStatuses(const QString& providerId, const QVector<heap::integrations::ExternalStatus>& statuses);
  // Settings shows the mapping: read the synced projects' statuses again if
  // the last read is over 5 minutes old (throttled by the provider).
  Q_INVOKABLE void refreshTrackerStatuses(const QString& providerId);
  // The tracker's working time (hours per day, days per week), kept as
  // integrations.<id>.timeTracking for trackerEstimate (item 19).
  void rememberTrackerTimeTracking(const QString& providerId, const QJsonObject& config);

  // Tests: the clock the throttled tracker reads (pictures, statuses) go by.
  void setFetchClockForTest(std::function<QDateTime()> now) {
    m_fetchClock = std::move(now);
  }
  // Map one of this provider's statuses onto a heap column. An empty or
  // unknown \p column clears the override and restores the built-in guess.
  // Remapping does not rewrite tasks already mirrored; the next sync does.
  Q_INVOKABLE void setStatusMapping(const QString& providerId, const QString& status, const QString& column);

  // ---- Notifications & automation ----
  Q_INVOKABLE void notify(const QString& title, const QString& body, const QString& kind = QString());
  // Post the given rich payload via the native notification backend.
  // `taskId` is the optional task tied to this toast — it is encoded into
  // the notification id so the action handlers can route back.
  Q_INVOKABLE void notifyTask(const QString& taskId, const QString& title, const QString& body, const QString& kind = QString());
  // notifyTask() judged at `now` rather than the wall clock: runAutomationAt()
  // decides quiet hours for its own moment, and so must what it posts.
  void notifyTaskAt(const QString& taskId, const QString& title, const QString& body, const QString& kind, const QDateTime& now);
  // The confirmation for something quick-captured from outside the app (the
  // global hotkeys' capture window). Unlike notify(), it is shown while a heap
  // window has focus — the capture window has it — and in quiet hours, since
  // the user just asked for it. Clicking it opens `taskId` when there is one.
  Q_INVOKABLE void notifyCapture(const QString& taskId, const QString& title, const QString& body);

  // An in-app toast from QML, for a view that refuses a key and says why.
  Q_INVOKABLE void showToast(const QString& message) {
    emit toast(message, QStringLiteral("info"));
  }
  // Slide the deadline of \p taskId forward by \p seconds (no-op if the
  // task currently has no deadline). Invoked by the "Snooze 1h" action.
  Q_INVOKABLE void snoozeDeadline(const QString& taskId, int seconds);

  // ---- Start at login (APP-154) ----
  // { supported, enabled, minimized }, read from the OS entry on every call so
  // Settings shows what the next login will really do. While the entry is off,
  // `minimized` is the remembered choice (settings.system.startMinimized).
  Q_INVOKABLE QVariantMap autostartState() const;
  // Writes or removes the OS entry and remembers the choice under
  // settings.system. Starting at login implies staying in the tray: an
  // unanswered close-to-tray question is answered "hide to tray". False (and
  // an error toast) when the OS refused the change.
  Q_INVOKABLE bool setAutostart(bool enabled, bool minimized);

  // ---- Reminder buttons (APP-155) ----
  // A heap://notify URI from a toast click, forwarded by the launch the shell
  // started (main.cpp). Routes to the same handlers as a native button.
  Q_INVOKABLE bool handleNotificationUri(const QString& uri);
  // A sample reminder with the real buttons, from Settings → Notifications,
  // shown even while heap has focus: it is how the user checks that the OS
  // lets heap's notifications through.
  Q_INVOKABLE void sendTestNotification();
  // Puts a shown reminder off: it comes back `minutes` after `now`, with the
  // same text, through the usual quiet-hours rules. Kept in snoozes.json.
  void snoozeReminderAt(const QString& notificationId, int minutes, const QDateTime& now);
  // A button of an in-app reminder toast: the same as the OS one (APP-256).
  Q_INVOKABLE void reminderAction(const QString& notificationId, const QString& actionId);
  // "Remind me" on a task (APP-319): a moment ("15:00", "завтра 9:00",
  // "через 20 мин"), a lead before the task's own time ("за 15 мин", "15m"),
  // or "" to take it off. One undo step; false (and a toast saying why) when
  // the text is not a time or a lead has no time to count from.
  Q_INVOKABLE bool setTaskReminder(const QString& taskId, const QString& text);
  bool setTaskReminderAt(const QString& taskId, const QString& text, const QDateTime& now);

  // Snoozed reminders waiting (tests).
  QVector<heap::notify::SnoozedReminder> pendingSnoozes() const {
    return m_snoozed;
  }

  // Review 2 (R3-095): the git "working on" line is off for a new profile,
  // but data from before 0.8.1 had it on. Run once for such data (settings
  // without `gitLineDefault`): an unset switch is written as on, a set one
  // stays. Returns whether `app` changed.
  static bool migrateGitWorkingLine(QJsonObject& app);
  // 0.8.1: every profile moves to the lowkey look of the design references
  // once — the dark slot to "heap-ink", the light one to "heap-light". The
  // user's own themes stay in the list. Returns whether `app` changed.
  static bool migrateLowkeyTheme(QJsonObject& app);
  // lowkey -> Tabless (APP-283): a tracker bound to "lowkey-example" follows
  // the example profile to its new id. True if anything changed.
  static bool migrateExampleProfileBindings(QJsonObject& app);
  // The example profile itself, its events, links into it and the active id.
  // True if it was renamed (the file is saved again).
  bool migrateExampleProfileId(QVector<CalEvent>& events);

  // The tray menu as the OS shows it (R3-103): [{ id, text, hint, enabled }],
  // an empty id for a separator. For tests and capture harnesses.
  Q_INVOKABLE QVariantList trayMenuItems();

  // ---- Event ops ----
  // A fresh event id. Shared by the draft and the series edits, which both
  // need one and must not invent different shapes.
  static QString mintEventId();
  Q_INVOKABLE QVariantMap newEventDraft(double startHour, const QDate& date) const;
  Q_INVOKABLE void saveEvent(const QVariantMap& draft);
  Q_INVOKABLE void updateEvent(const QString& id, double start, double end, const QDate& date);
  Q_INVOKABLE void deleteEvent(const QString& id);

  // ── Recurrence ──
  //
  // A repeating event is stored once, as a master carrying an RRULE. The dates
  // it lands on are computed here rather than written out: a daily standup
  // would otherwise be a row per working day forever, and a merge conflict for
  // every one of them.
  //
  // `eventOccurrences` is what the views read — plain event maps with real
  // dates, plus `masterId` and `originalDate` on anything generated, so a click
  // can find the series again.
  Q_INVOKABLE QVariantList eventOccurrences(const QDate& from, const QDate& to) const;
  // The Today screen's day (APP-260): {date, workday, workStart, workEnd,
  // fromHour, toHour, allDay[], blocks[], free[], load{meetings, tasks, free,
  // overWork} (minutes), facts{meetings, planned, dueToday}, inProgress[],
  // deadlines[], overdue[], people[], undated}. The active profile's tasks
  // and everyone's meetings; `allProfiles` adds every profile's tasks.
  // deadlines[] holds the day and the deadlineHorizon() days after it, by day;
  // deadlineDays[] = {date, offset (from today), kind, first, count} says
  // where each day starts in it (plan/DueHorizon.h).
  Q_INVOKABLE QVariantMap todayData(const QDate& date, bool allProfiles = false) const;
  // Today's "Ближайшие": how many days after the shown day its deadlines
  // reach, from settings.calendar.deadlineHorizon, snapped to the choices.
  Q_INVOKABLE int deadlineHorizon() const;
  Q_INVOKABLE QVariantList deadlineHorizonChoices() const;
  // The first meeting of `now`'s day that has not started yet ({} = none):
  // the tray menu's "Далее: 11:00 1:1 с Олегом" line (R3-103).
  Q_INVOKABLE QVariantMap nextEventAfter(const QDateTime& now) const;

  // "Не беспокоить 1 ч": notifications are held as in quiet hours until
  // `now` + minutes (settings notifications.dndUntil).
  Q_INVOKABLE void doNotDisturbFor(int minutes, const QDateTime& now);
  // The same day as a pure fact for the calendars (APP-247): load minutes.
  Q_INVOKABLE QVariantMap dayLoad(const QDate& date) const;
  // The stored event behind an occurrence: the master, or the override that
  // stands in for it. Empty when there is none.
  Q_INVOKABLE QVariantMap eventSeriesMaster(const QString& masterId) const;

  // Scope is "this", "following" or "all" — the three answers every calendar
  // asks for when a repeating event is edited or deleted.
  Q_INVOKABLE void saveOccurrence(const QVariantMap& draft, const QString& scope);
  Q_INVOKABLE void deleteOccurrence(const QString& masterId, const QDate& occurrenceDate, const QString& scope);
  // A drag or a resize on the grid, answered with the same scope the editor
  // asks for. `occurrence` is the map eventOccurrences handed the view.
  // `deltaHours` moves the whole event (a day is 24), so the piece of an
  // overnight event after midnight moves the event it belongs to, by as much
  // as it was dragged. A resize sets a single-day occurrence's own edges.
  Q_INVOKABLE void moveOccurrence(const QVariantMap& occurrence, double deltaHours, const QString& scope);
  Q_INVOKABLE void resizeOccurrence(const QVariantMap& occurrence, double start, double end, const QString& scope);
  // The stored event as the editor reads it: every field, on the viewer's
  // clock, with the zone it is kept in as "tz" (and "srcTz"/"srcStart" when
  // that zone is not the viewer's).
  Q_INVOKABLE QVariantMap eventById(const QString& id) const;
  // ── Time zones (A6-7) ──
  // The event editor's zone picker: zones for what was typed ("new york",
  // "EST", "UTC+5", "москва"), each {id, city, region, offset}; with nothing
  // typed, the zones the user's meetings are kept in.
  Q_INVOKABLE QVariantList timeZoneSearch(const QString& query) const;
  // `hour` on `date` on the viewer's clock, as the hour in zone `tz` (NaN
  // for no zone), and how many days that is from `date` there (-1, 0, 1).
  Q_INVOKABLE double zoneHour(const QDate& date, double hour, const QString& tz) const;
  Q_INVOKABLE int zoneDayShift(const QDate& date, double hour, const QString& tz) const;
  // "New York" for America/New_York, "UTC−5" for a fixed offset.
  Q_INVOKABLE QString zoneName(const QString& tz) const;
  // Whether `tz` is the viewer's own zone (or none).
  Q_INVOKABLE bool isLocalZone(const QString& tz) const;
  // The viewer's own zone id, and a zone's offset now ("UTC+3"; "" is the
  // viewer's).
  Q_INVOKABLE QString localZoneId() const;
  Q_INVOKABLE QString zoneOffsetLabel(const QString& tz) const;
  QStringList recentEventZones() const;
  // Whether heap can expand this RRULE body — what the editor's custom field
  // checks before it saves.
  Q_INVOKABLE bool isValidRRule(const QString& rule) const;

  // ── .ics ──
  //
  // Import returns a summary rather than a bool: a file that brought in nine
  // events and skipped one is neither a success nor a failure, and the user
  // has to be told which. Keys: imported, updated, skipped, warnings.
  Q_INVOKABLE QVariantMap importIcs(const QUrl& fileUrl);
  Q_INVOKABLE bool exportIcsToFile(const QUrl& fileUrl) const;
  // "<install id>.heap": what qualifies this install's event ids as .ics UIDs,
  // so another heap's ev-2 is not taken for ours (TIME-25).
  QString icsUidDomain() const;

  // ── Calendar subscriptions (APP-118) ──
  // A calendar heap reads from a link — Outlook's published ICS address, or
  // Google's / iCloud's — and refreshes on its own. Read-only: its events are
  // "sub:<id>:…" and every event write refuses them. The link is a secret
  // (anyone holding it reads the calendar) and lives in the keychain; the
  // list itself is settings.calendars.subscriptions [{ id, name, minutes }].
  //
  // Each entry: { id, name, minutes, events, lastSync (ISO, "" = never),
  // error ("" = fine), busy, live, minMinutes }. `live` is the desktop
  // Outlook telling heap about every change (the timer is then only a safety
  // net); `minMinutes` the shortest cadence the kind allows.
  QVariantList calendarSubscriptions() const;
  // { ok, id } or { ok: false, error }. Fetches straight away.
  Q_INVOKABLE QVariantMap addCalendarSubscription(const QString& name, const QString& link, int minutes);
  // The Outlook installed on this computer, read over COM (Windows): the way
  // in when an Exchange server will not publish a calendar as a link. Same
  // read-only subscription, kind "outlook", no link.
  Q_INVOKABLE bool outlookDesktopAvailable() const;
  Q_INVOKABLE QVariantMap addOutlookDesktopCalendar(int minutes);
  // Drops the subscription, its link and the events it brought.
  Q_INVOKABLE void removeCalendarSubscription(const QString& id);
  Q_INVOKABLE void refreshCalendarSubscription(const QString& id);
  // A calendar's refresh interval, after it was added: 1 minute and up for
  // the Outlook on this computer, 5 and up for a link, a day at most.
  Q_INVOKABLE void setCalendarSubscriptionMinutes(const QString& id, int minutes);
  Q_INVOKABLE bool isSubscriptionEvent(const QString& id) const;
  // The name of the calendar an event came from, "" for the user's own.
  Q_INVOKABLE QString subscriptionNameOf(const QString& eventId) const;
  // What a fetch does with the body it got, without the network: parse, then
  // replace the subscription's events. Returns how many events it now holds,
  // or -1 with `error` set when the text is not a calendar.
  int applyCalendarSubscriptionFeed(const QString& id, const QByteArray& body, QString* error = nullptr);
  // Replaces subscription `id`'s events with `incoming` (already prefixed).
  // Returns how many single events and series it now holds.
  int replaceSubscriptionEvents(const QString& id, const QVector<CalEvent>& incoming);
  Q_INVOKABLE void scheduleTask(const QString& taskId, double startHour, const QDate& date);
  // First hour on `date` where a block of `durationHours` does not land on top
  // of an existing event, starting from the workday (or from now, for today).
  // The "schedule this" menu item used to hardcode 14:00 and stack blocks.
  Q_INVOKABLE double nextFreeSlot(const QDate& date, double durationHours) const;
  // Books the task into the first gap on `date` that fits the block it will
  // actually get (its estimate, else the focus-block length).
  Q_INVOKABLE void scheduleTaskAtNextFreeSlot(const QString& taskId, const QDate& date);
  // How long a block for this task is, in minutes.
  Q_INVOKABLE int taskBlockMinutes(const QString& taskId) const;
  // The nearest free window for the task's block on `date` (APP-253): busy is
  // meetings and task blocks, the window inside the working hours of a
  // working day. {found, date, start, hours, nextDate, nextStart, lateStart}
  // — when today has no room, the next working day's first window and what
  // is left of this evening (-1 when nothing fits before midnight).
  Q_INVOKABLE QVariantMap freeWindow(const QString& taskId, const QDate& date) const;
  QVariantMap freeWindowAt(const QString& taskId, const QDate& date, const QDateTime& now) const;
  // Plans the task at `start` on `date` (the answer to "no room today").
  // Only "when" changes; one undo step with its toast.
  Q_INVOKABLE bool placeTaskAt(const QString& taskId, const QDate& date, double start);
  // Carrying leftovers on by hand (APP-248). `mode`: "tomorrow" (keeps the
  // time it had), "window" (the nearest free window from now on), "someday"
  // or "clear" (no "when"); the deadline is touched only by "clearDue", which
  // removes it and nothing else. One undo step and one toast for the whole
  // selection (it says when nothing changed, or when a deadline keeps a task
  // overdue); returns how many tasks changed.
  Q_INVOKABLE int carryTasks(const QStringList& ids, const QString& mode);
  int carryTasksAt(const QStringList& ids, const QString& mode, const QDateTime& now);
  // Drag-to-reschedule in Week, Month and Timeline (APP-249), and the keys
  // that do the same. Sets one date of one task: `field` is "scheduled" (when
  // it is planned) or "due" (its deadline). `hasTime` says whether the clock
  // part of `when` is real; an invalid `when` clears the field. Local only —
  // nothing is sent to a tracker, and a tracker's deadline changed here stays
  // the local value (sync keeps a deadline the user edited). A focus block
  // that the old schedule came from moves with it. One undo step, with an
  // undo toast. False when the task is unknown or nothing changed.
  Q_INVOKABLE bool rescheduleTask(const QString& taskId, const QString& field, const QDateTime& when, bool hasTime);
  Q_INVOKABLE bool clearTaskDate(const QString& taskId, const QString& field);
  // A task's own block on the week grid, moved or stretched: the schedule
  // goes to `date` at `startHour`, and the length becomes the estimate. One
  // undo step.
  Q_INVOKABLE bool resizeTaskBlock(const QString& taskId, const QDate& date, double startHour, double endHour);
  // A "doing" column gets a focus block for a card that enters it and gives
  // the future ones back when the card leaves. In Progress always is.
  Q_INVOKABLE bool isDoingStatus(const QString& statusId) const;
  Q_INVOKABLE void setStatusDoing(const QString& statusId, bool doing);
  Q_INVOKABLE QString scheduledLabelFor(const QString& taskId, const QDate& date) const;
  // The tasks a calendar range shows, pre-sorted by day, for the week and
  // month views: those due in [from, to] and those scheduled in it. Each map
  // carries the fields the views filter and draw with, plus `dueDay` and
  // `schedDay` — the day's offset from `from`, or -1. Only candidates have
  // their fields read, which is what keeps a 10k-task profile's week cheap.
  Q_INVOKABLE QVariantList calendarTasks(const QDate& from, const QDate& to, bool includeArchived) const;
  // The occurrences ahead of each open repeating task that fall in [from,
  // to] (A2-25), worked out from its rule and drawn faintly by Week and
  // Month: [{id, title, priority, date, day, hasTime, hour, recurrence}],
  // `day` the offset from `from`. Display only: nothing is created ahead.
  Q_INVOKABLE QVariantList calendarRepeats(const QDate& from, const QDate& to) const;
  // A repeat said in words, as the editors' "Custom" row takes it (B7):
  // "every 3 weeks", "каждые 2 дня", "mon, wed", "пн, ср", "2 weeks". The
  // engine's token ("every:3:week"), or "" when the words are not a repeat.
  Q_INVOKABLE QString recurrenceFromText(const QString& text) const;
  // The load of each day from `from` on (APP-247): [{date, workday,
  // meetings, tasks, free, overWork, work}] in minutes, each minute counted
  // once — the fact the calendar's day headers show.
  Q_INVOKABLE QVariantList dayLoads(const QDate& from, int days) const;
  // The calendar's "Without a date" tray (APP-264): what "is:undated" finds,
  // narrowed by the section's query. [{id, title, status, category,
  // priority, blockMinutes}].
  Q_INVOKABLE QVariantList undatedTasks(const QString& search) const;

  // ---- People ops ----
  Q_INVOKABLE void cyclePerson(const QString& id);
  Q_INVOKABLE void setPersonState(const QString& id, const QString& state);
  // "Кому написать" (APP-321): the open asks, people with state "todo", as
  // Today and the sidebar list them: [{id, name, question, color}].
  Q_INVOKABLE QVariantList openPings() const;
  // The sidebar's "Кому написать" (dogfood 2026-10-20): the asks to write
  // ("todo") first, then the ones waiting for a reply ("pinged"), as
  // [{id, name, question, color, state, waiting, stateAt, ago, remindAt,
  // remindHasTime, when, overdue}]. `ago` is how long the reply has been
  // waited for ("2 ч"), `when` when to write ("завтра 15:00").
  Q_INVOKABLE QVariantList pingRows() const;
  QVariantList pingRowsAt(const QDateTime& now) const;
  // The "Когда написать" field read against now (heap::people::resolvePingWhen):
  // {ok, empty, at, hasTime, rolled, error, label}. A bare time already gone
  // today is tomorrow's; another moment in the past is refused ("past").
  Q_INVOKABLE QVariantMap parsePingWhen(const QString& text) const;
  QVariantMap parsePingWhenAt(const QString& text, const QDateTime& now) const;
  Q_INVOKABLE QVariantMap newPersonDraft() const;
  Q_INVOKABLE QVariantMap personById(const QString& id) const;
  // What lowkey knows links to the person (DG-002): tasks waiting on them
  // ({id, key, title}) and their upcoming meetings ({id, date, start, allDay,
  // title}), matched on the meeting's attendees line.
  Q_INVOKABLE QVariantMap personLinks(const QString& id) const;
  // The person an "@handle" in a note names, or empty.
  Q_INVOKABLE QString personIdForHandle(const QString& handle) const;
  // Finding someone by what was typed (heap::text::personMatchRank): the id,
  // a login derived from the name ("r.losev", "roman.losev"), or the name's
  // words in either script ("roman lo", "роман"). personMatchRank is the
  // rank for one candidate (0 = no match); matchPeople is the active
  // profile's people, best first, at most `limit` of them, as
  // {id, name, role, color, rank}.
  Q_INVOKABLE int personMatchRank(const QString& query, const QString& name, const QString& id) const;
  Q_INVOKABLE QVariantList matchPeople(const QString& query, int limit) const;
  Q_INVOKABLE bool savePerson(const QVariantMap& draft);
  Q_INVOKABLE void deletePerson(const QString& id);

  // ---- People picker ----
  // Everyone the rail's "+" can put on the list: the profile's Docs contacts
  // and its People, folded into one row per human. A contact that already has
  // a Person behind it (`personId`, written by the Mattermost import) is that
  // Person's row, so a colleague imported from a DM is offered once, not twice.
  // Each entry carries { key, contactKey, personId, name, role, handle,
  // channel, color, state, source, active }.
  Q_INVOKABLE QVariantList pingCandidates() const;
  // The PersonEditor draft for a picked candidate. An existing Person opens as
  // itself, moved to "todo" when it was idle; a contact with no Person yet
  // opens as a new one carrying the contact's name, role and colour.
  Q_INVOKABLE QVariantMap pingDraftFor(const QVariantMap& candidate) const;
  // The draft behind "create contact «name»": a new Person plus, on save, a
  // Docs contact to find it by next time.
  Q_INVOKABLE QVariantMap newContactDraft(const QString& name) const;

  Q_INVOKABLE int pendingPeopleCount() const {
    return m_people.todoCount();
  }

  // ---- Status (kanban column) ops ----
  Q_INVOKABLE void addStatus(const QString& typedName, const QString& color = QString());
  Q_INVOKABLE void renameStatus(const QString& id, const QString& typedName);
  Q_INVOKABLE void setStatusColor(const QString& id, const QString& color);
  // A column's stage (APP-259): backlog / todo / prog / half / blocked /
  // review / done. It is the shape of the status mark and where Done goes.
  Q_INVOKABLE QString statusCategory(const QString& id) const;
  Q_INVOKABLE void setStatusCategory(const QString& id, const QString& category);
  // Columns whose stage was assigned by the upgrade, not picked: shown once
  // so the user can check them.
  Q_INVOKABLE QStringList columnsWithGuessedCategory() const;
  // Advisory limit on how many cards a column should hold. 0 = none.
  Q_INVOKABLE void setStatusWipLimit(const QString& id, int limit);
  // Auto-archive per column (APP-122): a card that has sat in the column for
  // `days` days goes to the archive; 0 = never. Done keeps its Settings →
  // Tasks slider as the one place its number lives; any other column stores
  // it on the column.
  Q_INVOKABLE int statusArchiveDays(const QString& id) const;
  Q_INVOKABLE void setStatusArchiveDays(const QString& id, int days);
  Q_INVOKABLE void moveStatus(const QString& id, int newIndex);
  // `into`: the column its tasks move to (X-Dlg-Small "Перенести задачи в",
  // R2-041); empty or unknown = the first remaining column.
  Q_INVOKABLE void deleteStatus(const QString& id, const QString& into = QString());

  // ---- Status counts ----
  // Every status' task count, built in one pass and cached until the model
  // changes. Prefer this over repeated countByStatus calls: the rail and the
  // top bar used to ask for six separate counts per task edit, each a full
  // scan. Archived tasks are not counted; "_total" is the live task count.
  QVariantMap statusCounts() const;

  QVariantMap taskTitles() const {
    return m_taskTitles;
  }

  QVariantMap taskKeys() const {
    return m_taskKeys;
  }

  QVariantMap taskStages() const {
    return m_taskStages;
  }

  // ---- Saved views (src/AppControllerSavedViews.cpp) ----
  // `state` is the filter state as Main.qml holds it: {query, priorities
  // (list or {P0: true} map), sort, archived, showDone, view}. Every mutation
  // is one undo step with a toast. Names are made unique ("Name (2)").
  QVariantList savedViews() const;
  QVariantMap savedViewCounts() const;

  QString strictQuery() const {
    return m_strictQuery;
  }

  void setStrictQuery(const QString& q);
  // Returns the new view's id; "" when there is no profile.
  Q_INVOKABLE QString saveView(const QString& name, const QVariantMap& state);
  Q_INVOKABLE bool renameSavedView(const QString& id, const QString& name);
  // Overwrites the view's filters with `state`, keeping its name and place.
  Q_INVOKABLE bool updateSavedView(const QString& id, const QVariantMap& state);
  Q_INVOKABLE QString duplicateSavedView(const QString& id);
  Q_INVOKABLE bool deleteSavedView(const QString& id);
  // -1 = up, +1 = down; false at either end.
  Q_INVOKABLE bool moveSavedView(const QString& id, int delta);
  // {} when there is no such view.
  Q_INVOKABLE QVariantMap savedView(const QString& id) const;
  // True when `state` no longer matches the view — what shows it as modified.
  Q_INVOKABLE bool savedViewDiffers(const QString& id, const QVariantMap& state) const;
  // The names of the views whose query filters on the column `statusId`:
  // deleting the column drops that clause and widens them (IDIOT-TASKS-11).
  Q_INVOKABLE QStringList savedViewsUsingStatus(const QString& statusId) const;

  // settingsMap() is private and also cached; this exists so a test can prove
  // the cache does not outlive the settings it was built from.
  QVariantMap settingsMapForTest() const {
    return settingsMap();
  }

  // Every task holding the status, archived ones included (what a column
  // delete re-homes).
  Q_INVOKABLE int countByStatus(const QString& statusId) const;

  // ---- Lookups ----
  Q_INVOKABLE QVariantMap taskById(const QString& id) const;

  // Compile what the user typed in the search box into something a view can
  // filter with. The board hands the raw text to TaskFilterProxy, which does
  // this in C++; the views that build their own JS arrays (archive, timeline,
  // week, month) call this instead, so one search box means the same thing
  // everywhere.
  //
  // Returns { isQuery, freeText, ids }: `freeText` is the loose words with the
  // clauses removed, already lowercased for SearchTextRole; `ids` lists the
  // tasks of the active profile that satisfy the clauses, and is meaningful
  // only when `isQuery` is true (with no clauses every task would be in it).
  Q_INVOKABLE QVariantMap compileSearch(const QString& text) const;
  // The command line (APP-267): `text` in the language quick capture speaks
  // read as clauses ({words, kind, clause, value}) and the words left, and the
  // tasks of the active profile it finds — `limit` of them, open before
  // archived, the one whose id was typed first — with how many there are.
  Q_INVOKABLE QVariantMap commandLine(const QString& text, int limit = 50) const;

  // Does this text hold at least one clause? Parsing only — it never walks the
  // task list, so the search field can ask on every keystroke to show whether
  // it is filtering structurally.
  Q_INVOKABLE bool searchIsQuery(const QString& text) const;
  // The tokens of `text` that look like clauses but mean nothing (an unknown
  // field, column, priority or date), for the search box to point out.
  Q_INVOKABLE QStringList searchProblems(const QString& text) const;

  // The clause fields the search box understands ("deadline", "status", …),
  // sorted. For the hint under the field.
  Q_INVOKABLE QStringList searchFields() const;
  Q_INVOKABLE QString eventHourLabel(double hour) const;
  // The Jira sprint in progress when a card is in one (APP-255), else the ISO
  // week ("wk 41").
  Q_INVOKABLE QString sprintLabel() const;
  // Jira sprints ending in [from, to] (APP-255), read-only facts from the last
  // pull: [{name, state, start, end, endDay (days after `from`), tasks}], by
  // end date. For the sprint marker on the week, the month and the list.
  Q_INVOKABLE QVariantList sprintMarkers(const QDate& from, const QDate& to) const;
  // The active sprint ({name, start, end, daysLeft}), empty when none.
  Q_INVOKABLE QVariantMap currentSprint() const;
  Q_INVOKABLE QString humanDate(const QDate& date) const;
  // Displayed dates in the UI language (APP-188): the pattern of a named
  // style ("dayMonth", "weekdayDay", "longWeekday"… see text/LocaleFormat.h)
  // for `lang` (empty = the current one). QML formats through I18n.date(),
  // which passes its own lang so the binding repaints on a language switch.
  Q_INVOKABLE QString datePattern(const QString& style, const QString& lang = QString()) const;
  // "Oct 6" / "6 окт." and "Oct 6, 15:15" — the clock follows the 12h/24h
  // setting.
  Q_INVOKABLE QString dateLabel(const QDate& d, const QString& style) const;
  Q_INVOKABLE QString dateTimeLabel(const QDateTime& dt, const QString& style) const;

  // ---- Timeline / week helpers ----
  Q_INVOKABLE QString deadlineBucket(const QDate& deadline) const;  // overdue/today/tomorrow/thisweek/nextweek/later/nodl
  Q_INVOKABLE QString deadlineDiffLabel(const QDate& deadline) const;
  Q_INVOKABLE QString shortDate(const QDate& d) const;  // "Fri, May 15" / "пт, 15 мая"
  Q_INVOKABLE int isoWeekNumber(const QDate& d) const;

  // ---- Free-form datetime parser (heap chrono) ----
  Q_INVOKABLE QVariantMap parseDateTime(const QString& input, const QDateTime& reference = QDateTime()) const;
  Q_INVOKABLE QVariantList parseAllDateTimes(const QString& input, const QDateTime& reference = QDateTime()) const;

  // ---- Opt-in timing (APP-161, diag/PerfLog.h) ----
  // A popup calls this as it starts to show: span `name` (begun earlier by the
  // global hotkey, or now) ends on the next frame `item`'s window presents, and
  // is logged. A no-op unless HEAP_PERF_LOG=1 or --perf-log.
  Q_INVOKABLE void perfMarkShown(const QString& name, QObject* item) const;

  Q_INVOKABLE void copyToClipboard(const QString& text);
  // A task's key ("APP-123") onto the clipboard, and a toast that says so.
  Q_INVOKABLE void copyTaskKey(const QString& key);

  // ---- Free-form text classification (used by QuickCapture / TaskEditor) ----
  // Returns one of "focus" | "sync" | "ticket" | "contact" | "none".
  Q_INVOKABLE QString classifyTaskKind(const QString& text) const;
  // For a "sync": the event type it books — "standup" | "oneone" | "sync" |
  // "none" (a one-off call or meeting).
  Q_INVOKABLE QString meetingType(const QString& text) const;
  // Returns { title, desc, handles: [..], ticketKey, priority }. With
  // keepTicketKey the key stays in the title (see heap::text::extractMeta).
  Q_INVOKABLE QVariantMap extractTaskMeta(const QString& text, bool keepTicketKey = false) const;
  // Suggest a slug-style person id ("e.zaharov") from a free-form name.
  // Avoids collisions with already-existing ids in the active profile
  // by appending "-2", "-3", … on conflict.
  Q_INVOKABLE QString suggestPersonId(const QString& name, const QString& exceptId = QString()) const;
  // The collision walk behind suggestPersonId(), over a base that is already
  // an id rather than a display name — a Mattermost handle, say, whose dots
  // the name slugifier would eat. Returns `base`, or `base-2`, `base-3`… when
  // somebody else already holds it.
  QString uniquePersonId(const QString& base, const QString& exceptId = QString()) const;

  // ---- Profiles ----
  QVariantList profiles() const;

  QString activeProfileId() const {
    return m_activeProfileId;
  }

  void setActiveProfileId(const QString& id);
  Q_INVOKABLE QString createProfile(const QString& name, const QString& color = QString());
  Q_INVOKABLE void renameProfile(const QString& id, const QString& newName);
  Q_INVOKABLE void setProfileColor(const QString& id, const QString& color);
  Q_INVOKABLE void deleteProfile(const QString& id);
  Q_INVOKABLE QString duplicateProfile(const QString& id, const QString& newName);
  Q_INVOKABLE QVariantMap profileById(const QString& id) const;
  // Moves tasks of the active profile to `profileId` (A5-14): history,
  // checklist, local layer and timer sessions with them; a local id the
  // target holds is renumbered and what pointed at it follows. A tracker card
  // stays (it belongs to the profile whose integration pulled it) and is
  // counted. One undo step, on both profiles' stacks. Returns {moved,
  // trackerKept, renamed: {old: new}, profileName, error}; error "noProfile",
  // "sameProfile", "tracker" (only tracker cards asked for) or "nothing".
  Q_INVOKABLE QVariantMap moveTasksToProfile(const QStringList& ids, const QString& profileId);

  // The picker for `ids`, from wherever a task menu is (moveToProfileRequested).
  Q_INVOKABLE void requestMoveToProfile(const QStringList& ids) {
    emit moveToProfileRequested(ids);
  }
  // Whether a profile other than `exceptId` is already called `name`
  // (ignoring case): the editor says so inline (IDIOT-SHELL-12).
  Q_INVOKABLE bool profileNameTaken(const QString& name, const QString& exceptId) const;
  // The longest profile name kept (IDIOT-SHELL-16).
  static constexpr int kMaxProfileName = 64;

  // Rewrite every task id that starts with `oldPrefix-<digits>` to use
  // `newPrefix-<digits>`. CalEvent.taskId backlinks are kept in sync so
  // scheduled-task labels stay attached. Returns the number of tasks
  // renamed. Used by Settings → Tasks when toggling idPrefix.
  Q_INVOKABLE int renameTaskIdPrefix(const QString& oldPrefix, const QString& newPrefix);

  // The id the next task under this prefix gets, for the setting's example
  // (EYE-29: "APP-4, APP-5…" beside a profile at APP-313).
  Q_INVOKABLE QString nextTaskIdFor(const QString& prefix) const {
    return mintTaskId(prefix.trimmed().toUpper());
  }
  // Profile JSON import / export (replaces the older Markdown export).
  Q_INVOKABLE QString exportActiveProfileJson() const;
  Q_INVOKABLE bool exportActiveProfileToFile(const QUrl& fileUrl) const;
  // Render a Markdown summary of the active profile (tasks grouped by column,
  // people, notes) and put it on the clipboard. Bound to the profile.exportMd
  // shortcut (Ctrl+Shift+E).
  Q_INVOKABLE void copyActiveProfileMarkdownToClipboard();
  // Weekly "what I shipped" report (HEAP-78): done tasks from the last 7 days
  // with tracked time, copied to the clipboard as Markdown.
  Q_INVOKABLE void copyWeeklyReportToClipboard();
  // The Monday recap (WEAK PECAP): last week's column moves, grouped by
  // from -> to. { weekStart, weekEnd ("yyyy-MM-dd", end exclusive),
  // groups: [{ from, fromName, fromColor, to, toName, toColor,
  // tasks: [{ id, title, priority }] }] }, groups in board column order.
  Q_INVOKABLE QVariantMap weeklyRecap() const;
  // The same for the week before the one `today` falls in.
  Q_INVOKABLE QVariantMap weeklyRecapFor(const QDate& today) const;
  // The facts of the week `day` falls in, for the recap (DG-123):
  // { weekStart, weekEnd (Monday, Sunday), closed: [{ id, title, closedAt }]
  // oldest first, perDay: [7 counts, Monday first] } - the active profile's
  // tasks marked done in that week.
  Q_INVOKABLE QVariantMap weekFacts(const QDate& day) const;

  // Every recorded move of the active profile, oldest first.
  QVector<StatusChange> statusLog() const {
    return m_statusLog;
  }
  Q_INVOKABLE QString importProfileFromJson(const QString& jsonText, bool activate = true);
  Q_INVOKABLE QString importProfileFromFile(const QUrl& fileUrl, bool activate = true);
  // The import preview (N-Dlg-Log-Import, R3-080): what a profile export holds
  // before anything is written — { file, name, version, tasks, notes, views,
  // events, activeName } or { error }.
  Q_INVOKABLE QVariantMap previewProfileImport(const QUrl& fileUrl);
  // Merges an export into the active profile: what it has that the profile
  // does not is added; an item whose id is already here is left as it is and
  // named in the toast. Returns an error text, empty on success.
  Q_INVOKABLE QString mergeProfileFromFile(const QUrl& fileUrl);

  Q_INVOKABLE QVariantList commandPaletteEntries() const;
  // Full text over every note and every doc page of every profile, one row per
  // section, in the palette's row shape: { kind: "note"|"docPage", label, sub,
  // body, profileId, noteId|pageId, line, color }. Every word of `query` must
  // appear; `limit` <= 0 means no limit.
  Q_INVOKABLE QVariantList searchFullText(const QString& query, int limit = 50) const;
  // Ids of the active profile's notes / doc pages whose title, folder or body
  // contain every word of `query` — what the list filters show.
  Q_INVOKABLE QStringList notesMatching(const QString& query) const;
  // "In other profiles: 6 in Personal" for a filter that finds nothing here;
  // "" when no other profile has a match either (A3-13).
  Q_INVOKABLE QString matchesElsewhere(const QString& text) const;
  QStringList notesMatchingExactly_(const QString& query) const;
  Q_INVOKABLE QStringList docPagesMatching(const QString& query) const;

  // ---- Backups ----
  Q_INVOKABLE QVariantList listBackups() const;

  // Where the backup copies are, and how many are kept (Settings → Data).
  Q_INVOKABLE QString backupFolder() const {
    return backupDirPath();
  }

  Q_INVOKABLE int backupRetention() const;
  Q_INVOKABLE bool restoreFromBackup(const QString& fileName);

  // ---- Time machine (APP-162, src/storage/Snapshots.h) ----
  // Hourly snapshots in <dataDir>/history, newest first: { name, at (ISO),
  // day ("yyyy-MM-dd"), time ("HH:mm"), sizeKb, tag, profiles, tasks, notes,
  // docs }. The counts come from each file's summary line, nothing is inflated.
  Q_INVOKABLE QVariantList listSnapshots() const;
  // One snapshot, read-only, next to the state as it is now: { ok, error,
  // name, at, profiles: [{ id, name, color, tasks, notes, docs, existsNow,
  // tasksAdded, tasksRemoved, tasksChanged, notesAdded, notesRemoved,
  // notesChanged }], totals: { the same six }, missing: [{ kind ("task" |
  // "note" | "doc"), id, title, profileId, profileName, profileExists }],
  // changed: [same shape] }. "Removed" = in the snapshot, gone now.
  Q_INVOKABLE QVariantMap previewSnapshot(const QString& name);
  // The whole state goes back to the snapshot. The state it replaces is
  // snapshotted first (tag "pre"), so a restore is itself restorable.
  Q_INVOKABLE bool restoreSnapshot(const QString& name);
  // The snapshot's file, shown in the file manager (DG-121 "Show file").
  Q_INVOKABLE bool revealSnapshot(const QString& name) const;
  // The snapshot's profile `profileId` comes back as a new profile, "<name>
  // (restored HH:MM)", beside the current one. Returns the new id, "" on failure.
  Q_INVOKABLE QString restoreSnapshotProfile(const QString& name, const QString& profileId);
  // One task, note or doc page (`kind` "task" | "note" | "doc") from the
  // snapshot's profile `profileId` goes back into that profile: re-inserted
  // when it is gone, overwritten when it is there. Switches to the profile
  // first; undoable.
  Q_INVOKABLE bool restoreSnapshotItem(const QString& name, const QString& kind, const QString& profileId, const QString& itemId);
  // Writes the current state into history right now under `tag`. Returns the
  // file name, "" on failure.
  QString takeSnapshotNow(const QString& tag);
  void snapshotBeforeChange(const QString& tag);
  bool gitLinksBranches() const;
  QString historyDirPath() const;

  // ---- Shortcuts (rebindable keyboard catalog) ----
  QVariantList shortcuts() const {
    return m_shortcuts;
  }

  Q_INVOKABLE QString shortcutFor(const QString& id) const;
  Q_INVOKABLE QString defaultShortcutFor(const QString& id) const;
  Q_INVOKABLE QString shortcutDescription(const QString& id) const;
  Q_INVOKABLE QString shortcutLabel(const QString& id) const;
  // A key as it is written beside its action (keymap.md): "d", "Shift S",
  // "g b", "Ctrl K", "Enter"; the macOS glyphs on a Mac. One notation for the
  // menus, the cheat sheet, the hints and the tooltips (APP-279).
  Q_INVOKABLE QString keyText(const QString& sequence) const;
  Q_INVOKABLE QString shortcutText(const QString& id) const;
  // Why a key cannot be bound (the system keeps it, Tab drives dialogs); "".
  Q_INVOKABLE QString reservedShortcutReason(const QString& sequence) const;
  // A key every app uses for one thing (Ctrl+C, Backspace…): bindable, after
  // a question; empty when it is free or the action's own default (A7-9).
  Q_INVOKABLE QString commonShortcutWarning(const QString& id, const QString& sequence) const;
  // The action whose sequence starts with `sequence`, or that `sequence`
  // starts with (g and g b): such a key is refused, not swapped; "" when none.
  Q_INVOKABLE QString prefixShortcutConflict(const QString& id, const QString& sequence) const;
  // A key press as the keymap reads it (KeyRouter), for recording a binding:
  // the physical key in any layout. `scanCode` is the event's nativeScanCode.
  Q_INVOKABLE QString keyChord(int key, int modifiers, const QString& text, quint32 scanCode) const;
  // The catalog action holding `sequence` (or the built-in key that does, as a
  // catalog id or a builtin.* id shortcutLabel() names); empty when free.
  Q_INVOKABLE QString findShortcutConflict(const QString& id, const QString& sequence) const;

  // Only the built-in half of that: a key no rebinding can free.
  Q_INVOKABLE QString builtinShortcutConflict(const QString& id, const QString& sequence) const {
    return builtinShortcutOwner(id, normalizeSequence(sequence));
  }
  Q_INVOKABLE bool setShortcut(const QString& id, const QString& sequence);

  QString keymapScheme() const {
    return m_keymapScheme;
  }

  // "classic" or "vim"; anything else is ignored. Rebindings are kept, and a
  // default of the new scheme that one of them already holds steps aside.
  void setKeymapScheme(const QString& scheme);
  // The default key of `id` in `scheme` ("classic" / "vim"), whichever is on.
  Q_INVOKABLE QString schemeShortcutFor(const QString& id, const QString& scheme) const;
  // Whether a key press is the key of catalogue action `id` (or of its
  // second keys, "id.alt…") — read as KeyRouter reads it, so a private key
  // handler follows the scheme, the rebinding and the Russian layout. Only
  // a one-chord binding can match one press.
  Q_INVOKABLE bool keyMatches(const QString& id, int key, int modifiers, const QString& text, quint32 scanCode) const;
  Q_INVOKABLE void resetShortcut(const QString& id);
  Q_INVOKABLE void resetAllShortcuts();
  // A control that has a catalogue shortcut was used with the mouse (APP-166).
  // The third time for an action, shortcutHintRequested fires — once, never
  // again for that action. Off with Settings → Shortcuts "Suggest shortcuts".
  Q_INVOKABLE void noteMouseAction(const QString& shortcutId);
  // Interface scale one step up (> 0), down (< 0) or back to 100 % (0) along
  // `steps` (Theme.scaleSteps), written to settings.appearance.uiScale like
  // Settings → Appearance → Scale does. Returns the new scale.
  Q_INVOKABLE double stepUiScale(int direction, const QVariantList& steps);
  // The step of `steps` the system's text size asks for (APP-183): what
  // Theme.scale is while settings.appearance.uiScale is unset. 1.0 under
  // tests unless HEAP_TEXT_SCALE says otherwise.
  Q_INVOKABLE double systemUiScale(const QVariantList& steps) const;
  // How much the system already scales the primary screen (1 at 100 %,
  // 1.25 at 125 %): the default interface scale is not stacked on top.
  Q_INVOKABLE double systemPixelRatio() const;
  // What the primary screen's size asks for (heap::ui::screenDefaultScale):
  // 125 % on a desktop monitor with small pixels (27" 2560×1440), 110 % on
  // another unscaled screen, 100 % where the system scales.
  Q_INVOKABLE double screenUiScale() const;
  // The scale the app is drawn at while the user has not picked one — what
  // Theme.scale shows and what the zoom keys step from (SHELL-3): the
  // screen's (screenUiScale), or the system's text size when that is larger.
  Q_INVOKABLE double defaultUiScale(const QVariantList& steps) const;
  // Paints a window's own title bar dark or light to match the theme; the
  // OS otherwise follows its own app mode (a white bar over a dark heap).
  Q_INVOKABLE void setWindowFrameDark(QObject* window, bool dark) const;

  // Where the last mouse press or touch landed, in global coordinates. The
  // window overlay's pressed() fires for presses inside a dialog too, so a
  // dialog that closes on a press beside it (APP-126) checks the point.
  Q_INVOKABLE QPointF lastPressGlobalPos() const {
    return m_lastPressGlobal;
  }
  // How the capture hotkey reaches heap from other apps (APP-171): "native"
  // (Windows, macOS), "x11", "portal" (Wayland), or "none" — then Settings
  // says to bind `heap --capture` in the desktop's own keyboard settings.
  Q_INVOKABLE QString globalHotkeyBackend() const;

  // ---- Undo ----
  // Undo/redo the last recorded operation. undoLastDeletion() is the old name,
  // kept because QML and several tests call it.
  Q_INVOKABLE void undo();
  // Whether the step Ctrl+Z would take back next is the one that made task
  // \p id — undoing it deletes the task (IDIOT-DOC-17).
  Q_INVOKABLE bool undoWouldRemoveTask(const QString& id) const;
  // Whether that step touches only notes, doc pages and the Docs catalogue:
  // the bare "u" in Knowledge takes back nothing done on another screen
  // (IDIOT-KNOW-5, A4-4).
  Q_INVOKABLE bool undoIsKnowledgeOnly() const;
  Q_INVOKABLE void redo();

  Q_INVOKABLE void undoLastDeletion() {
    undo();
  }

  // The operation a toast is about to name: the one being recorded right now
  // (an undoable toast is emitted from inside its operation) or, failing that,
  // the newest undoable one. The toast keeps this and hands it to undoEntry().
  Q_INVOKABLE double undoSerialForToast() const;
  // Undo the operation recorded under `serial` — the toast's own action, not
  // whatever happens to be on top of the stack. Newer operations stay; when
  // one of them has changed the same thing since, nothing is reverted and the
  // user is told. Returns whether it was undone.
  Q_INVOKABLE bool undoEntry(double serial);

  // Group several calls from QML into one undo step (a quick-captured "sync"
  // is a task and its meeting). Nests; every begin needs its end.
  Q_INVOKABLE void beginUndoGroup(const QString& label);
  Q_INVOKABLE void endUndoGroup();

  Q_INVOKABLE void clearPendingUndo();

  // How many operations are currently undoable. Exposed for tests.
  Q_INVOKABLE int undoDepth() const {
    return m_undo.depth();
  }

  // ---- Git focus / watcher ----
  QString focusedTaskId() const {
    return m_focusedTaskId;
  }

  QString focusedBranch() const {
    return m_focusedBranch;
  }

  QString focusedRepo() const {
    return m_focusedRepo;
  }

  QVariantMap focusedRepoState() const {
    return m_focusedRepoState;
  }

  QVariantMap coachMark() const {
    return m_coachMark;
  }

  // Integration setting "reviewFold" (on by default): a merge / pull
  // request about a Jira issue shows on the issue's card instead of a card
  // of its own (APP-293). Off brings the folded cards back.
  Q_INVOKABLE bool reviewFoldOn(const QString& providerId) const;
  Q_INVOKABLE void setReviewFold(const QString& providerId, bool on);
  // ---- Coach marks (src/hints/CoachMarks.h) ----
  // Puts coach mark `id` on offer for its anchor, with what it needs to say
  // (`payload`, merged with {id}). Refused — false — for an unknown or
  // already read id, while another waits, or once one was shown this session.
  Q_INVOKABLE bool offerCoachMark(const QString& id, const QVariantMap& payload = {});
  // The anchor put it on screen: no other coach mark this session.
  Q_INVOKABLE void coachMarkShown(const QString& id);
  // Read (any button, Esc, a press outside, the settings it points to):
  // never offered again.
  Q_INVOKABLE void dismissCoachMark(const QString& id);
  // Its anchor went away before it could show: off offer, still unread.
  Q_INVOKABLE void dropCoachMark(const QString& id);
  Q_INVOKABLE bool coachMarkSeen(const QString& id) const;
  // One more go at what coach mark `id` is about, this session (the week
  // arrows clicked: weekKeys); the count so far. Not saved.
  Q_INVOKABLE int noteCoachMarkUse(const QString& id);
  // Unread again (`id`, or all when empty), nothing on offer, a fresh session.
  Q_INVOKABLE void forgetCoachMarks(const QString& id = {});
  // Off under QStandardPaths test mode, so a coach mark never takes the
  // keyboard in another suite's flow; the coach mark tests turn them on.
  Q_INVOKABLE void setCoachMarksEnabled(bool on);
  // A visit to `view` (Main.qml on every view change): one more coach mark
  // may show, and feature hints due there go on offer. The id offered, or "".
  Q_INVOKABLE QString noteHintVisit(const QString& view);
  // A feature was used (featureHintRules' feature keys): its hint is moot.
  Q_INVOKABLE void noteFeatureUsed(const QString& feature);
  // A long wheel gesture in `view` (ScrollFeel::longWheelScroll): the
  // middle-click autoscroll card may go on offer. True when it did.
  Q_INVOKABLE bool noteLongWheel(const QString& view);
  // "Later": not today, unread; the second time it is read.
  Q_INVOKABLE void snoozeCoachMark(const QString& id);
  // "Skip all": every coach mark read for good, until forgetCoachMarks().
  Q_INVOKABLE void skipAllCoachMarks();
  Q_INVOKABLE bool coachMarksSkipped() const;
  // Tests: the clock hints use (an invalid one = the real clock).
  Q_INVOKABLE void setHintClockForTest(const QDateTime& now);
  bool showWhoseMove() const {
    return m_showWhoseMove;
  }

  bool focusedBannerDismissed() const {
    return m_dismissedBranches.contains(m_focusedBranch);
  }

  Q_INVOKABLE void dismissGitBanner();
  Q_INVOKABLE void openFocusedTask();
  // Task-id prefixes the branch matcher should recognise: the configured local
  // one, plus the project key of every mirrored issue in the profile.
  Q_INVOKABLE QStringList collectPrefixes() const;
  // The configured local task-id prefix, upper-cased; "TASK" when unset or
  // not a usable ticket stem (a letter, then letters and digits).
  QString taskIdPrefix() const;
  // Every profile as it stands now: the stored copies, with the active one's
  // tasks and columns taken from the live models (the stored copy lags them).
  QVector<Profile> profilesSnapshot() const;
  // Turn what the matcher found in a branch name into a task id. For a local
  // task the key IS the id; a mirrored issue's id carries the provider, so it
  // is resolved through the tracker key instead.
  Q_INVOKABLE QString taskIdForBranchMatch(const QString& matchedId) const;
  Q_INVOKABLE void refreshGitForTaskBranch(const QString& taskId);
  // Create (and switch to) the task's branch from a task card. Honors the
  // configured integrations.github.branchTemplate; toasts the result.
  Q_INVOKABLE void createBranchForTask(const QString& taskId);

  // ---- CODEOWNERS and SSH dev-server repos (AppControllerCodeOwners.cpp) ----
  // Who owns what the task's open PR/MR changes: GitWatcher's report
  // ({found, location, requirements, toPing, prNumber, prUrl, …}), {} when
  // nothing is known. Read only: Tabless never asks anyone for a review.
  Q_INVOKABLE QVariantMap codeOwnersOf(const QString& taskId) const;
  // "Кому написать" from CODEOWNERS: one row per owner still to ask on one
  // of my open PRs, [{handle, prNumber, prUrl, prTitle, taskId, key}].
  Q_INVOKABLE QVariantList codeOwnerPings() const;
  // "Написал" on such a row: hidden until the app restarts or the PR's
  // owners change.
  Q_INVOKABLE void dismissCodeOwnerPing(const QString& key);
  // Stands in for GitWatcher's report in tests (no forge, no repo); an
  // empty report removes it.
  Q_INVOKABLE void setCodeOwnersReportForTest(const QString& repo, const QString& branch, const QVariantMap& report);
  // Host aliases from ~/.ssh/config (and its Includes), for the picker.
  Q_INVOKABLE QStringList sshHosts() const;
  // A watched-repo entry that lives on a server (`host:/path`, ssh://…).
  Q_INVOKABLE bool isRemoteRepo(const QString& entry) const;
  // The spelling settings keep: canonical for a remote entry, as typed
  // (trimmed) otherwise; "" when it is neither.
  Q_INVOKABLE QString normalizeRepoEntry(const QString& entry) const;
  // The dev-server form as typed: {entry, problem} (RemoteRepo.h, checkRemoteForm).
  Q_INVOKABLE QVariantMap remoteRepoForm(const QString& host, const QString& path) const;
  // Connects once and answers with remoteRepoTested().
  Q_INVOKABLE void testRemoteRepo(const QString& entry);
  // A remote watched repo as the last poll left it: {remote, branch, error}
  // (error = a remoteRepoTested() failure key, "" while it answers); {} for
  // a local or unknown entry.
  Q_INVOKABLE QVariantMap remoteRepoStatus(const QString& entry) const;

  // ---- The two dots of a merge / pull request card (AppControllerMr.cpp,
  // dogfood 2026-10-20 item 9) ----
  // Bumped (once per event-loop turn) when a task's PR facts, a ticket or a
  // CODEOWNERS report changed: the cards read mrOf() again.
  Q_PROPERTY(int mrRevision READ mrRevision NOTIFY mrRevisionChanged)

  int mrRevision() const {
    return m_mrRevision;
  }

  // From facts already here, never a request: {ci, merge (heap::git::mrDots),
  // ref ("!481" / "#481"), state, draft, conflict, changes, waiting [names],
  // waitingHandles, approved [names], since (when the request was opened)}.
  // `task` is a card's task map (id, prState, prNumber, prUrl, ticket…); the
  // model adds what the map leaves out. {} when there is no open request.
  Q_INVOKABLE QVariantMap mrOf(const QVariantMap& task) const;
  // "Кому написать" gets the people the task's request waits on: a person
  // already known moves to "todo", a handle no one has becomes a person.
  // One undo step, one toast. Returns how many; 0 says so in a toast.
  Q_INVOKABLE int pingMrReviewers(const QString& taskId);
  // Stands in for the PR watcher in tests (no gh, no repo).
  Q_INVOKABLE void setGitInfoForTest(const QString& taskId, const QVariantMap& info);

  // ---- "Ждут меня" (AppControllerThreads.cpp, dogfood 2026-10-21 item 2) ----
  // Where on a merge / pull request someone waits for me: a review asked of
  // me, a thread whose last word is not mine, an @mention not answered. The
  // threads are read only for a request whose updated_at or note count moved
  // since the last read (a quiet one costs no request); nothing is posted.
  Q_PROPERTY(int mrThreadsRevision READ mrThreadsRevision NOTIFY mrThreadsChanged)

  int mrThreadsRevision() const {
    return m_mrThreadsRev;
  }

  // The sidebar block, the longest wait first: [{url, ref, title, reason
  // (review|replied|started|mention|threads), count, who, since, ago,
  // openUrl, taskId}].
  Q_INVOKABLE QVariantList mrWaitRows() const;
  // The task's request: {count (threads waiting on me), ago} for the card.
  Q_INVOKABLE QVariantMap mrThreadsBrief(const QString& taskId) const;
  // The task document: [{wait (me|them), place (file:line or ""), who,
  // excerpt, notes, ago, url, reason}], waiting on me first.
  Q_INVOKABLE QVariantList mrThreadsOf(const QString& taskId) const;
  // Opens a thread's web page (https only). False when it is not one.
  Q_INVOKABLE bool openMrThread(const QString& url);

  Q_INVOKABLE QString lastOpenedThreadUrlForTest() const {
    return m_lastOpenedThreadUrl;
  }

  // Tests: a request and its threads as if pulled and read. `raw` is a
  // GitLab discussions page or a GitHub GraphQL answer; `me` my login.
  Q_INVOKABLE bool setMrThreadsForTest(const QString& provider,
                                       const QString& url,
                                       const QString& title,
                                       const QString& author,
                                       const QStringList& reviewers,
                                       const QString& me,
                                       const QString& raw);
  Q_INVOKABLE void clearMrThreadsForTest();
  Q_INVOKABLE void setMrOpenForTest(const QString& url, bool open);

 signals:
  void mrThreadsChanged();
  void codeOwnersChanged();
  void mrRevisionChanged();
  void remoteReposChanged();
  // `failure` is "" on success or a key: unreachable, auth, hostKey, noPath,
  // noGit, notRepo, timeout, noSsh, other. `branch` is what is checked out.
  void remoteRepoTested(const QString& entry, const QString& failure, const QString& detail, const QString& branch);
  void storageStateChanged();
  void lastExitUncleanChanged();
  void keychainProblemChanged();
  void selectedDateChanged();
  void todayChanged();
  void themeChanged();
  void densityChanged();
  void languageChanged();
  void currentViewChanged();
  void shellNoticeChanged();
  // "Done" asked for, but no column has the "done" stage (APP-268).
  void doneColumnMissing();
  void focusedStatusChanged();
  void workdayChanged();
  void crumbProjectChanged();
  void crumbUserChanged();
  void docsStateChanged();
  void notesStateChanged();
  void activeNoteChanged();
  // Emitted just before the active note changes, while the old one is still
  // active. The notes editor debounces its writes, so its last keystrokes are
  // only in the text field at this point; it flushes on this signal, and they
  // land in the note they were typed into instead of the one being opened.
  void aboutToChangeActiveNote();
  // Asks every editor with a debounced write pending (notes, doc pages, the
  // docs catalogue) to write it now. Emitted before anything that reads the
  // whole profile — an export, a search — so it sees what is on screen.
  void flushEditorsRequested();
  void activeDocPageChanged();
  void appSettingsJsonChanged();
  void calendarSubscriptionsChanged();
  void statusesChanged();
  void pendingUndoChanged();
  void onboardingChanged();
  // Emitted after resetToFirstRun() rebuilds a fresh install — Main.qml re-opens
  // the Welcome dialog and surfaces a confirmation toast.
  void firstRunReset();
  // "Reset all settings" went through; `message` is the toast text.
  void settingsReset(const QString& message);
  // Emitted by replayWelcome() — Main.qml re-opens the Welcome guide from step 0
  // without changing any persisted onboarding flags.
  void welcomeReplayRequested();
  // noteMouseAction() decided it is time to mention `sequence` for `label`.
  void shortcutHintRequested(const QString& shortcutId, const QString& sequence, const QString& label);
  void profilesChanged();
  void activeProfileChanged();
  void shortcutsChanged();
  void keymapSchemeChanged();
  void blockedStuckChanged();
  void statusCountsChanged();
  void taskTitlesChanged();
  void taskKeysChanged();
  void taskStagesChanged();
  void savedViewsChanged();
  void savedViewCountsChanged();
  void strictQueryChanged();
  // `routeId` ("meeting:<event id>") makes it a reminder with buttons
  // (APP-155); empty for a plain heads-up.
  void notification(const QString& title, const QString& body, const QString& kind, const QString& routeId = QString());
  // `kind` tints the toast: "info" (default when empty), "success", "warning"
  // or "error". Every C++ toast used to arrive as info, failures included.
  void toast(const QString& message, const QString& kind = QString());
  // Device-flow OAuth: prompts the Integrations card to show a "enter this code
  // in your browser" banner. An empty `code` clears the banner (flow finished).
  void oauthDeviceCode(const QString& providerId, const QString& code, const QString& verificationUri);
  // A browser sign-in succeeded but the provider still needs a scope field
  // (Asana workspace, ClickUp list, Sentry org/project, Bitbucket repo) before
  // it can sync. The card opens Advanced so the user can see what is missing.
  void integrationNeedsFields(const QString& providerId, const QStringList& labels);
  // A credential sign-in finished. The card clears its password field on both
  // outcomes, so a failed attempt never leaves one sitting in the UI.
  void integrationLoginFinished(const QString& providerId, bool ok);
  // A card action came back: `action` is "sync", "test", "oauth" or "signin".
  // `message` is what the toast said on failure (empty on success). The card
  // shows its buttons as busy until this arrives and keeps the last error on
  // screen, since the toast is gone in a few seconds (design audit DES-5).
  void integrationActionFinished(const QString& providerId, const QString& action, bool ok, const QString& message);
  // A sync brought new cards (APP-180): the toast names them and offers to
  // show them. One per pull, instead of the plain toast.
  void syncNews(const QString& message, const QStringList& taskIds);
  void syncingChanged();
  void unseenTasksChanged();
  void syncNewTaskIdsChanged();
  void eventLogChanged();
  // Raised whenever the keychain contents change — on the async load at startup
  // and after every write. integrationSecret() is a plain Q_INVOKABLE (secrets
  // are not properties), so QML re-reads it by binding to this signal.
  void integrationSecretsChanged();
  // The answer to one fetchTicketComments (HEAP-117). Carries the task id so a
  // reply for a ticket the user has since navigated away from can be dropped.
  // `error` is empty on success; an empty list with no error means no comments.
  void ticketCommentsLoaded(const QString& taskId, const QVariantList& comments, const QString& error);
  // A picture of a tracker ticket's text arrived or failed (dogfood
  // 2026-10-20): trackerDescription reads differently now.
  void trackerImagesChanged();
  void ciJobsLoaded(const QString& taskId, const QVariantList& jobs, const QString& error);
  void ciLogLoaded(const QString& taskId, const QString& jobId, const QString& text, const QString& error);
  void updateStatusChanged();
  void integrationStatesChanged();
  void integrationHealthChanged();
  void pausedFeaturesChanged();
  // A task's history gained an event (APP-165).
  void taskHistoryChanged(const QString& taskId);
  // Emitted when a newer release is found — Main.qml shows an actionable toast.
  void updateAvailable(const QString& version, const QString& url);
  void updateProgressChanged();
  // The package arrived and matches the release's SHA-256 — Main.qml offers
  // the restart.
  void updateReadyToInstall(const QString& version, const QString& sha256);
  void undoableToast(const QString& message, int seconds);
  // A reminder with buttons shown in the window (the start of a task block,
  // APP-256); the buttons call reminderAction(notificationId, …).
  // `kind` is "taskBlock" or "remind" (APP-319): which buttons it has.
  void reminderToast(const QString& notificationId, const QString& message, const QString& kind);
  // A ping's notification asked to open the person (dogfood 2026-10-20).
  void pingOpenRequested(const QString& personId);
  // "Next free window" found no room on `date` (APP-253): the toast says so
  // and offers the next working day's window and, when one is left, a time
  // this evening. Nothing is planned until the person picks.
  void freeWindowMissing(
      const QString& taskId, const QString& title, const QDate& date, const QDate& nextDate, double nextStart, double lateStart);
  // A status change did not reach the tracker. The UI offers a retry.
  void trackerPushFailed(const QString& taskId, const QString& message);
  // A conflict on this task was settled, one field or all (APP-163).
  void trackerConflictResolved(const QString& taskId);
  // The fresh check before a write found the issue outside the filter: the
  // status was not sent. The UI may offer "send anyway" behind a dialog that
  // names the issue, its status in the tracker now and the target; answering
  // it calls confirmTrackerPush (APP-204).
  // A move of a card whose tracker is written to, held until the user says
  // send or keep it here (trackerAskBeforeWrite).
  void trackerWriteAsk(const QString& taskId,
                       const QString& key,
                       const QString& title,
                       const QString& tracker,
                       const QString& providerId,
                       const QString& from,
                       const QString& to);
  void trackerPushNeedsConfirm(const QString& taskId,
                               const QString& key,
                               const QString& title,
                               const QString& tracker,
                               const QString& remoteStatus,
                               const QString& target);
  // A move of a card the tracker is read-only for (outside the filter, gone)
  // was refused. `url` opens the issue.
  void trackerReadOnlyMove(const QString& taskId, const QString& message, const QString& url);
  // The one-time "heap no longer changes the status in …" notice (APP-243).
  void trackerWriteNotice(const QString& message);
  void focusedGitChanged();
  void showWhoseMoveChanged();
  void coachMarkChanged();
  // `profileId` is the profile the task lives in; the window switches there
  // only once the editor's unsaved edits are settled (PRES-1).
  void openTaskRequested(const QString& id, const QString& profileId);
  // A card's menu asked for "Move to profile…" (A5-14): the window opens
  // its picker for these tasks.
  void moveToProfileRequested(const QStringList& ids);
  // "Open" on a meeting / standup reminder: the calendar at `date`, and the
  // event's editor when `eventId` names a stored event (APP-155).
  void openEventRequested(const QString& eventId, const QDate& date);
  void selectedTaskIdsChanged();
  // Raised by the OS-level global hotkeys (Quick-capture from anywhere). QML
  // brings the window forward and opens the matching capture popup.
  void quickCaptureRequested();
  void quickCaptureNotesRequested();
  // Raised when the user asks to restore the window from the tray (tray click
  // or the tray menu's "Show" entry). QML un-hides and activates the window.
  void showWindowRequested();

 public:
  // One automation tick at `now`. The timer calls it with the wall clock;
  // tests call it with the clock they need.
  void runAutomationAt(const QDateTime& now);
  // Re-reads the date. When it moved, `today` changes, and a selection that
  // was on the old today follows it — the calendar is where the user left it,
  // "today". Called by the midnight timer, the minute tick and on resume.
  void refreshToday(const QDate& current = QDate::currentDate());

 private slots:
  void runAutomation();

 private:
  // settings.calendar.timeFormat == "12h".
  bool twelveHourClock() const;
  // `base` if no task holds it, else "base-2", "base-3", … Two pulled issues
  // can want the same id (the same number from two repos), and upsert on a
  // colliding id replaces the other task rather than adding one.
  QString uniqueTaskId(const QString& base) const;
  QHash<QString, QString> jiraIssuesByKey() const;
  // A review's card folded into `issueId`: archived, and marked so turning
  // folding off can bring back exactly these cards.
  void markReviewFolded(const QString& cardId, const QString& issueId);
  void offerReviewFoldHint(const QString& issueId, const Task& review);
  QVariantMap m_coachMark;
  // One coach mark per visit to a view (noteHintVisit starts a new one).
  bool m_coachMarkShownThisSession = false;
  bool m_coachMarksEnabled = !QStandardPaths::isTestModeEnabled();
  QHash<QString, int> m_coachMarkUses;
  QDateTime m_hintClock;  // tests only; invalid = the real clock
  QDateTime hintNow() const;
  static QVariantMap reviewGitFacts(const QJsonObject& details, const QString& externalId, const QString& url, const QDateTime& seenAt);

  // The review folded into each Jira issue (APP-293), as the last pull or
  // profile load picked it (dogfood 2026-10-21): the whole of its details,
  // so the issue's dots have its merge facts too, not just its CI. In
  // memory, like the "PR #n" it feeds; by issue task id.
  struct FoldedReview {
    QJsonObject details;
    QString externalId;
    QString url;
    QString provider;
    QString cardId;  // its own archived card, if it ever had one
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime seenAt;  // the pull that saw it; invalid = from a saved card
    int link = 0;      // linkedIssueKey's strength
  };

  QHash<QString, FoldedReview> m_foldedReviews;
  // When a pull last saw each merge / pull request, by URL: which of two
  // looks at its CI is the later one. In memory.
  QHash<QString, QDateTime> m_reviewSeenAt;
  static bool foldedBeats(const FoldedReview& a, const FoldedReview& b);
  void applyFold(const QString& issueId, const FoldedReview& review);

  TaskModel m_tasks;
  EventModel m_events;
  PersonModel m_people;
  ActivePeopleModel m_activePeople;
  QVariantList m_statuses;
  QDate m_today;
  QDate m_selectedDate;
  QString m_theme = "dark";
  QString m_density = "comfy";
  QString m_language = "en";
  QString m_currentView = "today";  // heap 2: Today is the start screen (APP-260)
  // section -> the view it was last left on (APP-258).
  QHash<QString, QString> m_sectionViews;
  QString m_shellNotice;
  QString m_focusedStatus;
  int m_workdayStart = 9;
  int m_workdayEnd = 19;
  QString m_crumbProject;
  QString m_crumbUser = "You";
  QString m_docsState;
  QString m_notesState;
  NoteModel m_notes;
  QString m_activeNoteId;
  // The note the last bare newNote() made; left untouched, it is dropped when
  // another note opens, and a second "+" opens it again (IDIOT-KNOW-14).
  QString m_freshNoteId;
  bool freshNoteUntouched() const;
  // Opened notes, the latest first; not saved (Ctrl+Tab).
  QStringList m_noteMru;
  void touchNoteMru(const QString& id);
  // setActiveNoteId was called from another screen: Notes opens that note.
  bool m_noteOpenRequested = false;
  // An empty note, opened; `chain`: quick notes in the next hour go on in it.
  QString makeBlankNote(bool chain);
  // What going to Notes opens on: the empty note, made unless it is open.
  QString startFreshNote();
  // Leaving Notes: the open note's typing flushed, and an empty new note
  // dropped for the note opened before it.
  void leaveFreshNote();
  DocPageModel m_docPages;
  QString m_activeDocPageId;

  // Keeps `notesState` and the active note's body the same thing. Called on
  // every edit and every switch; silent when there is no active note.
  void syncActiveNoteBody();
  // After undo/redo touched the notes: the open note may be gone, or be back
  // with a different body. Keeps `notesState` pointing at a note that exists.
  void reconcileActiveNote();

  // Who asked for a status write (APP-243/APP-204). Auto = a move, an undo, a
  // kept conflict or the queue: needs the tracker's switch on. Explicit = the
  // card's own "send": allowed with the switch off. Both are checked against
  // the tracker first. Confirmed = "send anyway" after the dialog: no check.
  enum class PushMode : std::uint8_t { Auto, Explicit, Confirmed };
  // Tell a mirrored card's tracker about its status. No-op for local tasks.
  void pushStatusToTracker(const QString& taskId, const QString& status, PushMode mode = PushMode::Auto);
  // The answer to the fresh check that comes before every write.
  void onIssueChecked(const QString& providerId,
                      const QString& externalId,
                      const QString& project,
                      bool ok,
                      int httpStatus,
                      const QString& error,
                      const QString& remoteStatus,
                      int filterMatch);
  // A board column's name as the user sees it ("prog" → "In progress").
  QString columnDisplayName(const QString& statusId) const;
  // Mark a status as not sent, with the reason the card shows.
  void markPushHeld(const QString& taskId, const QString& status, const QString& reason);

  struct PendingCheck {
    QString taskId;
    QString status;
    PushMode mode = PushMode::Auto;
  };

  // pushKey → the write waiting on its check.
  QHash<QString, PendingCheck> m_pendingChecks;
  bool m_trackerWriteNoticeDone = false;
  // The one-time git "working on" migration ran (or the data is new): saved
  // as settings.gitLineDefault so it never runs again.
  bool m_gitLineDefaultDone = false;
  // The one-time move to the lowkey themes ran: settings.themeLowkey.
  bool m_themeLowkeyDone = false;
  // Drop the not-yet-started focus blocks planned for a task that is finished.
  void dropFutureFocusBlocks(const QString& taskId);
  void onTaskPushed(const QString& providerId,
                    const QString& externalId,
                    const QString& project,
                    bool ok,
                    const QString& error,
                    const QString& remoteStatus = QString());
  // Key of m_pendingPushes. `project` is the issue's own repo/project (empty
  // when it is not known): in a cross-project pull the number alone is ambiguous.
  static QString pushKey(const QString& providerId, const QString& project, const QString& externalId);
  // pushKey → task id, for pushes still in flight.
  QHash<QString, QString> m_pendingPushes;
  // Non-zero while a bulk move runs moveTask per card: one toast for the lot.
  int m_bulkMoveDepth = 0;
  // toggleDone() says "Done: X" itself; the move's own "X → Done" would be a
  // second toast for the same action (DG-140).
  bool m_moveToastHeld = false;
  // The recurrence note a held move toast leaves for toggleDone (A2-18).
  QString m_heldRecursNote;
  // "tomorrow, Tue, Oct 13" / "завтра, вт, 13 окт" for a toast.
  QString dayWords_(const QDate& d) const;
  // "6 h 10 min" / "6 ч 10 мин".
  QString durationWords_(int secs) const;
  // Sound palette (APP-177): muted for CLI requests; a bulk move plays one
  // sound when the loop ends, not one per card — "done" if anything closed,
  // else "refuse" if a card was refused. -1: nothing pending.
  bool m_soundMuted = false;
  int m_soundPending = -1;
  void completionSoundOnMove_(const QString& fromStatus, const QString& toStatus);
  bool stopTimersInOtherProfiles_(const QString& onlyId = {});
  // Plays `cue` (a heap::platform::SoundCue) if the Sound settings, quiet
  // hours (judged at `at`, the wall clock when invalid), focus mode and the
  // system allow it.
  void playSound_(int cue, const QDateTime& at = QDateTime());
  // The meeting chimes due at `now` (APP-178): one melody per tick at most.
  void meetingChimesAt(const QDateTime& now);
  // Providers whose next tasksFetched answers a quiet follow-up pull.
  QSet<QString> m_settlePulls;

  // A pull whose missing issues are being looked up: its stats and whether it
  // was a quiet follow-up, held so one toast reports both (INT-6).
  struct PendingSyncReport {
    MergeStats stats;
    bool settlePull = false;
  };

  QHash<QString, PendingSyncReport> m_pendingLookups;

  // `notesState` must always belong to a note. Text that arrives with no note
  // open — typed into an empty editor, or captured with Ctrl+Shift+N — becomes
  // a note of its own here. Without this it lived only in `notesState`, which
  // is no longer written to disk since notes became a list, and the next "+"
  // overwrote it.
  void adoptOrphanNotesState();
  // A note with no body, made active without announcing a new notesState:
  // callers write the body next and that is the one change the editor sees.
  QString createActiveNote(const QString& title);
  // ---- Attachments (AppControllerAttachments.cpp) ----
  // Every attachment id something still points at: any profile's tasks,
  // descriptions, notes and doc pages, the open editor text, and the undo
  // history — so a file detached a minute ago survives a cleanup and Ctrl+Z
  // still finds it.
  QSet<QString> referencedAttachmentIds() const;
  // Every attachment a note, doc page, description or the Docs catalogue has
  // linked to since the app started. A text editor's own undo history is out
  // of reach from here, and Ctrl+Z in it can bring back a link the text no
  // longer holds, so the cleanup leaves these alone until the next start
  // (KNOW-3). Fed from the models' signals by trackAttachmentRefsInText().
  QSet<QString> m_attachmentsLinkedThisSession;
  void trackAttachmentRefsInText();
  // The ids one profile uses, for its export.
  QStringList profileAttachmentIds(const Profile& p) const;
  // The export's "attachments" array, or an empty one (and *omittedBytes set)
  // when the files would push the export past the cap.
  QJsonArray attachmentsForExport(const Profile& p, qint64* omittedBytes, int* omittedCount) const;
  // Writes an import's files into the store. Returns old id → new id for every
  // file whose content did not match its id (it is stored under the right one
  // and the references are rewritten).
  QHash<QString, QString> importAttachmentBlobs(const QJsonArray& blobs, QStringList* problems);
  // A string of the attachments feature, in the UI language.
  QString attText(const char* key) const;
  void toastAddFailure(const QString& name, int error);

  // Reads a vault folder and decides what importing it would do (see
  // heap::notes::planImport); the summary is what import and preview return.
  QVariantMap planNotesImport(const QUrl& folderUrl, QVector<heap::notes::VaultPlanItem>* plan) const;
  // Tells the contact importer about imported contacts that a docs change
  // removed (dismiss) or brought back (restore).
  void syncDismissedContacts(const QString& beforeJson, const QString& afterJson);
  // The palette rows for one profile's notes and doc pages (see searchFullText).
  QVariantList fullTextEntries(const Profile& p) const;
  QString m_appSettingsJson;
  // The profile's language while HEAP_LANG=pseudo shows English (APP-189).
  QString m_languageUnderPseudo;
  // heap::platform::systemTextScale(), read on first use (APP-183).
  mutable double m_systemTextScale = 0;
  // settingsMap()'s parse cache, keyed on the string above so that no writer
  // of it has to remember to invalidate anything.
  mutable QString m_settingsCacheSource;
  mutable QVariantMap m_settingsCache;
  // statusCounts()' cache; invalidated from the task model's own signals so
  // every mutation path is covered without each one remembering to.
  mutable QVariantMap m_statusCounts;
  mutable bool m_statusCountsDirty = true;
  // taskTitles: rebuilt once per event-loop turn after the task model
  // changes, and announced only when a title or id really changed, so a
  // status drag does not re-render every open note.
  QVariantMap m_taskTitles;
  QVariantMap m_taskKeys;
  QVariantMap m_taskStages;
  bool m_taskTitlesQueued = false;
  void refreshTaskTitles();
  // The active profile's saved views and savedViewCounts()' cache. The count
  // signal is coalesced: a bulk edit fires the model's signals per row.
  QVector<heap::savedviews::SavedView> m_savedViews;
  // Column moves of the active profile (WEAK PECAP), and the status each task
  // was last seen in, which is how a move is noticed whatever path made it.
  QVector<StatusChange> m_statusLog;
  QHash<QString, QString> m_knownStatus;
  void rememberStatuses();
  void noteStatusMoves(int first, int last);
  mutable QVariantMap m_savedViewCounts;
  mutable bool m_savedViewCountsDirty = true;
  QTimer m_savedViewCountsTimer;
  void wireSavedViews();
  void dropSavedViewCounts();
  void setSavedViews(const QVector<heap::savedviews::SavedView>& views);
  bool m_welcomeSeen = false;  // onboarding: welcome dialog shown at least once
  bool m_demoActive = false;   // onboarding: profile still holds seeded demo
  mutable QString m_installId;  // settings.installId, minted on first use

  // Profiles
  QVector<Profile> m_profiles;
  QString m_activeProfileId;
  int profileIndexOf(const QString& id) const;
  QString makeProfileId(const QString& name) const;
  void snapshotActiveProfile();
  void applyProfileToModels(const Profile& p);
  Profile makeStartingProfile(const QString& name, const QString& color) const;
  // A fresh install (APP-271): one empty profile of the person's own and the
  // first-run look. Shared by the constructor's fresh-install path and
  // resetToFirstRun(). The sample data lives only in the example profile.
  void seedStartingWorkspace();
  // The example profile and its meetings, built from SampleData.
  Profile buildExampleProfile(QVector<CalEvent>* events) const;

  // Shortcuts
  QVariantList m_shortcuts;  // [{id,label,labelEn,description,defaultSequence,sequence}]
  // A fresh install starts on the classic keymap; a settings file written
  // before the choice existed was on Vim (load sets it).
  QString m_keymapScheme = QStringLiteral("classic");
  // Default keys of the catalogue taken by a rebinding step aside (an
  // upgrade, a scheme switch); returns "<key> <action>" for each the user kept.
  QStringList yieldDefaultsTo(QVariantMap& overrides) const;
  int shortcutIndexOf(const QString& id) const;
  void seedShortcutCatalog();
  void applyShortcutOverrides(const QVariantMap& overrides);
  QString normalizeSequence(const QString& raw) const;

  // Global hotkeys — OS-level Quick-capture triggers (work unfocused). Re-armed
  // from the matching catalog sequences whenever they change. Each combination
  // is registered under one of these ids and routed back in onGlobalHotkey().
  enum GlobalHotkeyId { HotkeyQuickCapture = 1, HotkeyQuickCaptureNotes = 2 };

  std::unique_ptr<heap::platform::GlobalHotkey> m_globalHotkey;
  void registerGlobalHotkeys();
  void onGlobalHotkey(int id);

  // Automation
  QTimer* m_automationTimer = nullptr;
  std::unique_ptr<heap::notify::NotificationCenter> m_notifier;
  void onNotifierAction(const QString& notificationId, const QString& actionId);
  void onNotifierActivated(const QString& notificationId);

  // The task a reminder is about, and the profile it lives in. Reminders cover
  // every profile; the ref names the profile (PRES-2), an older one does not.
  struct ReminderTask {
    QString profileId;  // empty: no such task any more
    Task task;
  };

  ReminderTask reminderTask(const QString& ref) const;
  QSet<QString> m_blockedStuckIds;
  // Reminders already delivered, by key (see src/cal/Reminders.h), with when.
  // Persisted to reminders.json so a restart does not announce them again.
  QHash<QString, QDateTime> m_sentReminders;

  // Notifications that arrived during quiet hours, delivered when they end.
  struct HeldNotification {
    QString title;
    QString body;
    QString kind;
    QString taskId;
  };

  QVector<HeldNotification> m_heldNotifications;
  // CI runs last seen per merge request (observeCiAt), and the sources heard
  // from since launch.
  QHash<QString, heap::ci::Seen> m_ciSeen;
  bool m_ciSeenLoaded = false;
  QSet<QString> m_ciSourcesHeard;
  QString ciSeenFilePath() const;
  // A folded review's archived card takes the pull's CI, so the issue's
  // document does not show a stale run.
  void keepFoldedCiCurrent(const QString& cardId, const QJsonObject& pulled);
  QString remindersFilePath() const;
  void loadSentReminders();
  // Reminder buttons (APP-155): what each kind offers, what was last shown
  // under an id (so a snooze can bring the same text back), and the snoozes.
  QVector<heap::notify::NotificationAction> reminderActions(const QString& kind, bool canJoin = false) const;
  // The link of a meeting a reminder is about, or "" (R3-025).
  QString meetingJoinUrl(const QString& eventId) const;
  // Rebuilds the tray menu and tooltip from the timer, the next meeting and
  // the language (R3-027, R3-103).
  void refreshTray();
  // The rows refreshTray() gives the OS menu, and its tooltip.
  QVariantList trayItemsAt(const QDateTime& now, QString* tooltip);
  void onTrayItem(const QString& id);

  // "Не беспокоить 1 ч" from the tray: notifications.dndUntil via
  // doNotDisturbFor, the same item lifts it.

  struct ShownReminder {
    QString title;
    QString body;
    QString kind;
    QDate date;  // meetings: the occurrence's day, for "Open"
  };

  QHash<QString, ShownReminder> m_shownReminders;
  QVector<heap::notify::SnoozedReminder> m_snoozed;
  QString snoozesFilePath() const;
  void loadSnoozes();
  void saveSnoozes() const;
  void fireDueSnoozes(const QDateTime& now);
  void openReminder(const QString& notificationId);
  void saveSentReminders() const;
  bool reminderSent(const QString& key) const;
  QSet<QString> sentReminderKeys() const;
  void markReminderSent(const QString& key, const QDateTime& at);
  // While > 0, markReminderSent() leaves the file to the end of the batch:
  // the minute tick used to rewrite reminders.json once per reminder.
  int m_reminderBatchDepth = 0;
  bool m_reminderSavePending = false;
  void holdNotification(const HeldNotification& n);
  void flushHeldNotifications(const QDateTime& now);
  // settings.calendar.workDays, Monday to Friday by default.
  bool isWorkDay(const QDate& day) const;
  // Fires at the next local midnight; see refreshToday().
  QTimer* m_midnightTimer = nullptr;
  // Set by an application event filter before any item sees the press.
  QPointF m_lastPressGlobal{-1e9, -1e9};
  void armMidnightTimer();
  QVariantMap settingsMap() const;
  // The calendar snap grid in hours, from settings.calendar.snapMinutes. Every
  // event write path clamps against the same grid (heap::cal::clampHours).
  double snapStepHours() const;
  // saveEvent's second half: normalizes the span, checks the rule and stores.
  // Series edits build a CalEvent themselves and come in here.
  void storeEvent(CalEvent e);
  bool inQuietHours(const QDateTime& when) const;
  static double nextQuarterHour(const QDateTime& when);
  // What is taken on `date` (APP-253): meetings and task blocks of the
  // active profile, each as hours on the day; `exceptTaskId`'s own block and
  // its focus blocks left out, so moving a task does not collide with itself.
  QVector<QPair<double, double>> busySpans(const QDate& date, const QString& exceptTaskId, const QDateTime& now) const;
  // The lunch break in hours (APP-319), {-1, -1} while it is off.
  std::pair<double, double> lunchSpan() const;
  void scheduleFocusBlockFor(const QString& taskId);
  void focusBlockOnStatusChange(const QString& taskId, const QString& from, const QString& to);
  // The local layer's own bookkeeping (AppControllerLocal.cpp).
  void noteOpenBlockers_(const QString& taskId);
  void followCardDone_(const Task* before, const Task& after);
  // Rewrites a task's checklist through `edit` as one undo step.
  void editChecklist_(const QString& id, const std::function<void(QVector<LocalCheckItem>&)>& edit);
  // A task by id, tracker key or URL in the active profile; -1 = none.
  int resolveTaskRef_(const QString& ref) const;
  // TaskQuery::compile with this board's statuses and new ids, and the
  // blocked set when the query asks `is:blocked` (APP-250).
  heap::query::TaskQuery compileTaskQuery_(const QString& text) const;
  // Whether `text` is the applied saved view's own query (strictQuery).
  bool isStrictQuery_(const QString& text) const;
  QString m_strictQuery;
  bool m_followingCards = false;
  // Keeps a task's scheduledAt on the focus block it came from: moved with
  // it, cleared when it is deleted (after == nullptr).
  void followFocusBlock(const CalEvent& before, const CalEvent* after);
  // The reverse of followFocusBlock: a schedule moved from a view takes the
  // focus block it came from along (APP-249).
  void moveLinkedFocusBlocks(const QString& taskId, const QDateTime& was, const QDateTime& now);

  // Persistence
  QTimer* m_saveTimer = nullptr;
  bool m_loading = false;
  QDateTime m_lastBackupAt;
  void scheduleSave();
  void saveStateNow();
  void loadStateOnStart();
  // Everything after the bytes are known to be a state document: the schema
  // ladder, settings, profiles, events. `viewOnly` loads a backup for display
  // in a read-only session (no pre-migration copy, no recovery records).
  void loadStateDocument(QJsonObject root, bool viewOnly);
  QString stateFilePath() const;
  QString backupDirPath() const;
  // Writes `bytes` over state.json and reloads from it (a restore's last step).
  bool replaceStateFile(const QByteArray& bytes);
  // The time machine's parsed snapshot: one at a time, kept while the dialog
  // looks at it so a restore does not inflate it again.
  bool loadSnapshot(const QString& name, QString* error);
  QString m_snapName;
  QVector<Profile> m_snapProfiles;
  QVector<CalEvent> m_snapEvents;
  // When the newest rotational backup on disk was taken; invalid when none.
  QDateTime newestBackupTime() const;
  // Crash/corruption recovery for loadStateOnStart(): find the newest backup
  // that still parses (returns its object + path), and move a damaged
  // state.json aside so a fresh seed can never silently overwrite it.
  bool recoverFromNewestBackup(QJsonObject& out, QString& fromPath);
  // Moves (or, when a lock forbids the rename, copies `bytes` to) a damaged
  // state.json aside. Returns the quarantine file name, empty on failure — in
  // which case nothing may be written over the original.
  QString quarantineCorruptState(const QString& path, const QByteArray& bytes);
  // Copies the pre-migration state.json into the backup dir before the schema
  // ladder rewrites it. Exempt from retention pruning: it is the only pre-v4
  // image of the user's data.
  void retainPreMigrationBackup(const QString& path, int fromVersion);
  // Set when state.json was written by a newer build than this one. Every save
  // path is a no-op while it is true: this build cannot represent the fields it
  // did not parse, so writing would drop them.
  bool m_saveBlocked = false;

  // ---- Storage health + background save (PLAT-1/4/5/23) ----
  QString m_storageState = QStringLiteral("ok");
  bool m_lastExitUnclean = false;
  QDateTime m_lastSaveTime;
  QString m_sessionMarkerPath;  // empty = no marker kept (CLI, tests)
  QString m_storageMessage;
  QString m_storageReason;
  int m_storageDamagedAt = -1;
  // How the message is worded: string keys with their arguments, so a
  // language switch re-words it (R2-039).
  QVector<QPair<QString, QStringList>> m_storageSpec;
  void setStorageStateT(const QString& state, const QVector<QPair<QString, QStringList>>& spec);
  void recomposeStorageMessage();
  QString classifyWriteFailure() const;
  QJsonObject buildStateHead();
  QVariantMap m_keychainProblem;
  bool m_whatsNewDue = false;
  QString m_updatedFrom;
  void setStorageState(const QString& state, const QString& message);
  // state.json exists but could not be read: show the newest backup (or an
  // empty workspace) read-only and never write over the file.
  void enterUnreadableMode(const QString& error);
  // Drops every profile and re-reads state.json (restore, retry after a lock).
  void reloadStateFromDisk();
  // Whether an edit was made while saving was blocked (it will not persist).
  bool m_editsWhileBlocked = false;
  // When the last "this edit is not saved" toast was shown, ms since epoch.
  qint64 m_lastBlockedEditToastMs = 0;
  std::unique_ptr<heap::storage::AsyncSaver> m_saver;
  quint64 m_saveGeneration = 0;
  QTimer* m_saveRetryTimer = nullptr;
  QTimer* m_storageRetryTimer = nullptr;
  int m_saveRetryStep = 0;
  void onSaveFinished(const heap::storage::SaveOutcome& outcome);
  bool backupDueNow(const QDateTime& now);
  // Copies state.json into backups/ now (regardless of the rotation interval).
  // Returns the copy's file name, empty on failure.
  QString snapshotStateToBackups(const QString& tag = QString());
  // Unknown keys read from disk at the same schema version, written back
  // untouched (PLAT-26): the document root and settings.
  QJsonObject m_rootExtra;
  QJsonObject m_settingsExtra;
  // Next number to mint per task id prefix, across every profile (TASKS-1/31).
  QHash<QString, int> m_taskSeq;
  static inline bool s_headless = false;
  // What resetSettingsToDefaults() replaced, for its Undo.
  QString m_settingsBeforeReset;
  // The id newTaskDraft & co. hand out for `stem`: past the persisted counter
  // and past every id any profile holds under it.
  QString mintTaskId(const QString& stem) const;
  // Records that `id` was taken, so it is never handed out again.
  void noteTaskIdUsed(const QString& id);
  // Gives every task of `p` (a profile about to be added) whose id another
  // profile already holds a fresh one, and points the references inside `p`
  // — links, #KEY-1 mentions — and `events`' task links at it. Returns
  // old id -> new id (PLAT-9).
  QHash<QString, QString> reissueSharedTaskIds(Profile& p, QVector<CalEvent>* events);
  // Adds `events` (another profile's, or a snapshot's) to the pool as
  // `profileId`'s own: each gets a fresh id, and an override follows its
  // series to the copy's (PLAT-10, TM-1).
  void addEventsAsCopies(const QVector<CalEvent>& events, const QString& profileId);
  static bool isSampleEvent(const CalEvent& e);
  int statusIndexOf(const QString& id) const;
  // Whether another column (not `exceptId`) already carries `name`, ignoring case.
  bool statusNameTaken(const QString& name, const QString& exceptId) const;
  // `base`, or "base (N)" with the first N no profile uses.
  QString uniqueProfileName(const QString& base) const;
  // moveTask(), with the card's rank in its new column set in the same model
  // change (moveTaskTo knows it before the move).
  void moveTaskRanked(const QString& id, const QString& newStatus, std::optional<double> rank);
  // One column's tasks in board order (rank, then id so the order is total).
  QVector<::Task> columnTasks(const QString& statusId, const QString& excludeId) const;
  // Spread a column's ranks back out when a gap has shrunk too far to take
  // another midpoint. See src/board/Rank.h.
  void rebalanceColumn(const QString& statusId);

  // ---- Undo/redo ----
  // The stack records what an operation changed (see src/undo/UndoStack.h)
  // rather than each mutator describing itself, which is why bulk move and
  // bulk archive are undoable now without either of them knowing about undo.
  heap::undo::UndoStack m_undo;
  int m_undoScopeDepth = 0;
  quint64 m_undoSerialCounter = 0;
  quint64 m_openUndoSerial = 0;  // serial of the outermost scope being recorded

  // Snapshots the collections on construction and pushes the diff on
  // destruction. Declaring one at the top of a mutator is the whole contract:
  //
  //   UndoScope scope(this, tr_("task.deleted").arg(id));
  //
  // An operation that changes nothing pushes nothing, so a no-op move or a
  // guard that returns early leaves the stack alone.
  class UndoScope {
   public:
    UndoScope(AppController* owner, QString label);
    ~UndoScope();
    UndoScope(const UndoScope&) = delete;
    UndoScope& operator=(const UndoScope&) = delete;
    UndoScope(UndoScope&&) = delete;
    UndoScope& operator=(UndoScope&&) = delete;

    // Abandon the recording — used where the operation replaces the whole
    // workspace and a per-element diff would be meaningless.
    void abandon() {
      m_armed = false;
    }

    // Several mutators only know what to call the operation part-way through
    // (the column a card landed in, how many tasks a bulk action touched).
    void setLabel(QString label) {
      m_label = std::move(label);
    }

    void setRedoLabel(QString label) {
      m_redoLabel = std::move(label);
    }

   private:
    AppController* m_owner;
    QString m_label;
    QString m_redoLabel;
    bool m_armed = true;
    // Scopes nest: an operation built out of other operations records one
    // entry, not one per part. Only the outermost scope snapshots and pushes.
    bool m_outermost = true;
    quint64 m_serial = 0;
    QVector<::Task> m_tasks;
    QVector<::CalEvent> m_events;
    QVector<::Person> m_people;
    QVector<::DocPage> m_docPages;
    QVector<::Note> m_notes;
    QVariantList m_statuses;
    QString m_docsState;
    QVector<heap::savedviews::SavedView> m_savedViews;
  };

  // One task's priority, unrecorded; false when nothing changed.
  bool setPriorityOf(const QString& id, const QString& priority);

  // Scopes opened by beginUndoGroup() and closed by endUndoGroup().
  std::vector<std::unique_ptr<UndoScope>> m_undoGroups;

  // Applies one recorded entry in either direction and refreshes what the UI
  // derives from the models.
  void applyUndoEntry(const heap::undo::Entry& entry, bool backward);
  // Hands the open note's unsaved keystrokes to it before an undo or redo.
  void flushNotesForUndo();
  // Drops the undo history of the workspace being left; a profile removal
  // stays (IDIOT-SHELL-2).
  void clearWorkspaceUndo();
  // Each profile keeps its undo history while another one is active (A3-14,
  // A5-7): a stray Ctrl ] must not make a delete final. Parked on leaving a
  // workspace, put back on returning to it.
  QHash<QString, heap::undo::UndoStack> m_parkedUndo;
  void parkWorkspaceUndo(const QString& profileId);
  // Is `key` some task's id or a tracker card's key in this workspace?
  bool ticketKeyTaken(const QString& key) const;
  QString taskIdForKey(const QString& key) const;
  void restoreWorkspaceUndo(const QString& profileId);
  // Runs a recorded move between profiles back (backward) or again, and
  // rewrites the entry to say where the tasks are now. False when either
  // profile is gone or nothing could move.
  bool applyProfileMove_(heap::undo::Entry& entry, bool backward);
  // The "+Profile name" in a line of capture (A5-21), against every profile.
  heap::capture::ProfileSigil profileSigilIn(const QString& raw) const;
  // The twin of a move entry on the other profile's parked stack goes when
  // one side takes the move back (and comes back with a redo).
  QString profileMoveOtherSide_(const heap::undo::Entry& entry) const;
  // Moves `ids` from one profile to another (either may be the active one)
  // and brings the live models in step. See profile/ProfileMove.h.
  heap::profilemove::Result relocateTasks_(const QString& fromId,
                                           const QString& toId,
                                           const QStringList& ids,
                                           heap::profilemove::Options opt);

  // Selection state
  QSet<QString> m_selectedTaskIds;
  QStringList m_selectedTaskIdsList;  // ordered cache for QML
  void rebuildSelectionList_();

  struct SelectionFilter {
    QString search;
    QStringList priorities;
    bool showArchived = true;
    bool hideDone = false;
  } m_selectionFilter;

  // Drops selected ids m_selectionFilter hides; emits when it dropped any.
  void pruneSelectionToFilter_();
  // The one predicate the filter bar counts and the selection prune share.
  bool passesFilter_(int row, const heap::query::TaskQuery& q, const QStringList& priorities, bool showArchived, bool hideDone) const;

  std::unique_ptr<heap::chrono::ChronoParser> m_chrono;

  // ---- Git watcher ----
  std::unique_ptr<heap::git::GitWatcher> m_gitWatcher;

  // ---- Auto-update (HEAP-63) ----
  std::unique_ptr<heap::update::Updater> m_updater;
  heap::update::FeatureBuild* m_featureBuild = nullptr;  // parented to this
  QString m_updateStatus;  // a tr_() key, "" before the first check
  QString m_updateStatusArg;
  QString m_latestReleaseUrl;
  int m_packageKind = 0;  // a heap::update::PackageKind, read once at startup
  bool m_updatesFromStore = false;  // running in a Flatpak, read once at startup
  QTimer* m_updateTimer = nullptr;  // hourly; the updater's gate decides
  QString m_updatePhase;
  double m_updateProgress = 0.0;
  QString m_updateSha256;
  QString m_updatePackage;  // the verified file, once "ready"
  void setUpdatePhase(const QString& phase, const QString& statusKey, const QString& arg = QString());

  // ---- Tracker sync (HEAP-74 GitHub, HEAP-75 Jira + GitLab) ----
  // Every connected + configured provider runs concurrently; a task's
  // externalProvider routes status pushes to the matching one.
  std::vector<std::unique_ptr<heap::integrations::IntegrationProvider>> m_syncProviders;
  // What each provider in m_syncProviders was built from (providerBuildKey):
  // a settings write that leaves it the same keeps the provider and its pull.
  QHash<QString, QByteArray> m_syncProviderKeys;
  // Directory providers live apart from m_syncProviders: they have none of the
  // IntegrationProvider verbs. Keyed by provider id; only Mattermost for now.
  QHash<QString, heap::integrations::MattermostClient*> m_directoryClients;
  QHash<QString, QString> m_directoryConfigHash;
  // Access tokens for the trackers, kept in the OS keychain (HEAP-74/75).
  heap::integrations::SecretStore* m_secretStore = nullptr;
  // Looks for a due periodic pull (integrations.autoSyncMinutes, up to a
  // month; APP-123). The last sync's time lives in autosync.json so a
  // restart does not start the wait over.
  QTimer* m_syncTimer = nullptr;
  QDateTime m_lastTrackerSync;
  QString autoSyncFilePath() const;
  void loadLastTrackerSync();
  void saveLastTrackerSync() const;

 public:
  // The periodic-sync check at `now` (the timer passes the clock). Public
  // for the tests, like runAutomationAt.
  void autoSyncTickAt(const QDateTime& now);

  QDateTime lastTrackerSync() const {
    return m_lastTrackerSync;
  }

  // Live updates (integrations/LiveSync.h, integrations.liveUpdates, on
  // unless set false): the probe-or-pull check at `now`, and the catch-up
  // pull of every tracker (window back, wake, network back). Public for the
  // tests; the timer and the OS signals pass the clock.
  void liveTickAt(const QDateTime& now);
  void liveCatchUpAt(const QDateTime& now);
  bool liveUpdatesEnabled() const;

  heap::integrations::live::ProbeState liveProbeState(const QString& providerId) const {
    return m_liveProbe.value(providerId);
  }

  void setLivePresence(heap::integrations::live::Presence presence) {
    m_livePresence = presence;
  }

 private:
  QTimer* m_liveTimer = nullptr;
  QHash<QString, heap::integrations::live::ProbeState> m_liveProbe;
  // The keys of the status-history lookup in flight, per tracker.
  QHash<QString, QStringList> m_statusTimesAsked;
  // A history lookup asks about this many issues a sync (Jira's own cap).
  static constexpr int kStatusTimeBatch = 40;
  heap::integrations::live::Presence m_livePresence = heap::integrations::live::Presence::Active;
  QDateTime m_liveLastTick;
  QDateTime m_liveLeftAt;
  QDateTime m_liveLastCatchUp;
  void onChangesProbed(const QString& providerId, bool ok, int httpStatus, int retryAfter, const QDateTime& newest);
  // A pull nobody asked for: quiet unless it brings news, like the settle pull.
  // `afterRunningPull`: a probe's change; a pull already under way is
  // followed by one more rather than taken as the answer.
  void livePull(const QString& providerId, const QDateTime& now, bool afterRunningPull = false);

  // Reconcile the active providers with the current integrations settings.
  void applyIntegrationSettings();
  // Merge a provider's non-secret settings with its keychain secrets.
  QVariantMap integrationConfig(const QString& providerId) const;
  // Write one non-secret integration field into the settings blob and persist.
  void setIntegrationField(const QString& providerId, const QString& field, const QVariant& value);
  // The user's status → column choices for this provider, as StatusMap wants
  // them. Keys are the tracker's own status strings.
  QHash<QString, QString> statusOverridesFor(const QString& providerId) const;
  // Record the statuses a pull actually carried, so the mapping UI can offer
  // the real vocabulary instead of asking the user to type it. Returns true
  // when something new was learned and the settings blob needs writing.
  bool rememberSeenStatuses(const QString& providerId, const QStringList& statuses);
  // Same, for several fields at once. Every write rebuilds the providers, so a
  // sign-in that set three fields one by one tore down and rebuilt the sync
  // layer three times — enough to kill a request already in flight.
  void setIntegrationFields(const QString& providerId, const QVariantMap& fields);
  // Card labels of the requiredKeys this provider still has empty. A browser
  // sign-in proves who you are but not what to sync, so the scope fields
  // (Asana workspace, ClickUp list, Sentry org/project, Bitbucket repo) can
  // still be missing afterwards.
  // Public so the card can recompute the notice as fields are edited, rather
  // than only seeing it once in the toast that follows a sign-in.
 public:
  Q_INVOKABLE QStringList missingRequiredFields(const QString& providerId) const;

 private:
  // The same list, told explicitly whether to treat the browser sign-in as
  // having answered the client-ID field. A manual connect is about to clear
  // authMode, so it must be validated as the token card it is becoming — not
  // as the OAuth card the config still says it is.
  QStringList missingRequiredFields(const QString& providerId, bool signedInViaBrowser) const;
  // Finish a Jira browser sign-in: ask accessible-resources which Atlassian
  // site the new token was granted and cache its cloudId. A 3LO token is not
  // bound to a site, and the gateway path needs that id.
  void resolveJiraSite(const QString& accessToken, const QString& label);
  // Build (or rebuild) the directory client from the current config. Returns
  // null when the provider is not connected or not configured. Rebuilds only
  // when the effective config actually changed: applyIntegrationSettings runs
  // on every settings write, and dropping the client mid-fetch would silently
  // abandon a request already in flight.
  heap::integrations::MattermostClient* directoryClient(const QString& providerId);
  void fetchDirectory(const QString& providerId, bool rebindProfile = false);
  // Drop a directory client safely. Never `delete`: this can run from inside
  // the client's own reply handler.
  void retireDirectoryClient(const QString& providerId);
  // Contacts the user deleted, per profile, so a later sync does not bring
  // them back. They cannot live in the docs blob: DocsView rewrites it whole
  // and would drop any key it does not know.
  QStringList dismissedContacts(const QString& providerId) const;
  QStringList dismissedTasks(const QString& providerId) const;
  // Create the Person behind an imported contact, or return the id of the one
  // already there. Never edits an existing Person: those are the user's notes
  // about someone, not a mirror of the directory.
  // `linkedPersonId` is the Person this contact already points at, if any: it
  // is the only id that may be reused when the derived one is already taken.
  QString upsertImportedPerson(const heap::integrations::ExternalContact& ext, const QString& linkedPersonId);

  // A Docs contact's stable identity across a docsState rewrite: its external
  // id when it has one, else its Mattermost handle, else its name. The picker
  // hands this back on save so the Person that was just created can be linked
  // to the contact it came from without holding on to an array index.
  static QString docsContactKey(const QJsonObject& contact);
  // Point the Docs contact `contactKey` at `personId`. No-op when the contact
  // is gone or already links there.
  void linkDocsContact(const QString& contactKey, const QString& personId);
  // Point every Docs contact linked to `fromPersonId` at `toPersonId`: the
  // person was renamed.
  void relinkDocsContacts(const QString& fromPersonId, const QString& toPersonId);
  // Append a Docs contact for a Person created through the rail's picker, so
  // the next search finds them among the contacts.
  void appendDocsContact(const Person& p);
  // One-time move of any plaintext tokens found in state.json into the keychain.
  void migrateLegacySecrets();
  // Renew an expiring OAuth access token, then run `then`. Providers that use a
  // PAT, or whose token does not expire, fall straight through. A browser
  // sign-in hands out a token that lives ~2h (GitLab), so without this every
  // sync after the first couple of hours came back empty.
  // `onFail` runs instead when the token could not be renewed.
  void ensureFreshToken(const QString& providerId, std::function<void()> then, std::function<void()> onFail = {});
  // Refresh if needed, then pull. Looks the provider up again afterwards,
  // because a refresh rebuilds m_syncProviders.
  // `byHand`: the user asked (a card's Sync now, the strip's retry) — one
  // pull goes even while the tracker's sync waits out a failure or is paused.
  void syncProviderNow(const QString& providerId, bool byHand = false);
  // The host a tracker's requests go to, for FeatureGate's log line.
  QString featureHostOf(const QString& providerId) const;
  // FeatureGate's store next to state.json, and its signals.
  void initFeatureGate();
  // Exchange the stored refresh token for a new access token. `done` gets true
  // when the provider is usable again. A refusal from the provider disconnects
  // the card so the user is prompted to sign in again.
  void refreshOAuthToken(const QString& providerId, std::function<void(bool)> done);
  // Used by refreshOAuthToken; providers are rebuilt whenever a secret changes,
  // so it cannot borrow a provider's own manager.
  QNetworkAccessManager* m_oauthNam = nullptr;

  // Calendar subscriptions: the fetcher, the minute tick that decides who is
  // due, and per-subscription state that is not worth saving.
  struct CalSubState {
    QDateTime lastAttempt;
    QDateTime lastSync;
    QString error;
    bool busy = false;
  };

  QNetworkAccessManager* m_calNam = nullptr;
  QTimer* m_calTimer = nullptr;
  // The worker reading the desktop Outlook; waited for on exit.
  QThread* m_outlookThread = nullptr;
  QHash<QString, CalSubState> m_calSubState;
  QSet<QString> m_calSubSecretsLoaded;
  // The desktop Outlook's change events (dogfood 2026-10-19): the watch
  // thread while an Outlook subscription exists, whether it is attached, and
  // the debounce that turns a burst of events into one read.
  heap::cal::OutlookWatch* m_outlookWatch = nullptr;
  bool m_outlookLive = false;
  QTimer* m_outlookDebounce = nullptr;
  QDateTime m_outlookFirstChange;
  // A change arrived while a read was running: read once more after it.
  bool m_outlookReadAgain = false;
  void ensureOutlookWatch(bool wanted);
  void onOutlookChanged();
  QVariantList calendarSubscriptionSettings() const;
  void writeCalendarSubscriptionSettings(const QVariantList& list);
  void applyCalendarSubscriptions();
  void fetchCalendarSubscription(const QString& id);
  // True (and says so) when `id` or `masterId` is a subscription's event.
  bool refuseSubscriptionEdit(const QString& id, const QString& masterId = QString());
  // Providers whose refresh is already in flight — a sync and a push firing
  // together must not both spend the (single-use, rotated) refresh token.
  QSet<QString> m_refreshing;
  // Providers whose 401 already bought one refresh-and-retry, so a tracker that
  // answers 401 no matter what cannot loop. Cleared by a successful pull.
  QSet<QString> m_retriedAfter401;
  // ---- Offline tolerance (audit INT-5/INT-6/INT-8) ----
  QVariantMap integrationStates() const;
  // A provider's own English reason, in the UI language when heap knows it.
  QString providerReason(const QString& reason) const;
  // A refresh that failed for want of a network: keep the session, retry
  // later by the shared back-off (net/Backoff.h), say "offline" meanwhile.
  void scheduleRefreshRetry(const QString& providerId);
  void setProviderOffline(const QString& providerId, bool offline);
  QSet<QString> m_offlineProviders;
  // When each tracker last answered and how its last failure read (APP-164).
  // This session only: a restart starts the page over.
  QHash<QString, heap::integrations::ProviderHealth> m_syncHealth;
  // ---- Sync visibility (APP-180/186/187) ----
  // Pulls out right now, by provider id, each with the serial of its start:
  // a pull that never answers is let go after kSyncWatchdogMs.
  QHash<QString, int> m_syncInFlight;
  int m_syncStartSerial = 0;
  void setSyncInFlight(const QString& providerId, bool inFlight);
  QSet<QString> m_unseenTaskIds;
  int m_unseenRevision = 0;
  // `is:new`: the cards of the latest sync that brought any. A sync run
  // (Sync now, one card's sync, an auto-sync tick) starts a new set; the
  // pulls inside it, a Jira follow-up included, add to it.
  QStringList m_syncNewIds;
  int m_syncRunSerial = 0;
  int m_syncNewSerial = -1;
  void noteSyncAdded(const QStringList& ids, bool markUnseen);
  heap::notify::EventLog m_eventLog;
  // Set while a toast that was already logged with more to it (the tasks or
  // the place it is about) goes out, so the plain toast hook skips it.
  bool m_eventLogMuted = false;
  void toastAndLog(const QString& message, const QString& kind, const QStringList& taskIds = {}, const QString& route = QString());
  // Task history (APP-165). Saved as the root key "taskHistory" of state.json.
  heap::history::TaskHistory m_history;
  // True while a tracker pull is the one changing tasks.
  bool m_historySync = false;
  void recordTaskChange(const Task* before, const Task& after);
  void recordSyncHealth(const QString& providerId, bool ok, int items, int httpStatus, const QString& error);
  // Token refreshes failed in a row (no network, a 5xx) and whether the
  // retry timer is armed: one chain per provider (scheduleRefreshRetry).
  QHash<QString, int> m_refreshFailures;
  QSet<QString> m_refreshRetryArmed;
  // A move that could not be sent (tracker disconnected or unreachable):
  // flagged on the card and sent after the next successful pull.
  void queueTrackerPush(const QString& taskId, const QString& status);
  void resolveTrackerConflictFields(const QString& taskId, const QStringList& fields, bool useTracker);
  void flushQueuedPushes(const QString& providerId);
  // What a pull under the card's current settings is scoped to. See
  // heap::integrations::scopeFingerprint.
  QString scopeFingerprintFor(const QString& providerId) const;
  // Integration setting "reviewMovable" (APP-242, off by default): merge /
  // pull request cards may be moved between columns, locally only.
  bool reviewCardsMovable(const QString& providerId) const;
  // provider + newline + issue key → the statuses the issue's workflow can move
  // to, as of the last pull. Only trackers that report transitions (Jira)
  // fill it; an issue without an entry is not second-guessed.
  QHash<QString, QStringList> m_trackerTransitions;
  // The same answers keyed by where in the workflow they were seen (provider,
  // project, issue type, status): after a push moves an issue, the moves out of
  // its new status are known from any other issue the last pull saw there,
  // instead of the guard dropping until the next pull (INT-4).
  QHash<QString, QStringList> m_workflowTransitions;
  // provider + newline + lower-case status name → the tracker's kind of that
  // status (Jira's statusCategory), as the pulls saw it: what an unmapped
  // status falls back to (dogfood 2026-10-19).
  QHash<QString, QString> m_trackerStatusCategories;
  void learnTrackerStatusCategory(const QString& providerId, const QString& status, const QString& category);
  // StatusMap::column with the status's kind before `fallback`.
  QString trackerColumn(const QString& providerId,
                        const QString& status,
                        const QHash<QString, QString>& overrides,
                        const QString& fallback) const;
  // A tracker's text (a description, a comment) as markdown: Jira Server/DC
  // writes wiki markup, read the way trackerDescription reads it.
  QString trackerTextAsMarkdown(const Task& t, const QString& text) const;
  // The pictures of a ticket's text pointed at the attachments store, or at
  // a stand-in while one is fetched / when it failed (TrackerImages.h).
  // Asks the provider for any not fetched yet.
  QString withTrackerImages(const Task& t, const QString& markdown) const;
  // Where one picture of a ticket stands; starts its fetch when it is due.
  heap::integrations::TrackerImageState trackerImageState(const QString& provider, const heap::integrations::TrackerAttachment& f) const;
  // Now, for the throttled tracker reads (setFetchClockForTest).
  QDateTime fetchNow() const;
  std::function<QDateTime()> m_fetchClock;
  void fetchTrackerImage(const QString& providerId, const QString& attachmentId, const QString& name, const QString& mime);
  // provider + newline + attachment id → attachments-store id, kept in
  // tracker-images.json; failures and fetches in flight are per session.
  mutable QHash<QString, QString> m_trackerImageIds;
  mutable bool m_trackerImagesLoaded = false;
  // When each picture was asked for and how it went: one ask per picture,
  // a failure retried 5, 10, 20… minutes later (2026-10-20 incident rules).
  mutable heap::integrations::FetchBackoff m_trackerImageBackoff;
  void loadTrackerImages() const;
  // A board column whose name says testing ("Testing", "QA", "Тест"), for
  // a testing status nobody mapped; empty when the board has none.
  QString testingColumn() const;
  // Comments last read per ticket (ticketComments), loaded on first use.
  mutable QJsonObject m_commentCache;
  mutable bool m_commentCacheLoaded = false;
  QString commentCachePath() const;
  void loadCommentCache() const;
  void storeTicketComments(const Task& t, const QVector<heap::integrations::ExternalComment>& comments, int total);
  // ticket-comments.json, written debounced by storeTicketComments.
  void writeCommentCache();
  QTimer* m_commentCacheSaveTimer = nullptr;
  static QString commentCacheKey(const Task& t);
  static QString workflowTransitionsKey(const QString& providerId, const QString& project, const QString& issueType, const QString& status);
  // The built-in key `normalized` would collide with if action `id` took it;
  // empty when none. See kBuiltinKeys.
  static QString builtinShortcutOwner(const QString& id, const QString& normalized);
  QString m_focusedTaskId, m_focusedBranch, m_focusedRepo;
  QVariantMap m_focusedRepoState;
  QSet<QString> m_dismissedBranches;  // in-memory only; per branch name
  // CODEOWNERS reports by repo + '\n' + branch, each with its "taskId".
  QHash<QString, QVariantMap> m_codeOwners;
  QSet<QString> m_dismissedOwnerPings;  // prUrl + '|' + handle, in memory
  int m_mrRevision = 0;
  bool m_mrBumpQueued = false;
  void bumpMrRevision();
  void wireMrRevision();
  // PrInfo's review facts into a setGitInfoForId() entry.
  static void putPrReviewFacts(QVariantMap& entry, const QVariantMap& pr);
  void onCodeOwnersUpdated(const QString& repo, const QString& branch, const QVariantMap& report);
  // A repo that stopped being watched takes its reports along (a tracker
  // ticket's report is not a repo's and stays).
  void dropCodeOwnersOutside(const QStringList& repoKeys);
  // One report in m_codeOwners: a changed "who to ping" re-arms the pings
  // the user hid.
  void storeOwnersEntry(const QString& key, const QVariantMap& entry);
  // ---- CODEOWNERS of tracker PR / MR tickets, no CLI (dogfood 2026-10-20) ----
  // The connected GitLab / GitHub trackers as the forge client needs them:
  // host, API base and the same auth headers the tracker sync sends.
  QList<heap::git::ForgeAuth> forgeCredentials() const;
  // Hands them to the watcher (a repo on that forge then needs no gh/glab).
  void pushForgeCredentials();
  // Who owns what a ticket's PR / MR changes — a card the GitHub / GitLab
  // tracker made, or an issue a review was folded into — read through that
  // tracker's API even with no watched repo, kept under "tracker:<taskId>".
  // Only my own PRs. Asked at most once a minute unless `force`.
  void refreshTrackerOwners(const QString& taskId, bool force);
  // The window came back: the tickets already showing owners, and my open
  // PR / MR cards, are asked again (the minute's cache still applies).
  void refreshTrackerOwnersAll();
  heap::git::ForgeApi* forgeApi();
  heap::git::ForgeApi* m_forgeApi = nullptr;
  QHash<QString, QElapsedTimer> m_trackerOwnersAt;  // taskId → last ask
  QHash<QString, QString> m_forgeLogins;            // apiBase → my login there

  // "Ждут меня": the review requests the trackers pulled, by URL (folded
  // into a Jira issue or not), and the watch that reads their threads.
  struct ThreadMr {
    QString provider;  // "gitlab" | "github"
    QString url;
    QString project;
    QString title;
    QString author;
    QStringList reviewers;
    QDateTime createdAt;
    QDateTime updatedAt;
    int number = 0;
    int notes = -1;
    bool open = false;
    QString me;  // tests only: my login, when no forge is connected
  };

  QHash<QString, ThreadMr> m_threadMrs;
  heap::git::MrThreadWatch* m_threadWatch = nullptr;
  QSet<QString> m_threadLoginAsked;        // apiBase: /user asked once this launch
  QHash<QString, QString> m_threadLogins;  // provider → my login there (last sync)
  int m_mrThreadsRev = 0;
  QString m_lastOpenedThreadUrl;
  heap::git::MrThreadWatch* threadWatch();
  bool mrThreadsOn() const;
  // A tracker pull's review requests (mergeExternalTasks); a complete pull
  // forgets the ones of that tracker it no longer carries.
  void noteThreadMrs(const QString& providerId, const QVector<heap::integrations::ExternalTask>& issues, bool complete);
  // Hands the open ones to the watch (which reads only what moved).
  void syncMrThreads();
  QString threadMeFor(const ThreadMr& mr) const;
  QString taskIdForMrUrl(const QString& url) const;
  QString mrUrlForTask(const QString& taskId) const;
  // Repos whose branch the watcher has reported since launch (the first
  // report is the launch itself, not a switch).
  QSet<QString> m_gitReposReported;
  bool m_showWhoseMove = true;        // settings.git.showWhoseMove, cached for the cards
  void applyGitSettingsFromMap(const QVariantMap& git);
  // Re-derive the focused branch's task id under the current id-prefix and
  // refresh the banner. Needed because a prefix change (settings/profile) does
  // not move HEAD, so no branchChanged fires to re-run the match on its own.
  void refreshFocusedTaskId();
  void onGitBranchChanged(const QString& repo, const QString& branch, const QString& matchedId);
  void onGitRepoState(const QString& repo, const QVariantMap& state);
  void onGitCommits(const QString& repo, const QVariantMap& commitsByTask);

  // ── Safety net (APP-157…) ──────────────────────────────────────────
  // Quiet, opt-in heads-ups, each off until switched on under
  // settings.safety. They say what they noticed, once; none of them changes
  // a task, a timer or a status. See AppControllerSafety.cpp.
 public:
  // The end-of-day check (APP-157) at `now`. runAutomationAt() calls it on
  // every tick; it fires at most once a day, at settings.safety.endOfDayTime.
  void checkEndOfDayAt(const QDateTime& now);
  // The "waiting on a reply" reminder (APP-158) at `now`, from the same tick.
  void checkWaitingAt(const QDateTime& now);
  // "Кому написать" at `now` (dogfood 2026-10-20), from the same tick: an
  // ask whose time came notifies once (a missed one on the next launch,
  // within two days), and with "waitingOn" on, a reply waited for
  // `waitingDays` gets one soft nudge.
  void checkPingsAt(const QDateTime& now);

  // settings.safety, for QML: which heads-ups are on.
  Q_PROPERTY(QVariantMap safety READ safetySettings NOTIFY appSettingsJsonChanged)
  // The lunch break (APP-319) for the calendar's band: {start, end} in hours,
  // empty while it is off. Set under settings.calendar.lunchOn/Start/End.
  Q_PROPERTY(QVariantMap lunch READ lunch NOTIFY appSettingsJsonChanged)
  QVariantMap lunch() const;
  // Writes settings.safety.<key>; Settings → Safety net's rows use it.
  Q_INVOKABLE void setSafetySetting(const QString& key, const QVariant& value);
  // Waiting on a reply (APP-158), active profile: task id →
  // { personId, name, color, since, days }.
  Q_PROPERTY(QVariantMap waitingOn READ waitingOnMap NOTIFY waitingOnChanged)
  QVariantMap waitingOnMap() const;

  QVector<WaitingOn> waitingOnLinks() const {
    return m_waitingOn;
  }

  // Link a task to the person whose answer it waits on, from now. Picking
  // someone again (or the same person) starts the wait over and re-arms its
  // one reminder.
  Q_INVOKABLE void setWaitingOn(const QString& taskId, const QString& personId);
  Q_INVOKABLE void clearWaitingOn(const QString& taskId);

  // "You've seen this before" (APP-159): when `text` looks like an error or a
  // stack trace, the note, doc page or task (any profile) that already
  // mentions its gist — { kind, id, title, profileId, date } — or an empty
  // map. Empty too while settings.safety.seenBefore is off. `excludeTaskId`
  // is the task being edited, which would otherwise find itself.
  Q_INVOKABLE QVariantMap seenBefore(const QString& text, const QString& excludeTaskId = QString());

  // Focus mode (APP-160). On: notifications are held back (meetings pass
  // unless settings.safety.immersionPassMeetings is false) and the timer runs
  // on the current task — `preferredTaskId` if it names one, else the single
  // selected task, else the current branch's. Off: the timer it started stops
  // and immersionEnded() says how many notifications wait; nothing is
  // delivered until releaseImmersionHeld(). Not persisted: a restart ends it.
  Q_PROPERTY(bool immersion READ immersion NOTIFY immersionChanged)
  Q_PROPERTY(QDateTime immersionStartedAt READ immersionStartedAt NOTIFY immersionChanged)
  Q_PROPERTY(QString immersionTaskId READ immersionTaskId NOTIFY immersionChanged)
  // How many notifications wait for the end of focus mode (the immersion
  // screen's "3 события подождут", DG-132).
  Q_PROPERTY(int immersionHeldCount READ immersionHeldCountInt NOTIFY immersionChanged)

  bool immersion() const {
    return m_immersionStartedAt.isValid();
  }

  QDateTime immersionStartedAt() const {
    return m_immersionStartedAt;
  }

  QString immersionTaskId() const {
    return m_immersionTaskId;
  }

  qsizetype immersionHeldCount() const {
    return m_immersionHeld.size();
  }

  int immersionHeldCountInt() const {
    return static_cast<int>(m_immersionHeld.size());
  }

  Q_INVOKABLE void startImmersion(const QString& preferredTaskId = QString());
  Q_INVOKABLE void stopImmersion();
  Q_INVOKABLE void toggleImmersion(const QString& preferredTaskId = QString());
  // Hands over what focus mode held back, through the usual paths. Returns
  // how many there were.
  Q_INVOKABLE int releaseImmersionHeld();

  // The standup draft (APP-170): "Yesterday / Today / Blockers" from the last
  // working day's column moves, commits and timer, today's work and meetings,
  // and the blocked cards. Text for the user to edit; nothing is sent.
  Q_INVOKABLE QString standupDraft();
  QString standupDraftFor(const QDate& today);
  // The day's summary (APP-190) for the end-of-day dialog: { date,
  // closed: [{id, title}], carryOver: [{id, title}], timers: [{id, title,
  // since}] }. Closed today and still-open work dated today or earlier come
  // from the active workspace; running timers from every workspace. It only
  // reads. The *At form takes `now`, for tests.
  Q_INVOKABLE QVariantMap endOfDaySummary() const;
  Q_INVOKABLE QVariantMap endOfDaySummaryAt(const QDateTime& now) const;
  // What the end-of-day check reads from the workspace at `now`; the
  // repository part is filled in by git, asynchronously.
  heap::safety::EndOfDayFacts endOfDayFacts() const;
  // The tasks the day's summary reads (APP-190).
  QVector<heap::safety::DayTask> dayTasks() const;

 signals:
  // A safety-net notice for the in-app toast. `taskIds` are the tasks its
  // "Show" opens the board on (may be empty).
  void safetyNotice(const QString& kind, const QString& title, const QString& body, const QStringList& taskIds);
  // Bring the board up narrowed to these tasks (a notice was clicked).
  void safetyOpenTasksRequested(const QStringList& taskIds);
  void waitingOnChanged();
  void immersionChanged();
  // Focus mode ended after `minutes`, holding back `held` notifications.
  void immersionEnded(int held, int minutes);

 private:
  QVariantMap safetySettings() const;
  // Delivers a safety-net notice: held during quiet hours like any other,
  // an OS notification when the window is in the background, the in-app
  // toast (safetyNotice) always.
  void safetyNotify(const QString& kind, const QString& title, const QString& body, const QStringList& taskIds, const QDateTime& now);
  void finishEndOfDay(const QDateTime& now, const heap::safety::RepoDirt& dirt);
  void onWorkingTreeChecked(const QString& repo, int changedFiles, int stashes, bool ok);
  // An end-of-day check waiting for git's answer about m_eodPendingRepo.
  QDateTime m_eodPendingAt;
  QString m_eodPendingRepo;
  // Task id → the newest commit naming it, from the watcher's log.
  QHash<QString, QDateTime> m_lastCommitAt;
  // The active profile's waiting-on links (the others' are in m_profiles).
  QVector<WaitingOn> m_waitingOn;
  // A person's state went from `before` to their current one: a reply ends
  // every wait on them; a deleted person's waits go with them.
  void personStateMoved(const QString& personId, const QString& before);
  // Focus mode (APP-160).
  QDateTime m_immersionStartedAt;
  QString m_immersionTaskId;
  bool m_immersionStartedTimer = false;
  QVector<HeldNotification> m_immersionHeld;
  // Task id → its recent commits ({sha, subject, at}) from the watcher.
  QHash<QString, QVariantList> m_taskCommits;
  // Holds `n` when focus mode says so; true when it did.
  bool holdForImmersion(const HeldNotification& n);
};
