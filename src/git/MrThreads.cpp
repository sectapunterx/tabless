#include "git/MrThreads.h"
#include "net/FeatureGate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace heap::git {

namespace {

QString bare(const QString& handle) {
  QString s = handle.trimmed();
  while(s.startsWith(QLatin1Char('@'))) {
    s = s.mid(1);
  }
  return s;
}

bool same(const QString& a, const QString& b) {
  const QString x = bare(a);
  return !x.isEmpty() && x.compare(bare(b), Qt::CaseInsensitive) == 0;
}

QDateTime stamp(const QJsonValue& v) {
  QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
  if(!t.isValid()) {
    t = QDateTime::fromString(v.toString(), Qt::ISODate);
  }
  return t;
}

// Pages one after another ("[…]\n[…]"): each top-level array, in order.
QList<QJsonArray> splitPages(const QByteArray& pages) {
  QList<QJsonArray> out;
  int depth = 0;
  bool inString = false;
  qsizetype start = -1;
  for(qsizetype i = 0; i < pages.size(); ++i) {
    const char c = pages.at(i);
    if(inString) {
      if(c == '\\') {
        ++i;
      } else if(c == '"') {
        inString = false;
      }
      continue;
    }
    if(c == '"') {
      inString = true;
    } else if(c == '[' || c == '{') {
      if(depth++ == 0) {
        start = i;
      }
    } else if((c == ']' || c == '}') && --depth == 0 && start >= 0) {
      const QJsonDocument doc = QJsonDocument::fromJson(pages.mid(start, i - start + 1));
      if(doc.isArray()) {
        out << doc.array();
      }
      start = -1;
    }
  }
  return out;
}

// GitLab's system notes that matter here: a push, an approval, a review asked.
void readSystemNote(const QJsonObject& n, MrThreadFacts& f) {
  const QString body = n.value(QStringLiteral("body")).toString().trimmed();
  const QString who = n.value(QStringLiteral("author")).toObject().value(QStringLiteral("username")).toString();
  const QDateTime at = stamp(n.value(QStringLiteral("created_at")));
  static const QRegularExpression pushRx(QStringLiteral("^added \\d+ (new )?commits?"));
  if(pushRx.match(body).hasMatch()) {
    f.events.append({MrEvent::Push, who, at});
  } else if(body.startsWith(QStringLiteral("approved this merge request"))) {
    f.events.append({MrEvent::Approved, who, at});
  } else if(body.startsWith(QStringLiteral("requested review from"))) {
    static const QRegularExpression handleRx(QStringLiteral("@([\\w.\\-]+)"));
    auto it = handleRx.globalMatch(body);
    while(it.hasNext()) {
      f.events.append({MrEvent::ReviewRequested, it.next().captured(1), at});
    }
  }
}

ThreadNote githubNote(const QJsonObject& c) {
  ThreadNote n;
  n.id = QString::number(c.value(QStringLiteral("databaseId")).toVariant().toLongLong());
  n.author = c.value(QStringLiteral("author")).toObject().value(QStringLiteral("login")).toString();
  n.at = stamp(c.value(QStringLiteral("createdAt")));
  n.body = c.value(QStringLiteral("body")).toString();
  n.url = c.value(QStringLiteral("url")).toString();
  return n;
}

}  // namespace

