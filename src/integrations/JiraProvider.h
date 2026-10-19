#pragma once

#include "integrations/IntegrationProvider.h"
#include "integrations/IntegrationTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace heap::integrations {

// Parse a Jira Cloud REST v3 search response ({ "issues": [...] }) into
// ExternalTasks. `baseUrl` builds each issue's browse URL. Descriptions arrive
// as ADF (Atlassian Document Format) JSON and are converted to markdown.
// Pure, no network — unit-tested with canned JSON.
// `sprintField` is the instance's sprint custom field ("customfield_10020"),
// empty when the site has none (APP-255): the issue's current sprint then goes
// to details.sprint. `pointsFields` are its story-point fields (the first one
// with a value wins); with timetracking they go to details.estimate
// {original, remaining, spent (seconds), points} (dogfood 2026-10-19).
QVector<ExternalTask> parseJiraIssues(const QByteArray& json,
                                      const QString& baseUrl,
                                      const QString& sprintField = QString(),
                                      const QStringList& pointsFields = {});

// A site's story-point fields, read from a /field response: Jira Software's
// own "Story point estimate" (schema.custom …:jsw-story-points) first, then a
// numeric field named "Story Points" / "Story point estimate" (company-managed
// Cloud projects and Server/DC). Empty when the site has none. Pure.
QStringList parseJiraStoryPointsFields(const QByteArray& fieldsJson);

// When the issue entered the status it is in now, from one issue with
// fields status,created and expand=changelog (dogfood 2026-10-19). Cloud lists
// the histories newest first, Server/DC oldest first, so the newest status
// item is looked for, not the first or last. Only an answer whose newest
// move ends in the status the issue has is trusted (Cloud keeps the last 100
// histories); an issue never moved entered its status when it was created.
// `ok` false when the body says neither. Pure.
ExternalStatusChange parseJiraStatusChange(const QByteArray& issueJson, bool* ok = nullptr);
// The same for each issue of a search answer ({issues: […]}); an issue it
// cannot tell for is left out. Pure.
QVector<ExternalStatusChange> parseJiraStatusChanges(const QByteArray& searchJson);

// The sprint custom field of a site, read from a /field response: the one
// whose schema.custom is com.pyxis.greenhopper.jira:gh-sprint. Empty when the
// site has no Jira Software (APP-255). Pure.
QString parseJiraSprintField(const QByteArray& fieldsJson);

// The sprint an issue is in now, from its sprint field's value (APP-255):
// {name, state, start, end}, the dates as ISO strings. The active one when
// there is one, else the nearest future one; a closed sprint is never it.
// Takes Cloud's objects and Server's "…Sprint@1a2b[id=1,state=ACTIVE,…]"
// strings. Empty when the issue is in no open sprint. Pure.
QJsonObject jiraCurrentSprint(const QJsonValue& sprintFieldValue);

// Convert an Atlassian Document Format value (string or ADF object) to
// markdown: headings, lists, task lists, code blocks, quotes, tables and links
// keep their shape. Exposed for unit testing.
QString jiraAdfToMarkdown(const QByteArray& adfJson);

// Parse a /issue/{key}/comment response (HEAP-117). Bodies arrive as ADF on
// Cloud and as plain strings on Server/DC; both flatten to text. No network.
// `total` (optional) is how many the issue has in all, -1 when not said.
QVector<ExternalComment> parseJiraComments(const QByteArray& json, int* total = nullptr);

// Status names from a /status response (an array of {name, …}), de-duplicated
// and sorted. Pure, no network.
QStringList parseJiraStatuses(const QByteArray& json);

// Make whatever the user pasted into a site root. People paste the URL from
// their browser ("acme.atlassian.net/jira/software/projects/LTE/boards/1"), and
// every one of those forms has to end up as "https://acme.atlassian.net".
// Pure — unit-tested.
QString normalizeJiraBaseUrl(const QString& raw);

// The JQL used when the user leaves the field empty. Not just "order by updated
// DESC": the search endpoint rejects a query with no search restriction with
// "Unbounded JQL queries are not allowed here", which made every sync on a
// fresh Jira card come back empty.
QString defaultJiraJql();

// The JQL that asks "is this one issue still in the user's filter?" before a
// status write (APP-204): `key = "KEY" AND (<filter>)`, with the filter's
// ORDER BY dropped. An empty filter is the default one. Pure — unit-tested.
QString jiraJqlForIssue(const QString& key, const QString& jql);

