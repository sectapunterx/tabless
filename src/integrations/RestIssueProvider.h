#pragma once

#include "integrations/IntegrationProvider.h"
#include "integrations/IntegrationTypes.h"
#include "integrations/ProviderDescriptor.h"
// valueAtPath / fieldStr / parseTrackerTimestamp and friends live in their own
// header so a bespoke parser can use them without linking the REST engine.
#include "integrations/TrackerFields.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace heap::integrations {

// One IntegrationProvider implementation that drives every generic REST tracker
// from its ProviderDescriptor — auth, list endpoint, response parsing and
// optional status write-back are all data. Adding a token+REST+JSON provider is
// a descriptor entry, no new networking code. Bespoke providers (Jira, Trello)
// stay as their own subclasses.
class RestIssueProvider : public IntegrationProvider {
  Q_OBJECT

 public:
  explicit RestIssueProvider(ProviderDescriptor desc, QObject* parent = nullptr);
  ~RestIssueProvider() override;

  // Config keys correspond to the descriptor's uiFields (non-secret from
  // settings, secret from the keychain). Values are trimmed.
  void setConfig(const QVariantMap& cfg);
  bool isConfigured() const;

  QString id() const override {
    return m_desc.id;
  }

  QString displayName() const override {
    return m_desc.displayName;
  }

  void testConnection() override;
  void pullTasks() override;
  void pushStatusChange(const QString& externalId, const QString& newStatus, const QString& project) override;
  void fetchComments(const QString& externalId, const QString& project) override;
  // GET issuePathTemplate. In a repo/project filter the issue is in it when
  // it still lives there; under "assigned to me" when the signed-in user is
  // one of its assignees (selfUserPath, asked once per provider).
  void checkIssue(const QString& externalId, const QString& project) override;
  void fetchCiJobs(const QString& externalId, const QString& project, const QString& ref) override;
  void fetchCiLog(const QString& externalId, const QString& project, const QString& jobId) override;

 private:
  // The signed-in user's login, fetched once. `done(login, httpStatus, error)`.
  void withSelfLogin(const std::function<void(const QString&, int, const QString&)>& done);
  QString m_selfLogin;
  // Expand "{key}" / "{key:enc}" placeholders from the config (plus any `extra`
  // overrides such as {externalId} / {state}). ":enc" percent-encodes the value;
  // host/baseUrl values have trailing slashes trimmed.
  QString expand(const QString& tmpl, const QVariantMap& extra = {}) const;
  QNetworkRequest buildRequest(const QString& url) const;
  // Resolved base URL (descriptor template, falling back to baseUrlFallback when
  // {host} is blank). Trailing slash trimmed.
  QString resolvedBaseUrl() const;
  // The list endpoint: the zero-config "assigned to me" path when the scoping
  // field is blank and a self endpoint exists, else the scoped list path.
  QString listPath() const;
  // True when the descriptor has a self endpoint and the scoping field is blank.
  bool inSelfScope() const;

  // One pull in progress. A pull is a walk over pages, and every page may be
  // retried, so the state that used to live in a single lambda's capture now
  // outlives it.
  struct Pull {
    QString base;  // resolved base URL, for FieldMap::urlTemplate
    bool crossProject = false;
    QUrl url;         // the page currently being fetched
    int page = 0;     // pages already accepted
    int offset = 0;   // PageStyle::Offset only
    int attempt = 0;  // retries of the current page
    QVector<ExternalTask> tasks;
    bool active = false;
    // The merge / pull request lists walked after the issues (APP-242).
    bool review = false;
    QStringList reviewQueue;  // full URLs still to walk
    int reviewPage = 0;       // pages of the current list
    QString issuesTruncated;  // why the issue walk stopped early, if it did
    QString reviewError;      // the first review list that failed
    QVector<ExternalTask> reviewTasks;
    // After the lists (APP-318): the open ones' CI, one at a time.
    QVector<ExternalTask> ciTasks;
    QVector<int> ciQueue;
  };

  // The roles whose merge / pull requests this pull also brings (APP-242);
  // empty when the provider has none or they are switched off.
  QStringList reviewRoles() const;
  // After the issue walk: queue the review lists and walk them.
  void startReviews(const QStringList& roles);
  void queueReviews(const QStringList& roles, const QString& me);
  // Walk the next queued review list, or finish the pull.
  void nextReview();
  // Emit issues and review items together.
  void finishAll();
  void emitAll(const QVector<ExternalTask>& tasks);
  // The CI of the next queued open merge / pull request, then the rest.
  void nextCi();
  void storeCi(int index, const QString& state, const QString& url, const QString& ref, const QString& at);
  // A GET whose answer may be a redirect to another host (a GitHub log is
  // served from blob storage): followed once, without the credentials.
  void getFollowing(const QString& url, const std::function<void(QNetworkReply*)>& done);

  // Fetch m_pull.url, retrying it on 429/5xx, and hand the body to onPage().
  void fetchPage();
  // Accept one page's tasks and either queue the next page or finish.
  void onPage(const QByteArray& body, const QByteArray& linkHeader);
  // Work out where the next page lives. Empty URL = the walk is over.
  QUrl nextPageUrl(const QByteArray& body, const QByteArray& linkHeader, int lastCount);
  // Emit what the walk gathered, plus an error when it stopped early.
  void finishPull(const QString& truncatedReason);

  ProviderDescriptor m_desc;
  QVariantMap m_cfg;
  QNetworkAccessManager* m_nam = nullptr;
  Pull m_pull;
};

// The headers every authenticated call of this provider carries: its extra
// headers (Accept, API version) and the token in the provider's PAT header,
// or as a Bearer token when it came from browser sign-in. Shared with the
// CLI-free forge client (git/ForgeApi), so the token is read and sent the
// same way everywhere.
QList<QPair<QByteArray, QByteArray>> authHeaders(const ProviderDescriptor& desc, const QVariantMap& cfg);

// Pure, unit-tested extraction of a list response into ExternalTasks using a
// declarative FieldMap. `baseUrl` feeds FieldMap::urlTemplate. No network.
QVector<ExternalTask> parseWithFieldMap(const QByteArray& json, const FieldMap& map, const QString& providerId, const QString& baseUrl);

// The same, for a comment list. Entries whose `skipIfTrue` leaf is true are
// dropped. No network.
QVector<ExternalComment> parseCommentsWithMap(const QByteArray& json, const CommentMap& map);

}  // namespace heap::integrations
