// Unit tests for the descriptor-driven tracker sync: the generic FieldMap
// parser (RestIssueProvider), the provider registry's field mappings, the
// bespoke Trello parsers, the SecretStore cache and the HTTP error formatter.
// No network.

#include "FakeHttpServer.h"

#include "integrations/CiStatus.h"
#include "integrations/OAuthRefresh.h"
#include "integrations/ProviderRegistry.h"
#include "integrations/ReplyError.h"
#include "integrations/RestIssueProvider.h"
#include "integrations/SecretStore.h"
#include "integrations/StatusMap.h"
#include "integrations/TrelloProvider.h"
#include "platform/Paths.h"

#include <QCoreApplication>
#include <QFile>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimeZone>

#include <gtest/gtest.h>

using namespace heap::integrations;

namespace {

QVector<ExternalTask> parseVia(const QString& providerId, const QByteArray& json, const QString& baseUrl = QString()) {
  const ProviderDescriptor* d = findDescriptor(providerId);
  EXPECT_NE(d, nullptr) << "no descriptor for " << providerId.toStdString();
  if(!d) {
    return {};
  }
  if(d->parser) {
    return d->parser(json, baseUrl);
  }
  return parseWithFieldMap(json, d->fields, d->id, baseUrl);
}

}  // namespace

TEST(FieldMapParse, DotPathsBoolStatusAndUrlTemplate) {
  // Exercises nested dot-paths (status.name), an object-array label, and the
  // {baseUrl}/{id} url template — the Redmine shape.
  const QByteArray json = R"({"issues":[
    {"id":42,"subject":"Do thing","description":"body","status":{"name":"In Progress"},
     "priority":{"name":"High"},"updated_on":"2026-01-02T03:04:05Z"}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("redmine"), json, QStringLiteral("https://redmine.example.com/"));
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].providerId, QStringLiteral("redmine"));
  EXPECT_EQ(tasks[0].externalId, QStringLiteral("42"));  // numeric id stringified
  EXPECT_EQ(tasks[0].title, QStringLiteral("Do thing"));
  EXPECT_EQ(tasks[0].status, QStringLiteral("In Progress"));  // dot-path status.name
  EXPECT_EQ(tasks[0].priority, QStringLiteral("High"));
  EXPECT_EQ(tasks[0].url, QStringLiteral("https://redmine.example.com/issues/42"));  // trailing slash trimmed
}

TEST(FieldMapParse, GiteaIssuesWithLabels) {
  const QByteArray json = R"([
    {"number":7,"title":"Fix build","body":"desc","state":"open",
     "html_url":"https://gitea.com/o/r/issues/7","updated_at":"2026-01-02T03:04:05Z",
     "labels":[{"name":"bug"},{"name":"ci"}]}])";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("gitea"), json);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].externalId, QStringLiteral("7"));
  EXPECT_EQ(tasks[0].status, QStringLiteral("open"));
  EXPECT_EQ(tasks[0].url, QStringLiteral("https://gitea.com/o/r/issues/7"));
  ASSERT_EQ(tasks[0].labels.size(), 2);
  EXPECT_EQ(tasks[0].labels[0], QStringLiteral("bug"));
  EXPECT_TRUE(tasks[0].updatedAt.isValid());
}

TEST(FieldMapParse, TodoistBoolStatusFalseIsOpen) {
  const QByteArray json = R"({"results":[
    {"id":"abc","content":"Buy milk","description":"d","is_completed":false,"url":"https://todoist.com/app/task/abc"}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("todoist"), json);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].externalId, QStringLiteral("abc"));
  EXPECT_EQ(tasks[0].title, QStringLiteral("Buy milk"));
  EXPECT_EQ(tasks[0].status, QStringLiteral("open"));  // boolFalseStatus
}

TEST(FieldMapParse, AsanaCompletedIsClosed) {
  const QByteArray json = R"({"data":[
    {"gid":"111","name":"Task A","notes":"n","completed":true,
     "permalink_url":"https://app.asana.com/0/0/111","modified_at":"2026-01-02T03:04:05Z"}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("asana"), json);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].externalId, QStringLiteral("111"));
  EXPECT_EQ(tasks[0].status, QStringLiteral("closed"));  // boolTrueStatus
  EXPECT_EQ(tasks[0].url, QStringLiteral("https://app.asana.com/0/0/111"));
}

TEST(FieldMapParse, ClickUpNestedStatusAndPriority) {
  const QByteArray json = R"({"tasks":[
    {"id":"9x","name":"CU task","description":"d","status":{"status":"in progress"},
     "priority":{"priority":"high"},"url":"https://app.clickup.com/t/9x"}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("clickup"), json);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].status, QStringLiteral("in progress"));
  EXPECT_EQ(tasks[0].priority, QStringLiteral("high"));
}

TEST(FieldMapParse, SentryRootArray) {
  const QByteArray json = R"([
    {"id":"55","title":"TypeError","culprit":"foo","status":"unresolved",
     "permalink":"https://sentry.io/i/55/","lastSeen":"2026-01-02T03:04:05Z"}])";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("sentry"), json);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].externalId, QStringLiteral("55"));
  EXPECT_EQ(tasks[0].status, QStringLiteral("unresolved"));
  EXPECT_EQ(tasks[0].url, QStringLiteral("https://sentry.io/i/55/"));
}

TEST(FieldMapParse, BitbucketDeepDotPaths) {
  const QByteArray json = R"({"values":[
    {"id":3,"title":"BB issue","content":{"raw":"body text"},"state":"new","priority":"major",
     "links":{"html":{"href":"https://bitbucket.org/ws/repo/issues/3"}},"updated_on":"2026-01-02T03:04:05Z"}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("bitbucket"), json);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].externalId, QStringLiteral("3"));
  EXPECT_EQ(tasks[0].body, QStringLiteral("body text"));  // content.raw
  EXPECT_EQ(tasks[0].status, QStringLiteral("new"));
  EXPECT_EQ(tasks[0].priority, QStringLiteral("major"));
  EXPECT_EQ(tasks[0].url, QStringLiteral("https://bitbucket.org/ws/repo/issues/3"));  // links.html.href
}

TEST(FieldMapParse, HandlesEmptyAndMismatchedShape) {
  const ProviderDescriptor* d = findDescriptor(QStringLiteral("bitbucket"));
  ASSERT_NE(d, nullptr);
  EXPECT_TRUE(parseWithFieldMap(QByteArray(), d->fields, d->id, QString()).isEmpty());
  EXPECT_TRUE(parseWithFieldMap("[]", d->fields, d->id, QString()).isEmpty());  // array, wanted object envelope
  EXPECT_TRUE(parseWithFieldMap("not json", d->fields, d->id, QString()).isEmpty());
}

TEST(RegistryDescriptors, GithubAndGitlabReuseTestedParsers) {
  const QByteArray gh = R"([{"number":12,"title":"t","state":"open","html_url":"u","updated_at":"2026-01-02T03:04:05Z"}])";
  const QVector<ExternalTask> ghTasks = parseVia(QStringLiteral("github"), gh);
  ASSERT_EQ(ghTasks.size(), 1);
  EXPECT_EQ(ghTasks[0].providerId, QStringLiteral("github"));
  EXPECT_EQ(ghTasks[0].externalId, QStringLiteral("12"));

  const QByteArray gl = R"([{"iid":42,"title":"t","state":"opened","web_url":"u","updated_at":"2026-01-02T03:04:05Z"}])";
  const QVector<ExternalTask> glTasks = parseVia(QStringLiteral("gitlab"), gl);
  ASSERT_EQ(glTasks.size(), 1);
  EXPECT_EQ(glTasks[0].externalId, QStringLiteral("42"));  // iid, not internal id
}

TEST(RegistryCatalog, HasExpectedProvidersAndSecrets) {
  const auto& cat = providerCatalog();
  EXPECT_GE(cat.size(), 12);
  // Jira and Trello are bespoke; the rest are generic.
  EXPECT_TRUE(findDescriptor(QStringLiteral("jira"))->bespoke);
  EXPECT_TRUE(findDescriptor(QStringLiteral("trello"))->bespoke);
  EXPECT_FALSE(findDescriptor(QStringLiteral("gitea"))->bespoke);
  // Every provider marks its token/key field secret.
  for(const ProviderDescriptor& d : cat) {
    EXPECT_FALSE(d.secretKeys.isEmpty()) << d.id.toStdString();
  }
  EXPECT_EQ(findDescriptor(QStringLiteral("github")), &cat.at(0));
}

TEST(RegistryCatalog, OAuthConfiguredForGitForges) {
  for(const char* id : {"github", "gitlab", "gitea", "forgejo"}) {
    const ProviderDescriptor* d = findDescriptor(QString::fromLatin1(id));
    ASSERT_NE(d, nullptr) << id;
    EXPECT_TRUE(d->oauth.supported) << id;
    EXPECT_FALSE(d->oauth.authUrl.isEmpty()) << id;
    EXPECT_FALSE(d->oauth.tokenUrl.isEmpty()) << id;
  }
  // GitLab/Gitea use PKCE (no secret); GitHub OAuth Apps need a client secret.
  EXPECT_TRUE(findDescriptor(QStringLiteral("gitlab"))->oauth.usePkce);
  EXPECT_TRUE(findDescriptor(QStringLiteral("gitea"))->oauth.usePkce);
  EXPECT_FALSE(findDescriptor(QStringLiteral("github"))->oauth.usePkce);
  // Self-hosted forges keep the {host} placeholder for their endpoints.
  EXPECT_TRUE(findDescriptor(QStringLiteral("gitlab"))->oauth.authUrl.contains(QStringLiteral("{host}")));
  // Redmine has no OAuth at all — its API key is the only way in.
  EXPECT_FALSE(findDescriptor(QStringLiteral("redmine"))->oauth.supported);
}

TEST(RegistryCatalog, ConfidentialProvidersAskForASecret) {
  // These refuse a public client, so one-click only lights up in a build that
  // carries the CI credentials. Each also has its own token-request dialect.
  const struct {
    const char* id;
    TokenStyle style;
  } kCases[] = {
      {"todoist", TokenStyle::FormBody},
      {"asana", TokenStyle::FormBody},
      {"clickup", TokenStyle::JsonBody},
      {"bitbucket", TokenStyle::BasicAuthForm},
  };

  for(const auto& c : kCases) {
    const ProviderDescriptor* d = findDescriptor(QString::fromLatin1(c.id));
    ASSERT_NE(d, nullptr) << c.id;
    EXPECT_TRUE(d->oauth.supported) << c.id;
    EXPECT_TRUE(d->oauth.needsSecret) << c.id;
    EXPECT_EQ(d->oauth.tokenStyle, c.style) << c.id;
    EXPECT_FALSE(d->oauth.authUrl.isEmpty()) << c.id;
    EXPECT_FALSE(d->oauth.tokenUrl.isEmpty()) << c.id;
    EXPECT_EQ(d->oauth.effectiveFlow(), OAuthFlow::AuthCode) << c.id;
    // The client secret goes to the keychain like any other secret, and the
    // Advanced fields let a self-built binary supply its own OAuth app.
    EXPECT_TRUE(d->secretKeys.contains(QStringLiteral("clientSecret"))) << c.id;
    bool hasClientIdField = false;
    for(const FieldSpec& f : d->uiFields) {
      hasClientIdField = hasClientIdField || f.key == QStringLiteral("clientId");
    }
    EXPECT_TRUE(hasClientIdField) << c.id;
  }
  // Todoist's scopes are comma-separated, unlike everyone else's.
  EXPECT_EQ(findDescriptor(QStringLiteral("todoist"))->oauth.scopeSeparator, QStringLiteral(","));
}

