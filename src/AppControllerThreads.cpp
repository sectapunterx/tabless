// "Ждут меня" (dogfood 2026-10-21 item 2): the owner kept in their head where
// on a merge / pull request they were pinged and which threads were left
// unanswered. Now the sidebar says so, oldest wait first; the MR card under
// the cursor says "1 тред ждёт тебя"; the MR's document lists its open
// threads, waiting on me and waiting on them.
//
// Where it comes from: the review requests a tracker pull already brought
// (mergeExternalTasks → noteThreadMrs) and, for each open one, its threads —
// read through ForgeApi's gate (back-off, breaker, ≤2 in flight, dedupe) and
// only when the list says the request moved: its updated_at or its note
// count. A quiet request is never asked again (incident 2026-10-20: a
// company GitLab took a re-asked request for brute force). Read only:
// nothing is posted, resolved or reacted to.

#include "AppController.h"

#include "git/ForgeApi.h"
#include "git/MrThreads.h"
#include "integrations/IntegrationTypes.h"
#include "net/FeatureGate.h"
#include "people/PingTime.h"

#include <QDesktopServices>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

bool isOpenState(const QString& state) {
  return state == QLatin1String("open") || state == QLatin1String("opened");
}

// "Вчера" is "1 дн"; under a minute is still a minute ("написал 1 мин назад").
QString agoAtLeastMinute(const QDateTime& since, const QDateTime& now, bool ru) {
  if(!since.isValid()) {
    return {};
  }
  const QDateTime from = since.secsTo(now) < 60 ? now.addSecs(-60) : since;
  return heap::people::agoShort(from, now, ru);
}

QString excerptOf(const QString& body) {
  QString s = body.simplified();
  // Quoted lines and code fences say little in one line.
  s.remove(QStringLiteral("```"));
  constexpr int kMax = 160;
  if(s.size() > kMax) {
    s = s.left(kMax - 1).trimmed() + QChar(0x2026);
  }
  return s;
}

}  // namespace

heap::git::MrThreadWatch* AppController::threadWatch() {
  if(m_threadWatch == nullptr) {
    m_threadWatch = new heap::git::MrThreadWatch(forgeApi(), this);
    connect(m_threadWatch, &heap::git::MrThreadWatch::changed, this, [this]() {
      ++m_mrThreadsRev;
      emit mrThreadsChanged();
      bumpMrRevision();
    });
  }
  return m_threadWatch;
}

bool AppController::mrThreadsOn() const {
  return settingsMap().value("git").toMap().value("mrThreads", true).toBool();
}

void AppController::noteThreadMrs(const QString& providerId, const QVector<heap::integrations::ExternalTask>& issues, bool complete) {
  if(providerId != QLatin1String("gitlab") && providerId != QLatin1String("github")) {
    return;
  }
  QSet<QString> seen;
  for(const heap::integrations::ExternalTask& ext : issues) {
    const QString kind = ext.details.value(QStringLiteral("kind")).toString();
    if((kind != QLatin1String("mr") && kind != QLatin1String("pr")) || ext.url.isEmpty()) {
      continue;
    }
    ThreadMr mr;
    mr.provider = providerId;
    mr.url = ext.url;
    mr.project = ext.project;
    mr.title = ext.title;
    mr.author = ext.author;
    mr.createdAt = ext.createdAt;
    mr.updatedAt = ext.updatedAt;
    mr.notes = ext.commentCount;
    mr.number = QString(ext.externalId).remove(QChar('!')).remove(QChar('#')).toInt();
    mr.open = isOpenState(ext.details.value(QStringLiteral("state")).toString());
    for(const auto& r : ext.details.value(QStringLiteral("reviewers")).toArray()) {
      mr.reviewers << r.toString();
    }
    seen.insert(mr.url);
    m_threadMrs.insert(mr.url, mr);
  }
  if(complete) {
    m_threadMrs.removeIf([&](const QHash<QString, ThreadMr>::iterator& it) {
      return it.value().provider == providerId && it.value().me.isEmpty() && !seen.contains(it.key());
    });
  }
  syncMrThreads();
}

