#include "git/ForgeApi.h"
#include "git/PrFacts.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <memory>
#include <utility>

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
  if(status == 0 || status >= 500) {
    return QStringLiteral("unreachable");
  }
  // 401/403: the token is refused; 404: the token cannot see the project.
  return QStringLiteral("denied");
}

// ---- ForgeApi ----

ForgeApi::ForgeApi(QObject* parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)) {
}

ForgeApi::~ForgeApi() = default;

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
  QNetworkRequest req{QUrl(url)};
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  req.setTransferTimeout(m_timeoutMs);
  req.setRawHeader("User-Agent", "tabless-forge");
  for(const auto& h : auth.headers) {
    req.setRawHeader(h.first, h.second);
  }
  if(!accept.isEmpty()) {
    req.setRawHeader("Accept", accept);
  }
  ++m_requests;
  QNetworkReply* reply = m_nam->get(req);  // the only verb this class has
  connect(reply, &QNetworkReply::finished, this, [reply, done]() {
    reply->deleteLater();
    Reply r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    r.body = reply->readAll();
    r.link = reply->rawHeader("Link");
    r.nextPage = reply->rawHeader("X-Next-Page").trimmed();
    done(r);
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
                                const std::function<void(const OwnersFacts&, int)>& done) {
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
    getAllPages(
        auth, mr + QStringLiteral("/diffs?per_page=100"), [this, auth, mr, approvals, done](const Reply& last, const QByteArray& pages) {
          if(last.ok()) {
            bool ok = false;
            const QStringList files = parseGlabChangedFiles(pages, &ok);
            if(ok) {
              approvals(files);
              return;
            }
          }
          if(last.status != 404) {
            done(OwnersFacts{}, last.ok() ? 0 : last.status);
            return;
          }
          // Before GitLab 15.7 there is no /diffs: the legacy /changes.
          get(auth, mr + QStringLiteral("/changes"), [approvals, done](const Reply& c) {
            bool ok = false;
            const QStringList files = c.ok() ? parseGlabChangedFiles(c.body, &ok) : QStringList{};
            if(!ok) {
              done(OwnersFacts{}, c.status);
              return;
            }
            approvals(files);
          });
        });
    return;
  }
  const QString pull = base + QStringLiteral("/pulls/%1").arg(pr.number);
  getAllPages(
      auth, pull + QStringLiteral("/files?per_page=100"), [this, auth, pull, facts, done](const Reply& last, const QByteArray& pages) {
        bool ok = false;
        const QStringList files = last.ok() ? parseGhPrFiles(pages, &ok) : QStringList{};
        if(!ok) {
          done(OwnersFacts{}, last.status);
          return;
        }
        getAllPages(auth, pull + QStringLiteral("/reviews?per_page=100"), [facts, files, done](const Reply& rl, const QByteArray& reviews) {
          if(!rl.ok()) {
            done(OwnersFacts{}, rl.status);
            return;
          }
          OwnersFacts f = facts;
          f.ok = true;
          f.files = files;
          f.approvedBy = parseGithubApprovedBy(reviews);
          done(f, rl.status);
        });
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
        t.reason = b.status == 0 || b.status >= 500 ? QStringLiteral("unreachable") : QStringLiteral("noRepoAccess");
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
        t.reason = r.status == 0 || r.status >= 500 ? QStringLiteral("unreachable") : QStringLiteral("noRepoAccess");
        done(t);
      },
      auth.glab ? QByteArray() : QByteArrayLiteral("application/vnd.github.raw"));
}

void ForgeApi::fetchOwnersReport(const ForgeAuth& auth,
                                 const QString& project,
                                 const PrInfo& pr,
                                 const std::function<void(const QVariantMap&)>& done) {
  const QString host = forgeHostOf(auth.host);
  fetchOwnersFacts(auth, project, pr, [this, auth, project, pr, host, done](const OwnersFacts& facts, int status) {
    if(!facts.ok) {
      const QString reason = ownersReasonForStatus(status);
      done(ownersReasonVariant(reason.isEmpty() ? QStringLiteral("unreachable") : reason, host, auth.glab, pr));
      return;
    }
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
  });
}

}  // namespace heap::git
