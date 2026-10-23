#pragma once

#include "integrations/IntegrationTypes.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace heap::integrations {

// Abstract interface every tracker connector (Jira / GitHub / GitLab) will
// implement. Defined now as the foundation for the post-release two-way sync;
// concrete providers, the OAuth flow, the SecretStore and the SyncScheduler
// land in follow-up work. All operations are async — results arrive via the
// signals so the UI thread never blocks on the network.
class IntegrationProvider : public QObject {
  Q_OBJECT
 public:
  ~IntegrationProvider() override = default;

  virtual QString id() const = 0;  // "jira" | "github" | "gitlab"
  virtual QString displayName() const = 0;

  // Validate credentials. Emits connectionTested.
  virtual void testConnection() = 0;
  // Pull external tasks matching the configured filter. Emits tasksFetched.
  virtual void pullTasks() = 0;
  // Push a local status change back to the tracker. Emits taskPushed. `project`
  // is the issue's own repo/project, when known: it is written back there, not
  // to the configured one, which may since point somewhere else.
  virtual void pushStatusChange(const QString& externalId, const QString& newStatus, const QString& project) = 0;

  // How issueChecked answers "is it still in the filter?". Plain ints: they
  // travel in a signal argument.
  static constexpr int FilterUnknown = -1;  // the tracker cannot be asked; only the status was checked
  static constexpr int FilterOut = 0;
  static constexpr int FilterIn = 1;

  // A fresh look at one issue right before its status is written (APP-204):
  // its status now, and whether it still matches the configured filter.
  // Answers issueChecked once. The default cannot ask, and says so with the
  // error "unsupported" — no status is then written for that tracker.
  virtual void checkIssue(const QString& externalId, const QString& project) {
    emit issueChecked(externalId, project, false, 0, QStringLiteral("unsupported"), QString(), FilterUnknown);
  }

  // Read one issue's most recent comments (HEAP-117). `project` is the issue's
  // own repo/project, which in a cross-project pull is not the configured one.
  // Always answers commentsFetched, including with an error. The default says
  // "unsupported" so a provider that has no comment endpoint needs no code.
  virtual void fetchComments(const QString& externalId, const QString& /*project*/) {
    emit commentsFetched(externalId, {}, QStringLiteral("unsupported"));
  }

  // CI of a merge / pull request (APP-318), on demand. `ref` is what the
  // sync stored: the pipeline id (GitLab) or the head commit (GitHub).
  // Always answer, with an error when they cannot: "unsupported" by default.
  virtual void fetchCiJobs(const QString& externalId, const QString& /*project*/, const QString& /*ref*/) {
    emit ciJobsFetched(externalId, {}, QStringLiteral("unsupported"), {});
  }

  virtual void fetchCiLog(const QString& externalId, const QString& /*project*/, const QString& jobId) {
    emit ciLogFetched(externalId, jobId, QString(), QStringLiteral("unsupported"));
  }

  // Editing what came from the tracker (owner item 16): the description, the
  // title, the due date, the assignee, a Smart Checklist item. Only a
  // provider that says it can is asked, and AppController asks only where
  // the user switched content writes on. Answers contentEdited once; the
  // provider reads the issue first and writes nothing when the tracker's
  // value is no longer edit.base (a conflict).
  virtual bool canEditContent() const {
    return false;
  }

  virtual void editContent(const ContentEdit& edit) {
    ContentEditResult r;
    r.externalId = edit.externalId;
    r.project = edit.project;
    r.field = edit.field;
    r.outcome = QStringLiteral("unsupported");
    r.error = QStringLiteral("unsupported");
    emit contentEdited(r);
  }

  // Who the issue can be assigned to, matching `query` (read only). Answers
  // assignableUsersFound once: [{id, name, email}].
  virtual void searchAssignableUsers(const QString& externalId, const QString& query) {
    emit assignableUsersFound(externalId, query, {}, QStringLiteral("unsupported"));
  }

  // Every status the workflows of the synced projects can put an issue in, so
  // the mapping UI can offer them before an issue in that status has ever
  // been pulled. Answers statusesFetched or projectStatusesFetched (Jira:
  // after a pull, for the projects its issues are in — the whole instance's
  // list buried the user's own statuses, dogfood 2026-10-20); the default
  // has nothing to say.
  virtual void fetchStatuses() {
  }

  // The projects whose statuses were read before, and when (kept in the
  // settings), so a rebuilt provider or a restart does not ask again.
  virtual void seedProjectStatuses(const QHash<QString, QDateTime>& /*fetchedAt*/) {
  }

  // An attachment's bytes, read only (dogfood 2026-10-20: screenshots in a
  // ticket's text). Answers attachmentFetched once; the default cannot.
  virtual void fetchAttachment(const QString& attachmentId, const QString& /*fileName*/) {
    emit attachmentFetched(attachmentId, QByteArray(), QStringLiteral("unsupported"));
  }

  // Look issues up by id, outside the configured filter. A filter like
  // "statusCategory != Done" drops an issue the moment it is closed, and its
  // absence from the pull read as "deleted in the tracker" (INT-6, audit
  // 2026-09-30). Answers issuesLookedUp once, with the issues that exist and
  // the ids the tracker says do not; an id that failed for another reason is
  // in neither. Only a provider that says it can is asked.
  virtual bool canLookUpIssues() const {
    return false;
  }

  virtual void lookUpIssues(const QStringList& /*externalIds*/) {
    emit issuesLookedUp({}, {});
  }

  // When each issue entered its current status, from the tracker's history
  // (dogfood 2026-10-19). Asked only for the issues whose time is not known
  // yet, after a pull. Answers statusChangesFetched once, with what it could
  // read; an issue that failed is left out and asked for again next sync.
  virtual bool canFetchStatusChanges() const {
    return false;
  }