void AppController::syncMrThreads() {
  if(!mrThreadsOn()) {
    if(m_threadWatch != nullptr) {
      m_threadWatch->clear();
    }
    return;
  }
  if(m_threadMrs.isEmpty() && m_threadWatch == nullptr) {
    return;
  }
  const QList<heap::git::ForgeAuth> creds = forgeCredentials();
  QHash<QString, heap::git::ForgeAuth> authOf;
  for(const heap::git::ForgeAuth& a : creds) {
    authOf.insert(a.providerId, a);
  }
  QSet<QString> needLogin;
  for(const ThreadMr& mr : std::as_const(m_threadMrs)) {
    if(mr.open && mr.me.isEmpty() && authOf.contains(mr.provider)) {
      needLogin.insert(mr.provider);
    }
  }
  m_threadLogins.clear();
  for(const heap::git::ForgeAuth& a : creds) {
    const QString login = m_forgeLogins.value(a.apiBase);
    m_threadLogins.insert(a.providerId, login);
    // Who I am there: asked once per launch (and shared with CODEOWNERS);
    // without it nothing is read and nothing shown.
    if(login.isEmpty() && needLogin.contains(a.providerId) && !m_threadLoginAsked.contains(a.apiBase)) {
      m_threadLoginAsked.insert(a.apiBase);
      forgeApi()->fetchLogin(a.withFeature(heap::net::features::mrThreads()), [this, a](const QString& me, int) {
        if(!me.isEmpty()) {
          m_forgeLogins.insert(a.apiBase, me);
          syncMrThreads();
        }
      });
    }
  }
  QList<heap::git::WatchedMr> list;
  list.reserve(m_threadMrs.size());
  for(const ThreadMr& mr : std::as_const(m_threadMrs)) {
    heap::git::WatchedMr w;
    w.url = mr.url;
    w.auth = mr.me.isEmpty() ? authOf.value(mr.provider) : heap::git::ForgeAuth{};
    w.project = mr.project;
    w.number = mr.number;
    w.stamp = heap::git::threadStamp(mr.updatedAt, mr.notes);
    w.me = threadMeFor(mr);
    w.open = mr.open;
    list.append(w);
  }
  threadWatch()->sync(list);
  ++m_mrThreadsRev;
  emit mrThreadsChanged();
}

QString AppController::threadMeFor(const ThreadMr& mr) const {
  return mr.me.isEmpty() ? m_threadLogins.value(mr.provider) : mr.me;
}

QString AppController::taskIdForMrUrl(const QString& url) const {
  if(url.isEmpty()) {
    return {};
  }
  for(const Task& t : m_tasks.items()) {
    if(!t.archived && t.externalUrl == url) {
      return t.id;
    }
  }
  // Folded into a Jira issue (APP-293): the issue's card is the request's.
  for(auto it = m_foldedReviews.constBegin(); it != m_foldedReviews.constEnd(); ++it) {
    if(it->url == url && m_tasks.indexOfId(it.key()) >= 0) {
      return it.key();
    }
  }
  return {};
}

QString AppController::mrUrlForTask(const QString& taskId) const {
  const int row = taskId.isEmpty() ? -1 : m_tasks.indexOfId(taskId);
  if(row < 0) {
    return {};
  }
  const Task& t = m_tasks.items().at(row);
  if(isReviewItem(t)) {
    return t.externalUrl;
  }
  return m_foldedReviews.value(taskId).url;
}

namespace {

struct Judged {
  QString url;
  heap::git::MrWaits waits;
};

}  // namespace