TEST(RegistryCatalog, PublicClientsCarryNoSecretAndAskForNone) {
  // Sentry, GitLab and the self-hosted forges accept a public client: PKCE
  // stands in for the secret, so there is nothing confidential to ship and the
  // client ID can be committed. The card must not offer a secret field either —
  // the provider has no such value to give.
  for(const char* id : {"sentry", "gitlab"}) {
    const ProviderDescriptor* d = findDescriptor(QString::fromLatin1(id));
    ASSERT_NE(d, nullptr) << id;
    EXPECT_TRUE(d->oauth.supported) << id;
    EXPECT_FALSE(d->oauth.needsSecret) << id;
    EXPECT_TRUE(d->oauth.usePkce) << id << ": PKCE is what replaces the secret";
    EXPECT_TRUE(d->oauth.clientSecret.isEmpty()) << id;
    EXPECT_FALSE(d->secretKeys.contains(QStringLiteral("clientSecret"))) << id;
    for(const FieldSpec& f : d->uiFields) {
      EXPECT_NE(f.key, QStringLiteral("clientSecret")) << id << ": asks for a secret that does not exist";
    }
  }

  // Sentry's ID is committed rather than injected from CI, which is what lets
  // browser sign-in work in a build made without the release credentials —
  // this very one.
  const ProviderDescriptor* sentry = findDescriptor(QStringLiteral("sentry"));
  ASSERT_NE(sentry, nullptr);
  EXPECT_FALSE(sentry->oauth.clientId.isEmpty()) << "a committed client ID is the point of the public client";
  EXPECT_EQ(sentry->oauth.tokenStyle, TokenStyle::FormBody);
  EXPECT_EQ(sentry->oauth.effectiveFlow(), OAuthFlow::AuthCode);
}

TEST(RegistryCatalog, JiraAndTrelloUseTheirOwnFlows) {
  // Atlassian has no PKCE-only mode, so Jira needs a secret; `audience` picks
  // the Jira API and `prompt=consent` is what makes it issue a refresh token.
  const ProviderDescriptor* jira = findDescriptor(QStringLiteral("jira"));
  ASSERT_NE(jira, nullptr);
  EXPECT_TRUE(jira->oauth.supported);
  EXPECT_TRUE(jira->oauth.needsSecret);
  EXPECT_EQ(jira->oauth.tokenStyle, TokenStyle::JsonBody);
  EXPECT_TRUE(jira->oauth.scope.contains(QStringLiteral("offline_access")));
  bool hasAudience = false;
  bool hasPrompt = false;
  for(const auto& kv : jira->oauth.extraAuthParams) {
    hasAudience = hasAudience || (kv.first == QStringLiteral("audience") && kv.second == QStringLiteral("api.atlassian.com"));
    hasPrompt = hasPrompt || (kv.first == QStringLiteral("prompt") && kv.second == QStringLiteral("consent"));
  }
  EXPECT_TRUE(hasAudience);
  EXPECT_TRUE(hasPrompt);
  // Only the token is always required now — browser sign-in supplies the site,
  // and JiraProvider checks the Basic-auth pair itself.
  EXPECT_FALSE(jira->requiredKeys.contains(QStringLiteral("email")));
  EXPECT_TRUE(jira->requiredKeys.contains(QStringLiteral("token")));

  // Trello answers with the token in the URL fragment: no code, no exchange,
  // no secret — only the public app key, under its own query name.
  const ProviderDescriptor* trello = findDescriptor(QStringLiteral("trello"));
  ASSERT_NE(trello, nullptr);
  EXPECT_TRUE(trello->oauth.supported);
  EXPECT_EQ(trello->oauth.effectiveFlow(), OAuthFlow::ImplicitFragment);
  EXPECT_FALSE(trello->oauth.needsSecret);
  EXPECT_TRUE(trello->oauth.tokenUrl.isEmpty());
  EXPECT_FALSE(trello->oauth.usePkce);
  EXPECT_EQ(trello->oauth.clientIdParam, QStringLiteral("key"));
  EXPECT_EQ(trello->oauth.redirectParam, QStringLiteral("return_url"));
  EXPECT_EQ(trello->oauth.scope, QStringLiteral("read")) << "sync is pull-only";
  bool asksForAFragment = false;
  for(const auto& kv : trello->oauth.extraAuthParams) {
    asksForAFragment = asksForAFragment || (kv.first == QStringLiteral("callback_method") && kv.second == QStringLiteral("fragment"));
  }
  EXPECT_TRUE(asksForAFragment);
}

TEST(RegistryCatalog, EveryOAuthProviderCarriesEnoughToRunTheFlow) {
  for(const ProviderDescriptor& d : providerCatalog()) {
    if(!d.oauth.supported) {
      continue;
    }
    const bool device = d.oauth.effectiveFlow() == OAuthFlow::Device;
    const bool fragment = d.oauth.effectiveFlow() == OAuthFlow::ImplicitFragment;
    EXPECT_FALSE(device ? d.oauth.deviceAuthUrl.isEmpty() : d.oauth.authUrl.isEmpty()) << d.id.toStdString();
    // Only the fragment flow skips the token endpoint — it is handed the token.
    EXPECT_EQ(d.oauth.tokenUrl.isEmpty(), fragment) << d.id.toStdString();
    EXPECT_FALSE(d.oauth.clientIdParam.isEmpty()) << d.id.toStdString();
    EXPECT_FALSE(d.oauth.redirectParam.isEmpty()) << d.id.toStdString();
    EXPECT_FALSE(d.oauth.scopeSeparator.isEmpty()) << d.id.toStdString();
  }
}

TEST(RegistryCatalog, SelfEndpointMakesScopeOptional) {
  // GitHub/GitLab expose a "my issues" endpoint so an OAuth-connected user needs
  // no repo/project — the scoping field is dropped from requiredKeys.
  const ProviderDescriptor* gh = findDescriptor(QStringLiteral("github"));
  ASSERT_NE(gh, nullptr);
  EXPECT_EQ(gh->scopeKey, QStringLiteral("repo"));
  EXPECT_FALSE(gh->selfListPathTemplate.isEmpty());
  EXPECT_FALSE(gh->requiredKeys.contains(QStringLiteral("repo")));
  EXPECT_TRUE(gh->requiredKeys.contains(QStringLiteral("token")));

  const ProviderDescriptor* gl = findDescriptor(QStringLiteral("gitlab"));
  ASSERT_NE(gl, nullptr);
  EXPECT_EQ(gl->scopeKey, QStringLiteral("projectId"));
  EXPECT_FALSE(gl->selfListPathTemplate.isEmpty());
  EXPECT_FALSE(gl->requiredKeys.contains(QStringLiteral("projectId")));
  EXPECT_EQ(gl->baseUrlFallback, QStringLiteral("https://gitlab.com"));  // gitlab.com default
}

TEST(TrelloParse, ListsResolveCardStatus) {
  const QByteArray lists = R"([{"id":"L1","name":"In Progress"},{"id":"L2","name":"Done"}])";
  const QHash<QString, QString> byId = parseTrelloLists(lists);
  ASSERT_EQ(byId.size(), 2);
  EXPECT_EQ(byId.value(QStringLiteral("L1")), QStringLiteral("In Progress"));

  const QByteArray cards = R"([
    {"id":"c1","name":"Card one","desc":"d","idList":"L1","url":"https://trello.com/c/c1",
     "dateLastActivity":"2026-01-02T03:04:05Z","labels":[{"name":"red"}]},
    {"id":"c2","name":"Card two","idList":"unknown","url":"https://trello.com/c/c2"}])";
  const QVector<ExternalTask> tasks = parseTrelloCards(cards, byId);
  ASSERT_EQ(tasks.size(), 2);
  EXPECT_EQ(tasks[0].providerId, QStringLiteral("trello"));
  EXPECT_EQ(tasks[0].externalId, QStringLiteral("c1"));
  EXPECT_EQ(tasks[0].status, QStringLiteral("In Progress"));  // resolved from idList
  ASSERT_EQ(tasks[0].labels.size(), 1);
  EXPECT_EQ(tasks[1].status, QString());  // unknown list → empty (StatusMap falls back)
}

TEST(TrelloParse, HandlesGarbage) {
  EXPECT_TRUE(parseTrelloLists("{}").isEmpty());
  EXPECT_TRUE(parseTrelloCards("not json", {}).isEmpty());
}

// The fallback file holds access tokens and OAuth refresh tokens in plain
// text. It is what a build without QtKeychain uses, and what every --data-dir
// run uses regardless. QSaveFile renames a temp file into place, so without an
// explicit call the result inherits the default mask or the parent's ACL.
TEST(SecretStoreCache, TheFallbackFileIsReadableOnlyByItsOwner) {
  const QString path = heap::paths::dataDir() + QStringLiteral("/secrets.json");
  QFile::remove(path);

  SecretStore store;
  store.setValue(QStringLiteral("github"), QStringLiteral("token"), QStringLiteral("ghp_secret"));
  ASSERT_TRUE(QFile::exists(path)) << "the fallback file was not written to " << path.toStdString();

  const QFileDevice::Permissions perms = QFile::permissions(path);
  EXPECT_TRUE(perms.testFlag(QFileDevice::ReadOwner));
  EXPECT_TRUE(perms.testFlag(QFileDevice::WriteOwner)) << "the app locked itself out of its own secrets";

#ifndef Q_OS_WIN
  // Only asserted off Windows: there QFile::permissions() reports a guess
  // rather than the ACL unless NTFS permission lookup is switched on, so the
  // group/other bits it returns say nothing about who can actually read the
  // file. The exposure this guards is the POSIX one — a keychain-less Linux
  // build writing tokens at whatever the umask allows.
  for(const QFileDevice::Permission p : {QFileDevice::ReadGroup,
                                         QFileDevice::WriteGroup,
                                         QFileDevice::ExeGroup,
                                         QFileDevice::ReadOther,
                                         QFileDevice::WriteOther,
                                         QFileDevice::ExeOther}) {
    EXPECT_FALSE(perms.testFlag(p)) << "secrets.json is reachable beyond its owner";
  }
#endif

  // Rewriting the file must not widen it again.
  store.setValue(QStringLiteral("gitlab"), QStringLiteral("token"), QStringLiteral("glpat_secret"));
  EXPECT_TRUE(QFile::permissions(path).testFlag(QFileDevice::ReadOwner));
#ifndef Q_OS_WIN
  EXPECT_FALSE(QFile::permissions(path).testFlag(QFileDevice::ReadOther));
#endif

  store.setValue(QStringLiteral("github"), QStringLiteral("token"), QString());
  store.setValue(QStringLiteral("gitlab"), QStringLiteral("token"), QString());
  QFile::remove(path);
}

TEST(TrelloParse, DueBadgesMembersAndCreatedFromId) {
  // A Trello object id starts with the creation time in hex seconds:
  // 0x67b5e880 = 2025-02-19T…
  const QByteArray cards = R"([
    {"id":"67b5e880aaaabbbbccccdddd","name":"Card","idList":"L1","url":"https://trello.com/c/x",
     "due":"2026-08-15T17:00:00.000Z","badges":{"comments":5},
     "members":[{"fullName":"Ada Lovelace"}],
     "labels":[{"name":"urgent","color":"red_dark"},{"name":"nocolor","color":""}]}])";
  const QVector<ExternalTask> tasks = parseTrelloCards(cards, {});
  ASSERT_EQ(tasks.size(), 1);
  const ExternalTask& t = tasks[0];
  EXPECT_EQ(t.commentCount, 5);
  EXPECT_EQ(t.assignee, QStringLiteral("Ada Lovelace"));
  ASSERT_TRUE(t.dueAt.isValid());
  EXPECT_TRUE(t.dueHasTime);  // a real instant, not an all-day date
  EXPECT_EQ(t.dueAt.toUTC().date(), QDate(2026, 8, 15));
  ASSERT_TRUE(t.createdAt.isValid());
  EXPECT_EQ(t.createdAt.toUTC().date(), QDate(2025, 2, 19));
  // Trello names its colours; heap needs hex.
  EXPECT_EQ(t.labelColors.value(QStringLiteral("urgent")), QStringLiteral("#eb5a46"));
  EXPECT_FALSE(t.labelColors.contains(QStringLiteral("nocolor")));
}

// ── HEAP-117: generic FieldMap extraction ──

