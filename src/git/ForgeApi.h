#pragma once

#include "CodeOwners.h"
#include "GitTypes.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <memory>

class QNetworkAccessManager;

// The forge without its CLI (dogfood 2026-10-20): GitLab's v4 API and
// GitHub's REST API (and GitHub Enterprise's /api/v3), read with the token of
// the GitLab / GitHub tracker the user already connected — so a work PC with
// neither `glab` nor `gh` installed still sees its MRs and their CODEOWNERS.
//
// Read only, always: every call is a GET. Nothing is ever sent to the forge,
// no reviewer is requested, no comment is written. The token travels only to
// the API base of the integration it belongs to and is never logged.
namespace heap::git {

// One connected forge integration, as GitWatcher and the tracker tickets use
// it. Built by AppController from the integration's config with the same
// auth-header code the tracker sync uses (integrations::authHeaders).
struct ForgeAuth {
  bool glab = false;                             // GitLab v4; false = GitHub REST
  QString providerId;                            // "gitlab" | "github"
  QString host;                                  // the web host it points at, lower-case: "gitlab.company.ru"
  QString apiBase;                               // "https://gitlab.company.ru/api/v4", "https://api.github.com"
  QList<QPair<QByteArray, QByteArray>> headers;  // auth + Accept; never logged

  bool valid() const {
    return !apiBase.isEmpty();
  }

  bool operator==(const ForgeAuth& o) const = default;
};

// "https://GitLab.Company.ru:8443/sub/" → "gitlab.company.ru"; a bare host is
// lower-cased; GitHub's API host is GitHub's host ("api.github.com" →
// "github.com").
QString forgeHostOf(const QString& urlOrHost);
// Where the API lives for an instance's web base ("https://gitlab.x.ru",
// with any relative root kept). GitHub.com has its own API host; any other
// GitHub host is an Enterprise server with /api/v3.
QString forgeApiBase(bool glab, const QString& webBase);
bool forgeHostMatches(const ForgeAuth& auth, const QString& repoHost);
// Which of `auths` may answer for a repository whose origin names `repoHost`:
// the ones on that host; when none is, the host may be an ssh alias
// (`git@gitlab:group/app.git`, a HostName that is not the web host), so every
// one of them is a candidate to try — except for github.com and gitlab.com,
// which are nobody's alias. Indices into `auths`, the likelier first.
QList<int> forgeAuthCandidates(const QList<ForgeAuth>& auths, const QString& repoHost);

// A PR / MR web URL taken apart: https://host/group/app/-/merge_requests/7,
// https://github.com/o/r/pull/12.
struct ForgePrRef {
  QString host;
  QString project;  // "group/sub/app", "o/r"
  int number = 0;
  bool glab = false;

  bool valid() const {
    return !host.isEmpty() && project.contains(QLatin1Char('/')) && number > 0;
  }
};

ForgePrRef parseForgePrUrl(const QString& url);

// ---- The pure half: answers into the watcher's types ----

// GitHub REST `GET /repos/o/r/pulls/N` (or one element of the list): state
// "open" / "closed" / "merged", requested reviewers and teams, mergeable.
PrInfo parseGithubRestPull(const QJsonObject& o);
// The first PR / MR of a list answer, or an empty PrInfo (no state).
PrInfo parseForgePrList(const QByteArray& raw, bool glab);
// `GET /user`: `login` (GitHub) or `username` (GitLab).
QString parseForgeLogin(const QByteArray& raw);
// GitHub `…/pulls/N/reviews`, every page one after another: who approved in
// their latest say, and the review decision that adds up to
// ("CHANGES_REQUESTED" | "APPROVED" | "").
QStringList parseGithubApprovedBy(const QByteArray& pages, QString* decision = nullptr);
// The `rel="next"` URL of a Link header, "" when there is none.
QString nextLinkUrl(const QByteArray& linkHeader);

// What GitWatcher and the tracker tickets show: CodeOwnersReport::toVariant()
// plus the PR's number, URL, title, flavor, file count and target branch.
QVariantMap ownersReportVariant(const CodeOwnersReport& report, const PrInfo& pr, bool glab, int fileCount);
// A report that only says why there is none (TaskOwnersPanel's quiet line):
// {reason, host, flavor, prNumber, prUrl}. `reason` is an I18n key suffix
// (owners.reason.<reason>): noAccess, denied, unreachable, noRepoAccess,
// notMine, off.
QVariantMap ownersReasonVariant(const QString& reason, const QString& host, bool glab, const PrInfo& pr);
// The reason for an HTTP status of a PR / MR read ("" for a success).
QString ownersReasonForStatus(int status);

class ForgeApi : public QObject {
  Q_OBJECT
 public:
  explicit ForgeApi(QObject* parent = nullptr);
  ~ForgeApi() override;