MrThreadFacts parseGitlabDiscussions(const QByteArray& pages, const QString& mrUrl, bool* ok) {
  MrThreadFacts f;
  const QList<QJsonArray> arrays = splitPages(pages);
  const bool good = !arrays.isEmpty() || pages.trimmed().isEmpty();
  QString base = mrUrl;
  const qsizetype hash = base.indexOf(QLatin1Char('#'));
  if(hash >= 0) {
    base.truncate(hash);
  }
  for(const QJsonArray& arr : arrays) {
    for(const auto& dv : arr) {
      const QJsonObject d = dv.toObject();
      MrThread t;
      t.id = d.value(QStringLiteral("id")).toString();
      bool anyResolvable = false;
      bool allResolved = true;
      for(const auto& nv : d.value(QStringLiteral("notes")).toArray()) {
        const QJsonObject n = nv.toObject();
        if(n.value(QStringLiteral("system")).toBool()) {
          readSystemNote(n, f);
          continue;
        }
        if(n.value(QStringLiteral("resolvable")).toBool()) {
          anyResolvable = true;
          allResolved = allResolved && n.value(QStringLiteral("resolved")).toBool();
        }
        if(t.path.isEmpty()) {
          const QJsonObject pos = n.value(QStringLiteral("position")).toObject();
          if(!pos.isEmpty()) {
            t.path = pos.value(QStringLiteral("new_path")).toString();
            if(t.path.isEmpty()) {
              t.path = pos.value(QStringLiteral("old_path")).toString();
            }
            t.line = pos.value(QStringLiteral("new_line")).toInt();
            if(t.line <= 0) {
              t.line = pos.value(QStringLiteral("old_line")).toInt();
            }
          }
        }
        ThreadNote note;
        note.id = QString::number(n.value(QStringLiteral("id")).toVariant().toLongLong());
        note.author = n.value(QStringLiteral("author")).toObject().value(QStringLiteral("username")).toString();
        note.at = stamp(n.value(QStringLiteral("created_at")));
        note.body = n.value(QStringLiteral("body")).toString();
        note.url = base.isEmpty() ? QString() : base + QStringLiteral("#note_") + note.id;
        t.notes.append(note);
      }
      if(t.notes.isEmpty()) {
        continue;  // system notes only
      }
      t.resolvable = anyResolvable;
      t.resolved = anyResolvable && allResolved;
      t.noteCount = static_cast<int>(t.notes.size());
      t.starter = t.notes.first().author;
      f.threads.append(t);
    }
  }
  f.ok = good;
  if(ok != nullptr) {
    *ok = good;
  }
  return f;
}

QByteArray githubThreadsQuery(const QString& owner, const QString& repo, int number) {
  // Read only: review threads (resolved or not, the file and line, who
  // started it, the last 20 words), the general conversation's last 50, and
  // the timeline's review requests, reviews and commits.
  static const QString kQuery = QStringLiteral(
      "query($owner:String!,$name:String!,$number:Int!){viewer{login}"
      "repository(owner:$owner,name:$name){pullRequest(number:$number){"
      "reviewThreads(first:100){nodes{id isResolved path line "
      "head:comments(first:1){nodes{author{login}}} "
      "comments(last:20){totalCount nodes{databaseId author{login} body createdAt url}}}}"
      "comments(last:50){totalCount nodes{databaseId author{login} body createdAt url}}"
      "timelineItems(last:50,itemTypes:[REVIEW_REQUESTED_EVENT,PULL_REQUEST_REVIEW,PULL_REQUEST_COMMIT]){nodes{__typename "
      "... on ReviewRequestedEvent{createdAt requestedReviewer{... on User{login}}} "
      "... on PullRequestReview{author{login} submittedAt state} "
      "... on PullRequestCommit{commit{committedDate}}}}}}}");
  QJsonObject vars{{QStringLiteral("owner"), owner}, {QStringLiteral("name"), repo}, {QStringLiteral("number"), number}};
  return QJsonDocument(QJsonObject{{QStringLiteral("query"), kQuery}, {QStringLiteral("variables"), vars}}).toJson(QJsonDocument::Compact);
}