QVariantList AppController::mrWaitRows() const {
  if(!mrThreadsOn()) {
    return {};
  }
  const bool ru = m_language == QStringLiteral("ru");
  const QDateTime now = QDateTime::currentDateTime();
  QList<Judged> judged;
  for(const ThreadMr& mr : std::as_const(m_threadMrs)) {
    const QString me = threadMeFor(mr);
    if(!mr.open || me.isEmpty()) {
      continue;
    }
    const heap::git::MrThreadFacts facts = m_threadWatch != nullptr ? m_threadWatch->factsFor(mr.url) : heap::git::MrThreadFacts{};
    // GitLab keeps a reviewer listed after the review: only its threads
    // and pushes say whether one is still owed.
    if(!facts.ok && mr.provider == QLatin1String("gitlab")) {
      continue;
    }
    heap::git::MrBrief brief;
    brief.url = mr.url;
    brief.author = mr.author;
    brief.reviewers = mr.reviewers;
    brief.createdAt = mr.createdAt;
    brief.open = true;
    brief.reviewersPending = mr.provider == QLatin1String("github");
    const heap::git::MrWaits w = heap::git::summarizeWaits(brief, facts, me);
    if(w.waitsOnMe()) {
      judged.append({mr.url, w});
    }
  }
  std::sort(judged.begin(), judged.end(), [](const Judged& a, const Judged& b) {
    return heap::git::waitsLonger(a.waits, a.url, b.waits, b.url);
  });
  QVariantList rows;
  for(const Judged& j : std::as_const(judged)) {
    const ThreadMr mr = m_threadMrs.value(j.url);
    const QString pid = j.waits.who.isEmpty() ? QString() : personIdForHandle(j.waits.who);
    const int prow = pid.isEmpty() ? -1 : m_people.indexOfId(pid);
    QString who = prow >= 0 ? m_people.items().at(prow).name : j.waits.who;
    if(who.startsWith(QLatin1Char('@'))) {
      who = who.mid(1);
    }
    const bool gitlab = mr.provider == QLatin1String("gitlab");
    const QString taskId = taskIdForMrUrl(mr.url);
    // "✕ CI" on the row: the same reading as the card's (dogfood 2026-10-22).
    const QString ci = taskId.isEmpty() ? QString() : ciOf(taskId).value(QStringLiteral("state")).toString();
    rows.append(QVariantMap{{QStringLiteral("url"), mr.url},
                            {QStringLiteral("ref"), (gitlab ? QStringLiteral("!") : QStringLiteral("#")) + QString::number(mr.number)},
                            {QStringLiteral("title"), mr.title},
                            {QStringLiteral("reason"), j.waits.reason},
                            {QStringLiteral("count"), j.waits.onMe},
                            {QStringLiteral("review"), j.waits.review},
                            {QStringLiteral("who"), who},
                            {QStringLiteral("since"), j.waits.since},
                            {QStringLiteral("ago"), heap::people::agoShort(j.waits.since, now, ru)},
                            {QStringLiteral("openUrl"), j.waits.url.isEmpty() ? mr.url : j.waits.url},
                            {QStringLiteral("flavor"), gitlab ? QStringLiteral("gitlab") : QStringLiteral("github")},
                            {QStringLiteral("ci"), ci},
                            {QStringLiteral("taskId"), taskId}});
  }
  return rows;
}

QVariantMap AppController::mrThreadsBrief(const QString& taskId) const {
  const QString url = mrUrlForTask(taskId);
  const auto it = m_threadMrs.constFind(url);
  if(url.isEmpty() || it == m_threadMrs.constEnd() || !it->open || !mrThreadsOn() || m_threadWatch == nullptr) {
    return {};
  }
  const QString me = threadMeFor(*it);
  const heap::git::MrThreadFacts facts = m_threadWatch->factsFor(url);
  if(me.isEmpty() || !facts.ok) {
    return {};
  }
  heap::git::MrBrief brief;
  brief.url = url;
  brief.author = it->author;
  brief.open = true;
  const heap::git::MrWaits w = heap::git::summarizeWaits(brief, facts, me);
  if(w.onMe <= 0) {
    return {};
  }
  QDateTime oldest;
  for(const heap::git::ThreadItem& item : w.items) {
    if(item.verdict.wait == heap::git::ThreadWait::OnMe && item.verdict.since.isValid() &&
       (!oldest.isValid() || item.verdict.since < oldest)) {
      oldest = item.verdict.since;
    }
  }
  return {{QStringLiteral("count"), w.onMe},
          {QStringLiteral("ago"), heap::people::agoShort(oldest, QDateTime::currentDateTime(), m_language == QStringLiteral("ru"))}};
}