TEST(FieldMapParse, ArrayIndexAndFallbackPath) {
  FieldMap map;
  map.id = QStringLiteral("id");
  map.title = QStringLiteral("name");
  map.assignee = QStringLiteral("assignees.0.login|assignee.login");
  map.project = QStringLiteral("repo.full_name");

  // The indexed path resolves.
  const QByteArray withArray = R"([{"id":"1","name":"t","assignees":[{"login":"first"},{"login":"second"}]}])";
  QVector<ExternalTask> tasks = parseWithFieldMap(withArray, map, QStringLiteral("x"), QString());
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].assignee, QStringLiteral("first"));

  // An empty array falls through to the second alternative.
  const QByteArray withSingular = R"([{"id":"1","name":"t","assignees":[],"assignee":{"login":"solo"}}])";
  tasks = parseWithFieldMap(withSingular, map, QStringLiteral("x"), QString());
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].assignee, QStringLiteral("solo"));

  // Neither present → empty, and an out-of-range index does not crash.
  const QByteArray withNeither = R"([{"id":"1","name":"t","assignees":[]}])";
  tasks = parseWithFieldMap(withNeither, map, QStringLiteral("x"), QString());
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_TRUE(tasks[0].assignee.isEmpty());
}

TEST(FieldMapParse, TimestampShapes) {
  bool hasTime = true;
  // A bare date is a day: local midnight, no clock.
  const QDateTime day = parseTrackerTimestamp(QJsonValue(QStringLiteral("2026-08-15")), &hasTime);
  ASSERT_TRUE(day.isValid());
  EXPECT_EQ(day.date(), QDate(2026, 8, 15));
  EXPECT_EQ(day.time(), QTime(0, 0));
  EXPECT_FALSE(hasTime);

  // An instant keeps its clock.
  const QDateTime instant = parseTrackerTimestamp(QJsonValue(QStringLiteral("2026-08-15T17:30:00Z")), &hasTime);
  ASSERT_TRUE(instant.isValid());
  EXPECT_TRUE(hasTime);
  EXPECT_EQ(instant.toUTC().time(), QTime(17, 30));

  // Epoch milliseconds, as a number and as a string (ClickUp sends strings).
  const QDateTime fromNumber = parseTrackerTimestamp(QJsonValue(qint64(1755277800000)));
  ASSERT_TRUE(fromNumber.isValid());
  const QDateTime fromString = parseTrackerTimestamp(QJsonValue(QStringLiteral("1755277800000")));
  EXPECT_EQ(fromNumber, fromString);

  // Nothing, and nonsense, are both "no date" rather than a crash or an epoch.
  EXPECT_FALSE(parseTrackerTimestamp(QJsonValue()).isValid());
  EXPECT_FALSE(parseTrackerTimestamp(QJsonValue(QStringLiteral(""))).isValid());
  EXPECT_FALSE(parseTrackerTimestamp(QJsonValue(QStringLiteral("not a date"))).isValid());
  // A small number is a count, not an epoch.
  EXPECT_FALSE(parseTrackerTimestamp(QJsonValue(42)).isValid());
}

TEST(FieldMapParse, NormalizeHexColorAndSanitizeProject) {
  EXPECT_EQ(normalizeHexColor(QStringLiteral("d73a4a")), QStringLiteral("#d73a4a"));
  EXPECT_EQ(normalizeHexColor(QStringLiteral("#D73A4A")), QStringLiteral("#D73A4A"));
  EXPECT_EQ(normalizeHexColor(QStringLiteral("#fff")), QStringLiteral("#fff"));
  // A colour name would reach a QML colour property unvalidated; drop it.
  EXPECT_TRUE(normalizeHexColor(QStringLiteral("green")).isEmpty());
  EXPECT_TRUE(normalizeHexColor(QStringLiteral("")).isEmpty());

  EXPECT_EQ(sanitizeProject(QStringLiteral("acme/web")), QStringLiteral("acme/web"));
  EXPECT_EQ(sanitizeProject(QStringLiteral("group/sub/app")), QStringLiteral("group/sub/app"));
  EXPECT_EQ(sanitizeProject(QStringLiteral("PROJ")), QStringLiteral("PROJ"));
  EXPECT_EQ(sanitizeProject(QStringLiteral("dot.net.core")), QStringLiteral("dot.net.core"));
  // The value is spliced into URL paths and task ids.
  EXPECT_TRUE(sanitizeProject(QStringLiteral("acme/../../evil")).isEmpty());
  EXPECT_TRUE(sanitizeProject(QStringLiteral("acme/./web")).isEmpty());
  EXPECT_TRUE(sanitizeProject(QStringLiteral("acme web")).isEmpty());
  EXPECT_TRUE(sanitizeProject(QStringLiteral("acme?x=1")).isEmpty());
  EXPECT_TRUE(sanitizeProject(QStringLiteral("https://evil/acme")).isEmpty());
}

TEST(FieldMapParse, GiteaAssigneeRepoAndBareHexColor) {
  const QByteArray json = R"([
    {"number":7,"title":"Fix build","state":"open","html_url":"https://gitea.com/o/r/issues/7",
     "updated_at":"2026-01-02T03:04:05Z","created_at":"2025-12-01T00:00:00Z","due_date":"2026-03-01T00:00:00Z",
     "comments":2,"user":{"login":"rep"},"assignees":[{"login":"dev"}],
     "repository":{"full_name":"o/r"},"milestone":{"title":"v1"},
     "labels":[{"name":"bug","color":"d73a4a"}]}])";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("gitea"), json);
  ASSERT_EQ(tasks.size(), 1);
  const ExternalTask& t = tasks[0];
  EXPECT_EQ(t.assignee, QStringLiteral("dev"));
  EXPECT_EQ(t.author, QStringLiteral("rep"));
  EXPECT_EQ(t.commentCount, 2);
  EXPECT_EQ(t.project, QStringLiteral("o/r"));
  EXPECT_EQ(t.milestone, QStringLiteral("v1"));
  EXPECT_TRUE(t.dueAt.isValid());
  EXPECT_EQ(t.labelColors.value(QStringLiteral("bug")), QStringLiteral("#d73a4a"));
}

TEST(FieldMapParse, ClickUpEpochMillisecondDatesAndTagColors) {
  const QByteArray json = R"({"tasks":[
    {"id":"9x","name":"CU task","status":{"status":"in progress"},"url":"https://app.clickup.com/t/9x",
     "date_updated":"1755277800000","date_created":"1740000000000",
     "due_date":"1755277800000","due_date_time":true,
     "assignees":[{"username":"dev"}],"creator":{"username":"rep"},
     "list":{"name":"Sprint"},
     "tags":[{"name":"infra","tag_bg":"#7b68ee"}]}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("clickup"), json);
  ASSERT_EQ(tasks.size(), 1);
  const ExternalTask& t = tasks[0];
  EXPECT_TRUE(t.updatedAt.isValid());
  EXPECT_TRUE(t.createdAt.isValid());
  ASSERT_TRUE(t.dueAt.isValid());
  EXPECT_TRUE(t.dueHasTime);
  EXPECT_EQ(t.assignee, QStringLiteral("dev"));
  EXPECT_EQ(t.author, QStringLiteral("rep"));
  EXPECT_EQ(t.project, QStringLiteral("Sprint"));
  ASSERT_EQ(t.labels.size(), 1);
  EXPECT_EQ(t.labelColors.value(QStringLiteral("infra")), QStringLiteral("#7b68ee"));
}

TEST(FieldMapParse, ClickUpAllDayDueDateHasNoClock) {
  // due_date_time=false means the epoch value is an all-day marker.
  const QByteArray json = R"({"tasks":[
    {"id":"9x","name":"t","status":{"status":"open"},"due_date":"1755277800000","due_date_time":false}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("clickup"), json);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_TRUE(tasks[0].dueAt.isValid());
  EXPECT_FALSE(tasks[0].dueHasTime);
}

TEST(FieldMapParse, AsanaDueAtBeatsDueOn) {
  const QByteArray json = R"({"data":[
    {"gid":"111","name":"Task A","completed":false,"permalink_url":"https://app.asana.com/0/0/111",
     "modified_at":"2026-01-02T03:04:05Z","created_at":"2025-12-01T00:00:00Z",
     "due_on":"2026-08-15","due_at":"2026-08-15T17:00:00.000Z",
     "assignee":{"name":"Ada"},"created_by":{"name":"Grace"},
     "projects":[{"name":"Roadmap"}],"resource_subtype":"default_task"}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("asana"), json);
  ASSERT_EQ(tasks.size(), 1);
  const ExternalTask& t = tasks[0];
  ASSERT_TRUE(t.dueAt.isValid());
  EXPECT_TRUE(t.dueHasTime) << "due_at is an instant and should win over due_on";
  EXPECT_EQ(t.assignee, QStringLiteral("Ada"));
  EXPECT_EQ(t.author, QStringLiteral("Grace"));
  EXPECT_EQ(t.project, QStringLiteral("Roadmap"));
}

TEST(FieldMapParse, AsanaFallsBackToDueOn) {
  const QByteArray json = R"({"data":[
    {"gid":"111","name":"t","completed":false,"due_on":"2026-08-15"}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("asana"), json);
  ASSERT_EQ(tasks.size(), 1);
  ASSERT_TRUE(tasks[0].dueAt.isValid());
  EXPECT_EQ(tasks[0].dueAt.date(), QDate(2026, 8, 15));
  EXPECT_FALSE(tasks[0].dueHasTime);
}

TEST(FieldMapParse, TodoistDueVariantsAndEitherCompletionKey) {
  // v1 renamed is_completed to checked; both spellings have to work.
  const QByteArray legacy = R"({"results":[{"id":"a","content":"t","is_completed":true}]})";
  EXPECT_EQ(parseVia(QStringLiteral("todoist"), legacy)[0].status, QStringLiteral("closed"));
  const QByteArray v1 = R"({"results":[{"id":"a","content":"t","checked":true}]})";
  EXPECT_EQ(parseVia(QStringLiteral("todoist"), v1)[0].status, QStringLiteral("closed"));

  const QByteArray dated = R"({"results":[
    {"id":"a","content":"t","checked":false,"added_at":"2025-12-01T00:00:00Z","note_count":2,
     "due":{"date":"2026-08-15"}}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("todoist"), dated);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_EQ(tasks[0].status, QStringLiteral("open"));
  EXPECT_EQ(tasks[0].commentCount, 2);
  EXPECT_TRUE(tasks[0].createdAt.isValid());
  ASSERT_TRUE(tasks[0].dueAt.isValid());
  EXPECT_FALSE(tasks[0].dueHasTime);
  // Todoist's 1–4 priority has no StatusMap name, so it must not be mapped at
  // all — mapping it would resolve to the fallback and overwrite the user's.
  EXPECT_TRUE(tasks[0].priority.isEmpty());

  const QByteArray timed = R"({"results":[
    {"id":"a","content":"t","checked":false,"due":{"date":"2026-08-15","datetime":"2026-08-15T17:00:00Z"}}]})";
  const QVector<ExternalTask> timedTasks = parseVia(QStringLiteral("todoist"), timed);
  ASSERT_EQ(timedTasks.size(), 1);
  EXPECT_TRUE(timedTasks[0].dueHasTime);
}

TEST(FieldMapParse, SentryAssignedToLevelAndCounts) {
  const QByteArray json = R"([
    {"id":"55","title":"TypeError","culprit":"foo","status":"unresolved",
     "permalink":"https://sentry.io/i/55/","lastSeen":"2026-01-02T03:04:05Z",
     "firstSeen":"2025-11-01T00:00:00Z","numComments":3,"level":"error",
     "assignedTo":{"name":"Ada"},"project":{"slug":"backend"}}])";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("sentry"), json);
  ASSERT_EQ(tasks.size(), 1);
  const ExternalTask& t = tasks[0];
  EXPECT_EQ(t.assignee, QStringLiteral("Ada"));
  EXPECT_EQ(t.commentCount, 3);
  EXPECT_EQ(t.issueType, QStringLiteral("error"));
  EXPECT_EQ(t.project, QStringLiteral("backend"));
  EXPECT_TRUE(t.createdAt.isValid());
}