// The live-updates probe's JQL (integrations/LiveSync.h): the user's filter,
// narrowed to issues updated in the last `minutes`, newest first —
// `(<filter>) AND updated >= -15m ORDER BY updated DESC`. Relative, so the
// Jira user's own time zone never enters it. Pure — unit-tested.
QString jiraChangedSinceJql(const QString& jql, int minutes);

// The newest `updated` in a probe's search answer, invalid when it has no
// issue. Pure — unit-tested.
QDateTime jiraNewestUpdated(const QByteArray& searchJson);

// Which of an issue's workflow transitions (GET …/transitions' array) a drop
// on `column` executes. The user's mapping decides first: the first
// transition whose target status maps to the column. A status nobody mapped
// is chosen by its kind only when it is the one transition of that kind —
// a Done column next to "Done", "Won't Do" and "Duplicate" is not ours to
// pick (review 2026-10-19). Empty when there is none or no single one;
// `targetStatus` gets the transition's status name. Pure — unit-tested.
QString pickJiraTransition(const QJsonArray& transitions,
                           const QString& column,
                           const QHash<QString, QString>& overrides,
                           QString* targetStatus = nullptr);

// Which Jira a site is. The two are different products behind the same name:
// Cloud serves /rest/api/3 and authenticates a pair (account email + API
// token); Server and Data Center serve /rest/api/2 and take a Personal Access
// Token as a bearer credential. Picking wrong produces a 401 or a 404 with
// nothing pointing at the real cause, so heap detects it rather than asking.
enum class JiraDeployment : std::uint8_t {
  Unknown,
  Cloud,
  Server,  // covers Data Center — same API, same auth
};

// Read the deploymentType out of a /rest/api/2/serverInfo response. Unknown
// when the body says nothing useful. Pure — unit-tested.
JiraDeployment parseJiraDeployment(const QByteArray& serverInfoJson);

// The deployment to assume when the probe itself fails: only Atlassian runs
// *.atlassian.net, and everything else self-hosted is Server/DC. Pure.
JiraDeployment guessJiraDeployment(const QString& baseUrl);

// Pick which Atlassian site to use out of an accessible-resources response
// ([{ "id": cloudId, "url": …, "name": … }]). Prefers the one matching
// `preferredUrl`; otherwise takes the first. Returns {cloudId, siteUrl} — both
// empty when the response grants no site. Pure — unit-tested.
struct JiraSite {
  QString cloudId;
  QString url;
};

JiraSite pickJiraSite(const QByteArray& accessibleResourcesJson, const QString& preferredUrl);

// Concrete IntegrationProvider backed by the Jira Cloud REST API v3 (HEAP-75).
// Auth is either HTTP Basic (account email + API token) or, after a browser
// sign-in, a Bearer access token through the Atlassian API gateway. Status
// pushes go through the issue's available workflow transitions.
class JiraProvider : public IntegrationProvider {
  Q_OBJECT

 public:
  explicit JiraProvider(QObject* parent = nullptr);
  ~JiraProvider() override;

  // Configure from the Settings → Integrations Jira card. `baseUrl` is the site
  // ("https://acme.atlassian.net"); `email` + `token` form the Basic-auth pair;
  // `jql` selects which issues to pull. Empty `jql` falls back to
  // defaultJiraJql().
  void setConfig(const QString& baseUrl, const QString& email, const QString& token, const QString& jql);
  // Browser sign-in (OAuth 2.0 3LO). No email: the token identifies the user,
  // and every call goes to the gateway, which is the only host that accepts a
  // 3LO token. `cloudId` comes from accessible-resources at sign-in time and is
  // cached in the card's config; `siteUrl` only builds browse links.
  void setOAuthConfig(const QString& cloudId, const QString& siteUrl, const QString& token, const QString& jql);
  bool isConfigured() const;

  // Override the auto-detection. Only the tests use this; the app lets
  // detectDeployment() decide on the first request.
  void setDeployment(JiraDeployment deployment) {
    m_deployment = deployment;
  }

  JiraDeployment deployment() const {
    return m_deployment;
  }

  // Host the scoped-token fallback goes through. Only tests change it.
  void setGatewayRoot(const QString& root) {
    m_gatewayRoot = root;
  }

  QString id() const override {
    return QStringLiteral("jira");
  }

  QString displayName() const override {
    return QStringLiteral("Jira");
  }