QVariantList AppController::mrThreadsOf(const QString& taskId) const {
  const QString url = mrUrlForTask(taskId);
  const auto it = m_threadMrs.constFind(url);
  if(url.isEmpty() || it == m_threadMrs.constEnd() || !it->open || !mrThreadsOn() || m_threadWatch == nullptr) {
    return {};
  }
  const QString me = threadMeFor(*it);
  const heap::git::MrThreadFacts facts = m_threadWatch->factsFor(url);
  if(me.isEmpty() || !facts.ok) {
    return {};
  }
  heap::git::MrBrief brief;
  brief.url = url;
  brief.author = it->author;
  brief.open = true;
  const heap::git::MrWaits w = heap::git::summarizeWaits(brief, facts, me);
  const bool ru = m_language == QStringLiteral("ru");
  const QDateTime now = QDateTime::currentDateTime();
  const auto nameOf = [this](const QString& handle) {
    const QString pid = personIdForHandle(handle);
    const int row = pid.isEmpty() ? -1 : m_people.indexOfId(pid);
    return row >= 0 ? m_people.items().at(row).name : handle;
  };
  QVariantList out;
  for(const heap::git::ThreadItem& item : w.items) {
    const heap::git::MrThread& t = item.thread;
    const heap::git::ThreadNote& last = t.notes.last();
    QDateTime mineAt;
    for(const heap::git::ThreadNote& n : t.notes) {
      if(n.author.compare(me, Qt::CaseInsensitive) == 0) {
        mineAt = n.at;
      }
    }
    const bool lastMine = last.author.compare(me, Qt::CaseInsensitive) == 0;
    out.append(QVariantMap{
        {QStringLiteral("wait"), item.verdict.wait == heap::git::ThreadWait::OnMe ? QStringLiteral("me") : QStringLiteral("them")},
        {QStringLiteral("reason"), item.verdict.reason},
        {QStringLiteral("place"),
         t.path.isEmpty() ? QString()
         : t.line > 0     ? t.path + QLatin1Char(':') + QString::number(t.line)
                          : t.path},
        {QStringLiteral("who"), lastMine ? QString() : nameOf(last.author)},
        {QStringLiteral("mine"), lastMine},
        {QStringLiteral("excerpt"), excerptOf(last.body)},
        {QStringLiteral("notes"), qMax(t.noteCount, static_cast<int>(t.notes.size()))},
        {QStringLiteral("mineAgo"), lastMine ? QString() : agoAtLeastMinute(mineAt, now, ru)},
        {QStringLiteral("ago"), heap::people::agoShort(item.verdict.since, now, ru)},
        {QStringLiteral("url"), item.verdict.url.isEmpty() ? last.url : item.verdict.url}});
  }
  return out;
}

bool AppController::openMrThread(const QString& url) {
  const QUrl u(url);
  // The link came from the forge's answer: only a web page is opened.
  if(!u.isValid() || u.host().isEmpty() || (u.scheme() != QLatin1String("https") && u.scheme() != QLatin1String("http"))) {
    return false;
  }
  m_lastOpenedThreadUrl = url;
  if(!QStandardPaths::isTestModeEnabled()) {
    QDesktopServices::openUrl(u);
  }
  return true;
}

bool AppController::setMrThreadsForTest(const QString& provider,
                                        const QString& url,
                                        const QString& title,
                                        const QString& author,
                                        const QStringList& reviewers,
                                        const QString& me,
                                        const QString& raw) {
  ThreadMr mr;
  mr.provider = provider;
  mr.url = url;
  mr.title = title;
  mr.author = author;
  mr.reviewers = reviewers;
  mr.me = me;
  mr.open = true;
  mr.createdAt = QDateTime::currentDateTime().addDays(-2);
  const qsizetype at = std::max(url.lastIndexOf(QLatin1Char('/')), qsizetype(0));
  mr.number = url.mid(at + 1).toInt();
  m_threadMrs.insert(url, mr);
  bool ok = false;
  const heap::git::MrThreadFacts facts = provider == QLatin1String("github") ? heap::git::parseGithubThreads(raw.toUtf8(), &ok)
                                                                             : heap::git::parseGitlabDiscussions(raw.toUtf8(), url, &ok);
  threadWatch()->setFactsForTests(url, facts, QStringLiteral("test"));
  return ok;
}

void AppController::setMrOpenForTest(const QString& url, bool open) {
  auto it = m_threadMrs.find(url);
  if(it == m_threadMrs.end()) {
    return;
  }
  it->open = open;
  ++m_mrThreadsRev;
  emit mrThreadsChanged();
  bumpMrRevision();
}

void AppController::clearMrThreadsForTest() {
  m_threadMrs.clear();
  if(m_threadWatch != nullptr) {
    m_threadWatch->clear();
  }
  ++m_mrThreadsRev;
  emit mrThreadsChanged();
}
