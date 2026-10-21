#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <algorithm>

// The one retry rule for every request Tabless sends to someone else's
// server (owner, 2026-10-21, after a company GitLab's devops called a failing
// request re-sent every minute "brute force"): after a failure the next try
// waits 5 minutes, then 10, 20, 40… doubling, up to a day — or the server's
// own Retry-After when that is longer (a day at most as well). Pure: the
// caller says how many failures in a row and what time it is.
namespace heap::net {

inline constexpr qint64 kFirstRetrySecs = 5 * 60;
inline constexpr qint64 kMaxRetrySecs = 24 * 60 * 60;

// The wait after `failures` failures in a row (0 = none, no wait). A
// `retryAfterSecs` the server asked for counts when it is longer.
inline qint64 retryDelaySecs(int failures, qint64 retryAfterSecs = 0) {
  qint64 secs = 0;
  if(failures > 0) {
    secs = kFirstRetrySecs;
    for(int i = 1; i < failures && secs < kMaxRetrySecs; ++i) {
      secs *= 2;
    }
    secs = std::min(secs, kMaxRetrySecs);
  }
  if(retryAfterSecs > secs) {
    secs = std::min(retryAfterSecs, kMaxRetrySecs);
  }
  return secs;
}

inline qint64 retryDelayMs(int failures, qint64 retryAfterSecs = 0) {
  return retryDelaySecs(failures, retryAfterSecs) * 1000;
}

// A Retry-After header: delta-seconds or an HTTP date, in seconds from `now`,
// at most a day. 0 when absent, unreadable or in the past.
inline qint64 retryAfterHeaderSecs(const QByteArray& header, const QDateTime& now) {
  const QByteArray v = header.trimmed();
  if(v.isEmpty()) {
    return 0;
  }
  bool isNumber = false;
  const qint64 secs = v.toLongLong(&isNumber);
  if(isNumber) {
    return std::clamp<qint64>(secs, 0, kMaxRetrySecs);
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
  return std::clamp<qint64>(now.secsTo(at), 0, kMaxRetrySecs);
}

// Whether an answer is the server failing: no answer at all (network,
// timeout), 429, or a 5xx.
inline bool isServerFailure(int httpStatus) {
  return httpStatus <= 0 || httpStatus == 429 || httpStatus >= 500;
}

}  // namespace heap::net
