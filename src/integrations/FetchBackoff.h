#pragma once

#include "net/Backoff.h"

#include <QDateTime>
#include <QHash>
#include <QString>

// When a remote read may go out again (2026-10-20: a GitLab request that
// failed was re-sent every minute and the company's devops called the app a
// brute force). Every read that is not the sync itself goes through one of
// these: a success is good for `freshFor`, a failure — or an answer that never
// came, e.g. the provider was rebuilt while it was out — waits 5 minutes, then
// 10, 20… up to a day (heap::net::retryDelaySecs, the one rule for every
// remote read). Pure: the caller says what time it is.
namespace heap::integrations {

class FetchBackoff {
 public:
  static constexpr qint64 kFirstRetrySecs = heap::net::kFirstRetrySecs;
  static constexpr qint64 kMaxRetrySecs = heap::net::kMaxRetrySecs;

  // The wait after `attempts` asks in a row with no success.
  static qint64 retryAfterSecs(int attempts) {
    return heap::net::retryDelaySecs(attempts);
  }

  // May `key` be asked now? Not while the last ask's wait runs, nor while a
  // success is younger than `freshForSecs`.
  bool due(const QString& key, const QDateTime& now, qint64 freshForSecs) const {
    const auto it = m_entries.constFind(key);
    if(it == m_entries.constEnd()) {
      return true;
    }
    if(it->attempts > 0 && it->askedAt.isValid() && it->askedAt.secsTo(now) < retryAfterSecs(it->attempts)) {
      return false;
    }
    return !(it->okAt.isValid() && it->okAt.secsTo(now) < freshForSecs);
  }

  // An ask went out: counts as a failure until it succeeds.
  void asked(const QString& key, const QDateTime& now) {
    Entry& e = m_entries[key];
    e.askedAt = now;
    ++e.attempts;
    e.error.clear();
  }

  void succeeded(const QString& key, const QDateTime& now) {
    Entry& e = m_entries[key];
    e.okAt = now;
    e.attempts = 0;
    e.error.clear();
  }

  void failed(const QString& key, const QString& error) {
    m_entries[key].error = error.isEmpty() ? QStringLiteral("failed") : error;
  }

  // Asked, not answered yet, and its wait not over: still on its way.
  bool pending(const QString& key, const QDateTime& now) const {
    const auto it = m_entries.constFind(key);
    return it != m_entries.constEnd() && it->attempts > 0 && it->error.isEmpty() && it->askedAt.isValid() &&
           it->askedAt.secsTo(now) < retryAfterSecs(it->attempts);
  }

  // The last failure's message while no success followed; empty otherwise.
  QString error(const QString& key) const {
    const auto it = m_entries.constFind(key);
    return it == m_entries.constEnd() || it->attempts == 0 ? QString() : it->error;
  }

  QDateTime succeededAt(const QString& key) const {
    return m_entries.value(key).okAt;
  }

  // A success known from before (stored), unless one is known here already.
  void seedSuccess(const QString& key, const QDateTime& at) {
    Entry& e = m_entries[key];
    if(!e.okAt.isValid() || e.okAt < at) {
      e.okAt = at;
    }
  }

  // As if never asked: a request whose answer can no longer come.
  void forget(const QString& key) {
    m_entries.remove(key);
  }

  void clear() {
    m_entries.clear();
  }

 private:
  struct Entry {
    QDateTime okAt;
    QDateTime askedAt;
    int attempts = 0;
    QString error;
  };

  QHash<QString, Entry> m_entries;
};

}  // namespace heap::integrations
