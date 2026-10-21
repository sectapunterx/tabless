#include "git/ForgeApi.h"
#include "git/PrFacts.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <memory>
#include <utility>

// CMake injects it into heap_core; a test target building this file alone
// gets the fallback.
#ifndef HEAP_VERSION
#define HEAP_VERSION "0.0.0-dev"
#endif

namespace heap::git {

namespace {

QString enc(const QString& s) {
  return QString::fromUtf8(QUrl::toPercentEncoding(s));
}

// "/projects/group%2Fapp" or "/repos/o/r".
QString projectBase(const ForgeAuth& auth, const QString& project) {
  return auth.glab ? QStringLiteral("/projects/") + enc(project) : QStringLiteral("/repos/") + project;
}

bool isPublicForge(const QString& host) {
  return host == QLatin1String("github.com") || host == QLatin1String("gitlab.com");
}

}  // namespace

QString forgeHostOf(const QString& urlOrHost) {
  QString s = urlOrHost.trimmed();
  if(s.isEmpty()) {
    return {};
  }
  if(!s.contains(QStringLiteral("://"))) {
    s.prepend(QStringLiteral("https://"));
  }
  QString host = QUrl(s).host().toLower();
  if(host == QLatin1String("api.github.com") || host == QLatin1String("www.github.com")) {
    host = QStringLiteral("github.com");
  }
  return host;
}

QString forgeApiBase(bool glab, const QString& webBase) {
  QString base = webBase.trimmed();
  while(base.endsWith(QLatin1Char('/'))) {
    base.chop(1);
  }
  if(base.isEmpty()) {
    return glab ? QStringLiteral("https://gitlab.com/api/v4") : QStringLiteral("https://api.github.com");
  }
  if(!base.contains(QStringLiteral("://"))) {
    base.prepend(QStringLiteral("https://"));
  }
  if(glab) {
    return base.endsWith(QStringLiteral("/api/v4")) ? base : base + QStringLiteral("/api/v4");
  }
  if(forgeHostOf(base) == QLatin1String("github.com")) {
    return QStringLiteral("https://api.github.com");
  }
  return base.endsWith(QStringLiteral("/api/v3")) ? base : base + QStringLiteral("/api/v3");
}

bool forgeHostMatches(const ForgeAuth& auth, const QString& repoHost) {
  const QString h = forgeHostOf(repoHost);
  return !h.isEmpty() && h == forgeHostOf(auth.host);
}

QList<int> forgeAuthCandidates(const QList<ForgeAuth>& auths, const QString& repoHost) {
  QList<int> matched;
  for(int i = 0; i < auths.size(); ++i) {
    if(auths.at(i).valid() && forgeHostMatches(auths.at(i), repoHost)) {
      matched << i;
    }
  }
  const QString host = forgeHostOf(repoHost);
  if(!matched.isEmpty() || host.isEmpty() || isPublicForge(host)) {
    return matched;
  }
  // An alias: the one whose kind the name hints at first ("gitlab" → GitLab).
  QList<int> likely;
  QList<int> rest;
  const bool saysLab = host.contains(QStringLiteral("gitlab")) || host.contains(QStringLiteral("lab"));
  const bool saysHub = host.contains(QStringLiteral("github")) || host.contains(QStringLiteral("hub"));
  for(int i = 0; i < auths.size(); ++i) {
    if(!auths.at(i).valid()) {
      continue;
    }
    const bool hinted = auths.at(i).glab ? saysLab : saysHub;
    (hinted ? likely : rest) << i;
  }
  return likely + rest;
}

ForgePrRef parseForgePrUrl(const QString& url) {
  ForgePrRef r;
  const QUrl u(url.trimmed());
  if(!u.isValid() || u.host().isEmpty()) {
    return r;
  }
  QString path = u.path();
  static const QRegularExpression glabRx(QStringLiteral("^/(.+?)/-/merge_requests/(\\d+)"));
  static const QRegularExpression ghRx(QStringLiteral("^/([^/]+/[^/]+)/pull/(\\d+)"));
  if(const auto m = glabRx.match(path); m.hasMatch()) {
    r.glab = true;
    r.project = m.captured(1);
    r.number = m.captured(2).toInt();
  } else if(const auto g = ghRx.match(path); g.hasMatch()) {
    r.project = g.captured(1);
    r.number = g.captured(2).toInt();
  } else {
    return ForgePrRef{};
  }
  r.host = forgeHostOf(u.host());
  return r;
}

PrInfo parseGithubRestPull(const QJsonObject& o) {
  PrInfo info;
  if(o.isEmpty()) {
    return info;
  }
  const QString state = o.value(QStringLiteral("state")).toString().toLower();
  const bool merged = o.value(QStringLiteral("merged")).toBool() || o.value(QStringLiteral("merged_at")).isString();
  info.state = merged ? QStringLiteral("merged") : state;
  info.number = o.value(QStringLiteral("number")).toInt();
  info.url = o.value(QStringLiteral("html_url")).toString();
  info.title = o.value(QStringLiteral("title")).toString();
  info.baseRef = o.value(QStringLiteral("base")).toObject().value(QStringLiteral("ref")).toString();
  info.headSha = o.value(QStringLiteral("head")).toObject().value(QStringLiteral("sha")).toString();
  info.draft = o.value(QStringLiteral("draft")).toBool();
  info.author = o.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")).toString();
  for(const auto& v : o.value(QStringLiteral("requested_reviewers")).toArray()) {
    const QString login = v.toObject().value(QStringLiteral("login")).toString();
    if(!login.isEmpty()) {
      info.reviewRequests << login;
    }
  }
  for(const auto& v : o.value(QStringLiteral("requested_teams")).toArray()) {
    const QString slug = v.toObject().value(QStringLiteral("slug")).toString();
    if(!slug.isEmpty()) {
      info.reviewRequests << slug;
    }
  }
  const QJsonValue mergeable = o.value(QStringLiteral("mergeable"));
  if(mergeable.isBool()) {
    info.mergeable = mergeable.toBool() ? QStringLiteral("MERGEABLE") : QStringLiteral("CONFLICTING");
  }
  return info;
}

PrInfo parseForgePrList(const QByteArray& raw, bool glab) {
  const QJsonDocument doc = QJsonDocument::fromJson(raw);
  const QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray{};
  if(arr.isEmpty()) {
    return {};
  }
  const QJsonObject first = arr.first().toObject();
  if(glab) {
    return parsePrJson(QJsonDocument(first).toJson(QJsonDocument::Compact), true);
  }
  return parseGithubRestPull(first);
}

QString parseForgeLogin(const QByteArray& raw) {
  const QJsonObject o = QJsonDocument::fromJson(raw).object();
  const QString login = o.value(QStringLiteral("login")).toString();
  return login.isEmpty() ? o.value(QStringLiteral("username")).toString() : login;
}

QStringList parseGithubApprovedBy(const QByteArray& pages, QString* decision) {
  // Pages one after another ("[…][…]"): split at the top-level brackets.
  QHash<QString, QString> latest;
  QStringList order;
  int depth = 0;
  bool inString = false;
  qsizetype start = -1;
  const auto take = [&](const QByteArray& page) {
    for(const auto& v : QJsonDocument::fromJson(page).array()) {
      const QJsonObject r = v.toObject();
      const QString who = r.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")).toString();
      const QString state = r.value(QStringLiteral("state")).toString().toUpper();
      // A comment or a pending draft says nothing about approval.
      if(who.isEmpty() || state == QLatin1String("COMMENTED") || state == QLatin1String("PENDING")) {
        continue;
      }
      if(!latest.contains(who)) {
        order << who;
      }
      latest.insert(who, state);
    }
  };
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
      take(pages.mid(start, i - start + 1));
      start = -1;
    }
  }
  QStringList approved;
  bool changes = false;
  for(const QString& who : std::as_const(order)) {
    const QString s = latest.value(who);
    if(s == QLatin1String("APPROVED")) {
      approved << who;
    } else if(s == QLatin1String("CHANGES_REQUESTED")) {
      changes = true;
    }
  }
  if(decision != nullptr) {
    *decision = changes ? QStringLiteral("CHANGES_REQUESTED") : !approved.isEmpty() ? QStringLiteral("APPROVED") : QString();
  }
  return approved;
}