TEST(FieldMapParse, RedmineTrackerProjectAndVersion) {
  const QByteArray json = R"({"issues":[
    {"id":42,"subject":"Do thing","status":{"name":"New"},"updated_on":"2026-01-02T03:04:05Z",
     "created_on":"2025-12-01T00:00:00Z","due_date":"2026-03-01",
     "assigned_to":{"name":"Ada"},"author":{"name":"Grace"},
     "tracker":{"name":"Feature"},"project":{"name":"Core"},"fixed_version":{"name":"2.1"}}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("redmine"), json, QStringLiteral("https://r.example.com"));
  ASSERT_EQ(tasks.size(), 1);
  const ExternalTask& t = tasks[0];
  EXPECT_EQ(t.assignee, QStringLiteral("Ada"));
  EXPECT_EQ(t.author, QStringLiteral("Grace"));
  EXPECT_EQ(t.issueType, QStringLiteral("Feature"));
  EXPECT_EQ(t.project, QStringLiteral("Core"));
  EXPECT_EQ(t.milestone, QStringLiteral("2.1"));
  ASSERT_TRUE(t.dueAt.isValid());
  EXPECT_FALSE(t.dueHasTime);
  // Journals need a per-issue request, so the count stays unknown.
  EXPECT_EQ(t.commentCount, -1);
}

TEST(FieldMapParse, BitbucketKindReporterAndMilestone) {
  const QByteArray json = R"({"values":[
    {"id":3,"title":"BB issue","content":{"raw":"body"},"state":"new","priority":"major",
     "links":{"html":{"href":"https://bitbucket.org/ws/repo/issues/3"}},
     "updated_on":"2026-01-02T03:04:05Z","created_on":"2025-12-01T00:00:00Z",
     "assignee":{"display_name":"Ada"},"reporter":{"display_name":"Grace"},
     "kind":"bug","repository":{"full_name":"ws/repo"},"milestone":{"name":"M1"}}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("bitbucket"), json);
  ASSERT_EQ(tasks.size(), 1);
  const ExternalTask& t = tasks[0];
  EXPECT_EQ(t.assignee, QStringLiteral("Ada"));
  EXPECT_EQ(t.author, QStringLiteral("Grace"));
  EXPECT_EQ(t.issueType, QStringLiteral("bug"));
  EXPECT_EQ(t.project, QStringLiteral("ws/repo"));
  EXPECT_EQ(t.milestone, QStringLiteral("M1"));
}

TEST(FieldMapParse, AProjectThatIsNotAPlainPathIsDropped) {
  const QByteArray json = R"({"values":[
    {"id":3,"title":"t","state":"new","repository":{"full_name":"ws/../../etc"}}]})";
  const QVector<ExternalTask> tasks = parseVia(QStringLiteral("bitbucket"), json);
  ASSERT_EQ(tasks.size(), 1);
  EXPECT_TRUE(tasks[0].project.isEmpty());
}

TEST(FieldMapParse, GitlabListRequestsLabelDetails) {
  // The colour only arrives when the list endpoint asks for it.
  const ProviderDescriptor* d = findDescriptor(QStringLiteral("gitlab"));
  ASSERT_NE(d, nullptr);
  EXPECT_TRUE(d->listPathTemplate.contains(QStringLiteral("with_labels_details=true")));
  EXPECT_TRUE(d->selfListPathTemplate.contains(QStringLiteral("with_labels_details=true")));
}

// ── HEAP-117: read-only comments ──

TEST(CommentParse, GithubShapeIsExtractedAndEmptyBodiesDropped) {
  const ProviderDescriptor* d = findDescriptor(QStringLiteral("github"));
  ASSERT_NE(d, nullptr);
  const QByteArray json = R"([
    {"user":{"login":"ada"},"body":"first","created_at":"2026-01-02T03:04:05Z",
     "html_url":"https://github.com/acme/web/issues/1#issuecomment-1"},
    {"user":{"login":"grace"},"body":"","created_at":"2026-01-03T00:00:00Z"},
    {"user":{"login":"grace"},"body":"second","created_at":"2026-01-04T00:00:00Z"}])";
  const QVector<ExternalComment> comments = parseCommentsWithMap(json, d->comments);
  ASSERT_EQ(comments.size(), 2) << "an empty body was kept";
  EXPECT_EQ(comments[0].author, QStringLiteral("ada"));
  EXPECT_EQ(comments[0].body, QStringLiteral("first"));
  EXPECT_TRUE(comments[0].createdAt.isValid());
  EXPECT_FALSE(comments[0].url.isEmpty());
  // The endpoint has no sort parameter, so the provider reverses afterwards.
  EXPECT_TRUE(d->comments.newestLast);
}

TEST(CommentParse, GitlabSystemNotesAreSkipped) {
  const ProviderDescriptor* d = findDescriptor(QStringLiteral("gitlab"));
  ASSERT_NE(d, nullptr);
  const QByteArray json = R"([
    {"author":{"username":"ada"},"body":"a real comment","created_at":"2026-01-04T00:00:00Z","system":false},
    {"author":{"username":"ada"},"body":"changed the description","created_at":"2026-01-03T00:00:00Z","system":true}])";
  const QVector<ExternalComment> comments = parseCommentsWithMap(json, d->comments);
  ASSERT_EQ(comments.size(), 1) << "a system note was shown as a comment";
  EXPECT_EQ(comments[0].body, QStringLiteral("a real comment"));
  // GitLab sorts server-side, so nothing is reversed.
  EXPECT_FALSE(d->comments.newestLast);
}

TEST(CommentParse, HandlesGarbageAndAnEmptyList) {
  const ProviderDescriptor* d = findDescriptor(QStringLiteral("github"));
  ASSERT_NE(d, nullptr);
  EXPECT_TRUE(parseCommentsWithMap(QByteArray(), d->comments).isEmpty());
  EXPECT_TRUE(parseCommentsWithMap("not json", d->comments).isEmpty());
  EXPECT_TRUE(parseCommentsWithMap("[]", d->comments).isEmpty());
  EXPECT_TRUE(parseCommentsWithMap("{}", d->comments).isEmpty());
}

TEST(CommentParse, OnlyProvidersWithAKnownEndpointDeclareOne) {
  // A descriptor with a path must also say how to read the response, and one
  // without a path answers "unsupported" rather than issuing a request.
  for(const ProviderDescriptor& d : providerCatalog()) {
    if(d.commentsPathTemplate.isEmpty()) {
      continue;
    }
    EXPECT_FALSE(d.comments.body.isEmpty()) << d.id.toStdString() << " has a comments path but no body mapping";
    EXPECT_FALSE(d.comments.author.isEmpty()) << d.id.toStdString();
    EXPECT_TRUE(d.commentsPathTemplate.contains(QStringLiteral("{externalId}"))) << d.id.toStdString();
  }
  // Jira handles its own; the pull-only providers have none.
  for(const char* id : {"github", "gitlab", "gitea", "forgejo"}) {
    const ProviderDescriptor* d = findDescriptor(QLatin1String(id));
    ASSERT_NE(d, nullptr);
    EXPECT_FALSE(d->commentsPathTemplate.isEmpty()) << id << " lost its comments endpoint";
  }
  for(const char* id : {"todoist", "asana", "clickup", "sentry", "bitbucket", "redmine"}) {
    const ProviderDescriptor* d = findDescriptor(QLatin1String(id));
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(d->commentsPathTemplate.isEmpty()) << id << " declares comments heap cannot read";
  }
}

TEST(FieldMapParse, AsanaRequestsTheFieldsItParsesAndNoUnscopedOnes) {
  const ProviderDescriptor* d = findDescriptor(QStringLiteral("asana"));
  ASSERT_NE(d, nullptr);
  // Asana returns only what opt_fields names.
  for(const char* field : {"created_at", "due_on", "due_at", "assignee.name", "created_by.name", "projects.name", "resource_subtype"}) {
    EXPECT_TRUE(d->listPathTemplate.contains(QLatin1String(field))) << field << " missing from the Asana opt_fields";
  }
  // tags:read is not in the scope list, and an unscoped opt_field fails the
  // whole request rather than omitting that one field.
  EXPECT_FALSE(d->listPathTemplate.contains(QStringLiteral("tags")));
}

TEST(SecretStoreCache, SetValueGetHasRemove) {
  SecretStore store;
  store.setValue(QStringLiteral("github"), QStringLiteral("token"), QStringLiteral("ghp_secret"));
  EXPECT_EQ(store.value(QStringLiteral("github"), QStringLiteral("token")), QStringLiteral("ghp_secret"));
  EXPECT_TRUE(store.has(QStringLiteral("github"), QStringLiteral("token")));

  // Setting empty clears it.
  store.setValue(QStringLiteral("github"), QStringLiteral("token"), QString());
  EXPECT_FALSE(store.has(QStringLiteral("github"), QStringLiteral("token")));

  store.setValue(QStringLiteral("jira"), QStringLiteral("token"), QStringLiteral("x"));
  store.remove(QStringLiteral("jira"), QStringLiteral("token"));
  EXPECT_FALSE(store.has(QStringLiteral("jira"), QStringLiteral("token")));
}

// ── Error reporting (ReplyError.h) ──────────────────────────────────────────
// A failed request must name the status and the tracker's own message; before
// this, every failure surfaced as Qt's "server replied: Unauthorized" — or, for
// a pull, as "Synced 0 issue(s)".

TEST(ReplyError, JiraErrorMessagesArray) {
  const QByteArray body = R"({"errorMessages":["Unbounded JQL queries are not allowed here."],"errors":{}})";
  EXPECT_EQ(describeHttpError(400, body, QStringLiteral("ignored")),
            QStringLiteral("HTTP 400 — Unbounded JQL queries are not allowed here."));
}

TEST(ReplyError, JiraFieldErrorsKeepTheFieldName) {
  const QByteArray body = R"({"errorMessages":[],"errors":{"jql":"Field 'foo' does not exist."}})";
  EXPECT_EQ(describeHttpError(400, body, QString()), QStringLiteral("HTTP 400 — jql: Field 'foo' does not exist."));
}

TEST(ReplyError, GithubAndGitlabMessageKeys) {
  EXPECT_EQ(describeHttpError(401, R"({"message":"Bad credentials"})", QString()), QStringLiteral("HTTP 401 — Bad credentials"));
  EXPECT_EQ(describeHttpError(401, R"({"error":"invalid_token","error_description":"Token is expired"})", QString()),
            QStringLiteral("HTTP 401 — Token is expired"));
}

TEST(ReplyError, NestedErrorsArray) {
  // Asana / Gitea shape.
  EXPECT_EQ(describeHttpError(403, R"({"errors":[{"message":"Not the right scope"}]})", QString()),
            QStringLiteral("HTTP 403 — Not the right scope"));
}

TEST(ReplyError, FallsBackToAStatusHint) {
  // No body at all: the status code still says something actionable.
  EXPECT_EQ(describeHttpError(401, {}, QString()), QStringLiteral("HTTP 401 — unauthorized — check the token"));
  EXPECT_EQ(describeHttpError(404, {}, QString()), QStringLiteral("HTTP 404 — not found — check the URL, project or repo"));
  // Unmapped status with no body → whatever Qt said.
  EXPECT_EQ(describeHttpError(500, {}, QStringLiteral("Internal Server Error")), QStringLiteral("HTTP 500 — Internal Server Error"));
}

TEST(ReplyError, NonJsonBodies) {
  // Trello answers in plain text.
  EXPECT_EQ(describeHttpError(401, "invalid key", QString()), QStringLiteral("HTTP 401 — invalid key"));
  // An HTML error page is markup, not a message — fall back to the hint.
  EXPECT_EQ(describeHttpError(403, "<html><body>Forbidden</body></html>", QString()),
            QStringLiteral("HTTP 403 — forbidden — the token is missing a required scope"));
}

TEST(ReplyError, NoStatusMeansTheRequestNeverLanded) {
  // DNS failure / offline: no HTTP status to prefix.
  EXPECT_EQ(describeHttpError(0, {}, QStringLiteral("Host acme.atlassian.net not found")),
            QStringLiteral("Host acme.atlassian.net not found"));
}