  void setTimeoutMs(int ms) {
    m_timeoutMs = ms;
  }

  struct Reply {
    int status = 0;  // HTTP status; 0 = no answer (network, timeout)
    QByteArray body;
    QByteArray link;      // Link header
    QByteArray nextPage;  // GitLab's X-Next-Page

    bool ok() const {
      return status >= 200 && status < 300;
    }
  };

  using Done = std::function<void(const Reply&)>;

  // One GET of apiBase + `path` (path and query, already encoded). `accept`
  // replaces the integration's Accept header (GitHub's raw file contents).
  void get(const ForgeAuth& auth, const QString& path, const Done& done, const QByteArray& accept = {});
  // Every page of a list: `pages` is each page's body, one after another,
  // `last` the reply that ended the walk (an error stops it).
  void getAllPages(const ForgeAuth& auth,
                   const QString& firstPath,
                   const std::function<void(const Reply& last, const QByteArray& pages)>& done,
                   int maxPages = 30);

  // Who the token belongs to. `login` empty when it could not be read.
  void fetchLogin(const ForgeAuth& auth, const std::function<void(const QString& login, int status)>& done);
  // Whether this integration knows the project ("group/app", "o/r").
  void probeProject(const ForgeAuth& auth, const QString& project, const std::function<void(bool known, int status)>& done);
  // The PR / MR whose source branch is `branch`: the open one, else the
  // latest in any state (so a merged one shows as merged). An empty PrInfo
  // with a 2xx status is "no PR".
  void fetchPrForBranch(const ForgeAuth& auth,
                        const QString& project,
                        const QString& branch,
                        const std::function<void(const PrInfo& pr, int status)>& done);
  // One PR / MR by number (iid), with what a whose-move needs.
  void fetchPr(const ForgeAuth& auth, const QString& project, int number, const std::function<void(const PrInfo& pr, int status)>& done);
  // Changed files (every page), who approved, who is asked.
  void fetchOwnersFacts(const ForgeAuth& auth,
                        const QString& project,
                        const PrInfo& pr,
                        const std::function<void(const OwnersFacts& facts, int status)>& done);

  struct CodeOwnersText {
    bool ok = false;     // the answer is known (found or not)
    bool found = false;  // a CODEOWNERS file was read
    QString location;
    QString text;
    QString reason;  // with !ok: noRepoAccess, unreachable
  };

  // The first CODEOWNERS of the forge's places on `ref`. A 404 everywhere is
  // "no file" only when the branch itself can be read; otherwise the token
  // sees the MR but not the repository's files (noRepoAccess).
  void fetchCodeOwnersText(const ForgeAuth& auth,
                           const QString& project,
                           const QString& ref,
                           const std::function<void(const CodeOwnersText& result)>& done);
  // All of it for one open PR: facts, CODEOWNERS, the report
  // (ownersReportVariant), or a reason (ownersReasonVariant).
  void fetchOwnersReport(const ForgeAuth& auth,
                         const QString& project,
                         const PrInfo& pr,
                         const std::function<void(const QVariantMap& report)>& done);

  // How many requests went out, and with which methods — for the tests that
  // pin "read only".
  int requestCount() const {
    return m_requests;
  }

 private:
  void pageStep(const ForgeAuth& auth,
                const QString& path,
                const std::shared_ptr<QByteArray>& pages,
                int page,
                int maxPages,
                const std::function<void(const Reply&, const QByteArray&)>& done);
  void codeOwnersStep(
      const ForgeAuth& auth, const QString& project, const QString& ref, int at, const std::function<void(const CodeOwnersText&)>& done);

  QNetworkAccessManager* m_nam = nullptr;
  int m_timeoutMs = 20000;
  int m_requests = 0;
};

}  // namespace heap::git