QString nextLinkUrl(const QByteArray& linkHeader) {
  // <https://…?page=2>; rel="next", <…>; rel="last"
  for(const QByteArray& part : linkHeader.split(',')) {
    const qsizetype lt = part.indexOf('<');
    const qsizetype gt = part.indexOf('>');
    if(lt >= 0 && gt > lt && part.mid(gt).contains("rel=\"next\"")) {
      return QString::fromUtf8(part.mid(lt + 1, gt - lt - 1));
    }
  }
  return {};
}

QVariantMap ownersReportVariant(const CodeOwnersReport& report, const PrInfo& pr, bool glab, int fileCount) {
  QVariantMap v = report.toVariant();
  v[QStringLiteral("prNumber")] = pr.number;
  v[QStringLiteral("prUrl")] = pr.url;
  v[QStringLiteral("prTitle")] = pr.title;
  v[QStringLiteral("flavor")] = glab ? QStringLiteral("gitlab") : QStringLiteral("github");
  v[QStringLiteral("fileCount")] = fileCount;
  v[QStringLiteral("baseRef")] = pr.baseRef;
  return v;
}

QVariantMap ownersReasonVariant(const QString& reason, const QString& host, bool glab, const PrInfo& pr) {
  return QVariantMap{{QStringLiteral("reason"), reason},
                     {QStringLiteral("host"), host},
                     {QStringLiteral("flavor"), glab ? QStringLiteral("gitlab") : QStringLiteral("github")},
                     {QStringLiteral("prNumber"), pr.number},
                     {QStringLiteral("prUrl"), pr.url},
                     {QStringLiteral("prTitle"), pr.title},
                     {QStringLiteral("baseRef"), pr.baseRef}};
}