TEST(ReplyError, LongMessagesAreClamped) {
  const QByteArray body = QByteArray(R"({"message":")") + QByteArray(400, 'x') + R"("})";
  const QString out = describeHttpError(500, body, QString());
  EXPECT_TRUE(out.endsWith(QStringLiteral("…")));
  EXPECT_LT(out.size(), 230);
}

TEST(ReplyError, CredentialsInTheRequestUrlNeverReachTheMessage) {
  // Qt's errorString() quotes the whole URL, and Trello carries key + token in
  // the query string. The message ends up in a toast and in the log file.
  const QString qtError =
      QStringLiteral("Error transferring https://api.trello.com/1/members/me?key=APPKEY123&token=SECRET456 - server replied: Unauthorized");
  // Both routes to the fallback text: no response at all (0), and a status with
  // neither a usable body nor a built-in hint (418 + an HTML page).
  for(const int status : {0, 418}) {
    const QString out = describeHttpError(status, "<html>teapot</html>", qtError);
    EXPECT_FALSE(out.contains(QStringLiteral("SECRET456"))) << out.toStdString();
    EXPECT_FALSE(out.contains(QStringLiteral("APPKEY123"))) << out.toStdString();
    EXPECT_TRUE(out.contains(QStringLiteral("https://api.trello.com/1/members/me"))) << out.toStdString();
    EXPECT_TRUE(out.contains(QStringLiteral("server replied: Unauthorized"))) << out.toStdString();
  }
  // A URL without a query is left alone.
  EXPECT_EQ(describeHttpError(0, {}, QStringLiteral("Error transferring https://gitlab.com/api/v4/issues - timeout")),
            QStringLiteral("Error transferring https://gitlab.com/api/v4/issues - timeout"));
}

// ── A 401 Qt reports as an authentication error ─────────────────────────────
// Qt turns every 401 into AuthenticationRequiredError, whose errorString is
// "Host requires authentication" — and when it gives up before recording the
// status attribute, the status reads 0. That is the only signal two retries
// key on (Jira's scoped-token gateway fallback, the OAuth refresh-on-401), and
// it left the user staring at Qt's words with nothing to change.

TEST(ReplyStatus, QtAuthErrorsAreAStatusInTheirOwnRight) {
  using heap::integrations::detail::httpStatusFor;
  // No status recorded: the error is all there is to go on.
  EXPECT_EQ(httpStatusFor(0, QNetworkReply::AuthenticationRequiredError), 401);
  EXPECT_EQ(httpStatusFor(0, QNetworkReply::ProxyAuthenticationRequiredError), 407);
  // A recorded status always wins — Qt raises the auth error alongside a real
  // 401 too, and inventing one for, say, a 403 would be a lie.
  EXPECT_EQ(httpStatusFor(403, QNetworkReply::AuthenticationRequiredError), 403);
  EXPECT_EQ(httpStatusFor(200, QNetworkReply::NoError), 200);
  // Anything else with no status stays "never reached the server".
  EXPECT_EQ(httpStatusFor(0, QNetworkReply::HostNotFoundError), 0);
  EXPECT_EQ(httpStatusFor(0, QNetworkReply::NoError), 0);
}

TEST(ReplyError, AnUnauthorizedRequestNeverShowsQtsWords) {
  // What the user actually saw: "Jira connection failed: Host requires
  // authentication" — Qt's jargon, naming neither the tracker nor the fix.
  const QString qtAuthError = QStringLiteral("Host requires authentication");
  const int status = detail::httpStatusFor(0, QNetworkReply::AuthenticationRequiredError);
  const QString out = describeHttpError(status, {}, qtAuthError);
  EXPECT_EQ(out, QStringLiteral("HTTP 401 — unauthorized — check the token"));
  EXPECT_FALSE(out.contains(QStringLiteral("Host requires authentication")));
}

TEST(ReplyError, AProxyRejectionSaysSo) {
  EXPECT_EQ(
      describeHttpError(
          detail::httpStatusFor(0, QNetworkReply::ProxyAuthenticationRequiredError), {}, QStringLiteral("Proxy requires authentication")),
      QStringLiteral("HTTP 407 — the network proxy rejected the request"));
}

// ── Keychain blob limit (SecretStore::chunkValue) ───────────────────────────
// Windows Credential Manager caps a blob at 2560 bytes; an Atlassian access
// token is a JWT that can be longer, so big values are stored in parts.

TEST(SecretStoreChunks, ShortValuesStayWhole) {
  EXPECT_EQ(SecretStore::chunkValue(QStringLiteral("ghp_short"), 2000), QStringList{QStringLiteral("ghp_short")});
  EXPECT_EQ(SecretStore::chunkValue(QString(2000, QLatin1Char('a')), 2000).size(), 1);
}

TEST(SecretStoreChunks, LongValuesSplitAndRejoinLosslessly) {
  QString jwt;
  for(int i = 0; i < 5000; ++i) {
    jwt.append(QChar(QLatin1Char(static_cast<char>('a' + (i % 26)))));
  }
  const QStringList parts = SecretStore::chunkValue(jwt, 2000);
  EXPECT_EQ(parts.size(), 3);
  for(const QString& part : parts) {
    EXPECT_LE(part.toUtf8().size(), 2000);
  }
  EXPECT_EQ(parts.join(QString()), jwt);
}

TEST(SecretStoreChunks, NonAsciiPartsStillFitTheByteLimit) {
  const QString value(1500, QChar(0x044F));  // "я": 2 bytes each in UTF-8
  const QStringList parts = SecretStore::chunkValue(value, 2000);
  EXPECT_GT(parts.size(), 1);
  for(const QString& part : parts) {
    EXPECT_LE(part.toUtf8().size(), 2000);
  }
  EXPECT_EQ(parts.join(QString()), value);
}

// ── OAuth token refresh (OAuthManager) ──────────────────────────────────────
// A browser sign-in hands out a token that expires (GitLab: two hours). It was
// never refreshed, so syncing went quiet a couple of hours after connecting.

TEST(OAuthRefresh, ParsesATokenResponse) {
  const QDateTime now = QDateTime(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);
  const QByteArray body = R"({"access_token":"at-2","refresh_token":"rt-2","expires_in":7200,"token_type":"bearer"})";
  const OAuthResult r = parseTokenResponse(body, now);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.accessToken, QStringLiteral("at-2"));
  // Providers rotate the refresh token; storing the new one is what keeps the
  // session alive past the next expiry.
  EXPECT_EQ(r.refreshToken, QStringLiteral("rt-2"));
  EXPECT_EQ(r.expiresAt, now.addSecs(7200));
}

TEST(OAuthRefresh, ReportsTheProvidersError) {
  const OAuthResult r = parseTokenResponse(R"({"error":"invalid_grant","error_description":"The refresh token is invalid."})");
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, QStringLiteral("The refresh token is invalid."));
  EXPECT_FALSE(parseTokenResponse("not json").ok);
}

TEST(OAuthRefresh, NonExpiringTokenNeedsNothing) {
  const QDateTime now = QDateTime(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);
  // GitHub OAuth apps issue tokens with no expiry — no expires_in, nothing to
  // refresh, and refreshing anyway would burn the grant for no reason.
  EXPECT_FALSE(tokenNeedsRefresh(QDateTime(), now));
  EXPECT_FALSE(parseTokenResponse(R"({"access_token":"gho_x"})", now).expiresAt.isValid());
}

TEST(OAuthRefresh, RefreshesJustBeforeExpiry) {
  const QDateTime now = QDateTime(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);
  EXPECT_FALSE(tokenNeedsRefresh(now.addSecs(3600), now));
  EXPECT_TRUE(tokenNeedsRefresh(now.addSecs(30), now));   // expires mid-sync
  EXPECT_TRUE(tokenNeedsRefresh(now.addSecs(-60), now));  // already expired
}

// ─── Paging and backoff ──────────────────────────────────────────────
//
// Every tracker caps a list response. A pull that read only the first page
// silently lost everything past it: a repo with 140 open issues mirrored 100
// and said nothing. These cover the two halves — knowing where the next page
// is, and not giving up on a 429.

TEST(Paging, NextLinkIsTheOnlyRelationThatCounts) {
  // GitHub sends all four relations in one header, in no guaranteed order.
  const QByteArray gh = R"(<https://api.github.com/repos/o/r/issues?page=1>; rel="prev", )"
                        R"(<https://api.github.com/repos/o/r/issues?page=3>; rel="next", )"
                        R"(<https://api.github.com/repos/o/r/issues?page=9>; rel="last")";
  EXPECT_EQ(nextLinkFromHeader(gh), QStringLiteral("https://api.github.com/repos/o/r/issues?page=3"));

  // Unquoted, and with the relation before other parameters.
  EXPECT_EQ(nextLinkFromHeader(R"(<https://x/2>; rel=next; type="application/json")"), QStringLiteral("https://x/2"));
  // Several relations on one link.
  EXPECT_EQ(nextLinkFromHeader(R"(<https://x/2>; rel="prev next")"), QStringLiteral("https://x/2"));
  // The last page says only where it has been.
  EXPECT_TRUE(nextLinkFromHeader(R"(<https://x/1>; rel="first", <https://x/8>; rel="prev")").isEmpty());
  // A relation that merely starts with "next" is not next.
  EXPECT_TRUE(nextLinkFromHeader(R"(<https://x/2>; rel="nextish")").isEmpty());
  EXPECT_TRUE(nextLinkFromHeader({}).isEmpty());
}

TEST(Paging, RetryAfterReadsBothFormsAndIgnoresThePast) {
  const QDateTime now = QDateTime(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);
  EXPECT_EQ(retryAfterMs("30", now), 30000);
  // The HTTP-date form, two minutes out.
  EXPECT_EQ(retryAfterMs("Thu, 01 Jan 2026 12:02:00 GMT", now), 120000);
  // A date already gone by means "no wait", not a negative one.
  EXPECT_EQ(retryAfterMs("Thu, 01 Jan 2026 11:00:00 GMT", now), 0);
  EXPECT_EQ(retryAfterMs("0", now), 0);
  EXPECT_EQ(retryAfterMs("-5", now), 0);
  EXPECT_EQ(retryAfterMs("soon", now), 0);
  EXPECT_EQ(retryAfterMs({}, now), 0);
}

TEST(Paging, QueryParamReplacesRatherThanAppends) {
  const QUrl base(QStringLiteral("https://x/api/tasks?limit=100&state=all"));
  const QUrl once = withQueryParam(base, QStringLiteral("cursor"), QStringLiteral("abc"));
  EXPECT_TRUE(once.toString().contains(QStringLiteral("cursor=abc")));
  EXPECT_TRUE(once.toString().contains(QStringLiteral("limit=100")));
  // A second page must not stack `cursor=abc&cursor=def`.
  const QUrl twice = withQueryParam(once, QStringLiteral("cursor"), QStringLiteral("def"));
  EXPECT_TRUE(twice.toString().contains(QStringLiteral("cursor=def")));
  EXPECT_FALSE(twice.toString().contains(QStringLiteral("cursor=abc")));
}

TEST(Paging, EveryDescriptorsRecipeIsUsable) {
  // A half-filled recipe is worse than none: the walk would either stop at page
  // one (silently, the bug this fixes) or step a parameter the server ignores
  // and re-read page one forever. maxPages bounds the latter, but only after
  // 20 pointless requests.
  for(const ProviderDescriptor& d : providerCatalog()) {
    const std::string who = d.id.toStdString();
    EXPECT_GT(d.paging.maxPages, 0) << who;
    switch(d.paging.style) {
      case PageStyle::None:
        break;
      case PageStyle::LinkHeader:
        break;
      case PageStyle::BodyNext:
        EXPECT_FALSE(d.paging.bodyPath.isEmpty()) << who << " has nowhere to read the cursor from";
        break;
      case PageStyle::Offset:
        EXPECT_FALSE(d.paging.offsetParam.isEmpty()) << who << " has nothing to step";
        EXPECT_GT(d.paging.pageSize, 0) << who << " cannot tell a short page from a full one";
        break;
    }
    if(d.paging.style != PageStyle::None) {
      EXPECT_FALSE(d.bespoke) << who << " is bespoke; RestIssueProvider never runs its recipe";
    }
  }
}

