#include "integrations/StatusMap.h"

#include <QSet>

namespace heap::integrations {

namespace {

QString norm(const QString& s) {
  return s.trimmed().toLower();
}

}  // namespace

QString StatusMap::defaultColumn(const QString& providerStatus) {
  static const QHash<QString, QString> kTable = {
      // backlog
      {QStringLiteral("backlog"), QStringLiteral("backlog")},
      {QStringLiteral("funnel"), QStringLiteral("backlog")},
      {QStringLiteral("icebox"), QStringLiteral("backlog")},
      // todo
      {QStringLiteral("to do"), QStringLiteral("todo")},
      {QStringLiteral("todo"), QStringLiteral("todo")},
      {QStringLiteral("open"), QStringLiteral("todo")},
      {QStringLiteral("reopened"), QStringLiteral("todo")},
      // GitLab names an open issue "opened"; it fell through to the fallback.
      {QStringLiteral("opened"), QStringLiteral("todo")},
      {QStringLiteral("selected for development"), QStringLiteral("todo")},
      // The words Jira's stock and discovery workflows use for "not started"
      // (dogfood 2026-10-20: a project's own statuses all read "not
      // recognised").
      {QStringLiteral("new"), QStringLiteral("todo")},
      {QStringLiteral("ready"), QStringLiteral("todo")},
      {QStringLiteral("ready for development"), QStringLiteral("todo")},
      {QStringLiteral("accepted"), QStringLiteral("todo")},
      {QStringLiteral("evaluate"), QStringLiteral("todo")},
      {QStringLiteral("in discovery"), QStringLiteral("todo")},
      {QStringLiteral("ready for discovery"), QStringLiteral("todo")},
      // in progress
      {QStringLiteral("in progress"), QStringLiteral("prog")},
      {QStringLiteral("in development"), QStringLiteral("prog")},
      {QStringLiteral("doing"), QStringLiteral("prog")},
      {QStringLiteral("started"), QStringLiteral("prog")},
      // review
      {QStringLiteral("in review"), QStringLiteral("review")},
      {QStringLiteral("review"), QStringLiteral("review")},
      {QStringLiteral("code review"), QStringLiteral("review")},
      {QStringLiteral("in code review"), QStringLiteral("review")},
      {QStringLiteral("ready for review"), QStringLiteral("review")},
      // Testing reads as review on the stock board; a board with a testing
      // column of its own gets them there (AppController::trackerColumn).
      {QStringLiteral("ready for testing"), QStringLiteral("review")},
      {QStringLiteral("in testing"), QStringLiteral("review")},
      {QStringLiteral("testing"), QStringLiteral("review")},
      {QStringLiteral("ready for qa"), QStringLiteral("review")},
      {QStringLiteral("in qa"), QStringLiteral("review")},
      {QStringLiteral("qa"), QStringLiteral("review")},
      // blocked
      {QStringLiteral("blocked"), QStringLiteral("blocked")},
      {QStringLiteral("impediment"), QStringLiteral("blocked")},
      {QStringLiteral("on hold"), QStringLiteral("blocked")},
      {QStringLiteral("waiting"), QStringLiteral("blocked")},
      // done
      {QStringLiteral("done"), QStringLiteral("done")},
      {QStringLiteral("closed"), QStringLiteral("done")},
      {QStringLiteral("resolved"), QStringLiteral("done")},
      {QStringLiteral("complete"), QStringLiteral("done")},
      {QStringLiteral("completed"), QStringLiteral("done")},
      {QStringLiteral("merged"), QStringLiteral("done")},
      // Closed without being done: off the board all the same.
      {QStringLiteral("obsolete"), QStringLiteral("done")},
      {QStringLiteral("cancelled"), QStringLiteral("done")},
      {QStringLiteral("canceled"), QStringLiteral("done")},
      {QStringLiteral("suspended"), QStringLiteral("done")},
      {QStringLiteral("declined"), QStringLiteral("done")},
      {QStringLiteral("rejected"), QStringLiteral("done")},
      {QStringLiteral("won't do"), QStringLiteral("done")},
      {QStringLiteral("won't fix"), QStringLiteral("done")},
      {QStringLiteral("duplicate"), QStringLiteral("done")},
      // Jira's own workflow names in Russian, as a Russian-speaking account
      // sees them, and the words teams use most for their own.
      {QStringLiteral("бэклог"), QStringLiteral("backlog")},
      {QStringLiteral("к выполнению"), QStringLiteral("todo")},
      {QStringLiteral("сделать"), QStringLiteral("todo")},
      {QStringLiteral("открыт"), QStringLiteral("todo")},
      {QStringLiteral("открыта"), QStringLiteral("todo")},
      {QStringLiteral("открыто"), QStringLiteral("todo")},
      {QStringLiteral("переоткрыт"), QStringLiteral("todo")},
      {QStringLiteral("в работе"), QStringLiteral("prog")},
      {QStringLiteral("в процессе"), QStringLiteral("prog")},
      {QStringLiteral("на ревью"), QStringLiteral("review")},
      {QStringLiteral("ревью"), QStringLiteral("review")},
      {QStringLiteral("код-ревью"), QStringLiteral("review")},
      {QStringLiteral("на проверке"), QStringLiteral("review")},
      {QStringLiteral("проверка"), QStringLiteral("review")},
      {QStringLiteral("заблокировано"), QStringLiteral("blocked")},
      {QStringLiteral("заблокирована"), QStringLiteral("blocked")},
      {QStringLiteral("готово"), QStringLiteral("done")},
      {QStringLiteral("выполнено"), QStringLiteral("done")},
      {QStringLiteral("закрыт"), QStringLiteral("done")},
      {QStringLiteral("закрыта"), QStringLiteral("done")},
      {QStringLiteral("закрыто"), QStringLiteral("done")},
      {QStringLiteral("решен"), QStringLiteral("done")},
      {QStringLiteral("решён"), QStringLiteral("done")},
      {QStringLiteral("решена"), QStringLiteral("done")},
      {QStringLiteral("решено"), QStringLiteral("done")},
      // Merge and pull requests (APP-242) have statuses of their own, so the
      // mapping can put them apart from issues: open = waiting for review.
      {QStringLiteral("mr open"), QStringLiteral("review")},
      {QStringLiteral("mr draft"), QStringLiteral("prog")},
      {QStringLiteral("mr merged"), QStringLiteral("done")},
      {QStringLiteral("mr closed"), QStringLiteral("done")},
      {QStringLiteral("pr open"), QStringLiteral("review")},
      {QStringLiteral("pr draft"), QStringLiteral("prog")},
      {QStringLiteral("pr merged"), QStringLiteral("done")},
      {QStringLiteral("pr closed"), QStringLiteral("done")},
  };
  return kTable.value(norm(providerStatus));
}

bool StatusMap::isTestingStatus(const QString& providerStatus) {
  const QString s = norm(providerStatus);
  return s == QLatin1String("ready for testing") || s == QLatin1String("in testing") || s == QLatin1String("testing") ||
         s == QLatin1String("ready for qa") || s == QLatin1String("in qa") || s == QLatin1String("qa");
}

bool StatusMap::closesUndone(const QString& providerStatus) {
  static const QSet<QString> kUndone = {QStringLiteral("obsolete"),
                                        QStringLiteral("cancelled"),
                                        QStringLiteral("canceled"),
                                        QStringLiteral("suspended"),
                                        QStringLiteral("declined"),
                                        QStringLiteral("rejected"),
                                        QStringLiteral("won't do"),
                                        QStringLiteral("won't fix"),
                                        QStringLiteral("duplicate")};
  return kUndone.contains(norm(providerStatus));
}

QString StatusMap::column(const QString& providerStatus, const QHash<QString, QString>& overrides, const QString& fallback) {
  const QString key = norm(providerStatus);
  // User overrides win. Match case-insensitively against normalized keys.
  for(auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
    if(norm(it.key()) == key) {
      return it.value();
    }
  }
  const QString built = defaultColumn(providerStatus);
  return built.isEmpty() ? fallback : built;
}

QString StatusMap::categoryColumn(const QString& statusCategory) {
  const QString c = norm(statusCategory);
  if(c == QLatin1String("new")) {
    return QStringLiteral("todo");
  }
  if(c == QLatin1String("indeterminate")) {
    return QStringLiteral("prog");
  }
  if(c == QLatin1String("done")) {
    return QStringLiteral("done");
  }
  return {};
}

QString StatusMap::priority(const QString& providerPriority, const QString& fallback) {
  static const QHash<QString, QString> kTable = {
      {QStringLiteral("highest"), QStringLiteral("P0")},
      {QStringLiteral("critical"), QStringLiteral("P0")},
      {QStringLiteral("blocker"), QStringLiteral("P0")},
      {QStringLiteral("high"), QStringLiteral("P1")},
      {QStringLiteral("major"), QStringLiteral("P1")},
      {QStringLiteral("medium"), QStringLiteral("P2")},
      {QStringLiteral("normal"), QStringLiteral("P2")},
      {QStringLiteral("low"), QStringLiteral("P3")},
      {QStringLiteral("lowest"), QStringLiteral("P3")},
      {QStringLiteral("minor"), QStringLiteral("P3")},
      {QStringLiteral("trivial"), QStringLiteral("P3")},
  };
  return kTable.value(norm(providerPriority), fallback);
}

}  // namespace heap::integrations