QString ownersReasonForStatus(int status) {
  if(status >= 200 && status < 300) {
    return {};
  }
  if(status == 0) {
    return QStringLiteral("unreachable");
  }
  // The server answered with its own failure (or asked to slow down): asked
  // again only after the gate's back-off, never on a timer or a focus alone.
  if(status == 429 || status >= 500) {
    return QStringLiteral("serverError");
  }
  // 401/403: the token is refused; 404: the token cannot see the project.
  return QStringLiteral("denied");
}

// ---- ForgeApi ----

ForgeApi::ForgeApi(QObject* parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)) {
}

ForgeApi::~ForgeApi() = default;

// ---- The gate every request goes through (see ForgeApi.h, Politeness) ----

namespace {

struct PathState {
  int failures = 0;
  qint64 retryAt = 0;
  int lastStatus = 0;
};

struct HostState {
  int inFlight = 0;
  QList<qint64> failures;  // when, within the trip window
  qint64 pausedUntil = 0;
  int pauseStatus = 0;
  int trips = 0;
  qint64 lastSentMono = -1;
  bool drainScheduled = false;
  QStringList queue;  // call keys, oldest first
  int sent = 0;
};

struct Call {
  QString hostKey;
  QString url;
  QByteArray accept;
  ForgeAuth auth;
  QList<QPair<QPointer<ForgeApi>, ForgeApi::Done>> waiters;
};

struct Gate {
  QHash<QString, PathState> paths;  // by URL
  QHash<QString, HostState> hosts;  // by API base
  QHash<QString, Call> calls;       // waiting or on the wire, by URL + Accept
  // apiBase|MR path|head sha → the status the MR's file list failed with:
  // not asked again until the MR has another head commit.
  QHash<QString, int> filesFailed;
  std::function<qint64()> clock;
  QElapsedTimer mono;
};

Gate& gate() {
  static Gate g;
  if(!g.mono.isValid()) {
    g.mono.start();
  }
  return g;
}

qint64 nowMs() {
  const Gate& g = gate();
  return g.clock ? g.clock() : QDateTime::currentMSecsSinceEpoch();
}

void hold(QObject* ctx, const ForgeApi::Done& done, int status) {
  QTimer::singleShot(0, ctx, [done, status]() {
    ForgeApi::Reply r;
    r.status = status;
    r.held = true;
    done(r);
  });
}

}  // namespace

bool ForgeApi::isFailure(int status) {
  return status == 0 || status == 429 || status >= 500;
}

qint64 ForgeApi::backoffMs(int failures, int retryAfterSec) {
  const int n = qBound(1, failures, 16);
  qint64 ms = kFirstBackoffMs << (n - 1);
  ms = qMin(ms, kMaxBackoffMs);
  if(retryAfterSec > 0) {
    ms = qMax(ms, qMin(static_cast<qint64>(retryAfterSec) * 1000, kMaxBackoffMs));
  }
  return ms;
}

void ForgeApi::setClockForTests(std::function<qint64()> now) {
  gate().clock = std::move(now);
}

void ForgeApi::resetPolitenessForTests() {
  Gate& g = gate();
  g.paths.clear();
  g.hosts.clear();
  g.calls.clear();
  g.filesFailed.clear();
}

int ForgeApi::sentTo(const QString& apiBase) {
  return gate().hosts.value(apiBase).sent;
}