TEST(Paging, AnOffsetWalkAgreesWithTheSizeItsTemplateAsks) {
  // `pageSize` has to equal the limit baked into the list template, or a full
  // page looks short and the walk stops one page early.
  static const QRegularExpression limitRx(QStringLiteral(R"([?&](?:limit|per_page|pagelen)=(\d+))"));
  for(const ProviderDescriptor& d : providerCatalog()) {
    if(d.paging.style != PageStyle::Offset) {
      continue;
    }
    const QRegularExpressionMatch m = limitRx.match(d.listPathTemplate);
    if(!m.hasMatch()) {
      continue;  // the endpoint has a fixed page size it does not let us name
    }
    EXPECT_EQ(m.captured(1).toInt(), d.paging.pageSize) << d.id.toStdString() << ": template and recipe disagree";
  }
}

TEST(Paging, TheForgesFollowTheLinkHeader) {
  // Not an implementation detail: GitHub, GitLab and Gitea all cap at 100 and
  // all say "there is more" the same way. If one of these loses its recipe,
  // that provider goes back to silently truncating.
  for(const char* id : {"github", "gitlab", "gitea", "forgejo", "sentry"}) {
    const ProviderDescriptor* d = findDescriptor(QString::fromLatin1(id));
    ASSERT_NE(d, nullptr) << id;
    EXPECT_EQ(d->paging.style, PageStyle::LinkHeader) << id;
  }
}

// ─── The walk itself, over a socket ──────────────────────────────────
//
// Driven with hand-built descriptors rather than real ones: what is under test
// is the engine, and a synthetic "{host}" descriptor can point at the fake
// server without any provider's hardcoded API host getting in the way.

namespace {

ProviderDescriptor pagingDesc() {
  ProviderDescriptor d;
  d.id = QStringLiteral("fake");
  d.displayName = QStringLiteral("Fake");
  d.baseUrlTemplate = QStringLiteral("{host}");
  d.requiredKeys = {QStringLiteral("host")};
  d.listPathTemplate = QStringLiteral("/issues?limit=2");
  d.fields.id = QStringLiteral("id");
  d.fields.title = QStringLiteral("title");
  // Retries are real waits; a unit test should not spend three seconds proving
  // that it waited.
  d.retry.baseDelayMs = 1;
  d.retry.maxDelayMs = 5;
  return d;
}

// Two issues, ids derived from the page so a test can tell the pages apart.
QByteArray pageBody(int page) {
  const QByteArray n = QByteArray::number(page);
  return "[{\"id\":\"" + n + "a\",\"title\":\"first\"},{\"id\":\"" + n + "b\",\"title\":\"second\"}]";
}

QByteArray nextLink(const QString& base, const QByteArray& pathAndQuery) {
  return "<" + base.toUtf8() + pathAndQuery + ">; rel=\"next\"";
}

struct PullWatcher {
  QVector<ExternalTask> tasks;
  int fetched = 0;
  int failed = 0;
  int lastStatus = -1;
  QString lastError;

  explicit PullWatcher(IntegrationProvider* p) {
    QObject::connect(p, &IntegrationProvider::tasksFetched, p, [this](const QVector<ExternalTask>& t) {
      tasks = t;
      ++fetched;
    });
    QObject::connect(p, &IntegrationProvider::pullFailed, p, [this](int status, const QString& error) {
      lastStatus = status;
      lastError = error;
      ++failed;
    });
  }
};

}  // namespace

// QNetworkAccessManager needs an application object, and gtest_main does not
// make one. It outlives the suite on purpose.
class PagingWalk : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    if(QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "heap_rest_providers_tests";
      static char* argv[] = {arg0, nullptr};
      new QCoreApplication(argc, argv);
    }
  }
};

TEST_F(PagingWalk, FollowsTheLinkHeaderToTheLastPage) {
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  d.paging.style = PageStyle::LinkHeader;

  srv.route("GET /issues?limit=2", {200, pageBody(1), {{"Link", nextLink(srv.base(), "/issues?limit=2&page=2")}}});
  srv.route("GET /issues?limit=2&page=2", {200, pageBody(2), {}});

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));

  // One signal for the whole walk — AppController toasts per tasksFetched, and
  // a per-page toast would be four toasts for a big repo.
  EXPECT_EQ(w.fetched, 1);
  EXPECT_EQ(w.failed, 0);
  ASSERT_EQ(w.tasks.size(), 4);
  EXPECT_EQ(w.tasks.at(0).externalId, QStringLiteral("1a"));
  EXPECT_EQ(w.tasks.at(3).externalId, QStringLiteral("2b"));
  EXPECT_EQ(srv.seen().size(), 2);
}

TEST_F(PagingWalk, ANextLinkToAnotherHostIsNotFollowed) {
  // The link is server-supplied and the request carries a token. Following it
  // off-origin is the same mistake SameOriginRedirectPolicy exists to prevent.
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  d.paging.style = PageStyle::LinkHeader;
  srv.route("GET /issues?limit=2", {200, pageBody(1), {{"Link", "<https://evil.example/issues>; rel=\"next\""}}});

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));

  EXPECT_EQ(w.tasks.size(), 2) << "the walk must end rather than leave the origin";
  EXPECT_EQ(srv.seen().size(), 1);
  EXPECT_EQ(w.failed, 0);
}

TEST_F(PagingWalk, StopsAtTheCapAndSaysSo) {
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  d.paging.style = PageStyle::LinkHeader;
  d.paging.maxPages = 3;
  // A server that always claims another page — a bug upstream, or a filter that
  // never narrows. Without the cap this is an infinite pull.
  srv.route("GET /issues?limit=2", {200, pageBody(1), {{"Link", nextLink(srv.base(), "/issues?limit=2")}}});

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));

  EXPECT_EQ(srv.seen().size(), 3);
  EXPECT_EQ(w.tasks.size(), 6);
  // Bounded, but not silently: hitting the cap means issues were left behind.
  EXPECT_EQ(w.failed, 1);
  EXPECT_EQ(w.lastStatus, 0);
  EXPECT_TRUE(w.lastError.contains(QStringLiteral("3-page limit"))) << w.lastError.toStdString();
}

TEST_F(PagingWalk, A429IsWaitedOutAndThePageRetried) {
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  srv.routeSequence("GET /issues?limit=2", {{429, "{}", {{"Retry-After", "0"}}}, {200, pageBody(1), {}}});

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));

  EXPECT_EQ(srv.seen().size(), 2) << "the same page, asked for twice";
  EXPECT_EQ(w.tasks.size(), 2);
  EXPECT_EQ(w.failed, 0) << "a rate limit that clears is not an error the user needs to see";
}

TEST_F(PagingWalk, APermanentErrorIsNotRetried) {
  // 404 and 401 will answer identically however long we wait; retrying only
  // delays the toast that tells the user their config is wrong.
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  srv.route("GET /issues?limit=2", {404, "{\"message\":\"Not Found\"}", {}});

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.failed > 0;
  }));

  EXPECT_EQ(srv.seen().size(), 1);
  EXPECT_EQ(w.fetched, 0) << "nothing was fetched, so nothing should be merged";
  EXPECT_EQ(w.lastStatus, 404);
}

TEST_F(PagingWalk, AFailureMidWalkKeepsThePagesAlreadyRead) {
  // Throwing away 100 real issues because page 2 timed out is a worse answer
  // than a short one — but a short one has to say it is short.
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  d.paging.style = PageStyle::LinkHeader;
  d.retry.maxRetries = 1;
  srv.route("GET /issues?limit=2", {200, pageBody(1), {{"Link", nextLink(srv.base(), "/issues?limit=2&page=2")}}});
  srv.route("GET /issues?limit=2&page=2", {503, "{}", {}});

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.failed > 0;
  }));

  EXPECT_EQ(w.fetched, 1);
  EXPECT_EQ(w.tasks.size(), 2) << "page one survives";
  // Status 0 on purpose: a mid-walk 401 must not send AppController into its
  // refresh-and-resync path, which would re-pull page one forever.
  EXPECT_EQ(w.lastStatus, 0);
  EXPECT_TRUE(w.lastError.contains(QStringLiteral("only 1 page"))) << w.lastError.toStdString();
  // 5xx is worth one retry before giving up on the page.
  EXPECT_EQ(srv.seen().size(), 3);
}

TEST_F(PagingWalk, ABodyCursorGoesBackAsAQueryParameter) {
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  d.listPathTemplate = QStringLiteral("/tasks?limit=2");
  d.fields.arrayPointer = QStringLiteral("results");
  d.paging.style = PageStyle::BodyNext;
  d.paging.bodyPath = QStringLiteral("next_cursor");
  d.paging.cursorParam = QStringLiteral("cursor");

  srv.route("GET /tasks?limit=2", {200, "{\"results\":[{\"id\":\"1\",\"title\":\"a\"}],\"next_cursor\":\"CUR2\"}", {}});
  srv.route("GET /tasks?limit=2&cursor=CUR2", {200, "{\"results\":[{\"id\":\"2\",\"title\":\"b\"}],\"next_cursor\":null}", {}});

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));

  ASSERT_EQ(w.tasks.size(), 2);
  EXPECT_EQ(srv.lastRequest("GET /tasks").query, QByteArray("limit=2&cursor=CUR2"));
}

TEST_F(PagingWalk, AnOffsetWalkStopsOnAShortPage) {
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  d.paging.style = PageStyle::Offset;
  d.paging.offsetParam = QStringLiteral("offset");
  d.paging.pageSize = 2;

  srv.route("GET /issues?limit=2", {200, pageBody(1), {}});                                      // full → keep going
  srv.route("GET /issues?limit=2&offset=2", {200, "[{\"id\":\"3a\",\"title\":\"last\"}]", {}});  // short → stop

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));

  EXPECT_EQ(w.tasks.size(), 3);
  EXPECT_EQ(srv.seen().size(), 2) << "a short page ends the walk; an endpoint that ignores offset stops after one";
}

TEST_F(PagingWalk, ASecondPullWhileOneIsInFlightIsIgnored) {
  // The auto-sync timer and the Sync-now button can land together. Two walks
  // sharing one accumulator would interleave their pages and emit twice.
  heap::testing::FakeHttpServer srv;
  ProviderDescriptor d = pagingDesc();
  srv.route("GET /issues?limit=2", {200, pageBody(1), {}});

  RestIssueProvider p(d);
  p.setConfig({{QStringLiteral("host"), srv.base()}});
  PullWatcher w(&p);
  p.pullTasks();
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));
  // Give a stray second walk a chance to show up before declaring it absent.
  heap::testing::waitUntil(
      [&] {
        return w.fetched > 1;
      },
      300);

  EXPECT_EQ(w.fetched, 1);
  EXPECT_EQ(srv.seen().size(), 1);
}

// A body that only repeats the reason phrase gets an explanation (audit B5).
TEST(ReplyError, DescribeHttpError_BareNotFound_ExplainsAccess) {
  EXPECT_EQ(describeHttpError(404, R"({"message":"Not Found"})", QString()),
            QStringLiteral("HTTP 404 — Not Found: the repo or project does not exist, or this token has no access to it"));
  EXPECT_EQ(describeHttpError(404, R"({"message":"404 Project Not Found"})", QString()),
            QStringLiteral("HTTP 404 — 404 Project Not Found: the repo or project does not exist, or this token has no access to it"));
}

TEST(ReplyError, DescribeHttpError_SpecificMessage_IsLeftAlone) {
  EXPECT_EQ(describeHttpError(401, R"({"message":"Bad credentials"})", QString()), QStringLiteral("HTTP 401 — Bad credentials"));
}

// ─── Merge / pull requests (APP-242): read-only, by role ─────────────