MrThreadFacts parseGithubThreads(const QByteArray& raw, bool* ok) {
  MrThreadFacts f;
  const QJsonObject root = QJsonDocument::fromJson(raw).object();
  const QJsonObject data = root.value(QStringLiteral("data")).toObject();
  const QJsonObject pr = data.value(QStringLiteral("repository")).toObject().value(QStringLiteral("pullRequest")).toObject();
  const bool good = !pr.isEmpty();
  if(ok != nullptr) {
    *ok = good;
  }
  if(!good) {
    return f;
  }
  f.viewer = data.value(QStringLiteral("viewer")).toObject().value(QStringLiteral("login")).toString();
  for(const auto& tv : pr.value(QStringLiteral("reviewThreads")).toObject().value(QStringLiteral("nodes")).toArray()) {
    const QJsonObject o = tv.toObject();
    MrThread t;
    t.id = o.value(QStringLiteral("id")).toString();
    t.resolvable = true;
    t.resolved = o.value(QStringLiteral("isResolved")).toBool();
    t.path = o.value(QStringLiteral("path")).toString();
    t.line = o.value(QStringLiteral("line")).toInt();
    const QJsonArray head = o.value(QStringLiteral("head")).toObject().value(QStringLiteral("nodes")).toArray();
    if(!head.isEmpty()) {
      t.starter = head.first().toObject().value(QStringLiteral("author")).toObject().value(QStringLiteral("login")).toString();
    }
    const QJsonObject comments = o.value(QStringLiteral("comments")).toObject();
    for(const auto& cv : comments.value(QStringLiteral("nodes")).toArray()) {
      t.notes.append(githubNote(cv.toObject()));
    }
    t.noteCount = qMax(static_cast<int>(t.notes.size()), comments.value(QStringLiteral("totalCount")).toInt());
    if(!t.notes.isEmpty()) {
      f.threads.append(t);
    }
  }
  // The conversation: one stream of general notes.
  MrThread general;
  general.id = QStringLiteral("conversation");
  const QJsonObject conv = pr.value(QStringLiteral("comments")).toObject();
  for(const auto& cv : conv.value(QStringLiteral("nodes")).toArray()) {
    general.notes.append(githubNote(cv.toObject()));
  }
  general.noteCount = qMax(static_cast<int>(general.notes.size()), conv.value(QStringLiteral("totalCount")).toInt());
  if(!general.notes.isEmpty()) {
    general.starter = general.notes.first().author;
    f.threads.append(general);
  }
  for(const auto& ev : pr.value(QStringLiteral("timelineItems")).toObject().value(QStringLiteral("nodes")).toArray()) {
    const QJsonObject e = ev.toObject();
    const QString type = e.value(QStringLiteral("__typename")).toString();
    if(type == QLatin1String("ReviewRequestedEvent")) {
      const QString who = e.value(QStringLiteral("requestedReviewer")).toObject().value(QStringLiteral("login")).toString();
      if(!who.isEmpty()) {
        f.events.append({MrEvent::ReviewRequested, who, stamp(e.value(QStringLiteral("createdAt")))});
      }
    } else if(type == QLatin1String("PullRequestReview")) {
      const QString who = e.value(QStringLiteral("author")).toObject().value(QStringLiteral("login")).toString();
      const bool approved = e.value(QStringLiteral("state")).toString() == QLatin1String("APPROVED");
      f.events.append({approved ? MrEvent::Approved : MrEvent::Reviewed, who, stamp(e.value(QStringLiteral("submittedAt")))});
    } else if(type == QLatin1String("PullRequestCommit")) {
      f.events.append(
          {MrEvent::Push, QString(), stamp(e.value(QStringLiteral("commit")).toObject().value(QStringLiteral("committedDate")))});
    }
  }
  f.ok = true;
  return f;
}

bool mentions(const QString& body, const QString& login) {
  const QString me = bare(login);
  if(me.isEmpty()) {
    return false;
  }
  const QRegularExpression rx(QStringLiteral("(^|[^\\w@])@") + QRegularExpression::escape(me) + QStringLiteral("(?![\\w-])"),
                              QRegularExpression::CaseInsensitiveOption);
  return rx.match(body).hasMatch();
}

ThreadVerdict judgeThread(const MrThread& t, const QString& me, const QString& mrAuthor, const QDateTime& lastMineAt) {
  ThreadVerdict v;
  if(bare(me).isEmpty() || t.notes.isEmpty() || t.resolved) {
    return v;
  }
  int myLast = -1;
  bool mentioned = false;
  for(int i = 0; i < t.notes.size(); ++i) {
    const ThreadNote& n = t.notes.at(i);
    if(same(n.author, me)) {
      myLast = i;
    } else if(mentions(n.body, me)) {
      mentioned = true;
    }
  }
  if(!t.resolvable) {
    // A general note waits only for an answer to an @mention: the first
    // one after my last word anywhere on the request.
    for(int i = myLast + 1; i < t.notes.size(); ++i) {
      const ThreadNote& n = t.notes.at(i);
      if(same(n.author, me) || !mentions(n.body, me) || (lastMineAt.isValid() && n.at.isValid() && n.at <= lastMineAt)) {
        continue;
      }
      v.wait = ThreadWait::OnMe;
      v.reason = QStringLiteral("mention");
      v.since = n.at;
      v.who = n.author;
      v.url = n.url;
      return v;
    }
    return v;
  }
  const ThreadNote& last = t.notes.last();
  if(same(last.author, me)) {
    v.wait = ThreadWait::OnThem;
    v.since = last.at;
    v.url = last.url;
    return v;
  }
  const bool wrote = myLast >= 0 || same(t.starter, me);
  const bool author = same(mrAuthor, me);
  if(!wrote && !author && !mentioned) {
    return v;  // not my conversation
  }
  const ThreadNote& first = t.notes.at(myLast + 1);
  v.wait = ThreadWait::OnMe;
  v.reason = wrote ? QStringLiteral("replied") : mentioned && !author ? QStringLiteral("mention") : QStringLiteral("started");
  v.since = first.at;
  v.who = last.author;
  v.url = first.url;
  return v;
}