void ForgeApi::get(const ForgeAuth& auth, const QString& path, const Done& done, const QByteArray& accept) {
  if(!auth.valid()) {
    done(Reply{});
    return;
  }
  // `path` may already be a full URL (a Link header's next page): it has to
  // stay on this API, or the token would go with it somewhere else.
  QString url = path.startsWith(QStringLiteral("http")) ? path : auth.apiBase + path;
  if(!url.startsWith(auth.apiBase)) {
    done(Reply{});
    return;
  }
  Gate& g = gate();
  const qint64 now = nowMs();
  const auto ps = g.paths.constFind(url);
  if(ps != g.paths.constEnd() && now < ps->retryAt) {
    hold(this, done, ps->lastStatus);  // failed lately: not asked again yet
    return;
  }
  HostState& hs = g.hosts[auth.apiBase];
  if(now < hs.pausedUntil) {
    hold(this, done, hs.pauseStatus);
    return;
  }
  const QString key = url + QLatin1Char('\n') + QString::fromLatin1(accept);
  auto existing = g.calls.find(key);
  if(existing != g.calls.end()) {
    existing->waiters.append({QPointer<ForgeApi>(this), done});  // already on its way
    return;
  }
  Call call;
  call.hostKey = auth.apiBase;
  call.url = url;
  call.accept = accept;
  call.auth = auth;
  call.waiters.append({QPointer<ForgeApi>(this), done});
  g.calls.insert(key, call);
  hs.queue.append(key);
  drain(auth.apiBase);
}

void ForgeApi::drain(const QString& hostKey) {
  Gate& g = gate();
  HostState& hs = g.hosts[hostKey];
  while(hs.inFlight < kMaxInFlightPerHost && !hs.queue.isEmpty()) {
    const QString key = hs.queue.first();
    auto it = g.calls.find(key);
    if(it == g.calls.end()) {
      hs.queue.removeFirst();
      continue;
    }
    // Paused while it waited: everyone waiting hears so, nothing is sent.
    if(nowMs() < hs.pausedUntil) {
      hs.queue.removeFirst();
      const Call call = g.calls.take(key);
      for(const auto& w : call.waiters) {
        if(w.first) {
          hold(w.first, w.second, hs.pauseStatus);
        }
      }
      continue;
    }
    const qint64 mono = g.mono.elapsed();
    if(hs.lastSentMono >= 0 && mono - hs.lastSentMono < kMinGapMs) {
      if(!hs.drainScheduled) {
        hs.drainScheduled = true;
        QTimer::singleShot(kMinGapMs - (mono - hs.lastSentMono), this, [this, hostKey]() {
          gate().hosts[hostKey].drainScheduled = false;
          drain(hostKey);
        });
      }
      return;
    }
    ForgeApi* sender = nullptr;
    for(const auto& w : std::as_const(it->waiters)) {
      if(w.first) {
        sender = w.first;
        break;
      }
    }
    hs.queue.removeFirst();
    if(sender == nullptr) {
      g.calls.erase(it);  // everyone who asked is gone
      continue;
    }
    ++hs.inFlight;
    ++hs.sent;
    hs.lastSentMono = mono;
    sender->send(Pending{it->auth, it->url, it->accept});
  }
}

