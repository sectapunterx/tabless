#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// The tracker comments kept for reading offline (ticket-comments.json,
// dogfood 2026-10-19): {key: {comments: [{author, body, createdAt, url}],
// total, fetchedAt}}. A cache, not data, so it is bounded twice — by tickets
// and by the text it holds; 300 tickets of 30 long comments each came to
// ~100 MB rewritten on every read (review 2026-10-19). Pure.
namespace heap::integrations::commentcache {

inline constexpr int kMaxTickets = 300;
// Characters of comment text in all, about 4–8 MB on disk.
inline constexpr qsizetype kMaxChars = 2 * 1024 * 1024;

// The comment text one ticket's entry holds, in characters.
inline qsizetype entryChars(const QJsonObject& entry) {
  qsizetype n = 0;
  for(const QJsonValue& v : entry.value(QStringLiteral("comments")).toArray()) {
    const QJsonObject c = v.toObject();
    n += c.value(QStringLiteral("body")).toString().size() + c.value(QStringLiteral("author")).toString().size() +
         c.value(QStringLiteral("url")).toString().size();
  }
  return n;
}

// Drops the tickets read longest ago (fetchedAt, ISO text, so it sorts)
// until both bounds hold. The newest entry always stays: one ticket alone is
// bounded by what a fetch keeps. How many were dropped.
inline int trim(QJsonObject& tickets, int maxTickets = kMaxTickets, qsizetype maxChars = kMaxChars) {
  qsizetype total = 0;
  for(auto it = tickets.constBegin(); it != tickets.constEnd(); ++it) {
    total += entryChars(it.value().toObject());
  }
  int dropped = 0;
  while(tickets.size() > 1 && (tickets.size() > maxTickets || total > maxChars)) {
    QString oldest;
    QString oldestAt;
    for(auto it = tickets.constBegin(); it != tickets.constEnd(); ++it) {
      const QString at = it.value().toObject().value(QStringLiteral("fetchedAt")).toString();
      if(oldest.isEmpty() || at < oldestAt) {
        oldest = it.key();
        oldestAt = at;
      }
    }
    total -= entryChars(tickets.value(oldest).toObject());
    tickets.remove(oldest);
    ++dropped;
  }
  return dropped;
}

}  // namespace heap::integrations::commentcache