TEST(ClosingReferences, ReadsTheUsualPhrasings) {
  EXPECT_EQ(closingReferences(QStringLiteral("Closes #12")), QStringList{QStringLiteral("#12")});
  EXPECT_EQ(closingReferences(QStringLiteral("fixes acme/web#7, #8 and #9.\nResolves: #8")),
            (QStringList{QStringLiteral("acme/web#7"), QStringLiteral("#8"), QStringLiteral("#9")}));
  EXPECT_TRUE(closingReferences(QStringLiteral("see #12; prefixes #3")).isEmpty());
  EXPECT_TRUE(closingReferences(QString()).isEmpty());
}

// APP-293: the Jira issue a review is about — branch, then title, then a
// closing phrase or a /browse/ link in the description.
TEST(LinkedIssueKey, BranchThenTitleThenBody) {
  const QSet<QString> known{QStringLiteral("PROJ-12"), QStringLiteral("PROJ-7"), QStringLiteral("OPS-3")};
  EXPECT_EQ(linkedIssueKey(QStringLiteral("feature/proj-12-login"), QStringLiteral("OPS-3 tweak"), QString(), known),
            QStringLiteral("PROJ-12"));
  EXPECT_EQ(linkedIssueKey(QStringLiteral("fix-proj-7"), QString(), QString(), known), QStringLiteral("PROJ-7"));
  EXPECT_EQ(linkedIssueKey(QStringLiteral("main"), QStringLiteral("[OPS-3] Rotate keys"), QString(), known), QStringLiteral("OPS-3"));
  EXPECT_EQ(linkedIssueKey(QString(), QStringLiteral("Login"), QStringLiteral("Closes PROJ-7"), known), QStringLiteral("PROJ-7"));
  EXPECT_EQ(linkedIssueKey(QString(), QString(), QStringLiteral("https://acme.atlassian.net/browse/OPS-3"), known),
            QStringLiteral("OPS-3"));
}

TEST(LinkedIssueKey, NoMatchWithoutAKnownKey) {
  const QSet<QString> known{QStringLiteral("PROJ-12")};
  // Not a known issue, a longer number, a word's tail, a passing mention.
  EXPECT_TRUE(linkedIssueKey(QStringLiteral("feature/proj-99"), QString(), QString(), known).isEmpty());
  EXPECT_TRUE(linkedIssueKey(QStringLiteral("proj-123"), QString(), QString(), known).isEmpty());
  EXPECT_TRUE(linkedIssueKey(QString(), QStringLiteral("XPROJ-12"), QString(), known).isEmpty());
  EXPECT_TRUE(linkedIssueKey(QString(), QString(), QStringLiteral("similar to PROJ-12"), known).isEmpty());
  EXPECT_TRUE(linkedIssueKey(QStringLiteral("proj-12"), QString(), QString(), {}).isEmpty());
}

namespace {

// The real GitLab / GitHub descriptors, pointed at the fake server.
ProviderDescriptor forgeAt(const QString& id) {
  ProviderDescriptor d = *findDescriptor(id);
  d.baseUrlTemplate = QStringLiteral("{host}");
  d.retry.baseDelayMs = 1;
  d.retry.maxDelayMs = 5;
  return d;
}

int writes(const heap::testing::FakeHttpServer& srv) {
  int n = 0;
  for(const auto& r : srv.requests()) {
    if(r.method != "GET") {
      ++n;
    }
  }
  return n;
}

QList<QByteArray> queriesOf(const heap::testing::FakeHttpServer& srv, const QByteArray& path) {
  QList<QByteArray> out;
  for(const auto& r : srv.requests()) {
    if(r.path == path) {
      out.append(QByteArray::fromPercentEncoding(r.query));
    }
  }
  return out;
}

}  // namespace

TEST_F(PagingWalk, GitlabPullsMergeRequestsAssignedAndToReviewByDefault) {
  heap::testing::FakeHttpServer srv;
  srv.route("GET /api/v4/issues", {200, R"([{"iid":17,"title":"issue","state":"opened","web_url":"https://g/acme/app/-/issues/17"}])", {}});
  srv.route("GET /api/v4/user", {200, R"({"username":"me"})", {}});
  srv.route(
      "GET /api/v4/merge_requests",
      {200,
       R"([{"iid":17,"title":"mr","state":"opened","web_url":"https://g/acme/app/-/merge_requests/17","references":{"full":"acme/app!17"}}])",
       {}});

  RestIssueProvider p(forgeAt(QStringLiteral("gitlab")));
  p.setConfig({{QStringLiteral("host"), srv.base()}, {QStringLiteral("token"), QStringLiteral("tok")}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));
  EXPECT_EQ(w.failed, 0) << w.lastError.toStdString();
  // The issue and the merge request with the same number are two items, and
  // the same request from four lists is one.
  ASSERT_EQ(w.tasks.size(), 2);
  EXPECT_EQ(w.tasks.at(0).externalId, QStringLiteral("17"));
  EXPECT_EQ(w.tasks.at(1).externalId, QStringLiteral("!17"));
  EXPECT_TRUE(w.tasks.at(1).crossProject);
  EXPECT_TRUE(p.lastPullComplete());

  const QList<QByteArray> asked = queriesOf(srv, "/api/v4/merge_requests");
  ASSERT_EQ(asked.size(), 4) << "assignee and reviewer, open and lately updated";
  int assigned = 0;
  int reviewing = 0;
  for(const QByteArray& q : asked) {
    assigned += q.contains("scope=assigned_to_me") ? 1 : 0;
    reviewing += q.contains("reviewer_username=me") ? 1 : 0;
    EXPECT_FALSE(q.contains("created_by_me")) << q.toStdString();
    EXPECT_TRUE(q.contains("state=opened") || q.contains("updated_after=")) << q.toStdString();
  }
  EXPECT_EQ(assigned, 2);
  EXPECT_EQ(reviewing, 2);
  EXPECT_EQ(writes(srv), 0);
}

TEST_F(PagingWalk, GitlabScopedToAProjectAsksThatProject_AndRolesCanBeTurnedOff) {
  heap::testing::FakeHttpServer srv;
  srv.route("GET /api/v4/projects/acme%2Fapp/issues", {200, "[]", {}});
  srv.route("GET /api/v4/projects/acme%2Fapp/merge_requests", {200, "[]", {}});
  {
    RestIssueProvider p(forgeAt(QStringLiteral("gitlab")));
    p.setConfig({{QStringLiteral("host"), srv.base()},
                 {QStringLiteral("token"), QStringLiteral("tok")},
                 {QStringLiteral("projectId"), QStringLiteral("acme/app")},
                 {QStringLiteral("mrRoles"), QStringLiteral("author")}});
    PullWatcher w(&p);
    p.pullTasks();
    ASSERT_TRUE(heap::testing::waitUntil([&] {
      return w.fetched > 0;
    }));
    const QList<QByteArray> asked = queriesOf(srv, "/api/v4/projects/acme%2Fapp/merge_requests");
    ASSERT_EQ(asked.size(), 2);
    for(const QByteArray& q : asked) {
      EXPECT_TRUE(q.contains("scope=created_by_me")) << q.toStdString();
    }
    EXPECT_FALSE(srv.seen().contains("GET /api/v4/user")) << "no reviewer list, no need to know who I am";
  }
  const qsizetype before = srv.requests().size();
  RestIssueProvider off(forgeAt(QStringLiteral("gitlab")));
  off.setConfig({{QStringLiteral("host"), srv.base()},
                 {QStringLiteral("token"), QStringLiteral("tok")},
                 {QStringLiteral("projectId"), QStringLiteral("acme/app")},
                 {QStringLiteral("mrRoles"), QString()}});
  PullWatcher w(&off);
  off.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));
  EXPECT_EQ(srv.requests().size(), before + 1) << "the issues only";
}

TEST_F(PagingWalk, AFailingMergeRequestListCostsOnlyItsItems) {
  heap::testing::FakeHttpServer srv;
  srv.route("GET /api/v4/issues", {200, R"([{"iid":1,"title":"issue","state":"opened"}])", {}});
  // No /api/v4/user: the reviewer lists cannot be built; the assignee lists 403.
  srv.route("GET /api/v4/merge_requests", {403, R"({"message":"403 Forbidden"})", {}});
  RestIssueProvider p(forgeAt(QStringLiteral("gitlab")));
  p.setConfig({{QStringLiteral("host"), srv.base()}, {QStringLiteral("token"), QStringLiteral("tok")}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0 && w.failed > 0;
  }));
  ASSERT_EQ(w.tasks.size(), 1);
  EXPECT_EQ(w.lastStatus, 0) << "not a sign-in problem";
  EXPECT_FALSE(p.lastPullComplete()) << "a partial pull must not mark cards gone";
}

TEST_F(PagingWalk, GithubPullRequestsOnlyWhenSwitchedOn) {
  heap::testing::FakeHttpServer srv;
  srv.route("GET /issues", {200, "[]", {}});
  srv.route(
      "GET /search/issues",
      {200,
       R"({"items":[{"number":7,"html_url":"https://github.com/o/r/pull/7","title":"pr","state":"open","pull_request":{},"repository_url":"https://api.github.com/repos/o/r"}]})",
       {}});
  {
    RestIssueProvider p(forgeAt(QStringLiteral("github")));
    p.setConfig({{QStringLiteral("host"), srv.base()}, {QStringLiteral("token"), QStringLiteral("tok")}});
    PullWatcher w(&p);
    p.pullTasks();
    ASSERT_TRUE(heap::testing::waitUntil([&] {
      return w.fetched > 0;
    }));
    EXPECT_TRUE(w.tasks.isEmpty());
    EXPECT_FALSE(srv.seen().contains("GET /search/issues")) << "off by default: nothing changes for those who did not turn it on";
  }
  RestIssueProvider p(forgeAt(QStringLiteral("github")));
  p.setConfig({{QStringLiteral("host"), srv.base()},
               {QStringLiteral("token"), QStringLiteral("tok")},
               {QStringLiteral("pullRequests"), QStringLiteral("true")}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));
  ASSERT_EQ(w.tasks.size(), 1);
  EXPECT_EQ(w.tasks.at(0).status, QStringLiteral("PR open"));
  const QList<QByteArray> asked = queriesOf(srv, "/search/issues");
  EXPECT_EQ(asked.size(), 6) << "assignee, review-requested and reviewed-by; open and lately closed";
  bool requested = false;
  for(const QByteArray& q : asked) {
    EXPECT_TRUE(q.contains("is:pr")) << q.toStdString();
    EXPECT_FALSE(q.contains("author:@me")) << q.toStdString();
    requested = requested || q.contains("review-requested:@me");
  }
  EXPECT_TRUE(requested);
  EXPECT_EQ(writes(srv), 0);
}

TEST_F(PagingWalk, AMergeRequestIsNeverWrittenBack) {
  heap::testing::FakeHttpServer srv;
  RestIssueProvider p(forgeAt(QStringLiteral("gitlab")));
  p.setConfig({{QStringLiteral("host"), srv.base()},
               {QStringLiteral("token"), QStringLiteral("tok")},
               {QStringLiteral("projectId"), QStringLiteral("42")}});
  bool answered = false;
  bool ok = true;
  QObject::connect(
      &p, &IntegrationProvider::taskPushed, &p, [&](const QString&, const QString&, bool success, const QString&, const QString&) {
        answered = true;
        ok = success;
      });
  p.pushStatusChange(QStringLiteral("!17"), QStringLiteral("done"), QStringLiteral("acme/app"));
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return answered;
  }));
  EXPECT_FALSE(ok);
  EXPECT_TRUE(srv.requests().isEmpty());
}

TEST_F(PagingWalk, MergeRequestNotesComeFromTheMergeRequest) {
  heap::testing::FakeHttpServer srv;
  srv.route("GET /api/v4/projects/acme%2Fapp/merge_requests/17/notes",
            {200, R"([{"id":1,"body":"looks good","author":{"username":"bob"}}])", {}});
  RestIssueProvider p(forgeAt(QStringLiteral("gitlab")));
  p.setConfig({{QStringLiteral("host"), srv.base()}, {QStringLiteral("token"), QStringLiteral("tok")}});
  int got = -1;
  QObject::connect(&p, &IntegrationProvider::commentsFetched, &p, [&](const QString&, const QVector<ExternalComment>& c, const QString&) {
    got = static_cast<int>(c.size());
  });
  p.fetchComments(QStringLiteral("!17"), QStringLiteral("acme/app"));
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return got >= 0;
  }));
  EXPECT_EQ(got, 1);
}