void ForgeApi::send(const Pending& p) {
  QNetworkRequest req{QUrl(p.url)};
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  req.setTransferTimeout(m_timeoutMs);
  // Who is asking, by name and version, for whoever reads the server's log.
  req.setRawHeader("User-Agent", QByteArrayLiteral("Tabless/") + QByteArrayLiteral(HEAP_VERSION) + QByteArrayLiteral(" (read-only)"));
  for(const auto& h : p.auth.headers) {
    req.setRawHeader(h.first, h.second);
  }
  if(!p.accept.isEmpty()) {
    req.setRawHeader("Accept", p.accept);
  }
  ++m_requests;
  QNetworkReply* reply = m_nam->get(req);  // the only verb this class has
  const QString key = p.url + QLatin1Char('\n') + QString::fromLatin1(p.accept);
  const QString hostKey = p.auth.apiBase;
  const QString url = p.url;
  // A reply torn down unanswered (its ForgeApi destroyed mid-flight) must not
  // leave the call behind: a later identical GET would wait on it forever.
  auto answered = std::make_shared<bool>(false);
  connect(reply, &QObject::destroyed, [answered, key, hostKey]() {
    if(*answered) {
      return;
    }
    Gate& g = gate();
    HostState& hs = g.hosts[hostKey];
    hs.inFlight = qMax(0, hs.inFlight - 1);
    const Call call = g.calls.take(key);
    for(const auto& w : call.waiters) {
      if(w.first) {
        hold(w.first, w.second, 0);
      }
    }
  });
  // The gate, not this object, owns the call: answered even if the asking
  // ForgeApi is gone by then (its waiters are skipped).
  connect(reply, &QNetworkReply::finished, reply, [reply, key, hostKey, url, answered]() {
    *answered = true;
    reply->deleteLater();
    Reply r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.body = reply->readAll();
    r.link = reply->rawHeader("Link");
    r.nextPage = reply->rawHeader("X-Next-Page").trimmed();
    r.retryAfter = reply->rawHeader("Retry-After").trimmed();

    Gate& g = gate();
    HostState& hs = g.hosts[hostKey];
    hs.inFlight = qMax(0, hs.inFlight - 1);
    const qint64 now = nowMs();
    if(isFailure(r.status)) {
      bool numeric = false;
      const int retryAfter = r.retryAfter.toInt(&numeric);
      PathState& ps = g.paths[url];
      ++ps.failures;
      ps.lastStatus = r.status;
      ps.retryAt = now + backoffMs(ps.failures, numeric ? retryAfter : -1);
      hs.failures.append(now);
      while(!hs.failures.isEmpty() && now - hs.failures.first() > kHostTripWindowMs) {
        hs.failures.removeFirst();
      }
      if(hs.failures.size() >= kHostTripFailures) {
        ++hs.trips;
        hs.pausedUntil = now + qMin(kHostPauseMs << qMin(hs.trips - 1, 8), kMaxHostPauseMs);
        hs.pauseStatus = r.status;
        hs.failures.clear();
      }
    } else {
      g.paths.remove(url);
      if(r.ok()) {
        hs.trips = 0;
      }
    }
    const Call call = g.calls.take(key);
    for(const auto& w : call.waiters) {
      if(w.first) {
        w.second(r);
      }
    }
    // Whoever is still alive carries the queue on.
    for(const auto& w : call.waiters) {
      if(w.first) {
        w.first->drain(hostKey);
        return;
      }
    }
    for(auto it = g.calls.constBegin(); it != g.calls.constEnd(); ++it) {
      if(it->hostKey != hostKey) {
        continue;
      }
      for(const auto& w : it->waiters) {
        if(w.first) {
          w.first->drain(hostKey);
          return;
        }
      }
    }
  });
}

void ForgeApi::getAllPages(const ForgeAuth& auth,
                           const QString& firstPath,
                           const std::function<void(const Reply&, const QByteArray&)>& done,
                           int maxPages) {
  pageStep(auth, firstPath, std::make_shared<QByteArray>(), 0, maxPages, done);
}

void ForgeApi::pageStep(const ForgeAuth& auth,
                        const QString& path,
                        const std::shared_ptr<QByteArray>& pages,
                        int page,
                        int maxPages,
                        const std::function<void(const Reply&, const QByteArray&)>& done) {
  get(auth, path, [this, auth, path, pages, page, maxPages, done](const Reply& r) {
    if(!r.ok()) {
      done(r, *pages);
      return;
    }
    if(!pages->isEmpty()) {
      *pages += '\n';
    }
    *pages += r.body;
    QString next = nextLinkUrl(r.link);
    if(next.isEmpty() && !r.nextPage.isEmpty()) {
      // A GitLab page may say only the number of the next one.
      QUrl u(path.startsWith(QStringLiteral("http")) ? path : auth.apiBase + path);
      QUrlQuery q(u);
      q.removeAllQueryItems(QStringLiteral("page"));
      q.addQueryItem(QStringLiteral("page"), QString::fromUtf8(r.nextPage));
      u.setQuery(q);
      next = u.toString(QUrl::FullyEncoded);
    }
    if(next.isEmpty() || page + 1 >= maxPages) {
      done(r, *pages);
      return;
    }
    pageStep(auth, next, pages, page + 1, maxPages, done);
  });
}

void ForgeApi::fetchLogin(const ForgeAuth& auth, const std::function<void(const QString&, int)>& done) {
  get(auth, QStringLiteral("/user"), [done](const Reply& r) {
    done(r.ok() ? parseForgeLogin(r.body) : QString(), r.status);
  });
}

void ForgeApi::probeProject(const ForgeAuth& auth, const QString& project, const std::function<void(bool, int)>& done) {
  get(auth, projectBase(auth, project), [done](const Reply& r) {
    done(r.ok(), r.status);
  });
}