MrWaits summarizeWaits(const MrBrief& mr, const MrThreadFacts& facts, const QString& me) {
  MrWaits w;
  if(!mr.open || bare(me).isEmpty()) {
    return w;
  }
  QDateTime lastMine;
  for(const MrThread& t : facts.threads) {
    for(const ThreadNote& n : t.notes) {
      if(same(n.author, me) && n.at.isValid() && (!lastMine.isValid() || n.at > lastMine)) {
        lastMine = n.at;
      }
    }
  }
  QDateTime lastPush;
  QDateTime askedAt;
  QDateTime gaveAt;  // my approval or review
  for(const MrEvent& e : facts.events) {
    if(!e.at.isValid()) {
      continue;
    }
    if(e.kind == MrEvent::Push && (!lastPush.isValid() || e.at > lastPush)) {
      lastPush = e.at;
    } else if(e.kind == MrEvent::ReviewRequested && same(e.who, me) && (!askedAt.isValid() || e.at > askedAt)) {
      askedAt = e.at;
    } else if((e.kind == MrEvent::Approved || e.kind == MrEvent::Reviewed) && same(e.who, me) && (!gaveAt.isValid() || e.at > gaveAt)) {
      gaveAt = e.at;
    }
  }

  QList<ThreadItem> onMe;
  QList<ThreadItem> onThem;
  for(const MrThread& t : facts.threads) {
    const ThreadVerdict v = judgeThread(t, me, mr.author, lastMine);
    if(v.wait == ThreadWait::OnMe) {
      onMe.append({t, v});
    } else if(v.wait == ThreadWait::OnThem) {
      onThem.append({t, v});
    }
  }
  const auto older = [](const ThreadItem& a, const ThreadItem& b) {
    if(a.verdict.since.isValid() != b.verdict.since.isValid()) {
      return a.verdict.since.isValid();
    }
    if(a.verdict.since != b.verdict.since) {
      return a.verdict.since < b.verdict.since;
    }
    return a.thread.id < b.thread.id;
  };
  std::stable_sort(onMe.begin(), onMe.end(), older);
  std::stable_sort(onThem.begin(), onThem.end(), older);
  w.items = onMe + onThem;
  w.onMe = static_cast<int>(onMe.size());

  // A review asked of me (never of my own request) and not given since the
  // last push — or since it was asked, whichever is later.
  bool asked = false;
  for(const QString& r : mr.reviewers) {
    asked = asked || same(r, me);
  }
  if(asked && !same(mr.author, me)) {
    QDateTime from = lastPush;
    if(askedAt.isValid() && (!from.isValid() || askedAt > from)) {
      from = askedAt;
    }
    QDateTime gave = gaveAt;
    if(!mr.reviewersPending && lastMine.isValid() && (!gave.isValid() || lastMine > gave)) {
      gave = lastMine;
    }
    w.review = !gave.isValid() || (from.isValid() && gave < from);
    w.reviewSince = from.isValid() ? from : mr.createdAt;
  }

  if(w.onMe >= 2) {
    w.reason = QStringLiteral("threads");
    w.since = onMe.first().verdict.since;
    w.who = onMe.first().verdict.who;
    w.url = onMe.first().verdict.url;
  } else if(w.onMe == 1) {
    w.reason = onMe.first().verdict.reason;
    w.since = onMe.first().verdict.since;
    w.who = onMe.first().verdict.who;
    w.url = onMe.first().verdict.url;
  } else if(w.review) {
    w.reason = QStringLiteral("review");
    w.since = w.reviewSince;
    w.url = mr.url;
  }
  // The row waits as long as its oldest reason has.
  if(w.review && w.reviewSince.isValid() && (!w.since.isValid() || w.reviewSince < w.since)) {
    w.since = w.reviewSince;
  }
  if(w.url.isEmpty()) {
    w.url = mr.url;
  }
  return w;
}

bool waitsLonger(const MrWaits& a, const QString& aUrl, const MrWaits& b, const QString& bUrl) {
  if(a.since.isValid() != b.since.isValid()) {
    return a.since.isValid();
  }
  if(a.since != b.since) {
    return a.since < b.since;
  }
  return aUrl < bUrl;
}

QString threadStamp(const QDateTime& updatedAt, int noteCount) {
  const QString when = updatedAt.isValid() ? updatedAt.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral("-");
  return when + QLatin1Char('|') + QString::number(noteCount);
}

// ---- MrThreadWatch ----