  void testConnection() override;
  void pullTasks() override;
  void pushStatusChange(const QString& externalId, const QString& newStatus, const QString& project) override;
  void fetchComments(const QString& externalId, const QString& project) override;
  void fetchStatuses() override;
  // GET /issue/{key}?fields=status, then a search for the key within the
  // user's JQL (jiraJqlForIssue): one issue back means still in the filter.
  void checkIssue(const QString& externalId, const QString& project) override;

  bool canLookUpIssues() const override {
    return true;
  }

  // GET /issue/{key} for each key: a 404 is "missing", a 200 is found.
  void lookUpIssues(const QStringList& externalIds) override;

  bool canFetchStatusChanges() const override {
    return true;
  }

  // One search `key in (…)` with fields status,created and the changelog
  // expanded (Cloud /search/jql, Server /search), at most
  // kStatusChangeBatch keys a call.
  void fetchStatusChanges(const QStringList& externalIds) override;
  static constexpr int kStatusChangeBatch = 40;

  bool canProbeChanges() const override {
    return true;
  }

  // One search for jiraChangedSinceJql, maxResults 1, fields=updated.
  void probeChanges(int withinMinutes) override;

  void setStatusOverrides(const QHash<QString, QString>& overrides) override {
    m_statusOverrides = overrides;
  }

 private:
  QHash<QString, QString> m_statusOverrides;

  // One finished request, already drained — the reply itself is gone by the
  // time a callback runs, because a retry may outlive it.
  struct ApiResult {
    bool ok = false;
    int status = 0;   // 0 = the request never reached the server
    QByteArray body;  // response body (valid on success and on failure)
    QString error;    // human-readable, empty when ok
    QByteArray retryAfter;  // the Retry-After header, raw; empty when none
  };

  using ApiCallback = std::function<void(const ApiResult&)>;

  // Replace an unauthorized result's message with one naming what to change.
  // Qt renders every 401 as "Host requires authentication" and Jira's own body
  // is no more actionable, so neither tells the user what is wrong.
  ApiResult explainAuthFailure(const ApiResult& result, bool cloudIdResolved) const;

  // Send to <api base>/rest/api/3<path>, retrying once through the Atlassian
  // API gateway if the site host answers 401 (see m_apiBase).
  void send(const QByteArray& method, const QString& path, const QByteArray& body, const ApiCallback& done);
  void sendOnce(const QByteArray& method, const QString& path, const QByteArray& body, const ApiCallback& done);
  // GET {site}/rest/api/2/serverInfo once, to learn Cloud from Server. Falls
  // back to guessJiraDeployment() when the probe fails.
  void detectDeployment(const std::function<void()>& then);
  // Run `then` once the deployment is known, probing for it if need be.
  void ensureDeployment(const std::function<void()>& then);
  // "/rest/api/3" on Cloud, "/rest/api/2" on Server/DC.
  QString apiRoot() const;
  // GET {site}/_edge/tenant_info — unauthenticated, returns the site's cloudId.
  void resolveCloudId(std::function<void(bool)> done);

  // One page of a search, accumulating into m_pulled. `cursor` is the Cloud
  // nextPageToken, `startAt` the Server row offset; the deployment decides
  // which one is in play.
  void pullPage(const QString& cursor, int startAt);

  QNetworkAccessManager* m_nam = nullptr;
  QString m_baseUrl;  // "https://acme.atlassian.net" (no trailing slash)
  QString m_email;    // empty in OAuth mode
  QString m_token;
  QString m_jql;
  bool m_oauth = false;
  JiraDeployment m_deployment = JiraDeployment::Unknown;

  // Where API calls actually go. Starts as the site itself, which is what a
  // classic (unscoped) API token expects. Atlassian's newer scoped tokens are
  // rejected there with a 401 and only work through
  // https://api.atlassian.com/ex/jira/{cloudId}, so the first 401 switches us
  // over for the rest of this provider's life.
  QString m_apiBase;
  QString m_gatewayRoot = QStringLiteral("https://api.atlassian.com");

  // A pull is a walk over search pages: one tasksFetched at the end, whatever
  // it took to get there.
  QVector<ExternalTask> m_pulled;
  int m_pullPage = 0;
  // The sprint custom field (APP-255), looked up once per provider; a lookup
  // that fails leaves it empty and the pull goes on without sprints.
  QString m_sprintField;
  // Story-point fields, looked up with the sprint field.
  QStringList m_pointsFields;
  bool m_sprintFieldKnown = false;
  bool m_pulling = false;
  bool m_usingGateway = false;
  bool m_gatewayTried = false;
};

}  // namespace heap::integrations