void ForgeApi::fetchPr(const ForgeAuth& auth, const QString& project, int number, const std::function<void(const PrInfo&, int)>& done) {
  const QString base = projectBase(auth, project);
  if(auth.glab) {
    get(auth, base + QStringLiteral("/merge_requests/%1").arg(number), [done](const Reply& r) {
      done(r.ok() ? parsePrJson(r.body, true) : PrInfo{}, r.status);
    });
    return;
  }
  get(auth, base + QStringLiteral("/pulls/%1").arg(number), [this, auth, base, number, done](const Reply& r) {
    if(!r.ok()) {
      done(PrInfo{}, r.status);
      return;
    }
    PrInfo pr = parseGithubRestPull(QJsonDocument::fromJson(r.body).object());
    if(pr.state != QLatin1String("open")) {
      done(pr, r.status);
      return;
    }
    // GitHub's REST pull has no review decision: the reviews add it up.
    getAllPages(auth,
                base + QStringLiteral("/pulls/%1/reviews?per_page=100").arg(number),
                [pr, done, status = r.status](const Reply& rr, const QByteArray& pages) {
                  PrInfo out = pr;
                  if(rr.ok()) {
                    QString decision;
                    parseGithubApprovedBy(pages, &decision);
                    out.reviewDecision = !decision.isEmpty()             ? decision
                                         : !out.reviewRequests.isEmpty() ? QStringLiteral("REVIEW_REQUIRED")
                                                                         : QString();
                  }
                  done(out, status);
                });
  });
}

void ForgeApi::fetchPrForBranch(const ForgeAuth& auth,
                                const QString& project,
                                const QString& branch,
                                const std::function<void(const PrInfo&, int)>& done) {
  const QString base = projectBase(auth, project);
  QString open;
  QString any;
  if(auth.glab) {
    open = base + QStringLiteral("/merge_requests?source_branch=%1&state=opened&per_page=1").arg(enc(branch));
    any = base + QStringLiteral("/merge_requests?source_branch=%1&order_by=updated_at&per_page=1").arg(enc(branch));
  } else {
    const QString head = enc(project.section(QLatin1Char('/'), 0, 0) + QLatin1Char(':') + branch);
    open = base + QStringLiteral("/pulls?head=%1&state=open&per_page=1").arg(head);
    any = base + QStringLiteral("/pulls?head=%1&state=all&per_page=1").arg(head);
  }
  const auto detail = [this, auth, project, done](const PrInfo& listed, int status) {
    // The list leaves out the pipeline and the merge status; the MR itself
    // (and on GitHub its reviews) has them.
    if(listed.number <= 0) {
      done(listed, status);
      return;
    }
    fetchPr(auth, project, listed.number, [listed, status, done](const PrInfo& full, int s) {
      done(full.state.isEmpty() ? listed : full, full.state.isEmpty() ? status : s);
    });
  };
  get(auth, open, [this, auth, any, detail, done](const Reply& r) {
    if(!r.ok()) {
      done(PrInfo{}, r.status);
      return;
    }
    const PrInfo pr = parseForgePrList(r.body, auth.glab);
    if(!pr.state.isEmpty()) {
      detail(pr, r.status);
      return;
    }
    get(auth, any, [auth, detail, done](const Reply& r2) {
      if(!r2.ok()) {
        done(PrInfo{}, r2.status);
        return;
      }
      detail(parseForgePrList(r2.body, auth.glab), r2.status);
    });
  });
}