MrThreadWatch::MrThreadWatch(ForgeApi* api, QObject* parent) : QObject(parent), m_api(api) {
}

void MrThreadWatch::clear() {
  if(m_entries.isEmpty()) {
    return;
  }
  m_entries.clear();
  emit changed();
}

void MrThreadWatch::setFactsForTests(const QString& url, const MrThreadFacts& facts, const QString& stamp) {
  Entry& e = m_entries[url];
  e.facts = facts;
  e.readStamp = stamp;
  e.wantStamp = stamp;
  emit changed();
}

void MrThreadWatch::sync(const QList<WatchedMr>& mrs) {
  QSet<QString> keep;
  QList<const WatchedMr*> due;
  for(const WatchedMr& mr : mrs) {
    if(!mr.open || mr.url.isEmpty()) {
      continue;  // closed or merged: forgotten, never read
    }
    keep.insert(mr.url);
    // Read only on a forge I know who I am on.
    if(bare(mr.me).isEmpty() || !mr.auth.valid() || mr.number <= 0 || !mr.project.contains(QLatin1Char('/'))) {
      continue;
    }
    Entry& e = m_entries[mr.url];
    e.wantStamp = mr.stamp;
    if(!e.reading && threadsNeedRead(e.readStamp, mr.stamp)) {
      due.append(&mr);
    }
  }
  bool dropped = false;
  for(auto it = m_entries.begin(); it != m_entries.end();) {
    if(!keep.contains(it.key())) {
      it = m_entries.erase(it);
      dropped = true;
    } else {
      ++it;
    }
  }
  if(dropped) {
    emit changed();
  }
  int budget = kReadsPerSync;
  for(const WatchedMr* mr : std::as_const(due)) {
    if(budget-- <= 0) {
      break;
    }
    read(*mr);
  }
}

void MrThreadWatch::read(const WatchedMr& mr) {
  if(m_api == nullptr) {
    return;
  }
  m_entries[mr.url].reading = true;
  ++m_reads;
  QPointer<MrThreadWatch> self(this);
  const QString url = mr.url;
  const QString stamp = mr.stamp;
  if(mr.auth.glab) {
    // Concatenated, not .arg()-chained: the encoded path holds "%2F", which a
    // second .arg() would take for its own placeholder.
    const QString path = QStringLiteral("/projects/") + QString::fromUtf8(QUrl::toPercentEncoding(mr.project)) +
                         QStringLiteral("/merge_requests/") + QString::number(mr.number) + QStringLiteral("/discussions?per_page=100");
    m_api->getAllPages(
        mr.auth.withFeature(heap::net::features::mrThreads()),
        path,
        [self, url, stamp](const ForgeApi::Reply& last, const QByteArray& pages) {
          if(!self) {
            return;
          }
          bool ok = false;
          const MrThreadFacts f = last.ok() ? parseGitlabDiscussions(pages, url, &ok) : MrThreadFacts{};
          self->finish(url, stamp, f, ok ? 200 : (last.ok() ? 422 : last.status), last.held);
        },
        10);
    return;
  }
  const QString owner = mr.project.section(QLatin1Char('/'), 0, 0);
  const QString repo = mr.project.section(QLatin1Char('/'), 1, 1);
  m_api->graphql(mr.auth.withFeature(heap::net::features::mrThreads()),
                 githubThreadsQuery(owner, repo, mr.number),
                 [self, url, stamp](const ForgeApi::Reply& r) {
                   if(!self) {
                     return;
                   }
                   bool ok = false;
                   const MrThreadFacts f = r.ok() ? parseGithubThreads(r.body, &ok) : MrThreadFacts{};
                   self->finish(url, stamp, f, ok ? 200 : (r.ok() ? 422 : r.status), r.held);
                 });
}

void MrThreadWatch::finish(const QString& url, const QString& stamp, const MrThreadFacts& facts, int status, bool held) {
  auto it = m_entries.find(url);
  if(it == m_entries.end()) {
    return;  // closed or gone meanwhile
  }
  it->reading = false;
  if(facts.ok) {
    it->facts = facts;
    it->readStamp = stamp;
    emit changed();
    return;
  }
  // The server failing (or held by the gate's back-off): asked again only
  // when the gate lets it. Anything else (no access, not found, an answer
  // that is not threads) is not asked again until the request moves.
  if(held || ForgeApi::isFailure(status)) {
    return;
  }
  it->readStamp = stamp;
  if(it->facts.ok) {
    it->facts = MrThreadFacts{};
    emit changed();
  }
}

}  // namespace heap::git