  virtual void fetchStatusChanges(const QStringList& /*externalIds*/) {
    emit statusChangesFetched({});
  }

  // Live updates (integrations/LiveSync.h): a cheap "did anything in the
  // filter change in the last `withinMinutes`?" — one issue, one field — so
  // the full pull runs only when the answer is yes. Answers changesProbed
  // once. Only a provider that says it can is asked.
  virtual bool canProbeChanges() const {
    return false;
  }

  virtual void probeChanges(int /*withinMinutes*/) {
    emit changesProbed(false, 0, 0, QDateTime(), QStringLiteral("unsupported"));
  }

  // The user's status → column choices, which a push must honour when it
  // picks a target status. The default provider pushes by column name alone.
  virtual void setStatusOverrides(const QHash<QString, QString>& /*overrides*/) {
  }

  // Whether the most recent tasksFetched carried every issue the filter
  // matches. False after a walk cut short by an error or the page cap: an
  // issue missing from such a pull may simply be on a page never fetched, so
  // it must not be read as deleted upstream.
  bool lastPullComplete() const {
    return m_lastPullComplete;
  }

  // How many comments the issue of the last commentsFetched has in all (the
  // list holds the newest few); -1 when the tracker did not say.
  int lastCommentTotal() const {
    return m_lastCommentTotal;
  }

  // The Retry-After (seconds) of the failure the last pullFailed reported, 0
  // when the server gave none — the live updates wait it out.
  int lastRetryAfterSecs() const {
    return m_lastRetryAfterSecs;
  }

 signals:
  void connectionTested(bool ok, const QString& error);
  // Only emitted for a successful pull. A failed one used to report an empty
  // list, which the UI could not tell apart from an empty backlog ("Synced 0
  // issue(s)") — failures go to pullFailed with the provider's own message.
  void tasksFetched(const QVector<ExternalTask>& tasks);
  void pullFailed(int httpStatus, const QString& error);
  // `project` echoes the one pushStatusChange was given, so two issues that share
  // a number in different repos are told apart. `remoteStatus` is the tracker's
  // status for the issue after a push that went through, as a pull would report
  // it, or empty when the tracker did not say.
  void taskPushed(const QString& externalId, const QString& project, bool ok, const QString& error, const QString& remoteStatus);
  // Newest first. An empty list with an empty error means the issue has none.
  // How many the issue has in all is lastCommentTotal() right after, when said.
  void commentsFetched(const QString& externalId, const QVector<ExternalComment>& comments, const QString& error);
  void statusesFetched(const QStringList& statuses);
  // The statuses of each project asked about, in full; a project whose
  // request failed is left out and asked again after the next pull.
  void projectStatusesFetched(const QVector<ExternalStatus>& statuses);
  // The tracker's working time (Jira: {enabled, hoursPerDay, daysPerWeek}),
  // what a "1d" or "1w" of its estimates is (item 19, dogfood 2026-10-20).
  void timeTrackingFetched(const QJsonObject& config);
  // fetchAttachment's answer: the bytes, or an error (bytes empty).
  void attachmentFetched(const QString& attachmentId, const QByteArray& bytes, const QString& error);
  void statusChangesFetched(const QVector<ExternalStatusChange>& changes);
  // The failed jobs of a pipeline: [{id, name, stage, seconds, url, summary,
  // hasLog}] (heap::ci), or an error; `counts` is how far the run got,
  // {total, done, failed}.
  void ciJobsFetched(const QString& externalId, const QVariantList& jobs, const QString& error, const QVariantMap& counts);
  // The tail of one job's log, cleaned (heap::ci::logTail), or an error.
  void ciLogFetched(const QString& externalId, const QString& jobId, const QString& text, const QString& error);
  void issuesLookedUp(const QVector<ExternalTask>& found, const QStringList& missing);
  void contentEdited(const heap::integrations::ContentEditResult& result);
  void assignableUsersFound(const QString& externalId, const QString& query, const QVariantList& users, const QString& error);
  // probeChanges' answer: `newest` is the latest `updated` among the issues
  // changed in the window (the tracker's clock), invalid when none was.
  // `retryAfter` is the server's Retry-After in seconds, 0 when it gave none.
  void changesProbed(bool ok, int httpStatus, int retryAfter, const QDateTime& newest, const QString& error);
  // checkIssue's answer. `httpStatus` 0 with ok=false means the tracker was
  // never reached (offline). `remoteStatus` is as a pull would report it;
  // `filterMatch` is FilterIn, FilterOut or FilterUnknown.
  void issueChecked(const QString& externalId,
                    const QString& project,
                    bool ok,
                    int httpStatus,
                    const QString& error,
                    const QString& remoteStatus,
                    int filterMatch);

 public:
  // Taken out of the app's provider list (switched off, signed out, or
  // rebuilt by a settings write), alive only until the event loop frees it:
  // a provider that checks this sends nothing more, so a read answered in
  // that window does not go on to its write.
  void retire() {
    m_retired = true;
  }

  bool retired() const {
    return m_retired;
  }

 protected:
  using QObject::QObject;

  // Providers set this right before emitting tasksFetched.
  void setLastPullComplete(bool complete) {
    m_lastPullComplete = complete;
  }

  // Providers set this right before emitting commentsFetched.
  void setLastCommentTotal(int total) {
    m_lastCommentTotal = total;
  }

  // Providers set this right before emitting pullFailed.
  void setLastRetryAfterSecs(int secs) {
    m_lastRetryAfterSecs = qMax(0, secs);
  }

 private:
  bool m_lastPullComplete = true;
  int m_lastCommentTotal = -1;
  int m_lastRetryAfterSecs = 0;
  bool m_retired = false;
};

}  // namespace heap::integrations