void ForgeApi::fetchOwnersFacts(const ForgeAuth& auth,
                                const QString& project,
                                const PrInfo& pr,
                                const std::function<void(const OwnersFacts&, int)>& done,
                                const std::optional<QStringList>& knownFiles) {
  const QString base = projectBase(auth, project);
  OwnersFacts facts;
  facts.baseRef = pr.baseRef;
  facts.author = pr.author;
  facts.requested = pr.reviewRequests;
  if(auth.glab) {
    const QString mr = base + QStringLiteral("/merge_requests/%1").arg(pr.number);
    const auto approvals = [this, auth, mr, facts, done](const QStringList& files) {
      get(auth, mr + QStringLiteral("/approvals"), [facts, files, done](const Reply& a) {
        // Not knowing who approved is not "nobody approved".
        if(!a.ok()) {
          done(OwnersFacts{}, a.status);
          return;
        }
        OwnersFacts f = facts;
        f.ok = true;
        f.files = files;
        f.approvedBy = parseGlabApprovedBy(a.body);
        done(f, a.status);
      });
    };
    if(knownFiles) {
      approvals(*knownFiles);  // a checkout said: no file list from the server
      return;
    }
    // The changed files only change with a new commit: read once per head.
    const QString filesKey = pr.headSha.isEmpty() ? QString() : auth.apiBase + QLatin1Char('|') + mr + QLatin1Char('|') + pr.headSha;
    if(!filesKey.isEmpty() && m_filesByHead.contains(filesKey)) {
      approvals(m_filesByHead.value(filesKey));
      return;
    }
    // The server could not list them for this head: it is not asked again
    // (not after a back-off either) until the MR moves.
    const auto unavailable = [done](int status) {
      OwnersFacts f;
      f.filesUnavailable = true;
      done(f, status);
    };
    if(!filesKey.isEmpty() && gate().filesFailed.contains(filesKey)) {
      unavailable(gate().filesFailed.value(filesKey));
      return;
    }
    const auto failedFiles = [filesKey, unavailable](int status) {
      if(!filesKey.isEmpty()) {
        QHash<QString, int>& failed = gate().filesFailed;
        if(failed.size() >= 256) {
          failed.clear();
        }
        failed.insert(filesKey, status);
      }
      unavailable(status);
    };
    const auto remember = [this, filesKey, approvals](const QStringList& files) {
      if(!filesKey.isEmpty()) {
        if(m_filesByHead.size() >= 256) {
          m_filesByHead.clear();
        }
        m_filesByHead.insert(filesKey, files);
      }
      approvals(files);
    };
    getAllPages(auth,
                mr + QStringLiteral("/diffs?per_page=100"),
                [this, auth, mr, remember, failedFiles, done](const Reply& last, const QByteArray& pages) {
                  if(last.ok()) {
                    bool ok = false;
                    const QStringList files = parseGlabChangedFiles(pages, &ok);
                    if(ok) {
                      remember(files);
                      return;
                    }
                  }
                  // Before GitLab 15.7 there is no /diffs (404); a server that fails
                  // on it (a 500 on a big MR, seen 2026-10-20) may still answer the
                  // legacy /changes — asked once, then the gate's back-off holds both.
                  if(last.status != 404 && !(last.status >= 500 && !last.held)) {
                    done(OwnersFacts{}, last.ok() ? 0 : last.status);
                    return;
                  }
                  get(auth, mr + QStringLiteral("/changes"), [remember, failedFiles, done, diffsStatus = last.status](const Reply& c) {
                    bool ok = false;
                    const QStringList files = c.ok() ? parseGlabChangedFiles(c.body, &ok) : QStringList{};
                    if(ok) {
                      remember(files);
                      return;
                    }
                    // Both lists failed on the server (or /diffs did and /changes is
                    // gone): the server cannot give this MR's files.
                    if((c.status == 429 || c.status >= 500) || (diffsStatus >= 500 && c.status == 404)) {
                      failedFiles(c.status == 404 ? diffsStatus : c.status);
                      return;
                    }
                    done(OwnersFacts{}, c.status);
                  });
                });
    return;
  }
  const QString pull = base + QStringLiteral("/pulls/%1").arg(pr.number);
  const auto reviews = [this, auth, pull, facts, done](const QStringList& files) {
    getAllPages(auth, pull + QStringLiteral("/reviews?per_page=100"), [facts, files, done](const Reply& rl, const QByteArray& pages) {
      if(!rl.ok()) {
        done(OwnersFacts{}, rl.status);
        return;
      }
      OwnersFacts f = facts;
      f.ok = true;
      f.files = files;
      f.approvedBy = parseGithubApprovedBy(pages);
      done(f, rl.status);
    });
  };
  if(knownFiles) {
    reviews(*knownFiles);
    return;
  }
  getAllPages(auth, pull + QStringLiteral("/files?per_page=100"), [reviews, done](const Reply& last, const QByteArray& pages) {
    bool ok = false;
    const QStringList files = last.ok() ? parseGhPrFiles(pages, &ok) : QStringList{};
    if(!ok) {
      done(OwnersFacts{}, last.status);
      return;
    }
    reviews(files);
  });
}

void ForgeApi::fetchCodeOwnersText(const ForgeAuth& auth,
                                   const QString& project,
                                   const QString& ref,
                                   const std::function<void(const CodeOwnersText&)>& done) {
  codeOwnersStep(auth, project, ref, 0, done);
}

