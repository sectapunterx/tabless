#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>

namespace heap::integrations::live {

// Live updates from the trackers (dogfood 2026-10-19, item 14).
//
// A tracker pushes changes through webhooks, and a webhook needs a public
// HTTPS endpoint to call — a desktop app behind a NAT has none, and a relay
// in between would be a server that sees every ticket. So "as soon as it
// changes in Jira" is approximated from this side: while Tabless is open, a
// cheap question every minute or so — "did anything in my filter change since
// the last pull?" (one issue, one field) — and the real pull only when the
// answer is yes. On top of that, a catch-up pull when the user comes back to
// the window after a while, when the machine wakes up and when the network
// returns: the moments a stale board is most likely.
//
// All of it reads; nothing here writes to a tracker. The rules are pure and
// take `now`, so they are tested without a clock (see AppController for the
// bookkeeping around them).

// qBound with every bound taken as 64-bit (Qt 6.11 overloads qBound for
// mixed types, which makes qBound<qint64>(int, …) ambiguous).
inline qint64 boundTo64(qint64 lo, qint64 v, qint64 hi) {
  return std::clamp(v, lo, hi);
}

// How present the user is, which sets how often the probe asks.
enum class Presence : std::uint8_t {
  Active,      // the window has focus
  Background,  // open, but another app is in front
  Hidden,      // minimised or closed to the tray
};

inline constexpr int kProbeActiveSecs = 60;
inline constexpr int kProbeBackgroundSecs = 5 * 60;
inline constexpr int kProbeHiddenSecs = 15 * 60;
// Windows' battery saver: nobody is waiting on a ticket badly enough to
// spend a laptop's last percent on it.
inline constexpr int kProbeSaverSecs = 15 * 60;
// How often the controller's timer looks; the cadences above are multiples.
inline constexpr int kTickSecs = 15;
// Errors back off from a minute, doubling, to half an hour.
inline constexpr int kMinBackoffSecs = 60;
inline constexpr int kMaxBackoffSecs = 30 * 60;
// A Retry-After longer than this is taken as this.
inline constexpr int kMaxRetryAfterSecs = 60 * 60;
// Coming back to the window after this long pulls.
inline constexpr int kAwaySecs = 3 * 60;
// Two catch-up pulls never come closer than this, whatever triggered them.
inline constexpr int kCatchUpGapSecs = 60;
// A tick this much later than due means the machine slept in between.
inline constexpr int kSleepGapSecs = 90;
// The probe's window reaches this far behind the last pull: clocks differ.
inline constexpr int kProbeSlackMinutes = 2;
// A probe unanswered this long is given up on.
inline constexpr int kProbeTimeoutSecs = 2 * 60;
// Beyond a week since the last pull, asking "what changed" is a full pull.
inline constexpr int kProbeMaxWindowMinutes = 7 * 24 * 60;

inline int probeIntervalSecs(Presence presence, bool batterySaver) {
  int secs = kProbeActiveSecs;
  if(presence == Presence::Background) {
    secs = kProbeBackgroundSecs;
  } else if(presence == Presence::Hidden) {
    secs = kProbeHiddenSecs;
  }
  return batterySaver ? qMax(secs, kProbeSaverSecs) : secs;
}

// 60 s, 120 s, 240 s … up to half an hour, for the n-th failure in a row.
inline int backoffSecs(int failures) {
  if(failures <= 0) {
    return 0;
  }
  qint64 secs = kMinBackoffSecs;
  for(int i = 1; i < failures && secs < kMaxBackoffSecs; ++i) {
    secs *= 2;
  }
  return static_cast<int>(qMin<qint64>(secs, kMaxBackoffSecs));
}

// A Retry-After header: delta-seconds or an HTTP date. 0 when absent or
// unreadable, so the caller falls back to its own backoff.
inline int retryAfterSecs(const QByteArray& header, const QDateTime& now) {
  const QByteArray v = header.trimmed();
  if(v.isEmpty()) {
    return 0;
  }
  bool isNumber = false;
  const qint64 secs = v.toLongLong(&isNumber);
  if(isNumber) {
    return static_cast<int>(boundTo64(0, secs, kMaxRetryAfterSecs));
  }
  // HTTP dates end in "GMT", which Qt's RFC 2822 reader does not take.
  QString date = QString::fromLatin1(v);
  if(date.endsWith(QStringLiteral(" GMT"))) {
    date.chop(3);
    date += QStringLiteral("+0000");
  }
  const QDateTime at = QDateTime::fromString(date, Qt::RFC2822Date);
  if(!at.isValid()) {
    return 0;
  }
  return static_cast<int>(boundTo64(0, now.secsTo(at), kMaxRetryAfterSecs));
}

// One tracker's live state.
struct ProbeState {
  QDateTime lastProbe;  // when the last probe went out (local clock)
  QDateTime lastPull;   // when the last pull answered (local clock)
  QDateTime retryAt;    // nothing before this: a 429 or a run of errors
  // The newest `updated` the board already accounts for — the tracker's own
  // clock, so it is compared only with the tracker's timestamps.
  QDateTime watermark;
  // The newest change a probe saw that no pull has taken in yet. It becomes
  // the watermark only when a pull answers: a pull skipped (one already
  // running) or failed must leave the change to be pulled again.
  QDateTime pending;
  int failures = 0;
  bool inFlight = false;
  // A probe found a change while a pull was already running, which may have
  // left before the change: one more pull follows that one.
  bool pullQueued = false;
};

enum class Action : std::uint8_t {
  None,
  Probe,  // ask whether anything changed
  Pull,   // pull outright: there is nothing to compare to
};

// What to do for a tracker that can be probed, at `now`. `syncing` is a pull
// of it already under way.
inline Action nextAction(const ProbeState& s, Presence presence, bool batterySaver, bool syncing, const QDateTime& now) {
  if(syncing) {
    return Action::None;
  }
  // A probe that never answered (its provider rebuilt under it) stops
  // counting as under way after a while.
  if(s.inFlight && s.lastProbe.isValid() && s.lastProbe <= now && s.lastProbe.secsTo(now) < kProbeTimeoutSecs) {
    return Action::None;
  }
  if(s.retryAt.isValid() && now < s.retryAt) {
    return Action::None;
  }
  // The cadence counts from whichever came last: a pull answers the probe's
  // question too.
  QDateTime last = s.lastProbe;
  if(s.lastPull.isValid() && (!last.isValid() || s.lastPull > last)) {
    last = s.lastPull;
  }
  // Half a tick of slack, so a timer a hair early does not wait a whole tick.
  // A `last` in the future (the clock was set back) is stale, not recent:
  // waiting for it would stop the probing until the clock caught up.
  if(last.isValid() && last <= now && last.secsTo(now) < probeIntervalSecs(presence, batterySaver) - (kTickSecs / 2)) {
    return Action::None;
  }
  // Nor can a probe window be measured from a pull in the future: pull.
  if(!s.lastPull.isValid() || s.lastPull > now || s.lastPull.secsTo(now) >= static_cast<qint64>(kProbeMaxWindowMinutes) * 60) {
    return Action::Pull;
  }
  return Action::Probe;
}

// How many minutes back the probe asks about: since the last pull, plus slack.
inline int probeWindowMinutes(const QDateTime& lastPull, const QDateTime& now) {
  if(!lastPull.isValid()) {
    return kProbeMaxWindowMinutes;
  }
  const qint64 secs = qMax<qint64>(0, lastPull.secsTo(now));
  const qint64 minutes = ((secs + 59) / 60) + kProbeSlackMinutes;
  return static_cast<int>(boundTo64(1, minutes, kProbeMaxWindowMinutes));
}

inline void probeSent(ProbeState& s, const QDateTime& now) {
  s.inFlight = true;
  s.lastProbe = now;
}

// A failed request: back off, by the server's Retry-After when a 429 / 503
// gave one, else doubling from a minute.
inline void failed(ProbeState& s, int httpStatus, int retryAfter, const QDateTime& now) {
  ++s.failures;
  int wait = backoffSecs(s.failures);
  if((httpStatus == 429 || httpStatus == 503) && retryAfter > 0) {
    wait = retryAfter;
  }
  s.retryAt = now.addSecs(wait);
}

// The probe's answer. `newest` is the most recently updated issue in the
// window (invalid when none was). True when the board is behind and a pull
// should follow. The change is only noted here (`pending`); the watermark
// moves when a pull has answered, so a change counts once a pull took it in —
// and a tracker slow to show it in that pull does not trigger a pull a minute.
inline bool probeAnswered(ProbeState& s, bool ok, int httpStatus, int retryAfter, const QDateTime& newest, const QDateTime& now) {
  s.inFlight = false;
  if(!ok) {
    failed(s, httpStatus, retryAfter, now);
    return false;
  }
  s.failures = 0;
  s.retryAt = QDateTime();
  if(!newest.isValid()) {
    return false;
  }
  if(s.watermark.isValid() && newest <= s.watermark) {
    return false;
  }
  if(!s.pending.isValid() || newest > s.pending) {
    s.pending = newest;
  }
  return true;
}

// Whether a live pull may go out at `now`: not while a 429 or a run of
// errors asked for a pause.
inline bool mayPull(const ProbeState& s, const QDateTime& now) {
  return !s.retryAt.isValid() || now >= s.retryAt;
}

// A pull answered at `now`; `newest` is the latest `updated` among its issues.
// True when another pull is due: a probe saw a change while this one ran.
inline bool pullAnswered(ProbeState& s, const QDateTime& newest, const QDateTime& now) {
  s.lastPull = now;
  s.failures = 0;
  s.retryAt = QDateTime();
  if(newest.isValid() && (!s.watermark.isValid() || newest > s.watermark)) {
    s.watermark = newest;
  }
  if(s.pullQueued) {
    // This pull may have left before the change; the next one takes it in.
    s.pullQueued = false;
    return true;
  }
  if(s.pending.isValid() && (!s.watermark.isValid() || s.pending > s.watermark)) {
    s.watermark = s.pending;
  }
  s.pending = QDateTime();
  return false;
}

// A pull failed: the change it was for stays pending, and the next probe
// waits out the backoff before it asks again.
inline void pullFailed(ProbeState& s, int httpStatus, int retryAfter, const QDateTime& now) {
  s.pullQueued = false;
  failed(s, httpStatus, retryAfter, now);
}

// Whether a catch-up pull (window back, wake, network back) goes ahead: not
// right after another one, and not when the last pull is this fresh anyway.
inline bool catchUpDue(const QDateTime& lastPull, const QDateTime& lastCatchUp, const QDateTime& now) {
  if(lastCatchUp.isValid() && lastCatchUp <= now && lastCatchUp.secsTo(now) < kCatchUpGapSecs) {
    return false;
  }
  return !lastPull.isValid() || lastPull > now || lastPull.secsTo(now) >= kCatchUpGapSecs;
}

// The window comes back to the front: worth a pull after a real absence.
inline bool returnedAfterAway(const QDateTime& leftAt, const QDateTime& now) {
  return leftAt.isValid() && leftAt <= now && leftAt.secsTo(now) >= kAwaySecs;
}

// The timer ticks every `tickSecs`; a tick far later than that means the
// machine was asleep. A clock set back is not a sleep.
inline bool sleptBetween(const QDateTime& lastTick, const QDateTime& now, int tickSecs) {
  return lastTick.isValid() && lastTick <= now && lastTick.secsTo(now) > static_cast<qint64>(tickSecs) + kSleepGapSecs;
}

}  // namespace heap::integrations::live