// ── CI of my merge / pull requests (APP-318) ──

TEST(CiStatus, OneWordForAPipeline) {
  EXPECT_EQ(heap::ci::gitlabState(QStringLiteral("success")), QStringLiteral("success"));
  EXPECT_EQ(heap::ci::gitlabState(QStringLiteral("failed")), QStringLiteral("failed"));
  EXPECT_EQ(heap::ci::gitlabState(QStringLiteral("pending")), QStringLiteral("running"));
  EXPECT_EQ(heap::ci::gitlabState(QStringLiteral("manual")), QString());
  const QJsonArray ok =
      QJsonDocument::fromJson(R"([{"status":"completed","conclusion":"success"},{"status":"completed","conclusion":"skipped"}])").array();
  const QJsonArray red = QJsonDocument::fromJson(R"([{"status":"in_progress"},{"status":"completed","conclusion":"failure"}])").array();
  const QJsonArray going = QJsonDocument::fromJson(R"([{"status":"queued"},{"status":"completed","conclusion":"success"}])").array();
  EXPECT_EQ(heap::ci::githubState(ok, QString()), QStringLiteral("success"));
  EXPECT_EQ(heap::ci::githubState(red, QString()), QStringLiteral("failed")) << "a failure wins over one still going";
  EXPECT_EQ(heap::ci::githubState(going, QString()), QStringLiteral("running"));
  EXPECT_EQ(heap::ci::githubState({}, QStringLiteral("failure")), QStringLiteral("failed")) << "a CI that posts statuses";
  EXPECT_EQ(heap::ci::githubState({}, QString()), QString());
  EXPECT_TRUE(heap::ci::turnedRed(QStringLiteral("success"), QStringLiteral("failed")));
  EXPECT_TRUE(heap::ci::turnedRed(QStringLiteral("running"), QStringLiteral("failed")));
  EXPECT_FALSE(heap::ci::turnedRed(QString(), QStringLiteral("failed"))) << "a first sync is not news";
  EXPECT_FALSE(heap::ci::turnedRed(QStringLiteral("failed"), QStringLiteral("failed")));
}

TEST(CiStatus, TheLogTailIsCleanText) {
  QByteArray raw;
  for(int i = 0; i < 500; ++i) {
    raw += "line " + QByteArray::number(i) + "\n";
  }
  raw += "section_start:1700000000:build_script\r\x1b[0K\x1b[31;1mERROR: tests failed\x1b[0m\n";
  raw += "progress 10%\rprogress 100%\n";
  raw += "section_end:1700000001:build_script\r\x1b[0K\n\n\n";
  const QString tail = heap::ci::logTail(raw);
  const QStringList lines = tail.split(QLatin1Char('\n'));
  EXPECT_EQ(lines.size(), heap::ci::kLogTailLines);
  EXPECT_FALSE(tail.contains(QChar(0x1b))) << "colours out";
  EXPECT_FALSE(tail.contains(QStringLiteral("section_start"))) << "GitLab's markers out";
  EXPECT_TRUE(tail.contains(QStringLiteral("ERROR: tests failed")));
  EXPECT_EQ(lines.last(), QStringLiteral("progress 100%")) << "a progress line at its last state, no trailing blanks";
  EXPECT_TRUE(heap::ci::isErrorLine(QStringLiteral("ERROR: tests failed")));
  EXPECT_FALSE(heap::ci::isErrorLine(QStringLiteral("line 3")));
}

TEST_F(PagingWalk, GitlabOpenMergeRequestsCarryTheirPipeline) {
  heap::testing::FakeHttpServer srv;
  srv.route("GET /api/v4/issues", {200, "[]", {}});
  srv.route("GET /api/v4/user", {200, R"({"username":"me"})", {}});
  srv.route(
      "GET /api/v4/merge_requests",
      {200,
       R"([{"iid":17,"title":"mr","state":"opened","web_url":"https://g/acme/app/-/merge_requests/17","references":{"full":"acme/app!17"},"author":{"username":"Me"}},
                 {"iid":18,"title":"old","state":"merged","web_url":"https://g/acme/app/-/merge_requests/18","references":{"full":"acme/app!18"},"author":{"username":"bob"}}])",
       {}});
  srv.route(
      "GET /api/v4/projects/acme%2Fapp/merge_requests/17",
      {200,
       R"({"iid":17,"sha":"head","head_pipeline":{"id":901,"sha":"abc","status":"failed","web_url":"https://g/acme/app/-/pipelines/901"}})",
       {}});

  RestIssueProvider p(forgeAt(QStringLiteral("gitlab")));
  p.setConfig({{QStringLiteral("host"), srv.base()}, {QStringLiteral("token"), QStringLiteral("tok")}});
  PullWatcher w(&p);
  p.pullTasks();
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return w.fetched > 0;
  }));
  ASSERT_EQ(w.tasks.size(), 2);
  const QJsonObject open = w.tasks.at(0).details;
  EXPECT_EQ(open.value(QStringLiteral("pipeline")).toString(), QStringLiteral("failed"));
  EXPECT_EQ(open.value(QStringLiteral("ciRef")).toString(), QStringLiteral("901"));
  EXPECT_EQ(open.value(QStringLiteral("pipelineUrl")).toString(), QStringLiteral("https://g/acme/app/-/pipelines/901"));
  // The commit the pipeline ran on, not the head (it can lag): what tells a
  // new run from the same one for a CI notification (dogfood 2026-10-20).
  EXPECT_EQ(open.value(QStringLiteral("ciSha")).toString(), QStringLiteral("abc"));
  // Mine (I opened it) against one I only review: only mine notify.
  EXPECT_TRUE(open.value(QStringLiteral("mine")).toBool(false));
  EXPECT_FALSE(w.tasks.at(1).details.value(QStringLiteral("mine")).toBool(true));
  EXPECT_FALSE(srv.seen().contains(QByteArray("GET /api/v4/projects/acme%2Fapp/merge_requests/18"))) << "a merged one is not asked";
  EXPECT_EQ(writes(srv), 0) << "CI is read only";
}

TEST_F(PagingWalk, GitlabFailedJobsAndALogTail) {
  heap::testing::FakeHttpServer srv;
  srv.route("GET /api/v4/projects/acme%2Fapp/pipelines/901/jobs",
            {200,
             R"([{"id":55,"name":"test","stage":"test","status":"failed","duration":61.2,"web_url":"https://g/j/55"},
                 {"id":56,"name":"lint","stage":"test","status":"success"}])",
             {}});
  srv.route("GET /api/v4/projects/acme%2Fapp/jobs/55/trace", {200, "ok\n\x1b[31mFAILED: 1 test\x1b[0m\n", {}});
  srv.route("GET /api/v4/projects/acme%2Fapp/jobs/57/trace", {403, R"({"message":"403 Forbidden"})", {}});
  RestIssueProvider p(forgeAt(QStringLiteral("gitlab")));
  p.setConfig({{QStringLiteral("host"), srv.base()}, {QStringLiteral("token"), QStringLiteral("tok")}});
  QVariantList jobs;
  bool gotJobs = false;
  QObject::connect(&p, &IntegrationProvider::ciJobsFetched, &p, [&](const QString&, const QVariantList& j, const QString&) {
    jobs = j;
    gotJobs = true;
  });
  p.fetchCiJobs(QStringLiteral("!17"), QStringLiteral("acme/app"), QStringLiteral("901"));
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return gotJobs;
  }));
  ASSERT_EQ(jobs.size(), 1);
  EXPECT_EQ(jobs.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("test"));
  QString text;
  QString error;
  int logs = 0;
  QObject::connect(&p, &IntegrationProvider::ciLogFetched, &p, [&](const QString&, const QString&, const QString& t, const QString& e) {
    text = t;
    error = e;
    ++logs;
  });
  p.fetchCiLog(QStringLiteral("!17"), QStringLiteral("acme/app"), QStringLiteral("55"));
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return logs == 1;
  }));
  EXPECT_EQ(text, QStringLiteral("ok\nFAILED: 1 test"));
  p.fetchCiLog(QStringLiteral("!17"), QStringLiteral("acme/app"), QStringLiteral("57"));
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return logs == 2;
  }));
  EXPECT_TRUE(text.isEmpty());
  EXPECT_FALSE(error.isEmpty()) << "no rights to the log: an error, said quietly by the UI";
}

TEST_F(PagingWalk, GithubPullRequestChecksFromTheHeadCommit) {
  heap::testing::FakeHttpServer srv;
  srv.route("GET /repos/acme/app/pulls/5", {200, R"({"number":5,"head":{"sha":"abc123"}})", {}});
  srv.route("GET /repos/acme/app/commits/abc123/check-runs",
            {200,
             R"({"check_runs":[{"id":77,"name":"build","status":"completed","conclusion":"failure","app":{"slug":"github-actions"},
                 "html_url":"https://github.com/acme/app/runs/77","output":{"summary":"2 tests failed"}},
                {"id":78,"name":"ci/jenkins","status":"completed","conclusion":"failure","app":{"slug":"jenkins"}}]})",
             {}});
  srv.route("GET /repos/acme/app/commits/abc123/status", {200, R"({"state":"pending","statuses":[]})", {}});
  srv.route("GET /repos/acme/app/actions/jobs/77/logs", {200, "2026-10-16T10:00:00Z ##[error]Process completed with exit code 1.\n", {}});
  RestIssueProvider p(forgeAt(QStringLiteral("github")));
  p.setConfig({{QStringLiteral("host"), srv.base()}, {QStringLiteral("token"), QStringLiteral("tok")}});
  QVariantList jobs;
  bool gotJobs = false;
  QObject::connect(&p, &IntegrationProvider::ciJobsFetched, &p, [&](const QString&, const QVariantList& j, const QString&) {
    jobs = j;
    gotJobs = true;
  });
  p.fetchCiJobs(QStringLiteral("5"), QStringLiteral("acme/app"), QStringLiteral("abc123"));
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return gotJobs;
  }));
  ASSERT_EQ(jobs.size(), 2);
  EXPECT_TRUE(jobs.at(0).toMap().value(QStringLiteral("hasLog")).toBool());
  EXPECT_EQ(jobs.at(0).toMap().value(QStringLiteral("summary")).toString(), QStringLiteral("2 tests failed"));
  EXPECT_FALSE(jobs.at(1).toMap().value(QStringLiteral("hasLog")).toBool()) << "another CI's check has no log here";
  QString text;
  bool gotLog = false;
  QObject::connect(&p, &IntegrationProvider::ciLogFetched, &p, [&](const QString&, const QString&, const QString& t, const QString&) {
    text = t;
    gotLog = true;
  });
  p.fetchCiLog(QStringLiteral("5"), QStringLiteral("acme/app"), QStringLiteral("77"));
  ASSERT_TRUE(heap::testing::waitUntil([&] {
    return gotLog;
  }));
  EXPECT_TRUE(text.contains(QStringLiteral("exit code 1")));
}

TEST(StatusMapReview, MergeAndPullRequestsHaveTheirOwnColumns) {
  using heap::integrations::StatusMap;
  EXPECT_EQ(StatusMap::column(QStringLiteral("MR open"), {}, QStringLiteral("todo")), QStringLiteral("review"));
  EXPECT_EQ(StatusMap::column(QStringLiteral("MR draft"), {}, QStringLiteral("todo")), QStringLiteral("prog"));
  EXPECT_EQ(StatusMap::column(QStringLiteral("MR merged"), {}, QStringLiteral("todo")), QStringLiteral("done"));
  EXPECT_EQ(StatusMap::column(QStringLiteral("PR closed"), {}, QStringLiteral("todo")), QStringLiteral("done"));
  // An open GitLab issue is To Do, not a guess.
  EXPECT_EQ(StatusMap::defaultColumn(QStringLiteral("opened")), QStringLiteral("todo"));
}