void ForgeApi::codeOwnersStep(
    const ForgeAuth& auth, const QString& project, const QString& ref, int at, const std::function<void(const CodeOwnersText&)>& done) {
  const QString base = projectBase(auth, project);
  const QStringList locations = codeOwnersLocations(auth.glab ? OwnersFlavor::GitLab : OwnersFlavor::GitHub);
  if(at >= locations.size()) {
    // Nowhere: no file, or no access to the files at all? The branch says.
    const QString branch = base + (auth.glab ? QStringLiteral("/repository/branches/") : QStringLiteral("/branches/")) + enc(ref);
    get(auth, branch, [done](const Reply& b) {
      CodeOwnersText t;
      t.ok = b.ok();
      if(!b.ok()) {
        t.reason = ForgeApi::isFailure(b.status) ? ownersReasonForStatus(b.status) : QStringLiteral("noRepoAccess");
      }
      done(t);
    });
    return;
  }
  const QString loc = locations.at(at);
  const QString path = auth.glab ? base + QStringLiteral("/repository/files/%1/raw?ref=%2").arg(enc(loc), enc(ref))
                                 : base + QStringLiteral("/contents/%1?ref=%2").arg(loc, enc(ref));
  get(
      auth,
      path,
      [this, auth, project, ref, at, loc, done](const Reply& r) {
        if(r.ok()) {
          CodeOwnersText t;
          t.ok = true;
          t.found = true;
          t.location = loc;
          t.text = QString::fromUtf8(r.body);
          done(t);
          return;
        }
        if(r.status == 404) {
          codeOwnersStep(auth, project, ref, at + 1, done);
          return;
        }
        CodeOwnersText t;
        t.reason = ForgeApi::isFailure(r.status) ? ownersReasonForStatus(r.status) : QStringLiteral("noRepoAccess");
        done(t);
      },
      auth.glab ? QByteArray() : QByteArrayLiteral("application/vnd.github.raw"));
}

void ForgeApi::fetchOwnersReport(const ForgeAuth& auth,
                                 const QString& project,
                                 const PrInfo& pr,
                                 const std::function<void(const QVariantMap&)>& done,
                                 const LocalOwners& local) {
  const QString host = forgeHostOf(auth.host);
  // GitLab's id for the project, from the MR itself: no group path to spell
  // (and to get wrong) for a project deep in nested groups.
  const QString proj = auth.glab && pr.projectId > 0 ? QString::number(pr.projectId) : project;
  const std::optional<QStringList> known = local.known ? std::optional<QStringList>(local.files) : std::nullopt;
  fetchOwnersFacts(
      auth,
      proj,
      pr,
      [this, auth, proj, pr, host, local, done](const OwnersFacts& facts, int status) {
        if(!facts.ok) {
          const QString reason = facts.filesUnavailable ? QStringLiteral("noFiles") : ownersReasonForStatus(status);
          done(ownersReasonVariant(reason.isEmpty() ? QStringLiteral("unreachable") : reason, host, auth.glab, pr));
          return;
        }
        const OwnersFlavor flavor = auth.glab ? OwnersFlavor::GitLab : OwnersFlavor::GitHub;
        const QString author = facts.author.isEmpty() ? pr.author : facts.author;
        if(local.known) {
          const CodeOwnersReport rep =
              buildOwnersReport(local.ownersLocation, local.ownersText, flavor, facts.files, facts.approvedBy, facts.requested, author);
          done(ownersReportVariant(rep, pr, auth.glab, static_cast<int>(facts.files.size())));
          return;
        }
        ownersFromForgeFile(auth, proj, pr, facts, done);
      },
      known);
}

void ForgeApi::ownersFromForgeFile(const ForgeAuth& auth,
                                   const QString& project,
                                   const PrInfo& pr,
                                   const OwnersFacts& facts,
                                   const std::function<void(const QVariantMap&)>& done) {
  const QString host = forgeHostOf(auth.host);
  const QString ref = facts.baseRef.isEmpty() ? pr.baseRef : facts.baseRef;
  fetchCodeOwnersText(auth, project, ref, [auth, pr, facts, host, done](const CodeOwnersText& t) {
    if(!t.ok) {
      done(ownersReasonVariant(t.reason, host, auth.glab, pr));
      return;
    }
    const OwnersFlavor flavor = auth.glab ? OwnersFlavor::GitLab : OwnersFlavor::GitHub;
    const QString author = facts.author.isEmpty() ? pr.author : facts.author;
    const CodeOwnersReport rep = buildOwnersReport(t.location, t.text, flavor, facts.files, facts.approvedBy, facts.requested, author);
    done(ownersReportVariant(rep, pr, auth.glab, static_cast<int>(facts.files.size())));
  });
}

}  // namespace heap::git
