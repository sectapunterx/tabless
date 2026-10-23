#pragma once

#include "Backoff.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

// A feature that keeps failing stops asking (owner, 2026-10-21). Tabless
// reads from other people's servers for several features at once — each
// tracker's sync, CODEOWNERS, "Ждут меня", CI, tracker images, calendar
// links, live updates. A server that answers one of them with errors for half
// an hour is not helped by an answer a minute: the feature is paused, sends
// nothing at all, and asks once a day whether it works again. Switching the
// feature off is the user's call — the strip offers it, nothing here does.
//
// The rule is pure (FeatureRecord + the functions below take `now`); the
// FeatureGate object around it keeps one record per feature for the whole
// process, persisted next to state.json so a restart does not start the
// hammering over.
namespace heap::net {

// The features, by id. A tracker's own ones carry its integration id.
namespace features {
inline QString codeOwners() {
  return QStringLiteral("codeowners");
}

inline QString mrThreads() {
  return QStringLiteral("mrThreads");
}

inline QString trackerImages() {
  return QStringLiteral("trackerImages");
}

// The git watcher's PR / MR facts (state, CI, reviews) of my branches.
inline QString prFacts() {
  return QStringLiteral("prFacts");
}

inline QString sync(const QString& integrationId) {
  return QStringLiteral("sync:") + integrationId;
}

inline QString live(const QString& integrationId) {
  return QStringLiteral("live:") + integrationId;
}

// Edits of a ticket's content written to the tracker (owner item 16).
inline QString edit(const QString& integrationId) {
  return QStringLiteral("edit:") + integrationId;
}

inline QString ci(const QString& integrationId) {
  return QStringLiteral("ci:") + integrationId;
}

inline QString calendar(const QString& subscriptionId) {
  return QStringLiteral("calendar:") + subscriptionId;
}
}  // namespace features

// Why a feature is paused.
enum class PauseKind : std::uint8_t {
  None,
  Errors,    // 5xx, 429, timeouts, no network — ≥5 in a row over ≥30 min
  NoAccess,  // 401 / 403 — ≥3 in a row
};

inline constexpr int kPauseFailures = 5;
inline constexpr qint64 kPauseSpanSecs = 30 * 60;
inline constexpr int kNoAccessFailures = 3;
inline constexpr qint64 kProbeEverySecs = 24 * 60 * 60;

// What an answer means for the feature's health.
enum class Outcome : std::uint8_t {
  Success,   // 2xx / 3xx
  Failure,   // the server failing or unreachable
  NoAccess,  // 401 / 403
  Neutral,   // a 404, a 400 …: an answer about the data, not the server's health
};

Outcome outcomeOf(int httpStatus);

struct FeatureRecord {
  int failures = 0;      // failures in a row, either kind
  int authFailures = 0;  // 401 / 403 in a row
  QDateTime firstFailureAt;
  QDateTime lastFailureAt;
  QDateTime lastAttemptAt;
  int lastStatus = 0;
  QString host;
  qint64 retryAfterSecs = 0;
  PauseKind pause = PauseKind::None;
  QDateTime pausedAt;
  QDateTime nextProbeAt;
  // The user put the strip's line away (×); a change of state shows it again.
  bool hidden = false;
  // "Попробовать снова": the next request goes, once, whatever the rule says.
  bool retryOnce = false;

  bool paused() const {
    return pause != PauseKind::None;
  }

  bool clean() const {
    return failures == 0 && !paused();
  }

  QJsonObject toJson() const;
  static FeatureRecord fromJson(const QJsonObject& o);
};

// What a feature may do at `now`.
enum class Decision : std::uint8_t {
  Go,      // send
  Wait,    // back-off after a failure still runs (only with `backoff`)
  Paused,  // paused: nothing goes
  Probe,   // paused, but the daily quiet probe is due: one request goes
};

// `backoff`: a feature that is one request stream (a tracker's sync, a
// calendar link) also waits out the shared retry rule between failures; a
// feature of many requests (CODEOWNERS of every MR) leaves that to the
// per-request back-off and is only paused as a whole.
Decision decide(const FeatureRecord& r, const QDateTime& now, bool backoff);

// A request goes out under Go or Probe. A probe moves the next one a day on,
// so the other requests of the feature stay held while it runs.
void noteAttempt(FeatureRecord& r, Decision d, const QDateTime& now);

enum class Change : std::uint8_t {
  None,
  Paused,       // just paused
  StillPaused,  // a probe failed
  Resumed,      // a success while paused
};

Change noteFailure(FeatureRecord& r, int httpStatus, const QString& host, qint64 retryAfterSecs, const QDateTime& now);
Change noteSuccess(FeatureRecord& r, const QDateTime& now);
// Dispatch by outcomeOf(httpStatus).
Change noteAnswer(FeatureRecord& r, int httpStatus, const QString& host, qint64 retryAfterSecs, const QDateTime& now);

// "feature codeowners paused: 6 × HTTP 500 since 15:23 on host gitlab.x.ru"
// — for the owner to show the server's admins.
QString pauseLogLine(const QString& feature, const FeatureRecord& r);

class FeatureGate : public QObject {
  Q_OBJECT
 public:
  static FeatureGate& instance();

  // Where the records live (a json next to state.json); loads it. Empty: in
  // memory only.
  void setStorePath(const QString& path);

  QString storePath() const {
    return m_path;
  }

  // May `feature` send a request now? Counts the attempt when it may.
  bool allow(const QString& feature, bool backoff = false);
  // Without counting anything.
  Decision peek(const QString& feature, bool backoff = false) const;
  bool isPaused(const QString& feature) const;
  // How a request of `feature` ended. `httpStatus` 0 = no answer; a negative
  // one is a failure without HTTP (a CLI that exited non-zero).
  void report(const QString& feature, int httpStatus, const QString& host = {}, qint64 retryAfterSecs = 0);

  // "Попробовать снова": exactly one request may go now.
  void retryOnce(const QString& feature);
  // ×: the strip hides the line until the state changes.
  void hide(const QString& feature);
  // The feature was switched off or its connection removed: forget it.
  void forget(const QString& feature);

  FeatureRecord record(const QString& feature) const {
    return m_records.value(feature);
  }

  QStringList pausedFeatures() const;

  // Tests: a clock to move by hand, and a clean slate (in memory).
  static void setClockForTests(std::function<QDateTime()> now);
  void resetForTests();
  QDateTime now() const;

 signals:
  void changed();
  void paused(const QString& feature);
  void resumed(const QString& feature);

 private:
  FeatureGate() = default;
  void load();
  void save() const;
  void apply(const QString& feature, Change c);

  QHash<QString, FeatureRecord> m_records;
  QString m_path;
};

}  // namespace heap::net
