#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

// When a tracker card entered its column (dogfood 2026-10-19). The week recap
// reads Task::statusChangedAt and the status log, and both used to carry the
// moment a pull first noticed a move: auto-sync is off by default, so a ticket
// moved to Review on Friday and pulled on Monday read as moved on Monday. Pure,
// so the rules are tested without a clock or a tracker.
namespace heap::integrations::statustime {

// What a pull alone can tell about when the issue entered its status, before
// the tracker's history is read. The truth lies between `categoryChangedAt`
// (Jira's statuscategorychangedate: the last move that changed the status's
// kind) and `updatedAt` (no move can postdate the issue's last update). A
// move into Done changes the kind, so for a Done status the former is exact
// — when it is newer than `notBefore`, the card's last stamp (an older one
// says the kind did not change this time). Otherwise the update, which a
// comment can push later but never earlier than the move. A new card with
// neither entered its status when it was created. Never later than `now`.
inline QDateTime provisional(const QDateTime& categoryChangedAt,
                             const QDateTime& updatedAt,
                             const QDateTime& createdAt,
                             const QDateTime& notBefore,
                             const QDateTime& now,
                             bool doneKind) {
  // A tracker clock a few minutes ahead is still not the future: its
  // timestamps are held to `now`, not thrown away (thrown away, every move
  // read as made right now — review 2026-10-19).
  const auto clamp = [&now](const QDateTime& d) {
    return d.isValid() ? qMin(d, now) : d;
  };
  const QDateTime category = clamp(categoryChangedAt);
  const QDateTime updated = clamp(updatedAt);
  const QDateTime created = clamp(createdAt);
  if(doneKind && category.isValid() && (!notBefore.isValid() || category > notBefore)) {
    return category;
  }
  if(updated.isValid()) {
    return updated;
  }
  if(category.isValid()) {
    return category;
  }
  if(!notBefore.isValid() && created.isValid()) {
    return created;
  }
  return now;
}

// The time the tracker's history gave for the status a card's issue is in,
// kept in ExternalMeta::details as {statusSince: {status, at}} so it is read
// once per move, not once per sync. Invalid when none is kept for `status`.
inline QDateTime since(const QJsonObject& details, const QString& status) {
  const QJsonObject s = details.value(QStringLiteral("statusSince")).toObject();
  if(status.isEmpty() || s.value(QStringLiteral("status")).toString().compare(status, Qt::CaseInsensitive) != 0) {
    return {};
  }
  return QDateTime::fromString(s.value(QStringLiteral("at")).toString(), Qt::ISODate);
}

inline void setSince(QJsonObject& details, const QString& status, const QDateTime& at) {
  details.insert(QStringLiteral("statusSince"),
                 QJsonObject{{QStringLiteral("status"), status}, {QStringLiteral("at"), at.toString(Qt::ISODateWithMs)}});
}

// How many times the tracker's history was asked about `status` without an
// answer it could use. An issue whose history never gives one (a workflow
// the parser cannot follow) is asked a few times, then left on the
// provisional time — asked forever, it held its place in every capped batch
// and the issues behind it were never asked (review 2026-10-19).
inline constexpr int kMaxTries = 3;

inline int tries(const QJsonObject& details, const QString& status) {
  const QJsonObject t = details.value(QStringLiteral("statusSinceTries")).toObject();
  if(status.isEmpty() || t.value(QStringLiteral("status")).toString().compare(status, Qt::CaseInsensitive) != 0) {
    return 0;
  }
  return t.value(QStringLiteral("n")).toInt();
}

inline void setTries(QJsonObject& details, const QString& status, int n) {
  details.insert(QStringLiteral("statusSinceTries"), QJsonObject{{QStringLiteral("status"), status}, {QStringLiteral("n"), n}});
}

// A pull replaces the tracker's facts on the card; the time read for the
// status the issue is still in is heap's own and stays, and so does the count
// of tries for it.
inline void carrySince(const QJsonObject& previous, QJsonObject& next, const QString& status) {
  if(since(previous, status).isValid()) {
    next.insert(QStringLiteral("statusSince"), previous.value(QStringLiteral("statusSince")));
  }
  if(tries(previous, status) > 0) {
    next.insert(QStringLiteral("statusSinceTries"), previous.value(QStringLiteral("statusSinceTries")));
  }
}

}  // namespace heap::integrations::statustime
